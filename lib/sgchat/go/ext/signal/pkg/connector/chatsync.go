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
	"encoding/base64"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/simplevent"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

func (s *SignalClient) syncChats(ctx context.Context) {
	if s.UserLogin.Metadata.(*signalid.UserLoginMetadata).ChatsSynced {
		return
	}
	if s.Client.Store.EphemeralBackupKey != nil {
		zerolog.Ctx(ctx).Info().Msg("Fetching transfer archive before syncing chats")
		meta, err := s.Client.WaitForTransfer(ctx)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to request transfer archive")
			return
		} else if meta.Error != "" {
			zerolog.Ctx(ctx).Error().Str("error_type", meta.Error).Msg("Transfer archive request was rejected")
			s.UserLogin.Metadata.(*signalid.UserLoginMetadata).ChatsSynced = true
			err = s.UserLogin.Save(ctx)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to save user login metadata after transfer archive request was rejected")
			}
			return
		}
		err = s.Client.FetchAndProcessTransfer(ctx, meta)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to fetch and process transfer archive")
			return
		}
		zerolog.Ctx(ctx).Info().Msg("Transfer archive fetched and processed, syncing chats")
	}
	chats, err := s.Client.Store.BackupStore.GetBackupChats(ctx)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to get chats from backup store")
		return
	}
	zerolog.Ctx(ctx).Info().Int("chat_count", len(chats)).Msg("Fetched chats to sync from database")
	for _, chat := range chats {
		recipient, err := s.Client.Store.BackupStore.GetBackupRecipient(ctx, chat.RecipientId)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to get recipient for chat")
			continue
		}
		resyncEvt := &simplevent.ChatResync{
			EventMeta: simplevent.EventMeta{
				Type: bridgev2.RemoteEventChatResync,
				LogContext: func(c zerolog.Context) zerolog.Context {
					return c.
						Int("message_count", chat.TotalMessages).
						Uint64("backup_chat_id", chat.Id).
						Uint64("backup_recipient_id", chat.RecipientId)
				},
				CreatePortal: true,
			},
			LatestMessageTS: time.UnixMilli(int64(chat.LatestMessageID)),
		}
		switch dest := recipient.Destination.(type) {
		case *backuppb.Recipient_Contact:
			aci := tryCastUUID(dest.Contact.GetAci())
			pni := tryCastUUID(dest.Contact.GetPni())
			if chat.TotalMessages == 0 {
				zerolog.Ctx(ctx).Debug().
					Stringer("aci", aci).
					Stringer("pni", pni).
					Uint64("e164", dest.Contact.GetE164()).
					Msg("Skipping direct chat with no messages and deleting data")
				err = s.Client.Store.BackupStore.DeleteBackupChat(ctx, chat.Id)
				if err != nil {
					zerolog.Ctx(ctx).Err(err).Msg("Failed to delete chat from backup store")
				}
				continue
			}
			processedRecipient, err := s.Client.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aci, pni, nil)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to get full recipient data")
				continue
			}
			dmInfo := s.makeCreateDMResponse(ctx, processedRecipient, chat)
			resyncEvt.PortalKey = dmInfo.PortalKey
			resyncEvt.ChatInfo = dmInfo.PortalInfo
		case *backuppb.Recipient_Self:
			processedRecipient, err := s.Client.Store.RecipientStore.LoadAndUpdateRecipient(ctx, s.Client.Store.ACI, uuid.Nil, nil)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to get full recipient data")
				continue
			}
			dmInfo := s.makeCreateDMResponse(ctx, processedRecipient, chat)
			resyncEvt.PortalKey = dmInfo.PortalKey
			resyncEvt.ChatInfo = dmInfo.PortalInfo
		case *backuppb.Recipient_Group:
			if len(dest.Group.MasterKey) != libsignalgo.GroupMasterKeyLength {
				continue
			}
			rawGroupID, err := libsignalgo.GroupMasterKey(dest.Group.MasterKey).GroupIdentifier()
			if err != nil {
				zerolog.Ctx(ctx).Err(err).
					Uint64("recipient_id", recipient.Id).
					Msg("Failed to get group identifier from master key")
				continue
			}
			groupID := types.GroupIdentifier(base64.StdEncoding.EncodeToString(rawGroupID[:]))
			groupInfo, err := s.getGroupInfo(ctx, groupID, dest.Group.GetSnapshot().GetVersion(), chat)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to get full group info")
				continue
			}
			resyncEvt.PortalKey = s.makePortalKey(string(groupID))
			resyncEvt.ChatInfo = groupInfo
		default:
			zerolog.Ctx(ctx).Debug().
				Type("destination_type", dest).
				Uint64("backup_chat_id", chat.Id).
				Uint64("backup_recipient_id", chat.RecipientId).
				Msg("Ignoring and deleting chat with unsupported destination type")
			err = s.Client.Store.BackupStore.DeleteBackupChat(ctx, chat.Id)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to delete chat from backup store")
			}
			continue
		}
		if !s.UserLogin.QueueRemoteEvent(resyncEvt).Success {
			return
		}
	}
	s.UserLogin.Metadata.(*signalid.UserLoginMetadata).ChatsSynced = true
	err = s.UserLogin.Save(ctx)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to save user login metadata after syncing chats")
	}
}
