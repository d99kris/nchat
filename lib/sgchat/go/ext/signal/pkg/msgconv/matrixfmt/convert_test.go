package matrixfmt_test

import (
	"context"
	"fmt"
	"testing"

	"github.com/google/uuid"
	"github.com/stretchr/testify/assert"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/msgconv/matrixfmt"
	"go.mau.fi/mautrix-signal/pkg/msgconv/signalfmt"
)

var formatParams = &matrixfmt.HTMLParser{
	GetUUIDFromMXID: func(_ context.Context, id id.UserID) uuid.UUID {
		if id.Homeserver() == "signal" {
			return uuid.MustParse(id.Localpart())
		}
		return uuid.Nil
	},
}

func TestParse_Empty(t *testing.T) {
	text, entities := matrixfmt.Parse(context.TODO(), formatParams, &event.MessageEventContent{
		MsgType: event.MsgText,
		Body:    "",
	})
	assert.Equal(t, "", text)
	assert.Empty(t, entities)
}

func TestParse_EmptyHTML(t *testing.T) {
	text, entities := matrixfmt.Parse(context.TODO(), formatParams, &event.MessageEventContent{
		MsgType:       event.MsgText,
		Body:          "",
		Format:        event.FormatHTML,
		FormattedBody: "",
	})
	assert.Equal(t, "", text)
	assert.Empty(t, entities)
}

func TestParse_Plaintext(t *testing.T) {
	text, entities := matrixfmt.Parse(context.TODO(), formatParams, &event.MessageEventContent{
		MsgType: event.MsgText,
		Body:    "Hello world!",
	})
	assert.Equal(t, "Hello world!", text)
	assert.Empty(t, entities)
}

func TestParse_HTML(t *testing.T) {
	tests := []struct {
		name string
		in   string
		out  string
		ent  signalfmt.BodyRangeList
	}{
		{name: "Plain", in: "Hello, World!", out: "Hello, World!"},
		{name: "Basic", in: "<strong>Hello</strong>, World!", out: "Hello, World!", ent: signalfmt.BodyRangeList{{
			Start:  0,
			Length: 5,
			Value:  signalfmt.StyleBold,
		}}},
		{name: "UnnecessaryWhitespace", in: "<strong>  Hello  </strong>, World!", out: "Hello, World!", ent: signalfmt.BodyRangeList{{
			Start:  0,
			Length: 5,
			Value:  signalfmt.StyleBold,
		}}},
		{name: "UnnecessaryWhitespaceParagraph", in: "<p>  Hello  </p>", out: "Hello"},
		{name: "EmptyParagraph", in: "<p>Hello</p><p>   </p>", out: "Hello"},
		{
			name: "MultiBasic",
			in:   "<strong><em>Hell</em>o</strong>, <del>Wo<span data-mx-spoiler>rld</span></del><code>!</code>",
			out:  "Hello, World!",
			ent: signalfmt.BodyRangeList{{
				Start:  0,
				Length: 5,
				Value:  signalfmt.StyleBold,
			}, {
				Start:  0,
				Length: 4,
				Value:  signalfmt.StyleItalic,
			}, {
				Start:  7,
				Length: 5,
				Value:  signalfmt.StyleStrikethrough,
			}, {
				Start:  9,
				Length: 3,
				Value:  signalfmt.StyleSpoiler,
			}, {
				Start:  12,
				Length: 1,
				Value:  signalfmt.StyleMonospace,
			}},
		},
		{
			name: "TrimSpace",
			in:   "<strong> Hello   </strong>",
			out:  "Hello",
			ent: signalfmt.BodyRangeList{{
				Start:  0,
				Length: 5,
				Value:  signalfmt.StyleBold,
			}},
		},
		{
			name: "List",
			in:   "<ul><li>woof</li><li><strong>meow</strong></li><li><pre><code>hmm\nmeow</code></pre></li><li><blockquote>meow<br><h1>meow</h1></blockquote></li></ul>",
			out:  "* woof\n* meow\n* hmm\n  meow\n* > meow\n  > \n  > # meow",
			ent: signalfmt.BodyRangeList{{
				Start:  9,
				Length: 4,
				Value:  signalfmt.StyleBold,
			}, {
				Start:  16,
				Length: 3,
				Value:  signalfmt.StyleMonospace,
			}, {
				// FIXME optimally this would be a single range with the previous one so the indent is also monospace
				Start:  22,
				Length: 4,
				Value:  signalfmt.StyleMonospace,
			}, {
				Start:  45,
				Length: 6,
				Value:  signalfmt.StyleBold,
			}},
		},
		{
			name: "OrderedList",
			in:   "<ol start=9><li>woof</li><li><strong>meow</strong></li><li><pre><code>hmm\nmeow</code></pre></li><li><blockquote>meow<br><h1>meow</h1></blockquote></li></ol>",
			out:  "9.  woof\n10. meow\n11. hmm\n    meow\n12. > meow\n    > \n    > # meow",
			ent: signalfmt.BodyRangeList{{
				Start:  13,
				Length: 4,
				Value:  signalfmt.StyleBold,
			}, {
				Start:  22,
				Length: 3,
				Value:  signalfmt.StyleMonospace,
			}, {
				Start:  30,
				Length: 4,
				Value:  signalfmt.StyleMonospace,
			}, {
				Start:  59,
				Length: 6,
				Value:  signalfmt.StyleBold,
			}},
		},
	}
	matrixfmt.DebugLog = func(format string, args ...any) {
		fmt.Printf(format, args...)
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			fmt.Println("--------------------------------------------------------------------------------")
			parsed := formatParams.Parse(test.in, matrixfmt.NewContext(context.TODO()))
			assert.Equal(t, test.out, parsed.String.String())
			assert.Equal(t, test.ent, parsed.Entities)
		})
	}
}
