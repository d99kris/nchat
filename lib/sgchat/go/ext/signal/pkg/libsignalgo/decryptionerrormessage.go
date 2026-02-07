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

type DecryptionErrorMessage struct {
	nc  noCopy
	ptr *C.SignalDecryptionErrorMessage
}

func wrapDecryptionErrorMessage(ptr *C.SignalDecryptionErrorMessage) *DecryptionErrorMessage {
	decryptionErrorMessage := &DecryptionErrorMessage{ptr: ptr}
	runtime.SetFinalizer(decryptionErrorMessage, (*DecryptionErrorMessage).Destroy)
	return decryptionErrorMessage
}

func DeserializeDecryptionErrorMessage(messageBytes []byte) (*DecryptionErrorMessage, error) {
	var dem C.SignalMutPointerDecryptionErrorMessage
	signalFfiError := C.signal_decryption_error_message_deserialize(
		&dem,
		BytesToBuffer(messageBytes),
	)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapDecryptionErrorMessage(dem.raw), nil
}

func DecryptionErrorMessageForOriginalMessage(originalBytes []byte, originalType CiphertextMessageType, originalTs uint64, originalSenderDeviceID uint) (*DecryptionErrorMessage, error) {
	var dem C.SignalMutPointerDecryptionErrorMessage
	signalFfiError := C.signal_decryption_error_message_for_original_message(
		&dem,
		BytesToBuffer(originalBytes),
		C.uint8_t(originalType),
		C.uint64_t(originalTs),
		C.uint32_t(originalSenderDeviceID),
	)
	runtime.KeepAlive(originalBytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapDecryptionErrorMessage(dem.raw), nil
}

func DecryptionErrorMessageFromSerializedContent(serialized []byte) (*DecryptionErrorMessage, error) {
	var dem C.SignalMutPointerDecryptionErrorMessage
	signalFfiError := C.signal_decryption_error_message_extract_from_serialized_content(&dem, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapDecryptionErrorMessage(dem.raw), nil
}

func (dem *DecryptionErrorMessage) mutPtr() C.SignalMutPointerDecryptionErrorMessage {
	return C.SignalMutPointerDecryptionErrorMessage{dem.ptr}
}

func (dem *DecryptionErrorMessage) constPtr() C.SignalConstPointerDecryptionErrorMessage {
	return C.SignalConstPointerDecryptionErrorMessage{dem.ptr}
}

func (dem *DecryptionErrorMessage) Clone() (*DecryptionErrorMessage, error) {
	var cloned C.SignalMutPointerDecryptionErrorMessage
	signalFfiError := C.signal_decryption_error_message_clone(&cloned, dem.constPtr())
	runtime.KeepAlive(dem)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapDecryptionErrorMessage(cloned.raw), nil
}

func (dem *DecryptionErrorMessage) Destroy() error {
	dem.CancelFinalizer()
	return wrapError(C.signal_decryption_error_message_destroy(dem.mutPtr()))
}

func (dem *DecryptionErrorMessage) CancelFinalizer() {
	runtime.SetFinalizer(dem, nil)
}

func (dem *DecryptionErrorMessage) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_decryption_error_message_serialize(&serialized, dem.constPtr())
	runtime.KeepAlive(dem)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (dem *DecryptionErrorMessage) GetTimestamp() (uint64, error) {
	var ts C.uint64_t
	signalFfiError := C.signal_decryption_error_message_get_timestamp(&ts, dem.constPtr())
	runtime.KeepAlive(dem)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint64(ts), nil
}

func (dem *DecryptionErrorMessage) GetDeviceID() (uint32, error) {
	var deviceID C.uint32_t
	signalFfiError := C.signal_decryption_error_message_get_device_id(&deviceID, dem.constPtr())
	runtime.KeepAlive(dem)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(deviceID), nil
}

func (dem *DecryptionErrorMessage) GetRatchetKey() (*PublicKey, error) {
	var pk C.SignalMutPointerPublicKey
	signalFfiError := C.signal_decryption_error_message_get_ratchet_key(&pk, dem.constPtr())
	runtime.KeepAlive(dem)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPublicKey(pk.raw), nil
}
