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
	"fmt"
	"runtime"
	"unsafe"
)

func BorrowedMutableBuffer(length int) C.SignalBorrowedMutableBuffer {
	data := make([]byte, length)
	return C.SignalBorrowedMutableBuffer{
		base:   (*C.uchar)(unsafe.Pointer(&data[0])),
		length: C.size_t(len(data)),
	}
}

func BytesToBuffer(data []byte) C.SignalBorrowedBuffer {
	buf := C.SignalBorrowedBuffer{
		length: C.size_t(len(data)),
	}
	if len(data) > 0 {
		buf.base = (*C.uchar)(unsafe.Pointer(&data[0]))
	}
	return buf
}

func ManyBytesToBuffer[T ~[]byte](datas []T) (C.SignalBorrowedSliceOfBuffers, func()) {
	buffers := make([]C.SignalBorrowedBuffer, len(datas))
	var pinner runtime.Pinner
	for i, data := range datas {
		if len(data) == 0 {
			panic(fmt.Errorf("empty slice passed to ManyBytesToBuffer at index %d", i))
		}
		pinner.Pin(&data[0])
		buffers[i] = BytesToBuffer(data)
	}
	return C.SignalBorrowedSliceOfBuffers{
		base:   unsafe.SliceData(buffers),
		length: C.size_t(len(buffers)),
	}, pinner.Unpin
}

func EmptyBorrowedBuffer() C.SignalBorrowedBuffer {
	return C.SignalBorrowedBuffer{}
}

// TODO: Try out this code from ChatGPT that might be more memory safe
// - Makes copy of data
// - Sets finalizer to free memory
//
//type CBytesWrapper struct {
//	c unsafe.Pointer
//}
//
//func CBytes(b []byte) *CBytesWrapper {
//	if len(b) == 0 {
//		return &CBytesWrapper{nil}
//	}
//	c := C.malloc(C.size_t(len(b)))
//	copy((*[1 << 30]byte)(c)[:], b)
//	return &CBytesWrapper{c}
//}
//
//func BytesToBuffer(data []byte) C.SignalBorrowedBuffer {
//	cData := CBytes(data)
//	buf := C.SignalBorrowedBuffer{
//		length: C.uintptr_t(len(data)),
//	}
//	if len(data) > 0 {
//		buf.base = (*C.uchar)(cData.c)
//	}
//
//	// Setting finalizer here
//	runtime.SetFinalizer(cData, func(c *CBytesWrapper) { C.free(c.c) })
//
//	return buf
//}
//
