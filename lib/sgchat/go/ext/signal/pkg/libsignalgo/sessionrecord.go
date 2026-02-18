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
	"time"
)

type SessionRecord struct {
	nc  noCopy
	ptr *C.SignalSessionRecord
}

func wrapSessionRecord(ptr *C.SignalSessionRecord) *SessionRecord {
	sessionRecord := &SessionRecord{ptr: ptr}
	runtime.SetFinalizer(sessionRecord, (*SessionRecord).Destroy)
	return sessionRecord
}

func DeserializeSessionRecord(serialized []byte) (*SessionRecord, error) {
	var ptr C.SignalMutPointerSessionRecord
	signalFfiError := C.signal_session_record_deserialize(&ptr, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSessionRecord(ptr.raw), nil
}

func (sr *SessionRecord) mutPtr() C.SignalMutPointerSessionRecord {
	return C.SignalMutPointerSessionRecord{sr.ptr}
}

func (sr *SessionRecord) constPtr() C.SignalConstPointerSessionRecord {
	return C.SignalConstPointerSessionRecord{sr.ptr}
}

func (sr *SessionRecord) Clone() (*SessionRecord, error) {
	var clone C.SignalMutPointerSessionRecord
	signalFfiError := C.signal_session_record_clone(
		&clone,
		sr.constPtr(),
	)
	runtime.KeepAlive(sr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSessionRecord(clone.raw), nil
}

func (sr *SessionRecord) Destroy() error {
	sr.CancelFinalizer()
	return wrapError(C.signal_session_record_destroy(sr.mutPtr()))
}

func (sr *SessionRecord) CancelFinalizer() {
	runtime.SetFinalizer(sr, nil)
}

func (sr *SessionRecord) ArchiveCurrentState() error {
	defer runtime.KeepAlive(sr)
	return wrapError(C.signal_session_record_archive_current_state(sr.mutPtr()))
}

func (sr *SessionRecord) CurrentRatchetKeyMatches(key *PublicKey) (bool, error) {
	if sr == nil || key == nil {
		return false, nil
	}
	var result C.bool
	signalFfiError := C.signal_session_record_current_ratchet_key_matches(
		&result,
		sr.constPtr(),
		key.constPtr(),
	)
	runtime.KeepAlive(sr)
	runtime.KeepAlive(key)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(result), nil
}

func (sr *SessionRecord) HasCurrentState() (bool, error) {
	var result C.bool
	signalFfiError := C.signal_session_record_has_usable_sender_chain(
		&result,
		sr.constPtr(),
		C.uint64_t(time.Now().Unix()),
	)
	runtime.KeepAlive(sr)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(result), nil
}

func (sr *SessionRecord) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_session_record_serialize(&serialized, sr.constPtr())
	runtime.KeepAlive(sr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (sr *SessionRecord) GetLocalRegistrationID() (uint32, error) {
	var result C.uint32_t
	signalFfiError := C.signal_session_record_get_local_registration_id(&result, sr.constPtr())
	runtime.KeepAlive(sr)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(result), nil
}

func (sr *SessionRecord) GetRemoteRegistrationID() (uint32, error) {
	var result C.uint32_t
	signalFfiError := C.signal_session_record_get_remote_registration_id(&result, sr.constPtr())
	runtime.KeepAlive(sr)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(result), nil
}
