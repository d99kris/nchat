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

const BackupKeyLength = 32

type BackupKey [BackupKeyLength]byte

func (bk *BackupKey) Slice() []byte {
	if bk == nil {
		return nil
	}
	return bk[:]
}

const BackupIDLength = 16

type BackupID = fixedArray16
type BackupMetadataKey = fixedArray32
type BackupMediaID = fixedArray15
type BackupMediaKey = fixedArray64

func (bk *BackupKey) cFixedArray() *C.SignalType_FixedArray32_uint8_t {
	return (*C.SignalType_FixedArray32_uint8_t)(unsafe.Pointer(bk))
}

func (bk *BackupKey) cConstFixedArray() cFixedArray32Compat {
	return cFixedArray32Compat(bk.cFixedArray())
}

func GenerateRandomBackupKey() *BackupKey {
	return (*BackupKey)(random.Bytes(BackupKeyLength))
}

func BytesToBackupKey(bytes []byte) *BackupKey {
	if len(bytes) != BackupKeyLength {
		return nil
	}
	return (*BackupKey)(bytes)
}

func (bk *BackupKey) DeriveBackupID(aci ServiceID) (*BackupID, error) {
	var out BackupID
	signalFfiError := C.signal_backup_key_derive_backup_id(
		out.cFixedArray(),
		bk.cConstFixedArray(),
		aci.cConstFixedArray(),
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
		bk.cConstFixedArray(),
		aci.cConstFixedArray(),
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
		out.cFixedArray(),
		bk.cConstFixedArray(),
	)
	runtime.KeepAlive(bk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}

func (bk *BackupKey) DeriveMediaID(mediaName string) (*BackupMediaID, error) {
	var out BackupMediaID
	mediaNameStr, mediaNameFree := GoStringToCString(mediaName)
	defer mediaNameFree()
	signalFfiError := C.signal_backup_key_derive_media_id(
		out.cFixedArray(),
		bk.cConstFixedArray(),
		mediaNameStr,
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
		out.cFixedArray(),
		bk.cConstFixedArray(),
		mediaID.cConstFixedArray(),
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
		out.cFixedArray(),
		bk.cConstFixedArray(),
		mediaID.cConstFixedArray(),
	)
	runtime.KeepAlive(bk)
	runtime.KeepAlive(mediaID)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &out, nil
}
