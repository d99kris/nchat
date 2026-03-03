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

extern int signal_load_sender_key_callback(void *store_ctx, SignalMutPointerSenderKeyRecord *out, SignalMutPointerProtocolAddress sender, SignalUuid distribution_id);
extern int signal_store_sender_key_callback(void *store_ctx, SignalMutPointerProtocolAddress sender, SignalUuid distribution_id, SignalMutPointerSenderKeyRecord record);
extern void signal_destroy_sender_key_store_callback(void *store_ctx);
*/
import "C"
import (
	"context"
	"unsafe"

	"github.com/google/uuid"
)

type SenderKeyStore interface {
	LoadSenderKey(ctx context.Context, sender *Address, distributionID uuid.UUID) (*SenderKeyRecord, error)
	StoreSenderKey(ctx context.Context, sender *Address, distributionID uuid.UUID, record *SenderKeyRecord) error
}

//export signal_load_sender_key_callback
func signal_load_sender_key_callback(storeCtx unsafe.Pointer, recordp *C.SignalMutPointerSenderKeyRecord, address C.SignalMutPointerProtocolAddress, distributionID C.SignalUuid) C.int {
	return wrapStoreCallback(storeCtx, func(store SenderKeyStore, ctx context.Context) error {
		record, err := store.LoadSenderKey(ctx, &Address{ptr: address.raw}, *(*uuid.UUID)(unsafe.Pointer(&distributionID)))
		if err == nil && record != nil {
			record.CancelFinalizer()
			recordp.raw = record.ptr
		}
		return err
	})
}

//export signal_store_sender_key_callback
func signal_store_sender_key_callback(storeCtx unsafe.Pointer, address C.SignalMutPointerProtocolAddress, distributionID C.SignalUuid, senderKeyRecord C.SignalMutPointerSenderKeyRecord) C.int {
	return wrapStoreCallback(storeCtx, func(store SenderKeyStore, ctx context.Context) error {
		record := SenderKeyRecord{ptr: senderKeyRecord.raw}
		cloned, err := record.Clone()
		if err != nil {
			return err
		}

		return store.StoreSenderKey(ctx, &Address{ptr: address.raw}, *(*uuid.UUID)(unsafe.Pointer(&distributionID)), cloned)
	})
}

//export signal_destroy_sender_key_store_callback
func signal_destroy_sender_key_store_callback(storeCtx unsafe.Pointer) {
	// No-op: Go's garbage collector handles cleanup
}

func (ctx *CallbackContext) wrapSenderKeyStore(store SenderKeyStore) C.SignalConstPointerFfiSenderKeyStoreStruct {
	return C.SignalConstPointerFfiSenderKeyStoreStruct{&C.SignalSenderKeyStore{
		ctx:              wrapStore(ctx, store),
		load_sender_key:  C.SignalFfiBridgeSenderKeyStoreLoadSenderKey(C.signal_load_sender_key_callback),
		store_sender_key: C.SignalFfiBridgeSenderKeyStoreStoreSenderKey(C.signal_store_sender_key_callback),
		destroy:          C.SignalFfiBridgeSenderKeyStoreDestroy(C.signal_destroy_sender_key_store_callback),
	}}
}
