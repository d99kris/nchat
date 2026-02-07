// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2023 Tulir Asokan
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package signalfmt

import (
	"context"
	"html"
	"slices"
	"strings"

	"github.com/google/uuid"
	"golang.org/x/exp/maps"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

type UserInfo struct {
	MXID id.UserID
	Name string
}

type FormatParams struct {
	GetUserInfo func(ctx context.Context, uuid uuid.UUID) UserInfo
}

type formatContext struct {
	IsInCodeblock bool
}

func (ctx formatContext) TextToHTML(text string) string {
	if ctx.IsInCodeblock {
		return html.EscapeString(text)
	}
	return event.TextToHTML(text)
}

func Parse(ctx context.Context, message string, ranges []*signalpb.BodyRange, params *FormatParams) *event.MessageEventContent {
	content := &event.MessageEventContent{
		MsgType:  event.MsgText,
		Body:     message,
		Mentions: &event.Mentions{},
	}
	if len(ranges) == 0 {
		return content
	}
	// LinkedRangeTree.Add depends on the ranges being sorted by increasing start index and then decreasing length.
	slices.SortFunc(ranges, func(a, b *signalpb.BodyRange) int {
		if *a.Start == *b.Start {
			if *a.Length == *b.Length {
				return 0
			} else if *a.Length < *b.Length {
				return 1
			} else {
				return -1
			}
		} else if *a.Start < *b.Start {
			return -1
		} else {
			return 1
		}
	})

	lrt := &LinkedRangeTree{}
	mentions := map[id.UserID]struct{}{}
	utf16Message := NewUTF16String(message)
	maxLength := len(utf16Message)
	for _, r := range ranges {
		br := BodyRange{
			Start:  int(*r.Start),
			Length: int(*r.Length),
		}.TruncateEnd(maxLength)
		switch rv := r.GetAssociatedValue().(type) {
		case *signalpb.BodyRange_Style_:
			br.Value = Style(rv.Style)
		case *signalpb.BodyRange_MentionAci:
			parsed, err := uuid.Parse(rv.MentionAci)
			if err != nil {
				continue
			}
			userInfo := params.GetUserInfo(ctx, parsed)
			if userInfo.MXID == "" {
				continue
			}
			mentions[userInfo.MXID] = struct{}{}
			// This could replace the wrong thing if there's a mention without fffc.
			// Maybe use NewUTF16String and do index replacements for the plaintext body too,
			// or just replace the plaintext body by parsing the generated HTML.
			content.Body = strings.Replace(content.Body, "\uFFFC", userInfo.Name, 1)
			br.Value = Mention{UserInfo: userInfo, UUID: parsed}
		}
		lrt.Add(br)
	}

	content.Mentions.UserIDs = maps.Keys(mentions)
	content.FormattedBody = lrt.Format(utf16Message, formatContext{})
	content.Format = event.FormatHTML
	//content.Body = format.HTMLToText(content.FormattedBody)
	return content
}
