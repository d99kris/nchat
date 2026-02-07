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

package signalfmt_test

import (
	"context"
	"testing"

	"github.com/google/uuid"
	"github.com/stretchr/testify/assert"
	"google.golang.org/protobuf/proto"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/msgconv/signalfmt"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

var realUser = uuid.New()

func TestParse(t *testing.T) {
	formatParams := &signalfmt.FormatParams{
		GetUserInfo: func(ctx context.Context, uuid uuid.UUID) signalfmt.UserInfo {
			if uuid == realUser {
				return signalfmt.UserInfo{
					MXID: "@test:example.com",
					Name: "Matrix User",
				}
			} else {
				return signalfmt.UserInfo{
					MXID: id.UserID("@signal_" + uuid.String() + ":example.com"),
					Name: "Signal User",
				}
			}
		},
	}
	tests := []struct {
		name string
		ins  string
		ine  []*signalpb.BodyRange
		body string
		html string

		extraChecks func(*testing.T, *event.MessageEventContent)
	}{
		{
			name: "empty",
			extraChecks: func(t *testing.T, content *event.MessageEventContent) {
				assert.Empty(t, content.FormattedBody)
				assert.Empty(t, content.Body)
			},
		},
		{
			name: "plain",
			ins:  "Hello world!",
			body: "Hello world!",
			extraChecks: func(t *testing.T, content *event.MessageEventContent) {
				assert.Empty(t, content.FormattedBody)
				assert.Empty(t, content.Format)
			},
		},
		{
			name: "mention",
			ins:  "Hello \uFFFC",
			ine: []*signalpb.BodyRange{{
				Start:  proto.Uint32(6),
				Length: proto.Uint32(1),
				AssociatedValue: &signalpb.BodyRange_MentionAci{
					MentionAci: realUser.String(),
				},
			}},
			body: "Hello Matrix User",
			html: "Hello <a href=\"https://matrix.to/#/@test:example.com\">Matrix User</a>",
		},
		{
			name: "basic style",
			ins:  "Hello, World!",
			ine: []*signalpb.BodyRange{{
				Start:           proto.Uint32(0),
				Length:          proto.Uint32(5),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_BOLD},
			}, {
				Start:           proto.Uint32(3),
				Length:          proto.Uint32(6),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_ITALIC},
			}, {
				Start:           proto.Uint32(4),
				Length:          proto.Uint32(5),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_STRIKETHROUGH},
			}, {
				Start:           proto.Uint32(4),
				Length:          proto.Uint32(5),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_SPOILER},
			}, {
				Start:           proto.Uint32(7),
				Length:          proto.Uint32(5),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_MONOSPACE},
			}, {
				Start:           proto.Uint32(12),
				Length:          proto.Uint32(1),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_BOLD},
			}},
			body: "Hello, World!",
			html: "<strong>Hel<em>l<del><span data-mx-spoiler>o</span></del></em></strong><em><del><span data-mx-spoiler>, <code>Wo</code></span></del></em><code>rld</code><strong>!</strong>",
		},
		{
			name: "basic style with sorting",
			ins:  "Hello, World!",
			ine: []*signalpb.BodyRange{
				{
					Start:           proto.Uint32(12),
					Length:          proto.Uint32(1),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_BOLD},
				},
				{
					Start:           proto.Uint32(0),
					Length:          proto.Uint32(5),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_BOLD},
				},
				{
					Start:           proto.Uint32(4),
					Length:          proto.Uint32(5),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_STRIKETHROUGH},
				},
				{
					Start:           proto.Uint32(7),
					Length:          proto.Uint32(5),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_MONOSPACE},
				},
				{
					Start:           proto.Uint32(4),
					Length:          proto.Uint32(4),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_SPOILER},
				},
				{
					Start:           proto.Uint32(3),
					Length:          proto.Uint32(6),
					AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_ITALIC},
				},
			},
			body: "Hello, World!",
			html: "<strong>Hel<em>l<del><span data-mx-spoiler>o</span></del></em></strong><em><del><span data-mx-spoiler>, <code>W</code></span><code>o</code></del></em><code>rld</code><strong>!</strong>",
		},
		{
			name: "overflow",
			ins:  "Hello, World!",
			ine: []*signalpb.BodyRange{{
				Start:           proto.Uint32(0),
				Length:          proto.Uint32(20),
				AssociatedValue: &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_BOLD},
			}},
			body: "Hello, World!",
			html: "<strong>Hello, World!</strong>",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			parsed := signalfmt.Parse(context.TODO(), test.ins, test.ine, formatParams)
			assert.Equal(t, test.body, parsed.Body)
			assert.Equal(t, test.html, parsed.FormattedBody)
		})
	}
}
