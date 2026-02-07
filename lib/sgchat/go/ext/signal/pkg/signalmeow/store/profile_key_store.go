// mautrix-signal - A Matrix-signal puppeting bridge.
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

	"github.com/google/uuid"
	"go.mau.fi/util/dbutil"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

const (
	loadProfileKeyQuery  = `SELECT profile_key FROM signalmeow_recipients WHERE account_id=$1 AND aci_uuid=$2`
	storeProfileKeyQuery = `
		INSERT INTO signalmeow_recipients (account_id, aci_uuid, profile_key)
		VALUES ($1, $2, $3)
		ON CONFLICT (account_id, aci_uuid) DO UPDATE SET profile_key=excluded.profile_key
	`
)

func scanProfileKey(row dbutil.Scannable) (*libsignalgo.ProfileKey, error) {
	return scanRecord(row, libsignalgo.DeserializeProfileKey)
}

func (s *sqlStore) LoadProfileKey(ctx context.Context, theirACI uuid.UUID) (*libsignalgo.ProfileKey, error) {
	return scanProfileKey(s.db.QueryRow(ctx, loadProfileKeyQuery, s.AccountID, theirACI))
}

func (s *sqlStore) MyProfileKey(ctx context.Context) (*libsignalgo.ProfileKey, error) {
	return scanProfileKey(s.db.QueryRow(ctx, loadProfileKeyQuery, s.AccountID, s.AccountID))
}

func (s *sqlStore) StoreProfileKey(ctx context.Context, theirACI uuid.UUID, key libsignalgo.ProfileKey) error {
	_, err := s.db.Exec(ctx, storeProfileKeyQuery, s.AccountID, theirACI, key.Slice())
	return err
}
