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

package upgrades

import (
	"context"

	"go.mau.fi/util/dbutil"
)

func init() {
	Table.Register(-1, 20, 13, "Add missing columns for backup chat table", dbutil.TxnModeOn, func(ctx context.Context, db *dbutil.Database) (err error) {
		var exists bool
		if exists, err = db.ColumnExists(ctx, "signalmeow_backup_chat", "latest_message_id"); err == nil && !exists {
			_, err = db.Exec(ctx, `
				ALTER TABLE signalmeow_backup_chat ADD COLUMN latest_message_id BIGINT;
				ALTER TABLE signalmeow_backup_chat ADD COLUMN total_message_count INTEGER;
			`)
		}
		return
	})
}
