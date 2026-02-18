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

package connector

import (
	"context"
	"time"

	"go.mau.fi/util/ffmpeg"
	"go.mau.fi/util/jsontime"
	"go.mau.fi/util/ptr"

	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/event"
)

func supportedIfFFmpeg() event.CapabilitySupportLevel {
	if ffmpeg.Supported() {
		return event.CapLevelPartialSupport
	}
	return event.CapLevelRejected
}

func capID() string {
	base := "fi.mau.signal.capabilities.2025_12_09"
	if ffmpeg.Supported() {
		return base + "+ffmpeg"
	}
	return base
}

const MaxFileSize = 100 * 1024 * 1024
const MaxTextLength = 2000

var signalCaps = &event.RoomFeatures{
	ID: capID(),

	Formatting: map[event.FormattingFeature]event.CapabilitySupportLevel{
		// Features that Signal supports natively
		event.FmtBold:          event.CapLevelFullySupported,
		event.FmtItalic:        event.CapLevelFullySupported,
		event.FmtStrikethrough: event.CapLevelFullySupported,
		event.FmtSpoiler:       event.CapLevelFullySupported,
		event.FmtInlineCode:    event.CapLevelFullySupported,
		event.FmtCodeBlock:     event.CapLevelFullySupported,
		event.FmtUserLink:      event.CapLevelFullySupported,

		// Features that aren't supported on Signal, but are converted into a markdown-like representation
		event.FmtBlockquote:    event.CapLevelPartialSupport,
		event.FmtInlineLink:    event.CapLevelPartialSupport,
		event.FmtUnorderedList: event.CapLevelPartialSupport,
		event.FmtOrderedList:   event.CapLevelPartialSupport,
		event.FmtListStart:     event.CapLevelPartialSupport,
		event.FmtHeaders:       event.CapLevelPartialSupport,
	},
	File: map[event.CapabilityMsgType]*event.FileFeatures{
		event.MsgImage: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"image/gif":  event.CapLevelFullySupported,
				"image/png":  event.CapLevelFullySupported,
				"image/jpeg": event.CapLevelFullySupported,
				"image/webp": event.CapLevelFullySupported,
				"image/bmp":  event.CapLevelFullySupported,
			},
			MaxWidth:         4096,
			MaxHeight:        4096,
			MaxSize:          MaxFileSize,
			Caption:          event.CapLevelFullySupported,
			MaxCaptionLength: MaxTextLength,
		},
		event.MsgVideo: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"video/mp4":  event.CapLevelFullySupported,
				"video/ogg":  event.CapLevelFullySupported,
				"video/webm": event.CapLevelFullySupported,
			},
			MaxSize:          MaxFileSize,
			Caption:          event.CapLevelFullySupported,
			MaxCaptionLength: MaxTextLength,
		},
		event.MsgAudio: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"audio/aac":  event.CapLevelFullySupported,
				"audio/mpeg": event.CapLevelFullySupported,
			},
			MaxSize: MaxFileSize,
		},
		event.MsgFile: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"*/*": event.CapLevelFullySupported,
			},
			MaxSize:          MaxFileSize,
			Caption:          event.CapLevelFullySupported,
			MaxCaptionLength: MaxTextLength,
		},
		event.CapMsgSticker: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"image/webp": event.CapLevelFullySupported,
				"image/png":  event.CapLevelFullySupported,
				"image/apng": event.CapLevelFullySupported,
				"image/gif":  supportedIfFFmpeg(),
			},
			Caption: event.CapLevelDropped,
			MaxSize: MaxFileSize,
		},
		event.CapMsgVoice: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"audio/aac": event.CapLevelFullySupported,
				"audio/ogg": supportedIfFFmpeg(),
			},
			Caption:     event.CapLevelDropped,
			MaxSize:     MaxFileSize,
			MaxDuration: ptr.Ptr(jsontime.S(1 * time.Hour)),
		},
		event.CapMsgGIF: {
			MimeTypes: map[string]event.CapabilitySupportLevel{
				"image/gif": event.CapLevelFullySupported,
				"video/mp4": event.CapLevelFullySupported,
			},
			Caption: event.CapLevelFullySupported,
			MaxSize: MaxFileSize,
		},
	},
	State: event.StateFeatureMap{
		event.StateRoomName.Type:                {Level: event.CapLevelFullySupported},
		event.StateRoomAvatar.Type:              {Level: event.CapLevelFullySupported},
		event.StateTopic.Type:                   {Level: event.CapLevelFullySupported},
		event.StateBeeperDisappearingTimer.Type: {Level: event.CapLevelFullySupported},
	},
	MemberActions: event.MemberFeatureMap{
		event.MemberActionInvite:       event.CapLevelFullySupported,
		event.MemberActionRevokeInvite: event.CapLevelFullySupported,
		event.MemberActionLeave:        event.CapLevelFullySupported,
		event.MemberActionBan:          event.CapLevelFullySupported,
		event.MemberActionKick:         event.CapLevelFullySupported,
	},
	MaxTextLength:     MaxTextLength, // TODO support arbitrary sized text messages with files
	LocationMessage:   event.CapLevelPartialSupport,
	Poll:              event.CapLevelRejected,
	Thread:            event.CapLevelUnsupported,
	Reply:             event.CapLevelFullySupported,
	Edit:              event.CapLevelFullySupported,
	EditMaxCount:      10,
	EditMaxAge:        ptr.Ptr(jsontime.S(24 * time.Hour)),
	Delete:            event.CapLevelFullySupported,
	DeleteForMe:       false,
	DeleteMaxAge:      ptr.Ptr(jsontime.S(24 * time.Hour)),
	DisappearingTimer: signalDisappearingCap,

	Reaction:             event.CapLevelFullySupported,
	ReactionCount:        1,
	AllowedReactions:     nil,
	CustomEmojiReactions: false,
	ReadReceipts:         true,
	TypingNotifications:  true,

	DeleteChat: true,
	MessageRequest: &event.MessageRequestFeatures{
		AcceptWithMessage: event.CapLevelPartialSupport,
		AcceptWithButton:  event.CapLevelFullySupported,
	},
}

var signalDisappearingCap = &event.DisappearingTimerCapability{
	Types: []event.DisappearingType{event.DisappearingTypeAfterRead},
}

var signalCapsNoteToSelf *event.RoomFeatures
var signalCapsDM *event.RoomFeatures

func init() {
	signalCapsDM = ptr.Clone(signalCaps)
	signalCapsDM.ID = capID() + "+dm"
	signalCapsDM.MemberActions = nil
	signalCapsDM.State = event.StateFeatureMap{
		event.StateBeeperDisappearingTimer.Type: {Level: event.CapLevelFullySupported},
	}
	signalCapsNoteToSelf = ptr.Clone(signalCapsDM)
	signalCapsNoteToSelf.EditMaxAge = nil
	signalCapsNoteToSelf.DeleteMaxAge = nil
	signalCapsNoteToSelf.ID = capID() + "+note_to_self"
}

func (s *SignalClient) GetCapabilities(ctx context.Context, portal *bridgev2.Portal) *event.RoomFeatures {
	if portal.Receiver == s.UserLogin.ID && portal.ID == networkid.PortalID(s.UserLogin.ID) {
		return signalCapsNoteToSelf
	} else if portal.RoomType == database.RoomTypeDM {
		return signalCapsDM
	}
	return signalCaps
}

var signalGeneralCaps = &bridgev2.NetworkGeneralCapabilities{
	DisappearingMessages: true,
	AggressiveUpdateInfo: true,
	ImplicitReadReceipts: true,
	Provisioning: bridgev2.ProvisioningCapabilities{
		ResolveIdentifier: bridgev2.ResolveIdentifierCapabilities{
			CreateDM:       true,
			LookupPhone:    true,
			LookupUsername: false, // TODO implement
			ContactList:    true,
		},
		GroupCreation: map[string]bridgev2.GroupTypeCapabilities{
			"group": {
				TypeDescription: "a group chat",

				Name:         bridgev2.GroupFieldCapability{Allowed: true, Required: true, MaxLength: 32},
				Avatar:       bridgev2.GroupFieldCapability{Allowed: true},
				Disappear:    bridgev2.GroupFieldCapability{Allowed: true, DisappearSettings: signalDisappearingCap},
				Participants: bridgev2.GroupFieldCapability{Allowed: true},
			},
		},
	},
}

func (s *SignalConnector) GetCapabilities() *bridgev2.NetworkGeneralCapabilities {
	return signalGeneralCaps
}

func (s *SignalConnector) GetBridgeInfoVersion() (info, capabilities int) {
	return 1, 7
}
