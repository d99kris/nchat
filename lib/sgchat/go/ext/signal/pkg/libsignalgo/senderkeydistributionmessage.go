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
	"unsafe"

	"github.com/google/uuid"
)

func ProcessSenderKeyDistributionMessage(ctx context.Context, message *SenderKeyDistributionMessage, fromSender *Address, store SenderKeyStore) error {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	signalFfiError := C.signal_process_sender_key_distribution_message(
		fromSender.constPtr(),
		message.constPtr(),
		callbackCtx.wrapSenderKeyStore(store),
	)
	runtime.KeepAlive(message)
	runtime.KeepAlive(fromSender)
	return callbackCtx.wrapError(signalFfiError)
}

type SenderKeyDistributionMessage struct {
	nc  noCopy
	ptr *C.SignalSenderKeyDistributionMessage
}

func wrapSenderKeyDistributionMessage(ptr *C.SignalSenderKeyDistributionMessage) *SenderKeyDistributionMessage {
	sc := &SenderKeyDistributionMessage{ptr: ptr}
	runtime.SetFinalizer(sc, (*SenderKeyDistributionMessage).Destroy)
	return sc
}

func NewSenderKeyDistributionMessage(ctx context.Context, sender *Address, distributionID uuid.UUID, store SenderKeyStore) (*SenderKeyDistributionMessage, error) {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	var skdm C.SignalMutPointerSenderKeyDistributionMessage
	signalFfiError := C.signal_sender_key_distribution_message_create(
		&skdm,
		sender.constPtr(),
		*(*C.SignalUuid)(unsafe.Pointer(&distributionID)),
		callbackCtx.wrapSenderKeyStore(store),
	)
	runtime.KeepAlive(sender)
	runtime.KeepAlive(distributionID)
	if signalFfiError != nil {
		return nil, callbackCtx.wrapError(signalFfiError)
	}
	return wrapSenderKeyDistributionMessage(skdm.raw), nil
}

func DeserializeSenderKeyDistributionMessage(serialized []byte) (*SenderKeyDistributionMessage, error) {
	var skdm C.SignalMutPointerSenderKeyDistributionMessage
	signalFfiError := C.signal_sender_key_distribution_message_deserialize(&skdm, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderKeyDistributionMessage(skdm.raw), nil
}

func (sc *SenderKeyDistributionMessage) mutPtr() C.SignalMutPointerSenderKeyDistributionMessage {
	return C.SignalMutPointerSenderKeyDistributionMessage{sc.ptr}
}

func (sc *SenderKeyDistributionMessage) constPtr() C.SignalConstPointerSenderKeyDistributionMessage {
	return C.SignalConstPointerSenderKeyDistributionMessage{sc.ptr}
}

func (sc *SenderKeyDistributionMessage) Destroy() error {
	sc.CancelFinalizer()
	return wrapError(C.signal_sender_key_distribution_message_destroy(sc.mutPtr()))
}

func (sc *SenderKeyDistributionMessage) CancelFinalizer() {
	runtime.SetFinalizer(sc, nil)
}

func (sc *SenderKeyDistributionMessage) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_sender_key_distribution_message_serialize(&serialized, sc.constPtr())
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (sc *SenderKeyDistributionMessage) Process(ctx context.Context, sender *Address, store SenderKeyStore) error {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	signalFfiError := C.signal_process_sender_key_distribution_message(
		sender.constPtr(),
		sc.constPtr(),
		callbackCtx.wrapSenderKeyStore(store),
	)
	runtime.KeepAlive(sender)
	if signalFfiError != nil {
		return callbackCtx.wrapError(signalFfiError)
	}
	return nil
}
