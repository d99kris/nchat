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
)

type DeviceTransferKey struct {
	privateKey []byte
}

func GenerateDeviceTransferKey() (*DeviceTransferKey, error) {
	var resp C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_device_transfer_generate_private_key(&resp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &DeviceTransferKey{privateKey: CopySignalOwnedBufferToBytes(resp)}, nil
}

func (dtk *DeviceTransferKey) PrivateKeyMaterial() []byte {
	return dtk.privateKey
}

func (dtk *DeviceTransferKey) GenerateCertificate(name string, days int) ([]byte, error) {
	var resp C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_device_transfer_generate_certificate(&resp, BytesToBuffer(dtk.privateKey), C.CString(name), C.uint32_t(days))
	runtime.KeepAlive(dtk)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(resp), nil
}
