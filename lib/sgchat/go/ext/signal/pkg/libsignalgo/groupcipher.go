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

func GroupEncrypt(ctx context.Context, ptext []byte, sender *Address, distributionID uuid.UUID, store SenderKeyStore) (*CiphertextMessage, error) {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	var ciphertextMessage C.SignalMutPointerCiphertextMessage
	signalFfiError := C.signal_group_encrypt_message(
		&ciphertextMessage,
		sender.constPtr(),
		*(*C.SignalUuid)(unsafe.Pointer(&distributionID)),
		BytesToBuffer(ptext),
		callbackCtx.wrapSenderKeyStore(store))
	runtime.KeepAlive(ptext)
	runtime.KeepAlive(sender)
	if signalFfiError != nil {
		return nil, callbackCtx.wrapError(signalFfiError)
	}
	return wrapCiphertextMessage(ciphertextMessage.raw), nil
}

func GroupDecrypt(ctx context.Context, ctext []byte, sender *Address, store SenderKeyStore) ([]byte, error) {
	callbackCtx := NewCallbackContext(ctx)
	defer callbackCtx.Unref()
	var resp C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_group_decrypt_message(
		&resp,
		sender.constPtr(),
		BytesToBuffer(ctext),
		callbackCtx.wrapSenderKeyStore(store))
	runtime.KeepAlive(ctext)
	runtime.KeepAlive(sender)
	if signalFfiError != nil {
		return nil, callbackCtx.wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(resp), nil
}
