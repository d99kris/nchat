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
	"encoding/base64"
	"errors"
	"runtime"
	"unsafe"

	"github.com/google/uuid"
	"go.mau.fi/util/random"
)

const ProfileKeyLength = 32
const AccessKeyLength = 16
const ProfileKeyVersionLength = 64

type ProfileKey [ProfileKeyLength]byte
type ProfileKeyCommitment = fixedArray97
type ProfileKeyVersion [ProfileKeyVersionLength]byte
type AccessKey [AccessKeyLength]byte

func (pk *ProfileKey) cFixedArray() *C.SignalType_FixedArray32_uint8_t {
	return (*C.SignalType_FixedArray32_uint8_t)(unsafe.Pointer(pk))
}

func (pk *ProfileKey) cConstFixedArray() cFixedArray32Compat {
	return cFixedArray32Compat(pk.cFixedArray())
}

func (pkv *ProfileKeyVersion) cFixedArray() *C.SignalType_FixedArray64_uint8_t {
	return (*C.SignalType_FixedArray64_uint8_t)(unsafe.Pointer(pkv))
}

func (pkv *ProfileKeyVersion) cConstFixedArray() cFixedArray64Compat {
	return cFixedArray64Compat(pkv.cFixedArray())
}

func (ak *AccessKey) cFixedArray() *C.SignalType_FixedArray16_uint8_t {
	return (*C.SignalType_FixedArray16_uint8_t)(unsafe.Pointer(ak))
}

func (ak *AccessKey) cConstFixedArray() cFixedArray16Compat {
	return cFixedArray16Compat(ak.cFixedArray())
}

func DeserializeProfileKey(bytes []byte) (*ProfileKey, error) {
	if len(bytes) == 0 {
		return nil, nil
	} else if len(bytes) != ProfileKeyLength {
		return nil, errors.New("invalid profile key length")
	}
	key := ProfileKey(bytes)
	return &key, nil
}

var blankProfileKey ProfileKey

func (pk *ProfileKey) IsEmpty() bool {
	return pk == nil || *pk == blankProfileKey
}

func (pv *ProfileKeyVersion) String() string {
	return string(pv[:])
}

func (pk *ProfileKey) Slice() []byte {
	if pk.IsEmpty() {
		return nil
	}
	return pk[:]
}

func (ak *AccessKey) Xor(other *AccessKey) *AccessKey {
	if ak == nil {
		return other
	} else if other == nil {
		return ak
	}
	var result AccessKey
	for i := 0; i < AccessKeyLength; i++ {
		result[i] = ak[i] ^ other[i]
	}
	return &result
}

func (ak *AccessKey) String() string {
	return base64.StdEncoding.EncodeToString(ak[:])
}

func (pk *ProfileKey) GetCommitment(u uuid.UUID) (*ProfileKeyCommitment, error) {
	var result ProfileKeyCommitment
	c_uuid := NewACIServiceID(u).cConstFixedArray()

	signalFfiError := C.signal_profile_key_get_commitment(
		result.cFixedArray(),
		pk.cConstFixedArray(),
		c_uuid,
	)
	runtime.KeepAlive(pk)
	runtime.KeepAlive(u)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	return &result, nil
}

func (pk *ProfileKey) GetProfileKeyVersion(u uuid.UUID) (*ProfileKeyVersion, error) {
	var result ProfileKeyVersion
	c_uuid := NewACIServiceID(u).cConstFixedArray()

	signalFfiError := C.signal_profile_key_get_profile_key_version(
		result.cFixedArray(),
		pk.cConstFixedArray(),
		c_uuid,
	)
	runtime.KeepAlive(pk)
	runtime.KeepAlive(u)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	return &result, nil
}

func (pk *ProfileKey) DeriveAccessKey() (*AccessKey, error) {
	var result AccessKey

	signalFfiError := C.signal_profile_key_derive_access_key(
		result.cFixedArray(),
		pk.cConstFixedArray(),
	)
	runtime.KeepAlive(pk)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	return &result, nil
}

type ProfileKeyCredentialRequestContext [473]byte
type ProfileKeyCredentialRequest = fixedArray329
type ProfileKeyCredentialResponse []byte
type ProfileKeyCredentialPresentation []byte
type ExpiringProfileKeyCredential = fixedArray153
type ExpiringProfileKeyCredentialResponse = fixedArray497

func (p *ProfileKeyCredentialRequestContext) cFixedArray() *C.SignalType_FixedArray473_uint8_t {
	return (*C.SignalType_FixedArray473_uint8_t)(unsafe.Pointer(p))
}

func (p *ProfileKeyCredentialRequestContext) cConstFixedArray() cFixedArray473Compat {
	return cFixedArray473Compat(p.cFixedArray())
}

func CreateProfileKeyCredentialRequestContext(serverPublicParams *ServerPublicParams, u uuid.UUID, profileKey ProfileKey) (*ProfileKeyCredentialRequestContext, error) {
	var result ProfileKeyCredentialRequestContext
	randBytes := Randomness(random.Bytes(RandomnessLength))
	c_uuid := NewACIServiceID(u).cConstFixedArray()

	signalFfiError := C.signal_server_public_params_create_profile_key_credential_request_context_deterministic(
		result.cFixedArray(),
		C.SignalConstPointerServerPublicParams{serverPublicParams},
		randBytes.cConstFixedArray(),
		c_uuid,
		profileKey.cConstFixedArray(),
	)
	runtime.KeepAlive(u)
	runtime.KeepAlive(profileKey)
	runtime.KeepAlive(randBytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &result, nil
}

func (p *ProfileKeyCredentialRequestContext) ProfileKeyCredentialRequestContextGetRequest() (*ProfileKeyCredentialRequest, error) {
	var result ProfileKeyCredentialRequest

	signalFfiError := C.signal_profile_key_credential_request_context_get_request(
		result.cFixedArray(),
		p.cConstFixedArray(),
	)
	runtime.KeepAlive(p)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &result, nil
}

func NewExpiringProfileKeyCredentialResponse(b []byte) (*ExpiringProfileKeyCredentialResponse, error) {
	borrowedBuffer := BytesToBuffer(b)
	signalFfiError := C.signal_expiring_profile_key_credential_response_check_valid_contents(borrowedBuffer)
	runtime.KeepAlive(b)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	response := ExpiringProfileKeyCredentialResponse(b)
	return &response, nil
}

func ReceiveExpiringProfileKeyCredential(spp *ServerPublicParams, requestContext *ProfileKeyCredentialRequestContext, response *ExpiringProfileKeyCredentialResponse, currentTimeInSeconds uint64) (*ExpiringProfileKeyCredential, error) {
	var credential ExpiringProfileKeyCredential
	signalFfiError := C.signal_server_public_params_receive_expiring_profile_key_credential(
		credential.cFixedArray(),
		C.SignalConstPointerServerPublicParams{spp},
		requestContext.cConstFixedArray(),
		response.cConstFixedArray(),
		(C.uint64_t)(currentTimeInSeconds),
	)
	runtime.KeepAlive(requestContext)
	runtime.KeepAlive(response)
	runtime.KeepAlive(currentTimeInSeconds)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &credential, nil
}

func (a ProfileKeyCredentialPresentation) CheckValidContents() error {
	signalFfiError := C.signal_profile_key_credential_presentation_check_valid_contents(BytesToBuffer(a))
	runtime.KeepAlive(a)
	return wrapError(signalFfiError)
}

func (a ProfileKeyCredentialPresentation) UUIDCiphertext() (UUIDCiphertext, error) {
	var out UUIDCiphertext
	signalFfiError := C.signal_profile_key_credential_presentation_get_uuid_ciphertext(out.cFixedArray(), BytesToBuffer(a))
	runtime.KeepAlive(a)
	if signalFfiError != nil {
		return UUIDCiphertext{}, wrapError(signalFfiError)
	}
	return out, nil
}

func (a ProfileKeyCredentialPresentation) ProfileKeyCiphertext() (ProfileKeyCiphertext, error) {
	var out ProfileKeyCiphertext
	signalFfiError := C.signal_profile_key_credential_presentation_get_profile_key_ciphertext(out.cFixedArray(), BytesToBuffer(a))
	runtime.KeepAlive(a)
	if signalFfiError != nil {
		return ProfileKeyCiphertext{}, wrapError(signalFfiError)
	}
	return out, nil
}
