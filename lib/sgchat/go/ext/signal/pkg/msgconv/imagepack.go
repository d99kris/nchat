// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2026 Tulir Asokan
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
	"bytes"
	"context"
	"encoding/hex"
	"fmt"
	"net/url"
	"strconv"
	"strings"

	"go.mau.fi/util/emojishortcodes"
	"google.golang.org/protobuf/proto"
	"maunium.net/go/mautrix"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

const StickerSourceID = "signal"
const PackURLFormat = "https://signal.art/addstickers/#pack_id=%x&pack_key=%x"

const PackIDLength = 16
const PackKeyLength = 32
const PackURLLength = len(PackURLFormat) - len("%x")*2 + PackIDLength*2 + PackKeyLength*2

var zeroPackID = make([]byte, PackIDLength)

func ParseStickerMeta(info *event.BridgedSticker) *signalpb.DataMessage_Sticker {
	if info == nil || info.Network != StickerSourceID || len(info.PackURL) != PackURLLength {
		return nil
	}
	stickerID, err := strconv.ParseUint(info.ID, 10, 32)
	if err != nil {
		return nil
	}
	packID, packKey, err := parsePackURL(info.PackURL)
	if err != nil || len(packID) != PackIDLength || len(packKey) != PackKeyLength || bytes.Equal(packID, zeroPackID) {
		return nil
	}
	return &signalpb.DataMessage_Sticker{
		PackId:    packID,
		PackKey:   packKey,
		StickerId: proto.Uint32(uint32(stickerID)),
		Emoji:     &info.Emoji,
	}
}

func parsePackURL(rawURL string) (packID, packKey []byte, err error) {
	parsed, err := url.Parse(rawURL)
	if err != nil {
		return nil, nil, fmt.Errorf("invalid URL: %w", err)
	} else if parsed.Host != "signal.art" || !strings.HasPrefix(parsed.Path, "/addstickers") {
		return nil, nil, fmt.Errorf("invalid host or path in URL")
	}
	q, err := url.ParseQuery(parsed.Fragment)
	if err != nil {
		return nil, nil, fmt.Errorf("invalid URL fragment: %w", err)
	}
	packID, err = hex.DecodeString(q.Get("pack_id"))
	if err != nil {
		return nil, nil, fmt.Errorf("invalid pack ID in URL: %w", err)
	}
	packKey, err = hex.DecodeString(q.Get("pack_key"))
	if err != nil {
		return nil, nil, fmt.Errorf("invalid pack key in URL: %w", err)
	}
	return
}

func (mc *MessageConverter) DownloadImagePack(ctx context.Context, url string) (*bridgev2.ImportedImagePack, error) {
	packID, packKey, err := parsePackURL(url)
	if err != nil {
		return nil, bridgev2.WrapRespErr(err, mautrix.MNotFound)
	}
	manifest, err := signalmeow.DownloadStickerPackManifest(ctx, packID, packKey)
	if err != nil {
		return nil, fmt.Errorf("failed to download sticker pack manifest: %w", err)
	}
	topLevelExtra := map[string]any{
		"fi.mau.signal.stickerpack": map[string]any{
			"pack_id":  hex.EncodeToString(packID),
			"pack_key": hex.EncodeToString(packKey),
		},
	}
	content := &event.ImagePackEventContent{
		Images: make(map[string]*event.ImagePackImage, len(manifest.Stickers)),
		Metadata: event.ImagePackMetadata{
			DisplayName: manifest.GetTitle(),
			AvatarURL:   "",
			Usage:       []event.ImagePackUsage{event.ImagePackUsageSticker},
			Attribution: manifest.GetAuthor(),
			BridgedPack: &event.BridgedStickerPack{
				Network: StickerSourceID,
				URL:     fmt.Sprintf(PackURLFormat, packID, packKey),
			},
		},
	}
	imagesByID := make(map[uint32]id.ContentURIString, len(manifest.Stickers))
	uploadImage := func(sticker *signalpb.Pack_Sticker) (id.ContentURIString, error) {
		stickerID := sticker.GetId()
		existing, ok := imagesByID[stickerID]
		if ok {
			return existing, nil
		}
		var mxc id.ContentURIString
		if mc.DirectMedia {
			mediaID, err := signalid.DirectMediaSticker{
				PackID:    packID,
				PackKey:   packKey,
				StickerID: stickerID,
			}.AsMediaID()
			if err != nil {
				return "", fmt.Errorf("failed to create media ID for sticker %d: %w", stickerID, err)
			}
			mxc, err = mc.Bridge.Matrix.GenerateContentURI(ctx, mediaID)
			if err != nil {
				return "", fmt.Errorf("failed to generate content URI for sticker %d: %w", stickerID, err)
			}
		} else {
			dbKey := database.Key(fmt.Sprintf("stickercache:%x:%d", packID, stickerID))
			if cached := mc.Bridge.DB.KV.Get(ctx, dbKey); cached != "" {
				mxc = id.ContentURIString(cached)
				imagesByID[stickerID] = mxc
				return mxc, nil
			}
			data, err := signalmeow.DownloadStickerPackItem(ctx, packID, packKey, stickerID)
			if err != nil {
				return "", fmt.Errorf("failed to download sticker %d: %w", stickerID, err)
			}
			mxc, _, err = mc.Bridge.Bot.UploadMedia(ctx, "", data, "", sticker.GetContentType())
			if err != nil {
				return "", fmt.Errorf("failed to upload sticker %d: %w", stickerID, err)
			}
			mc.Bridge.DB.KV.Set(ctx, dbKey, string(mxc))
		}
		imagesByID[stickerID] = mxc
		return mxc, nil
	}
	for _, sticker := range manifest.Stickers {
		mxc, err := uploadImage(sticker)
		if err != nil {
			return nil, err
		}
		shortcode := emojishortcodes.Get(sticker.GetEmoji())
		realShortcode := shortcode
		i := 2
		for _, alreadyExists := content.Images[realShortcode]; alreadyExists; i++ {
			realShortcode = fmt.Sprintf("%s_%d", shortcode, i)
		}
		content.Images[realShortcode] = &event.ImagePackImage{
			URL:  mxc,
			Body: sticker.GetEmoji(),
			Info: &event.FileInfo{
				MimeType: sticker.GetContentType(),
				Width:    200,
				Height:   200,
				BridgedSticker: &event.BridgedSticker{
					Network: StickerSourceID,
					ID:      strconv.FormatUint(uint64(sticker.GetId()), 10),
					Emoji:   sticker.GetEmoji(),
					PackURL: content.Metadata.BridgedPack.URL,
				},
			},
		}
	}
	if manifest.Cover != nil {
		content.Metadata.AvatarURL, err = uploadImage(manifest.Cover)
		if err != nil {
			return nil, fmt.Errorf("failed to upload sticker pack cover: %w", err)
		}
	}
	return &bridgev2.ImportedImagePack{
		Content:   content,
		Extra:     topLevelExtra,
		Shortcode: hex.EncodeToString(packID),
	}, nil
}
