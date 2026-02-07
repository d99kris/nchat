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

var _ SessionStore = (*scopedSQLStore)(nil)

const (
	loadSessionQuery  = `SELECT their_service_id, their_device_id, record FROM signalmeow_sessions WHERE account_id=$1 AND service_id=$2 AND their_service_id=$3 AND their_device_id=$4`
	storeSessionQuery = `
		INSERT INTO signalmeow_sessions (account_id, service_id, their_service_id, their_device_id, record)
		VALUES ($1, $2, $3, $4, $5)
		ON CONFLICT (account_id, service_id, their_service_id, their_device_id) DO UPDATE SET record=excluded.record
	`
	allSessionsQuery                = `SELECT their_service_id, their_device_id, record FROM signalmeow_sessions WHERE account_id=$1 AND service_id=$2 AND their_service_id=$3`
	removeSessionQuery              = `DELETE FROM signalmeow_sessions WHERE account_id=$1 AND service_id=$2 AND their_service_id=$3 AND their_device_id=$4`
	removeSessionsForRecipientQuery = "DELETE FROM signalmeow_sessions WHERE account_id=$1 AND their_service_id=$2"
	deleteAllSessionsQuery          = "DELETE FROM signalmeow_sessions WHERE account_id=$1"
)

type SessionAddressTuple = libsignalgo.SessionAddressTuple

type SessionStore interface {
	libsignalgo.SessionStore
	ServiceScopedStore

	// AllSessionsForServiceID returns all sessions for the given service ID.
	AllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) ([]SessionAddressTuple, error)
	// RemoveSession removes the session for the given address.
	RemoveSession(ctx context.Context, address *libsignalgo.Address) error
	RemoveAllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) error
	// RemoveAllSessions removes all sessions for our ACI UUID
	RemoveAllSessions(ctx context.Context) error
}

func scanSessionRecord(row dbutil.Scannable) (tuple SessionAddressTuple, err error) {
	var rawServiceID string
	var rawRecord []byte
	err = row.Scan(&rawServiceID, &tuple.DeviceID, &rawRecord)
	if errors.Is(err, sql.ErrNoRows) {
		err = nil
	} else if err != nil {
		// return error as-is
	} else if tuple.Record, err = libsignalgo.DeserializeSessionRecord(rawRecord); err != nil {
		err = fmt.Errorf("failed to deserialize session record: %w", err)
	} else if tuple.ServiceID, err = libsignalgo.ServiceIDFromString(rawServiceID); err != nil {
		err = fmt.Errorf("failed to parse service ID: %w", err)
	} else if tuple.Address, err = tuple.ServiceID.Address(uint(tuple.DeviceID)); err != nil {
		err = fmt.Errorf("failed to construct address: %w", err)
	}
	return
}

func (s *scopedSQLStore) RemoveSession(ctx context.Context, address *libsignalgo.Address) error {
	theirServiceID, err := address.Name()
	if err != nil {
		return fmt.Errorf("failed to get their service ID: %w", err)
	}
	deviceID, err := address.DeviceID()
	if err != nil {
		return fmt.Errorf("failed to get their device ID: %w", err)
	}
	_, err = s.db.Exec(ctx, removeSessionQuery, s.AccountID, s.ServiceID, theirServiceID, deviceID)
	return err
}

func (s *scopedSQLStore) AllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) ([]SessionAddressTuple, error) {
	rows, err := s.db.Query(ctx, allSessionsQuery, s.AccountID, s.ServiceID, theirID)
	if err != nil {
		return nil, err
	}
	return dbutil.NewRowIterWithError(rows, scanSessionRecord, err).AsList()
}

func (s *scopedSQLStore) RemoveAllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) error {
	_, err := s.db.Exec(ctx, removeSessionsForRecipientQuery, s.AccountID, theirID)
	return err
}

func (s *scopedSQLStore) LoadSession(ctx context.Context, address *libsignalgo.Address) (*libsignalgo.SessionRecord, error) {
	theirServiceID, err := address.Name()
	if err != nil {
		return nil, fmt.Errorf("failed to get their service ID: %w", err)
	}
	deviceID, err := address.DeviceID()
	if err != nil {
		return nil, fmt.Errorf("failed to get their device ID: %w", err)
	}
	tuple, err := scanSessionRecord(s.db.QueryRow(ctx, loadSessionQuery, s.AccountID, s.ServiceID, theirServiceID, deviceID))
	return tuple.Record, err
}

func (s *scopedSQLStore) StoreSession(ctx context.Context, address *libsignalgo.Address, record *libsignalgo.SessionRecord) error {
	theirServiceID, err := address.Name()
	if err != nil {
		return fmt.Errorf("failed to get their service ID: %w", err)
	}
	deviceID, err := address.DeviceID()
	if err != nil {
		return fmt.Errorf("failed to get their device ID: %w", err)
	}
	serialized, err := record.Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize session record: %w", err)
	}
	_, err = s.db.Exec(ctx, storeSessionQuery, s.AccountID, s.ServiceID, theirServiceID, deviceID, serialized)
	return err
}

func (s *scopedSQLStore) RemoveAllSessions(ctx context.Context) error {
	_, err := s.db.Exec(ctx, deleteAllSessionsQuery, s.AccountID)
	return err
}
