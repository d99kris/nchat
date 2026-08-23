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
import "runtime"

type AccountEntropyPool string
type SVRKey = fixedArray32

func (aep AccountEntropyPool) DeriveSVRKey() ([]byte, error) {
	var out SVRKey
	aepC, free := GoStringToCString(string(aep))
	defer free()
	signalFfiError := C.signal_account_entropy_pool_derive_svr_key(
		out.cFixedArray(),
		aepC,
	)
	runtime.KeepAlive(aep)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return out[:], nil
}

func (aep AccountEntropyPool) DeriveBackupKey() ([]byte, error) {
	var out BackupKey
	aepC, free := GoStringToCString(string(aep))
	defer free()
	signalFfiError := C.signal_account_entropy_pool_derive_backup_key(
		out.cFixedArray(),
		aepC,
	)
	runtime.KeepAlive(aep)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return out[:], nil
}
