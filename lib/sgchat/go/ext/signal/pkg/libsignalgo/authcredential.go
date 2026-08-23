// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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
#include <stdlib.h>
*/
import "C"
import (
	"fmt"

	"github.com/google/uuid"
)

// type AuthCredential [181]byte
// type AuthCredentialResponse [361]byte
const AuthCredentialWithPniLength = 265

type AuthCredentialWithPni [AuthCredentialWithPniLength]byte
type AuthCredentialWithPniResponse [425]byte
type AuthCredentialPresentation []byte

func (ac *AuthCredentialWithPni) Slice() []byte {
	return (*ac)[:]
}

func ReceiveAuthCredentialWithPni(
	serverPublicParams *ServerPublicParams,
	aci uuid.UUID,
	pni uuid.UUID,
	redemptionTime uint64,
	authCredResponse AuthCredentialWithPniResponse,
) (*AuthCredentialWithPni, error) {
	var c_result C.SignalOwnedBuffer = C.SignalOwnedBuffer{}

	signalFfiError := C.signal_server_public_params_receive_auth_credential_with_pni_as_service_id(
		&c_result,
		C.SignalConstPointerServerPublicParams{serverPublicParams},
		NewACIServiceID(aci).cConstFixedArray(),
		NewPNIServiceID(pni).cConstFixedArray(),
		C.uint64_t(redemptionTime),
		BytesToBuffer(authCredResponse[:]),
	)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	resultBytes := CopySignalOwnedBufferToBytes(c_result)
	if len(resultBytes) != AuthCredentialWithPniLength {
		return nil, fmt.Errorf("invalid response length %d (expected %d)", len(resultBytes), AuthCredentialWithPniLength)
	}
	return (*AuthCredentialWithPni)(resultBytes), nil
}

func NewAuthCredentialWithPniResponse(b []byte) (*AuthCredentialWithPniResponse, error) {
	borrowedBuffer := BytesToBuffer(b)
	signalFfiError := C.signal_auth_credential_with_pni_response_check_valid_contents(borrowedBuffer)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	authCred := AuthCredentialWithPniResponse(b)
	return &authCred, nil
}

func CreateAuthCredentialWithPniPresentation(
	serverPublicParams *ServerPublicParams,
	randomness Randomness,
	groupSecretParams GroupSecretParams,
	authCredWithPni AuthCredentialWithPni,
) (*AuthCredentialPresentation, error) {
	var c_result C.SignalOwnedBuffer = C.SignalOwnedBuffer{}

	signalFfiError := C.signal_server_public_params_create_auth_credential_with_pni_presentation_deterministic(
		&c_result,
		C.SignalConstPointerServerPublicParams{serverPublicParams},
		randomness.cConstFixedArray(),
		groupSecretParams.cConstFixedArray(),
		BytesToBuffer(authCredWithPni[:]),
	)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	result := AuthCredentialPresentation(CopySignalOwnedBufferToBytes(c_result))
	return &result, nil
}
