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

package connector

import (
	"context"
	"crypto/sha256"
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/ptr"
	"maunium.net/go/mautrix"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

var (
	_ bridgev2.IdentifierResolvingNetworkAPI = (*SignalClient)(nil)
	_ bridgev2.GroupCreatingNetworkAPI       = (*SignalClient)(nil)
	_ bridgev2.ContactListingNetworkAPI      = (*SignalClient)(nil)
	_ bridgev2.GhostDMCreatingNetworkAPI     = (*SignalClient)(nil)
)

var _ bridgev2.IdentifierValidatingNetwork = (*SignalConnector)(nil)

const PrivateChatTopic = "Signal private chat"
const NoteToSelfName = "Signal Note to Self"

func (s *SignalClient) GetUserInfoWithRefreshAfter(ctx context.Context, ghost *bridgev2.Ghost, refreshAfter time.Duration) (*bridgev2.UserInfo, error) {
	userID, err := signalid.ParseUserIDAsServiceID(ghost.ID)
	if err != nil {
		return nil, err
	}
	if ghost.Name != "" && s.Main.Bridge.Background {
		// Don't do unnecessary fetches in background mode
		return nil, nil
	}
	var contact *types.Recipient
	if userID.Type == libsignalgo.ServiceIDTypePNI {
		contact, err = s.Client.Store.RecipientStore.LoadAndUpdateRecipient(ctx, uuid.Nil, userID.UUID, nil)
	} else {
		contact, err = s.Client.ContactByACIWithRefreshAfter(ctx, userID.UUID, refreshAfter)
	}
	if err != nil {
		return nil, err
	}
	meta := ghost.Metadata.(*signalid.GhostMetadata)
	if userID.Type != libsignalgo.ServiceIDTypePNI && (!s.Main.Config.UseOutdatedProfiles && meta.ProfileFetchedAt.After(contact.Profile.FetchedAt)) {
		return nil, nil
	}
	return s.contactToUserInfo(ctx, contact)
}

func (s *SignalClient) GetUserInfo(ctx context.Context, ghost *bridgev2.Ghost) (*bridgev2.UserInfo, error) {
	return s.GetUserInfoWithRefreshAfter(ctx, ghost, signalmeow.DefaultProfileRefreshAfter)
}

func (s *SignalClient) GetChatInfo(ctx context.Context, portal *bridgev2.Portal) (*bridgev2.ChatInfo, error) {
	userID, groupID, err := signalid.ParsePortalID(portal.ID)
	if err != nil {
		return nil, fmt.Errorf("failed to parse portal id: %w", err)
	}
	if groupID != "" {
		return s.getGroupInfo(ctx, groupID, 0, nil)
	} else {
		aci, pni := userID.ToACIAndPNI()
		contact, err := s.Client.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aci, pni, nil)
		if err != nil {
			return nil, err
		}
		return s.makeCreateDMResponse(ctx, contact, nil).PortalInfo, nil
	}
}

func (s *SignalClient) contactToUserInfo(ctx context.Context, contact *types.Recipient) (*bridgev2.UserInfo, error) {
	isBot := false
	ui := &bridgev2.UserInfo{
		IsBot:       &isBot,
		Identifiers: []string{},
		ExtraUpdates: func(ctx context.Context, ghost *bridgev2.Ghost) (changed bool) {
			meta := ghost.Metadata.(*signalid.GhostMetadata)
			if meta.ProfileFetchedAt.Before(contact.Profile.FetchedAt) {
				changed = meta.ProfileFetchedAt.IsZero() && !contact.Profile.FetchedAt.IsZero()
				meta.ProfileFetchedAt.Time = contact.Profile.FetchedAt
			}
			return false
		},
	}
	if contact.E164 != "" {
		ui.Identifiers = append(ui.Identifiers, "tel:"+contact.E164)
	}
	name := s.Main.Config.FormatDisplayname(contact)
	ui.Name = &name
	if s.Main.Config.UseContactAvatars && contact.ContactAvatar.Hash != "" {
		ui.Avatar = &bridgev2.Avatar{
			ID: networkid.AvatarID("hash:" + contact.ContactAvatar.Hash),
			Get: func(ctx context.Context) ([]byte, error) {
				if contact.ContactAvatar.Image == nil {
					return nil, fmt.Errorf("contact avatar not available")
				}
				return contact.ContactAvatar.Image, nil
			},
		}
	} else if contact.Profile.AvatarPath == "clear" {
		ui.Avatar = &bridgev2.Avatar{
			ID:     "",
			Remove: true,
		}
	} else if contact.Profile.AvatarPath != "" {
		ui.Avatar = &bridgev2.Avatar{
			ID: makeAvatarPathID(contact.Profile.AvatarPath),
		}

		if s.Main.MsgConv.DirectMedia {
			userID, err := signalid.ParseUserLoginID(s.UserLogin.ID)
			if err != nil {
				return nil, fmt.Errorf("failed to parse user login ID: %w", err)
			}
			mediaID, err := signalid.DirectMediaProfileAvatar{
				UserID:            userID,
				ContactID:         contact.ACI,
				ProfileAvatarPath: contact.Profile.AvatarPath,
			}.AsMediaID()
			if err != nil {
				return nil, err
			}
			ui.Avatar.MXC, err = s.Main.Bridge.Matrix.GenerateContentURI(ctx, mediaID)
			if err != nil {
				return nil, err
			}
			ui.Avatar.Hash = signalid.HashMediaID(mediaID)
		} else {
			ui.Avatar.Get = func(ctx context.Context) ([]byte, error) {
				return s.Client.DownloadUserAvatar(ctx, contact.Profile.AvatarPath, contact.Profile.Key)
			}
		}
	}
	return ui, nil
}

func (s *SignalConnector) ValidateUserID(id networkid.UserID) bool {
	_, err := signalid.ParseUserIDAsServiceID(id)
	return err == nil
}

func (s *SignalClient) CreateChatWithGhost(ctx context.Context, ghost *bridgev2.Ghost) (*bridgev2.CreateChatResponse, error) {
	parsedID, err := signalid.ParseUserIDAsServiceID(ghost.ID)
	if err != nil {
		return nil, err
	}
	resp, err := s.ResolveIdentifier(ctx, parsedID.String(), true)
	if err != nil {
		return nil, err
	} else if resp == nil {
		return nil, nil
	}
	resultID, err := signalid.ParseUserIDAsServiceID(resp.UserID)
	if err != nil {
		return nil, fmt.Errorf("failed to parse result user ID: %w", err)
	}
	if parsedID.Type == libsignalgo.ServiceIDTypePNI {
		if resultID.Type == libsignalgo.ServiceIDTypeACI && !resultID.IsEmpty() {
			resp.Chat.DMRedirectedTo = resp.UserID
		} else {
			resp.Chat.DMRedirectedTo = bridgev2.SpecialValueDMRedirectedToBot
		}
	}
	return resp.Chat, nil
}

func (s *SignalClient) ResolveIdentifier(ctx context.Context, number string, _ bool) (*bridgev2.ResolveIdentifierResponse, error) {
	var aci, pni uuid.UUID
	var e164Number uint64
	var recipient *types.Recipient
	serviceID, err := signalid.ParseUserIDAsServiceID(networkid.UserID(number))
	if err != nil {
		number, err = bridgev2.CleanPhoneNumber(number)
		if err != nil {
			return nil, bridgev2.WrapRespErr(err, mautrix.MInvalidParam)
		}
		e164Number, err = strconv.ParseUint(strings.TrimPrefix(number, "+"), 10, 64)
		if err != nil {
			return nil, bridgev2.WrapRespErr(fmt.Errorf("error parsing phone number: %w", err), mautrix.MInvalidParam)
		}
		e164String := fmt.Sprintf("+%d", e164Number)
		if recipient, err = s.Client.ContactByE164(ctx, e164String); err != nil {
			return nil, fmt.Errorf("error looking up number in local contact list: %w", err)
		} else if recipient != nil && (recipient.ACI == uuid.Nil || !s.Client.Store.RecipientStore.IsUnregistered(ctx, libsignalgo.NewACIServiceID(recipient.ACI))) {
			aci = recipient.ACI
			pni = recipient.PNI
		} else if resp, err := s.Client.LookupPhone(ctx, e164Number); err != nil {
			return nil, fmt.Errorf("error looking up number on server: %w", err)
		} else {
			aci = resp[e164Number].ACI
			pni = resp[e164Number].PNI
			if aci == uuid.Nil && pni == uuid.Nil {
				return nil, nil
			}
			recipient, err = s.Client.Store.RecipientStore.UpdateRecipientE164(ctx, aci, pni, e164String)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to save recipient entry after looking up phone")
			}
			aci, pni = recipient.ACI, recipient.PNI
			if aci != uuid.Nil {
				s.Client.Store.RecipientStore.MarkUnregistered(ctx, libsignalgo.NewACIServiceID(aci), false)
			}
		}
	} else {
		aci, pni = serviceID.ToACIAndPNI()
		recipient, err = s.Client.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aci, pni, nil)
		if err != nil {
			return nil, fmt.Errorf("error loading recipient: %w", err)
		}
	}
	zerolog.Ctx(ctx).Debug().
		Uint64("e164", e164Number).
		Stringer("aci", aci).
		Stringer("pni", pni).
		Msg("Found resolve identifier target user")

	userInfo, err := s.contactToUserInfo(ctx, recipient)
	if err != nil {
		return nil, fmt.Errorf("failed to convert contact: %w", err)
	}

	var userID networkid.UserID
	if aci != uuid.Nil {
		userID = signalid.MakeUserID(aci)
	} else {
		userID = signalid.MakeUserIDFromServiceID(libsignalgo.NewPNIServiceID(pni))
	}
	// createChat is a no-op: chats don't need to be created, and we always return chat info
	resp := &bridgev2.ResolveIdentifierResponse{
		UserID:   userID,
		UserInfo: userInfo,
		Chat:     s.makeCreateDMResponse(ctx, recipient, nil),
	}
	resp.Ghost, err = s.Main.Bridge.GetGhostByID(ctx, resp.UserID)
	if err != nil {
		return nil, fmt.Errorf("failed to get ghost: %w", err)
	}
	return resp, nil
}

func (s *SignalClient) CreateGroup(ctx context.Context, params *bridgev2.GroupCreateParams) (*bridgev2.CreateChatResponse, error) {
	group := &signalmeow.Group{
		Title:                        ptr.Val(params.Name).Name,
		Members:                      make([]*signalmeow.GroupMember, 1, len(params.Participants)+1),
		Description:                  ptr.Val(params.Topic).Topic,
		AnnouncementsOnly:            false,
		DisappearingMessagesDuration: uint32(ptr.Val(params.Disappear).Timer.Seconds()),
		AccessControl: &signalmeow.GroupAccessControl{
			Members:           signalmeow.AccessControl_MEMBER,
			AddFromInviteLink: signalmeow.AccessControl_UNSATISFIABLE,
			Attributes:        signalmeow.AccessControl_ADMINISTRATOR,
		},
	}
	var pl *event.PowerLevelsEventContent
	// TODO actually get PLs
	if pl != nil {
		if pl.EventsDefault > pl.UsersDefault {
			group.AnnouncementsOnly = true
		}
		if pl.Invite() > pl.UsersDefault {
			group.AccessControl.Members = signalmeow.AccessControl_ADMINISTRATOR
		}
		if pl.GetEventLevel(event.StateRoomName) <= pl.UsersDefault {
			group.AccessControl.Attributes = signalmeow.AccessControl_MEMBER
		}
	}
	group.Members[0] = &signalmeow.GroupMember{
		ACI:  s.Client.Store.ACI,
		Role: signalmeow.GroupMember_ADMINISTRATOR,
	}
	currentTS := uint64(time.Now().UnixMilli())
	for _, member := range params.Participants {
		userID, err := signalid.ParseUserIDAsServiceID(member)
		if err != nil {
			return nil, fmt.Errorf("invalid user ID %q: %w", member, err)
		}
		if userID.Type == libsignalgo.ServiceIDTypeACI {
			group.Members = append(group.Members, &signalmeow.GroupMember{
				ACI:  userID.UUID,
				Role: signalmeow.GroupMember_DEFAULT, // TODO set proper role from power levels
			})
		} else if userID.Type == libsignalgo.ServiceIDTypePNI {
			// TODO check if this is correct
			group.PendingMembers = append(group.PendingMembers, &signalmeow.PendingMember{
				ServiceID:     userID,
				Role:          signalmeow.GroupMember_DEFAULT,
				AddedByUserID: s.Client.Store.ACI,
				Timestamp:     currentTS,
			})
		}
	}
	_, err := signalmeow.PrepareGroupCreation(group)
	if err != nil {
		return nil, fmt.Errorf("failed to prepare group creation: %w", err)
	}
	var avatarBytes []byte
	var avatarMXC id.ContentURIString
	if params.Avatar != nil && params.Avatar.URL != "" {
		avatarMXC = params.Avatar.URL
		avatarBytes, err = s.Main.Bridge.Bot.DownloadMedia(ctx, params.Avatar.URL, nil)
		if err != nil {
			return nil, fmt.Errorf("failed to download avatar: %w", err)
		}
		group.AvatarPath, err = s.Client.UploadGroupAvatar(ctx, avatarBytes, group.GroupIdentifier)
		if err != nil {
			return nil, fmt.Errorf("failed to upload avatar: %w", err)
		}
	}
	portal, err := s.Main.Bridge.GetPortalByKey(ctx, s.makePortalKey(string(group.GroupIdentifier)))
	if err != nil {
		return nil, fmt.Errorf("failed to get portal: %w", err)
	}
	if params.RoomID != "" {
		err = portal.UpdateMatrixRoomID(ctx, params.RoomID, bridgev2.UpdateMatrixRoomIDParams{SyncDBMetadata: func() {
			portal.Name = group.Title
			portal.NameSet = true
			portal.Topic = group.Description
			portal.TopicSet = true
			portal.AvatarHash = sha256.Sum256(avatarBytes)
			portal.AvatarSet = true
			portal.AvatarMXC = avatarMXC
			portal.AvatarID = makeAvatarPathID(group.AvatarPath)
			if group.DisappearingMessagesDuration > 0 {
				portal.Disappear = database.DisappearingSetting{
					Type:  event.DisappearingTypeAfterRead,
					Timer: time.Duration(group.DisappearingMessagesDuration) * time.Second,
				}
			}
		}})
		if err != nil {
			return nil, fmt.Errorf("failed to set portal room ID: %w", err)
		}
	}
	resp, err := s.Client.CreateGroup(ctx, group, avatarBytes)
	if err != nil {
		return nil, fmt.Errorf("failed to create group: %w", err)
	}
	if params.RoomID != "" {
		// UpdateMatrixRoomID could do this for us if we passed ChatInfoSource to it,
		// but we only want to do it after the group is successfully created
		portal.UpdateBridgeInfo(ctx)
		portal.UpdateCapabilities(ctx, s.UserLogin, true)
	}
	wrappedInfo, err := s.wrapGroupInfo(ctx, resp, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to wrap group info for sync: %w", err)
	}
	return &bridgev2.CreateChatResponse{
		PortalKey:  portal.PortalKey,
		Portal:     portal,
		PortalInfo: wrappedInfo,
	}, nil
}

func (s *SignalClient) GetContactList(ctx context.Context) ([]*bridgev2.ResolveIdentifierResponse, error) {
	recipients, err := s.Client.Store.RecipientStore.LoadAllContacts(ctx)
	if err != nil {
		return nil, err
	}
	resp := make([]*bridgev2.ResolveIdentifierResponse, len(recipients))
	for i, recipient := range recipients {
		userInfo, err := s.contactToUserInfo(ctx, recipient)
		if err != nil {
			return nil, fmt.Errorf("failed to convert contact: %w", err)
		}
		recipientResp := &bridgev2.ResolveIdentifierResponse{
			UserInfo: userInfo,
			Chat:     s.makeCreateDMResponse(ctx, recipient, nil),
		}
		if recipient.ACI != uuid.Nil {
			recipientResp.UserID = signalid.MakeUserID(recipient.ACI)
			ghost, err := s.Main.Bridge.GetGhostByID(ctx, recipientResp.UserID)
			if err != nil {
				return nil, fmt.Errorf("failed to get ghost for %s: %w", recipient.ACI, err)
			}
			recipientResp.Ghost = ghost
		} else {
			recipientResp.UserID = signalid.MakeUserIDFromServiceID(libsignalgo.NewPNIServiceID(recipient.PNI))
		}
		resp[i] = recipientResp
	}
	return resp, nil
}

func (s *SignalClient) makeCreateDMResponse(ctx context.Context, recipient *types.Recipient, backupChat *store.BackupChat) *bridgev2.CreateChatResponse {
	name := ""
	topic := PrivateChatTopic
	selfUser := s.makeEventSender(s.Client.Store.ACI)
	members := &bridgev2.ChatMemberList{
		IsFull: true,
		MemberMap: map[networkid.UserID]bridgev2.ChatMember{
			selfUser.Sender: {
				EventSender: selfUser,
				Membership:  event.MembershipJoin,
				PowerLevel:  &moderatorPL,
			},
		},
		PowerLevels: &bridgev2.PowerLevelOverrides{
			Events: map[event.Type]int{
				event.StateRoomName:                0,
				event.StateTopic:                   0,
				event.StateRoomAvatar:              0,
				event.StateBeeperDisappearingTimer: 0,
			},
		},
	}
	if s.Main.Config.NumberInTopic && recipient.E164 != "" {
		topic = fmt.Sprintf("%s with %s", PrivateChatTopic, recipient.E164)
	}
	var serviceID libsignalgo.ServiceID
	var avatar *bridgev2.Avatar
	if recipient.ACI == uuid.Nil {
		name = s.Main.Config.FormatDisplayname(recipient)
		serviceID = libsignalgo.NewPNIServiceID(recipient.PNI)
	} else {
		if backupChat == nil {
			var err error
			backupChat, err = s.Client.Store.BackupStore.GetBackupChatByUserID(ctx, libsignalgo.NewACIServiceID(recipient.ACI))
			if err != nil {
				zerolog.Ctx(ctx).Warn().Err(err).Msg("Failed to get backup chat for recipient")
			}
		}
		members.OtherUserID = signalid.MakeUserID(recipient.ACI)
		if recipient.ACI == s.Client.Store.ACI {
			name = NoteToSelfName
			avatar = &bridgev2.Avatar{
				ID:     networkid.AvatarID(s.Main.Config.NoteToSelfAvatar),
				Remove: len(s.Main.Config.NoteToSelfAvatar) == 0,
				MXC:    s.Main.Config.NoteToSelfAvatar,
				Hash:   sha256.Sum256([]byte(s.Main.Config.NoteToSelfAvatar)),
			}
		} else {
			// The other user is only present if their ACI is known
			recipientUser := s.makeEventSender(recipient.ACI)
			members.MemberMap[recipientUser.Sender] = bridgev2.ChatMember{
				EventSender: recipientUser,
				Membership:  event.MembershipJoin,
				PowerLevel:  &moderatorPL,
			}
		}
		serviceID = libsignalgo.NewACIServiceID(recipient.ACI)
	}
	return &bridgev2.CreateChatResponse{
		PortalKey: s.makeDMPortalKey(serviceID),
		PortalInfo: &bridgev2.ChatInfo{
			Name:    &name,
			Avatar:  avatar,
			Topic:   &topic,
			Members: members,
			Type:    ptr.Ptr(database.RoomTypeDM),

			MessageRequest: ptr.Ptr(recipient.ACI != uuid.Nil && recipient.ProbablyMessageRequest()),
			CanBackfill:    backupChat != nil,
			ExtraUpdates:   updatePortalSyncMeta,
		},
	}
}

func makeAvatarPathID(avatarPath string) networkid.AvatarID {
	if avatarPath == "" {
		return ""
	}
	return networkid.AvatarID("path:" + avatarPath)
}
