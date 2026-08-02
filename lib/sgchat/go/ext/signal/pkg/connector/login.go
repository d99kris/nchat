// mautrix-signal - A Matrix-Signal puppeting bridge.
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

package connector

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"time"

	"github.com/coder/websocket"
	"github.com/google/uuid"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/database"
	"maunium.net/go/mautrix/bridgev2/status"

	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
)

func (s *SignalConnector) GetLoginFlows() []bridgev2.LoginFlow {
	return []bridgev2.LoginFlow{{
		Name:        "QR",
		Description: "Scan a QR code to pair the bridge to your Signal app",
		ID:          "qr",
	}}
}

func (s *SignalConnector) CreateLogin(ctx context.Context, user *bridgev2.User, flowID string) (bridgev2.LoginProcess, error) {
	if flowID != "qr" {
		return nil, fmt.Errorf("invalid login flow ID")
	}
	return &QRLogin{User: user, Main: s}, nil
}

type QRLogin struct {
	User       *bridgev2.User
	Main       *SignalConnector
	cancelChan context.CancelFunc
	ProvChan   chan signalmeow.ProvisioningResponse
	newQRCount int
}

var _ bridgev2.LoginProcessDisplayAndWait = (*QRLogin)(nil)

func (qr *QRLogin) Cancel() {
	qr.cancelChan()
	go func() {
		for range qr.ProvChan {
		}
	}()
}

const (
	LoginStepQR       = "fi.mau.signal.login.qr"
	LoginStepProcess  = "fi.mau.signal.login.processing"
	LoginStepComplete = "fi.mau.signal.login.complete"
)

const (
	qrRefreshInterval = 45 * time.Second
	maxQRRefreshes    = 20
)

var (
	ErrLoginTimedOut = bridgev2.RespError{
		ErrCode:    "FI.MAU.BRIDGE.LOGIN_TIMED_OUT",
		Err:        "The QR code wasn't scanned in time, please start a new login",
		StatusCode: http.StatusGone,
	}
	ErrLoginCancelled = bridgev2.RespError{
		ErrCode:    "FI.MAU.BRIDGE.LOGIN_CANCELLED",
		Err:        "Login process was cancelled",
		StatusCode: http.StatusGone,
	}
	ErrDeviceLinkMissingCapability = bridgev2.RespError{
		ErrCode:    "FI.MAU.SIGNAL.DEVICE_LINK_MISSING_CAPABILITY",
		Err:        "Signal rejected linking because the bridge is missing a capability required by your account's other devices. Please try again later",
		StatusCode: http.StatusConflict,
	}
	ErrDeviceLimitReached = bridgev2.RespError{
		ErrCode:    "FI.MAU.SIGNAL.DEVICE_LIMIT_REACHED",
		Err:        "Your Signal account already has the maximum number of linked devices. Remove one in the Signal app and try again",
		StatusCode: http.StatusBadRequest,
	}
	ErrDeviceLinkCodeInvalid = bridgev2.RespError{
		ErrCode:    "FI.MAU.SIGNAL.DEVICE_LINK_CODE_INVALID",
		Err:        "The scanned QR code was invalid or already used, please start a new login",
		StatusCode: http.StatusForbidden,
	}
	ErrDeviceLinkRateLimited = bridgev2.RespError{
		ErrCode:    "FI.MAU.SIGNAL.DEVICE_LINK_RATE_LIMITED",
		Err:        "Signal rate-limited the linking attempt, please wait a few minutes and try again",
		StatusCode: http.StatusTooManyRequests,
	}
	ErrDeviceLinkRejected = bridgev2.RespError{
		ErrCode:    "FI.MAU.SIGNAL.DEVICE_LINK_REJECTED",
		Err:        "Signal rejected linking the device",
		StatusCode: http.StatusBadRequest,
	}
)

// Statuses of PUT /v1/devices/link, per Signal-Server's DeviceController
func wrapProvisioningError(err error) error {
	var linkErr signalmeow.DeviceLinkError
	if errors.As(err, &linkErr) {
		switch linkErr.StatusCode {
		case http.StatusConflict:
			return ErrDeviceLinkMissingCapability
		case http.StatusLengthRequired:
			return ErrDeviceLimitReached
		case http.StatusForbidden:
			return ErrDeviceLinkCodeInvalid
		case http.StatusTooManyRequests:
			return ErrDeviceLinkRateLimited
		default:
			if linkErr.Message != "" {
				return ErrDeviceLinkRejected.AppendMessage(" (HTTP %d: %s)", linkErr.StatusCode, linkErr.Message)
			}
			return ErrDeviceLinkRejected.AppendMessage(" (HTTP %d)", linkErr.StatusCode)
		}
	}
	if websocket.CloseStatus(err) == websocket.StatusGoingAway {
		return ErrLoginTimedOut
	}
	return err
}

func (qr *QRLogin) Start(ctx context.Context) (*bridgev2.LoginStep, error) {
	log := qr.Main.Bridge.Log.With().
		Str("action", "login").
		Stringer("user_id", qr.User.MXID).
		Logger()
	provCtx, cancel := context.WithCancel(log.WithContext(qr.Main.Bridge.BackgroundCtx))
	qr.cancelChan = cancel
	// Don't use the start context here: the channel will outlive the start request.
	qr.ProvChan = signalmeow.PerformProvisioning(
		provCtx, qr.Main.Store, qr.Main.Config.DeviceName, qr.Main.Bridge.Config.Backfill.Enabled,
	)
	var resp signalmeow.ProvisioningResponse
	select {
	case resp = <-qr.ProvChan:
		if resp.Err != nil {
			return nil, wrapProvisioningError(resp.Err)
		} else if resp.State != signalmeow.StateProvisioningURLReceived {
			return nil, fmt.Errorf("unexpected state %v", resp.State)
		}
	case <-ctx.Done():
		cancel()
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			return nil, ErrLoginTimedOut
		}
		return nil, ErrLoginCancelled
	}
	return &bridgev2.LoginStep{
		Type:         bridgev2.LoginStepTypeDisplayAndWait,
		StepID:       LoginStepQR,
		Instructions: "Scan the QR code on your Signal app to log in",
		DisplayAndWaitParams: &bridgev2.LoginDisplayAndWaitParams{
			Type: bridgev2.LoginDisplayTypeQR,
			Data: resp.ProvisioningURL,
		},
	}, nil
}

func (qr *QRLogin) Wait(ctx context.Context) (*bridgev2.LoginStep, error) {
	if qr.ProvChan == nil {
		return nil, fmt.Errorf("login not started")
	}

	select {
	case resp := <-qr.ProvChan:
		if resp.Err != nil {
			qr.cancelChan()
			return nil, wrapProvisioningError(resp.Err)
		} else if resp.State != signalmeow.StateProvisioningDataReceived {
			qr.cancelChan()
			return nil, fmt.Errorf("unexpected state %v", resp.State)
		} else if resp.ProvisioningData.ACI == uuid.Nil {
			qr.cancelChan()
			return nil, fmt.Errorf("no signal account ID received")
		}
		return qr.loginComplete(ctx, resp.ProvisioningData)

	// Server will timeout the request after 60 seconds, but Signal Desktop opens
	// a new socket and gets a new QR code after 45 seconds. We should do the same.
	case <-time.After(qrRefreshInterval):
		qr.cancelChan()
		qr.newQRCount++
		if qr.newQRCount >= maxQRRefreshes {
			return nil, ErrLoginTimedOut
		}
		return qr.Start(ctx)

	case <-ctx.Done():
		qr.cancelChan()
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			return nil, ErrLoginTimedOut
		}
		return nil, ErrLoginCancelled
	}
}

func (qr *QRLogin) loginComplete(ctx context.Context, provData *store.DeviceData) (*bridgev2.LoginStep, error) {
	defer qr.cancelChan()
	ul, err := qr.User.NewLogin(ctx, &database.UserLogin{
		ID:         signalid.MakeUserLoginID(provData.ACI),
		RemoteName: provData.Number,
		RemoteProfile: status.RemoteProfile{
			Phone: provData.Number,
		},
		Metadata: &signalid.UserLoginMetadata{},
	}, &bridgev2.NewLoginParams{
		DeleteOnConflict: true,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to create user login: %w", err)
	}
	ul.Client.(*SignalClient).postLoginConnect()
	return &bridgev2.LoginStep{
		Type:         bridgev2.LoginStepTypeComplete,
		StepID:       LoginStepComplete,
		Instructions: fmt.Sprintf("Successfully logged in as %s / %s", provData.Number, provData.ACI),
		CompleteParams: &bridgev2.LoginCompleteParams{
			UserLoginID: ul.ID,
			UserLogin:   ul,
		},
	}, nil
}
