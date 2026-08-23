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

const RandomnessLength = 32

type Randomness = fixedArray32

func GenerateRandomness() Randomness {
	var randomness Randomness
	_, err := rand.Read(randomness[:])
	if err != nil {
		panic(err)
	}
	return randomness
}

const GroupMasterKeyLength = 32
const GroupIdentifierLength = 32
const GroupSecretParamsLength = 289

type GroupMasterKey [GroupMasterKeyLength]byte
type GroupSecretParams [GroupSecretParamsLength]byte
type GroupPublicParams = fixedArray97
type GroupIdentifier [GroupIdentifierLength]byte

func (gmk *GroupMasterKey) cFixedArray() *C.SignalType_FixedArray32_uint8_t {
	return (*C.SignalType_FixedArray32_uint8_t)(unsafe.Pointer(gmk))
}

func (gmk *GroupMasterKey) cConstFixedArray() cFixedArray32Compat {
	return cFixedArray32Compat(gmk.cFixedArray())
}

func (gsp *GroupSecretParams) cFixedArray() *C.SignalType_FixedArray289_uint8_t {
	return (*C.SignalType_FixedArray289_uint8_t)(unsafe.Pointer(gsp))
}

func (gsp *GroupSecretParams) cConstFixedArray() cFixedArray289Compat {
	return cFixedArray289Compat(gsp.cFixedArray())
}

func (gid *GroupIdentifier) cFixedArray() *C.SignalType_FixedArray32_uint8_t {
	return (*C.SignalType_FixedArray32_uint8_t)(unsafe.Pointer(gid))
}

func (gid *GroupIdentifier) cConstFixedArray() cFixedArray32Compat {
	return cFixedArray32Compat(gid.cFixedArray())
}

func (gid *GroupIdentifier) String() string {
	if gid == nil {
		return ""
	}
	return base64.StdEncoding.EncodeToString(gid[:])
}

type UUIDCiphertext = fixedArray65
type ProfileKeyCiphertext = fixedArray65

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
	var params GroupSecretParams
	signalFfiError := C.signal_group_secret_params_generate_deterministic(params.cFixedArray(), randomness.cConstFixedArray())
	runtime.KeepAlive(randomness)
	if signalFfiError != nil {
		return GroupSecretParams{}, wrapError(signalFfiError)
	}
	return params, nil
}

func DeriveGroupSecretParamsFromMasterKey(groupMasterKey GroupMasterKey) (GroupSecretParams, error) {
	var params GroupSecretParams
	signalFfiError := C.signal_group_secret_params_derive_from_master_key(params.cFixedArray(), groupMasterKey.cConstFixedArray())
	runtime.KeepAlive(groupMasterKey)
	if signalFfiError != nil {
		return GroupSecretParams{}, wrapError(signalFfiError)
	}
	return params, nil
}

func (gsp *GroupSecretParams) GetPublicParams() (*GroupPublicParams, error) {
	var publicParams GroupPublicParams
	signalFfiError := C.signal_group_secret_params_get_public_params(publicParams.cFixedArray(), gsp.cConstFixedArray())
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &publicParams, nil
}

func GetGroupIdentifier(groupPublicParams GroupPublicParams) (*GroupIdentifier, error) {
	var groupIdentifier GroupIdentifier
	signalFfiError := C.signal_group_public_params_get_group_identifier(groupIdentifier.cFixedArray(), groupPublicParams.cConstFixedArray())
	runtime.KeepAlive(groupPublicParams)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &groupIdentifier, nil
}

func (gsp *GroupSecretParams) DecryptBlobWithPadding(blob []byte) ([]byte, error) {
	var plaintext C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	borrowedBlob := BytesToBuffer(blob)
	signalFfiError := C.signal_group_secret_params_decrypt_blob_with_padding(
		&plaintext,
		gsp.cConstFixedArray(),
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
		gsp.cConstFixedArray(),
		randomness.cConstFixedArray(),
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
	var serviceIDBytes ServiceIDFixedBytes
	signalFfiError := C.signal_group_secret_params_decrypt_service_id(
		serviceIDBytes.cFixedArray(),
		gsp.cConstFixedArray(),
		ciphertextServiceID.cConstFixedArray(),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(ciphertextServiceID)
	if signalFfiError != nil {
		return EmptyServiceID, wrapError(signalFfiError)
	}

	serviceID := ServiceIDFromCFixedBytes(serviceIDBytes.cFixedArray())
	return serviceID, nil
}

func (gsp *GroupSecretParams) EncryptServiceID(serviceID ServiceID) (*UUIDCiphertext, error) {
	var cipherTextServiceID UUIDCiphertext
	signalFfiError := C.signal_group_secret_params_encrypt_service_id(
		cipherTextServiceID.cFixedArray(),
		gsp.cConstFixedArray(),
		serviceID.cConstFixedArray(),
	)
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &cipherTextServiceID, nil
}

func (gsp *GroupSecretParams) DecryptProfileKey(ciphertextProfileKey ProfileKeyCiphertext, u uuid.UUID) (*ProfileKey, error) {
	var profileKey ProfileKey
	signalFfiError := C.signal_group_secret_params_decrypt_profile_key(
		profileKey.cFixedArray(),
		gsp.cConstFixedArray(),
		ciphertextProfileKey.cConstFixedArray(),
		NewACIServiceID(u).cConstFixedArray(),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(ciphertextProfileKey)
	runtime.KeepAlive(u)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &profileKey, nil
}

func (gsp *GroupSecretParams) EncryptProfileKey(profileKey ProfileKey, u uuid.UUID) (*ProfileKeyCiphertext, error) {
	var ciphertextProfileKey ProfileKeyCiphertext
	signalFfiError := C.signal_group_secret_params_encrypt_profile_key(
		ciphertextProfileKey.cFixedArray(),
		gsp.cConstFixedArray(),
		profileKey.cConstFixedArray(),
		NewACIServiceID(u).cConstFixedArray(),
	)
	runtime.KeepAlive(gsp)
	runtime.KeepAlive(profileKey)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &ciphertextProfileKey, nil
}

func (gsp *GroupSecretParams) CreateExpiringProfileKeyCredentialPresentation(spp *ServerPublicParams, credential ExpiringProfileKeyCredential) (*ProfileKeyCredentialPresentation, error) {
	var out C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	randomness := GenerateRandomness()
	signalFfiError := C.signal_server_public_params_create_expiring_profile_key_credential_presentation_deterministic(
		&out,
		C.SignalConstPointerServerPublicParams{spp},
		randomness.cConstFixedArray(),
		gsp.cConstFixedArray(),
		credential.cConstFixedArray(),
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
	var masterKey GroupMasterKey
	signalFfiError := C.signal_group_secret_params_get_master_key(
		masterKey.cFixedArray(),
		gsp.cConstFixedArray(),
	)
	runtime.KeepAlive(gsp)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return &masterKey, nil
}
