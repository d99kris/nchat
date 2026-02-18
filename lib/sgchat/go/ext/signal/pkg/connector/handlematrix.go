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
	"errors"
	"fmt"
	"slices"
	"strconv"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/ptr"
	"go.mau.fi/util/variationselector"
	"google.golang.org/protobuf/proto"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/bridgev2/simplevent"
	"maunium.net/go/mautrix/event"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

var (
	_ bridgev2.EditHandlingNetworkAPI            = (*SignalClient)(nil)
	_ bridgev2.ReactionHandlingNetworkAPI        = (*SignalClient)(nil)
	_ bridgev2.RedactionHandlingNetworkAPI       = (*SignalClient)(nil)
	_ bridgev2.ReadReceiptHandlingNetworkAPI     = (*SignalClient)(nil)
	_ bridgev2.TypingHandlingNetworkAPI          = (*SignalClient)(nil)
	_ bridgev2.RoomNameHandlingNetworkAPI        = (*SignalClient)(nil)
	_ bridgev2.RoomAvatarHandlingNetworkAPI      = (*SignalClient)(nil)
	_ bridgev2.RoomTopicHandlingNetworkAPI       = (*SignalClient)(nil)
	_ bridgev2.ChatViewingNetworkAPI             = (*SignalClient)(nil)
	_ bridgev2.DisappearTimerChangingNetworkAPI  = (*SignalClient)(nil)
	_ bridgev2.DeleteChatHandlingNetworkAPI      = (*SignalClient)(nil)
	_ bridgev2.PollHandlingNetworkAPI            = (*SignalClient)(nil)
	_ bridgev2.MessageRequestAcceptingNetworkAPI = (*SignalClient)(nil)
)

func (s *SignalClient) sendMessage(ctx context.Context, portalID networkid.PortalID, content *signalpb.Content) error {
	userID, groupID, err := signalid.ParsePortalID(portalID)
	if err != nil {
		return err
	}
	if groupID != "" {
		result, err := s.Client.SendGroupMessage(ctx, groupID, content)
		if err != nil {
			return err
		}
		totalRecipients := len(result.FailedToSendTo) + len(result.SuccessfullySentTo)
		log := zerolog.Ctx(ctx).With().
			Int("total_recipients", totalRecipients).
			Int("failed_to_send_to_count", len(result.FailedToSendTo)).
			Int("successfully_sent_to_count", len(result.SuccessfullySentTo)).
			Logger()
		if len(result.SuccessfullySentTo) == 0 && len(result.FailedToSendTo) == 0 {
			log.Debug().Msg("No successes or failures - Probably sent to myself")
		} else if len(result.SuccessfullySentTo) == 0 {
			log.Error().Msg("Failed to send event to all members of Signal group")
			return errors.New("failed to send to any members of Signal group")
		} else if len(result.SuccessfullySentTo) < totalRecipients {
			if len(result.FailedToSendTo) > 0 {
				log.Warn().Msg("Failed to send event to some members of Signal group")
			} else {
				log.Warn().Msg("Only sent event to some members of Signal group")
			}
		} else {
			log.Debug().Msg("Sent event to all members of Signal group")
		}
		return nil
	} else {
		res := s.Client.SendMessage(ctx, userID, content)
		if !res.WasSuccessful {
			return res.Error
		}
		return nil
	}
}

func getTimestampForEvent(txnID networkid.RawTransactionID, evt *event.Event, origSender *bridgev2.OrigSender) uint64 {
	if origSender != nil {
		// Relaybot messages are never allowed to set the timestamp
		return uint64(time.Now().UnixMilli())
	}
	if len(txnID) > 0 {
		parsed, err := strconv.ParseUint(string(txnID), 10, 64)
		if err == nil {
			return parsed
		}
	}
	return uint64(evt.Timestamp)
}

func (s *SignalClient) HandleMatrixMessage(ctx context.Context, msg *bridgev2.MatrixMessage) (message *bridgev2.MatrixMessageResponse, err error) {
	converted, err := s.Main.MsgConv.ToSignal(
		ctx, s.Client, msg.Portal, msg.Event, msg.Content, msg.OrigSender != nil, msg.ReplyTo,
	)
	if err != nil {
		return nil, err
	}
	return s.doSendMessage(ctx, msg, converted, &signalid.MessageMetadata{
		ContainsAttachments: len(converted.Attachments) > 0,
	})
}

func (s *SignalClient) doSendMessage(
	ctx context.Context,
	msg *bridgev2.MatrixMessage,
	converted *signalpb.DataMessage,
	meta *signalid.MessageMetadata,
) (*bridgev2.MatrixMessageResponse, error) {
	ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
	converted.Timestamp = &ts
	if meta == nil {
		meta = &signalid.MessageMetadata{}
	}
	msgID := signalid.MakeMessageID(s.Client.Store.ACI, ts)
	msg.AddPendingToIgnore(networkid.TransactionID(msgID))
	err := s.sendMessage(ctx, msg.Portal.ID, &signalpb.Content{DataMessage: converted})
	if err != nil {
		return nil, bridgev2.WrapErrorInStatus(err).WithSendNotice(true)
	}
	dbMsg := &database.Message{
		ID:        msgID,
		SenderID:  signalid.MakeUserID(s.Client.Store.ACI),
		Timestamp: time.UnixMilli(int64(ts)),
		Metadata:  meta,
	}
	return &bridgev2.MatrixMessageResponse{
		DB:            dbMsg,
		RemovePending: networkid.TransactionID(msgID),
	}, nil
}

func (s *SignalClient) HandleMatrixEdit(ctx context.Context, msg *bridgev2.MatrixEdit) error {
	_, targetSentTimestamp, err := signalid.ParseMessageID(msg.EditTarget.ID)
	if err != nil {
		return fmt.Errorf("failed to parse target message ID: %w", err)
	} else if msg.EditTarget.SenderID != signalid.MakeUserID(s.Client.Store.ACI) {
		return fmt.Errorf("cannot edit other people's messages")
	}
	var replyTo *database.Message
	if msg.EditTarget.ReplyTo.MessageID != "" {
		replyTo, err = s.Main.Bridge.DB.Message.GetFirstOrSpecificPartByID(ctx, msg.Portal.Receiver, msg.EditTarget.ReplyTo)
		if err != nil {
			return fmt.Errorf("failed to get message reply target: %w", err)
		}
	}
	converted, err := s.Main.MsgConv.ToSignal(ctx, s.Client, msg.Portal, msg.Event, msg.Content, msg.OrigSender != nil, replyTo)
	if err != nil {
		return err
	}
	ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
	converted.Timestamp = &ts
	err = s.sendMessage(ctx, msg.Portal.ID, &signalpb.Content{EditMessage: &signalpb.EditMessage{
		TargetSentTimestamp: proto.Uint64(targetSentTimestamp),
		DataMessage:         converted,
	}})
	if err != nil {
		return bridgev2.WrapErrorInStatus(err).WithSendNotice(true)
	}
	msg.EditTarget.ID = signalid.MakeMessageID(s.Client.Store.ACI, ts)
	msg.EditTarget.Metadata = &signalid.MessageMetadata{ContainsAttachments: len(converted.Attachments) > 0}
	msg.EditTarget.EditCount++
	return nil
}

func (s *SignalClient) PreHandleMatrixReaction(ctx context.Context, msg *bridgev2.MatrixReaction) (bridgev2.MatrixReactionPreResponse, error) {
	return bridgev2.MatrixReactionPreResponse{
		SenderID: signalid.MakeUserID(s.Client.Store.ACI),
		EmojiID:  "",
		Emoji:    variationselector.FullyQualify(msg.Content.RelatesTo.Key),
	}, nil
}

func (s *SignalClient) HandleMatrixReaction(ctx context.Context, msg *bridgev2.MatrixReaction) (reaction *database.Reaction, err error) {
	targetAuthorACI, targetSentTimestamp, err := signalid.ParseMessageID(msg.TargetMessage.ID)
	if err != nil {
		return nil, fmt.Errorf("failed to parse target message ID: %w", err)
	}
	ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
	wrappedContent := &signalpb.Content{
		DataMessage: &signalpb.DataMessage{
			Timestamp:               proto.Uint64(ts),
			RequiredProtocolVersion: proto.Uint32(uint32(signalpb.DataMessage_REACTIONS)),
			Reaction: &signalpb.DataMessage_Reaction{
				Emoji:               proto.String(msg.PreHandleResp.Emoji),
				Remove:              proto.Bool(false),
				TargetAuthorAci:     proto.String(targetAuthorACI.String()),
				TargetSentTimestamp: proto.Uint64(targetSentTimestamp),
			},
		},
	}
	err = s.sendMessage(ctx, msg.Portal.ID, wrappedContent)
	if err != nil {
		return nil, err
	}
	return &database.Reaction{}, nil
}

func (s *SignalClient) HandleMatrixReactionRemove(ctx context.Context, msg *bridgev2.MatrixReactionRemove) error {
	targetAuthorACI, targetSentTimestamp, err := signalid.ParseMessageID(msg.TargetReaction.MessageID)
	if err != nil {
		return fmt.Errorf("failed to parse target message ID: %w", err)
	}
	ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
	wrappedContent := &signalpb.Content{
		DataMessage: &signalpb.DataMessage{
			Timestamp:               proto.Uint64(ts),
			RequiredProtocolVersion: proto.Uint32(uint32(signalpb.DataMessage_REACTIONS)),
			Reaction: &signalpb.DataMessage_Reaction{
				Emoji:               proto.String(msg.TargetReaction.Emoji),
				Remove:              proto.Bool(true),
				TargetAuthorAci:     proto.String(targetAuthorACI.String()),
				TargetSentTimestamp: proto.Uint64(targetSentTimestamp),
			},
		},
	}
	err = s.sendMessage(ctx, msg.Portal.ID, wrappedContent)
	if err != nil {
		return err
	}
	return nil
}

func (s *SignalClient) HandleMatrixMessageRemove(ctx context.Context, msg *bridgev2.MatrixMessageRemove) error {
	_, targetSentTimestamp, err := signalid.ParseMessageID(msg.TargetMessage.ID)
	if err != nil {
		return fmt.Errorf("failed to parse target message ID: %w", err)
	} else if msg.TargetMessage.SenderID != signalid.MakeUserID(s.Client.Store.ACI) {
		return fmt.Errorf("cannot delete other people's messages")
	}
	ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
	wrappedContent := &signalpb.Content{
		DataMessage: &signalpb.DataMessage{
			Timestamp: proto.Uint64(ts),
			Delete: &signalpb.DataMessage_Delete{
				TargetSentTimestamp: proto.Uint64(targetSentTimestamp),
			},
		},
	}
	err = s.sendMessage(ctx, msg.Portal.ID, wrappedContent)
	if err != nil {
		return err
	}
	return nil
}

func (s *SignalClient) HandleMatrixReadReceipt(ctx context.Context, receipt *bridgev2.MatrixReadReceipt) error {
	if !receipt.ReadUpTo.After(receipt.LastRead) {
		return nil
	}
	if receipt.LastRead.IsZero() {
		receipt.LastRead = receipt.ReadUpTo.Add(-5 * time.Second)
	}
	dbMessages, err := s.Main.Bridge.DB.Message.GetMessagesBetweenTimeQuery(ctx, receipt.Portal.PortalKey, receipt.LastRead, receipt.ReadUpTo)
	if err != nil {
		return fmt.Errorf("failed to get messages to mark as read: %w", err)
	} else if len(dbMessages) == 0 {
		return nil
	}
	messagesToRead := map[uuid.UUID][]uint64{}
	for _, msg := range dbMessages {
		userID, timestamp, err := signalid.ParseMessageID(msg.ID)
		if err != nil {
			continue
		}
		messagesToRead[userID] = append(messagesToRead[userID], timestamp)
	}
	zerolog.Ctx(ctx).Debug().
		Any("targets", messagesToRead).
		Msg("Collected read receipt target messages")

	// TODO send sync message manually containing all read receipts instead of a separate message for each recipient

	for destination, messages := range messagesToRead {
		// Don't send read receipts for own messages
		if destination == s.Client.Store.ACI {
			continue
		}
		// Don't use portal.sendSignalMessage because we're sending this straight to
		// who sent the original message, not the portal's ChatID
		ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
		result := s.Client.SendMessage(ctx, libsignalgo.NewACIServiceID(destination), signalmeow.ReadReceptMessageForTimestamps(messages))
		cancel()
		if !result.WasSuccessful {
			zerolog.Ctx(ctx).Err(result.Error).
				Stringer("destination", destination).
				Uints64("message_ids", messages).
				Msg("Failed to send read receipt to Signal")
		} else {
			zerolog.Ctx(ctx).Debug().
				Stringer("destination", destination).
				Uints64("message_ids", messages).
				Msg("Sent read receipt to Signal")
		}
	}
	return nil
}

func (s *SignalClient) HandleMatrixTyping(ctx context.Context, typing *bridgev2.MatrixTyping) error {
	userID, groupID, err := signalid.ParsePortalID(typing.Portal.ID)
	if err != nil {
		return err
	}
	typingMessage := signalmeow.TypingMessage(typing.IsTyping)
	if !userID.IsEmpty() && userID.Type == libsignalgo.ServiceIDTypeACI {
		result := s.Client.SendMessage(ctx, userID, typingMessage)
		if !result.WasSuccessful {
			return result.Error
		}
	} else if groupID != "" {
		_, err = s.Client.SendGroupMessage(ctx, groupID, typingMessage)
		if err != nil {
			return err
		}
	}
	return nil
}

func (s *SignalClient) handleMatrixRoomMeta(ctx context.Context, portal *bridgev2.Portal, gc *signalmeow.GroupChange, postUpdatePortal func()) (bool, error) {
	_, groupID, err := signalid.ParsePortalID(portal.ID)
	if err != nil || groupID == "" {
		return false, err
	}
	gc.Revision = portal.Metadata.(*signalid.PortalMetadata).Revision + 1
	revision, err := s.Client.UpdateGroup(ctx, gc, groupID)
	if err != nil {
		return false, err
	}
	if gc.ModifyTitle != nil {
		portal.Name = *gc.ModifyTitle
		portal.NameSet = true
	}
	if gc.ModifyDescription != nil {
		portal.Topic = *gc.ModifyDescription
		portal.TopicSet = true
	}
	if gc.ModifyAvatar != nil {
		portal.AvatarID = makeAvatarPathID(*gc.ModifyAvatar)
		portal.AvatarSet = true
	}
	if postUpdatePortal != nil {
		postUpdatePortal()
	}
	portal.Metadata.(*signalid.PortalMetadata).Revision = revision
	return true, nil
}

func (s *SignalClient) HandleMatrixRoomName(ctx context.Context, msg *bridgev2.MatrixRoomName) (bool, error) {
	return s.handleMatrixRoomMeta(ctx, msg.Portal, &signalmeow.GroupChange{
		ModifyTitle: &msg.Content.Name,
	}, nil)
}

func (s *SignalClient) HandleMatrixRoomAvatar(ctx context.Context, msg *bridgev2.MatrixRoomAvatar) (bool, error) {
	_, groupID, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil || groupID == "" {
		return false, err
	}
	var avatarPath string
	var avatarHash [32]byte
	if msg.Content.URL != "" {
		data, err := s.Main.Bridge.Bot.DownloadMedia(ctx, msg.Content.URL, nil)
		if err != nil {
			return false, fmt.Errorf("failed to download avatar: %w", err)
		}
		avatarHash = sha256.Sum256(data)
		avatarPath, err = s.Client.UploadGroupAvatar(ctx, data, groupID)
		if err != nil {
			return false, fmt.Errorf("failed to reupload avatar: %w", err)
		}
	}
	return s.handleMatrixRoomMeta(ctx, msg.Portal, &signalmeow.GroupChange{
		ModifyAvatar: &avatarPath,
	}, func() {
		msg.Portal.AvatarMXC = msg.Content.URL
		msg.Portal.AvatarHash = avatarHash
	})
}

func (s *SignalClient) HandleMatrixRoomTopic(ctx context.Context, msg *bridgev2.MatrixRoomTopic) (bool, error) {
	return s.handleMatrixRoomMeta(ctx, msg.Portal, &signalmeow.GroupChange{
		ModifyDescription: &msg.Content.Topic,
	}, nil)
}

func (s *SignalClient) HandleMatrixMembership(ctx context.Context, msg *bridgev2.MatrixMembershipChange) (*bridgev2.MatrixMembershipResult, error) {
	var targetIntent bridgev2.MatrixAPI
	var targetSignalID libsignalgo.ServiceID
	var err error
	if msg.Portal.RoomType == database.RoomTypeDM {
		switch msg.Type {
		case bridgev2.Invite:
			return nil, fmt.Errorf("cannot invite additional user to dm")
		default:
			return nil, nil
		}
	}
	targetSignalID, err = signalid.ParseGhostOrUserLoginID(msg.Target)
	if err != nil {
		return nil, fmt.Errorf("failed to parse target signal id: %w", err)
	}
	switch target := msg.Target.(type) {
	case *bridgev2.Ghost:
		targetIntent = target.Intent
	case *bridgev2.UserLogin:
		targetIntent = target.User.DoublePuppet(ctx)
		if targetIntent == nil {
			ghost, err := s.Main.Bridge.GetGhostByID(ctx, networkid.UserID(target.ID))
			if err != nil {
				return nil, fmt.Errorf("failed to get ghost for user: %w", err)
			}
			targetIntent = ghost.Intent
		}
	default:
		return nil, fmt.Errorf("cannot get target intent: unknown type: %T", target)
	}
	log := zerolog.Ctx(ctx).With().
		Str("From Membership", string(msg.Type.From)).
		Str("To Membership", string(msg.Type.To)).
		Logger()
	gc := &signalmeow.GroupChange{}
	role := signalmeow.GroupMember_DEFAULT
	if msg.Type.To == event.MembershipInvite || msg.Type == bridgev2.AcceptKnock {
		levels, err := msg.Portal.Bridge.Matrix.GetPowerLevels(ctx, msg.Portal.MXID)
		if err != nil {
			log.Err(err).Msg("Couldn't get power levels")
			if levels.GetUserLevel(targetIntent.GetMXID()) >= moderatorPL {
				role = signalmeow.GroupMember_ADMINISTRATOR
			}
		}
	}
	switch msg.Type {
	case bridgev2.AcceptInvite:
		if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("can't accept invite for non-ACI service ID")
		}
		gc.PromotePendingMembers = []*signalmeow.PromotePendingMember{{
			ACI: targetSignalID.UUID,
		}}
	case bridgev2.RevokeInvite, bridgev2.RejectInvite:
		gc.DeletePendingMembers = []*libsignalgo.ServiceID{&targetSignalID}
	case bridgev2.Leave, bridgev2.Kick:
		if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("can't kick non-ACI service ID")
		}
		gc.DeleteMembers = []*uuid.UUID{&targetSignalID.UUID}
	case bridgev2.Invite:
		if targetSignalID.Type == libsignalgo.ServiceIDTypeACI {
			gc.AddMembers = []*signalmeow.AddMember{{
				GroupMember: signalmeow.GroupMember{
					ACI:  targetSignalID.UUID,
					Role: role,
				},
			}}
		} else {
			gc.AddPendingMembers = []*signalmeow.PendingMember{{
				ServiceID:     targetSignalID,
				Role:          role,
				AddedByUserID: s.Client.Store.ACI,
				Timestamp:     uint64(msg.Event.Timestamp),
			}}
		}
	// TODO: joining and knocking requires a way to obtain the invite link
	// because the joining/knocking member doesn't have the GroupMasterKey yet
	// case bridgev2.Join:
	// 	gc.AddMembers = []*signalmeow.AddMember{{
	// 		GroupMember: signalmeow.GroupMember{
	// 			ACI:  targetSignalID,
	// 			Role: role,
	// 		},
	// 		JoinFromInviteLink: true,
	// 	}}
	// case bridgev2.Knock:
	// 	gc.AddRequestingMembers = []*signalmeow.RequestingMember{{
	// 		ACI:       targetSignalID,
	// 		Timestamp: uint64(time.Now().UnixMilli()),
	// 	}}
	case bridgev2.AcceptKnock:
		if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("can't accept knock from non-ACI service ID")
		}
		gc.PromoteRequestingMembers = []*signalmeow.RoleMember{{
			ACI:  targetSignalID.UUID,
			Role: role,
		}}
	case bridgev2.RetractKnock, bridgev2.RejectKnock:
		if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
			return nil, fmt.Errorf("can't reject knock from non-ACI service ID")
		}
		gc.DeleteRequestingMembers = []*uuid.UUID{&targetSignalID.UUID}
	case bridgev2.BanKnocked, bridgev2.BanInvited, bridgev2.BanJoined, bridgev2.BanLeft:
		gc.AddBannedMembers = []*signalmeow.BannedMember{{
			ServiceID: targetSignalID,
			Timestamp: uint64(time.Now().UnixMilli()),
		}}
		switch msg.Type {
		case bridgev2.BanJoined:
			if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
				return nil, fmt.Errorf("can't ban joined non-ACI service ID")
			}
			gc.DeleteMembers = []*uuid.UUID{&targetSignalID.UUID}
		case bridgev2.BanInvited:
			gc.DeletePendingMembers = []*libsignalgo.ServiceID{&targetSignalID}
		case bridgev2.BanKnocked:
			if targetSignalID.Type != libsignalgo.ServiceIDTypeACI {
				return nil, fmt.Errorf("can't ban knocked non-ACI service ID")
			}
			gc.DeleteRequestingMembers = []*uuid.UUID{&targetSignalID.UUID}
		}
	case bridgev2.Unban:
		gc.DeleteBannedMembers = []*libsignalgo.ServiceID{&targetSignalID}
	default:
		return nil, fmt.Errorf("unsupported membership change: %s -> %s", msg.Type.From, msg.Type.To)
	}
	_, groupID, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil || groupID == "" {
		return nil, err
	}
	gc.Revision = msg.Portal.Metadata.(*signalid.PortalMetadata).Revision + 1
	revision, err := s.Client.UpdateGroup(ctx, gc, groupID)
	if err != nil {
		return nil, err
	}
	if msg.Type == bridgev2.Invite && targetSignalID.Type != libsignalgo.ServiceIDTypePNI {
		err = targetIntent.EnsureJoined(ctx, msg.Portal.MXID)
		if err != nil {
			return nil, err
		}
	}
	msg.Portal.Metadata.(*signalid.PortalMetadata).Revision = revision
	return nil, nil
}

func plToRole(pl int) signalmeow.GroupMemberRole {
	if pl >= moderatorPL {
		return signalmeow.GroupMember_ADMINISTRATOR
	} else {
		return signalmeow.GroupMember_DEFAULT
	}
}

func plToAccessControl(pl int) *signalmeow.AccessControl {
	var accessControl signalmeow.AccessControl
	if pl >= moderatorPL {
		accessControl = signalmeow.AccessControl_ADMINISTRATOR
	} else {
		accessControl = signalmeow.AccessControl_MEMBER
	}
	return &accessControl
}

func hasAdminChanged(plc *bridgev2.SinglePowerLevelChange) bool {
	if plc == nil {
		return false
	}
	return (plc.NewLevel < moderatorPL) != (plc.OrigLevel < moderatorPL)
}

func (s *SignalClient) HandleMatrixPowerLevels(ctx context.Context, msg *bridgev2.MatrixPowerLevelChange) (bool, error) {
	if msg.Portal.RoomType == database.RoomTypeDM {
		return false, nil
	}
	gc := &signalmeow.GroupChange{}
	for _, plc := range msg.Users {
		if !hasAdminChanged(&plc.SinglePowerLevelChange) {
			continue
		}
		serviceID, err := signalid.ParseGhostOrUserLoginID(plc.Target)
		if err != nil || serviceID.Type != libsignalgo.ServiceIDTypeACI {
			continue
		}
		gc.ModifyMemberRoles = append(gc.ModifyMemberRoles, &signalmeow.RoleMember{
			ACI:  serviceID.UUID,
			Role: plToRole(plc.NewLevel),
		})
	}
	if hasAdminChanged(msg.EventsDefault) {
		announcementsOnly := msg.EventsDefault.NewLevel >= moderatorPL
		gc.ModifyAnnouncementsOnly = &announcementsOnly
	}
	if hasAdminChanged(msg.StateDefault) {
		gc.ModifyAttributesAccess = plToAccessControl(msg.StateDefault.NewLevel)
	}
	if hasAdminChanged(msg.Invite) {
		gc.ModifyMemberAccess = plToAccessControl(msg.Invite.NewLevel)
	}
	_, groupID, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil || groupID == "" {
		return false, err
	}
	revision, err := s.Client.UpdateGroup(ctx, gc, groupID)
	if err != nil {
		return false, err
	}
	msg.Portal.Metadata.(*signalid.PortalMetadata).Revision = revision
	return true, nil
}

func (s *SignalClient) HandleMatrixViewingChat(ctx context.Context, msg *bridgev2.MatrixViewingChat) error {
	if msg.Portal == nil {
		return nil
	}

	// Sync the other users ghost in DMs
	if msg.Portal.OtherUserID != "" {
		ghost, err := s.Main.Bridge.GetExistingGhostByID(ctx, msg.Portal.OtherUserID)
		if err != nil {
			return fmt.Errorf("failed to get ghost for sync: %w", err)
		} else if ghost == nil {
			zerolog.Ctx(ctx).Warn().
				Str("other_user_id", string(msg.Portal.OtherUserID)).
				Msg("No ghost found for other user in portal")
		} else {
			meta := ghost.Metadata.(*signalid.GhostMetadata)
			if meta.ProfileFetchedAt.Time.Add(5 * time.Minute).Before(time.Now()) {
				// Reset, but don't save, portal last sync time for immediate sync now
				meta.ProfileFetchedAt.Time = time.Time{}
				info, err := s.GetUserInfoWithRefreshAfter(ctx, ghost, 5*time.Minute)
				if err != nil {
					return fmt.Errorf("failed to get user info: %w", err)
				}
				ghost.UpdateInfo(ctx, info)
			}
		}
	}

	// always resync the portal if its stale
	portalMeta := msg.Portal.Metadata.(*signalid.PortalMetadata)
	if portalMeta.LastSync.Add(24 * time.Hour).Before(time.Now()) {
		s.UserLogin.QueueRemoteEvent(&simplevent.ChatResync{
			EventMeta: simplevent.EventMeta{
				Type:      bridgev2.RemoteEventChatResync,
				PortalKey: msg.Portal.PortalKey,
			},
			GetChatInfoFunc: s.GetChatInfo,
		})
	}

	return nil
}

func (s *SignalClient) HandleMatrixDisappearingTimer(ctx context.Context, msg *bridgev2.MatrixDisappearingTimer) (bool, error) {
	if msg.Content.Type != event.DisappearingTypeAfterRead && msg.Content.Timer.Duration != 0 {
		return false, fmt.Errorf("unsupported disappearing timer type: %s", msg.Content.Type)
	}
	userID, groupID, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil {
		return false, err
	}
	newSetting := database.DisappearingSetting{
		Type:  event.DisappearingTypeAfterRead,
		Timer: msg.Content.Timer.Duration,
	}
	if newSetting.Timer == 0 {
		newSetting.Type = event.DisappearingTypeNone
	}
	if groupID != "" {
		return s.handleMatrixRoomMeta(ctx, msg.Portal, &signalmeow.GroupChange{
			ModifyDisappearingMessagesDuration: ptr.Ptr(uint32(msg.Content.Timer.Seconds())),
		}, func() {
			msg.Portal.Disappear = newSetting
		})
	} else {
		ts := getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)
		res := s.Client.SendMessage(ctx, userID, &signalpb.Content{
			DataMessage: &signalpb.DataMessage{
				Timestamp:   ptr.Ptr(ts),
				Flags:       ptr.Ptr(uint32(signalpb.DataMessage_EXPIRATION_TIMER_UPDATE)),
				ExpireTimer: ptr.Ptr(uint32(msg.Content.Timer.Seconds())),
			},
		})
		if !res.WasSuccessful {
			return false, res.Error
		}
		msg.Portal.Disappear = newSetting
		return true, nil
	}
}

func (s *SignalClient) HandleMatrixDeleteChat(ctx context.Context, msg *bridgev2.MatrixDeleteChat) error {
	userID, groupID, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil {
		return fmt.Errorf("failed to parse portal ID: %w", err)
	}

	if msg.Content.FromMessageRequest {
		// TODO block and delete support?
		err = s.syncMessageRequestResponse(ctx, msg.Portal, signalpb.SyncMessage_MessageRequestResponse_DELETE)
		if err != nil {
			return fmt.Errorf("failed to send message request delete sync: %w", err)
		}
	}

	// Build ConversationIdentifier based on portal type
	var conversationID *signalpb.ConversationIdentifier
	if groupID == "" {
		conversationID = &signalpb.ConversationIdentifier{
			Identifier: &signalpb.ConversationIdentifier_ThreadServiceId{
				ThreadServiceId: userID.String(),
			},
		}
	} else {
		gid, err := groupID.Bytes()
		if err != nil {
			return fmt.Errorf("failed to parse group ID: %w", err)
		}
		conversationID = &signalpb.ConversationIdentifier{
			Identifier: &signalpb.ConversationIdentifier_ThreadGroupId{
				ThreadGroupId: gid[:],
			},
		}
	}

	// Retrieve most recent messages from the portal
	var mostRecentMessages []*signalpb.AddressableMessage
	dbMessages, err := s.Main.Bridge.DB.Message.GetMessagesBetweenTimeQuery(
		ctx,
		msg.Portal.PortalKey,
		time.Now().Add(-30*24*time.Hour), // Last 30 days
		time.Now(),
	)
	if err != nil {
		zerolog.Ctx(ctx).Warn().Err(err).Msg("Failed to get recent messages for conversation delete")
	} else if len(dbMessages) > 0 {
		// Limit to the 5 most recent messages overall
		limit := 5
		startIdx := 0
		if len(dbMessages) > limit {
			startIdx = len(dbMessages) - limit
		}

		// Create AddressableMessage for most recent messages
		for _, dbMsg := range dbMessages[startIdx:] {
			senderACI, timestamp, err := signalid.ParseMessageID(dbMsg.ID)
			if err != nil {
				continue
			}

			mostRecentMessages = append(mostRecentMessages, &signalpb.AddressableMessage{
				Author: &signalpb.AddressableMessage_AuthorServiceId{
					AuthorServiceId: senderACI.String(),
				},
				SentTimestamp: proto.Uint64(timestamp),
			})
		}
	}

	recipientID := s.Client.Store.ACIServiceID()
	// Send DeleteForMe sync message to self
	result := s.Client.SendMessage(ctx, recipientID, &signalpb.Content{
		SyncMessage: &signalpb.SyncMessage{
			DeleteForMe: &signalpb.SyncMessage_DeleteForMe{
				ConversationDeletes: []*signalpb.SyncMessage_DeleteForMe_ConversationDelete{{
					Conversation:       conversationID,
					MostRecentMessages: mostRecentMessages,
					IsFullDelete:       proto.Bool(true),
				}},
			},
		},
	})

	zerolog.Ctx(ctx).Debug().
		Str("portal_id", string(msg.Portal.ID)).
		Int("recent_messages_count", len(mostRecentMessages)).
		Msg("Sent conversation deletion to Signal")

	if !result.WasSuccessful {
		return fmt.Errorf("failed to send delete conversation sync message: %w %s %s", result.Error, userID, groupID)
	}

	return nil
}

func (s *SignalClient) HandleMatrixPollStart(ctx context.Context, msg *bridgev2.MatrixPollStart) (*bridgev2.MatrixMessageResponse, error) {
	optionNames := make([]string, len(msg.Content.PollStart.Answers))
	optionIDs := make([]string, len(msg.Content.PollStart.Answers))
	for i, option := range msg.Content.PollStart.Answers {
		optionNames[i] = option.Text
		optionIDs[i] = option.ID
	}
	converted := &signalpb.DataMessage{
		PollCreate: &signalpb.DataMessage_PollCreate{
			Question:      ptr.Ptr(msg.Content.PollStart.Question.Text),
			AllowMultiple: ptr.Ptr(msg.Content.PollStart.MaxSelections != 1),
			Options:       optionNames,
		},
		RequiredProtocolVersion: ptr.Ptr(uint32(signalpb.DataMessage_POLLS)),
	}
	return s.doSendMessage(ctx, &msg.MatrixMessage, converted, &signalid.MessageMetadata{
		MatrixPollOptionIDs: optionIDs,
	})
}

func (s *SignalClient) HandleMatrixPollVote(ctx context.Context, msg *bridgev2.MatrixPollVote) (*bridgev2.MatrixMessageResponse, error) {
	senderACI, msgTS, err := signalid.ParseMessageID(msg.VoteTo.ID)
	if err != nil {
		return nil, err
	}
	mxOptions := msg.VoteTo.Metadata.(*signalid.MessageMetadata).MatrixPollOptionIDs
	optionIndexes := make([]uint32, len(msg.Content.Response.Answers))
	for i, answer := range msg.Content.Response.Answers {
		if idx := slices.Index(mxOptions, answer); idx >= 0 {
			optionIndexes[i] = uint32(idx)
		} else if idx, err = strconv.Atoi(answer); err == nil && idx >= 0 {
			optionIndexes[i] = uint32(idx)
		} else {
			return nil, fmt.Errorf("unknown poll answer ID: %s", answer)
		}
	}
	converted := &signalpb.DataMessage{
		PollVote: &signalpb.DataMessage_PollVote{
			TargetAuthorAciBinary: senderACI[:],
			TargetSentTimestamp:   &msgTS,
			OptionIndexes:         optionIndexes,
			VoteCount:             proto.Uint32(1), // TODO
		},
		RequiredProtocolVersion: proto.Uint32(0),
	}
	return s.doSendMessage(ctx, &msg.MatrixMessage, converted, nil)
}

func (s *SignalClient) syncMessageRequestResponse(
	ctx context.Context,
	portal *bridgev2.Portal,
	respType signalpb.SyncMessage_MessageRequestResponse_Type,
) error {
	userID, groupID, err := signalid.ParsePortalID(portal.ID)
	if err != nil {
		return err
	}
	accept := &signalpb.SyncMessage_MessageRequestResponse{
		Type: respType.Enum(),
	}
	if groupID != "" {
		gidBytes, err := groupID.Bytes()
		if err != nil {
			return fmt.Errorf("failed to parse group ID: %w", err)
		}
		accept.GroupId = gidBytes[:]
	} else if userID.Type == libsignalgo.ServiceIDTypeACI {
		accept.ThreadAci = ptr.Ptr(userID.UUID.String())
	} else {
		return fmt.Errorf("invalid portal ID for message request response: %s", portal.ID)
	}
	res := s.Client.SendMessage(ctx, libsignalgo.NewACIServiceID(s.Client.Store.ACI), &signalpb.Content{
		SyncMessage: &signalpb.SyncMessage{
			MessageRequestResponse: accept,
		},
	})
	if !res.WasSuccessful {
		return res.Error
	}
	return nil
}

func (s *SignalClient) HandleMatrixAcceptMessageRequest(ctx context.Context, msg *bridgev2.MatrixAcceptMessageRequest) error {
	userID, _, err := signalid.ParsePortalID(msg.Portal.ID)
	if err != nil {
		return err
	}
	err = s.syncMessageRequestResponse(ctx, msg.Portal, signalpb.SyncMessage_MessageRequestResponse_ACCEPT)
	if err != nil {
		return fmt.Errorf("failed to sync message request acceptance: %w", err)
	}
	if userID.Type == libsignalgo.ServiceIDTypeACI {
		profileKey, err := s.Client.ProfileKeyForSignalID(ctx, s.Client.Store.ACI)
		if err != nil {
			return fmt.Errorf("failed to get own profile key: %w", err)
		}
		var pniSig *signalpb.PniSignatureMessage
		if s.Client.Store.AccountRecord.GetPhoneNumberSharingMode() == signalpb.AccountRecord_EVERYBODY {
			sig, err := s.Client.Store.PNIIdentityKeyPair.SignAlternateIdentity(s.Client.Store.ACIIdentityKeyPair.GetIdentityKey())
			if err != nil {
				return fmt.Errorf("failed to generate PNI signature: %w", err)
			}
			pniSig = &signalpb.PniSignatureMessage{
				Pni:       s.Client.Store.PNI[:],
				Signature: sig,
			}
		}
		res := s.Client.SendMessage(ctx, userID, &signalpb.Content{
			DataMessage: &signalpb.DataMessage{
				Flags:      proto.Uint32(uint32(signalpb.DataMessage_PROFILE_KEY_UPDATE)),
				ProfileKey: profileKey.Slice(),
				Timestamp:  proto.Uint64(getTimestampForEvent(msg.InputTransactionID, msg.Event, msg.OrigSender)),

				RequiredProtocolVersion: proto.Uint32(0),
			},
			PniSignatureMessage: pniSig,
		})
		if !res.WasSuccessful {
			return fmt.Errorf("failed to share profile key to accept message request: %w", res.Error)
		}
		// TODO send read receipts too?
	}
	return nil
}
