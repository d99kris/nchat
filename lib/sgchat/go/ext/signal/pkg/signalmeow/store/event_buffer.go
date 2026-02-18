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
	"errors"
	"time"
)

type BufferedEvent struct {
	Plaintext       []byte
	ServerTimestamp uint64
	InsertTimestamp int64
}

type EventBuffer interface {
	GetBufferedEvent(ctx context.Context, ciphertextHash [32]byte) (*BufferedEvent, error)
	PutBufferedEvent(ctx context.Context, ciphertextHash [32]byte, plaintext []byte, serverTimestamp uint64) error
	ClearBufferedEventPlaintext(ctx context.Context, ciphertextHash [32]byte) error
	DeleteBufferedEventsOlderThan(ctx context.Context, maxTS time.Time) error
}

var _ EventBuffer = (*sqlStore)(nil)

const (
	getBufferedEventQuery = `
		SELECT plaintext, server_timestamp, insert_timestamp
		FROM signalmeow_event_buffer
		WHERE account_id=$1 AND ciphertext_hash=$2
	`
	putBufferedEventQuery = `
		INSERT INTO signalmeow_event_buffer (account_id, ciphertext_hash, plaintext, server_timestamp, insert_timestamp)
		VALUES ($1, $2, $3, $4, $5)
	`
	clearBufferedEventPlaintextQuery = `UPDATE signalmeow_event_buffer SET plaintext=NULL WHERE account_id=$1 AND ciphertext_hash=$2`
	deleteOldBufferedEventsQuery     = `DELETE FROM signalmeow_event_buffer WHERE account_id=$1 AND insert_timestamp<$2 AND plaintext IS NULL`
)

func (s *sqlStore) GetBufferedEvent(ctx context.Context, ciphertextHash [32]byte) (*BufferedEvent, error) {
	var evt BufferedEvent
	err := s.db.QueryRow(ctx, getBufferedEventQuery, s.AccountID, ciphertextHash[:]).Scan(&evt.Plaintext, &evt.ServerTimestamp, &evt.InsertTimestamp)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	return &evt, nil
}

func (s *sqlStore) PutBufferedEvent(ctx context.Context, ciphertextHash [32]byte, plaintext []byte, serverTimestamp uint64) error {
	_, err := s.db.Exec(ctx, putBufferedEventQuery, s.AccountID, ciphertextHash[:], plaintext, serverTimestamp, time.Now().UnixMilli())
	return err
}

func (s *sqlStore) ClearBufferedEventPlaintext(ctx context.Context, ciphertextHash [32]byte) error {
	_, err := s.db.Exec(ctx, clearBufferedEventPlaintextQuery, s.AccountID, ciphertextHash[:])
	return err
}

func (s *sqlStore) DeleteBufferedEventsOlderThan(ctx context.Context, maxTS time.Time) error {
	_, err := s.db.Exec(ctx, deleteOldBufferedEventsQuery, s.AccountID, maxTS.UnixMilli())
	return err
}
