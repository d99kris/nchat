// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2024 Tulir Asokan
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

package msgconv

import (
	"context"
	"fmt"
	"net/http"
	"strings"

	"github.com/rs/zerolog"
	"go.mau.fi/util/exmime"
	"go.mau.fi/util/ffmpeg"
	"go.mau.fi/util/variationselector"
	"golang.org/x/exp/constraints"
	"google.golang.org/protobuf/proto"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/event"

	"go.mau.fi/mautrix-signal/pkg/msgconv/matrixfmt"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

func (mc *MessageConverter) ToSignal(
	ctx context.Context,
	client *signalmeow.Client,
	portal *bridgev2.Portal,
	evt *event.Event,
	content *event.MessageEventContent,
	relaybotFormatted bool,
	replyTo *database.Message,
) (*signalpb.DataMessage, error) {
	ctx = context.WithValue(ctx, contextKeyClient, client)
	ctx = context.WithValue(ctx, contextKeyPortal, portal)
	if evt.Type == event.EventSticker {
		content.MsgType = event.MessageType(event.EventSticker.Type)
	}

	dm := &signalpb.DataMessage{
		Preview: mc.convertURLPreviewToSignal(ctx, content),
	}
	if replyTo != nil {
		authorACI, messageID, err := signalid.ParseMessageID(replyTo.ID)
		if err == nil {
			dm.Quote = &signalpb.DataMessage_Quote{
				Id:        proto.Uint64(messageID),
				AuthorAci: proto.String(authorACI.String()),
				Type:      signalpb.DataMessage_Quote_NORMAL.Enum(),
			}
			if replyTo.Metadata.(*signalid.MessageMetadata).ContainsAttachments {
				dm.Quote.Attachments = make([]*signalpb.DataMessage_Quote_QuotedAttachment, 1)
			}
		}
	}
	if content.BeeperDisappearingTimer != nil {
		dm.ExpireTimer = proto.Uint32(uint32(content.BeeperDisappearingTimer.Timer.Seconds()))
	} else if portal.Disappear.Timer > 0 {
		dm.ExpireTimer = proto.Uint32(uint32(portal.Disappear.Timer.Seconds()))
	}
	if dm.ExpireTimer != nil && *dm.ExpireTimer != 0 {
		timerVersion := portal.Metadata.(*signalid.PortalMetadata).ExpirationTimerVersion
		if timerVersion > 0 {
			dm.ExpireTimerVersion = &timerVersion
		}
	}
	if content.MsgType == event.MsgEmote && !relaybotFormatted {
		content.Body = "/me " + content.Body
		if content.FormattedBody != "" {
			content.FormattedBody = "/me " + content.FormattedBody
		}
	}
	body, bodyRanges := matrixfmt.Parse(ctx, mc.MatrixFmtParams, content)
	switch content.MsgType {
	case event.MsgText, event.MsgNotice, event.MsgEmote:
		dm.Body = proto.String(body)
		dm.BodyRanges = bodyRanges
	case event.MsgImage, event.MsgVideo, event.MsgAudio, event.MsgFile:
		att, err := mc.convertFileToSignal(ctx, evt, content)
		if err != nil {
			return nil, fmt.Errorf("failed to convert attachment: %w", err)
		}
		if content.FileName != "" && (content.FileName != content.Body || content.Format == event.FormatHTML) {
			dm.Body = proto.String(body)
			dm.BodyRanges = bodyRanges
		}
		dm.Attachments = []*signalpb.AttachmentPointer{att}
	case event.MessageType(event.EventSticker.Type):
		if content.FileName == "" {
			content.FileName = "sticker" + exmime.ExtensionFromMimetype(content.Info.MimeType)
		}
		att, err := mc.convertFileToSignal(ctx, evt, content)
		if err != nil {
			return nil, fmt.Errorf("failed to convert sticker: %w", err)
		}
		att.Flags = proto.Uint32(uint32(signalpb.AttachmentPointer_BORDERLESS))
		var emoji *string
		// TODO check for single grapheme cluster?
		if len([]rune(content.Body)) == 1 {
			emoji = proto.String(variationselector.Remove(content.Body))
		}
		dm.Sticker = &signalpb.DataMessage_Sticker{
			// Signal iOS validates that pack id/key are of the correct length.
			// Android is fine with any non-nil values (like a zero-length byte string).
			PackId:    make([]byte, 16),
			PackKey:   make([]byte, 32),
			StickerId: proto.Uint32(0),

			Data:  att,
			Emoji: emoji,
		}
	case event.MsgLocation:
		lat, lon, err := parseGeoURI(content.GeoURI)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Invalid geo URI")
			return nil, err
		}
		locationString := fmt.Sprintf(mc.LocationFormat, lat, lon)
		dm.Body = &locationString
	default:
		return nil, fmt.Errorf("%w %s", bridgev2.ErrUnsupportedMessageType, content.MsgType)
	}
	return dm, nil
}

func maybeInt[T constraints.Integer](v T) *T {
	if v == 0 {
		return nil
	}
	return &v
}

func (mc *MessageConverter) convertFileToSignal(ctx context.Context, evt *event.Event, content *event.MessageEventContent) (*signalpb.AttachmentPointer, error) {
	log := zerolog.Ctx(ctx)
	data, err := mc.Bridge.Bot.DownloadMedia(ctx, content.URL, content.File)
	if err != nil {
		return nil, fmt.Errorf("%w: %w", bridgev2.ErrMediaDownloadFailed, err)
	}
	fileName := content.Body
	if content.FileName != "" {
		fileName = content.FileName
	}
	mime := content.GetInfo().MimeType
	if mime == "" {
		mime = http.DetectContentType(data)
	}
	if content.MSC3245Voice != nil && mime != "audio/aac" && ffmpeg.Supported() {
		data, err = ffmpeg.ConvertBytes(ctx, data, ".aac", []string{}, []string{"-c:a", "aac"}, mime)
		if err != nil {
			return nil, err
		}
		mime = "audio/aac"
		fileName += ".aac"
	} else if evt.Type == event.EventSticker {
		switch mime {
		case "image/webp", "image/png", "image/apng":
			// allowed
		case "image/gif":
			if !ffmpeg.Supported() {
				return nil, fmt.Errorf("converting gif stickers is not supported")
			}
			data, err = ffmpeg.ConvertBytes(ctx, data, ".apng", []string{}, []string{}, mime)
			if err != nil {
				return nil, fmt.Errorf("%w (gif to apng): %w", bridgev2.ErrMediaConvertFailed, err)
			}
			fileName += ".apng"
			mime = "image/apng"
		default:
			return nil, fmt.Errorf("unsupported content type for sticker %s", mime)
		}
	}
	att, err := getClient(ctx).UploadAttachment(ctx, data)
	if err != nil {
		log.Err(err).Msg("Failed to upload file")
		return nil, fmt.Errorf("%w: %w", bridgev2.ErrMediaReuploadFailed, err)
	}
	if content.MSC3245Voice != nil && mime == "audio/aac" {
		att.Flags = proto.Uint32(uint32(signalpb.AttachmentPointer_VOICE_MESSAGE))
	}
	if content.Info.MauGIF {
		att.Flags = proto.Uint32(uint32(signalpb.AttachmentPointer_GIF))
	}
	att.ContentType = proto.String(mime)
	att.FileName = &fileName
	att.Height = maybeInt(uint32(content.Info.Height))
	att.Width = maybeInt(uint32(content.Info.Width))
	if content.Info.Blurhash != "" {
		att.BlurHash = proto.String(content.Info.Blurhash)
	} else if content.Info.AnoaBlurhash != "" {
		att.BlurHash = proto.String(content.Info.AnoaBlurhash)
	}
	return att, nil
}

func parseGeoURI(uri string) (lat, long string, err error) {
	if !strings.HasPrefix(uri, "geo:") {
		err = fmt.Errorf("uri doesn't have geo: prefix")
		return
	}
	// Remove geo: prefix and anything after ;
	coordinates := strings.Split(strings.TrimPrefix(uri, "geo:"), ";")[0]
	splitCoordinates := strings.Split(coordinates, ",")
	if len(splitCoordinates) != 2 {
		err = fmt.Errorf("didn't find exactly two numbers separated by a comma")
	} else {
		lat = splitCoordinates[0]
		long = splitCoordinates[1]
	}
	return
}
