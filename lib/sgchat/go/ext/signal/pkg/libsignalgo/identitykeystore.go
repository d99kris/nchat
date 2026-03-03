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

extern int signal_get_identity_key_pair_callback(void *store_ctx, SignalMutPointerPrivateKey *keyp);
extern int signal_get_local_registration_id_callback(void *store_ctx, uint32_t *idp);
extern int signal_save_identity_key_callback(void *store_ctx, uint8_t *out, SignalMutPointerProtocolAddress address, SignalMutPointerPublicKey public_key);
extern int signal_get_identity_key_callback(void *store_ctx, SignalMutPointerPublicKey *public_keyp, SignalMutPointerProtocolAddress address);
extern int signal_is_trusted_identity_callback(void *store_ctx, bool *out, SignalMutPointerProtocolAddress address, SignalMutPointerPublicKey public_key, uint32_t direction);
extern void signal_destroy_identity_key_store_callback(void *store_ctx);
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
func signal_get_identity_key_pair_callback(storeCtx unsafe.Pointer, keyp *C.SignalMutPointerPrivateKey) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		key, err := store.GetIdentityKeyPair(ctx)
		if err != nil {
			return err
		}
		if key == nil {
			keyp.raw = nil
		} else {
			clone, err := key.privateKey.Clone()
			if err != nil {
				return err
			}
			clone.CancelFinalizer()
			keyp.raw = clone.ptr
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
func signal_save_identity_key_callback(storeCtx unsafe.Pointer, out *C.uint8_t, address C.SignalMutPointerProtocolAddress, publicKey C.SignalMutPointerPublicKey) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		publicKeyStruct := PublicKey{ptr: publicKey.raw}
		cloned, err := publicKeyStruct.Clone()
		if err != nil {
			return err
		}
		addr := &Address{ptr: address.raw}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return err
		}
		replaced, err := store.SaveIdentityKey(
			ctx,
			theirServiceID,
			&IdentityKey{cloned},
		)
		if err != nil {
			return err
		}
		if replaced {
			*out = 1
		} else {
			*out = 0
		}
		return nil
	})
}

//export signal_get_identity_key_callback
func signal_get_identity_key_callback(storeCtx unsafe.Pointer, public_keyp *C.SignalMutPointerPublicKey, address C.SignalMutPointerProtocolAddress) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		addr := &Address{ptr: address.raw}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return err
		}
		key, err := store.GetIdentityKey(ctx, theirServiceID)
		if err == nil && key != nil {
			key.publicKey.CancelFinalizer()
			public_keyp.raw = key.publicKey.ptr
		}
		return err
	})
}

//export signal_is_trusted_identity_callback
func signal_is_trusted_identity_callback(storeCtx unsafe.Pointer, out *C.bool, address C.SignalMutPointerProtocolAddress, public_key C.SignalMutPointerPublicKey, direction C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store IdentityKeyStore, ctx context.Context) error {
		addr := &Address{ptr: address.raw}
		theirServiceID, err := addr.NameServiceID()
		if err != nil {
			return err
		}
		trusted, err := store.IsTrustedIdentity(ctx, theirServiceID, &IdentityKey{&PublicKey{ptr: public_key.raw}}, SignalDirection(direction))
		if err != nil {
			return err
		}
		*out = C.bool(trusted)
		return nil
	})
}

//export signal_destroy_identity_key_store_callback
func signal_destroy_identity_key_store_callback(storeCtx unsafe.Pointer) {
	// No-op: Go's garbage collector handles cleanup
}

func (ctx *CallbackContext) wrapIdentityKeyStore(store IdentityKeyStore) C.SignalConstPointerFfiIdentityKeyStoreStruct {
	return C.SignalConstPointerFfiIdentityKeyStoreStruct{&C.SignalIdentityKeyStore{
		ctx:                            wrapStore(ctx, store),
		get_local_identity_private_key: C.SignalFfiBridgeIdentityKeyStoreGetLocalIdentityPrivateKey(C.signal_get_identity_key_pair_callback),
		get_local_registration_id:      C.SignalFfiBridgeIdentityKeyStoreGetLocalRegistrationId(C.signal_get_local_registration_id_callback),
		get_identity_key:               C.SignalFfiBridgeIdentityKeyStoreGetIdentityKey(C.signal_get_identity_key_callback),
		save_identity_key:              C.SignalFfiBridgeIdentityKeyStoreSaveIdentityKey(C.signal_save_identity_key_callback),
		is_trusted_identity:            C.SignalFfiBridgeIdentityKeyStoreIsTrustedIdentity(C.signal_is_trusted_identity_callback),
		destroy:                        C.SignalFfiBridgeIdentityKeyStoreDestroy(C.signal_destroy_identity_key_store_callback),
	}}
}
