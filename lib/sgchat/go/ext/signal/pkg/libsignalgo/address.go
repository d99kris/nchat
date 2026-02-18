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
)

type Address struct {
	nc  noCopy
	ptr *C.SignalProtocolAddress
}

func wrapAddress(ptr *C.SignalProtocolAddress) *Address {
	address := &Address{ptr: ptr}
	runtime.SetFinalizer(address, (*Address).Destroy)
	return address
}

func NewUUIDAddressFromString(uuidStr string, deviceID uint) (*Address, error) {
	serviceID, err := ServiceIDFromString(uuidStr)
	if err != nil {
		return nil, err
	}
	return serviceID.Address(deviceID)
}

func newAddress(name string, deviceID uint) (*Address, error) {
	var pa C.SignalMutPointerProtocolAddress
	signalFfiError := C.signal_address_new(&pa, C.CString(name), C.uint(deviceID))
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapAddress(pa.raw), nil
}

func (pa *Address) mutPtr() C.SignalMutPointerProtocolAddress {
	return C.SignalMutPointerProtocolAddress{pa.ptr}
}

func (pa *Address) constPtr() C.SignalConstPointerProtocolAddress {
	return C.SignalConstPointerProtocolAddress{pa.ptr}
}

func (pa *Address) Clone() (*Address, error) {
	var cloned C.SignalMutPointerProtocolAddress
	signalFfiError := C.signal_address_clone(&cloned, pa.constPtr())
	runtime.KeepAlive(pa)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return wrapAddress(cloned.raw), nil
}

func (pa *Address) Destroy() error {
	pa.CancelFinalizer()
	return wrapError(C.signal_address_destroy(pa.mutPtr()))
}

func (pa *Address) CancelFinalizer() {
	runtime.SetFinalizer(pa, nil)
}

func (pa *Address) Name() (string, error) {
	var name *C.char
	signalFfiError := C.signal_address_get_name(&name, pa.constPtr())
	runtime.KeepAlive(pa)
	if signalFfiError != nil {
		return "", wrapError(signalFfiError)
	}
	return CopyCStringToString(name), nil
}

func (pa *Address) NameServiceID() (ServiceID, error) {
	name, err := pa.Name()
	if err != nil {
		return ServiceID{}, err
	}
	return ServiceIDFromString(name)
}

func (pa *Address) DeviceID() (uint, error) {
	var deviceID C.uint
	signalFfiError := C.signal_address_get_device_id(&deviceID, pa.constPtr())
	runtime.KeepAlive(pa)
	if signalFfiError != nil {
		return 0, wrapError(signalFfiError)
	}
	return uint(deviceID), nil
}
