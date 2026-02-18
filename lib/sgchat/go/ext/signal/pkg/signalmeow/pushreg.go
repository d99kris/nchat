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

package signalmeow

import (
	"context"
	"encoding/json"
	"net/http"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

type ReqRegisterFCM struct {
	GCMRegistrationID string `json:"gcmRegistrationId,omitempty"`
	WebSocketChannel  bool   `json:"webSocketChannel"`
}

type ReqRegisterAPNs struct {
	APNRegistrationID  string `json:"apnRegistrationId,omitempty"`
	VoIPRegistrationID string `json:"voipRegistrationId,omitempty"`
}

func (cli *Client) registerPush(ctx context.Context, pushType string, data any) error {
	var resp *signalpb.WebSocketResponseMessage
	var err error
	if data != nil {
		body, err := json.Marshal(data)
		if err != nil {
			return err
		}
		resp, err = cli.AuthedWS.SendRequest(ctx, http.MethodPut, "/v1/accounts/"+pushType, body, nil)
	} else {
		resp, err = cli.AuthedWS.SendRequest(ctx, http.MethodDelete, "/v1/accounts/"+pushType, nil, nil)
	}
	if err != nil {
		return err
	}
	return web.DecodeWSResponseBody(ctx, nil, resp)
}

func (cli *Client) RegisterFCM(ctx context.Context, token string) error {
	if token == "" {
		return cli.registerPush(ctx, "gcm", nil)
	}
	return cli.registerPush(ctx, "gcm", &ReqRegisterFCM{
		GCMRegistrationID: token,
		WebSocketChannel:  true,
	})
}

func (cli *Client) RegisterAPNs(ctx context.Context, token string) error {
	if token == "" {
		return cli.registerPush(ctx, "apn", nil)
	}
	return cli.registerPush(ctx, "apn", &ReqRegisterAPNs{
		APNRegistrationID: token,
	})
}
