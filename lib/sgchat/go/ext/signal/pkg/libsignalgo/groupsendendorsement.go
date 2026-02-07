// mautrix-signal - A Matrix-signal puppeting bridge.
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
	"encoding/base64"
	"runtime"
	"time"
	"unsafe"
)

type GroupSendFullToken []byte

func (gsft GroupSendFullToken) String() string {
	return base64.StdEncoding.EncodeToString(gsft)
}

func (gsft GroupSendFullToken) CheckValidContents() error {
	signalFfiError := C.signal_group_send_full_token_check_valid_contents(
		BytesToBuffer(gsft),
	)
	runtime.KeepAlive(gsft)
	if signalFfiError != nil {
		return wrapError(signalFfiError)
	}
	return nil
}

func (gsft GroupSendFullToken) GetExpiration() (time.Time, error) {
	var expiration C.uint64_t
	signalFfiError := C.signal_group_send_full_token_get_expiration(
		&expiration,
		BytesToBuffer(gsft),
	)
	runtime.KeepAlive(gsft)
	if signalFfiError != nil {
		return time.Time{}, wrapError(signalFfiError)
	}
	return time.Unix(int64(expiration), 0), nil
}

type GroupSendToken []byte

func (gst GroupSendToken) CheckValidContents() error {
	signalFfiError := C.signal_group_send_token_check_valid_contents(
		BytesToBuffer(gst),
	)
	runtime.KeepAlive(gst)
	if signalFfiError != nil {
		return wrapError(signalFfiError)
	}
	return nil
}

func (gst GroupSendToken) ToFullToken(expiration time.Time) (GroupSendFullToken, error) {
	var fullToken C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_group_send_token_to_full_token(
		&fullToken,
		BytesToBuffer(gst),
		C.uint64_t(expiration.Unix()),
	)
	runtime.KeepAlive(gst)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(fullToken), nil
}

type GroupSendEndorsement []byte

func (gse GroupSendEndorsement) ToToken(groupSecretParams *GroupSecretParams) (GroupSendToken, error) {
	var token C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_group_send_endorsement_to_token(
		&token,
		BytesToBuffer(gse),
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(groupSecretParams)),
	)
	runtime.KeepAlive(gse)
	runtime.KeepAlive(groupSecretParams)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(token), nil
}

func (gse GroupSendEndorsement) ToFullToken(params *GroupSecretParams, expiration time.Time) (GroupSendFullToken, error) {
	token, err := gse.ToToken(params)
	if err != nil {
		return nil, err
	}
	return token.ToFullToken(expiration)
}

func (gse GroupSendEndorsement) CheckValidContents() error {
	signalFfiError := C.signal_group_send_endorsement_check_valid_contents(
		BytesToBuffer(gse),
	)
	runtime.KeepAlive(gse)
	if signalFfiError != nil {
		return wrapError(signalFfiError)
	}
	return nil
}

func (gse GroupSendEndorsement) Remove(other GroupSendEndorsement) (GroupSendEndorsement, error) {
	var result C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	signalFfiError := C.signal_group_send_endorsement_remove(
		&result,
		BytesToBuffer(gse),
		BytesToBuffer(other),
	)
	runtime.KeepAlive(gse)
	runtime.KeepAlive(other)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(result), nil
}

func GroupSendEndorsementCombine(endorsements ...GroupSendEndorsement) (GroupSendEndorsement, error) {
	var result C.SignalOwnedBuffer = C.SignalOwnedBuffer{}
	cEndorsements, unpin := ManyBytesToBuffer(endorsements)
	defer unpin()
	signalFfiError := C.signal_group_send_endorsement_combine(
		&result,
		cEndorsements,
	)
	runtime.KeepAlive(endorsements)
	if signalFfiError != nil {
		return nil, wrapError(signalFfiError)
	}
	return CopySignalOwnedBufferToBytes(result), nil
}

type GroupSendEndorsementsResponse []byte

func (gser GroupSendEndorsementsResponse) GetExpiration() (time.Time, error) {
	var expiration C.uint64_t
	signalFfiError := C.signal_group_send_endorsements_response_get_expiration(
		&expiration,
		BytesToBuffer(gser),
	)
	runtime.KeepAlive(gser)
	if signalFfiError != nil {
		return time.Time{}, wrapError(signalFfiError)
	}
	return time.Unix(int64(expiration), 0), nil
}

func (gser GroupSendEndorsementsResponse) CheckValidContents() error {
	signalFfiError := C.signal_group_send_endorsements_response_check_valid_contents(
		BytesToBuffer(gser),
	)
	runtime.KeepAlive(gser)
	if signalFfiError != nil {
		return wrapError(signalFfiError)
	}
	return nil
}

func (gser GroupSendEndorsementsResponse) ReceiveWithServiceIDs(
	groupMembers []ServiceID, localUser ServiceID, params *GroupSecretParams, spp *ServerPublicParams,
) (GroupSendEndorsement, map[ServiceID]GroupSendEndorsement, error) {
	var out C.SignalBytestringArray = C.SignalBytestringArray{}
	concatenatedMembers := make([]byte, len(groupMembers)*17)
	for i, member := range groupMembers {
		copy(concatenatedMembers[i*17:(i+1)*17], member.FixedBytes()[:])
	}
	signalFfiError := C.signal_group_send_endorsements_response_receive_and_combine_with_service_ids(
		&out,
		BytesToBuffer(gser),
		BytesToBuffer(concatenatedMembers),
		localUser.CFixedBytes(),
		C.uint64_t(time.Now().Unix()),
		(*[C.SignalGROUP_SECRET_PARAMS_LEN]C.uint8_t)(unsafe.Pointer(params)),
		C.SignalConstPointerServerPublicParams{spp},
	)
	runtime.KeepAlive(gser)
	runtime.KeepAlive(concatenatedMembers)
	runtime.KeepAlive(params)
	runtime.KeepAlive(spp)
	if signalFfiError != nil {
		return nil, nil, wrapError(signalFfiError)
	}
	endorsements := CopySignalBytestringArray[GroupSendEndorsement](out)
	memberEndorsements := make(map[ServiceID]GroupSendEndorsement, len(groupMembers))
	for i, member := range groupMembers {
		if len(endorsements) > i && len(endorsements[i]) > 0 {
			memberEndorsements[member] = endorsements[i]
		}
	}
	combined, err := GroupSendEndorsementCombine(endorsements...)
	if err != nil {
		return nil, memberEndorsements, err
	}
	return combined, memberEndorsements, nil
}
