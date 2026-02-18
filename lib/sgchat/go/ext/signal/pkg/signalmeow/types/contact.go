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
	"time"

	"github.com/google/uuid"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type Profile struct {
	Name       string
	About      string
	AboutEmoji string
	AvatarPath string
	Key        libsignalgo.ProfileKey
	FetchedAt  time.Time
	Credential []byte
}

func (p *Profile) Equals(other *Profile) bool {
	return p.Name == other.Name &&
		p.About == other.About &&
		p.AboutEmoji == other.AboutEmoji &&
		p.AvatarPath == other.AvatarPath &&
		p.Key == other.Key
}

// The Recipient struct combines information from two sources:
// - A Signal "contact": contact info harvested from our user's phone's contact list
// - A Signal "profile": contact info entered by the target user when registering for Signal
type Recipient struct {
	ACI           uuid.UUID
	PNI           uuid.UUID
	E164          string
	ContactName   string
	ContactAvatar ContactAvatar
	Nickname      string
	Profile       Profile

	NeedsPNISignature bool
	Blocked           bool
	Whitelisted       *bool
}

func (r *Recipient) ProbablyMessageRequest() bool {
	return r != nil && (r.NeedsPNISignature || (r.Whitelisted != nil && !*r.Whitelisted))
}

type ContactAvatar struct {
	Image       []byte
	ContentType string
	Hash        string
}
