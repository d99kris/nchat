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

typedef const SignalSignedPreKeyRecord const_signed_pre_key_record;

extern int signal_load_signed_pre_key_callback(void *store_ctx, SignalSignedPreKeyRecord **recordp, uint32_t id);
extern int signal_store_signed_pre_key_callback(void *store_ctx, uint32_t id, const_signed_pre_key_record *record);
*/
import "C"
import (
	"context"
	"unsafe"
)

type SignedPreKeyStore interface {
	LoadSignedPreKey(ctx context.Context, id uint32) (*SignedPreKeyRecord, error)
	StoreSignedPreKey(ctx context.Context, id uint32, signedPreKeyRecord *SignedPreKeyRecord) error
}

//export signal_load_signed_pre_key_callback
func signal_load_signed_pre_key_callback(storeCtx unsafe.Pointer, keyp **C.SignalSignedPreKeyRecord, id C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store SignedPreKeyStore, ctx context.Context) error {
		key, err := store.LoadSignedPreKey(ctx, uint32(id))
		if err == nil && key != nil {
			key.CancelFinalizer()
			*keyp = key.ptr
		}
		return err
	})
}

//export signal_store_signed_pre_key_callback
func signal_store_signed_pre_key_callback(storeCtx unsafe.Pointer, id C.uint32_t, preKeyRecord *C.const_signed_pre_key_record) C.int {
	return wrapStoreCallback(storeCtx, func(store SignedPreKeyStore, ctx context.Context) error {
		record := SignedPreKeyRecord{ptr: (*C.SignalSignedPreKeyRecord)(unsafe.Pointer(preKeyRecord))}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StoreSignedPreKey(ctx, uint32(id), cloned)
	})
}

func (ctx *CallbackContext) wrapSignedPreKeyStore(store SignedPreKeyStore) C.SignalConstPointerFfiSignedPreKeyStoreStruct {
	return C.SignalConstPointerFfiSignedPreKeyStoreStruct{&C.SignalSignedPreKeyStore{
		ctx:                  wrapStore(ctx, store),
		load_signed_pre_key:  C.SignalLoadSignedPreKey(C.signal_load_signed_pre_key_callback),
		store_signed_pre_key: C.SignalStoreSignedPreKey(C.signal_store_signed_pre_key_callback),
	}}
}
