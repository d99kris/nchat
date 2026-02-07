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
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"runtime"
	"unsafe"

	"github.com/google/uuid"
)

type Randomness [C.SignalRANDOMNESS_LEN]byte

func GenerateRandomness() Randomness {
	var randomness Randomness
	_, err := rand.Read(randomness[:])
	if err != nil {
		panic(err)
	}
	return randomness
}

const GroupMasterKeyLength = C.SignalGROUP_MASTER_KEY_LEN
const GroupIdentifierLength = C.SignalGROUP_IDENTIFIER_LEN

type GroupMasterKey [GroupMasterKeyLength]byte
type GroupSecretParams [C.SignalGROUP_SECRET_PARAMS_LEN]byte
type GroupPublicParams [C.SignalGROUP_PUBLIC_PARAMS_LEN]byte
type GroupIdentifier [GroupIdentifierLength]byte

func (gid *GroupIdentifier) String() string {
	if gid == nil {
		return ""
	}
	return base64.StdEncoding.EncodeToString(gid[:])
}

type UUIDCiphertext [C.SignalUUID_CIPHERTEXT_LEN]byte
type ProfileKeyCiphertext [C.SignalPROFILE_KEY_CIPHERTEXT_LEN]byte

func GenerateGroupSecretParams() (GroupSecretParams, error) {
	return GenerateGroupSecretParamsWithRandomness(GenerateRandomness())
}

func (gmk GroupMasterKey) GroupIdentifier() (*GroupIdentifier, error) {
	if groupSecretParams, err := DeriveGroupSecretParamsFromMasterKey(gmk); err != nil {
		return nil, fmt.Errorf("DeriveGroupSecretParamsFromMasterKey error: %w", err)
	} else if groupPublicParams, err := groupSecretParams.GetPublicParams(); err != nil {
		return nil, fmt.Errorf("GetPublicParams error: %w", err)
	} else if groupIdentifier, err := GetGroupIdentifier(*groupPublicParams); err != nil {
		return nil, fmt.Errorf("GetGroupIdentifier error: %w", err)
	} else {
		return groupIdentifier, nil
	}
}

func (gmk GroupMasterKey) SecretParams() (GroupSecretParams, error) {
	return DeriveGroupSecretParamsFromMasterKey(gmk)
}

func GenerateGroupSecretParamsWithRandomness(randomness Randomness) (GroupSecretParams, error) {
	var params [C.SignalGROUP_SECRET_PARAMS_LEN]C.uchar
	signalFfiError := C.signal_group_secret_params_generate_deterministic(&params, (*[C.SignalRANDOMNESS_LEN]C.uint8_t)(unsafe.Pointer(&randomness)))
	runtime.KeepAlive(randomness)
	if signalFfiError != nil {
		return GroupSecretParams{}, wrapError(signalFfiError)
	}
	var groupSecretParams GroupSecretParams
	copy(groupSecretParams[:], C.GoBytes(unsafe.Pointer(&params), C.int(C.SignalGROUP_SECRET_PARAMS_LEN)))
	return groupSecretParams, nil
}

func DeriveGroupSecretParamsFromMasterKey(groupMasterKey GroupMasterKey) (GroupSecretParams, error) {
	var params [C.SignalGROUP_SECRET_PARAMS_LEN]C.uchar
	signalFfiError := C.signal_group_secret_params_derive_from_master_key(&params, (*[C.SignalGROUP_MASTER_KEY_LEN]C.uint8_t)(unsafe.Pointer(&groupMasterKey)))
	runtime.KeepAlive(groupMasterKey)
	if signalFfiError != nil {
		return GroupSecretParams{}, wrapError(signalFfiError)
	}
	var groupSecretParams GroupSecretParams
	copy(groupSecretParams[:], C.GoBytes(unsafe.Pointer(&params), C.int(C.SignalGROUP_SECRET_PARAMS_LEN)))
	return groupSecretParams, nil
}

func (gsp *GroupSecretParams) GetPublicParams() (*GroupPublicParams, error) {
	var publicParams [C.SignalGROUP_PUBLIC_PARAMS_LEN]C.uchar
	signalFfiError := C.signal_group_secret_params_get_public_params(&publicParams, (*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)))
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var groupPublicParams GroupPublicParams
	copy(groupPublicParams[:], C.GoBytes(unsafe.Pointer(&publicParams), C.int(C.SignalGROUP_PUBLIC_PARAMS_LEN)))
	return &groupPublicParams, nil
}

func GetGroupIdentifier(groupPublicParams GroupPublicParams) (*GroupIdentifier, error) {
	var groupIdentifier [C.SignalGROUP_IDENTIFIER_LEN]C.uchar
	signalFfiError := C.signal_group_public_params_get_group_identifier(&groupIdentifier, (*[C.SignalGROUP_PUBLIC_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(&groupPublicParams)))
	runtime.KeepAlive(groupPublicParams)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var result GroupIdentifier
	copy(result[:], C.GoBytes(unsafe.Pointer(&groupIdentifier), C.int(C.SignalGROUP_IDENTIFIER_LEN)))
	return &result, nil
}

func (gsp *GroupSecretParams) DecryptBlobWithPadding(blob []byte) ([]byte, error) {
	var plaintext C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	borrowedBlob := BytesToBuffer(blob)
	signalFfiError := C.signal_group_secret_params_decrypt_blob_with_padding(
		&plaintext,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		borrowedBlob,
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(blob)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(plaintext), nil
}

func (gsp *GroupSecretParams) EncryptBlobWithPaddingDeterministic(randomness Randomness, plaintext []byte, padding_len uint32) ([]byte, error) {
	var ciphertext C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	borrowedPlaintext := BytesToBuffer(plaintext)
	signalFfiError := C.signal_group_secret_params_encrypt_blob_with_padding_deterministic(
		&ciphertext,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		(*[C.SignalRANDOMNESS_LEN]C.uint8_t)(unsafe.Pointer(&randomness)),
		borrowedPlaintext,
		(C.uint32_t)(padding_len),
	)
	runtime.KeepAlive(randomness)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(plaintext)
	runtime.KeepAlive(padding_len)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(ciphertext), nil
}

func (gsp *GroupSecretParams) DecryptServiceID(ciphertextServiceID UUIDCiphertext) (ServiceID, error) {
	u := C.SignalServiceIdFixedWidthBinaryBytes{}
	signalFfiError := C.signal_group_secret_params_decrypt_service_id(
		&u,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		(*[C.SignalUUID_CIPHERTEXT_LEN]C.uint8_t)(unsafe.Pointer(&ciphertextServiceID)),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(ciphertextServiceID)
	if signalFfiError != nil {
		return EmptyServiceID, wrapError(signalFfiError)
	}

	serviceID := ServiceIDFromCFixedBytes(&u)
	return serviceID, nil
}

func (gsp *GroupSecretParams) EncryptServiceID(serviceID ServiceID) (*UUIDCiphertext, error) {
	var cipherTextServiceID [C.SignalUUID_CIPHERTEXT_LEN]C.uchar
	signalFfiError := C.signal_group_secret_params_encrypt_service_id(
		&cipherTextServiceID,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		serviceID.CFixedBytes(),
	)
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var result UUIDCiphertext
	copy(result[:], C.GoBytes(unsafe.Pointer(&cipherTextServiceID), C.int(C.SignalUUID_CIPHERTEXT_LEN)))
	return &result, nil
}

func (gsp *GroupSecretParams) DecryptProfileKey(ciphertextProfileKey ProfileKeyCiphertext, u uuid.UUID) (*ProfileKey, error) {
	profileKey := [C.SignalPROFILE_KEY_LEN]C.uchar{}
	signalFfiError := C.signal_group_secret_params_decrypt_profile_key(
		&profileKey,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		(*[C.SignalPROFILE_KEY_CIPHERTEXT_LEN]C.uint8_t)(unsafe.Pointer(&ciphertextProfileKey)),
		NewACIServiceID(u).CFixedBytes(),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(ciphertextProfileKey)
	runtime.KeepAlive(u)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var result ProfileKey
	copy(result[:], C.GoBytes(unsafe.Pointer(&profileKey), C.int(C.SignalPROFILE_KEY_LEN)))
	return &result, nil
}

func (gsp *GroupSecretParams) EncryptProfileKey(profileKey ProfileKey, u uuid.UUID) (*ProfileKeyCiphertext, error) {
	ciphertextProfileKey := [C.SignalPROFILE_KEY_CIPHERTEXT_LEN]C.uchar{}
	signalFfiError := C.signal_group_secret_params_encrypt_profile_key(
		&ciphertextProfileKey,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(gsp)),
		(*[C.SignalPROFILE_KEY_LEN]C.uint8_t)(unsafe.Pointer(&profileKey)),
		NewACIServiceID(u).CFixedBytes(),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(profileKey)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var result ProfileKeyCiphertext
	copy(result[:], C.GoBytes(unsafe.Pointer(&ciphertextProfileKey), C.int(C.SignalPROFILE_KEY_CIPHERTEXT_LEN)))
	return &result, nil
}

func (gsp *GroupSecretParams) CreateExpiringProfileKeyCredentialPresentation(spp *ServerPublicParams, credential ExpiringProfileKeyCredential) (*ProfileKeyCredentialPresentation, error) {
	var out C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	randomness := GenerateRandomness()
	signalFfiError := C.signal_server_public_params_create_expiring_profile_key_credential_presentation_deterministic(
		&out,
		C.SignalConstPointerServerPublicParams{spp},
		(*[C.SignalRANDOMNESS_LEN]C.uint8_t)(unsafe.Pointer(&randomness)),
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uchar)(unsafe.Pointer(gsp)),
		(*[C.SignalEXPIRING_PROFILE_KEY_CREDENTIAL_LEN]C.uchar)(unsafe.Pointer(&credential)),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(credential)
	runtime.KeepAlive(randomness)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	presentationBytes := CopySignalOwnedBufferToBytes(out)
	presentation := ProfileKeyCredentialPresentation(presentationBytes)
	return &presentation, nil
}

func (gsp *GroupSecretParams) GetMasterKey() (*GroupMasterKey, error) {
	masterKeyBytes := [C.SignalGROUP_MASTER_KEY_LEN]C.uchar{}
	signalFfiError := C.signal_group_secret_params_get_master_key(
		&masterKeyBytes,
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uchar)(unsafe.Pointer(gsp)),
	)
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	var groupMasterKey GroupMasterKey
	copy(groupMasterKey[:], C.GoBytes(unsafe.Pointer(&masterKeyBytes), C.int(C.SignalGROUP_MASTER_KEY_LEN)))
	return &groupMasterKey, nil
}
