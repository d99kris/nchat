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
	"time"

	"github.com/rs/zerolog"
	"google.golang.org/protobuf/proto"
	"maunium.net/go/mautrix/event"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

func (mc *MessageConverter) convertURLPreviewsToBeeper(ctx context.Context, preview []*signalpb.Preview, attMap AttachmentMap) []*event.BeeperLinkPreview {
	output := make([]*event.BeeperLinkPreview, len(preview))
	for i, p := range preview {
		output[i] = mc.convertURLPreviewToBeeper(ctx, p, attMap)
	}
	return output
}

func (mc *MessageConverter) convertURLPreviewToBeeper(ctx context.Context, preview *signalpb.Preview, attMap AttachmentMap) *event.BeeperLinkPreview {
	output := &event.BeeperLinkPreview{
		MatchedURL: preview.GetUrl(),
		LinkPreview: event.LinkPreview{
			CanonicalURL: preview.GetUrl(),
			Title:        preview.GetTitle(),
			Description:  preview.GetDescription(),
		},
	}
	if preview.Image != nil {
		msg, err := mc.reuploadAttachment(ctx, preview.Image, attMap)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to reupload link preview image")
		} else {
			output.ImageURL = msg.Content.URL
			output.ImageEncryption = msg.Content.File
			output.ImageType = msg.Content.Info.MimeType
			output.ImageSize = event.IntOrString(msg.Content.Info.Size)
			output.ImageHeight = event.IntOrString(msg.Content.Info.Height)
			output.ImageWidth = event.IntOrString(msg.Content.Info.Width)
		}
	}
	return output
}

func (mc *MessageConverter) convertURLPreviewToSignal(ctx context.Context, content *event.MessageEventContent) []*signalpb.Preview {
	if len(content.BeeperLinkPreviews) == 0 {
		return nil
	}
	output := make([]*signalpb.Preview, len(content.BeeperLinkPreviews))
	for i, preview := range content.BeeperLinkPreviews {
		output[i] = &signalpb.Preview{
			Url:         proto.String(preview.MatchedURL),
			Title:       proto.String(preview.Title),
			Description: proto.String(preview.Description),
			Date:        proto.Uint64(uint64(time.Now().UnixMilli())),
		}
		if preview.ImageURL != "" || preview.ImageEncryption != nil {
			data, err := mc.Bridge.Bot.DownloadMedia(ctx, preview.ImageURL, preview.ImageEncryption)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Int("preview_index", i).Msg("Failed to download URL preview image")
				continue
			}
			uploaded, err := getClient(ctx).UploadAttachment(ctx, data)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Int("preview_index", i).Msg("Failed to reupload URL preview image")
				continue
			}
			uploaded.ContentType = proto.String(preview.ImageType)
			uploaded.Width = proto.Uint32(uint32(preview.ImageWidth))
			uploaded.Height = proto.Uint32(uint32(preview.ImageHeight))
			output[i].Image = uploaded
		}
	}
	return output
}
