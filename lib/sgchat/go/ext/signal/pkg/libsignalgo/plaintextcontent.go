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
import "runtime"

type PlaintextContent struct {
	nc  noCopy
	ptr *C.SignalPlaintextContent
}

func wrapPlaintextContent(ptr *C.SignalPlaintextContent) *PlaintextContent {
	plaintextContent := &PlaintextContent{ptr: ptr}
	runtime.SetFinalizer(plaintextContent, (*PlaintextContent).Destroy)
	return plaintextContent
}

func PlaintextContentFromDecryptionErrorMessage(message *DecryptionErrorMessage) (*PlaintextContent, error) {
	var pc C.SignalMutPointerPlaintextContent
	signalFfiError := C.signal_plaintext_content_from_decryption_error_message(
		&pc,
		message.constPtr(),
	)
	runtime.KeepAlive(message)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPlaintextContent(pc.raw), nil
}

func DeserializePlaintextContent(plaintextContentBytes []byte) (*PlaintextContent, error) {
	var pc C.SignalMutPointerPlaintextContent
	signalFfiError := C.signal_plaintext_content_deserialize(&pc, BytesToBuffer(plaintextContentBytes))
	runtime.KeepAlive(plaintextContentBytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPlaintextContent(pc.raw), nil
}

func (pc *PlaintextContent) mutPtr() C.SignalMutPointerPlaintextContent {
	return C.SignalMutPointerPlaintextContent{pc.ptr}
}

func (pc *PlaintextContent) constPtr() C.SignalConstPointerPlaintextContent {
	return C.SignalConstPointerPlaintextContent{pc.ptr}
}

func (pc *PlaintextContent) Clone() (*PlaintextContent, error) {
	var cloned C.SignalMutPointerPlaintextContent
	signalFfiError := C.signal_plaintext_content_clone(
		&cloned,
		pc.constPtr(),
	)
	runtime.KeepAlive(pc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPlaintextContent(cloned.raw), nil
}

func (pc *PlaintextContent) Destroy() error {
	pc.CancelFinalizer()
	return wrapError(C.signal_plaintext_content_destroy(pc.mutPtr()))
}

func (pc *PlaintextContent) CancelFinalizer() {
	runtime.SetFinalizer(pc, nil)
}

func (pc *PlaintextContent) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_plaintext_content_serialize(&serialized, pc.constPtr())
	runtime.KeepAlive(pc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (pc *PlaintextContent) GetBody() ([]byte, error) {
	var body C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_plaintext_content_get_body(&body, pc.constPtr())
	runtime.KeepAlive(pc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(body), nil
}
