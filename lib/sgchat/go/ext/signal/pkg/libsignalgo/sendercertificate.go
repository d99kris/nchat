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
	"unsafe"

	"github.com/google/uuid"
)

type SenderCertificate struct {
	nc  noCopy
	ptr *C.SignalSenderCertificate
}

func wrapSenderCertificate(ptr *C.SignalSenderCertificate) *SenderCertificate {
	sc := &SenderCertificate{ptr: ptr}
	runtime.SetFinalizer(sc, (*SenderCertificate).Destroy)
	return sc
}

// NewSenderCertificate should only be used for testing (at least according to
// the Swift bindings).
func NewSenderCertificate(sender *SealedSenderAddress, publicKey *PublicKey, expiration time.Time, signerCertificate *ServerCertificate, signerKey *PrivateKey) (*SenderCertificate, error) {
	var sc C.SignalMutPointerSenderCertificate
	signalFfiError := C.signal_sender_certificate_new(
		&sc,
		C.CString(sender.UUID.String()),
		C.CString(sender.E164),
		C.uint32_t(sender.DeviceID),
		publicKey.constPtr(),
		C.uint64_t(expiration.UnixMilli()),
		signerCertificate.constPtr(),
		signerKey.constPtr(),
	)
	runtime.KeepAlive(sender)
	runtime.KeepAlive(publicKey)
	runtime.KeepAlive(signerCertificate)
	runtime.KeepAlive(signerKey)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderCertificate(sc.raw), nil
}

func DeserializeSenderCertificate(serialized []byte) (*SenderCertificate, error) {
	var sc C.SignalMutPointerSenderCertificate
	signalFfiError := C.signal_sender_certificate_deserialize(&sc, BytesToBuffer(serialized))
	runtime.KeepAlive(serialized)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderCertificate(sc.raw), nil
}

func (sc *SenderCertificate) Clone() (*SenderCertificate, error) {
	var cloned C.SignalMutPointerSenderCertificate
	signalFfiError := C.signal_sender_certificate_clone(
		&cloned,
		sc.constPtr(),
	)
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapSenderCertificate(cloned.raw), nil
}

func (sc *SenderCertificate) mutPtr() C.SignalMutPointerSenderCertificate {
	return C.SignalMutPointerSenderCertificate{sc.ptr}
}

func (sc *SenderCertificate) constPtr() C.SignalConstPointerSenderCertificate {
	return C.SignalConstPointerSenderCertificate{sc.ptr}
}

func (sc *SenderCertificate) Destroy() error {
	sc.CancelFinalizer()
	return wrapError(C.signal_sender_certificate_destroy(sc.mutPtr()))
}

func (sc *SenderCertificate) CancelFinalizer() {
	runtime.SetFinalizer(sc, nil)
}

func (sc *SenderCertificate) Serialize() ([]byte, error) {
	var serialized C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_sender_certificate_get_serialized(&serialized, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(serialized), nil
}

func (sc *SenderCertificate) GetCertificate() ([]byte, error) {
	var certificate C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_sender_certificate_get_certificate(&certificate, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(certificate), nil
}

func (sc *SenderCertificate) GetSignature() ([]byte, error) {
	var signature C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_sender_certificate_get_signature(&signature, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(signature), nil
}

func (sc *SenderCertificate) GetSenderUUID() (uuid.UUID, error) {
	var rawUUID *C.char
	signalFfiError := C.signal_sender_certificate_get_sender_uuid(&rawUUID, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return uuid.UUID{}, wrapError(signalFfiError)
	}
	return uuid.Parse(CopyCStringToString(rawUUID))
}

func (sc *SenderCertificate) GetSenderE164() (string, error) {
	var e164 *C.char
	signalFfiError := C.signal_sender_certificate_get_sender_e164(&e164, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return "", wrapError(signalFfiError)
	}
	if e164 == nil {
		return "", nil
	}
	return CopyCStringToString(e164), nil
}

func (sc *SenderCertificate) GetExpiration() (time.Time, error) {
	var expiration C.uint64_t
	signalFfiError := C.signal_sender_certificate_get_expiration(&expiration, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return time.Time{}, wrapError(signalFfiError)
	}
	return time.UnixMilli(int64(uint64(expiration))), nil
}

func (sc *SenderCertificate) GetDeviceID() (uint32, error) {
	var deviceID C.uint32_t
	signalFfiError := C.signal_sender_certificate_get_device_id(&deviceID, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint32(deviceID), nil
}

func (sc *SenderCertificate) GetKey() (*PublicKey, error) {
	var key C.SignalMutPointerPublicKey
	signalFfiError := C.signal_sender_certificate_get_key(&key, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPublicKey(key.raw), nil
}

func (sc *SenderCertificate) Validate(trustRoots []*PublicKey, ts time.Time) (bool, error) {
	var valid C.bool
	constRoots := make([]C.SignalConstPointerPublicKey, len(trustRoots))
	for i, root := range trustRoots {
		constRoots[i] = root.constPtr()
	}
	signalFfiError := C.signal_sender_certificate_validate(
		&valid,
		sc.constPtr(),
		// TODO this might not be correct
		C.SignalBorrowedSliceOfConstPointerPublicKey{
			base:   unsafe.SliceData(constRoots),
			length: C.size_t(len(constRoots)),
		},
		C.uint64_t(ts.UnixMilli()),
	)
	runtime.KeepAlive(sc)
	runtime.KeepAlive(constRoots)
	if signalFfiError != nil {
		return false, wrapError(signalFfiError)
	}
	return bool(valid), nil
}

func (sc *SenderCertificate) GetServerCertificate() (*ServerCertificate, error) {
	var serverCertificate C.SignalMutPointerServerCertificate
	signalFfiError := C.signal_sender_certificate_get_server_certificate(&serverCertificate, sc.constPtr())
	runtime.KeepAlive(sc)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapServerCertificate(serverCertificate.raw), nil
}
