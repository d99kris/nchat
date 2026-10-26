// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2026 Tulir Asokan
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
)

type UsernameHash = fixedArray32

func DecryptUsernameLink(entropy, encryptedUsername []byte) (string, error) {
	var username C.SignalCStringPtr
	signalFfiError := C.signal_username_link_decrypt_username(&username, BytesToBuffer(entropy), BytesToBuffer(encryptedUsername))
	runtime.KeepAlive(entropy)
	runtime.KeepAlive(encryptedUsername)
	if signalFfiError != nil {
		return "", wrapError(signalFfiError)
	}
	return CopyCStringToString(username), nil
}

func HashUsername(username string) (UsernameHash, error) {
	cUsername, free := GoStringToCString(username)
	defer free()
	var result UsernameHash
	signalFfiError := C.signal_username_hash(result.cFixedArray(), cUsername)
	if signalFfiError != nil {
		return UsernameHash{}, wrapError(signalFfiError)
	}
	return result, nil
}
