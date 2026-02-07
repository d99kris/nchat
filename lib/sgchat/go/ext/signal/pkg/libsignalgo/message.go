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
	"context"
	"runtime"
	"time"
)

func Encrypt(ctx context.Context, plaintext []byte, forAddress *Address, sessionStore SessionStore, identityKeyStore IdentityKeyStore) (*CiphertextMessage, error) {
	var ciphertextMessage C.SignalMutPointerCiphertextMessage
	var now C.uint64_t = C.uint64_t(time.Now().Unix())
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	signalFfiError := C.signal_encrypt_message(
		&ciphertextMessage,
		BytesToBuffer(plaintext),
		forAddress.constPtr(),
		callbackCtx.wrapSessionStore(sessionStore),
		callbackCtx.wrapIdentityKeyStore(identityKeyStore),
		now,
	)
	runtime.KeepAlive(plaintext)
	runtime.KeepAlive(forAddress)
	if signalFfiError != nil {
		return nil, callbackCtx.wrapError(signalFfiError)
	}
	return wrapCiphertextMessage(ciphertextMessage.raw), nil
}

func Decrypt(ctx context.Context, message *Message, fromAddress *Address, sessionStore SessionStore, identityStore IdentityKeyStore) ([]byte, error) {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	var decrypted C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_decrypt_message(
		&decrypted,
		message.constPtr(),
		fromAddress.constPtr(),
		callbackCtx.wrapSessionStore(sessionStore),
		callbackCtx.wrapIdentityKeyStore(identityStore),
	)
	runtime.KeepAlive(message)
	runtime.KeepAlive(fromAddress)
	if signalFfiError != nil {
		return nil, callbackCtx.wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(decrypted), nil
}

type Message struct {
	nc  noCopy
	ptr *C.SignalMessage
}

func wrapMessage(ptr *C.SignalMessage) *Message {
	message := &Message{ptr: ptr}
	runtime.SetFinalizer(message, (*Message).Destroy)
	return message
}

func DeserializeMessage(serialized []byte) (*Message, error) {
	var m C.SignalMutPointerSignalMessage
	signalFfiError := C.signal_message_deserialize(&m, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapMessage(m.raw), nil
}

func (m *Message) mutPtr() C.SignalMutPointerSignalMessage {
	return C.SignalMutPointerSignalMessage{m.ptr}
}

func (m *Message) constPtr() C.SignalConstPointerSignalMessage {
	return C.SignalConstPointerSignalMessage{m.ptr}
}

func (m *Message) Clone() (*Message, error) {
	var cloned C.SignalMutPointerSignalMessage
	signalFfiError := C.signal_message_clone(&cloned, m.constPtr())
	runtime.KeepAlive(m)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapMessage(cloned.raw), nil
}

func (m *Message) Destroy() error {
	m.CancelFinalizer()
	return wrapError(C.signal_message_destroy(m.mutPtr()))
}

func (m *Message) CancelFinalizer() {
	runtime.SetFinalizer(m, nil)
}

func (m *Message) GetBody() ([]byte, error) {
	var body C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_message_get_body(&body, m.constPtr())
	runtime.KeepAlive(m)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(body), nil
}

func (m *Message) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_message_get_serialized(&serialized, m.constPtr())
	runtime.KeepAlive(m)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (m *Message) GetMessageVersion() (uint32, error) {
	var messageVersion C.uint32_t
	signalFfiError := C.signal_message_get_message_version(&messageVersion, m.constPtr())
	runtime.KeepAlive(m)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(messageVersion), nil
}

func (m *Message) GetCounter() (uint32, error) {
	var counter C.uint32_t
	signalFfiError := C.signal_message_get_counter(&counter, m.constPtr())
	runtime.KeepAlive(m)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(counter), nil
}

func (m *Message) VerifyMAC(sender, receiver *PublicKey, macKey []byte) (bool, error) {
	var result C.bool
	signalFfiError := C.signal_message_verify_mac(
		&result,
		m.constPtr(),
		sender.constPtr(),
		receiver.constPtr(),
		BytesToBuffer(macKey),
	)
	runtime.KeepAlive(m)
	runtime.KeepAlive(sender)
	runtime.KeepAlive(receiver)
	runtime.KeepAlive(macKey)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(result), nil
}
