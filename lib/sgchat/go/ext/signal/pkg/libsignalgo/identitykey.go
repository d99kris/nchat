// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
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

package libsignalgo

/*
#include "./libsignal-ffi.h"
*/
import "C"
import (
	"runtime"
)

type IdentityKey struct {
	publicKey *PublicKey
}

func NewIdentityKeyFromPublicKey(publicKey *PublicKey) (*IdentityKey, error) {
	return &IdentityKey{publicKey: publicKey}, nil
}

func NewIdentityKeyFromBytes(bytes []byte) (*IdentityKey, error) {
	publicKey, err := DeserializePublicKey(bytes)
	if err != nil {
		return nil, err
	}
	return &IdentityKey{publicKey: publicKey}, nil
}

func (i *IdentityKey) TrySerialize() []byte {
	if i == nil {
		return nil
	}
	serialized, err := i.Serialize()
	if err != nil {
		return nil
	}
	return serialized
}

func (i *IdentityKey) Serialize() ([]byte, error) {
	return i.publicKey.Serialize()
}

func DeserializeIdentityKey(bytes []byte) (*IdentityKey, error) {
	var publicKey C.SignalMutPointerPublicKey
	signalFfiError := C.signal_publickey_deserialize(&publicKey, BytesToBuffer(bytes))
	runtime.KeepAlive(bytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &IdentityKey{publicKey: wrapPublicKey(publicKey.raw)}, nil
}

func (i *IdentityKey) VerifyAlternateIdentity(other *IdentityKey, signature []byte) (bool, error) {
	var verify C.bool
	signalFfiError := C.signal_identitykey_verify_alternate_identity(
		&verify,
		i.publicKey.constPtr(),
		other.publicKey.constPtr(),
		BytesToBuffer(signature),
	)
	runtime.KeepAlive(i)
	runtime.KeepAlive(other)
	runtime.KeepAlive(signature)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(verify), nil
}

func (i *IdentityKey) Equal(other *IdentityKey) (bool, error) {
	result, err := i.publicKey.Compare(other.publicKey)
	return result == 0, err
}

type IdentityKeyPair struct {
	publicKey  *PublicKey
	privateKey *PrivateKey
}

func (i *IdentityKeyPair) GetPublicKey() *PublicKey {
	return i.publicKey
}

func (i *IdentityKeyPair) GetPrivateKey() *PrivateKey {
	return i.privateKey
}

func GenerateIdentityKeyPair() (*IdentityKeyPair, error) {
	privateKey, err := GeneratePrivateKey()
	if err != nil {
		return nil, err
	}
	publicKey, err := privateKey.GetPublicKey()
	if err != nil {
		return nil, err
	}
	return &IdentityKeyPair{publicKey: publicKey, privateKey: privateKey}, nil
}

func DeserializeIdentityKeyPair(bytes []byte) (*IdentityKeyPair, error) {
	var keys C.SignalPairOfMutPointerPublicKeyMutPointerPrivateKey
	signalFfiError := C.signal_identitykeypair_deserialize(&keys, BytesToBuffer(bytes))
	runtime.KeepAlive(bytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &IdentityKeyPair{publicKey: wrapPublicKey(keys.first.raw), privateKey: wrapPrivateKey(keys.second.raw)}, nil
}

func NewIdentityKeyPair(publicKey *PublicKey, privateKey *PrivateKey) (*IdentityKeyPair, error) {
	return &IdentityKeyPair{publicKey: publicKey, privateKey: privateKey}, nil
}

func (i *IdentityKeyPair) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_identitykeypair_serialize(
		&serialized,
		i.publicKey.constPtr(),
		i.privateKey.constPtr(),
	)
	runtime.KeepAlive(i)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (i *IdentityKeyPair) GetIdentityKey() *IdentityKey {
	return &IdentityKey{publicKey: i.publicKey}
}

func (i *IdentityKeyPair) SignAlternateIdentity(other *IdentityKey) ([]byte, error) {
	var signature C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_identitykeypair_sign_alternate_identity(
		&signature,
		i.publicKey.constPtr(),
		i.privateKey.constPtr(),
		other.publicKey.constPtr(),
	)
	runtime.KeepAlive(i)
	runtime.KeepAlive(other)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(signature), nil
}
