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
import "runtime"

type MessageBackupKey struct {
	nc  noCopy
	ptr *C.SignalMessageBackupKey
}

const MessageBackupKeyBytesLength = 32

type messageBackupKeyBytes = fixedArray32

func wrapMessageBackupKey(ptr *C.SignalMessageBackupKey) *MessageBackupKey {
	backupKey := &MessageBackupKey{ptr: ptr}
	runtime.SetFinalizer(backupKey, (*MessageBackupKey).Destroy)
	return backupKey
}

func MessageBackupKeyFromAccountEntropyPool(aep AccountEntropyPool, aci ServiceID) (*MessageBackupKey, error) {
	var bk C.SignalMutPointerMessageBackupKey
	aepC, free := GoStringToCString(string(aep))
	defer free()
	signalFfiError := C.signal_message_backup_key_from_account_entropy_pool(
		&bk,
		aepC,
		aci.cConstFixedArray(),
		nil, // TODO what's a forward secrecy token?
	)
	runtime.KeepAlive(aep)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapMessageBackupKey(bk.raw), nil
}

func MessageBackupKeyFromBackupKeyAndID(backupKey *BackupKey, backupID *BackupID) (*MessageBackupKey, error) {
	var bk C.SignalMutPointerMessageBackupKey
	signalFfiError := C.signal_message_backup_key_from_backup_key_and_backup_id(
		&bk,
		backupKey.cConstFixedArray(),
		backupID.cConstFixedArray(),
		nil, // TODO what's a forward secrecy token?
	)
	runtime.KeepAlive(backupKey)
	runtime.KeepAlive(backupID)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapMessageBackupKey(bk.raw), nil
}

func (bk *MessageBackupKey) mutPtr() C.SignalMutPointerMessageBackupKey {
	return C.SignalMutPointerMessageBackupKey{bk.ptr}
}

func (bk *MessageBackupKey) constPtr() C.SignalConstPointerMessageBackupKey {
	return C.SignalConstPointerMessageBackupKey{bk.ptr}
}

func (bk *MessageBackupKey) Destroy() error {
	runtime.SetFinalizer(bk, nil)
	return wrapError(C.signal_message_backup_key_destroy(bk.mutPtr()))
}

func (bk *MessageBackupKey) GetHMACKey() ([MessageBackupKeyBytesLength]byte, error) {
	var out messageBackupKeyBytes
	signalFfiError := C.signal_message_backup_key_get_hmac_key(
		out.cFixedArray(),
		bk.constPtr(),
	)
	if signalFfiError != nil {
		return [MessageBackupKeyBytesLength]byte(out), wrapError(signalFfiError)
	}
	return [MessageBackupKeyBytesLength]byte(out), nil
}

func (bk *MessageBackupKey) GetAESKey() ([MessageBackupKeyBytesLength]byte, error) {
	var out messageBackupKeyBytes
	signalFfiError := C.signal_message_backup_key_get_aes_key(
		out.cFixedArray(),
		bk.constPtr(),
	)
	if signalFfiError != nil {
		return [MessageBackupKeyBytesLength]byte(out), wrapError(signalFfiError)
	}
	return [MessageBackupKeyBytesLength]byte(out), nil
}
