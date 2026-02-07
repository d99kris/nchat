// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

type KyberPreKeyRecord struct {
	nc  noCopy
	ptr *C.SignalKyberPreKeyRecord
}

type KyberKeyPair struct {
	nc  noCopy
	ptr *C.SignalKyberKeyPair
}

type KyberPublicKey struct {
	nc  noCopy
	ptr *C.SignalKyberPublicKey
}

type KyberSecretKey struct {
	nc  noCopy
	ptr *C.SignalKyberSecretKey
}

func wrapKyberKeyPair(ptr *C.SignalKyberKeyPair) *KyberKeyPair {
	kp := &KyberKeyPair{ptr: ptr}
	runtime.SetFinalizer(kp, (*KyberKeyPair).Destroy)
	return kp
}

func (kp *KyberKeyPair) mutPtr() C.SignalMutPointerKyberKeyPair {
	return C.SignalMutPointerKyberKeyPair{kp.ptr}
}

func (kp *KyberKeyPair) constPtr() C.SignalConstPointerKyberKeyPair {
	return C.SignalConstPointerKyberKeyPair{kp.ptr}
}

func (kp *KyberKeyPair) Destroy() error {
	kp.CancelFinalizer()
	return wrapError(C.signal_kyber_key_pair_destroy(kp.mutPtr()))
}

func (kp *KyberKeyPair) CancelFinalizer() {
	runtime.SetFinalizer(kp, nil)
}

func wrapKyberPublicKey(ptr *C.SignalKyberPublicKey) *KyberPublicKey {
	publicKey := &KyberPublicKey{ptr: ptr}
	runtime.SetFinalizer(publicKey, (*KyberPublicKey).Destroy)
	return publicKey
}

func (k *KyberPublicKey) mutPtr() C.SignalMutPointerKyberPublicKey {
	return C.SignalMutPointerKyberPublicKey{k.ptr}
}

func (k *KyberPublicKey) constPtr() C.SignalConstPointerKyberPublicKey {
	return C.SignalConstPointerKyberPublicKey{k.ptr}
}

func (k *KyberPublicKey) Destroy() error {
	k.CancelFinalizer()
	return wrapError(C.signal_kyber_public_key_destroy(k.mutPtr()))
}

func (k *KyberPublicKey) CancelFinalizer() {
	runtime.SetFinalizer(k, nil)
}

func wrapKyberSecretKey(ptr *C.SignalKyberSecretKey) *KyberSecretKey {
	secretKey := &KyberSecretKey{ptr: ptr}
	runtime.SetFinalizer(secretKey, (*KyberSecretKey).Destroy)
	return secretKey
}

func (k *KyberSecretKey) mutPtr() C.SignalMutPointerKyberSecretKey {
	return C.SignalMutPointerKyberSecretKey{k.ptr}
}

func (k *KyberSecretKey) constPtr() C.SignalConstPointerKyberSecretKey {
	return C.SignalConstPointerKyberSecretKey{k.ptr}
}

func (k *KyberSecretKey) Destroy() error {
	k.CancelFinalizer()
	return wrapError(C.signal_kyber_secret_key_destroy(k.mutPtr()))
}

func (k *KyberSecretKey) CancelFinalizer() {
	runtime.SetFinalizer(k, nil)
}

func wrapKyberPreKeyRecord(ptr *C.SignalKyberPreKeyRecord) *KyberPreKeyRecord {
	kpkr := &KyberPreKeyRecord{ptr: ptr}
	runtime.SetFinalizer(kpkr, (*KyberPreKeyRecord).Destroy)
	return kpkr
}

func (kp *KyberKeyPair) GetPublicKey() (*KyberPublicKey, error) {
	var pub C.SignalMutPointerKyberPublicKey
	signalFfiError := C.signal_kyber_key_pair_get_public_key(&pub, kp.constPtr())
	runtime.KeepAlive(kp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPublicKey(pub.raw), nil
}

func (kp *KyberPublicKey) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_kyber_public_key_serialize(&serialized, kp.constPtr())
	runtime.KeepAlive(kp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func DeserializeKyberPublicKey(serialized []byte) (*KyberPublicKey, error) {
	var kyberPublicKey C.SignalMutPointerKyberPublicKey
	signalFfiError := C.signal_kyber_public_key_deserialize(&kyberPublicKey, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPublicKey(kyberPublicKey.raw), nil
}

func NewKyberPreKeyRecord(id uint32, timestamp time.Time, keyPair *KyberKeyPair, signature []byte) (*KyberPreKeyRecord, error) {
	var kpkr C.SignalMutPointerKyberPreKeyRecord
	signalFfiError := C.signal_kyber_pre_key_record_new(
		&kpkr,
		C.uint32_t(id),
		C.uint64_t(timestamp.UnixMilli()),
		keyPair.constPtr(),
		BytesToBuffer(signature),
	)
	runtime.KeepAlive(keyPair)
	runtime.KeepAlive(signature)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPreKeyRecord(kpkr.raw), nil
}

func DeserializeKyberPreKeyRecord(serialized []byte) (*KyberPreKeyRecord, error) {
	var kpkr C.SignalMutPointerKyberPreKeyRecord
	signalFfiError := C.signal_kyber_pre_key_record_deserialize(&kpkr, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPreKeyRecord(kpkr.raw), nil
}

func (kpkr *KyberPreKeyRecord) mutPtr() C.SignalMutPointerKyberPreKeyRecord {
	return C.SignalMutPointerKyberPreKeyRecord{kpkr.ptr}
}

func (kpkr *KyberPreKeyRecord) constPtr() C.SignalConstPointerKyberPreKeyRecord {
	return C.SignalConstPointerKyberPreKeyRecord{kpkr.ptr}
}

func (kpkr *KyberPreKeyRecord) Clone() (*KyberPreKeyRecord, error) {
	var cloned C.SignalMutPointerKyberPreKeyRecord
	signalFfiError := C.signal_kyber_pre_key_record_clone(&cloned, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPreKeyRecord(cloned.raw), nil
}

func (kpkr *KyberPreKeyRecord) Destroy() error {
	kpkr.CancelFinalizer()
	return wrapError(C.signal_kyber_pre_key_record_destroy(kpkr.mutPtr()))
}

func (kpkr *KyberPreKeyRecord) CancelFinalizer() {
	runtime.SetFinalizer(kpkr, nil)
}

func (kpkr *KyberPreKeyRecord) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_kyber_pre_key_record_serialize(&serialized, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (kpkr *KyberPreKeyRecord) GetSignature() ([]byte, error) {
	var signature C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_kyber_pre_key_record_get_signature(&signature, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(signature), nil
}

func (kpkr *KyberPreKeyRecord) GetID() (uint32, error) {
	var id C.uint32_t
	signalFfiError := C.signal_kyber_pre_key_record_get_id(&id, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(id), nil
}

func (kpkr *KyberPreKeyRecord) GetTimestamp() (time.Time, error) {
	var ts C.uint64_t
	signalFfiError := C.signal_kyber_pre_key_record_get_timestamp(&ts, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return time.Time{}, wrapError(signalFfiError)
	}
	return time.UnixMilli(int64(ts)), nil
}

func (kpkr *KyberPreKeyRecord) GetPublicKey() (*KyberPublicKey, error) {
	var pub C.SignalMutPointerKyberPublicKey
	signalFfiError := C.signal_kyber_pre_key_record_get_public_key(&pub, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberPublicKey(pub.raw), nil
}

func (kpkr *KyberPreKeyRecord) GetSecretKey() (*KyberSecretKey, error) {
	var sec C.SignalMutPointerKyberSecretKey
	signalFfiError := C.signal_kyber_pre_key_record_get_secret_key(&sec, kpkr.constPtr())
	runtime.KeepAlive(kpkr)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberSecretKey(sec.raw), nil
}

func KyberKeyPairGenerate() (*KyberKeyPair, error) {
	var kp C.SignalMutPointerKyberKeyPair
	signalFfiError := C.signal_kyber_key_pair_generate(&kp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapKyberKeyPair(kp.raw), nil
}
