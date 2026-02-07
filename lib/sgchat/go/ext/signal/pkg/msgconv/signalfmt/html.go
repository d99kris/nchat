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
	"strings"
	"unicode/utf16"
)

func (m Mention) Format(message string) string {
	return fmt.Sprintf(
		`<a href="%s">%s</a>`,
		m.MXID.URI().MatrixToURL(),
		strings.Replace(message, "\ufffc", m.Name, 1),
	)
}

func (s Style) Format(message string) string {
	switch s {
	case StyleBold:
		return fmt.Sprintf("<strong>%s</strong>", message)
	case StyleItalic:
		return fmt.Sprintf("<em>%s</em>", message)
	case StyleSpoiler:
		return fmt.Sprintf("<span data-mx-spoiler>%s</span>", message)
	case StyleStrikethrough:
		return fmt.Sprintf("<del>%s</del>", message)
	case StyleMonospace:
		if strings.ContainsRune(message, '\n') {
			// This is somewhat incorrect, as it won't allow inline text before/after a multiline monospace-formatted string.
			return fmt.Sprintf("<pre><code>%s</code></pre>", message)
		}
		return fmt.Sprintf("<code>%s</code>", message)
	default:
		return message
	}
}

type UTF16String []uint16

func NewUTF16String(s string) UTF16String {
	return utf16.Encode([]rune(s))
}

func (u UTF16String) String() string {
	return string(utf16.Decode(u))
}

func (lrt *LinkedRangeTree) Format(message UTF16String, ctx formatContext) string {
	if lrt == nil || lrt.Node == nil {
		return ctx.TextToHTML(message.String())
	}
	head := message[:lrt.Node.Start]
	headStr := ctx.TextToHTML(head.String())
	inner := message[lrt.Node.Start:lrt.Node.End()]
	tail := message[lrt.Node.End():]
	ourCtx := ctx
	if lrt.Node.Value == StyleMonospace {
		ourCtx.IsInCodeblock = true
	}
	childMessage := lrt.Child.Format(inner, ourCtx)
	formattedChildMessage := lrt.Node.Value.Format(childMessage)
	siblingMessage := lrt.Sibling.Format(tail, ctx)
	return headStr + formattedChildMessage + siblingMessage
}
