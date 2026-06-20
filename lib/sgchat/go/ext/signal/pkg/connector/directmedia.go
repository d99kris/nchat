package connector

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"

	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/mediaproxy"

	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

var _ bridgev2.DirectMediableNetwork = (*SignalConnector)(nil)

func (s *SignalConnector) SetUseDirectMedia() {
	s.MsgConv.DirectMedia = true
}

func (s *SignalConnector) Download(ctx context.Context, mediaID networkid.MediaID, params map[string]string) (mediaproxy.GetMediaResponse, error) {
	log := s.Bridge.Log.With().Str("component", "direct download").Logger()

	info, err := signalid.ParseDirectMediaInfo(mediaID)
	if err != nil {
		return nil, fmt.Errorf("failed to parse direct media id: %w", err)
	}

	var rawDataResp []byte
	switch info := info.(type) {
	case *signalid.DirectMediaAttachment:
		log.Info().
			Uint64("cdn_id", info.CDNID).
			Str("cdn_key", info.CDNKey).
			Uint32("cdn_number", info.CDNNumber).
			Int("key_len", len(info.Key)).
			Int("digest_len", len(info.Digest)).
			Bool("plaintext_digest", info.PlaintextDigest).
			Uint32("size", info.Size).
			Msg("Direct downloading attachment")

		return &mediaproxy.GetMediaResponseFile{
			Callback: func(w *os.File) (*mediaproxy.FileMeta, error) {
				_, err := signalmeow.DownloadAttachment(
					ctx, info.CDNID, info.CDNKey, info.CDNNumber, info.Key, info.Digest, info.PlaintextDigest, info.Size, w,
				)
				if err != nil {
					return nil, err
				}
				return &mediaproxy.FileMeta{}, nil
			},
		}, nil
	case *signalid.DirectMediaGroupAvatar:
		log.Info().
			Stringer("user_id", info.UserID).
			Hex("group_id", info.GroupID[:]).
			Str("group_avatar_path", info.GroupAvatarPath).
			Msg("Direct downloading group avatar")

		groupID := types.GroupIdentifier(base64.StdEncoding.EncodeToString(info.GroupID[:]))

		userLogin, err := s.Bridge.GetExistingUserLoginByID(ctx, signalid.MakeUserLoginID(info.UserID))
		if err != nil {
			return nil, fmt.Errorf("failed to get user login: %w", err)
		} else if userLogin == nil {
			return nil, bridgev2.ErrNotLoggedIn
		}

		client := userLogin.Client.(*SignalClient)

		groupMasterKey, err := client.Client.Store.GroupStore.MasterKeyFromGroupIdentifier(ctx, groupID)
		if err != nil {
			return nil, fmt.Errorf("failed to to get group master key: %w", err)
		}

		rawDataResp, err = client.Client.DownloadGroupAvatar(ctx, info.GroupAvatarPath, groupMasterKey)
		if err != nil {
			log.Err(err).Msg("Direct download failed")
			return nil, err
		}
	case *signalid.DirectMediaProfileAvatar:
		log.Info().
			Stringer("user_id", info.UserID).
			Stringer("contact_id", info.ContactID).
			Str("profile_avatar_path", info.ProfileAvatarPath).
			Msg("Direct downloading profile avatar")

		userLogin, err := s.Bridge.GetExistingUserLoginByID(ctx, signalid.MakeUserLoginID(info.UserID))
		if err != nil {
			return nil, fmt.Errorf("failed to get user login: %w", err)
		} else if userLogin == nil {
			return nil, bridgev2.ErrNotLoggedIn
		}

		client := userLogin.Client.(*SignalClient)

		profileKey, err := client.Client.Store.RecipientStore.LoadProfileKey(ctx, info.ContactID)
		if err != nil {
			return nil, fmt.Errorf("failed to get contact: %w", err)
		} else if profileKey == nil {
			return nil, fmt.Errorf("profile key not found")
		}

		rawDataResp, err = client.Client.DownloadUserAvatar(ctx, info.ProfileAvatarPath, *profileKey)
		if err != nil {
			log.Err(err).Msg("Direct download failed")
			return nil, err
		}
	case *signalid.DirectMediaSticker:
		log.Info().
			Hex("pack_id", info.PackID).
			Uint32("sticker_id", info.StickerID).
			Msg("Direct downloading sticker")

		rawDataResp, err = signalmeow.DownloadStickerPackItem(ctx, info.PackID, info.PackKey, info.StickerID)
		if err != nil {
			log.Err(err).Msg("Direct download failed")
			return nil, err
		}
	default:
		return nil, fmt.Errorf("no downloader for direct media type: %T", info)
	}
	if rawDataResp == nil {
		return nil, fmt.Errorf("unexpected fallthrough with no data")
	}
	return mediaproxy.GetMediaResponseRawData(rawDataResp), nil
}
