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

type CiphertextMessageType uint8

const (
	CiphertextMessageTypeWhisper   CiphertextMessageType = 2
	CiphertextMessageTypePreKey    CiphertextMessageType = 3
	CiphertextMessageTypeSenderKey CiphertextMessageType = 7
	CiphertextMessageTypePlaintext CiphertextMessageType = 8
)

type CiphertextMessage struct {
	nc  noCopy
	ptr *C.SignalCiphertextMessage
}

func wrapCiphertextMessage(ptr *C.SignalCiphertextMessage) *CiphertextMessage {
	ciphertextMessage := &CiphertextMessage{ptr: ptr}
	runtime.SetFinalizer(ciphertextMessage, (*CiphertextMessage).Destroy)
	return ciphertextMessage
}

func NewCiphertextMessage(plaintext *PlaintextContent) (*CiphertextMessage, error) {
	var ciphertextMessage C.SignalMutPointerCiphertextMessage
	signalFfiError := C.signal_ciphertext_message_from_plaintext_content(
		&ciphertextMessage,
		plaintext.constPtr(),
	)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapCiphertextMessage(ciphertextMessage.raw), nil
}

func (c *CiphertextMessage) mutPtr() C.SignalMutPointerCiphertextMessage {
	return C.SignalMutPointerCiphertextMessage{c.ptr}
}

func (c *CiphertextMessage) constPtr() C.SignalConstPointerCiphertextMessage {
	return C.SignalConstPointerCiphertextMessage{c.ptr}
}

func (c *CiphertextMessage) Destroy() error {
	c.CancelFinalizer()
	return wrapError(C.signal_ciphertext_message_destroy(c.mutPtr()))
}

func (c *CiphertextMessage) CancelFinalizer() {
	runtime.SetFinalizer(c, nil)
}

func (c *CiphertextMessage) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_ciphertext_message_serialize(&serialized, c.constPtr())
	runtime.KeepAlive(c)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (c *CiphertextMessage) MessageType() (CiphertextMessageType, error) {
	var messageType C.uint8_t
	signalFfiError := C.signal_ciphertext_message_type(&messageType, c.constPtr())
	runtime.KeepAlive(c)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return CiphertextMessageType(messageType), nil
}
