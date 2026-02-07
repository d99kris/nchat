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
import "unsafe"

func CopyCStringToString(cString *C.char) (s string) {
	s = C.GoString(cString)
	C.signal_free_string(cString)
	return
}

func CopyBufferToBytes(buffer *C.uchar, length C.size_t) (b []byte) {
	b = C.GoBytes(unsafe.Pointer(buffer), C.int(length))
	C.signal_free_buffer(buffer, length)
	return
}

func CopySignalOwnedBufferToBytes(buffer C.SignalOwnedBuffer) (b []byte) {
	b = C.GoBytes(unsafe.Pointer(buffer.base), C.int(buffer.length))
	C.signal_free_buffer(buffer.base, buffer.length)
	return
}

func CopySignalBytestringArray[T ~[]byte](buffer C.SignalBytestringArray) (b []T) {
	concatted := C.GoBytes(unsafe.Pointer(buffer.bytes.base), C.int(buffer.bytes.length))
	b = make([]T, int(buffer.lengths.length))
	sizeTSize := unsafe.Sizeof(C.size_t(0))
	offset := 0
	for i := 0; i < int(buffer.lengths.length); i++ {
		length := int(*(*C.size_t)(unsafe.Add(unsafe.Pointer(buffer.lengths.base), uintptr(i)*sizeTSize)))
		b[i] = concatted[offset : offset+length]
		offset += length
	}
	C.signal_free_bytestring_array(buffer)
	return
}
