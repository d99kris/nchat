// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2024 Tulir Asokan
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

type SGXClientState struct {
	nc  noCopy
	ptr *C.SignalSgxClientState
}

func wrapSGXClientState(ptr *C.SignalSgxClientState) *SGXClientState {
	cdsClientState := &SGXClientState{ptr: ptr}
	runtime.SetFinalizer(cdsClientState, (*SGXClientState).Destroy)
	return cdsClientState
}

func NewCDS2ClientState(mrenclave, attestationMessage []byte, currentTime time.Time) (*SGXClientState, error) {
	var cds C.SignalMutPointerSgxClientState
	signalFfiError := C.signal_cds2_client_state_new(
		&cds,
		BytesToBuffer(mrenclave),
		BytesToBuffer(attestationMessage),
		C.uint64_t(currentTime.UnixMilli()),
	)
	runtime.KeepAlive(mrenclave)
	runtime.KeepAlive(attestationMessage)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSGXClientState(cds.raw), nil
}

func (cds *SGXClientState) mutPtr() C.SignalMutPointerSgxClientState {
	return C.SignalMutPointerSgxClientState{cds.ptr}
}

func (cds *SGXClientState) constPtr() C.SignalConstPointerSgxClientState {
	return C.SignalConstPointerSgxClientState{cds.ptr}
}

func (cds *SGXClientState) Destroy() error {
	runtime.SetFinalizer(cds, nil)
	return wrapError(C.signal_sgx_client_state_destroy(cds.mutPtr()))
}

func (cds *SGXClientState) InitialRequest() ([]byte, error) {
	var resp C.SignalOwnedBuffer
	signalFfiError := C.signal_sgx_client_state_initial_request(&resp, cds.constPtr())
	runtime.KeepAlive(cds)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(resp), nil
}

func (cds *SGXClientState) CompleteHandshake(handshakeReceived []byte) error {
	signalFfiError := C.signal_sgx_client_state_complete_handshake(cds.mutPtr(), BytesToBuffer(handshakeReceived))
	runtime.KeepAlive(cds)
	runtime.KeepAlive(handshakeReceived)
	return wrapError(signalFfiError)
}

func (cds *SGXClientState) EstablishedSend(plaintext []byte) ([]byte, error) {
	var resp C.SignalOwnedBuffer
	signalFfiError := C.signal_sgx_client_state_established_send(
		&resp,
		cds.mutPtr(),
		BytesToBuffer(plaintext),
	)
	runtime.KeepAlive(cds)
	runtime.KeepAlive(plaintext)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(resp), nil
}

func (cds *SGXClientState) EstablishedReceive(ciphertext []byte) ([]byte, error) {
	var resp C.SignalOwnedBuffer
	signalFfiError := C.signal_sgx_client_state_established_recv(
		&resp,
		cds.mutPtr(),
		BytesToBuffer(ciphertext),
	)
	runtime.KeepAlive(cds)
	runtime.KeepAlive(ciphertext)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(resp), nil
}
