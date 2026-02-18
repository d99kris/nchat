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

typedef const SignalPreKeyRecord const_pre_key_record;

extern int signal_load_pre_key_callback(void *store_ctx, SignalPreKeyRecord **recordp, uint32_t id);
extern int signal_store_pre_key_callback(void *store_ctx, uint32_t id, const_pre_key_record *record);
extern int signal_remove_pre_key_callback(void *store_ctx, uint32_t id);
*/
import "C"
import (
	"context"
	"unsafe"
)

type PreKeyStore interface {
	LoadPreKey(ctx context.Context, id uint32) (*PreKeyRecord, error)
	StorePreKey(ctx context.Context, id uint32, preKeyRecord *PreKeyRecord) error
	RemovePreKey(ctx context.Context, id uint32) error
}

//export signal_load_pre_key_callback
func signal_load_pre_key_callback(storeCtx unsafe.Pointer, keyp **C.SignalPreKeyRecord, id C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store PreKeyStore, ctx context.Context) error {
		key, err := store.LoadPreKey(ctx, uint32(id))
		if err == nil && key != nil {
			key.CancelFinalizer()
			*keyp = key.ptr
		}
		return err
	})
}

//export signal_store_pre_key_callback
func signal_store_pre_key_callback(storeCtx unsafe.Pointer, id C.uint32_t, preKeyRecord *C.const_pre_key_record) C.int {
	return wrapStoreCallback(storeCtx, func(store PreKeyStore, ctx context.Context) error {
		record := PreKeyRecord{ptr: (*C.SignalPreKeyRecord)(unsafe.Pointer(preKeyRecord))}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StorePreKey(ctx, uint32(id), cloned)
	})
}

//export signal_remove_pre_key_callback
func signal_remove_pre_key_callback(storeCtx unsafe.Pointer, id C.uint32_t) C.int {
	return wrapStoreCallback(storeCtx, func(store PreKeyStore, ctx context.Context) error {
		return store.RemovePreKey(ctx, uint32(id))
	})
}

func (ctx *CallbackContext) wrapPreKeyStore(store PreKeyStore) C.SignalConstPointerFfiPreKeyStoreStruct {
	return C.SignalConstPointerFfiPreKeyStoreStruct{&C.SignalPreKeyStore{
		ctx:            wrapStore(ctx, store),
		load_pre_key:   C.SignalLoadPreKey(C.signal_load_pre_key_callback),
		store_pre_key:  C.SignalStorePreKey(C.signal_store_pre_key_callback),
		remove_pre_key: C.SignalRemovePreKey(C.signal_remove_pre_key_callback),
	}}
}
