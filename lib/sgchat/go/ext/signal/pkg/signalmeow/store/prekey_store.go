// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

	"go.mau.fi/util/dbutil"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

var _ PreKeyStore = (*scopedSQLStore)(nil)

type ServiceScopedStore interface {
	GetServiceID() libsignalgo.ServiceID
}

type PreKeyStore interface {
	libsignalgo.PreKeyStore
	libsignalgo.SignedPreKeyStore
	libsignalgo.KyberPreKeyStore
	ServiceScopedStore

	StoreLastResortKyberPreKey(ctx context.Context, preKeyID uint32, record *libsignalgo.KyberPreKeyRecord) error
	RemoveSignedPreKey(ctx context.Context, preKeyID uint32) error
	RemoveKyberPreKey(ctx context.Context, preKeyID uint32) error
	GetNextPreKeyID(ctx context.Context) (count, max uint32, err error)
	GetNextKyberPreKeyID(ctx context.Context) (count, max uint32, err error)
	IsKyberPreKeyLastResort(ctx context.Context, preKeyID uint32) (bool, error)
	AllPreKeys(ctx context.Context) ([]*libsignalgo.PreKeyRecord, error)
	AllNormalKyberPreKeys(ctx context.Context) ([]*libsignalgo.KyberPreKeyRecord, error)
	DeleteAllPreKeys(ctx context.Context) error
}

const (
	getAllPreKeysQuery   = `SELECT key_pair FROM signalmeow_pre_keys WHERE account_id=$1 AND service_id=$2 AND is_signed=$3`
	getPreKeyQuery       = `SELECT key_pair FROM signalmeow_pre_keys WHERE account_id=$1 AND service_id=$2 AND key_id=$3 AND is_signed=$4`
	insertPreKeyQuery    = `INSERT INTO signalmeow_pre_keys (account_id, service_id, key_id, is_signed, key_pair) VALUES ($1, $2, $3, $4, $5)`
	deletePreKeyQuery    = `DELETE FROM signalmeow_pre_keys WHERE account_id=$1 AND service_id=$2 AND key_id=$3 AND is_signed=$4`
	getLastPreKeyIDQuery = `SELECT COUNT(*), COALESCE(MAX(key_id), 0) FROM signalmeow_pre_keys WHERE account_id=$1 AND service_id=$2 AND is_signed=$3`

	getAllKyberPreKeysQuery   = `SELECT key_pair FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2 AND is_last_resort=false`
	getKyberPreKeyQuery       = `SELECT key_pair FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2 AND key_id=$3`
	insertKyberPreKeyQuery    = `INSERT INTO signalmeow_kyber_pre_keys (account_id, service_id, key_id, key_pair, is_last_resort) VALUES ($1, $2, $3, $4, $5)`
	deleteKyberPreKeyQuery    = `DELETE FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2 AND key_id=$3`
	getLastKyberPreKeyIDQuery = `SELECT COUNT(*), COALESCE(MAX(key_id), 0) FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2`
	isLastResortQuery         = `SELECT is_last_resort FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2 AND key_id=$3`
)

func scanRecord[T any](row dbutil.Scannable, deserializer func([]byte) (*T, error)) (*T, error) {
	record, err := dbutil.ScanSingleColumn[[]byte](row)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	return deserializer(record)
}

func scanPreKey(row dbutil.Scannable) (*libsignalgo.PreKeyRecord, error) {
	return scanRecord(row, libsignalgo.DeserializePreKeyRecord)
}

func scanSignedPreKey(row dbutil.Scannable) (*libsignalgo.SignedPreKeyRecord, error) {
	return scanRecord(row, libsignalgo.DeserializeSignedPreKeyRecord)
}

func scanKyberPreKey(row dbutil.Scannable) (*libsignalgo.KyberPreKeyRecord, error) {
	return scanRecord(row, libsignalgo.DeserializeKyberPreKeyRecord)
}

func (s *scopedSQLStore) GetServiceID() libsignalgo.ServiceID {
	return s.ServiceID
}

func (s *scopedSQLStore) LoadPreKey(ctx context.Context, preKeyID uint32) (*libsignalgo.PreKeyRecord, error) {
	return scanPreKey(s.db.QueryRow(ctx, getPreKeyQuery, s.AccountID, s.ServiceID, preKeyID, false))
}

func (s *scopedSQLStore) LoadSignedPreKey(ctx context.Context, preKeyID uint32) (*libsignalgo.SignedPreKeyRecord, error) {
	return scanSignedPreKey(s.db.QueryRow(ctx, getPreKeyQuery, s.AccountID, s.ServiceID, preKeyID, true))
}

func (s *scopedSQLStore) LoadKyberPreKey(ctx context.Context, preKeyID uint32) (*libsignalgo.KyberPreKeyRecord, error) {
	return scanKyberPreKey(s.db.QueryRow(ctx, getKyberPreKeyQuery, s.AccountID, s.ServiceID, preKeyID))
}

func (s *scopedSQLStore) StorePreKey(ctx context.Context, preKeyID uint32, preKey *libsignalgo.PreKeyRecord) error {
	id, err := preKey.GetID()
	if err != nil {
		return fmt.Errorf("failed to get prekey ID: %w", err)
	} else if preKeyID > 0 && id != preKeyID {
		return fmt.Errorf("prekey ID mismatch: expected %d, got %d", preKeyID, id)
	}
	serialized, err := preKey.Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize prekey: %w", err)
	}
	_, err = s.db.Exec(ctx, insertPreKeyQuery, s.AccountID, s.ServiceID, id, false, serialized)
	return err
}

func (s *scopedSQLStore) StoreSignedPreKey(ctx context.Context, preKeyID uint32, preKey *libsignalgo.SignedPreKeyRecord) error {
	id, err := preKey.GetID()
	if err != nil {
		return fmt.Errorf("failed to get signed prekey ID: %w", err)
	} else if preKeyID > 0 && id != preKeyID {
		return fmt.Errorf("prekey ID mismatch: expected %d, got %d", preKeyID, id)
	}
	serialized, err := preKey.Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize signed prekey: %w", err)
	}
	_, err = s.db.Exec(ctx, insertPreKeyQuery, s.AccountID, s.ServiceID, id, true, serialized)
	return err
}

func (s *scopedSQLStore) StoreKyberPreKey(ctx context.Context, preKeyID uint32, kyberPreKeyRecord *libsignalgo.KyberPreKeyRecord) error {
	return s.storeKyberPreKey(ctx, preKeyID, kyberPreKeyRecord, false)
}

func (s *scopedSQLStore) StoreLastResortKyberPreKey(ctx context.Context, preKeyID uint32, kyberPreKeyRecord *libsignalgo.KyberPreKeyRecord) error {
	return s.storeKyberPreKey(ctx, preKeyID, kyberPreKeyRecord, true)
}

func (s *scopedSQLStore) storeKyberPreKey(ctx context.Context, preKeyID uint32, kyberPreKeyRecord *libsignalgo.KyberPreKeyRecord, lastResort bool) error {
	id, err := kyberPreKeyRecord.GetID()
	if err != nil {
		return fmt.Errorf("failed to get kyber prekey record ID: %w", err)
	} else if preKeyID > 0 && id != preKeyID {
		return fmt.Errorf("prekey ID mismatch: expected %d, got %d", preKeyID, id)
	}
	serialized, err := kyberPreKeyRecord.Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize kyber prekey record: %w", err)
	}
	_, err = s.db.Exec(ctx, insertKyberPreKeyQuery, s.AccountID, s.ServiceID, id, serialized, lastResort)
	return err
}

func (s *scopedSQLStore) RemovePreKey(ctx context.Context, preKeyID uint32) error {
	_, err := s.db.Exec(ctx, deletePreKeyQuery, s.AccountID, s.ServiceID, preKeyID, false)
	return err
}

func (s *scopedSQLStore) RemoveSignedPreKey(ctx context.Context, preKeyID uint32) error {
	_, err := s.db.Exec(ctx, deletePreKeyQuery, s.AccountID, s.ServiceID, preKeyID, true)
	return err
}

func (s *scopedSQLStore) RemoveKyberPreKey(ctx context.Context, preKeyID uint32) error {
	_, err := s.db.Exec(ctx, deleteKyberPreKeyQuery, s.AccountID, s.ServiceID, preKeyID)
	return err
}

func (s *scopedSQLStore) MarkKyberPreKeyUsed(ctx context.Context, id uint32) error {
	isLastResort, err := s.IsKyberPreKeyLastResort(ctx, id)
	if err != nil {
		return err
	}
	if !isLastResort {
		return s.RemoveKyberPreKey(ctx, id)
	}
	return nil
}

func (s *scopedSQLStore) GetNextPreKeyID(ctx context.Context) (count, next uint32, err error) {
	err = s.db.QueryRow(ctx, getLastPreKeyIDQuery, s.AccountID, s.ServiceID, false).Scan(&count, &next)
	if err != nil {
		err = fmt.Errorf("failed to query next prekey ID: %w", err)
	}
	next++
	return
}

func (s *scopedSQLStore) GetNextKyberPreKeyID(ctx context.Context) (count, next uint32, err error) {
	err = s.db.QueryRow(ctx, getLastKyberPreKeyIDQuery, s.AccountID, s.ServiceID).Scan(&count, &next)
	if err != nil {
		err = fmt.Errorf("failed to query next kyber prekey ID: %w", err)
	}
	next++
	return
}

func (s *scopedSQLStore) IsKyberPreKeyLastResort(ctx context.Context, preKeyID uint32) (bool, error) {
	var isLastResort bool
	err := s.db.QueryRow(ctx, isLastResortQuery, s.AccountID, s.ServiceID, preKeyID).Scan(&isLastResort)
	if err != nil {
		return false, err
	}
	return isLastResort, nil
}

func (s *scopedSQLStore) DeleteAllPreKeys(ctx context.Context) error {
	return s.db.DoTxn(ctx, nil, func(ctx context.Context) error {
		_, err := s.db.Exec(ctx, "DELETE FROM signalmeow_pre_keys WHERE account_id=$1", s.AccountID)
		if err != nil {
			return err
		}
		_, err = s.db.Exec(ctx, "DELETE FROM signalmeow_kyber_pre_keys WHERE account_id=$1", s.AccountID)
		return err
	})
}

func (s *scopedSQLStore) AllPreKeys(ctx context.Context) ([]*libsignalgo.PreKeyRecord, error) {
	return dbutil.ConvertRowFn[*libsignalgo.PreKeyRecord](scanPreKey).
		NewRowIter(s.db.Query(ctx, getAllPreKeysQuery, s.AccountID, s.ServiceID, false)).
		AsList()
}

func (s *scopedSQLStore) AllNormalKyberPreKeys(ctx context.Context) ([]*libsignalgo.KyberPreKeyRecord, error) {
	return dbutil.ConvertRowFn[*libsignalgo.KyberPreKeyRecord](scanKyberPreKey).
		NewRowIter(s.db.Query(ctx, getAllKyberPreKeysQuery, s.AccountID, s.ServiceID)).
		AsList()
}
