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

	"go.mau.fi/util/dbutil"

	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

var _ GroupStore = (*sqlStore)(nil)

type dbGroup struct {
	OurAciUuid      string
	GroupIdentifier types.GroupIdentifier
	GroupMasterKey  types.SerializedGroupMasterKey
}

type GroupStore interface {
	MasterKeyFromGroupIdentifier(ctx context.Context, groupID types.GroupIdentifier) (types.SerializedGroupMasterKey, error)
	StoreMasterKey(ctx context.Context, groupID types.GroupIdentifier, key types.SerializedGroupMasterKey) error
}

const (
	getGroupByIDQuery         = `SELECT account_id, group_identifier, master_key FROM signalmeow_groups WHERE account_id=$1 AND group_identifier=$2`
	upsertGroupMasterKeyQuery = `
		INSERT INTO signalmeow_groups (account_id, group_identifier, master_key)
		VALUES ($1, $2, $3)
		ON CONFLICT (account_id, group_identifier) DO UPDATE
			SET master_key = excluded.master_key;
	`
)

func scanGroup(row dbutil.Scannable) (*dbGroup, error) {
	var g dbGroup
	err := row.Scan(&g.OurAciUuid, &g.GroupIdentifier, &g.GroupMasterKey)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	return &g, nil
}

func (s *sqlStore) MasterKeyFromGroupIdentifier(ctx context.Context, groupID types.GroupIdentifier) (types.SerializedGroupMasterKey, error) {
	g, err := scanGroup(s.db.QueryRow(ctx, getGroupByIDQuery, s.AccountID, groupID))
	if g == nil {
		return "", err
	} else {
		return g.GroupMasterKey, nil
	}
}

func (s *sqlStore) StoreMasterKey(ctx context.Context, groupID types.GroupIdentifier, key types.SerializedGroupMasterKey) error {
	_, err := s.db.Exec(ctx, upsertGroupMasterKeyQuery, s.AccountID, groupID, key)
	return err
}
