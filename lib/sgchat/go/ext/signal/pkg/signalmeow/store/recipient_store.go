// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

package store

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/dbutil"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

type RecipientStore interface {
	LoadProfileKey(ctx context.Context, theirACI uuid.UUID) (*libsignalgo.ProfileKey, error)
	StoreProfileKey(ctx context.Context, theirACI uuid.UUID, key libsignalgo.ProfileKey) error
	MyProfileKey(ctx context.Context) (*libsignalgo.ProfileKey, error)

	LoadAndUpdateRecipient(ctx context.Context, aci, pni uuid.UUID, updater RecipientUpdaterFunc) (*types.Recipient, error)
	IsBlocked(ctx context.Context, aci uuid.UUID) (bool, error)
	LoadRecipientByE164(ctx context.Context, e164 string) (*types.Recipient, error)
	StoreRecipient(ctx context.Context, recipient *types.Recipient) error
	UpdateRecipientE164(ctx context.Context, aci, pni uuid.UUID, e164 string) (*types.Recipient, error)

	IsUnregistered(ctx context.Context, serviceID libsignalgo.ServiceID) bool
	MarkUnregistered(ctx context.Context, serviceID libsignalgo.ServiceID, unregistered bool)

	LoadAllContacts(ctx context.Context) ([]*types.Recipient, error)
}

var _ RecipientStore = (*sqlStore)(nil)

const (
	getAllRecipientsQuery = `
		SELECT
			aci_uuid,
			pni_uuid,
			e164_number,
			contact_name,
			contact_avatar_hash,
			nickname,
			profile_key,
			profile_name,
			profile_about,
			profile_about_emoji,
			profile_avatar_path,
			profile_fetched_at,
			needs_pni_signature,
			blocked,
			whitelisted
		FROM signalmeow_recipients
		WHERE account_id = $1
	`
	getAllRecipientsWithNameOrPhoneQuery = getAllRecipientsQuery + `AND (contact_name <> '' OR profile_name <> '' OR e164_number <> '')`
	getRecipientByACIQuery               = getAllRecipientsQuery + `AND aci_uuid = $2`
	getRecipientByPNIQuery               = getAllRecipientsQuery + `AND pni_uuid = $2`
	getRecipientByACIOrPNIQuery          = getAllRecipientsQuery + `AND (($2<>'00000000-0000-0000-0000-000000000000' AND aci_uuid = $2) OR ($3<>'00000000-0000-0000-0000-000000000000' AND pni_uuid = $3))`
	getRecipientByPhoneQuery             = getAllRecipientsQuery + `AND e164_number = $2`
	deleteRecipientByPNIQuery            = `DELETE FROM signalmeow_recipients WHERE account_id = $1 AND pni_uuid = $2`
	upsertACIRecipientQuery              = `
		INSERT INTO signalmeow_recipients (
			account_id,
			aci_uuid,
			pni_uuid,
			e164_number,
			contact_name,
			contact_avatar_hash,
			nickname,
			profile_key,
			profile_name,
			profile_about,
			profile_about_emoji,
			profile_avatar_path,
			profile_fetched_at,
			needs_pni_signature,
			blocked,
			whitelisted
		)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)
		ON CONFLICT (account_id, aci_uuid) DO UPDATE SET
			pni_uuid = excluded.pni_uuid,
			e164_number = excluded.e164_number,
			contact_name = excluded.contact_name,
			contact_avatar_hash = excluded.contact_avatar_hash,
			nickname = excluded.nickname,
			profile_key = excluded.profile_key,
			profile_name = excluded.profile_name,
			profile_about = excluded.profile_about,
			profile_about_emoji = excluded.profile_about_emoji,
			profile_avatar_path = excluded.profile_avatar_path,
			profile_fetched_at = excluded.profile_fetched_at,
			needs_pni_signature = excluded.needs_pni_signature,
			blocked = excluded.blocked,
			whitelisted = excluded.whitelisted
	`
	upsertPNIRecipientQuery = `
		INSERT INTO signalmeow_recipients (
			account_id,
			pni_uuid,
			e164_number,
			contact_name,
			contact_avatar_hash
		)
		VALUES ($1, $2, $3, $4, $5)
		ON CONFLICT (account_id, pni_uuid) DO UPDATE SET
			e164_number = excluded.e164_number,
			contact_name = excluded.contact_name,
			contact_avatar_hash = excluded.contact_avatar_hash
	`
)

func scanRecipient(row dbutil.Scannable) (*types.Recipient, error) {
	var recipient types.Recipient
	var aci, pni uuid.NullUUID
	var profileKey []byte
	var profileFetchedAt sql.NullInt64
	err := row.Scan(
		&aci,
		&pni,
		&recipient.E164,
		&recipient.ContactName,
		&recipient.ContactAvatar.Hash,
		&recipient.Nickname,
		&profileKey,
		&recipient.Profile.Name,
		&recipient.Profile.About,
		&recipient.Profile.AboutEmoji,
		&recipient.Profile.AvatarPath,
		&profileFetchedAt,
		&recipient.NeedsPNISignature,
		&recipient.Blocked,
		&recipient.Whitelisted,
	)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	recipient.ACI = aci.UUID
	recipient.PNI = pni.UUID
	if profileFetchedAt.Valid {
		recipient.Profile.FetchedAt = time.UnixMilli(profileFetchedAt.Int64)
	}
	if len(profileKey) == libsignalgo.ProfileKeyLength {
		recipient.Profile.Key = libsignalgo.ProfileKey(profileKey)
	}
	return &recipient, err
}

func (s *sqlStore) LoadRecipientByACI(ctx context.Context, theirUUID uuid.UUID) (*types.Recipient, error) {
	return scanRecipient(s.db.QueryRow(ctx, getRecipientByACIQuery, s.AccountID, theirUUID))
}

func (s *sqlStore) LoadRecipientByPNI(ctx context.Context, theirUUID uuid.UUID) (*types.Recipient, error) {
	return scanRecipient(s.db.QueryRow(ctx, getRecipientByPNIQuery, s.AccountID, theirUUID))
}

type RecipientUpdaterFunc func(recipient *types.Recipient) (changed bool, err error)

func (s *sqlStore) mergeRecipients(ctx context.Context, first, second *types.Recipient, updater RecipientUpdaterFunc) (*types.Recipient, error) {
	if first.ACI == uuid.Nil {
		first, second = second, first
	}
	first.PNI = second.PNI
	zerolog.Ctx(ctx).Debug().
		Stringer("aci", first.ACI).
		Stringer("pni", first.PNI).
		Msg("Merging recipient entries in database")
	if second.E164 != "" {
		first.E164 = second.E164
	}
	if first.ContactName == "" {
		first.ContactName = second.ContactName
	}
	if first.ContactAvatar.Hash == "" {
		first.ContactAvatar = second.ContactAvatar
	}
	_, err := updater(first)
	if err != nil {
		return first, fmt.Errorf("failed to run updater function: %w", err)
	}
	err = s.DeleteRecipientByPNI(ctx, first.PNI)
	if err != nil {
		return first, fmt.Errorf("failed to delete duplicate PNI row: %w", err)
	}
	err = s.StoreRecipient(ctx, first)
	if err != nil {
		return first, fmt.Errorf("failed to store merged row: %w", err)
	}
	return first, nil
}

func (s *sqlStore) LoadAndUpdateRecipient(ctx context.Context, aci, pni uuid.UUID, updater RecipientUpdaterFunc) (outRecipient *types.Recipient, outErr error) {
	if aci == uuid.Nil && pni == uuid.Nil {
		return nil, fmt.Errorf("no ACI or PNI provided in LoadAndUpdateRecipient call")
	}
	if updater == nil {
		updater = func(recipient *types.Recipient) (bool, error) {
			return false, nil
		}
	}
	defer func() {
		if outRecipient != nil && outRecipient.ACI != uuid.Nil && outErr == nil {
			s.blockCacheLock.Lock()
			s.blockCache[outRecipient.ACI] = outRecipient.Blocked
			s.blockCacheLock.Unlock()
		}
	}()
	if ctx.Value(contextKeyContactLock) == nil {
		s.contactLock.Lock()
		defer s.contactLock.Unlock()
	}
	outErr = s.db.DoTxn(ctx, nil, func(ctx context.Context) error {
		var entries []*types.Recipient
		var err error
		if aci != uuid.Nil && pni != uuid.Nil {
			query := getRecipientByACIOrPNIQuery
			if s.db.Dialect == dbutil.Postgres {
				query += " FOR UPDATE"
			}
			entries, err = dbutil.ConvertRowFn[*types.Recipient](scanRecipient).
				NewRowIter(s.db.Query(ctx, query, s.AccountID, aci, pni)).
				AsList()
		} else if aci != uuid.Nil {
			var entry *types.Recipient
			entry, err = s.LoadRecipientByACI(ctx, aci)
			if entry != nil {
				entries = []*types.Recipient{entry}
			}
		} else if pni != uuid.Nil {
			var entry *types.Recipient
			entry, err = s.LoadRecipientByPNI(ctx, pni)
			if entry != nil {
				entries = []*types.Recipient{entry}
			}
		} else {
			panic("impossible case")
		}
		if err != nil {
			return err
		} else if len(entries) > 2 {
			return fmt.Errorf("got more than two recipient rows for ACI %s and PNI %s", aci, pni)
		} else if len(entries) < 2 {
			if len(entries) == 0 {
				outRecipient = &types.Recipient{
					ACI: aci,
					PNI: pni,
				}
			} else {
				outRecipient = entries[0]
			}
			changed, err := updater(outRecipient)
			if err != nil {
				return fmt.Errorf("failed to run updater function: %w", err)
			}
			// SQL only supports one ON CONFLICT clause, which means StoreRecipient will key on the ACI if it's present.
			// If we're adding an ACI to a PNI row, just delete the PNI row first to avoid conflicts on the PNI key.
			if outRecipient.PNI != uuid.Nil && outRecipient.ACI == uuid.Nil && aci != uuid.Nil {
				zerolog.Ctx(ctx).Debug().
					Stringer("aci", outRecipient.ACI).
					Stringer("pni", outRecipient.PNI).
					Msg("Deleting old PNI-only row before inserting row with both IDs")
				err = s.DeleteRecipientByPNI(ctx, outRecipient.PNI)
				if err != nil {
					return fmt.Errorf("failed to delete old PNI row: %w", err)
				}
			}
			if outRecipient.PNI == uuid.Nil && pni != uuid.Nil {
				outRecipient.PNI = pni
				changed = true
			}
			if outRecipient.ACI == uuid.Nil && aci != uuid.Nil {
				outRecipient.ACI = aci
				changed = true
			}
			if changed || len(entries) == 0 {
				zerolog.Ctx(ctx).Trace().
					Stringer("aci", outRecipient.ACI).
					Stringer("pni", outRecipient.PNI).
					Msg("Saving recipient row")
				err = s.StoreRecipient(ctx, outRecipient)
				if err != nil {
					return fmt.Errorf("failed to store updated recipient row: %w", err)
				}
			}
			return nil
		} else if outRecipient, err = s.mergeRecipients(ctx, entries[0], entries[1], updater); err != nil {
			return fmt.Errorf("failed to merge recipient rows for ACI %s and PNI %s: %w", aci, pni, err)
		} else {
			return nil
		}
	})
	return
}

func (s *sqlStore) IsBlocked(ctx context.Context, aci uuid.UUID) (bool, error) {
	s.blockCacheLock.RLock()
	cachedVal, ok := s.blockCache[aci]
	s.blockCacheLock.RUnlock()
	if ok {
		return cachedVal, nil
	}
	recipient, err := s.LoadAndUpdateRecipient(ctx, aci, uuid.Nil, nil)
	if err != nil {
		return false, err
	}
	return recipient.Blocked, nil
}

func (s *sqlStore) UpdateRecipientE164(ctx context.Context, aci, pni uuid.UUID, e164 string) (*types.Recipient, error) {
	return s.LoadAndUpdateRecipient(ctx, aci, pni, func(recipient *types.Recipient) (bool, error) {
		if recipient.E164 != e164 {
			recipient.E164 = e164
			return true, nil
		}
		return false, nil
	})
}
func (s *sqlStore) LoadRecipientByE164(ctx context.Context, e164 string) (*types.Recipient, error) {
	return scanRecipient(s.db.QueryRow(ctx, getRecipientByPhoneQuery, s.AccountID, e164))
}

func (s *sqlStore) LoadAllContacts(ctx context.Context) ([]*types.Recipient, error) {
	rows, err := s.db.Query(ctx, getAllRecipientsWithNameOrPhoneQuery, s.AccountID)
	return dbutil.NewRowIterWithError(rows, scanRecipient, err).AsList()
}

func (s *sqlStore) DeleteRecipientByPNI(ctx context.Context, pni uuid.UUID) error {
	_, err := s.db.Exec(ctx, deleteRecipientByPNIQuery, s.AccountID, pni)
	return err
}

func nullableUUID(u uuid.UUID) uuid.NullUUID {
	return uuid.NullUUID{UUID: u, Valid: u != uuid.Nil}
}

func (s *sqlStore) StoreRecipient(ctx context.Context, recipient *types.Recipient) (err error) {
	if recipient.ACI != uuid.Nil {
		_, err = s.db.Exec(
			ctx,
			upsertACIRecipientQuery,
			s.AccountID,
			recipient.ACI,
			nullableUUID(recipient.PNI),
			recipient.E164,
			recipient.ContactName,
			recipient.ContactAvatar.Hash,
			recipient.Nickname,
			recipient.Profile.Key.Slice(),
			recipient.Profile.Name,
			recipient.Profile.About,
			recipient.Profile.AboutEmoji,
			recipient.Profile.AvatarPath,
			dbutil.UnixMilliPtr(recipient.Profile.FetchedAt),
			recipient.NeedsPNISignature,
			recipient.Blocked,
			recipient.Whitelisted,
		)
		s.blockCacheLock.Lock()
		s.blockCache[recipient.ACI] = recipient.Blocked
		s.blockCacheLock.Unlock()
	} else if recipient.PNI != uuid.Nil {
		_, err = s.db.Exec(
			ctx,
			upsertPNIRecipientQuery,
			s.AccountID,
			recipient.PNI,
			recipient.E164,
			recipient.ContactName,
			recipient.ContactAvatar.Hash,
		)
	} else {
		err = fmt.Errorf("no ACI or PNI provided in StoreRecipient call")
	}
	return
}

const (
	isUnregisteredQuery   = `SELECT 1 FROM signalmeow_unregistered_users WHERE aci_uuid=$1`
	markUnregisteredQuery = `INSERT INTO signalmeow_unregistered_users (aci_uuid) VALUES ($1) ON CONFLICT (aci_uuid) DO NOTHING`
	markRegisteredQuery   = `DELETE FROM signalmeow_unregistered_users WHERE aci_uuid=$1`
)

func (s *sqlStore) IsUnregistered(ctx context.Context, serviceID libsignalgo.ServiceID) (unregistered bool) {
	if serviceID.Type != libsignalgo.ServiceIDTypeACI {
		return
	}
	_ = s.db.QueryRow(ctx, isUnregisteredQuery, serviceID.UUID).Scan(&unregistered)
	return
}

func (s *sqlStore) MarkUnregistered(ctx context.Context, serviceID libsignalgo.ServiceID, unregistered bool) {
	if serviceID.Type != libsignalgo.ServiceIDTypeACI {
		return
	}
	var err error
	if unregistered {
		_, err = s.db.Exec(ctx, markUnregisteredQuery, serviceID.UUID)
	} else {
		_, err = s.db.Exec(ctx, markRegisteredQuery, serviceID.UUID)
	}
	if err != nil {
		zerolog.Ctx(ctx).Err(err).
			Stringer("service_id", serviceID).
			Bool("unregistered", unregistered).
			Msg("Failed to mark recipient as unregistered")
	}
}
