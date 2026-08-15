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
import "unsafe"

type fixedArray15 [15]byte
type fixedArray16 [16]byte
type fixedArray17 [17]byte
type fixedArray32 [32]byte
type fixedArray64 [64]byte
type fixedArray65 [65]byte
type fixedArray97 [97]byte
type fixedArray129 [129]byte
type fixedArray153 [153]byte
type fixedArray177 [177]byte
type fixedArray289 [289]byte
type fixedArray329 [329]byte
type fixedArray409 [409]byte
type fixedArray473 [473]byte
type fixedArray497 [497]byte

func (a *fixedArray15) cFixedArray() *C.SignalType_FixedArray15_uint8_t {
	return (*C.SignalType_FixedArray15_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray15) cConstFixedArray() cFixedArray15Compat {
	return cFixedArray15Compat(a.cFixedArray())
}

func (a *fixedArray16) cFixedArray() *C.SignalType_FixedArray16_uint8_t {
	return (*C.SignalType_FixedArray16_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray16) cConstFixedArray() cFixedArray16Compat {
	return cFixedArray16Compat(a.cFixedArray())
}

func (a *fixedArray17) cFixedArray() *C.SignalType_FixedArray17_uint8_t {
	return (*C.SignalType_FixedArray17_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray17) cConstFixedArray() cFixedArray17Compat {
	return cFixedArray17Compat(a.cFixedArray())
}

func (a *fixedArray32) cFixedArray() *C.SignalType_FixedArray32_uint8_t {
	return (*C.SignalType_FixedArray32_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray32) cConstFixedArray() cFixedArray32Compat {
	return cFixedArray32Compat(a.cFixedArray())
}

func (a *fixedArray64) cFixedArray() *C.SignalType_FixedArray64_uint8_t {
	return (*C.SignalType_FixedArray64_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray64) cConstFixedArray() cFixedArray64Compat {
	return cFixedArray64Compat(a.cFixedArray())
}

func (a *fixedArray65) cFixedArray() *C.SignalType_FixedArray65_uint8_t {
	return (*C.SignalType_FixedArray65_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray65) cConstFixedArray() cFixedArray65Compat {
	return cFixedArray65Compat(a.cFixedArray())
}

func (a *fixedArray97) cFixedArray() *C.SignalType_FixedArray97_uint8_t {
	return (*C.SignalType_FixedArray97_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray97) cConstFixedArray() cFixedArray97Compat {
	return cFixedArray97Compat(a.cFixedArray())
}

func (a *fixedArray129) cFixedArray() *C.SignalType_FixedArray129_uint8_t {
	return (*C.SignalType_FixedArray129_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray129) cConstFixedArray() cFixedArray129Compat {
	return cFixedArray129Compat(a.cFixedArray())
}

func (a *fixedArray153) cFixedArray() *C.SignalType_FixedArray153_uint8_t {
	return (*C.SignalType_FixedArray153_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray153) cConstFixedArray() cFixedArray153Compat {
	return cFixedArray153Compat(a.cFixedArray())
}

func (a *fixedArray177) cFixedArray() *C.SignalType_FixedArray177_uint8_t {
	return (*C.SignalType_FixedArray177_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray177) cConstFixedArray() cFixedArray177Compat {
	return cFixedArray177Compat(a.cFixedArray())
}

func (a *fixedArray289) cFixedArray() *C.SignalType_FixedArray289_uint8_t {
	return (*C.SignalType_FixedArray289_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray289) cConstFixedArray() cFixedArray289Compat {
	return cFixedArray289Compat(a.cFixedArray())
}

func (a *fixedArray329) cFixedArray() *C.SignalType_FixedArray329_uint8_t {
	return (*C.SignalType_FixedArray329_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray329) cConstFixedArray() cFixedArray329Compat {
	return cFixedArray329Compat(a.cFixedArray())
}

func (a *fixedArray409) cFixedArray() *C.SignalType_FixedArray409_uint8_t {
	return (*C.SignalType_FixedArray409_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray409) cConstFixedArray() cFixedArray409Compat {
	return cFixedArray409Compat(a.cFixedArray())
}

func (a *fixedArray473) cFixedArray() *C.SignalType_FixedArray473_uint8_t {
	return (*C.SignalType_FixedArray473_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray473) cConstFixedArray() cFixedArray473Compat {
	return cFixedArray473Compat(a.cFixedArray())
}

func (a *fixedArray497) cFixedArray() *C.SignalType_FixedArray497_uint8_t {
	return (*C.SignalType_FixedArray497_uint8_t)(unsafe.Pointer(a))
}

func (a *fixedArray497) cConstFixedArray() cFixedArray497Compat {
	return cFixedArray497Compat(a.cFixedArray())
}
