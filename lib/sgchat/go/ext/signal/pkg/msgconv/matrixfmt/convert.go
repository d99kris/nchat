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

package matrixfmt

import (
	"context"

	"maunium.net/go/mautrix/event"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

func Parse(ctx context.Context, parser *HTMLParser, content *event.MessageEventContent) (string, []*signalpb.BodyRange) {
	if content.Format != event.FormatHTML {
		return content.Body, nil
	}
	parseCtx := NewContext(ctx)
	parseCtx.AllowedMentions = content.Mentions
	parsed := parser.Parse(content.FormattedBody, parseCtx)
	if parsed == nil {
		return "", nil
	}
	var bodyRanges []*signalpb.BodyRange
	if len(parsed.Entities) > 0 {
		bodyRanges = make([]*signalpb.BodyRange, len(parsed.Entities))
		for i, ent := range parsed.Entities {
			bodyRanges[i] = ent.Proto()
		}
	}
	return parsed.String.String(), bodyRanges
}
