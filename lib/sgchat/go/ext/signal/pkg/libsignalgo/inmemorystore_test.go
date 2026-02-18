// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
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

package libsignalgo_test

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"math/big"

	"github.com/google/uuid"
	"github.com/rs/zerolog/log"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type SenderKeyName struct {
	SenderName     string
	SenderDeviceID uint
	DistributionID uuid.UUID
}

type AddressKey struct {
	Name     string
	DeviceID uint
}

type InMemorySignalProtocolStore struct {
	identityKeyPair *libsignalgo.IdentityKeyPair
	registrationID  uint32

	identityKeyMap  map[libsignalgo.ServiceID][]byte
	preKeyMap       map[uint32]*libsignalgo.PreKeyRecord
	senderKeyMap    map[SenderKeyName]*libsignalgo.SenderKeyRecord
	sessionMap      map[AddressKey]*libsignalgo.SessionRecord
	signedPreKeyMap map[uint32]*libsignalgo.SignedPreKeyRecord
	kyberPreKeyMap  map[uint32]*libsignalgo.KyberPreKeyRecord
}

func NewInMemorySignalProtocolStore() *InMemorySignalProtocolStore {
	identityKeyPair, err := libsignalgo.GenerateIdentityKeyPair()
	if err != nil {
		panic(err)
	}

	registrationID, err := rand.Int(rand.Reader, big.NewInt(0x4000))
	if err != nil {
		panic(err)
	}

	return &InMemorySignalProtocolStore{
		identityKeyPair: identityKeyPair,
		registrationID:  uint32(registrationID.Uint64()),

		identityKeyMap:  make(map[libsignalgo.ServiceID][]byte),
		preKeyMap:       make(map[uint32]*libsignalgo.PreKeyRecord),
		senderKeyMap:    make(map[SenderKeyName]*libsignalgo.SenderKeyRecord),
		sessionMap:      make(map[AddressKey]*libsignalgo.SessionRecord),
		signedPreKeyMap: make(map[uint32]*libsignalgo.SignedPreKeyRecord),
		kyberPreKeyMap:  make(map[uint32]*libsignalgo.KyberPreKeyRecord),
	}
}

// Implementation of the SessionStore interface

func (ps *InMemorySignalProtocolStore) LoadSession(ctx context.Context, address *libsignalgo.Address) (*libsignalgo.SessionRecord, error) {
	log.Debug().Msg("LoadSession called")
	name, err := address.Name()
	if err != nil {
		return nil, err
	}
	deviceID, err := address.DeviceID()
	if err != nil {
		return nil, err
	}
	log.Debug().Interface("returning", ps.sessionMap[AddressKey{name, deviceID}]).Msg("LoadSession")
	return ps.sessionMap[AddressKey{name, deviceID}], nil
}

func (ps *InMemorySignalProtocolStore) StoreSession(ctx context.Context, address *libsignalgo.Address, record *libsignalgo.SessionRecord) error {
	log.Debug().Msg("StoreSession called")
	name, err := address.Name()
	if err != nil {
		return err
	}
	deviceID, err := address.DeviceID()
	if err != nil {
		return err
	}
	ps.sessionMap[AddressKey{name, deviceID}] = record
	return nil
}

// Implementation of the SenderKeyStore interface

func (ps *InMemorySignalProtocolStore) LoadSenderKey(ctx context.Context, sender *libsignalgo.Address, distributionID uuid.UUID) (*libsignalgo.SenderKeyRecord, error) {
	log.Debug().Msg("LoadSenderKey called")
	name, err := sender.Name()
	if err != nil {
		return nil, err
	}
	deviceID, err := sender.DeviceID()
	if err != nil {
		return nil, err
	}
	return ps.senderKeyMap[SenderKeyName{name, deviceID, distributionID}], nil
}

func (ps *InMemorySignalProtocolStore) StoreSenderKey(ctx context.Context, sender *libsignalgo.Address, distributionID uuid.UUID, record *libsignalgo.SenderKeyRecord) error {
	log.Debug().Msg("StoreSenderKey called")
	name, err := sender.Name()
	if err != nil {
		return err
	}
	deviceID, err := sender.DeviceID()
	if err != nil {
		return err
	}
	ps.senderKeyMap[SenderKeyName{name, deviceID, distributionID}] = record
	return nil
}

// Implementation of the IdentityKeyStore interface

func (ps *InMemorySignalProtocolStore) GetIdentityKeyPair(ctx context.Context) (*libsignalgo.IdentityKeyPair, error) {
	log.Debug().Msg("GetIdentityKeyPair called")
	return ps.identityKeyPair, nil
}

func (ps *InMemorySignalProtocolStore) GetLocalRegistrationID(ctx context.Context) (uint32, error) {
	log.Debug().Msg("GetLocalRegistrationID called")
	return ps.registrationID, nil
}

func (ps *InMemorySignalProtocolStore) SaveIdentityKey(ctx context.Context, theirServiceID libsignalgo.ServiceID, identityKey *libsignalgo.IdentityKey) (bool, error) {
	log.Debug().Msg("SaveIdentityKey called")
	replacing := false
	oldKeySerialized, ok := ps.identityKeyMap[theirServiceID]
	if ok {
		oldKey, err := libsignalgo.DeserializeIdentityKey(oldKeySerialized)
		if err != nil {
			log.Error().Err(err).Interface("oldKey", oldKey).Msg("Error deserializing old identity key")
		}
		if oldKey != nil {
			keysMatch, err := oldKey.Equal(identityKey)
			if err != nil {
				log.Error().Err(err).Interface("oldKey", oldKey).Interface("identityKey", identityKey).Msg("Error comparing identity keys")
			}
			replacing = !keysMatch
		}
	}
	serializedIdentityKey, err := identityKey.Serialize()
	if err != nil {
		log.Error().Err(err).Interface("identityKey", identityKey).Msg("Error serializing identity key")
	}

	hexIdentityKey := hex.EncodeToString(serializedIdentityKey)
	log.Debug().Str("hexIdentityKey", hexIdentityKey).Msg("SaveIdentityKey")

	ps.identityKeyMap[theirServiceID] = serializedIdentityKey
	return replacing, nil
}

func (ps *InMemorySignalProtocolStore) GetIdentityKey(ctx context.Context, theirServiceID libsignalgo.ServiceID) (*libsignalgo.IdentityKey, error) {
	log.Debug().Msg("GetIdentityKey called")
	serializedIdentityKey, ok := ps.identityKeyMap[theirServiceID]
	if !ok {
		return nil, nil
	}
	return libsignalgo.DeserializeIdentityKey(serializedIdentityKey)
}

func (ps *InMemorySignalProtocolStore) IsTrustedIdentity(ctx context.Context, theirServiceID libsignalgo.ServiceID, identityKey *libsignalgo.IdentityKey, direction libsignalgo.SignalDirection) (bool, error) {
	log.Debug().Msg("IsTrustedIdentity called")
	if existingSerialized, ok := ps.identityKeyMap[theirServiceID]; ok {
		existingKey, err := libsignalgo.DeserializeIdentityKey(existingSerialized)
		if err != nil {
			log.Error().Err(err).Interface("existingKey", existingKey).Msg("Error deserializing existing identity key")
		}
		return existingKey.Equal(identityKey)
	} else {
		log.Trace().Msg("Trusting on first use")
		return true, nil // Trust on first use
	}
}

// Implementation of the PreKeyStore interface

func (ps *InMemorySignalProtocolStore) LoadPreKey(ctx context.Context, id uint32) (*libsignalgo.PreKeyRecord, error) {
	return ps.preKeyMap[id], nil
}

func (ps *InMemorySignalProtocolStore) StorePreKey(ctx context.Context, id uint32, preKeyRecord *libsignalgo.PreKeyRecord) error {
	ps.preKeyMap[id] = preKeyRecord
	return nil
}

func (ps *InMemorySignalProtocolStore) RemovePreKey(ctx context.Context, id uint32) error {
	delete(ps.preKeyMap, id)
	return nil
}

// Implementation of the SignedPreKeyStore interface

func (ps *InMemorySignalProtocolStore) LoadSignedPreKey(context context.Context, id uint32) (*libsignalgo.SignedPreKeyRecord, error) {
	return ps.signedPreKeyMap[id], nil
}

func (ps *InMemorySignalProtocolStore) StoreSignedPreKey(context context.Context, id uint32, signedPreKeyRecord *libsignalgo.SignedPreKeyRecord) error {
	ps.signedPreKeyMap[id] = signedPreKeyRecord
	return nil
}

type BadInMemorySignalProtocolStore struct {
	*InMemorySignalProtocolStore
}

func (ps *BadInMemorySignalProtocolStore) LoadPreKey(ctx context.Context, id uint32) (*libsignalgo.PreKeyRecord, error) {
	return nil, errors.New("Test error")
}

func (ps *BadInMemorySignalProtocolStore) LoadKyberPreKey(ctx context.Context, id uint32) (*libsignalgo.KyberPreKeyRecord, error) {
	return nil, errors.New("Test error")
}

// Implementation of the KyberPreKeyStore interface
// TODO: this is just stubs, not implemented yet

func (ps *InMemorySignalProtocolStore) LoadKyberPreKey(ctx context.Context, id uint32) (*libsignalgo.KyberPreKeyRecord, error) {
	return ps.kyberPreKeyMap[id], nil
}

func (ps *InMemorySignalProtocolStore) StoreKyberPreKey(ctx context.Context, id uint32, kyberPreKeyRecord *libsignalgo.KyberPreKeyRecord) error {
	ps.kyberPreKeyMap[id] = kyberPreKeyRecord
	return nil
}

func (ps *InMemorySignalProtocolStore) MarkKyberPreKeyUsed(ctx context.Context, id uint32) error {
	//delete(ps.kyberPreKeyMap, id)
	return nil
}
