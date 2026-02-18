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

package msgconv

import (
	"context"

	"github.com/google/uuid"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/msgconv/matrixfmt"
	"go.mau.fi/mautrix-signal/pkg/msgconv/signalfmt"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
)

type contextKey int

const (
	contextKeyPortal contextKey = iota
	contextKeyClient
	contextKeyIntent
)

type MessageConverter struct {
	Bridge *bridgev2.Bridge

	SignalFmtParams *signalfmt.FormatParams
	MatrixFmtParams *matrixfmt.HTMLParser

	MaxFileSize       int64
	LocationFormat    string
	DisappearViewOnce bool
	DirectMedia       bool
	ExtEvPolls        bool
}

func NewMessageConverter(br *bridgev2.Bridge) *MessageConverter {
	return &MessageConverter{
		Bridge: br,
		SignalFmtParams: &signalfmt.FormatParams{
			GetUserInfo: func(ctx context.Context, uuid uuid.UUID) signalfmt.UserInfo {
				ghost, err := br.GetGhostByID(ctx, signalid.MakeUserID(uuid))
				if err != nil {
					// TODO log?
					return signalfmt.UserInfo{}
				}
				userInfo := signalfmt.UserInfo{
					MXID: ghost.Intent.GetMXID(),
					Name: ghost.Name,
				}
				userLogin := br.GetCachedUserLoginByID(networkid.UserLoginID(uuid.String()))
				portal := getPortal(ctx)
				if userLogin != nil && (portal.Receiver == "" || portal.Receiver == userLogin.ID) {
					userInfo.MXID = userLogin.UserMXID
					// TODO find matrix user displayname?
				}
				return userInfo
			},
		},
		MatrixFmtParams: &matrixfmt.HTMLParser{
			GetUUIDFromMXID: func(ctx context.Context, userID id.UserID) uuid.UUID {
				parsed, ok := br.Matrix.ParseGhostMXID(userID)
				if ok {
					u, _ := uuid.Parse(string(parsed))
					return u
				}
				user, _ := br.GetExistingUserByMXID(ctx, userID)
				// TODO log errors?
				if user != nil {
					preferredLogin, _, _ := getPortal(ctx).FindPreferredLogin(ctx, user, true)
					if preferredLogin != nil {
						u, _ := uuid.Parse(string(preferredLogin.ID))
						return u
					}
				}
				return uuid.Nil
			},
		},
		MaxFileSize: 50 * 1024 * 1024,
	}
}

func getClient(ctx context.Context) *signalmeow.Client {
	return ctx.Value(contextKeyClient).(*signalmeow.Client)
}

func getPortal(ctx context.Context) *bridgev2.Portal {
	return ctx.Value(contextKeyPortal).(*bridgev2.Portal)
}

func getIntent(ctx context.Context) bridgev2.MatrixAPI {
	return ctx.Value(contextKeyIntent).(bridgev2.MatrixAPI)
}
