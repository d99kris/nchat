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

extern int signal_load_signed_pre_key_callback(void *store_ctx, SignalMutPointerSignedPreKeyRecord *recordp, uint32_t id);
extern int signal_store_signed_pre_key_callback(void *store_ctx, uint32_t id, SignalMutPointerSignedPreKeyRecord record);
extern void signal_destroy_signed_pre_key_store_callback(void *store_ctx);
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
func signal_load_signed_pre_key_callback(storeCtx unsafe.Pointer, keyp *C.SignalMutPointerSignedPreKeyRecord, id C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store SignedPreKeyStore, ctx context.Context) error {
		key, err := store.LoadSignedPreKey(ctx, uint32(id))
		if err == nil && key != nil {
			key.CancelFinalizer()
			keyp.raw = key.ptr
		}
		return err
	})
}

//export signal_store_signed_pre_key_callback
func signal_store_signed_pre_key_callback(storeCtx unsafe.Pointer, id C.uint32_t, preKeyRecord C.SignalMutPointerSignedPreKeyRecord) C.int {
	return wrapStoreCallback(storeCtx, func(store SignedPreKeyStore, ctx context.Context) error {
		record := SignedPreKeyRecord{ptr: preKeyRecord.raw}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StoreSignedPreKey(ctx, uint32(id), cloned)
	})
}

//export signal_destroy_signed_pre_key_store_callback
func signal_destroy_signed_pre_key_store_callback(storeCtx unsafe.Pointer) {
	// No-op: Go's garbage collector handles cleanup
}

func (ctx *CallbackContext) wrapSignedPreKeyStore(store SignedPreKeyStore) C.SignalConstPointerFfiSignedPreKeyStoreStruct {
	return C.SignalConstPointerFfiSignedPreKeyStoreStruct{&C.SignalSignedPreKeyStore{
		ctx:                  wrapStore(ctx, store),
		load_signed_pre_key:  C.SignalFfiBridgeSignedPreKeyStoreLoadSignedPreKey(C.signal_load_signed_pre_key_callback),
		store_signed_pre_key: C.SignalFfiBridgeSignedPreKeyStoreStoreSignedPreKey(C.signal_store_signed_pre_key_callback),
		destroy:              C.SignalFfiBridgeSignedPreKeyStoreDestroy(C.signal_destroy_signed_pre_key_store_callback),
	}}
}
