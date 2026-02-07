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

typedef const SignalSessionRecord const_session_record;
typedef const SignalProtocolAddress const_address;

extern int signal_load_session_callback(void *store_ctx, SignalSessionRecord **recordp, const_address *address);
extern int signal_store_session_callback(void *store_ctx, const_address *address, const_session_record *record);
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
func signal_load_session_callback(storeCtx unsafe.Pointer, recordp **C.SignalSessionRecord, address *C.const_address) C.int {
	return wrapStoreCallback(storeCtx, func(store SessionStore, ctx context.Context) error {
		record, err := store.LoadSession(ctx, &Address{ptr: (*C.SignalProtocolAddress)(unsafe.Pointer(address))})
		if err == nil && record != nil {
			record.CancelFinalizer()
			*recordp = record.ptr
		}
		return err
	})
}

//export signal_store_session_callback
func signal_store_session_callback(storeCtx unsafe.Pointer, address *C.const_address, sessionRecord *C.const_session_record) C.int {
	return wrapStoreCallback(storeCtx, func(store SessionStore, ctx context.Context) error {
		record := SessionRecord{ptr: (*C.SignalSessionRecord)(unsafe.Pointer(sessionRecord))}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}
		return store.StoreSession(ctx, &Address{ptr: (*C.SignalProtocolAddress)(unsafe.Pointer(address))}, cloned)
	})
}

func (ctx *CallbackContext) wrapSessionStore(store SessionStore) C.SignalConstPointerFfiSessionStoreStruct {
	return C.SignalConstPointerFfiSessionStoreStruct{&C.SignalSessionStore{
		ctx:           wrapStore(ctx, store),
		load_session:  C.SignalLoadSession(C.signal_load_session_callback),
		store_session: C.SignalStoreSession(C.signal_store_session_callback),
	}}
}
