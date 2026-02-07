// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2025 Tulir Asokan
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
	"slices"

	"github.com/google/uuid"
	"go.mau.fi/util/exslices"
	"go.mau.fi/util/ptr"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
)

func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

type AttachmentMap map[uuid.UUID]*backuppb.FilePointer_LocatorInfo

func BackupToDataMessage(ci *backuppb.ChatItem, attMap AttachmentMap) (*signalpb.DataMessage, []*backuppb.Reaction) {
	var dm signalpb.DataMessage
	var reactions []*backuppb.Reaction
	switch ti := ci.Item.(type) {
	case *backuppb.ChatItem_StandardMessage:
		reactions = ti.StandardMessage.Reactions
		if text := ti.StandardMessage.Text; text != nil {
			dm.Body = &text.Body
			dm.BodyRanges = slices.DeleteFunc(exslices.CastFunc(text.BodyRanges, backupToSignalBodyRange), deleteNil)
		}
		dm.Attachments = make([]*signalpb.AttachmentPointer, 0, len(ti.StandardMessage.Attachments)+boolToInt(ti.StandardMessage.LongText != nil))
		if ti.StandardMessage.LongText != nil {
			randomUUID := uuid.New()
			dm.Attachments = append(
				dm.Attachments,
				backupToSignalAttachment(ti.StandardMessage.LongText, 0, randomUUID, attMap),
			)
		}
		for _, att := range ti.StandardMessage.Attachments {
			var clientUUID uuid.UUID
			if len(att.ClientUuid) == 16 {
				clientUUID = uuid.UUID(att.ClientUuid)
			} else {
				clientUUID = uuid.New()
			}
			dm.Attachments = append(
				dm.Attachments,
				backupToSignalAttachment(att.Pointer, att.Flag, clientUUID, attMap),
			)
		}
		dm.Preview = exslices.CastFunc(ti.StandardMessage.LinkPreview, func(from *backuppb.LinkPreview) *signalpb.Preview {
			return backupToSignalLinkPreview(from, attMap)
		})
	case *backuppb.ChatItem_ContactMessage:
		reactions = ti.ContactMessage.Reactions
		dm.Contact = []*signalpb.DataMessage_Contact{backupToSignalContact(ti.ContactMessage.Contact, attMap)}
	case *backuppb.ChatItem_StickerMessage:
		reactions = ti.StickerMessage.Reactions
		dm.Sticker = &signalpb.DataMessage_Sticker{
			PackId:    ti.StickerMessage.Sticker.PackId,
			PackKey:   ti.StickerMessage.Sticker.PackKey,
			StickerId: &ti.StickerMessage.Sticker.StickerId,
			Emoji:     ti.StickerMessage.Sticker.Emoji,
			Data:      backupToSignalAttachment(ti.StickerMessage.Sticker.Data, 0, uuid.New(), attMap),
		}
	case *backuppb.ChatItem_Poll:
		dm.PollCreate = &signalpb.DataMessage_PollCreate{
			Question:      &ti.Poll.Question,
			AllowMultiple: &ti.Poll.AllowMultiple,
			Options: exslices.CastFunc(ti.Poll.Options, func(from *backuppb.Poll_PollOption) string {
				return from.Option
			}),
		}
		// TODO handle votes
		// TODO handle hasEnded somehow?
	case *backuppb.ChatItem_RemoteDeletedMessage:
		// TODO handle some other way? (also disappeared view-once messages)
		return nil, nil
	case *backuppb.ChatItem_PaymentNotification:
		dm.Payment = &signalpb.DataMessage_Payment{
			Item: &signalpb.DataMessage_Payment_Notification_{
				Notification: &signalpb.DataMessage_Payment_Notification{
					Transaction: nil,
					Note:        ti.PaymentNotification.Note,
				},
			},
		}
	case *backuppb.ChatItem_GiftBadge:
		dm.GiftBadge = &signalpb.DataMessage_GiftBadge{
			ReceiptCredentialPresentation: ti.GiftBadge.ReceiptCredentialPresentation,
		}
	case *backuppb.ChatItem_ViewOnceMessage:
		reactions = ti.ViewOnceMessage.Reactions
		if ti.ViewOnceMessage.Attachment == nil {
			// TODO handle some other way?
			return nil, reactions
		}
		dm.IsViewOnce = ptr.Ptr(true)
		var clientUUID uuid.UUID
		if len(ti.ViewOnceMessage.Attachment.ClientUuid) == 16 {
			clientUUID = uuid.UUID(ti.ViewOnceMessage.Attachment.ClientUuid)
		} else {
			clientUUID = uuid.New()
		}
		dm.Attachments = []*signalpb.AttachmentPointer{backupToSignalAttachment(
			ti.ViewOnceMessage.Attachment.Pointer,
			ti.ViewOnceMessage.Attachment.Flag,
			clientUUID,
			attMap,
		)}
	}
	return &dm, reactions
}

func backupToSignalContact(from *backuppb.ContactAttachment, attMap AttachmentMap) *signalpb.DataMessage_Contact {
	var contact signalpb.DataMessage_Contact
	if from.Name != nil {
		contact.Name = &signalpb.DataMessage_Contact_Name{
			GivenName:  &from.Name.GivenName,
			FamilyName: &from.Name.FamilyName,
			Prefix:     &from.Name.Prefix,
			Suffix:     &from.Name.Suffix,
			MiddleName: &from.Name.MiddleName,
			Nickname:   &from.Name.Nickname,
		}
	}
	contact.Number = exslices.CastFunc(from.Number, func(from *backuppb.ContactAttachment_Phone) *signalpb.DataMessage_Contact_Phone {
		return &signalpb.DataMessage_Contact_Phone{
			Value: &from.Value,
			Type:  ptr.NonZero(signalpb.DataMessage_Contact_Phone_Type(from.Type)),
			Label: &from.Label,
		}
	})
	contact.Email = exslices.CastFunc(from.Email, func(from *backuppb.ContactAttachment_Email) *signalpb.DataMessage_Contact_Email {
		return &signalpb.DataMessage_Contact_Email{
			Value: &from.Value,
			Type:  ptr.NonZero(signalpb.DataMessage_Contact_Email_Type(from.Type)),
			Label: &from.Label,
		}
	})
	contact.Address = exslices.CastFunc(from.Address, func(from *backuppb.ContactAttachment_PostalAddress) *signalpb.DataMessage_Contact_PostalAddress {
		return &signalpb.DataMessage_Contact_PostalAddress{
			Type:         ptr.NonZero(signalpb.DataMessage_Contact_PostalAddress_Type(from.Type)),
			Label:        &from.Label,
			Street:       &from.Street,
			Pobox:        &from.Pobox,
			Neighborhood: &from.Neighborhood,
			City:         &from.City,
			Region:       &from.Region,
			Postcode:     &from.Postcode,
			Country:      &from.Country,
		}
	})
	if from.Avatar != nil {
		contact.Avatar = &signalpb.DataMessage_Contact_Avatar{
			Avatar: backupToSignalAttachment(from.Avatar, 0, uuid.New(), attMap),
		}
	}
	contact.Organization = ptr.NonZero(from.Organization)
	return &contact
}

func backupToSignalLinkPreview(from *backuppb.LinkPreview, attMap AttachmentMap) *signalpb.Preview {
	var ap *signalpb.AttachmentPointer
	if from.Image != nil {
		ap = backupToSignalAttachment(from.Image, 0, uuid.New(), attMap)
	}
	return &signalpb.Preview{
		Url:         &from.Url,
		Title:       from.Title,
		Image:       ap,
		Description: from.Description,
		Date:        from.Date,
	}
}

func backupToSignalAttachment(
	fp *backuppb.FilePointer,
	flag backuppb.MessageAttachment_Flag,
	clientUUID uuid.UUID,
	atts AttachmentMap,
) *signalpb.AttachmentPointer {
	sig := &signalpb.AttachmentPointer{
		//IncrementalMacChunkSize: fp.IncrementalMacChunkSize,
		ContentType:    fp.ContentType,
		IncrementalMac: fp.IncrementalMac,
		FileName:       fp.FileName,
		Flags:          ptr.NonZero(uint32(backupToSignalAttachmentFlag(flag))),
		Width:          fp.Width,
		Height:         fp.Height,
		Caption:        nil, // is this field deprecated or something?
		BlurHash:       fp.BlurHash,
		ClientUuid:     clientUUID[:],
	}
	if fp.LocatorInfo != nil {
		if fp.LocatorInfo.TransitCdnKey != nil {
			sig.AttachmentIdentifier = &signalpb.AttachmentPointer_CdnKey{CdnKey: *fp.LocatorInfo.TransitCdnKey}
			sig.Size = &fp.LocatorInfo.Size
			sig.Digest = fp.LocatorInfo.GetEncryptedDigest() // Note: may be nil if plaintextHash is set instead
			sig.CdnNumber = fp.LocatorInfo.TransitCdnNumber
		}
		sig.Key = fp.LocatorInfo.Key
		atts[clientUUID] = fp.LocatorInfo
	}
	return sig
}

func backupToSignalAttachmentFlag(flag backuppb.MessageAttachment_Flag) signalpb.AttachmentPointer_Flags {
	switch flag {
	case backuppb.MessageAttachment_VOICE_MESSAGE:
		return signalpb.AttachmentPointer_VOICE_MESSAGE
	case backuppb.MessageAttachment_BORDERLESS:
		return signalpb.AttachmentPointer_BORDERLESS
	case backuppb.MessageAttachment_GIF:
		return signalpb.AttachmentPointer_GIF
	case backuppb.MessageAttachment_NONE:
		fallthrough
	default:
		return 0
	}
}

func deleteNil(bodyRange *signalpb.BodyRange) bool {
	return bodyRange == nil
}

func backupToSignalBodyRange(from *backuppb.BodyRange) *signalpb.BodyRange {
	var out signalpb.BodyRange
	out.Start = &from.Start
	out.Length = &from.Length
	switch av := from.AssociatedValue.(type) {
	case *backuppb.BodyRange_MentionAci:
		// TODO confirm this is correct
		if len(av.MentionAci) != 16 {
			return nil
		}
		out.AssociatedValue = &signalpb.BodyRange_MentionAci{MentionAci: uuid.UUID(av.MentionAci).String()}
	case *backuppb.BodyRange_Style_:
		out.AssociatedValue = &signalpb.BodyRange_Style_{Style: signalpb.BodyRange_Style(av.Style)}
	}
	return &out
}
