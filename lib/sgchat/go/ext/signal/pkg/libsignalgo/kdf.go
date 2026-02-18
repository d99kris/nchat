// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
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
)

func HKDFDerive(outputLength int, inputKeyMaterial, salt, info []byte) ([]byte, error) {
	output := BorrowedMutableBuffer(outputLength)
	signalFfiError := C.signal_hkdf_derive(output, BytesToBuffer(inputKeyMaterial), BytesToBuffer(info), BytesToBuffer(salt))
	runtime.KeepAlive(inputKeyMaterial)
	runtime.KeepAlive(salt)
	runtime.KeepAlive(info)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	// No need to wrap this in a CopyBufferToBytes since this is allocated by
	// Go and thus will be properly garbage collected.
	return C.GoBytes(unsafe.Pointer(output.base), C.int(output.length)), nil
}
