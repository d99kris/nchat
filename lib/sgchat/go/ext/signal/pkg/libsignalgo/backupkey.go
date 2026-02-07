// mautrix-signal - A Matrix-signal puppeting bridge.
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
	"unsafe"

	"go.mau.fi/util/random"
)

type BackupKey [C.SignalBACKUP_KEY_LEN]byte

func (bk *BackupKey) Slice() []byte {
	if bk == nil {
		return nil
	}
	return bk[:]
}

const BackupIDLength = 16

type BackupID [BackupIDLength]byte
type BackupMetadataKey [C.SignalLOCAL_BACKUP_METADATA_KEY_LEN]byte
type BackupMediaID [C.SignalMEDIA_ID_LEN]byte
type BackupMediaKey [C.SignalMEDIA_ENCRYPTION_KEY_LEN]byte

func GenerateRandomBackupKey() *BackupKey {
	return (*BackupKey)(random.Bytes(C.SignalBACKUP_KEY_LEN))
}

func BytesToBackupKey(bytes []byte) *BackupKey {
	if len(bytes) != C.SignalBACKUP_KEY_LEN {
		return nil
	}
	return (*BackupKey)(bytes)
}

func (bk *BackupKey) DeriveBackupID(aci ServiceID) (*BackupID, error) {
	var out BackupID
	signalFfiError := C.signal_backup_key_derive_backup_id(
		(*[BackupIDLength]C.uint8_t)(unsafe.Pointer(&out)),
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(bk)),
		aci.CFixedBytes(),
	)
	runtime.KeepAlive(bk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}

func (bk *BackupKey) DeriveECKey(aci ServiceID) (*PrivateKey, error) {
	var out C.SignalMutPointerPrivateKey
	signalFfiError := C.signal_backup_key_derive_ec_key(
		&out,
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(&bk)),
		aci.CFixedBytes(),
	)
	runtime.KeepAlive(bk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapPrivateKey(out.raw), nil
}

func (bk *BackupKey) DeriveLocalBackupMetadataKey() (*BackupMetadataKey, error) {
	var out BackupMetadataKey
	signalFfiError := C.signal_backup_key_derive_local_backup_metadata_key(
		(*[C.SignalLOCAL_BACKUP_METADATA_KEY_LEN]C.uint8_t)(unsafe.Pointer(&out)),
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(bk)),
	)
	runtime.KeepAlive(bk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}

func (bk *BackupKey) DeriveMediaID(mediaName string) (*BackupMediaID, error) {
	var out BackupMediaID
	signalFfiError := C.signal_backup_key_derive_media_id(
		(*[C.SignalMEDIA_ID_LEN]C.uint8_t)(unsafe.Pointer(&out)),
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(bk)),
		C.CString(mediaName),
	)
	runtime.KeepAlive(bk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}

func (bk *BackupKey) DeriveMediaEncryptionKey(mediaID *BackupMediaID) (*BackupMediaKey, error) {
	var out BackupMediaKey
	signalFfiError := C.signal_backup_key_derive_media_encryption_key(
		(*[C.SignalMEDIA_ENCRYPTION_KEY_LEN]C.uint8_t)(unsafe.Pointer(&out)),
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(bk)),
		(*[C.SignalMEDIA_ID_LEN]C.uint8_t)(unsafe.Pointer(mediaID)),
	)
	runtime.KeepAlive(bk)
	runtime.KeepAlive(mediaID)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}

func (bk *BackupKey) DeriveThumbnailTransitEncryptionKey(mediaID *BackupMediaID) (*BackupMediaKey, error) {
	var out BackupMediaKey
	signalFfiError := C.signal_backup_key_derive_thumbnail_transit_encryption_key(
		(*[C.SignalMEDIA_ENCRYPTION_KEY_LEN]C.uint8_t)(unsafe.Pointer(&out)),
		(*[C.SignalBACKUP_KEY_LEN]C.uint8_t)(unsafe.Pointer(bk)),
		(*[C.SignalMEDIA_ID_LEN]C.uint8_t)(unsafe.Pointer(mediaID)),
	)
	runtime.KeepAlive(bk)
	runtime.KeepAlive(mediaID)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}
