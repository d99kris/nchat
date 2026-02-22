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

extern int signal_load_session_callback(void *store_ctx, SignalMutPointerSessionRecord *recordp, SignalMutPointerProtocolAddress address);
extern int signal_store_session_callback(void *store_ctx, SignalMutPointerProtocolAddress address, SignalMutPointerSessionRecord record);
extern void signal_destroy_session_store_callback(void *store_ctx);
*/
import "C"
import (
	"context"
	"unsafe"
)

type SessionStore interface {
	LoadSession(ctx context.Context, address *Address) (*SessionRecord, error)
	StoreSession(ctx context.Context, address *Address, record *SessionRecord) error
}

//export signal_load_session_callback
func signal_load_session_callback(storeCtx unsafe.Pointer, recordp *C.SignalMutPointerSessionRecord, address C.SignalMutPointerProtocolAddress) C.int {
	return wrapStoreCallback(storeCtx, func(store SessionStore, ctx context.Context) error {
		record, err := store.LoadSession(ctx, &Address{ptr: address.raw})
		if err == nil && record != nil {
			record.CancelFinalizer()
			recordp.raw = record.ptr
		}
		return err
	})
}

//export signal_store_session_callback
func signal_store_session_callback(storeCtx unsafe.Pointer, address C.SignalMutPointerProtocolAddress, sessionRecord C.SignalMutPointerSessionRecord) C.int {
	return wrapStoreCallback(storeCtx, func(store SessionStore, ctx context.Context) error {
		record := SessionRecord{ptr: sessionRecord.raw}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StoreSession(ctx, &Address{ptr: address.raw}, cloned)
	})
}

//export signal_destroy_session_store_callback
func signal_destroy_session_store_callback(storeCtx unsafe.Pointer) {
	// No-op: Go's garbage collector handles cleanup
}

func (ctx *CallbackContext) wrapSessionStore(store SessionStore) C.SignalConstPointerFfiSessionStoreStruct {
	return C.SignalConstPointerFfiSessionStoreStruct{&C.SignalSessionStore{
		ctx:           wrapStore(ctx, store),
		load_session:  C.SignalFfiBridgeSessionStoreLoadSession(C.signal_load_session_callback),
		store_session: C.SignalFfiBridgeSessionStoreStoreSession(C.signal_store_session_callback),
		destroy:       C.SignalFfiBridgeSessionStoreDestroy(C.signal_destroy_session_store_callback),
	}}
}
