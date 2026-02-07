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
	"fmt"

	"github.com/google/uuid"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

type BodyRangeValue interface {
	String() string
	Format(message string) string
	Proto() signalpb.BodyRangeAssociatedValue
}

type Mention struct {
	UserInfo
	UUID uuid.UUID
}

func (m Mention) String() string {
	return fmt.Sprintf("Mention{MXID: id.UserID(%q), Name: %q}", m.MXID, m.Name)
}

func (m Mention) Proto() signalpb.BodyRangeAssociatedValue {
	return &signalpb.BodyRange_MentionAci{
		MentionAci: m.UUID.String(),
	}
}

type Style int

const (
	StyleNone Style = iota
	StyleBold
	StyleItalic
	StyleSpoiler
	StyleStrikethrough
	StyleMonospace
)

func (s Style) Proto() signalpb.BodyRangeAssociatedValue {
	return &signalpb.BodyRange_Style_{
		Style: signalpb.BodyRange_Style(s),
	}
}

func (s Style) String() string {
	switch s {
	case StyleNone:
		return "StyleNone"
	case StyleBold:
		return "StyleBold"
	case StyleItalic:
		return "StyleItalic"
	case StyleSpoiler:
		return "StyleSpoiler"
	case StyleStrikethrough:
		return "StyleStrikethrough"
	case StyleMonospace:
		return "StyleMonospace"
	default:
		return fmt.Sprintf("Style(%d)", s)
	}
}
