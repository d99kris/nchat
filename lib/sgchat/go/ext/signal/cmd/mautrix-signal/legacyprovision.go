// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2024 Tulir Asokan
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is istributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package main

import (
	"encoding/json"
	"fmt"
	"net/http"

	"github.com/rs/zerolog"
	"maunium.net/go/mautrix"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/id"
)

func legacyResolveIdentifierOrStartChat(w http.ResponseWriter, r *http.Request, create bool) {
	login := m.Matrix.Provisioning.GetLoginForRequest(w, r)
	if login == nil {
		return
	}
	api := login.Client.(bridgev2.IdentifierResolvingNetworkAPI)
	phonenum := r.PathValue("phonenum")
	resp, err := api.ResolveIdentifier(r.Context(), phonenum, create)
	if err != nil {
		zerolog.Ctx(r.Context()).Err(err).Msg("Failed to resolve identifier")
		JSONResponse(w, http.StatusInternalServerError, &Error{
			Error:   fmt.Sprintf("Failed to resolve identifier: %v", err),
			ErrCode: "M_UNKNOWN",
		})
		return
	} else if resp == nil {
		JSONResponse(w, http.StatusNotFound, &Error{
			ErrCode: mautrix.MNotFound.ErrCode,
			Error:   "User not found on Signal",
		})
		return
	}
	status := http.StatusOK
	apiResp := &ResolveIdentifierResponse{
		ChatID: ResolveIdentifierResponseChatID{
			UUID:   string(resp.UserID),
			Number: phonenum,
		},
	}
	if resp.Ghost != nil {
		if resp.UserInfo != nil {
			resp.Ghost.UpdateInfo(r.Context(), resp.UserInfo)
		}
		apiResp.OtherUser = &ResolveIdentifierResponseOtherUser{
			MXID:        resp.Ghost.Intent.GetMXID(),
			DisplayName: resp.Ghost.Name,
			AvatarURL:   resp.Ghost.AvatarMXC.ParseOrIgnore(),
		}
	}
	if resp.Chat != nil {
		if resp.Chat.Portal == nil {
			resp.Chat.Portal, err = m.Bridge.GetPortalByKey(r.Context(), resp.Chat.PortalKey)
			if err != nil {
				zerolog.Ctx(r.Context()).Err(err).Msg("Failed to get portal")
				JSONResponse(w, http.StatusInternalServerError, &mautrix.RespError{
					Err:     "Failed to get portal",
					ErrCode: "M_UNKNOWN",
				})
				return
			}
		}
		if create && resp.Chat.Portal.MXID == "" {
			apiResp.JustCreated = true
			status = http.StatusCreated
			err = resp.Chat.Portal.CreateMatrixRoom(r.Context(), login, resp.Chat.PortalInfo)
			if err != nil {
				zerolog.Ctx(r.Context()).Err(err).Msg("Failed to create portal room")
				JSONResponse(w, http.StatusInternalServerError, &mautrix.RespError{
					Err:     "Failed to create portal room",
					ErrCode: "M_UNKNOWN",
				})
				return
			}
		}
		apiResp.RoomID = resp.Chat.Portal.MXID
	}
	JSONResponse(w, status, &Response{
		Success:                   true,
		Status:                    "ok",
		ResolveIdentifierResponse: apiResp,
	})
}

func legacyProvResolveIdentifier(w http.ResponseWriter, r *http.Request) {
	legacyResolveIdentifierOrStartChat(w, r, false)
}

func legacyProvPM(w http.ResponseWriter, r *http.Request) {
	legacyResolveIdentifierOrStartChat(w, r, true)
}

func JSONResponse(w http.ResponseWriter, status int, response any) {
	w.Header().Add("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(response)
}

type Error struct {
	Success bool   `json:"success"`
	Error   string `json:"error"`
	ErrCode string `json:"errcode"`
}

type Response struct {
	Success bool   `json:"success"`
	Status  string `json:"status"`

	// For response in ResolveIdentifier
	*ResolveIdentifierResponse
}

type ResolveIdentifierResponse struct {
	RoomID      id.RoomID                           `json:"room_id"`
	ChatID      ResolveIdentifierResponseChatID     `json:"chat_id"`
	JustCreated bool                                `json:"just_created"`
	OtherUser   *ResolveIdentifierResponseOtherUser `json:"other_user,omitempty"`
}

type ResolveIdentifierResponseChatID struct {
	UUID   string `json:"uuid"`
	Number string `json:"number"`
}

type ResolveIdentifierResponseOtherUser struct {
	MXID        id.UserID     `json:"mxid"`
	DisplayName string        `json:"displayname"`
	AvatarURL   id.ContentURI `json:"avatar_url"`
}
