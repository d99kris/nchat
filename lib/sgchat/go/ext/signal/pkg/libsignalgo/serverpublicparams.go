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
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"runtime"
	"unsafe"
)

type ServerPublicParams = C.SignalServerPublicParams
type NotarySignature [C.SignalSIGNATURE_LEN]byte

func DeserializeServerPublicParams(params []byte) (*ServerPublicParams, error) {
	if len(params) != C.SignalSERVER_PUBLIC_PARAMS_LEN {
		return nil, fmt.Errorf("invalid server public params length: %d (expected %d)", len(params), int(C.SignalSERVER_PUBLIC_PARAMS_LEN))
	}
	var out C.SignalMutPointerServerPublicParams
	signalFfiError := C.signal_server_public_params_deserialize(&out, BytesToBuffer(params[:]))
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return out.raw, nil
}

func ServerPublicParamsVerifySignature(
	serverPublicParams *ServerPublicParams,
	messageBytes []byte,
	NotarySignature NotarySignature,
) error {
	c_notarySignature := (*[C.SignalSIGNATURE_LEN]C.uint8_t)(unsafe.Pointer(&NotarySignature[0]))
	signalFfiError := C.signal_server_public_params_verify_signature(
		C.SignalConstPointerServerPublicParams{serverPublicParams},
		BytesToBuffer(messageBytes),
		c_notarySignature,
	)
	runtime.KeepAlive(messageBytes)
	return wrapError(signalFfiError)
}
