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
	"bytes"
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/emersion/go-vcard"
	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exmime"
	"go.mau.fi/util/ffmpeg"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/event"

	"go.mau.fi/mautrix-signal/pkg/msgconv/signalfmt"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

var (
	ErrAttachmentNotInBackup = errors.New("attachment not found in backup")
	ErrBackupNotSupported    = errors.New("downloading attachments from server-side backup is not yet supported")
)

func calculateLength(dm *signalpb.DataMessage) int {
	if dm.GetFlags()&uint32(signalpb.DataMessage_EXPIRATION_TIMER_UPDATE) != 0 {
		return 1
	}
	if dm.Sticker != nil || dm.PollVote != nil || dm.PollCreate != nil || dm.PollTerminate != nil {
		return 1
	}
	length := len(dm.Attachments) + len(dm.Contact)
	if dm.Body != nil {
		length++
	}
	if dm.Payment != nil {
		length++
	}
	if dm.GiftBadge != nil {
		length++
	}
	if length == 0 && dm.GetRequiredProtocolVersion() > uint32(signalpb.DataMessage_CURRENT) {
		length = 1
	}
	return length
}

func CanConvertSignal(dm *signalpb.DataMessage) bool {
	return calculateLength(dm) > 0
}

const ViewOnceDisappearTimer = 5 * time.Minute

func (mc *MessageConverter) ToMatrix(
	ctx context.Context,
	client *signalmeow.Client,
	portal *bridgev2.Portal,
	sender uuid.UUID,
	intent bridgev2.MatrixAPI,
	dm *signalpb.DataMessage,
	attMap AttachmentMap,
) *bridgev2.ConvertedMessage {
	ctx = context.WithValue(ctx, contextKeyClient, client)
	ctx = context.WithValue(ctx, contextKeyPortal, portal)
	ctx = context.WithValue(ctx, contextKeyIntent, intent)
	cm := &bridgev2.ConvertedMessage{
		ReplyTo:    nil,
		ThreadRoot: nil,
		Parts:      make([]*bridgev2.ConvertedMessagePart, 0, calculateLength(dm)),
	}
	if dm.GetFlags()&uint32(signalpb.DataMessage_EXPIRATION_TIMER_UPDATE) != 0 {
		cm.Parts = append(cm.Parts, mc.ConvertDisappearingTimerChangeToMatrix(
			ctx, dm.GetExpireTimer(), dm.ExpireTimerVersion, time.UnixMilli(int64(dm.GetTimestamp())), attMap != nil,
		))
		// Don't allow any other parts in a disappearing timer change message
		return cm
	}
	if dm.GetExpireTimer() > 0 {
		cm.Disappear.Type = event.DisappearingTypeAfterRead
		cm.Disappear.Timer = time.Duration(dm.GetExpireTimer()) * time.Second
	}
	if dm.Sticker != nil {
		cm.Parts = append(cm.Parts, mc.convertStickerToMatrix(ctx, dm.Sticker, attMap))
		// Don't allow any other parts in a sticker message
		return cm
	}
	if dm.PollVote != nil {
		cm.Parts = append(cm.Parts, mc.convertPollVoteToMatrix(ctx, dm.PollVote))
		return cm
	}
	if dm.PollCreate != nil {
		cm.Parts = append(cm.Parts, mc.convertPollCreateToMatrix(dm.PollCreate))
		return cm
	}
	if dm.PollTerminate != nil {
		cm.Parts = append(cm.Parts, mc.convertPollTerminateToMatrix(ctx, sender, dm.PollTerminate))
		return cm
	}
	for i, att := range dm.GetAttachments() {
		if att.GetContentType() != "text/x-signal-plain" {
			cm.Parts = append(cm.Parts, mc.convertAttachmentToMatrix(ctx, i, att, attMap))
		} else {
			longBody, err := mc.downloadSignalLongText(ctx, att, attMap)
			if err == nil {
				dm.Body = longBody
			} else {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to download Signal long text")
			}
		}
	}
	for _, contact := range dm.GetContact() {
		cm.Parts = append(cm.Parts, mc.convertContactToMatrix(ctx, contact, attMap))
	}
	if dm.Payment != nil {
		cm.Parts = append(cm.Parts, mc.convertPaymentToMatrix(ctx, dm.Payment))
	}
	if dm.GiftBadge != nil {
		cm.Parts = append(cm.Parts, mc.convertGiftBadgeToMatrix(ctx, dm.GiftBadge))
	}
	if dm.Body != nil {
		cm.Parts = append(cm.Parts, mc.convertTextToMatrix(ctx, dm, attMap))
	}
	if len(cm.Parts) == 0 && dm.GetRequiredProtocolVersion() > uint32(signalpb.DataMessage_CURRENT) {
		cm.Parts = append(cm.Parts, &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgNotice,
				Body:    "The bridge does not support this message type yet.",
			},
		})
	}
	if dm.GetIsViewOnce() && mc.DisappearViewOnce && (cm.Disappear.Timer == 0 || cm.Disappear.Timer > ViewOnceDisappearTimer) {
		cm.Disappear.Type = event.DisappearingTypeAfterRead
		cm.Disappear.Timer = ViewOnceDisappearTimer
		cm.Parts = append(cm.Parts, &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgText,
				Body:    "This is a view-once message. It will disappear in 5 minutes.",
			},
		})
	}
	cm.MergeCaption()
	for i, part := range cm.Parts {
		part.ID = signalid.MakeMessagePartID(i)
		part.DBMetadata = &signalid.MessageMetadata{
			ContainsAttachments: len(dm.GetAttachments()) > 0,
		}
	}
	if dm.Quote != nil {
		authorACI, err := uuid.Parse(dm.Quote.GetAuthorAci())
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Str("author_aci", dm.Quote.GetAuthorAci()).Msg("Failed to parse quote author ACI")
		} else {
			cm.ReplyTo = &networkid.MessageOptionalPartID{
				MessageID: signalid.MakeMessageID(authorACI, dm.Quote.GetId()),
			}
		}
	}
	return cm
}

func (mc *MessageConverter) ConvertDisappearingTimerChangeToMatrix(
	ctx context.Context, timer uint32, timerVersion *uint32, ts time.Time, isBackfill bool,
) *bridgev2.ConvertedMessagePart {
	portal := getPortal(ctx)
	setting := database.DisappearingSetting{
		Timer: time.Duration(timer) * time.Second,
		Type:  event.DisappearingTypeAfterRead,
	}
	if timer == 0 {
		setting.Type = ""
	}
	part := &bridgev2.ConvertedMessagePart{
		Type:    event.EventMessage,
		Content: bridgev2.DisappearingMessageNotice(time.Duration(timer)*time.Second, false),
		Extra: map[string]any{
			"com.beeper.action_message": map[string]any{
				"type":       "disappearing_timer",
				"timer":      setting.Timer.Milliseconds(),
				"timer_type": setting.Type,
				"implicit":   false,
				"backfill":   isBackfill,
			},
		},
		DontBridge: setting == portal.Disappear,
	}
	if isBackfill {
		return part
	}
	portalMeta := portal.Metadata.(*signalid.PortalMetadata)
	if timerVersion != nil && portalMeta.ExpirationTimerVersion > *timerVersion {
		zerolog.Ctx(ctx).Warn().
			Uint32("current_version", portalMeta.ExpirationTimerVersion).
			Uint32("new_version", *timerVersion).
			Msg("Ignoring outdated disappearing timer change")
		part.Content.Body += " (change ignored)"
		return part
	}
	if timerVersion != nil {
		portalMeta.ExpirationTimerVersion = *timerVersion
	} else {
		portalMeta.ExpirationTimerVersion = 1
	}
	portal.UpdateDisappearingSetting(ctx, setting, bridgev2.UpdateDisappearingSettingOpts{
		Sender:    getIntent(ctx),
		Timestamp: ts,
		Save:      true,
	})
	return part
}

func (mc *MessageConverter) convertTextToMatrix(ctx context.Context, dm *signalpb.DataMessage, attMap AttachmentMap) *bridgev2.ConvertedMessagePart {
	content := signalfmt.Parse(ctx, dm.GetBody(), dm.GetBodyRanges(), mc.SignalFmtParams)
	extra := map[string]any{}
	if len(dm.Preview) > 0 {
		content.BeeperLinkPreviews = mc.convertURLPreviewsToBeeper(ctx, dm.Preview, attMap)
	}
	return &bridgev2.ConvertedMessagePart{
		Type:    event.EventMessage,
		Content: content,
		Extra:   extra,
	}
}

func (mc *MessageConverter) convertPaymentToMatrix(_ context.Context, payment *signalpb.DataMessage_Payment) *bridgev2.ConvertedMessagePart {
	return &bridgev2.ConvertedMessagePart{
		Type: event.EventMessage,
		Content: &event.MessageEventContent{
			MsgType: event.MsgNotice,
			Body:    "Payments are not yet supported",
		},
		Extra: map[string]any{
			"fi.mau.signal.payment": payment,
		},
	}
}

func (mc *MessageConverter) convertGiftBadgeToMatrix(_ context.Context, giftBadge *signalpb.DataMessage_GiftBadge) *bridgev2.ConvertedMessagePart {
	return &bridgev2.ConvertedMessagePart{
		Type: event.EventMessage,
		Content: &event.MessageEventContent{
			MsgType: event.MsgNotice,
			Body:    "Gift badges are not yet supported",
		},
		Extra: map[string]any{
			"fi.mau.signal.gift_badge": giftBadge,
		},
	}
}

func (mc *MessageConverter) convertContactToVCard(ctx context.Context, contact *signalpb.DataMessage_Contact, attMap AttachmentMap) vcard.Card {
	card := make(vcard.Card)
	card.SetValue(vcard.FieldVersion, "4.0")
	name := contact.GetName()
	if name.GetFamilyName() != "" || name.GetGivenName() != "" {
		card.SetName(&vcard.Name{
			FamilyName:      name.GetFamilyName(),
			GivenName:       name.GetGivenName(),
			AdditionalName:  name.GetMiddleName(),
			HonorificPrefix: name.GetPrefix(),
			HonorificSuffix: name.GetSuffix(),
		})
	}
	if name.GetNickname() != "" {
		card.SetValue(vcard.FieldNickname, name.GetNickname())
	}
	if contact.GetOrganization() != "" {
		card.SetValue(vcard.FieldOrganization, contact.GetOrganization())
	}
	for _, addr := range contact.GetAddress() {
		field := vcard.Field{
			Value: strings.Join([]string{
				addr.GetPobox(),
				"", // extended address,
				addr.GetStreet(),
				addr.GetCity(),
				addr.GetRegion(),
				addr.GetPostcode(),
				addr.GetCountry(),
				// TODO put neighborhood somewhere?
			}, ";"),
			Params: make(vcard.Params),
		}
		if addr.GetLabel() != "" {
			field.Params.Set("LABEL", addr.GetLabel())
		}
		field.Params.Set(vcard.ParamType, strings.ToLower(addr.GetType().String()))
		card.Add(vcard.FieldAddress, &field)
	}
	for _, email := range contact.GetEmail() {
		field := vcard.Field{
			Value:  email.GetValue(),
			Params: make(vcard.Params),
		}
		field.Params.Set(vcard.ParamType, strings.ToLower(email.GetType().String()))
		if email.GetLabel() != "" {
			field.Params.Set("LABEL", email.GetLabel())
		}
		card.Add(vcard.FieldEmail, &field)
	}
	for _, phone := range contact.GetNumber() {
		field := vcard.Field{
			Value:  phone.GetValue(),
			Params: make(vcard.Params),
		}
		field.Params.Set(vcard.ParamType, strings.ToLower(phone.GetType().String()))
		if phone.GetLabel() != "" {
			field.Params.Set("LABEL", phone.GetLabel())
		}
		card.Add(vcard.FieldTelephone, &field)
	}
	if contact.GetAvatar().GetAvatar() != nil {
		avatarData, err := mc.downloadAttachment(ctx, contact.GetAvatar().GetAvatar(), attMap)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to download contact avatar")
		} else {
			mimeType := contact.GetAvatar().GetAvatar().GetContentType()
			if mimeType == "" {
				mimeType = http.DetectContentType(avatarData)
			}
			card.SetValue(vcard.FieldPhoto, fmt.Sprintf("data:%s;base64,%s", mimeType, base64.StdEncoding.EncodeToString(avatarData)))
		}
	}
	return card
}

func (mc *MessageConverter) convertContactToMatrix(ctx context.Context, contact *signalpb.DataMessage_Contact, attMap AttachmentMap) *bridgev2.ConvertedMessagePart {
	card := mc.convertContactToVCard(ctx, contact, attMap)
	contact.Avatar = nil
	extraData := map[string]any{
		"fi.mau.signal.contact": contact,
	}
	var buf bytes.Buffer
	err := vcard.NewEncoder(&buf).Encode(card)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to encode vCard")
		return &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgNotice,
				Body:    "Failed to encode vCard",
			},
			Extra: extraData,
		}
	}
	data := buf.Bytes()
	displayName := contact.GetName().GetNickname()
	if displayName == "" {
		displayName = contact.GetName().GetGivenName()
		if contact.GetName().GetFamilyName() != "" {
			if displayName != "" {
				displayName += " "
			}
			displayName += contact.GetName().GetFamilyName()
		}
	}
	if displayName == "" {
		displayName = "contact"
	}
	content := &event.MessageEventContent{
		MsgType: event.MsgFile,
		Body:    displayName + ".vcf",
		Info: &event.FileInfo{
			MimeType: "text/vcf",
			Size:     len(data),
		},
	}
	content.URL, content.File, err = getIntent(ctx).UploadMedia(ctx, getPortal(ctx).MXID, data, content.Info.MimeType, content.Body)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to upload vCard")
		return &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgNotice,
				Body:    "Failed to upload vCard",
			},
			Extra: extraData,
		}
	}
	return &bridgev2.ConvertedMessagePart{
		Type:    event.EventMessage,
		Content: content,
		Extra:   extraData,
	}
}

func (mc *MessageConverter) convertAttachmentToMatrix(ctx context.Context, index int, att *signalpb.AttachmentPointer, attMap AttachmentMap) *bridgev2.ConvertedMessagePart {
	part, err := mc.reuploadAttachment(ctx, att, attMap)
	if err != nil {
		if (errors.Is(err, signalmeow.ErrAttachmentNotFound) || errors.Is(err, ErrAttachmentNotInBackup)) && attMap != nil {
			return &bridgev2.ConvertedMessagePart{
				Type: event.EventMessage,
				Content: &event.MessageEventContent{
					MsgType: event.MsgNotice,
					Body:    fmt.Sprintf("Attachment no longer available %s", att.GetFileName()),
				},
			}
		} else if errors.Is(err, ErrBackupNotSupported) {
			return &bridgev2.ConvertedMessagePart{
				Type: event.EventMessage,
				Content: &event.MessageEventContent{
					MsgType: event.MsgNotice,
					Body:    "Downloading attachments from backup is not yet supported",
				},
			}
		}
		zerolog.Ctx(ctx).Err(err).Int("attachment_index", index).Msg("Failed to handle attachment")
		return &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgNotice,
				Body:    fmt.Sprintf("Failed to handle attachment %s: %v", att.GetFileName(), err),
			},
		}
	}
	return part
}

func (mc *MessageConverter) convertStickerToMatrix(ctx context.Context, sticker *signalpb.DataMessage_Sticker, attMap AttachmentMap) *bridgev2.ConvertedMessagePart {
	converted, err := mc.reuploadAttachment(ctx, sticker.GetData(), attMap)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to handle sticker")
		return &bridgev2.ConvertedMessagePart{
			Type: event.EventMessage,
			Content: &event.MessageEventContent{
				MsgType: event.MsgNotice,
				Body:    fmt.Sprintf("Failed to handle sticker: %v", err),
			},
		}
	}
	// Signal stickers are 512x512, so tell Matrix clients to render them as 200x200 to match Signal
	// https://github.com/signalapp/Signal-Desktop/blob/v7.77.0-beta.1/ts/components/conversation/Message.dom.tsx#L135
	if converted.Content.Info.Width == 512 && converted.Content.Info.Height == 512 {
		converted.Content.Info.Width = 200
		converted.Content.Info.Height = 200
	}
	converted.Content.Body = sticker.GetEmoji()
	converted.Type = event.EventSticker
	converted.Content.MsgType = ""
	if converted.Extra == nil {
		converted.Extra = map[string]any{}
	}
	// TODO fetch full pack metadata like the old bridge did?
	converted.Extra["fi.mau.signal.sticker"] = map[string]any{
		"id":    sticker.GetStickerId(),
		"emoji": sticker.GetEmoji(),
		"pack": map[string]any{
			"id":  sticker.GetPackId(),
			"key": sticker.GetPackKey(),
		},
	}
	return converted
}

func (mc *MessageConverter) downloadSignalLongText(ctx context.Context, att *signalpb.AttachmentPointer, attMap AttachmentMap) (*string, error) {
	data, err := mc.downloadAttachment(ctx, att, attMap)
	if err != nil {
		return nil, err
	}
	longBody := string(data)
	return &longBody, nil
}

func checkIfAttachmentExists(att *signalpb.AttachmentPointer, attMap AttachmentMap) error {
	if att.AttachmentIdentifier == nil {
		if len(att.GetClientUuid()) != 16 {
			return fmt.Errorf("no attachment identifier found")
		}
		target, ok := attMap[uuid.UUID(att.GetClientUuid())]
		if !ok {
			return fmt.Errorf("no attachment identifier and attachment not found in map")
		} else if target == nil || target.MediaTierCdnNumber == nil {
			return ErrAttachmentNotInBackup
		} else {
			// TODO add support for downloading attachments from backup
			return ErrBackupNotSupported
		}
	}
	return nil
}

func (mc *MessageConverter) downloadAttachment(ctx context.Context, att *signalpb.AttachmentPointer, attMap AttachmentMap) ([]byte, error) {
	if err := checkIfAttachmentExists(att, attMap); err != nil {
		return nil, err
	}
	var plaintextHash []byte
	if len(att.GetClientUuid()) == 16 {
		target, ok := attMap[uuid.UUID(att.GetClientUuid())]
		if ok {
			plaintextHash = target.GetPlaintextHash()
		}
	}
	return signalmeow.DownloadAttachmentWithPointer(ctx, att, plaintextHash)
}

func (mc *MessageConverter) reuploadAttachment(ctx context.Context, att *signalpb.AttachmentPointer, attMap AttachmentMap) (*bridgev2.ConvertedMessagePart, error) {
	fileName := att.GetFileName()
	content := &event.MessageEventContent{
		Info: &event.FileInfo{
			Width:  int(att.GetWidth()),
			Height: int(att.GetHeight()),
			Size:   int(att.GetSize()),
		},
	}
	mimeType := att.GetContentType()
	if err := checkIfAttachmentExists(att, attMap); err != nil {
		return nil, err
	} else if mc.DirectMedia {
		digest := att.Digest
		var plaintextDigest bool
		if digest == nil && len(att.GetClientUuid()) == 16 {
			locatorInfo, ok := attMap[uuid.UUID(att.GetClientUuid())]
			if ok {
				digest = locatorInfo.GetPlaintextHash()
				plaintextDigest = true
			}
		}
		mediaID, err := signalid.DirectMediaAttachment{
			CDNID:           att.GetCdnId(),
			CDNKey:          att.GetCdnKey(),
			CDNNumber:       att.GetCdnNumber(),
			Key:             att.Key,
			Digest:          digest,
			PlaintextDigest: plaintextDigest,
			Size:            att.GetSize(),
		}.AsMediaID()
		if err != nil {
			return nil, err
		}
		content.URL, err = mc.Bridge.Matrix.GenerateContentURI(ctx, mediaID)
	} else {
		data, err := mc.downloadAttachment(ctx, att, attMap)
		if err != nil {
			return nil, err
		}
		if mimeType == "" {
			mimeType = http.DetectContentType(data)
		}
		if att.GetFlags()&uint32(signalpb.AttachmentPointer_VOICE_MESSAGE) != 0 && ffmpeg.Supported() {
			data, err = ffmpeg.ConvertBytes(ctx, data, ".ogg", []string{}, []string{"-c:a", "libopus"}, mimeType)
			if err != nil {
				return nil, fmt.Errorf("failed to convert audio to ogg/opus: %w", err)
			}
			fileName += ".ogg"
			mimeType = "audio/ogg"
			content.MSC3245Voice = &event.MSC3245Voice{}
			// TODO include duration here (and in info) if there's some easy way to extract it with ffmpeg
			//content.MSC1767Audio = &event.MSC1767Audio{}
		}
		content.URL, content.File, err = getIntent(ctx).UploadMedia(ctx, getPortal(ctx).MXID, data, fileName, mimeType)
		if err != nil {
			return nil, err
		}
	}
	if att.GetBlurHash() != "" {
		content.Info.Blurhash = att.GetBlurHash()
		content.Info.AnoaBlurhash = att.GetBlurHash()
	}
	switch strings.Split(mimeType, "/")[0] {
	case "image":
		content.MsgType = event.MsgImage
	case "video":
		content.MsgType = event.MsgVideo
	case "audio":
		content.MsgType = event.MsgAudio
	default:
		content.MsgType = event.MsgFile
	}
	var extra map[string]any
	if att.GetFlags()&uint32(signalpb.AttachmentPointer_GIF) != 0 {
		content.Info.MauGIF = true
		extra = map[string]any{
			"info": map[string]any{
				"fi.mau.loop":          true,
				"fi.mau.autoplay":      true,
				"fi.mau.hide_controls": true,
				"fi.mau.no_audio":      true,
			},
		}
	}
	content.Body = fileName
	content.Info.MimeType = mimeType
	if content.Body == "" {
		content.Body = strings.TrimPrefix(string(content.MsgType), "m.") + exmime.ExtensionFromMimetype(mimeType)
	}
	return &bridgev2.ConvertedMessagePart{
		Type:    event.EventMessage,
		Content: content,
		Extra:   extra,
	}, nil
}

func (mc *MessageConverter) convertPollCreateToMatrix(create *signalpb.DataMessage_PollCreate) *bridgev2.ConvertedMessagePart {
	evtType := event.EventMessage
	if mc.ExtEvPolls {
		evtType = event.EventUnstablePollStart
	}
	maxChoices := 1
	if create.GetAllowMultiple() {
		maxChoices = len(create.GetOptions())
	}
	msc3381Answers := make([]map[string]any, len(create.GetOptions()))
	optionsListText := make([]string, len(create.GetOptions()))
	optionsListHTML := make([]string, len(create.GetOptions()))
	for i, option := range create.GetOptions() {
		msc3381Answers[i] = map[string]any{
			"id":                      strconv.Itoa(i),
			"org.matrix.msc1767.text": option,
		}
		optionsListText[i] = fmt.Sprintf("%d. %s\n", i+1, option)
		optionsListHTML[i] = fmt.Sprintf("<li>%s</li>", event.TextToHTML(option))
	}
	body := fmt.Sprintf("%s\n\n%s\n\n(This message is a poll. Please open Signal to vote.)", create.GetQuestion(), strings.Join(optionsListText, "\n"))
	formattedBody := fmt.Sprintf("<p>%s</p><ol>%s</ol><p>(This message is a poll. Please open Signal to vote.)</p>", event.TextToHTML(create.GetQuestion()), strings.Join(optionsListHTML, ""))
	return &bridgev2.ConvertedMessagePart{
		Type: evtType,
		Content: &event.MessageEventContent{
			MsgType:       event.MsgText,
			Body:          body,
			Format:        event.FormatHTML,
			FormattedBody: formattedBody,
		},
		Extra: map[string]any{
			"fi.mau.signal.poll": map[string]any{
				"question":       create.GetQuestion(),
				"allow_multiple": create.GetAllowMultiple(),
				"options":        create.GetOptions(),
			},
			"org.matrix.msc1767.message": []map[string]any{
				{"mimetype": "text/html", "body": formattedBody},
				{"mimetype": "text/plain", "body": body},
			},
			"org.matrix.msc3381.poll.start": map[string]any{
				"kind":           "org.matrix.msc3381.poll.disclosed",
				"max_selections": maxChoices,
				"question": map[string]any{
					"org.matrix.msc1767.text": create.GetQuestion(),
				},
				"answers": msc3381Answers,
			},
		},
		DBMetadata: nil,
		DontBridge: false,
	}
}

func (mc *MessageConverter) convertPollTerminateToMatrix(ctx context.Context, senderACI uuid.UUID, terminate *signalpb.DataMessage_PollTerminate) *bridgev2.ConvertedMessagePart {
	pollMessageID := signalid.MakeMessageID(senderACI, terminate.GetTargetSentTimestamp())
	pollMessage, err := mc.Bridge.DB.Message.GetPartByID(ctx, getPortal(ctx).Receiver, pollMessageID, "")
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to get poll terminate target message")
		return &bridgev2.ConvertedMessagePart{
			Type:       event.EventUnstablePollEnd,
			Content:    &event.MessageEventContent{},
			DontBridge: true,
		}
	}
	return &bridgev2.ConvertedMessagePart{
		Type: event.EventUnstablePollEnd,
		Content: &event.MessageEventContent{
			RelatesTo: &event.RelatesTo{
				Type:    event.RelReference,
				EventID: pollMessage.MXID,
			},
		},
		Extra: map[string]any{
			"org.matrix.msc3381.poll.end": map[string]any{},
		},
	}
}

var invalidPollVote = &bridgev2.ConvertedMessagePart{
	Type:       event.EventUnstablePollResponse,
	Content:    &event.MessageEventContent{},
	DontBridge: true,
}

func (mc *MessageConverter) convertPollVoteToMatrix(ctx context.Context, vote *signalpb.DataMessage_PollVote) *bridgev2.ConvertedMessagePart {
	if len(vote.GetTargetAuthorAciBinary()) != 16 {
		zerolog.Ctx(ctx).Debug().
			Str("author_aci_b64", base64.StdEncoding.EncodeToString(vote.GetTargetAuthorAciBinary())).
			Msg("Invalid author ACI in poll vote")
		return invalidPollVote
	}
	pollMessageID := signalid.MakeMessageID(uuid.UUID(vote.GetTargetAuthorAciBinary()), vote.GetTargetSentTimestamp())
	pollMessage, err := mc.Bridge.DB.Message.GetPartByID(ctx, getPortal(ctx).Receiver, pollMessageID, "")
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to get poll vote target message")
		return invalidPollVote
	} else if pollMessage == nil {
		zerolog.Ctx(ctx).Warn().Msg("Poll vote target message not found")
		return invalidPollVote
	}
	mxOptionIDs := pollMessage.Metadata.(*signalid.MessageMetadata).MatrixPollOptionIDs
	optionIDs := make([]string, len(vote.GetOptionIndexes()))
	for i, optionIndex := range vote.GetOptionIndexes() {
		if int(optionIndex) < len(mxOptionIDs) {
			optionIDs[i] = mxOptionIDs[optionIndex]
		} else {
			optionIDs[i] = strconv.Itoa(int(optionIndex))
		}
	}
	return &bridgev2.ConvertedMessagePart{
		Type: event.EventUnstablePollResponse,
		Content: &event.MessageEventContent{
			RelatesTo: &event.RelatesTo{
				Type:    event.RelReference,
				EventID: pollMessage.MXID,
			},
		},
		Extra: map[string]any{
			"org.matrix.msc3381.poll.response": map[string]any{
				"answers": optionIDs,
			},
		},
	}
}
