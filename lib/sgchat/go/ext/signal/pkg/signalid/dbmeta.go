// mautrix-signal - A Matrix-Signal puppeting bridge.
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

package signalid

import (
	"go.mau.fi/util/jsontime"
)

type PortalMetadata struct {
	Revision               uint32 `json:"revision,omitempty"`
	ExpirationTimerVersion uint32 `json:"expiration_timer_version,omitempty"`
	// Lazy resync tracking
	LastSync jsontime.Unix `json:"last_sync,omitempty"`
}

type MessageMetadata struct {
	ContainsAttachments bool     `json:"contains_attachments,omitempty"`
	MatrixPollOptionIDs []string `json:"matrix_poll_option_ids,omitempty"`
}

type UserLoginMetadata struct {
	ChatsSynced     bool               `json:"chats_synced,omitempty"`
	LastContactSync jsontime.UnixMilli `json:"last_contact_sync,omitempty"`
}

type GhostMetadata struct {
	ProfileFetchedAt jsontime.UnixMilli `json:"profile_fetched_at"`
}
