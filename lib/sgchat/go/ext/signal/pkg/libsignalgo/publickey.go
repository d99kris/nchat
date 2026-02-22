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

type PublicKey struct {
	nc  noCopy
	ptr *C.SignalPublicKey
}

func wrapPublicKey(ptr *C.SignalPublicKey) *PublicKey {
	if ptr == nil {
		return nil
	}
	publicKey := &PublicKey{ptr: ptr}
	runtime.SetFinalizer(publicKey, (*PublicKey).Destroy)
	return publicKey
}

func (pk *PublicKey) mutPtr() C.SignalMutPointerPublicKey {
	return C.SignalMutPointerPublicKey{pk.ptr}
}

func (pk *PublicKey) constPtr() C.SignalConstPointerPublicKey {
	return C.SignalConstPointerPublicKey{pk.ptr}
}

func (pk *PublicKey) Clone() (*PublicKey, error) {
	var cloned C.SignalMutPointerPublicKey
	signalFfiError := C.signal_publickey_clone(&cloned, pk.constPtr())
	runtime.KeepAlive(pk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPublicKey(cloned.raw), nil
}

func DeserializePublicKey(keyData []byte) (*PublicKey, error) {
	var pk C.SignalMutPointerPublicKey
	signalFfiError := C.signal_publickey_deserialize(&pk, BytesToBuffer(keyData))
	runtime.KeepAlive(keyData)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPublicKey(pk.raw), nil
}

func (pk *PublicKey) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_publickey_serialize(&serialized, pk.constPtr())
	runtime.KeepAlive(pk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (k *PublicKey) Destroy() error {
	k.CancelFinalizer()
	return wrapError(C.signal_publickey_destroy(k.mutPtr()))
}

func (k *PublicKey) CancelFinalizer() {
	runtime.SetFinalizer(k, nil)
}

func (k *PublicKey) Equal(other *PublicKey) (bool, error) {
	var comparison C.bool
	signalFfiError := C.signal_publickey_equals(&comparison, k.constPtr(), other.constPtr())
	runtime.KeepAlive(k)
	runtime.KeepAlive(other)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(comparison), nil
}

func (k *PublicKey) Bytes() ([]byte, error) {
	var pub C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_publickey_get_public_key_bytes(&pub, k.constPtr())
	runtime.KeepAlive(k)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(pub), nil
}

func (k *PublicKey) Verify(message, signature []byte) (bool, error) {
	var verify C.bool
	signalFfiError := C.signal_publickey_verify(
		&verify,
		k.constPtr(),
		BytesToBuffer(message),
		BytesToBuffer(signature),
	)
	runtime.KeepAlive(k)
	runtime.KeepAlive(message)
	runtime.KeepAlive(signature)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(verify), nil
}
