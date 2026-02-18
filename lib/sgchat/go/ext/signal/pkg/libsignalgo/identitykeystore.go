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

typedef const SignalProtocolAddress const_address;
typedef const SignalPublicKey const_public_key;

extern int signal_get_identity_key_pair_callback(void *store_ctx, SignalPrivateKey **keyp);
extern int signal_get_local_registration_id_callback(void *store_ctx, uint32_t *idp);
extern int signal_save_identity_key_callback(void *store_ctx, const_address *address, const_public_key *public_key);
extern int signal_get_identity_key_callback(void *store_ctx, SignalPublicKey **public_keyp, const_address *address);
extern int signal_is_trusted_identity_callback(void *store_ctx, const_address *address, const_public_key *public_key, unsigned int direction);
*/
import "C"
import (
	"context"
	"unsafe"
)

type SignalDirection uint

const (
	SignalDirectionSending   SignalDirection = 0
	SignalDirectionReceiving SignalDirection = 1
)

type IdentityKeyStore interface {
	GetIdentityKeyPair(ctx context.Context) (*IdentityKeyPair, error)
	GetLocalRegistrationID(ctx context.Context) (uint32, error)
	SaveIdentityKey(ctx context.Context, theirServiceID ServiceID, identityKey *IdentityKey) (bool, error)
	GetIdentityKey(ctx context.Context, theirServiceID ServiceID) (*IdentityKey, error)
	IsTrustedIdentity(ctx context.Context, theirServiceID ServiceID, identityKey *IdentityKey, direction SignalDirection) (bool, error)
}

//export signal_get_identity_key_pair_callback
func signal_get_identity_key_pair_callback(storeCtx unsafe.Pointer, keyp **C.SignalPrivateKey) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		key, err := store.GetIdentityKeyPair(ctx)
		if err != nil {
			return err
		}
		if key == nil {
			*keyp = nil
		} else {
			clone, err := key.privateKey.Clone()
			if err != nil {
				return err
			}
			clone.CancelFinalizer()
			*keyp = clone.ptr
		}
		return err
	})
}

//export signal_get_local_registration_id_callback
func signal_get_local_registration_id_callback(storeCtx unsafe.Pointer, idp *C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		registrationID, err := store.GetLocalRegistrationID(ctx)
		if err == nil {
			*idp = C.uint32_t(registrationID)
		}
		return err
	})
}

//export signal_save_identity_key_callback
func signal_save_identity_key_callback(storeCtx unsafe.Pointer, address *C.const_address, publicKey *C.const_public_key) C.int {
	return wrapStoreCallbackCustomReturn(storeCtx, func(store IdentityKeyStore, ctx context.Context) (int, error) {
		publicKeyStruct := PublicKey{ptr: (*C.SignalPublicKey)(unsafe.Pointer(publicKey))}
		cloned, err := publicKeyStruct.Clone()
		if err != nil {
			return -1, err
		}
		addr := &Address{ptr: (*C.SignalProtocolAddress)(unsafe.Pointer(address))}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return -1, err
		}
		replaced, err := store.SaveIdentityKey(
			ctx,
			theirServiceID,
			&IdentityKey{cloned},
		)
		if err != nil {
			return -1, err
		}
		if replaced {
			return 1, nil
		} else {
			return 0, nil
		}
	})
}

//export signal_get_identity_key_callback
func signal_get_identity_key_callback(storeCtx unsafe.Pointer, public_keyp **C.SignalPublicKey, address *C.const_address) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		addr := &Address{ptr: (*C.SignalProtocolAddress)(unsafe.Pointer(address))}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return err
		}
		key, err := store.GetIdentityKey(ctx, theirServiceID)
		if err == nil && key != nil {
			key.publicKey.CancelFinalizer()
			*public_keyp = key.publicKey.ptr
		}
		return err
	})
}

//export signal_is_trusted_identity_callback
func signal_is_trusted_identity_callback(storeCtx unsafe.Pointer, address *C.const_address, public_key *C.const_public_key, direction C.uint) C.int {
	return wrapStoreCallbackCustomReturn(storeCtx, func(store IdentityKeyStore, ctx context.Context) (int, error) {
		addr := &Address{ptr: (*C.SignalProtocolAddress)(unsafe.Pointer(address))}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return -1, err
		}
		trusted, err := store.IsTrustedIdentity(ctx, theirServiceID, &IdentityKey{&PublicKey{ptr: (*C.SignalPublicKey)(unsafe.Pointer(public_key))}}, SignalDirection(direction))
		if err != nil {
			return -1, err
		}
		if trusted {
			return 1, nil
		} else {
			return 0, nil
		}
	})
}

func (ctx *CallbackContext) wrapIdentityKeyStore(store IdentityKeyStore) C.SignalConstPointerFfiIdentityKeyStoreStruct {
	return C.SignalConstPointerFfiIdentityKeyStoreStruct{&C.SignalIdentityKeyStore{
		ctx:                       wrapStore(ctx, store),
		get_identity_key_pair:     C.SignalGetIdentityKeyPair(C.signal_get_identity_key_pair_callback),
		get_local_registration_id: C.SignalGetLocalRegistrationId(C.signal_get_local_registration_id_callback),
		save_identity:             C.SignalSaveIdentityKey(C.signal_save_identity_key_callback),
		get_identity:              C.SignalGetIdentityKey(C.signal_get_identity_key_callback),
		is_trusted_identity:       C.SignalIsTrustedIdentity(C.signal_is_trusted_identity_callback),
	}}
}
