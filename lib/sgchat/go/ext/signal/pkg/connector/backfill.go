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
	"fmt"
	"slices"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/ptr"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"

	"go.mau.fi/mautrix-signal/pkg/msgconv"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
)

var _ bridgev2.BackfillingNetworkAPI = (*SignalClient)(nil)

func tryCastUUID(b []byte) uuid.UUID {
	if len(b) == 16 {
		return uuid.UUID(b)
	}
	return uuid.Nil
}

func (s *SignalClient) FetchMessages(ctx context.Context, params bridgev2.FetchMessagesParams) (*bridgev2.FetchMessagesResponse, error) {
	if !s.IsLoggedIn() {
		return nil, bridgev2.ErrNotLoggedIn
	}
	userID, groupID, err := signalid.ParsePortalID(params.Portal.ID)
	if err != nil {
		return nil, fmt.Errorf("failed to parse portal ID: %w", err)
	}
	var chat *store.BackupChat
	if groupID != "" {
		chat, err = s.Client.Store.BackupStore.GetBackupChatByGroupID(ctx, groupID)
	} else {
		chat, err = s.Client.Store.BackupStore.GetBackupChatByUserID(ctx, userID)
	}
	if err != nil {
		return nil, fmt.Errorf("failed to get chat: %w", err)
	} else if chat == nil {
		zerolog.Ctx(ctx).Debug().Msg("Chat not found, returning nil response for backfill")
		return nil, nil
	}
	var anchorTS time.Time
	if params.AnchorMessage != nil {
		anchorTS = params.AnchorMessage.Timestamp
	}
	minTS := anchorTS
	items, err := s.Client.Store.BackupStore.GetBackupChatItems(ctx, chat.Id, anchorTS, params.Forward, params.Count)
	if err != nil {
		return nil, fmt.Errorf("failed to get chat items: %w", err)
	}
	if len(items) > 0 {
		minTS = time.UnixMilli(int64(items[0].DateSent))
	}
	// GetBackupChatItems returns in reverse chronological order, so flip the list
	slices.Reverse(items)
	var firstDirectionfulProcessed bool
	var isRead bool
	convertedMessages := make([]*bridgev2.BackfillMessage, 0, len(items))
	attMap := make(msgconv.AttachmentMap)
	recipientMap := make(map[uint64]*backuppb.Recipient)
	getRecipientACI := func(id uint64) (uuid.UUID, error) {
		recipient, ok := recipientMap[id]
		if !ok {
			recipient, err = s.Client.Store.BackupStore.GetBackupRecipient(ctx, id)
			if err != nil {
				return uuid.Nil, fmt.Errorf("failed to get recipient %d: %w", id, err)
			} else if len(recipient.GetContact().GetAci()) != 16 && recipient.GetSelf() == nil {
				zerolog.Ctx(ctx).Warn().
					Uint64("recipient_id", id).
					Type("recipient_type", recipient.GetDestination()).
					Msg("ACI not found for recipient")
			}
			recipientMap[id] = recipient
		}

		switch dest := recipient.Destination.(type) {
		case *backuppb.Recipient_Self:
			return s.Client.Store.ACI, nil
		case *backuppb.Recipient_Contact:
			if len(dest.Contact.GetAci()) == 16 {
				return uuid.UUID(dest.Contact.GetAci()), nil
			}
		}
		return uuid.Nil, nil
	}
	var prevStreamOrder int64
	findNextStreamOrder := func(i int) int64 {
		for ; i < len(items); i++ {
			inc, ok := items[i].DirectionalDetails.(*backuppb.ChatItem_Incoming)
			if ok {
				return int64(inc.Incoming.GetDateServerSent())
			}
		}
		return time.Now().UnixMilli()
	}
	for i, item := range items {
		var streamOrder int64
		switch dt := item.DirectionalDetails.(type) {
		case *backuppb.ChatItem_Incoming:
			streamOrder = int64(dt.Incoming.GetDateServerSent())
			prevStreamOrder = streamOrder
			if !firstDirectionfulProcessed {
				firstDirectionfulProcessed = true
				isRead = dt.Incoming.Read
			}
		case *backuppb.ChatItem_Outgoing:
			streamOrder = int64(item.GetDateSent())
			// Ensure stream order is higher than previous incoming item, but lower than next incoming item
			streamOrder = min(streamOrder, findNextStreamOrder(i+1)-1)
			streamOrder = max(streamOrder, prevStreamOrder+1)

			if !firstDirectionfulProcessed {
				firstDirectionfulProcessed = true
				isRead = true
			}
		}
		if len(attMap) > 0 {
			clear(attMap)
		}
		senderACI, err := getRecipientACI(item.AuthorId)
		if err != nil {
			return nil, err
		} else if senderACI == uuid.Nil {
			continue
		}
		dm, reactions := msgconv.BackupToDataMessage(item, attMap)
		if dm == nil {
			continue
		}
		cm := s.Main.MsgConv.ToMatrix(ctx, s.Client, params.Portal, senderACI, s.Main.Bridge.Bot, dm, attMap)
		convertedReactions := make([]*bridgev2.BackfillReaction, 0, len(reactions))
		for _, reaction := range reactions {
			reactionSenderACI, err := getRecipientACI(reaction.AuthorId)
			if err != nil {
				return nil, err
			} else if reactionSenderACI == uuid.Nil {
				continue
			}
			convertedReactions = append(convertedReactions, &bridgev2.BackfillReaction{
				TargetPart: ptr.Ptr(networkid.PartID("")),
				Timestamp:  time.UnixMilli(int64(reaction.SentTimestamp)),
				Sender:     s.makeEventSender(reactionSenderACI),
				Emoji:      reaction.GetEmoji(),
			})
		}
		msgID := signalid.MakeMessageID(senderACI, item.DateSent)
		convertedMessages = append(convertedMessages, &bridgev2.BackfillMessage{
			ConvertedMessage: cm,
			Sender:           s.makeEventSender(senderACI),
			ID:               msgID,
			TxnID:            networkid.TransactionID(msgID),
			Timestamp:        time.UnixMilli(int64(item.DateSent)),
			StreamOrder:      streamOrder,
			Reactions:        convertedReactions,
		})
	}
	return &bridgev2.FetchMessagesResponse{
		Messages:         convertedMessages,
		HasMore:          len(items) >= params.Count,
		Forward:          params.Forward,
		MarkRead:         isRead,
		ApproxTotalCount: chat.TotalMessages,
		CompleteCallback: func() {
			// When reaching the last backwards backfill batch, delete the chat from the backup store.
			// If backwards backfilling isn't enabled, delete immediately after the first backfill request.
			if (!params.Forward && len(items) < params.Count) || (!s.Main.Bridge.Config.Backfill.Queue.Enabled && !s.Main.Bridge.Config.Backfill.WillPaginateManually) {
				err := s.Client.Store.BackupStore.DeleteBackupChat(ctx, chat.Id)
				if err != nil {
					zerolog.Ctx(ctx).Err(err).Msg("Failed to delete chat from backup store")
				} else {
					zerolog.Ctx(ctx).Debug().Msg("Deleted chat from backup store as backfill seems finished")
				}
			} else {
				err := s.Client.Store.BackupStore.DeleteBackupChatItems(ctx, chat.Id, minTS)
				if err != nil {
					zerolog.Ctx(ctx).Err(err).Time("min_ts", minTS).Msg("Failed to delete messages from backup store")
				} else {
					zerolog.Ctx(ctx).Debug().Time("min_ts", minTS).Msg("Deleted messages from backup store")
				}
			}
		},
	}, nil
}
