// mautrix-signal - A Matrix-signal puppeting bridge.
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

package store

import (
	"context"
	"database/sql"
	"encoding/base64"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/dbutil"
	"go.mau.fi/util/ptr"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

type BackupChat struct {
	*backuppb.Chat
	TotalMessages   int
	LatestMessageID uint64
}

type BackupStore interface {
	AddBackupRecipient(ctx context.Context, recipient *backuppb.Recipient) error
	AddBackupChat(ctx context.Context, chat *backuppb.Chat) error
	AddBackupChatItem(ctx context.Context, item *backuppb.ChatItem) error
	RecalculateChatCounts(ctx context.Context) error
	ClearBackup(ctx context.Context) error

	GetBackupRecipient(ctx context.Context, recipientID uint64) (*backuppb.Recipient, error)
	GetBackupChatByUserID(ctx context.Context, userID libsignalgo.ServiceID) (*BackupChat, error)
	GetBackupChatByGroupID(ctx context.Context, groupID types.GroupIdentifier) (*BackupChat, error)
	GetBackupChats(ctx context.Context) ([]*BackupChat, error)
	GetBackupChatItems(ctx context.Context, chatID uint64, anchor time.Time, forward bool, limit int) ([]*backuppb.ChatItem, error)
	DeleteBackupChat(ctx context.Context, chatID uint64) error
	DeleteBackupChatItems(ctx context.Context, chatID uint64, minTime time.Time) error
}

var _ BackupStore = (*sqlStore)(nil)

const (
	addBackupRecipientQuery = `
		INSERT INTO signalmeow_backup_recipient (account_id, recipient_id, aci_uuid, pni_uuid, group_master_key, data)
		VALUES ($1, $2, $3, $4, $5, $6)
	`
	addBackupChatQuery = `
		INSERT INTO signalmeow_backup_chat (account_id, chat_id, recipient_id, data)
		VALUES ($1, $2, $3, $4)
	`
	addBackupChatItemQuery = `
		INSERT INTO signalmeow_backup_message (account_id, chat_id, sender_id, message_id, data)
		VALUES ($1, $2, $3, $4, $5)
		ON CONFLICT DO NOTHING
	`

	getBackupRecipientQuery = `
		SELECT data FROM signalmeow_backup_recipient WHERE account_id=$1 AND recipient_id=$2
	`
	getBackupChatByACIQuery = `
		SELECT chat.data, chat.latest_message_id, chat.total_message_count FROM signalmeow_backup_recipient rcp
		INNER JOIN signalmeow_backup_chat chat ON rcp.account_id=chat.account_id AND rcp.recipient_id=chat.recipient_id
		WHERE rcp.account_id=$1 AND rcp.aci_uuid=$2
	`
	getBackupChatByPNIQuery = `
		SELECT chat.data, chat.latest_message_id, chat.total_message_count FROM signalmeow_backup_recipient rcp
		INNER JOIN signalmeow_backup_chat chat ON rcp.account_id=chat.account_id AND rcp.recipient_id=chat.recipient_id
		WHERE rcp.account_id=$1 AND rcp.pni_uuid=$2
	`
	getBackupChatByGroupIDQuery = `
		SELECT chat.data, chat.latest_message_id, chat.total_message_count FROM signalmeow_groups g
		INNER JOIN signalmeow_backup_recipient rcp ON g.account_id=rcp.account_id AND g.master_key=rcp.group_master_key
		INNER JOIN signalmeow_backup_chat chat ON rcp.account_id=chat.account_id AND rcp.recipient_id=chat.recipient_id
		WHERE g.account_id=$1 AND g.group_identifier=$2
	`
	getAllBackupChatsQuery = `
		SELECT data, latest_message_id, total_message_count
		FROM signalmeow_backup_chat
		WHERE account_id=$1
	`
	getBackupChatItemsQuery = `
		SELECT data
		FROM signalmeow_backup_message
		WHERE account_id=$1 AND chat_id=$2 AND message_id > $3 AND message_id < $4
		ORDER BY message_id DESC
		LIMIT $5
	`
	deleteBackupChatQuery = `
		DELETE FROM signalmeow_backup_chat WHERE account_id=$1 AND chat_id=$2
	`
	deleteBackupChatItemsQuery = `
		DELETE FROM signalmeow_backup_message WHERE account_id=$1 AND chat_id=$2 AND message_id >= $3
	`
	recalculateChatCountsQuery = `
		UPDATE signalmeow_backup_chat
		SET latest_message_id = (
				SELECT message_id
				FROM signalmeow_backup_message
				WHERE account_id=signalmeow_backup_chat.account_id AND chat_id=signalmeow_backup_chat.chat_id
				ORDER BY message_id DESC
				LIMIT 1
			),
			total_message_count = (
				SELECT COUNT(*)
				FROM signalmeow_backup_message
				WHERE account_id=signalmeow_backup_chat.account_id AND chat_id=signalmeow_backup_chat.chat_id
			)
		WHERE account_id=$1
	`
)

func tryCastUUID(b []byte) uuid.UUID {
	if len(b) == 16 {
		return uuid.UUID(b)
	}
	return uuid.Nil
}

func (s *sqlStore) AddBackupRecipient(ctx context.Context, recipient *backuppb.Recipient) error {
	recipientData, err := proto.Marshal(recipient)
	if err != nil {
		return fmt.Errorf("failed to marshal recipient %d: %w", recipient.Id, err)
	}
	var aci, pni uuid.UUID
	var groupMasterKey types.SerializedGroupMasterKey
	switch dest := recipient.Destination.(type) {
	case *backuppb.Recipient_Contact:
		aci = tryCastUUID(dest.Contact.Aci)
		pni = tryCastUUID(dest.Contact.Pni)
		// TODO save identity key + trust level
		if aci != uuid.Nil || pni != uuid.Nil {
			_, err := s.LoadAndUpdateRecipient(ctx, aci, pni, func(recipient *types.Recipient) (changed bool, err error) {
				oldRecipient := ptr.Clone(recipient)
				if dest.Contact.E164 != nil {
					recipient.E164 = fmt.Sprintf("+%d", *dest.Contact.E164)
				}
				if len(dest.Contact.ProfileKey) == libsignalgo.ProfileKeyLength {
					recipient.Profile.Key = libsignalgo.ProfileKey(dest.Contact.ProfileKey)
				}
				if dest.Contact.ProfileGivenName != nil || dest.Contact.ProfileFamilyName != nil {
					recipient.Profile.Name = strings.TrimSpace(fmt.Sprintf("%s %s", dest.Contact.GetProfileGivenName(), dest.Contact.GetProfileFamilyName()))
				}
				if dest.Contact.ProfileSharing && !ptr.Val(recipient.Whitelisted) {
					recipient.Whitelisted = ptr.Ptr(true)
					changed = true
				}
				recipient.Blocked = dest.Contact.Blocked
				changed = changed ||
					oldRecipient.E164 != recipient.E164 ||
					oldRecipient.Profile.Key != recipient.Profile.Key ||
					oldRecipient.Profile.Name != recipient.Profile.Name ||
					oldRecipient.Blocked != recipient.Blocked
				return
			})
			if err != nil {
				return fmt.Errorf("failed to save info for recipient %d: %w", recipient.Id, err)
			}
		} else if dest.Contact.GetRegistered() != nil {
			zerolog.Ctx(ctx).Warn().
				Uint64("recipient_id", recipient.Id).
				Any("entry", dest.Contact).
				Msg("Both ACI and PNI are invalid for registered contact recipient")
		}
		if aci != uuid.Nil {
			s.MarkUnregistered(ctx, libsignalgo.NewACIServiceID(aci), dest.Contact.GetNotRegistered() != nil)
		}
	case *backuppb.Recipient_Group:
		groupMasterKey = types.SerializedGroupMasterKey(base64.StdEncoding.EncodeToString(dest.Group.MasterKey))
		if len(dest.Group.MasterKey) == libsignalgo.GroupMasterKeyLength {
			gid, err := libsignalgo.GroupMasterKey(dest.Group.MasterKey).GroupIdentifier()
			if err != nil {
				zerolog.Ctx(ctx).Err(err).
					Uint64("recipient_id", recipient.Id).
					Msg("Failed to get group identifier from master key")
			} else if err = s.StoreMasterKey(ctx, types.BytesToGroupIdentifier(gid), groupMasterKey); err != nil {
				return fmt.Errorf("failed to save group master key for recipient %d: %w", recipient.Id, err)
			}
		} else {
			zerolog.Ctx(ctx).Warn().
				Uint64("recipient_id", recipient.Id).
				Msg("Invalid group master key length")
		}
	case *backuppb.Recipient_Self:
		aci = s.AccountID
	default:
	}
	_, err = s.db.Exec(ctx, addBackupRecipientQuery, s.AccountID, recipient.Id, ptr.NonZero(aci), ptr.NonZero(pni), ptr.NonZero(groupMasterKey), recipientData)
	if err != nil {
		return fmt.Errorf("failed to add backup recipient %d: %w", recipient.Id, err)
	}
	return nil
}

func (s *sqlStore) AddBackupChat(ctx context.Context, chat *backuppb.Chat) error {
	chatData, err := proto.Marshal(chat)
	if err != nil {
		return fmt.Errorf("failed to marshal chat %d: %w", chat.Id, err)
	}
	_, err = s.db.Exec(ctx, addBackupChatQuery, s.AccountID, chat.Id, chat.RecipientId, chatData)
	if err != nil {
		return fmt.Errorf("failed to add backup chat %d: %w", chat.Id, err)
	}
	return nil
}

func (s *sqlStore) AddBackupChatItem(ctx context.Context, item *backuppb.ChatItem) error {
	itemData, err := proto.Marshal(item)
	if err != nil {
		return fmt.Errorf("failed to marshal chat item %d: %w", item.DateSent, err)
	}
	_, err = s.db.Exec(ctx, addBackupChatItemQuery, s.AccountID, item.ChatId, item.AuthorId, item.DateSent, itemData)
	if err != nil {
		return fmt.Errorf("failed to add backup chat item %d: %w", item.DateSent, err)
	}
	return nil
}

func (s *sqlStore) ClearBackup(ctx context.Context) error {
	_, err := s.db.Exec(ctx, "DELETE FROM signalmeow_backup_message WHERE account_id=$1", s.AccountID)
	if err != nil {
		return err
	}
	_, err = s.db.Exec(ctx, "DELETE FROM signalmeow_backup_chat WHERE account_id=$1", s.AccountID)
	if err != nil {
		return err
	}
	_, err = s.db.Exec(ctx, "DELETE FROM signalmeow_backup_recipient WHERE account_id=$1", s.AccountID)
	return err
}

func scanProto[T proto.Message](row dbutil.Scannable) (val T, err error) {
	var data []byte
	err = row.Scan(&data)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			err = nil
		}
		return
	}
	val = val.ProtoReflect().New().Interface().(T)
	err = proto.Unmarshal(data, val)
	return
}

func scanChat(row dbutil.Scannable) (*BackupChat, error) {
	var data []byte
	var latestMessageID, totalMessageCount sql.NullInt64
	err := row.Scan(&data, &latestMessageID, &totalMessageCount)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil
		}
		return nil, err
	}
	var chat backuppb.Chat
	err = proto.Unmarshal(data, &chat)
	if err != nil {
		return nil, err
	}
	return &BackupChat{
		Chat:            &chat,
		TotalMessages:   int(totalMessageCount.Int64),
		LatestMessageID: uint64(latestMessageID.Int64),
	}, nil
}

var chatScanner = dbutil.ConvertRowFn[*BackupChat](scanChat)
var messageScanner = dbutil.ConvertRowFn[*backuppb.ChatItem](scanProto[*backuppb.ChatItem])

func (s *sqlStore) GetBackupRecipient(ctx context.Context, recipientID uint64) (*backuppb.Recipient, error) {
	return scanProto[*backuppb.Recipient](s.db.QueryRow(ctx, getBackupRecipientQuery, s.AccountID, recipientID))
}

func (s *sqlStore) GetBackupChatByUserID(ctx context.Context, userID libsignalgo.ServiceID) (*BackupChat, error) {
	query := getBackupChatByACIQuery
	if userID.Type == libsignalgo.ServiceIDTypePNI {
		query = getBackupChatByPNIQuery
	}
	return scanChat(s.db.QueryRow(ctx, query, s.AccountID, userID.UUID))
}

func (s *sqlStore) GetBackupChatByGroupID(ctx context.Context, groupID types.GroupIdentifier) (*BackupChat, error) {
	return scanChat(s.db.QueryRow(ctx, getBackupChatByGroupIDQuery, s.AccountID, groupID))
}

func (s *sqlStore) GetBackupChats(ctx context.Context) ([]*BackupChat, error) {
	return chatScanner.NewRowIter(s.db.Query(ctx, getAllBackupChatsQuery, s.AccountID)).AsList()
}

func (s *sqlStore) GetBackupChatItems(ctx context.Context, chatID uint64, anchor time.Time, forward bool, limit int) ([]*backuppb.ChatItem, error) {
	var minTS, maxTS int64
	if anchor.IsZero() {
		maxTS = time.Now().Add(24 * time.Hour).UnixMilli()
	} else if forward {
		minTS = anchor.UnixMilli()
		maxTS = time.Now().Add(24 * time.Hour).UnixMilli()
	} else {
		maxTS = anchor.UnixMilli()
	}
	return messageScanner.NewRowIter(s.db.Query(ctx, getBackupChatItemsQuery, s.AccountID, chatID, minTS, maxTS, limit)).AsList()
}

func (s *sqlStore) DeleteBackupChatItems(ctx context.Context, chatID uint64, minTime time.Time) error {
	anchorTS := minTime.UnixMilli()
	if minTime.IsZero() {
		anchorTS = 0
	}
	_, err := s.db.Exec(ctx, deleteBackupChatItemsQuery, s.AccountID, chatID, anchorTS)
	return err
}

func (s *sqlStore) DeleteBackupChat(ctx context.Context, chatID uint64) error {
	_, err := s.db.Exec(ctx, deleteBackupChatQuery, s.AccountID, chatID)
	return err
}

func (s *sqlStore) RecalculateChatCounts(ctx context.Context) error {
	_, err := s.db.Exec(ctx, recalculateChatCountsQuery, s.AccountID)
	return err
}
