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

type SenderKeyRecord struct {
	nc  noCopy
	ptr *C.SignalSenderKeyRecord
}

func wrapSenderKeyRecord(ptr *C.SignalSenderKeyRecord) *SenderKeyRecord {
	sc := &SenderKeyRecord{ptr: ptr}
	runtime.SetFinalizer(sc, (*SenderKeyRecord).Destroy)
	return sc
}

func DeserializeSenderKeyRecord(serialized []byte) (*SenderKeyRecord, error) {
	var sc C.SignalMutPointerSenderKeyRecord
	signalFfiError := C.signal_sender_key_record_deserialize(&sc, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderKeyRecord(sc.raw), nil
}

func (skr *SenderKeyRecord) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_sender_key_record_serialize(&serialized, skr.constPtr())
	runtime.KeepAlive(skr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (skr *SenderKeyRecord) mutPtr() C.SignalMutPointerSenderKeyRecord {
	return C.SignalMutPointerSenderKeyRecord{skr.ptr}
}

func (skr *SenderKeyRecord) constPtr() C.SignalConstPointerSenderKeyRecord {
	return C.SignalConstPointerSenderKeyRecord{skr.ptr}
}

func (skr *SenderKeyRecord) Clone() (*SenderKeyRecord, error) {
	var cloned C.SignalMutPointerSenderKeyRecord
	signalFfiError := C.signal_sender_key_record_clone(&cloned, skr.constPtr())
	runtime.KeepAlive(skr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderKeyRecord(cloned.raw), nil
}

func (skr *SenderKeyRecord) Destroy() error {
	skr.CancelFinalizer()
	return wrapError(C.signal_sender_key_record_destroy(skr.mutPtr()))
}

func (skr *SenderKeyRecord) CancelFinalizer() {
	runtime.SetFinalizer(skr, nil)
}
