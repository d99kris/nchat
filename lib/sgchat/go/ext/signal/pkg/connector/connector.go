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
	"fmt"
	"strconv"
	"time"

	"github.com/google/uuid"
	"go.mau.fi/util/dbutil"
	"go.mau.fi/util/exhttp"
	"go.mau.fi/util/exsync"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/commands"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/event"
	"maunium.net/go/mautrix/id"

	"go.mau.fi/mautrix-signal/pkg/msgconv"
	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

type SignalConnector struct {
	MsgConv *msgconv.MessageConverter
	Store   *store.Container
	Bridge  *bridgev2.Bridge
	Config  SignalConfig
}

var _ bridgev2.NetworkConnector = (*SignalConnector)(nil)
var _ bridgev2.MaxFileSizeingNetwork = (*SignalConnector)(nil)
var _ bridgev2.TransactionIDGeneratingNetwork = (*SignalConnector)(nil)

func (s *SignalConnector) GetName() bridgev2.BridgeName {
	return bridgev2.BridgeName{
		DisplayName:      "Signal",
		NetworkURL:       "https://signal.org",
		NetworkIcon:      "mxc://maunium.net/wPJgTQbZOtpBFmDNkiNEMDUp",
		NetworkID:        "signal",
		BeeperBridgeType: "signal",
		DefaultPort:      29328,
	}
}

func (s *SignalConnector) Init(bridge *bridgev2.Bridge) {
	s.Store = store.NewStore(bridge.DB.Database, dbutil.ZeroLogger(bridge.Log.With().Str("db_section", "signalmeow").Logger()))
	s.Bridge = bridge
	s.MsgConv = msgconv.NewMessageConverter(bridge)
	s.MsgConv.LocationFormat = s.Config.LocationFormat
	s.MsgConv.DisappearViewOnce = s.Config.DisappearViewOnce
	s.MsgConv.ExtEvPolls = s.Config.ExtEvPolls
	bridge.Commands.(*commands.Processor).AddHandlers(CmdDiscardSenderKey)
}

func (s *SignalConnector) SetMaxFileSize(maxSize int64) {
	s.MsgConv.MaxFileSize = maxSize
}

func (s *SignalConnector) Start(ctx context.Context) error {
	s.ResetHTTPTransport()
	err := s.Store.Upgrade(ctx)
	if err != nil {
		return bridgev2.DBUpgradeError{Err: err, Section: "signalmeow"}
	}
	return nil
}

func (s *SignalConnector) ResetHTTPTransport() {
	settings := exhttp.SensibleClientSettings
	hs, ok := s.Bridge.Matrix.(bridgev2.MatrixConnectorWithHTTPSettings)
	if ok {
		settings = hs.GetHTTPClientSettings()
	}
	oldClient := web.SignalHTTPClient
	web.SignalHTTPClient = settings.WithTLSConfig(web.SignalTLSConfig).Compile()
	oldClient.CloseIdleConnections()
}

func (s *SignalConnector) ResetNetworkConnections() {
	for _, login := range s.Bridge.GetAllCachedUserLogins() {
		c := login.Client.(*SignalClient)
		if c.Client != nil {
			c.Client.ForceReconnect()
		}
	}
}

func (s *SignalConnector) LoadUserLogin(ctx context.Context, login *bridgev2.UserLogin) error {
	aci, err := uuid.Parse(string(login.ID))
	if err != nil {
		return fmt.Errorf("failed to parse user login ID: %w", err)
	}
	device, err := s.Store.DeviceByACI(ctx, aci)
	if err != nil {
		return fmt.Errorf("failed to get device from store: %w", err)
	}
	sc := &SignalClient{
		Main:      s,
		UserLogin: login,

		queueEmptyWaiter: exsync.NewEvent(),
	}
	if device != nil {
		sc.Client = signalmeow.NewClient(
			device,
			sc.UserLogin.Log.With().Str("component", "signalmeow").Logger(),
			sc.handleSignalEvent,
		)
		sc.Client.SyncContactsOnConnect = s.Config.SyncContactsOnStartup &&
			time.Since(login.Metadata.(*signalid.UserLoginMetadata).LastContactSync.Time) > 3*24*time.Hour
	}
	login.Client = sc
	return nil
}

func (s *SignalConnector) GenerateTransactionID(userID id.UserID, roomID id.RoomID, eventType event.Type) networkid.RawTransactionID {
	return networkid.RawTransactionID(strconv.FormatInt(time.Now().UnixMilli(), 10))
}
