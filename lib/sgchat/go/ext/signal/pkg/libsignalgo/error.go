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
	"fmt"
)

type ErrorCode int

func (e ErrorCode) Error() string {
	return fmt.Sprintf("libsignalgo.ErrorCode(%d)", int(e))
}

const (
	ErrorCodeUnknownError                                ErrorCode = 1
	ErrorCodeInvalidState                                ErrorCode = 2
	ErrorCodeInternalError                               ErrorCode = 3
	ErrorCodeNullParameter                               ErrorCode = 4
	ErrorCodeInvalidArgument                             ErrorCode = 5
	ErrorCodeInvalidType                                 ErrorCode = 6
	ErrorCodeInvalidUtf8String                           ErrorCode = 7
	ErrorCodeCancelled                                   ErrorCode = 8
	ErrorCodeProtobufError                               ErrorCode = 10
	ErrorCodeLegacyCiphertextVersion                     ErrorCode = 21
	ErrorCodeUnknownCiphertextVersion                    ErrorCode = 22
	ErrorCodeUnrecognizedMessageVersion                  ErrorCode = 23
	ErrorCodeInvalidMessage                              ErrorCode = 30
	ErrorCodeSealedSenderSelfSend                        ErrorCode = 31
	ErrorCodeInvalidKey                                  ErrorCode = 40
	ErrorCodeInvalidSignature                            ErrorCode = 41
	ErrorCodeInvalidAttestationData                      ErrorCode = 42
	ErrorCodeFingerprintVersionMismatch                  ErrorCode = 51
	ErrorCodeFingerprintParsingError                     ErrorCode = 52
	ErrorCodeUntrustedIdentity                           ErrorCode = 60
	ErrorCodeInvalidKeyIdentifier                        ErrorCode = 70
	ErrorCodeSessionNotFound                             ErrorCode = 80
	ErrorCodeInvalidRegistrationId                       ErrorCode = 81
	ErrorCodeInvalidSession                              ErrorCode = 82
	ErrorCodeInvalidSenderKeySession                     ErrorCode = 83
	ErrorCodeInvalidProtocolAddress                      ErrorCode = 84
	ErrorCodeDuplicatedMessage                           ErrorCode = 90
	ErrorCodeCallbackError                               ErrorCode = 100
	ErrorCodeVerificationFailure                         ErrorCode = 110
	ErrorCodeUsernameCannotBeEmpty                       ErrorCode = 120
	ErrorCodeUsernameCannotStartWithDigit                ErrorCode = 121
	ErrorCodeUsernameMissingSeparator                    ErrorCode = 122
	ErrorCodeUsernameBadDiscriminatorCharacter           ErrorCode = 123
	ErrorCodeUsernameBadNicknameCharacter                ErrorCode = 124
	ErrorCodeUsernameTooShort                            ErrorCode = 125
	ErrorCodeUsernameTooLong                             ErrorCode = 126
	ErrorCodeUsernameLinkInvalidEntropyDataLength        ErrorCode = 127
	ErrorCodeUsernameLinkInvalid                         ErrorCode = 128
	ErrorCodeUsernameDiscriminatorCannotBeEmpty          ErrorCode = 130
	ErrorCodeUsernameDiscriminatorCannotBeZero           ErrorCode = 131
	ErrorCodeUsernameDiscriminatorCannotBeSingleDigit    ErrorCode = 132
	ErrorCodeUsernameDiscriminatorCannotHaveLeadingZeros ErrorCode = 133
	ErrorCodeUsernameDiscriminatorTooLarge               ErrorCode = 134
	ErrorCodeIoError                                     ErrorCode = 140
	ErrorCodeInvalidMediaInput                           ErrorCode = 141
	ErrorCodeUnsupportedMediaInput                       ErrorCode = 142
	ErrorCodeConnectionTimedOut                          ErrorCode = 143
	ErrorCodeNetworkProtocol                             ErrorCode = 144
	ErrorCodeRateLimited                                 ErrorCode = 145
	ErrorCodeWebSocket                                   ErrorCode = 146
	ErrorCodeCdsiInvalidToken                            ErrorCode = 147
	ErrorCodeConnectionFailed                            ErrorCode = 148
	ErrorCodeChatServiceInactive                         ErrorCode = 149
	ErrorCodeRequestTimedOut                             ErrorCode = 150
	ErrorCodeRateLimitChallenge                          ErrorCode = 151
	ErrorCodePossibleCaptiveNetwork                      ErrorCode = 152
	ErrorCodeSvrDataMissing                              ErrorCode = 160
	ErrorCodeSvrRestoreFailed                            ErrorCode = 161
	ErrorCodeSvrRotationMachineTooManySteps              ErrorCode = 162
	ErrorCodeSvrRequestFailed                            ErrorCode = 163
	ErrorCodeAppExpired                                  ErrorCode = 170
	ErrorCodeDeviceDeregistered                          ErrorCode = 171
	ErrorCodeConnectionInvalidated                       ErrorCode = 172
	ErrorCodeConnectedElsewhere                          ErrorCode = 173
	ErrorCodeBackupValidation                            ErrorCode = 180
	ErrorCodeRegistrationInvalidSessionId                ErrorCode = 190
	ErrorCodeRegistrationUnknown                         ErrorCode = 192
	ErrorCodeRegistrationSessionNotFound                 ErrorCode = 193
	ErrorCodeRegistrationNotReadyForVerification         ErrorCode = 194
	ErrorCodeRegistrationSendVerificationCodeFailed      ErrorCode = 195
	ErrorCodeRegistrationCodeNotDeliverable              ErrorCode = 196
	ErrorCodeRegistrationSessionUpdateRejected           ErrorCode = 197
	ErrorCodeRegistrationCredentialsCouldNotBeParsed     ErrorCode = 198
	ErrorCodeRegistrationDeviceTransferPossible          ErrorCode = 199
	ErrorCodeRegistrationRecoveryVerificationFailed      ErrorCode = 200
	ErrorCodeRegistrationLock                            ErrorCode = 201
	ErrorCodeKeyTransparencyError                        ErrorCode = 210
	ErrorCodeKeyTransparencyVerificationFailed           ErrorCode = 211
	ErrorCodeRequestUnauthorized                         ErrorCode = 220
	ErrorCodeMismatchedDevices                           ErrorCode = 221
)

type SignalError struct {
	Code    ErrorCode
	Message string
}

func (e *SignalError) Error() string {
	return fmt.Sprintf("%d: %s", e.Code, e.Message)
}

func (e *SignalError) Unwrap() error {
	return e.Code
}

func (ctx *CallbackContext) wrapError(signalError *C.SignalFfiError) error {
	if signalError == nil {
		return nil
	}

	defer C.signal_error_free(signalError)

	errorType := C.signal_error_get_type(signalError)
	if ErrorCode(errorType) == ErrorCodeCallbackError {
		return ctx.Error
	} else {
		return wrapSignalError(signalError, errorType)
	}
}

func wrapError(signalError *C.SignalFfiError) error {
	if signalError == nil {
		return nil
	}

	defer C.signal_error_free(signalError)

	return wrapSignalError(signalError, C.signal_error_get_type(signalError))
}

func wrapSignalError(signalError *C.SignalFfiError, errorType C.uint32_t) error {
	var messageBytes *C.char
	getMessageError := C.signal_error_get_message(&messageBytes, signalError)
	if getMessageError != nil {
		// Ignore any errors from this, it will just end up being an empty string.
		C.signal_error_free(getMessageError)
	}
	return &SignalError{Code: ErrorCode(errorType), Message: CopyCStringToString(messageBytes)}
}
