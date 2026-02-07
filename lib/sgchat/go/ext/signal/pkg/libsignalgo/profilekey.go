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

const ProfileKeyLength = C.SignalPROFILE_KEY_LEN

type ProfileKey [ProfileKeyLength]byte
type ProfileKeyCommitment [C.SignalPROFILE_KEY_COMMITMENT_LEN]byte
type ProfileKeyVersion [C.SignalPROFILE_KEY_VERSION_ENCODED_LEN]byte
type AccessKey [C.SignalACCESS_KEY_LEN]byte

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
	for i := 0; i < C.SignalACCESS_KEY_LEN; i++ {
		result[i] = ak[i] ^ other[i]
	}
	return &result
}

func (ak *AccessKey) String() string {
	return base64.StdEncoding.EncodeToString(ak[:])
}

func (pk *ProfileKey) GetCommitment(u uuid.UUID) (*ProfileKeyCommitment, error) {
	c_result := [C.SignalPROFILE_KEY_COMMITMENT_LEN]C.uchar{}
	c_profileKey := (*[C.SignalPROFILE_KEY_LEN]C.uchar)(unsafe.Pointer(pk))
	c_uuid := NewACIServiceID(u).CFixedBytes()

	signalFfiError := C.signal_profile_key_get_commitment(
		&c_result,
		c_profileKey,
		c_uuid,
	)
	runtime.KeepAlive(pk)
	runtime.KeepAlive(u)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	var result ProfileKeyCommitment
	copy(result[:], C.GoBytes(unsafe.Pointer(&c_result), C.int(C.SignalPROFILE_KEY_COMMITMENT_LEN)))
	return &result, nil
}

func (pk *ProfileKey) GetProfileKeyVersion(u uuid.UUID) (*ProfileKeyVersion, error) {
	c_result := [C.SignalPROFILE_KEY_VERSION_ENCODED_LEN]C.uchar{}
	c_profileKey := (*[C.SignalPROFILE_KEY_LEN]C.uchar)(unsafe.Pointer(pk))
	c_uuid := NewACIServiceID(u).CFixedBytes()

	signalFfiError := C.signal_profile_key_get_profile_key_version(
		&c_result,
		c_profileKey,
		c_uuid,
	)
	runtime.KeepAlive(pk)
	runtime.KeepAlive(u)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	var result ProfileKeyVersion
	copy(result[:], C.GoBytes(unsafe.Pointer(&c_result), C.int(C.SignalPROFILE_KEY_VERSION_ENCODED_LEN)))
	return &result, nil
}

func (pk *ProfileKey) DeriveAccessKey() (*AccessKey, error) {
	c_result := [C.SignalACCESS_KEY_LEN]C.uchar{}
	c_profileKey := (*[C.SignalPROFILE_KEY_LEN]C.uchar)(unsafe.Pointer(pk))

	signalFfiError := C.signal_profile_key_derive_access_key(
		&c_result,
		c_profileKey,
	)
	runtime.KeepAlive(pk)

	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}

	var result AccessKey
	copy(result[:], C.GoBytes(unsafe.Pointer(&c_result), C.int(C.SignalACCESS_KEY_LEN)))
	return &result, nil
}

type ProfileKeyCredentialRequestContext [C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_CONTEXT_LEN]byte
type ProfileKeyCredentialRequest [C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_LEN]byte
type ProfileKeyCredentialResponse []byte
type ProfileKeyCredentialPresentation []byte
type ExpiringProfileKeyCredential [C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_LEN]byte
type ExpiringProfileKeyCredentialResponse [C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_RESPONSE_LEN]byte

func CreateProfileKeyCredentialRequestContext(serverPublicParams *ServerPublicParams, u uuid.UUID, profileKey ProfileKey) (*ProfileKeyCredentialRequestContext, error) {
	c_result := [C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_CONTEXT_LEN]C.uchar{}
	randBytes := [32]byte(random.Bytes(32))
	c_random := (*[32]C.uchar)(unsafe.Pointer(&randBytes[0]))
	c_profileKey := (*[C.SignalPROFILE_KEY_LEN]C.uchar)(unsafe.Pointer(&profileKey[0]))
	c_uuid := NewACIServiceID(u).CFixedBytes()

	signalFfiError := C.signal_server_public_params_create_profile_key_credential_request_context_deterministic(
		&c_result,
		C.SignalConstPointerServerPublicParams{serverPublicParams},
		c_random,
		c_uuid,
		c_profileKey,
	)
	runtime.KeepAlive(u)
	runtime.KeepAlive(profileKey)
	runtime.KeepAlive(randBytes)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	result := ProfileKeyCredentialRequestContext(C.GoBytes(unsafe.Pointer(&c_result), C.int(C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_CONTEXT_LEN)))
	return &result, nil
}

func (p *ProfileKeyCredentialRequestContext) ProfileKeyCredentialRequestContextGetRequest() (*ProfileKeyCredentialRequest, error) {
	c_result := [C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_LEN]C.uchar{}
	c_context := (*[C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_CONTEXT_LEN]C.uchar)(unsafe.Pointer(p))

	signalFfiError := C.signal_profile_key_credential_request_context_get_request(
		&c_result,
		c_context,
	)
	runtime.KeepAlive(p)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	result := ProfileKeyCredentialRequest(C.GoBytes(unsafe.Pointer(&c_result), C.int(C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_LEN)))
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
	c_credential := [C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_LEN]C.uchar{}
	signalFfiError := C.signal_server_public_params_receive_expiring_profile_key_credential(
		&c_credential,
		C.SignalConstPointerServerPublicParams{spp},
		(*[C.SignalPROFILE_KEY_CREDENTIAL_REQUEST_CONTEXT_LEN]C.uchar)(unsafe.Pointer(requestContext)),
		(*[C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_RESPONSE_LEN]C.uchar)(unsafe.Pointer(response)),
		(C.uint64_t)(currentTimeInSeconds),
	)
	runtime.KeepAlive(requestContext)
	runtime.KeepAlive(response)
	runtime.KeepAlive(currentTimeInSeconds)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	credential := ExpiringProfileKeyCredential{}
	copy(credential[:], C.GoBytes(unsafe.Pointer(&c_credential), C.int(C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_LEN)))
	return &credential, nil
}

func (a ProfileKeyCredentialPresentation) CheckValidContents() error {
	signalFfiError := C.signal_profile_key_credential_presentation_check_valid_contents(BytesToBuffer(a))
	runtime.KeepAlive(a)
	return wrapError(signalFfiError)
}

func (a ProfileKeyCredentialPresentation) UUIDCiphertext() (UUIDCiphertext, error) {
	out := [C.SignalUUID_CIPHERTEXT_LEN]C.uchar{}
	signalFfiError := C.signal_profile_key_credential_presentation_get_uuid_ciphertext(&out, BytesToBuffer(a))
	runtime.KeepAlive(a)
	if signalFfiError != nil {
		return UUIDCiphertext{}, wrapError(signalFfiError)
	}
	var result UUIDCiphertext
	copy(result[:], C.GoBytes(unsafe.Pointer(&out), C.int(C.SignalUUID_CIPHERTEXT_LEN)))
	return result, nil
}

func (a ProfileKeyCredentialPresentation) ProfileKeyCiphertext() (ProfileKeyCiphertext, error) {
	out := [C.SignalPROFILE_KEY_CIPHERTEXT_LEN]C.uchar{}
	signalFfiError := C.signal_profile_key_credential_presentation_get_profile_key_ciphertext(&out, BytesToBuffer(a))
	runtime.KeepAlive(a)
	if signalFfiError != nil {
		return ProfileKeyCiphertext{}, wrapError(signalFfiError)
	}
	var result ProfileKeyCiphertext
	copy(result[:], C.GoBytes(unsafe.Pointer(&out), C.int(C.SignalPROFILE_KEY_CIPHERTEXT_LEN)))
	return result, nil
}
