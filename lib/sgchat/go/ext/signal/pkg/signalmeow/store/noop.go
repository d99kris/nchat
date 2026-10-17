// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2026 Tulir Asokan
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

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type noopStore struct {
	Error error
}

var _ PreKeyStore = (*noopStore)(nil)
var _ SessionStore = (*noopStore)(nil)
var _ IdentityKeyStore = (*noopStore)(nil)
var _ libsignalgo.IdentityKeyStore = (*noopStore)(nil)

func (n *noopStore) LoadPreKey(ctx context.Context, id uint32) (*libsignalgo.PreKeyRecord, error) {
	return nil, n.Error
}

func (n *noopStore) StorePreKey(ctx context.Context, id uint32, preKeyRecord *libsignalgo.PreKeyRecord) error {
	return n.Error
}

func (n *noopStore) RemovePreKey(ctx context.Context, id uint32) error {
	return n.Error
}

func (n *noopStore) LoadSignedPreKey(ctx context.Context, id uint32) (*libsignalgo.SignedPreKeyRecord, error) {
	return nil, n.Error
}

func (n *noopStore) StoreSignedPreKey(ctx context.Context, id uint32, signedPreKeyRecord *libsignalgo.SignedPreKeyRecord) error {
	return n.Error
}

func (n *noopStore) LoadKyberPreKey(ctx context.Context, id uint32) (*libsignalgo.KyberPreKeyRecord, error) {
	return nil, n.Error
}

func (n *noopStore) StoreKyberPreKey(ctx context.Context, id uint32, kyberPreKeyRecord *libsignalgo.KyberPreKeyRecord) error {
	return n.Error
}

func (n *noopStore) MarkKyberPreKeyUsed(ctx context.Context, id uint32) error {
	return n.Error
}

func (n *noopStore) GetServiceID() libsignalgo.ServiceID {
	return libsignalgo.EmptyServiceID
}

func (n *noopStore) StoreLastResortKyberPreKey(ctx context.Context, preKeyID uint32, record *libsignalgo.KyberPreKeyRecord) error {
	return n.Error
}

func (n *noopStore) RemoveSignedPreKey(ctx context.Context, preKeyID uint32) error {
	return n.Error
}

func (n *noopStore) RemoveKyberPreKey(ctx context.Context, preKeyID uint32) error {
	return n.Error
}

func (n *noopStore) GetNextPreKeyID(ctx context.Context) (count, max uint32, err error) {
	return 0, 0, n.Error
}

func (n *noopStore) GetNextKyberPreKeyID(ctx context.Context) (count, max uint32, err error) {
	return 0, 0, n.Error
}

func (n *noopStore) IsKyberPreKeyLastResort(ctx context.Context, preKeyID uint32) (bool, error) {
	return false, n.Error
}

func (n *noopStore) AllPreKeys(ctx context.Context) ([]*libsignalgo.PreKeyRecord, error) {
	return nil, n.Error
}

func (n *noopStore) AllNormalKyberPreKeys(ctx context.Context) ([]*libsignalgo.KyberPreKeyRecord, error) {
	return nil, n.Error
}

func (n *noopStore) DeleteAllPreKeys(ctx context.Context) error {
	return n.Error
}

func (n *noopStore) LoadSession(ctx context.Context, address *libsignalgo.Address) (*libsignalgo.SessionRecord, error) {
	return nil, n.Error
}

func (n *noopStore) StoreSession(ctx context.Context, address *libsignalgo.Address, record *libsignalgo.SessionRecord) error {
	return n.Error
}

func (n *noopStore) AllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) ([]SessionAddressTuple, error) {
	return nil, n.Error
}

func (n *noopStore) RemoveSession(ctx context.Context, address *libsignalgo.Address) error {
	return n.Error
}

func (n *noopStore) RemoveAllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) error {
	return n.Error
}

func (n *noopStore) ArchiveAllSessionsForServiceID(ctx context.Context, theirID libsignalgo.ServiceID) error {
	return n.Error
}

func (n *noopStore) RemoveAllSessions(ctx context.Context) error {
	return n.Error
}

func (n *noopStore) SaveIdentityKey(ctx context.Context, theirServiceID libsignalgo.ServiceID, identityKey *libsignalgo.IdentityKey) (bool, error) {
	return false, n.Error
}

func (n *noopStore) GetIdentityKey(ctx context.Context, theirServiceID libsignalgo.ServiceID) (*libsignalgo.IdentityKey, error) {
	return nil, n.Error
}

func (n *noopStore) IsTrustedIdentity(ctx context.Context, theirServiceID libsignalgo.ServiceID, identityKey *libsignalgo.IdentityKey, direction libsignalgo.SignalDirection) (bool, error) {
	return true, n.Error
}

func (n *noopStore) GetIdentityKeyPair(ctx context.Context) (*libsignalgo.IdentityKeyPair, error) {
	return nil, n.Error
}

func (n *noopStore) GetLocalRegistrationID(ctx context.Context) (uint32, error) {
	return 0, n.Error
}
