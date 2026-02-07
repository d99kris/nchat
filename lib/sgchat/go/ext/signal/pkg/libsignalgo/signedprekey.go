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

type SignedPreKeyRecord struct {
	nc  noCopy
	ptr *C.SignalSignedPreKeyRecord
}

func wrapSignedPreKeyRecord(ptr *C.SignalSignedPreKeyRecord) *SignedPreKeyRecord {
	spkr := &SignedPreKeyRecord{ptr: ptr}
	runtime.SetFinalizer(spkr, (*SignedPreKeyRecord).Destroy)
	return spkr
}

func NewSignedPreKeyRecord(id uint32, timestamp time.Time, publicKey *PublicKey, privateKey *PrivateKey, signature []byte) (*SignedPreKeyRecord, error) {
	var spkr C.SignalMutPointerSignedPreKeyRecord
	signalFfiError := C.signal_signed_pre_key_record_new(
		&spkr,
		C.uint32_t(id),
		C.uint64_t(timestamp.UnixMilli()),
		publicKey.constPtr(),
		privateKey.constPtr(),
		BytesToBuffer(signature),
	)
	runtime.KeepAlive(publicKey)
	runtime.KeepAlive(privateKey)
	runtime.KeepAlive(signature)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSignedPreKeyRecord(spkr.raw), nil
}

func NewSignedPreKeyRecordFromPrivateKey(id uint32, timestamp time.Time, privateKey *PrivateKey, signature []byte) (*SignedPreKeyRecord, error) {
	pub, err := privateKey.GetPublicKey()
	if err != nil {
		return nil, err
	}
	return NewSignedPreKeyRecord(id, timestamp, pub, privateKey, signature)
}

func DeserializeSignedPreKeyRecord(serialized []byte) (*SignedPreKeyRecord, error) {
	var spkr C.SignalMutPointerSignedPreKeyRecord
	signalFfiError := C.signal_signed_pre_key_record_deserialize(&spkr, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSignedPreKeyRecord(spkr.raw), nil
}

func (spkr *SignedPreKeyRecord) mutPtr() C.SignalMutPointerSignedPreKeyRecord {
	return C.SignalMutPointerSignedPreKeyRecord{spkr.ptr}
}

func (spkr *SignedPreKeyRecord) constPtr() C.SignalConstPointerSignedPreKeyRecord {
	return C.SignalConstPointerSignedPreKeyRecord{spkr.ptr}
}

func (spkr *SignedPreKeyRecord) Clone() (*SignedPreKeyRecord, error) {
	var cloned C.SignalMutPointerSignedPreKeyRecord
	signalFfiError := C.signal_signed_pre_key_record_clone(&cloned, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSignedPreKeyRecord(cloned.raw), nil
}

func (spkr *SignedPreKeyRecord) Destroy() error {
	spkr.CancelFinalizer()
	return wrapError(C.signal_signed_pre_key_record_destroy(spkr.mutPtr()))
}

func (spkr *SignedPreKeyRecord) CancelFinalizer() {
	runtime.SetFinalizer(spkr, nil)
}

func (spkr *SignedPreKeyRecord) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_signed_pre_key_record_serialize(&serialized, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (spkr *SignedPreKeyRecord) GetSignature() ([]byte, error) {
	var signature C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_signed_pre_key_record_get_signature(&signature, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(signature), nil
}

func (spkr *SignedPreKeyRecord) GetID() (uint32, error) {
	var id C.uint32_t
	signalFfiError := C.signal_signed_pre_key_record_get_id(&id, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(id), nil
}

func (spkr *SignedPreKeyRecord) GetTimestamp() (time.Time, error) {
	var ts C.uint64_t
	signalFfiError := C.signal_signed_pre_key_record_get_timestamp(&ts, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return time.Time{}, wrapError(signalFfiError)
	}
	return time.UnixMilli(int64(ts)), nil
}

func (spkr *SignedPreKeyRecord) GetPublicKey() (*PublicKey, error) {
	var pub C.SignalMutPointerPublicKey
	signalFfiError := C.signal_signed_pre_key_record_get_public_key(
		&pub,
		spkr.constPtr(),
	)
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPublicKey(pub.raw), nil
}

func (spkr *SignedPreKeyRecord) GetPrivateKey() (*PrivateKey, error) {
	var priv C.SignalMutPointerPrivateKey
	signalFfiError := C.signal_signed_pre_key_record_get_private_key(&priv, spkr.constPtr())
	runtime.KeepAlive(spkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPrivateKey(priv.raw), nil
}
