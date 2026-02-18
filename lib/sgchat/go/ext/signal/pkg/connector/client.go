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
	"time"

	"github.com/rs/zerolog"
	"go.mau.fi/util/exsync"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"
	"maunium.net/go/mautrix/bridgev2/status"

	"go.mau.fi/mautrix-signal/pkg/signalid"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

type SignalClient struct {
	Main      *SignalConnector
	UserLogin *bridgev2.UserLogin
	Client    *signalmeow.Client
	Ghost     *bridgev2.Ghost

	queueEmptyWaiter *exsync.Event
}

var (
	_ bridgev2.NetworkAPI                  = (*SignalClient)(nil)
	_ bridgev2.BackgroundSyncingNetworkAPI = (*SignalClient)(nil)
)

var pushCfg = &bridgev2.PushConfig{
	FCM: &bridgev2.FCMPushConfig{
		// https://github.com/signalapp/Signal-Android/blob/main/app/src/main/res/values/firebase_messaging.xml#L4
		SenderID: "312334754206",
	},
	APNs: &bridgev2.APNsPushConfig{
		BundleID: "org.whispersystems.signal",
	},
}

func (s *SignalClient) GetPushConfigs() *bridgev2.PushConfig {
	return pushCfg
}

func (s *SignalClient) RegisterPushNotifications(ctx context.Context, pushType bridgev2.PushType, token string) error {
	if s.Client == nil {
		return bridgev2.ErrNotLoggedIn
	}
	switch pushType {
	case bridgev2.PushTypeFCM:
		return s.Client.RegisterFCM(ctx, token)
	case bridgev2.PushTypeAPNs:
		return s.Client.RegisterAPNs(ctx, token)
	default:
		return fmt.Errorf("unsupported push type: %s", pushType)
	}
}

func (s *SignalClient) LogoutRemote(ctx context.Context) {
	if s.Client == nil {
		return
	}
	err := s.Client.Unlink(ctx)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to unlink device")
	}
	err = s.Client.StopReceiveLoops()
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to stop receive loops for logout")
	}
	err = s.Main.Store.DeleteDevice(context.TODO(), &s.Client.Store.DeviceData)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to delete device from store")
	}
}

func (s *SignalClient) IsThisUser(_ context.Context, userID networkid.UserID) bool {
	if s.Client == nil {
		return false
	}
	return userID == signalid.MakeUserID(s.Client.Store.ACI)
}

func (s *SignalClient) bridgeStateLoop(statusChan <-chan signalmeow.SignalConnectionStatus) {
	var peekedConnectionStatus signalmeow.SignalConnectionStatus
	for {
		var connectionStatus signalmeow.SignalConnectionStatus
		if peekedConnectionStatus.Event != signalmeow.SignalConnectionEventNone {
			s.UserLogin.Log.Debug().
				Stringer("peeked_connection_status_event", peekedConnectionStatus.Event).
				Msg("Using peeked connectionStatus event")
			connectionStatus = peekedConnectionStatus
			peekedConnectionStatus = signalmeow.SignalConnectionStatus{}
		} else {
			var ok bool
			connectionStatus, ok = <-statusChan
			if !ok {
				s.UserLogin.Log.Debug().Msg("statusChan channel closed")
				return
			}
		}

		err := connectionStatus.Err
		switch connectionStatus.Event {
		case signalmeow.SignalConnectionEventConnected:
			s.UserLogin.Log.Debug().Msg("Sending Connected BridgeState")
			s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateConnected})

		case signalmeow.SignalConnectionEventDisconnected:
			s.UserLogin.Log.Debug().Msg("Received SignalConnectionEventDisconnected")

			// Debounce: wait 7s before sending TransientDisconnect, in case we get a reconnect
			// We should wait until the next message comes in, or 7 seconds has passed.
			// - If a disconnected event comes in, just loop again, unless it's been more than 7 seconds.
			// - If a non-disconnected event comes in, store it in peekedConnectionStatus,
			//   break out of this loop and go back to the top of the goroutine to handle it in the switch.
			// - If 7 seconds passes without any non-disconnect messages, send the TransientDisconnect.
			//   (Why 7 seconds? It was 5 at first, but websockets min retry is 5 seconds,
			//     so it would send TransientDisconnect right before reconnecting. 7 seems to work well.)
			debounceTimer := time.NewTimer(7 * time.Second)
		PeekLoop:
			for {
				var ok bool
				select {
				case peekedConnectionStatus, ok = <-statusChan:
					// Handle channel closing
					if !ok {
						s.UserLogin.Log.Debug().Msg("connectionStatus channel closed")
						return
					}
					// If it's another Disconnected event, just keep looping
					if peekedConnectionStatus.Event == signalmeow.SignalConnectionEventDisconnected {
						peekedConnectionStatus = signalmeow.SignalConnectionStatus{}
						continue
					}
					// If it's a non-disconnect event, break out of the PeekLoop and handle it in the switch
					break PeekLoop
				case <-debounceTimer.C:
					// Time is up, so break out of the loop and send the TransientDisconnect
					break PeekLoop
				}
			}
			// We're out of the PeekLoop, so either we got a non-disconnect event, or it's been 7 seconds (or both).
			// We want to send TransientDisconnect if it's been 7 seconds, but not if the latest event was something
			// other than Disconnected
			if !debounceTimer.Stop() { // If the timer has already expired
				// Send TransientDisconnect only if the latest event is a disconnect or no event
				// (peekedConnectionStatus could be something else if the timer and the event race)
				if peekedConnectionStatus.Event == signalmeow.SignalConnectionEventDisconnected ||
					peekedConnectionStatus.Event == signalmeow.SignalConnectionEventNone {
					s.UserLogin.Log.Debug().Msg("Sending TransientDisconnect BridgeState")
					if err == nil {
						s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateTransientDisconnect})
					} else {
						s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateTransientDisconnect, Error: "unknown-websocket-error", Message: err.Error()})
					}
				}
			}

		case signalmeow.SignalConnectionEventLoggedOut:
			s.UserLogin.Log.Debug().Msg("Sending BadCredentials BridgeState")
			if err == nil {
				s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateBadCredentials, Message: "You have been logged out of Signal, please reconnect"})
			} else {
				s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateBadCredentials, Message: err.Error()})
			}
			err = s.Client.ClearKeysAndDisconnect(context.TODO())
			if err != nil {
				s.UserLogin.Log.Error().Err(err).Msg("Failed to clear keys and disconnect")
			}

		case signalmeow.SignalConnectionEventError:
			s.UserLogin.Log.Debug().Msg("Sending TransientDisconnect BridgeState")
			s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateTransientDisconnect, Error: "unknown-websocket-error", Message: err.Error()})

		case signalmeow.SignalConnectionEventFatalError:
			s.UserLogin.Log.Debug().Msg("Sending UnknownError BridgeState")
			s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateUnknownError, Error: "unknown-websocket-error", Message: err.Error()})

		case signalmeow.SignalConnectionCleanShutdown:
			if s.Client.IsLoggedIn() {
				s.UserLogin.Log.Debug().Msg("Clean Shutdown - sending no BridgeState")
			} else {
				s.UserLogin.Log.Debug().Msg("Clean Shutdown, but logged out - Sending BadCredentials BridgeState")
				s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateBadCredentials, Message: "You have been logged out of Signal, please reconnect"})
			}
		}
	}
}

func (s *SignalClient) Connect(ctx context.Context) {
	if s.Client == nil {
		s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateBadCredentials, Message: "You're not logged into Signal"})
		return
	}
	s.updateRemoteProfile(ctx, false)
	s.tryConnect(ctx, 0, true)
}

func (s *SignalClient) ConnectBackground(ctx context.Context, _ *bridgev2.ConnectBackgroundParams) error {
	s.queueEmptyWaiter.Clear()
	ch, unauthCh, err := s.Client.StartWebsockets(ctx)
	if err != nil {
		return err
	}
	defer s.Disconnect()
	log := zerolog.Ctx(ctx)
	queueEmpty := s.queueEmptyWaiter.GetChan()
	didConnect := false
	for {
		select {
		case status := <-ch:
			switch status.Event {
			case web.SignalWebsocketConnectionEventConnected:
				log.Info().Msg("Authed websocket connected")
				didConnect = true
			case web.SignalWebsocketConnectionEventDisconnected:
				log.Err(status.Err).Msg("Authed websocket disconnected")
			case web.SignalWebsocketConnectionEventLoggedOut:
				log.Err(status.Err).Msg("Authed websocket logged out")
				return fmt.Errorf("authed websocket logged out: %w", status.Err)
			case web.SignalWebsocketConnectionEventError, web.SignalWebsocketConnectionEventFatalError:
				log.Err(status.Err).Msg("Authed websocket error")
				return fmt.Errorf("authed websocket errored: %w", status.Err)
			case web.SignalWebsocketConnectionEventCleanShutdown:
				log.Info().Msg("Authed websocket clean shutdown")
			}
		case status := <-unauthCh:
			switch status.Event {
			case web.SignalWebsocketConnectionEventConnected:
				log.Info().Msg("Unauthed websocket connected")
			case web.SignalWebsocketConnectionEventDisconnected:
				log.Err(status.Err).Msg("Unauthed websocket disconnected")
			case web.SignalWebsocketConnectionEventLoggedOut:
				log.Err(status.Err).Msg("Unauthed websocket logged out")
			case web.SignalWebsocketConnectionEventError, web.SignalWebsocketConnectionEventFatalError:
				log.Err(status.Err).Msg("Unauthed websocket error")
			case web.SignalWebsocketConnectionEventCleanShutdown:
				log.Info().Msg("Unauthed websocket clean shutdown")
			}
		case <-ctx.Done():
			log.Warn().Msg("Context finished before queue empty event")
			if didConnect {
				// Don't propagate timeout errors if the connection was successful at least once
				return nil
			}
			return ctx.Err()
		case <-queueEmpty:
			log.Info().Msg("Received queue empty event")
			return nil
		}
	}
}

func (s *SignalClient) Disconnect() {
	if s.Client == nil {
		return
	}
	err := s.Client.StopReceiveLoops()
	if err != nil {
		s.UserLogin.Log.Err(err).Msg("Failed to stop receive loops")
	}
}

func (s *SignalClient) postLoginConnect() {
	ctx := s.UserLogin.Log.WithContext(context.Background())
	// TODO it would be more proper to only connect after syncing,
	//      but currently syncing will fetch group info online, so it has to be connected.
	s.tryConnect(ctx, 0, false)
	if s.Client.Store.EphemeralBackupKey != nil {
		go func() {
			if s.Client.Store.MasterKey != nil {
				s.Client.SyncStorage(ctx)
			} else {
				s.UserLogin.Log.Warn().Msg("No master key for storage sync before backup sync")
			}
			s.syncChats(ctx)
		}()
	} else if s.Client.Store.MasterKey != nil {
		go s.Client.SyncStorage(ctx)
	}
}

func (s *SignalClient) tryConnect(ctx context.Context, retryCount int, doSync bool) {
	if retryCount == 0 {
		s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateConnecting})
	}
	ch, err := s.Client.StartReceiveLoops(ctx)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to start receive loops")
		s.UserLogin.BridgeState.Send(status.BridgeState{StateEvent: status.StateTransientDisconnect, Error: "unknown-websocket-error", Message: err.Error()})
		retryInSeconds := 2 << retryCount
		if retryInSeconds > 150 {
			retryInSeconds = 150
		}
		zerolog.Ctx(ctx).Debug().Int("retry_in_seconds", retryInSeconds).Msg("Sleeping and retrying connection")
		select {
		case <-time.After(time.Duration(retryInSeconds) * time.Second):
		case <-ctx.Done():
			zerolog.Ctx(ctx).Info().Msg("Context canceled, exit tryConnect")
			return
		}
		s.tryConnect(ctx, retryCount+1, doSync)
	} else {
		go s.bridgeStateLoop(ch)
		if doSync {
			go s.syncChats(ctx)
		}
	}
}

func (s *SignalClient) IsLoggedIn() bool {
	if s.Client == nil {
		return false
	}
	return s.Client.IsLoggedIn()
}
