// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2026 Tulir Asokan
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

package connector

import (
	"errors"
	"fmt"
	"net/http"

	"google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"
	"maunium.net/go/mautrix"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/status"
	"maunium.net/go/mautrix/event"

	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

func init() {
	status.BridgeStateHumanErrors.Update(status.BridgeStateErrorMap{
		"signal-logged-out":        "You have been logged out of Signal, please reconnect",
		"signal-not-logged-in":     "You're not logged into Signal",
		"signal-disconnected":      "Disconnected from Signal, reconnecting",
		"signal-connection-error":  "Connection to Signal failed, reconnecting",
		"signal-server-error":      "Signal server error, reconnecting",
		"signal-rate-limited":      "Rate limited by Signal, reconnecting",
		"signal-connect-failed":    "Failed to connect to Signal, retrying",
		"signal-connection-failed": "Connection to Signal failed",
	})
}

var (
	errEditOthersMessage = bridgev2.WrapErrorInStatus(errors.New("you can only edit your own messages")).
				WithErrorAsMessage().
				WithErrorReason(event.MessageStatusNoPermission).
				WithStatus(event.MessageStatusFail).
				WithIsCertain(true).
				WithSendNotice(true)
	errDeleteOthersMessage = bridgev2.WrapErrorInStatus(errors.New("you can only delete your own messages")).
				WithErrorAsMessage().
				WithErrorReason(event.MessageStatusNoPermission).
				WithStatus(event.MessageStatusFail).
				WithIsCertain(true).
				WithSendNotice(true)
	errInviteToDM = bridgev2.WrapErrorInStatus(errors.New("can't invite more users to Signal DMs")).
			WithErrorAsMessage().
			WithErrorReason(event.MessageStatusUnsupported).
			WithStatus(event.MessageStatusFail).
			WithIsCertain(true).
			WithSendNotice(true)
	errUnsupportedDisappearingType = bridgev2.WrapErrorInStatus(errors.New("only disappearing timers that start after reading are supported on Signal")).
					WithErrorAsMessage().
					WithErrorReason(event.MessageStatusUnsupported).
					WithStatus(event.MessageStatusFail).
					WithIsCertain(true).
					WithSendNotice(true)
)

func wrapConversionError(err error) error {
	if err == nil || errors.As(err, &bridgev2.MessageStatus{}) {
		return err
	}
	return bridgev2.WrapErrorInStatus(err).
		WithMessage("failed to convert message for Signal").
		WithIsCertain(true).
		WithSendNotice(true)
}

func wrapSendError(err error) error {
	if err == nil || errors.As(err, &bridgev2.MessageStatus{}) {
		return err
	}
	switch {
	case errors.Is(err, signalmeow.ErrUnregisteredUser):
		return bridgev2.WrapErrorInStatus(err).
			WithMessage("recipient isn't registered on Signal").
			WithErrorReason(event.MessageStatusNetworkError).
			WithStatus(event.MessageStatusFail).
			WithIsCertain(true).
			WithSendNotice(true)
	case errors.Is(err, signalmeow.ErrRateLimited), errors.Is(err, signalmeow.RateLimitError):
		return bridgev2.WrapErrorInStatus(err).
			WithMessage("rate limited by Signal, try again later").
			WithErrorReason(event.MessageStatusNetworkError).
			WithIsCertain(true).
			WithSendNotice(true)
	case errors.Is(err, signalmeow.AuthorizationFailedError):
		return bridgev2.WrapErrorInStatus(err).
			WithMessage("you don't have permission to do that in this Signal group").
			WithErrorReason(event.MessageStatusNoPermission).
			WithStatus(event.MessageStatusFail).
			WithIsCertain(true).
			WithSendNotice(true)
	case errors.Is(err, signalmeow.GroupPatchNotAcceptedError):
		return bridgev2.WrapErrorInStatus(err).
			WithMessage("Signal rejected the group change").
			WithErrorReason(event.MessageStatusNetworkError).
			WithStatus(event.MessageStatusFail).
			WithIsCertain(true).
			WithSendNotice(true)
	default:
		return bridgev2.WrapErrorInStatus(err).
			WithMessage("failed to send to Signal").
			WithSendNotice(true)
	}
}

func wrapRespError(err error) error {
	var mapped bridgev2.RespError
	var cdsRateLimit signalmeow.ContactDiscoveryRateLimitError
	switch {
	case errors.As(err, &cdsRateLimit), errors.Is(err, signalmeow.RateLimitError), errors.Is(err, signalmeow.ErrRateLimited),
		grpcstatus.Code(err) == codes.ResourceExhausted:
		mapped = bridgev2.RespError(mautrix.MLimitExceeded.WithMessage("Rate limited by Signal, please try again later").WithStatus(http.StatusBadRequest))
	case errors.Is(err, signalmeow.AuthorizationFailedError):
		mapped = bridgev2.RespError(mautrix.MForbidden.WithMessage("Signal denied the request"))
	default:
		mapped = bridgev2.RespError(mautrix.MUnknown.WithMessage("Request to Signal failed"))
	}
	return fmt.Errorf("%w: %w", mapped, err)
}

func wrapDirectMediaError(err error) error {
	var statusErr web.HTTPStatusError
	if !errors.As(err, &statusErr) {
		return err
	}
	switch statusErr.StatusCode {
	case http.StatusNotFound, http.StatusGone:
		return mautrix.MNotFound.WithMessage("Media not found on Signal")
	case http.StatusUnauthorized, http.StatusForbidden:
		return mautrix.MForbidden.WithMessage("Signal denied access to the media")
	case http.StatusTooManyRequests:
		return mautrix.MLimitExceeded.WithMessage("Rate limited by Signal, please try again later").WithCanRetry(true)
	default:
		return mautrix.MUnknown.WithMessage("Unexpected response from Signal").WithStatus(http.StatusBadGateway).WithCanRetry(true)
	}
}

func connectionErrorState(err error) status.BridgeState {
	state := status.BridgeState{
		StateEvent: status.StateTransientDisconnect,
		Error:      "signal-connection-error",
		Info:       map[string]any{"go_error": err.Error()},
	}
	var statusErr web.HTTPStatusError
	if errors.As(err, &statusErr) {
		if statusErr.StatusCode == http.StatusTooManyRequests {
			state.Error = "signal-rate-limited"
		} else if statusErr.StatusCode >= 500 {
			state.Error = "signal-server-error"
		}
	}
	return state
}

func (s *SignalClient) sendLoggedOutState(err error) {
	state := status.BridgeState{
		StateEvent: status.StateBadCredentials,
		Error:      "signal-logged-out",
		UserAction: status.UserActionRelogin,
	}
	if err != nil {
		state.Info = map[string]any{"go_error": err.Error()}
	}
	s.UserLogin.BridgeState.Send(state)
}
