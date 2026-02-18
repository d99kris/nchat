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

package upgrades

import (
	"context"
	"fmt"

	"github.com/rs/zerolog"
	"go.mau.fi/util/dbutil"
)

type PreKeyCounts struct {
	AccountID string
	ServiceID string
	Count     int
	MaxID     int
}

func scanPreKeyCounts(row dbutil.Scannable) (*PreKeyCounts, error) {
	var pkc PreKeyCounts
	return dbutil.ValueOrErr(&pkc, row.Scan(&pkc.AccountID, &pkc.ServiceID, &pkc.Count, &pkc.MaxID))
}

func deleteExtraPrekeys(ctx context.Context, db *dbutil.Database, selectQuery, deleteQuery string) error {
	preKeys, err := dbutil.ConvertRowFn[*PreKeyCounts](scanPreKeyCounts).NewRowIter(db.Query(ctx, selectQuery)).AsList()
	if err != nil {
		return fmt.Errorf("failed to query prekey counts: %w", err)
	}
	for _, pkc := range preKeys {
		if pkc.Count > 250 {
			zerolog.Ctx(ctx).Debug().
				Str("account_id", pkc.AccountID).
				Str("service_id", pkc.ServiceID).
				Int("max_id", pkc.MaxID).
				Int("count", pkc.Count).
				Msg("Too many prekeys, deleting all")
			_, err = db.Exec(ctx, deleteQuery, pkc.AccountID, pkc.ServiceID, pkc.MaxID-95)
			if err != nil {
				return fmt.Errorf("failed to delete extra prekeys for %s/%s: %w", pkc.AccountID, pkc.ServiceID, err)
			}
		}
	}
	return nil
}

func init() {
	Table.Register(-1, 16, 13, "Remove extra prekeys", dbutil.TxnModeOn, func(ctx context.Context, db *dbutil.Database) error {
		err := deleteExtraPrekeys(ctx, db, `
			SELECT account_id, service_id, COUNT(*), MAX(key_id) FROM signalmeow_pre_keys WHERE is_signed=false GROUP BY 1, 2
		`, `
			DELETE FROM signalmeow_pre_keys WHERE account_id=$1 AND service_id=$2 AND is_signed=false AND key_id<$3
		`)
		if err != nil {
			return fmt.Errorf("failed to process EC: %w", err)
		}
		err = deleteExtraPrekeys(ctx, db, `
			SELECT account_id, service_id, COUNT(*), MAX(key_id) FROM signalmeow_kyber_pre_keys WHERE is_last_resort=false GROUP BY 1, 2
		`, `
			DELETE FROM signalmeow_kyber_pre_keys WHERE account_id=$1 AND service_id=$2 AND is_last_resort=false AND key_id<$3
		`)
		if err != nil {
			return fmt.Errorf("failed to process kyber: %w", err)
		}
		return nil
	})
}
