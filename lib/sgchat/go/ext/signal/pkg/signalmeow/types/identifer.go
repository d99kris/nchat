// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber, Tulir Asokan
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

package types

import (
	"encoding/base64"
	"fmt"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type GroupIdentifier string

func BytesToGroupIdentifier(raw *libsignalgo.GroupIdentifier) GroupIdentifier {
	return GroupIdentifier(raw.String())
}

func (gid GroupIdentifier) String() string {
	return string(gid)
}

func (gid GroupIdentifier) Bytes() (raw libsignalgo.GroupIdentifier, err error) {
	var decoded []byte
	decoded, err = base64.StdEncoding.DecodeString(string(gid))
	if err == nil {
		if len(decoded) != 32 {
			err = fmt.Errorf("invalid group identifier length")
		} else {
			raw = libsignalgo.GroupIdentifier(decoded)
		}
	}
	return
}

// This is just base64 encoded group master key
type SerializedGroupMasterKey string
type SerializedInviteLinkPassword string
