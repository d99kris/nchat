// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

extern int signal_load_kyber_pre_key_callback(void *store_ctx, SignalMutPointerKyberPreKeyRecord *recordp, uint32_t id);
extern int signal_store_kyber_pre_key_callback(void *store_ctx, uint32_t id, SignalMutPointerKyberPreKeyRecord record);
extern int signal_mark_kyber_pre_key_used_callback(void *store_ctx, uint32_t id, uint32_t ec_prekey_id, SignalMutPointerPublicKey base_key);
extern void signal_destroy_kyber_pre_key_store_callback(void *store_ctx);
*/
import "C"
import (
	"context"
	"unsafe"
)

type KyberPreKeyStore interface {
	LoadKyberPreKey(ctx context.Context, id uint32) (*KyberPreKeyRecord, error)
	StoreKyberPreKey(ctx context.Context, id uint32, kyberPreKeyRecord *KyberPreKeyRecord) error
	MarkKyberPreKeyUsed(ctx context.Context, id uint32) error
}

//export signal_load_kyber_pre_key_callback
func signal_load_kyber_pre_key_callback(storeCtx unsafe.Pointer, keyp *C.SignalMutPointerKyberPreKeyRecord, id C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store KyberPreKeyStore, ctx context.Context) error {
		key, err := store.LoadKyberPreKey(ctx, uint32(id))
		if err == nil && key != nil {
			key.CancelFinalizer()
			keyp.raw = key.ptr
		}
		return err
	})
}

//export signal_store_kyber_pre_key_callback
func signal_store_kyber_pre_key_callback(storeCtx unsafe.Pointer, id C.uint32_t, preKeyRecord C.SignalMutPointerKyberPreKeyRecord) C.int {
	return wrapStoreCallback(storeCtx, func(store KyberPreKeyStore, ctx context.Context) error {
		record := KyberPreKeyRecord{ptr: preKeyRecord.raw}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StoreKyberPreKey(ctx, uint32(id), cloned)
	})
}

//export signal_mark_kyber_pre_key_used_callback
func signal_mark_kyber_pre_key_used_callback(storeCtx unsafe.Pointer, id C.uint32_t, ecPrekeyID C.uint32_t, baseKey C.SignalMutPointerPublicKey) C.int {
	return wrapStoreCallback(storeCtx, func(store KyberPreKeyStore, ctx context.Context) error {
		// TODO use ecPrekeyID and baseKey?
		return store.MarkKyberPreKeyUsed(ctx, uint32(id))
	})
}

//export signal_destroy_kyber_pre_key_store_callback
func signal_destroy_kyber_pre_key_store_callback(storeCtx unsafe.Pointer) {
	// No-op: Go's garbage collector handles cleanup
}

func (ctx *CallbackContext) wrapKyberPreKeyStore(store KyberPreKeyStore) C.SignalConstPointerFfiKyberPreKeyStoreStruct {
	return C.SignalConstPointerFfiKyberPreKeyStoreStruct{&C.SignalKyberPreKeyStore{
		ctx:                     wrapStore(ctx, store),
		load_kyber_pre_key:      C.SignalFfiBridgeKyberPreKeyStoreLoadKyberPreKey(C.signal_load_kyber_pre_key_callback),
		store_kyber_pre_key:     C.SignalFfiBridgeKyberPreKeyStoreStoreKyberPreKey(C.signal_store_kyber_pre_key_callback),
		mark_kyber_pre_key_used: C.SignalFfiBridgeKyberPreKeyStoreMarkKyberPreKeyUsed(C.signal_mark_kyber_pre_key_used_callback),
		destroy:                 C.SignalFfiBridgeKyberPreKeyStoreDestroy(C.signal_destroy_kyber_pre_key_store_callback),
	}}
}
