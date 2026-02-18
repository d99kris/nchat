// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

package signalmeow

import (
	"bytes"
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/ptr"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/events"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

type SignalConnectionEvent int

const (
	SignalConnectionEventNone SignalConnectionEvent = iota
	SignalConnectionEventConnected
	SignalConnectionEventDisconnected
	SignalConnectionEventLoggedOut
	SignalConnectionEventError
	SignalConnectionEventFatalError
	SignalConnectionCleanShutdown
)

// mapping from SignalConnectionEvent to its string representation
var signalConnectionEventNames = map[SignalConnectionEvent]string{
	SignalConnectionEventNone:         "SignalConnectionEventNone",
	SignalConnectionEventConnected:    "SignalConnectionEventConnected",
	SignalConnectionEventDisconnected: "SignalConnectionEventDisconnected",
	SignalConnectionEventLoggedOut:    "SignalConnectionEventLoggedOut",
	SignalConnectionEventError:        "SignalConnectionEventError",
	SignalConnectionEventFatalError:   "SignalConnectionEventFatalError",
	SignalConnectionCleanShutdown:     "SignalConnectionCleanShutdown",
}

// Implement the fmt.Stringer interface
func (s SignalConnectionEvent) String() string {
	return signalConnectionEventNames[s]
}

type SignalConnectionStatus struct {
	Event SignalConnectionEvent
	Err   error
}

func (cli *Client) StartWebsockets(ctx context.Context) (authChan, unauthChan chan web.SignalWebsocketConnectionStatus, err error) {
	authChan, unauthChan, _, _, err = cli.startWebsocketsInternal(ctx)
	return
}

func (cli *Client) startWebsocketsInternal(
	ctx context.Context,
) (
	authChan, unauthChan chan web.SignalWebsocketConnectionStatus,
	loopCtx context.Context, loopCancel context.CancelFunc,
	err error,
) {
	loopCtx, loopCancel = context.WithCancel(ctx)
	unauthChan, err = cli.connectUnauthedWS(loopCtx)
	if err != nil {
		loopCancel()
		return
	}
	zerolog.Ctx(ctx).Info().Msg("Unauthed websocket connecting")
	authChan, err = cli.connectAuthedWS(loopCtx, cli.incomingRequestHandler)
	if err != nil {
		loopCancel()
		return
	}
	zerolog.Ctx(ctx).Info().Msg("Authed websocket connecting")
	cli.loopCancel = loopCancel
	return
}

func (cli *Client) StartReceiveLoops(ctx context.Context) (chan SignalConnectionStatus, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "start receive loops").Logger()
	cbc := make(chan time.Time, 1)
	cli.writeCallbackCounter = cbc

	authChan, unauthChan, loopCtx, loopCancel, err := cli.startWebsocketsInternal(log.WithContext(ctx))
	if err != nil {
		return nil, err
	}
	statusChan := make(chan SignalConnectionStatus, 128)

	initialConnectChan := make(chan struct{})
	resetWriteCount := make(chan struct{}, 1)

	// Combine both websocket status channels into a single, more generic "Signal" connection status channel
	cli.loopWg.Add(2)
	go func() {
		defer cli.loopWg.Done()
		writeCallbackTimer := time.Now()
		callbackCount := 0
		for {
			select {
			case <-loopCtx.Done():
				return
			case <-resetWriteCount:
				callbackCount = 0
			case nextTS := <-cbc:
				if callbackCount >= 4 && time.Since(writeCallbackTimer) > 1*time.Minute {
					err := cli.Store.EventBuffer.DeleteBufferedEventsOlderThan(ctx, writeCallbackTimer)
					if err != nil {
						log.Err(err).Msg("Failed to delete old buffered event hashes")
					}
					writeCallbackTimer = nextTS
				} else {
					callbackCount++
				}
			}
		}
	}()
	go func() {
		defer cli.loopWg.Done()
		defer close(statusChan)
		defer loopCancel()
		var currentStatus, lastAuthStatus, lastUnauthStatus web.SignalWebsocketConnectionStatus
		for {
			select {
			case <-loopCtx.Done():
				log.Info().Msg("Context done, exiting websocket status loop")
				return
			case status := <-authChan:
				lastAuthStatus = status
				currentStatus = status

				switch status.Event {
				case web.SignalWebsocketConnectionEventConnecting:
					// do nothing?
				case web.SignalWebsocketConnectionEventConnected:
					log.Info().Msg("Authed websocket connected")
				case web.SignalWebsocketConnectionEventDisconnected:
					log.Err(status.Err).Msg("Authed websocket disconnected")
				case web.SignalWebsocketConnectionEventLoggedOut:
					log.Err(status.Err).Msg("Authed websocket logged out")
					// TODO: Also make sure unauthed websocket is disconnected
					//StopReceiveLoops(d)
				case web.SignalWebsocketConnectionEventError:
					log.Err(status.Err).Msg("Authed websocket error")
				case web.SignalWebsocketConnectionEventFatalError:
					log.Err(status.Err).Msg("Authed websocket fatal error")
				case web.SignalWebsocketConnectionEventCleanShutdown:
					log.Info().Msg("Authed websocket clean shutdown")
				}
				if status.Event != web.SignalWebsocketConnectionEventConnected {
					select {
					case resetWriteCount <- struct{}{}:
					default:
					}
				}
			case status := <-unauthChan:
				lastUnauthStatus = status
				currentStatus = status

				switch status.Event {
				case web.SignalWebsocketConnectionEventConnecting:
					// do nothing?
				case web.SignalWebsocketConnectionEventConnected:
					log.Info().
						Any("last_unauth_status", lastUnauthStatus).
						Any("last_auth_status", lastAuthStatus).
						Any("current_status", currentStatus).
						Msg("Unauthed websocket connected")
				case web.SignalWebsocketConnectionEventDisconnected:
					log.Err(status.Err).Msg("Unauthed websocket disconnected")
				case web.SignalWebsocketConnectionEventLoggedOut:
					log.Err(status.Err).Msg("Unauthed websocket logged out ** THIS SHOULD BE IMPOSSIBLE **")
				case web.SignalWebsocketConnectionEventError:
					log.Err(status.Err).Msg("Unauthed websocket error")
				case web.SignalWebsocketConnectionEventFatalError:
					log.Err(status.Err).Msg("Unauthed websocket fatal error")
				case web.SignalWebsocketConnectionEventCleanShutdown:
					log.Info().Msg("Unauthed websocket clean shutdown")
				}
			}

			var statusToSend SignalConnectionStatus
			if lastAuthStatus.Event == web.SignalWebsocketConnectionEventConnected && lastUnauthStatus.Event == web.SignalWebsocketConnectionEventConnected {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionEventConnected,
				}
				if initialConnectChan != nil {
					close(initialConnectChan)
					initialConnectChan = nil
				}
			} else if currentStatus.Event == web.SignalWebsocketConnectionEventDisconnected {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionEventDisconnected,
					Err:   currentStatus.Err,
				}
			} else if currentStatus.Event == web.SignalWebsocketConnectionEventLoggedOut {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionEventLoggedOut,
					Err:   currentStatus.Err,
				}
			} else if currentStatus.Event == web.SignalWebsocketConnectionEventError {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionEventError,
					Err:   currentStatus.Err,
				}
			} else if currentStatus.Event == web.SignalWebsocketConnectionEventFatalError {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionEventFatalError,
					Err:   currentStatus.Err,
				}
			} else if currentStatus.Event == web.SignalWebsocketConnectionEventCleanShutdown {
				statusToSend = SignalConnectionStatus{
					Event: SignalConnectionCleanShutdown,
				}
			}
			if statusToSend.Event != 0 && statusToSend.Event != cli.lastConnectionStatus.Event {
				log.Info().Any("status_to_send", statusToSend).Msg("Sending connection status")
				statusChan <- statusToSend
				cli.lastConnectionStatus = statusToSend
			}
		}
	}()

	// Send sync message once both websockets are connected
	cli.loopWg.Add(1)
	go func() {
		defer cli.loopWg.Done()
		select {
		case <-loopCtx.Done():
			return
		case <-initialConnectChan:
			log.Info().Msg("Both websockets connected, sending contacts sync request")
			err = cli.RegisterCapabilities(ctx)
			if err != nil {
				zerolog.Ctx(ctx).Err(err).Msg("Failed to register capabilities")
			} else {
				zerolog.Ctx(ctx).Debug().Msg("Successfully registered capabilities")
			}
			// Start loop to check for and upload more prekeys
			cli.loopWg.Add(1)
			go func() {
				defer cli.loopWg.Done()
				cli.keyCheckLoop(loopCtx)
			}()
			// TODO hacky
			if cli.SyncContactsOnConnect {
				cli.SendContactSyncRequest(loopCtx)
			}
			if cli.Store.MasterKey == nil {
				cli.SendStorageMasterKeyRequest(loopCtx)
			}
		}
	}()

	return statusChan, nil
}

func (cli *Client) ForceReconnect() {
	cli.AuthedWS.ForceReconnect()
	cli.UnauthedWS.ForceReconnect()
}

func (cli *Client) StopReceiveLoops() error {
	defer func() {
		cli.AuthedWS = nil
		cli.UnauthedWS = nil
	}()
	authErr := cli.AuthedWS.Close()
	unauthErr := cli.UnauthedWS.Close()
	if cli.loopCancel != nil {
		cli.loopCancel()
		cli.loopWg.Wait()
	}
	if authErr != nil {
		return authErr
	}
	if unauthErr != nil {
		return unauthErr
	}
	return nil
}

func (cli *Client) LastConnectionStatus() SignalConnectionStatus {
	return cli.lastConnectionStatus
}

func (cli *Client) ClearKeysAndDisconnect(ctx context.Context) error {
	// Essentially logout, clearing sessions and keys, and disconnecting websockets
	// but don't clear ACI UUID or profile keys or contacts, or anything else that
	// we can reuse if we reassociate with the same Signal account.
	// To fully "logout" delete the device from the database.
	clearErr := cli.Store.ClearDeviceKeys(ctx)
	clearErr2 := cli.Store.ClearPassword(ctx)
	stopLoopErr := cli.StopReceiveLoops()

	if clearErr != nil {
		return clearErr
	}
	if clearErr2 != nil {
		return clearErr2
	}
	return stopLoopErr
}

func (cli *Client) incomingRequestHandler(ctx context.Context, req *signalpb.WebSocketRequestMessage) (*web.SimpleResponse, error) {
	log := zerolog.Ctx(ctx).With().
		Str("handler", "incoming request handler").
		Str("verb", *req.Verb).
		Str("path", *req.Path).
		Uint64("incoming_request_id", *req.Id).
		Logger()
	ctx = log.WithContext(ctx)
	if *req.Verb == http.MethodPut && *req.Path == "/api/v1/message" {
		return cli.incomingAPIMessageHandler(ctx, req)
	} else if *req.Verb == http.MethodPut && *req.Path == "/api/v1/queue/empty" {
		log.Debug().Msg("Received queue empty notice")
		cli.handleEvent(&events.QueueEmpty{})
	} else {
		log.Warn().Any("req", req).Msg("Unknown websocket request message")
	}
	return &web.SimpleResponse{
		Status: 200,
	}, nil
}

func (cli *Client) incomingAPIMessageHandler(ctx context.Context, req *signalpb.WebSocketRequestMessage) (*web.SimpleResponse, error) {
	log := *zerolog.Ctx(ctx)
	envelope := &signalpb.Envelope{}
	err := proto.Unmarshal(req.Body, envelope)
	if err != nil {
		log.Err(err).Msg("Unmarshal error")
		return nil, err
	}
	log = log.With().
		Uint64("envelope_timestamp", envelope.GetTimestamp()).
		Uint64("server_timestamp", envelope.GetServerTimestamp()).
		Logger()
	ctx = log.WithContext(ctx)
	destinationServiceID, err := libsignalgo.ServiceIDFromString(envelope.GetDestinationServiceId())
	log.Debug().
		Str("destination_service_id", envelope.GetDestinationServiceId()).
		Str("source_service_id", envelope.GetSourceServiceId()).
		Uint32("source_device_id", envelope.GetSourceDevice()).
		Object("parsed_destination_service_id", destinationServiceID).
		Int32("envelope_type_id", int32(envelope.GetType())).
		Str("envelope_type", signalpb.Envelope_Type_name[int32(envelope.GetType())]).
		Msg("Received envelope")

	result := cli.decryptEnvelope(ctx, envelope)

	err = cli.handleDecryptedResult(ctx, result, envelope, destinationServiceID)
	if err != nil {
		log.Err(err).Msg("Error handling decrypted result")
		return nil, err
	}

	return &web.SimpleResponse{
		Status:        200,
		WriteCallback: cli.writeCallback,
	}, nil
}

func (cli *Client) writeCallback(preWriteTime time.Time) {
	ch := cli.writeCallbackCounter
	if ch != nil {
		select {
		case ch <- preWriteTime:
		default:
		}
	}
}

var ErrHandlerFailed = errors.New("event handler returned non-success status")

// TODO: we should split this up into multiple functions
func (cli *Client) handleDecryptedResult(
	ctx context.Context,
	result DecryptionResult,
	envelope *signalpb.Envelope,
	destinationServiceID libsignalgo.ServiceID,
) (retErr error) {
	if errors.Is(result.Err, context.Canceled) {
		return result.Err
	} else if ctx.Err() != nil {
		return ctx.Err()
	}
	log := zerolog.Ctx(ctx)
	if result.CiphertextHash != nil {
		defer func() {
			err := cli.Store.EventBuffer.ClearBufferedEventPlaintext(ctx, *result.CiphertextHash)
			if err != nil {
				log.Err(err).
					Hex("ciphertext_hash", result.CiphertextHash[:]).
					Msg("Failed to clear buffered event plaintext")
			} else {
				log.Debug().
					Hex("ciphertext_hash", result.CiphertextHash[:]).
					Msg("Deleted event plaintext from buffer")
			}
		}()
	}

	var theirServiceID libsignalgo.ServiceID
	var err error
	if result.SenderAddress == nil {
		log.Err(result.Err).
			Bool("urgent", envelope.GetUrgent()).
			Stringer("content_hint", result.ContentHint).
			Uint64("server_ts", envelope.GetServerTimestamp()).
			Uint64("client_ts", envelope.GetTimestamp()).
			Msg("No sender address received")
		return nil
	} else if theirServiceID, err = result.SenderAddress.NameServiceID(); err != nil {
		log.Warn().
			Uint64("server_ts", envelope.GetServerTimestamp()).
			Uint64("client_ts", envelope.GetTimestamp()).
			Msg("Failed to get sender name as service ID")
		return fmt.Errorf("failed to get sender name as service ID: %w", err)
	} else if theirServiceID.Type != libsignalgo.ServiceIDTypeACI {
		log.Warn().
			Any("their_service_id", theirServiceID).
			Uint64("server_ts", envelope.GetServerTimestamp()).
			Uint64("client_ts", envelope.GetTimestamp()).
			Msg("Dropping message from non-ACI sender")
		return nil
	}
	cli.Store.RecipientStore.MarkUnregistered(ctx, theirServiceID, false)

	handlerSuccess := true
	defer func() {
		if retErr == nil && !handlerSuccess {
			retErr = ErrHandlerFailed
		}
	}()
	// result.Err is set if there was an error during decryption and we
	// should notifiy the user that the message could not be decrypted
	if result.Err != nil {
		if errors.Is(result.Err, EventAlreadyProcessed) {
			log.Debug().Err(result.Err).
				Bool("urgent", envelope.GetUrgent()).
				Stringer("content_hint", result.ContentHint).
				Uint64("server_ts", envelope.GetServerTimestamp()).
				Uint64("client_ts", envelope.GetTimestamp()).
				Stringer("sender", theirServiceID).
				Msg("Ignoring already processed event")
			return nil
		}
		log.Err(result.Err).
			Bool("urgent", envelope.GetUrgent()).
			Stringer("content_hint", result.ContentHint).
			Uint64("server_ts", envelope.GetServerTimestamp()).
			Uint64("client_ts", envelope.GetTimestamp()).
			Stringer("sender", theirServiceID).
			Msg("Decryption error with known sender")
		// Only send decryption error event if the message was urgent,
		// to prevent spamming errors for typing notifications and whatnot
		if envelope.GetUrgent() &&
			result.ContentHint != signalpb.UnidentifiedSenderMessage_Message_IMPLICIT &&
			!strings.Contains(result.Err.Error(), "message with old counter") {
			handlerSuccess = cli.handleEvent(&events.DecryptionError{
				Sender:    theirServiceID.UUID,
				Err:       result.Err,
				Timestamp: envelope.GetTimestamp(),
			})
		}
		if result.Retriable {
			go func() {
				err := cli.sendRetryRequest(ctx, result, envelope.GetTimestamp())
				if err != nil {
					log.Err(err).Msg("Failed to send retry request in background")
				}
			}()
		}
		if !handlerSuccess {
			return ErrHandlerFailed
		}
		return nil
	}

	content := result.Content
	if content == nil {
		log.Warn().Msg("Decrypted content is nil")
		return nil
	}

	deviceID, _ := result.SenderAddress.DeviceID()
	log.Trace().
		Any("raw_data", content).
		Stringer("sender", theirServiceID).
		Uint("sender_device", deviceID).
		Msg("Raw event data")
	newLog := log.With().
		Stringer("sender_name", theirServiceID).
		Uint("sender_device_id", deviceID).
		Str("destination_service_id", destinationServiceID.String()).
		Logger()
	log = &newLog
	ctx = log.WithContext(ctx)
	logEvt := log.Debug()
	if result.CiphertextHash != nil {
		logEvt = logEvt.Hex("ciphertext_hash", result.CiphertextHash[:])
	}
	logEvt.Bool("unencrypted", result.Unencrypted).Msg("Decrypted message")

	if content.DecryptionErrorMessage != nil {
		handlerSuccess = true
		dem, err := libsignalgo.DeserializeDecryptionErrorMessage(content.DecryptionErrorMessage)
		if err != nil {
			log.Warn().Err(err).Msg("Failed to unmarshal decryption error message")
		} else {
			go func() {
				err := cli.handleRetryRequest(ctx, result, dem)
				if err != nil {
					log.Err(err).Msg("Failed to handle decryption error message in background")
				}
			}()
		}
		return
	} else if result.Unencrypted {
		log.Warn().Msg("Unexpected non-decryption-error content in unencrypted message")
		return nil
	}

	// If there's a sender key distribution message, process it
	if content.GetSenderKeyDistributionMessage() != nil {
		log.Debug().Msg("content includes sender key distribution message")
		skdm, err := libsignalgo.DeserializeSenderKeyDistributionMessage(content.GetSenderKeyDistributionMessage())
		if err != nil {
			log.Err(err).Msg("DeserializeSenderKeyDistributionMessage error")
			return err
		}
		err = libsignalgo.ProcessSenderKeyDistributionMessage(
			ctx,
			skdm,
			result.SenderAddress,
			cli.Store.SenderKeyStore,
		)
		if err != nil {
			log.Err(err).Msg("ProcessSenderKeyDistributionMessage error")
			return err
		}
	}

	if destinationServiceID == cli.Store.PNIServiceID() {
		_, err = cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, theirServiceID.UUID, uuid.Nil, func(recipient *types.Recipient) (changed bool, err error) {
			if recipient.Whitelisted == nil {
				log.Debug().Msg("Marking recipient as not whitelisted")
				recipient.Whitelisted = ptr.Ptr(false)
				changed = true
			}
			if !recipient.NeedsPNISignature {
				log.Debug().Msg("Marking recipient as needing PNI signature")
				recipient.NeedsPNISignature = true
				changed = true
			}
			return
		})
		if err != nil {
			log.Err(err).Msg("Failed to set needs_pni_signature flag after receiving message to PNI service ID")
		}
	}

	if content.PniSignatureMessage != nil {
		log.Debug().Msg("Content includes PNI signature message")
		err = cli.handlePNISignatureMessage(ctx, theirServiceID, content.PniSignatureMessage)
		if err != nil {
			log.Err(err).
				Hex("pni_raw", content.PniSignatureMessage.GetPni()).
				Stringer("aci", theirServiceID.UUID).
				Msg("Failed to verify ACI-PNI mapping")
		}
	}

	if content.SyncMessage != nil && theirServiceID == cli.Store.ACIServiceID() {
		handlerSuccess = cli.handleSyncMessage(ctx, content.SyncMessage, envelope)
		return nil
	}

	isBlocked, err := cli.Store.RecipientStore.IsBlocked(ctx, theirServiceID.UUID)
	if err != nil {
		log.Err(err).Stringer("sender", theirServiceID).Msg("Failed to check if sender is blocked")
	}

	var sendDeliveryReceipt bool
	if content.DataMessage != nil {
		handlerSuccess, sendDeliveryReceipt = cli.incomingDataMessage(
			ctx, content.DataMessage, theirServiceID.UUID, theirServiceID, envelope.GetServerTimestamp(), isBlocked,
		)
	} else if content.EditMessage != nil {
		handlerSuccess, sendDeliveryReceipt = cli.incomingEditMessage(
			ctx, content.EditMessage, theirServiceID.UUID, theirServiceID, envelope.GetServerTimestamp(), isBlocked,
		)
	}
	if sendDeliveryReceipt && handlerSuccess {
		err = cli.sendDeliveryReceipts(ctx, []uint64{content.DataMessage.GetTimestamp()}, theirServiceID.UUID)
		if err != nil {
			log.Err(err).Msg("sendDeliveryReceipts error")
		}
	}

	if content.TypingMessage != nil && (!isBlocked || content.TypingMessage.GetGroupId() != nil) {
		var groupID types.GroupIdentifier
		if content.TypingMessage.GetGroupId() != nil {
			gidBytes := content.TypingMessage.GetGroupId()
			groupID = types.GroupIdentifier(base64.StdEncoding.EncodeToString(gidBytes))
		}
		// No handler success check here, nobody cares if typing notifications are dropped
		cli.handleEvent(&events.ChatEvent{
			Info: events.MessageInfo{
				Sender:          theirServiceID.UUID,
				ChatID:          groupOrUserID(groupID, theirServiceID),
				ServerTimestamp: envelope.GetServerTimestamp(),
			},
			Event: content.TypingMessage,
		})
	}

	// DM call message (group call is an opaque callMessage and a groupCallUpdate in a dataMessage)
	if content.CallMessage != nil && (content.CallMessage.Offer != nil || content.CallMessage.Hangup != nil) && !isBlocked {
		handlerSuccess = cli.handleEvent(&events.Call{
			Info: events.MessageInfo{
				Sender:          theirServiceID.UUID,
				ChatID:          theirServiceID.String(),
				ServerTimestamp: envelope.GetServerTimestamp(),
			},
			// CallMessage doesn't have its own timestamp, use one from the envelope
			Timestamp: envelope.GetTimestamp(),
			IsRinging: content.CallMessage.Offer != nil,
		}) && handlerSuccess
	}

	// Read and delivery receipts
	if content.ReceiptMessage != nil {
		if content.GetReceiptMessage().GetType() == signalpb.ReceiptMessage_DELIVERY && theirServiceID == cli.Store.ACIServiceID() {
			// Ignore delivery receipts from other own devices
			return nil
		}
		handlerSuccess = cli.handleEvent(&events.Receipt{
			Sender:  theirServiceID.UUID,
			Content: content.ReceiptMessage,
		}) && handlerSuccess
	}
	return nil
}

func groupOrUserID(groupID types.GroupIdentifier, userID libsignalgo.ServiceID) string {
	if groupID == "" {
		return userID.String()
	}
	return string(groupID)
}

func (cli *Client) handleSyncMessage(ctx context.Context, msg *signalpb.SyncMessage, envelope *signalpb.Envelope) (handlerSuccess bool) {
	// TODO: handle more sync messages
	handlerSuccess = true
	log := zerolog.Ctx(ctx)
	if msg.Keys != nil {
		aep := libsignalgo.AccountEntropyPool(msg.Keys.GetAccountEntropyPool())
		cli.Store.MasterKey = msg.Keys.GetMaster()
		if aep != "" {
			aepMasterKey, err := aep.DeriveSVRKey()
			if err != nil {
				log.Err(err).Msg("Failed to derive master key from account entropy pool")
			} else if cli.Store.MasterKey == nil {
				cli.Store.MasterKey = aepMasterKey
				log.Debug().Msg("Derived master key from account entropy pool (no master key in sync message)")
			} else if !bytes.Equal(aepMasterKey, cli.Store.MasterKey) {
				log.Warn().Msg("Derived master key doesn't match one in sync message")
			} else {
				log.Debug().Msg("Derived master key matches one in sync message")
			}
		} else {
			log.Debug().Msg("No account entropy pool in sync message")
		}
		err := cli.Store.DeviceStore.PutDevice(ctx, &cli.Store.DeviceData)
		if err != nil {
			log.Err(err).Msg("Failed to save device after receiving master key")
		} else {
			log.Info().Msg("Received master key")
			go cli.SyncStorage(ctx)
		}
	} else if msg.GetFetchLatest().GetType() == signalpb.SyncMessage_FetchLatest_STORAGE_MANIFEST {
		log.Debug().Msg("Received storage manifest fetch latest notice")
		go cli.SyncStorage(ctx)
	}
	syncSent := msg.GetSent()
	if syncSent.GetMessage() != nil || syncSent.GetEditMessage() != nil {
		destination := syncSent.DestinationServiceId
		var syncDestinationServiceID libsignalgo.ServiceID
		if destination != nil {
			var err error
			syncDestinationServiceID, err = libsignalgo.ServiceIDFromString(*destination)
			if err != nil {
				log.Err(err).Msg("Sync message destination parse error")
				return
			}
			if syncSent.GetDestinationE164() != "" {
				aci, pni := syncDestinationServiceID.ToACIAndPNI()
				_, err = cli.Store.RecipientStore.UpdateRecipientE164(ctx, aci, pni, syncSent.GetDestinationE164())
				if err != nil {
					log.Err(err).Msg("Failed to update recipient E164 after receiving sync message")
				}
			}
		}
		for _, unident := range syncSent.GetUnidentifiedStatus() {
			changed, err := cli.saveSyncPNIIdentityKey(ctx, unident.GetDestinationServiceId(), unident.GetDestinationPniIdentityKey())
			if err != nil {
				log.Err(err).
					Str("destination_service_id", unident.GetDestinationServiceId()).
					Msg("Failed to save PNI identity key from sync message")
			} else if changed {
				log.Debug().
					Str("destination_service_id", unident.GetDestinationServiceId()).
					Msg("Saved new PNI identity key from sync message")
			}
		}

		if destination == nil && syncSent.GetMessage().GetGroupV2() == nil && syncSent.GetEditMessage().GetDataMessage().GetGroupV2() == nil {
			log.Warn().Msg("sync message sent destination is nil")
		} else if msg.Sent.Message != nil {
			// TODO handle expiration start ts, and maybe the sync message ts?
			cli.incomingDataMessage(ctx, msg.Sent.Message, cli.Store.ACI, syncDestinationServiceID, envelope.GetServerTimestamp(), false)
		} else if msg.Sent.EditMessage != nil {
			cli.incomingEditMessage(ctx, msg.Sent.EditMessage, cli.Store.ACI, syncDestinationServiceID, envelope.GetServerTimestamp(), false)
		}
	}
	if msg.Contacts != nil {
		log.Debug().Msg("Recieved sync message contacts")
		blob := msg.Contacts.Blob
		if blob != nil {
			contactsBytes, err := DownloadAttachmentWithPointer(ctx, blob, nil)
			if err != nil {
				log.Err(err).Msg("Contacts Sync DownloadAttachment error")
			}
			// unmarshall contacts
			contacts, avatars, err := unmarshalContactDetailsMessages(contactsBytes)
			if err != nil {
				log.Err(err).Msg("Contacts Sync unmarshalContactDetailsMessages error")
			}
			log.Debug().Int("contact_count", len(contacts)).Msg("Contacts Sync received contacts")
			convertedContacts := make([]*types.Recipient, 0, len(contacts))
			err = cli.Store.DoContactTxn(ctx, func(ctx context.Context) error {
				for i, signalContact := range contacts {
					if signalContact.Aci == nil || *signalContact.Aci == "" {
						// TODO lookup PNI via CDSI and store that when ACI is missing?
						log.Info().
							Any("contact", signalContact).
							Msg("Signal Contact UUID is nil, skipping")
						continue
					}
					contact, err := cli.StoreContactDetailsAsContact(ctx, signalContact, &avatars[i])
					if err != nil {
						return err
					}
					convertedContacts = append(convertedContacts, contact)
				}
				return nil
			})
			if err != nil {
				log.Err(err).Msg("Error storing contacts")
			} else {
				handlerSuccess = cli.handleEvent(&events.ContactList{
					Contacts: convertedContacts,
				})
			}
		}
	}
	if msg.Read != nil {
		handlerSuccess = cli.handleEvent(&events.ReadSelf{
			Timestamp: envelope.GetTimestamp(),
			Messages:  msg.GetRead(),
		})
	}
	if msg.DeleteForMe != nil {
		handlerSuccess = cli.handleEvent(&events.DeleteForMe{
			Timestamp:               envelope.GetTimestamp(),
			SyncMessage_DeleteForMe: msg.DeleteForMe,
		})
	}
	if msg.MessageRequestResponse != nil {
		aciUUID, _ := uuid.Parse(msg.MessageRequestResponse.GetThreadAci())
		if aciUUID != uuid.Nil && msg.MessageRequestResponse.GetType() == signalpb.SyncMessage_MessageRequestResponse_ACCEPT {
			_, err := cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aciUUID, uuid.Nil, func(recipient *types.Recipient) (changed bool, err error) {
				changed = !ptr.Val(recipient.Whitelisted) || recipient.NeedsPNISignature
				recipient.Whitelisted = ptr.Ptr(true)
				recipient.NeedsPNISignature = false
				return
			})
			if err != nil {
				log.Err(err).Msg("Failed to clear needs_pni_signature flag after message request accept")
			}
		}
		var groupID *libsignalgo.GroupIdentifier
		if len(msg.MessageRequestResponse.GroupId) == libsignalgo.GroupIdentifierLength {
			groupID = (*libsignalgo.GroupIdentifier)(msg.MessageRequestResponse.GroupId)
		}
		handlerSuccess = cli.handleEvent(&events.MessageRequestResponse{
			Timestamp: envelope.GetTimestamp(),
			ThreadACI: aciUUID,
			GroupID:   groupID,
			Type:      msg.MessageRequestResponse.GetType(),
			Raw:       msg.MessageRequestResponse,
		})
	}
	return
}

func (cli *Client) saveSyncPNIIdentityKey(ctx context.Context, serviceIDString string, identityKeyBytes []byte) (bool, error) {
	if identityKeyBytes == nil {
		return false, nil
	}
	identityKey, err := libsignalgo.DeserializeIdentityKey(identityKeyBytes)
	if err != nil {
		return false, fmt.Errorf("failed to deserialize PNI identity key: %w", err)
	}
	serviceID, err := libsignalgo.ServiceIDFromString(serviceIDString)
	if err != nil {
		return false, fmt.Errorf("failed to parse PNI service ID: %w", err)
	} else if serviceID.Type != libsignalgo.ServiceIDTypePNI {
		return false, nil
	}
	changed, err := cli.Store.IdentityKeyStore.SaveIdentityKey(ctx, serviceID, identityKey)
	if err != nil {
		return false, fmt.Errorf("failed to save PNI identity key: %w", err)
	}
	return changed, nil
}

func (cli *Client) handlePNISignatureMessage(ctx context.Context, sender libsignalgo.ServiceID, msg *signalpb.PniSignatureMessage) error {
	if sender.Type != libsignalgo.ServiceIDTypeACI {
		return fmt.Errorf("PNI signature message sender is not an ACI")
	}
	pniBytes := msg.GetPni()
	if len(pniBytes) != 16 {
		return fmt.Errorf("unexpected PNI length %d (expected 16)", len(pniBytes))
	}
	pni := uuid.UUID(pniBytes)
	pniServiceID := libsignalgo.NewPNIServiceID(pni)
	pniIdentity, err := cli.Store.IdentityKeyStore.GetIdentityKey(ctx, pniServiceID)
	if err != nil {
		return fmt.Errorf("failed to get identity for PNI %s: %w", pni, err)
	} else if pniIdentity == nil {
		zerolog.Ctx(ctx).Debug().
			Stringer("aci", sender.UUID).
			Stringer("pni", pni).
			Msg("Fetching PNI identity for signature verification as it wasn't found in store")
		err = cli.FetchAndProcessPreKey(ctx, pniServiceID, 0)
		if err != nil {
			return fmt.Errorf("failed to fetch prekey for PNI %s after identity wasn't found in store: %w", pni, err)
		} else if pniIdentity, err = cli.Store.IdentityKeyStore.GetIdentityKey(ctx, pniServiceID); err != nil {
			return fmt.Errorf("failed to get identity for PNI %s after fetching: %w", pni, err)
		} else if pniIdentity == nil {
			return fmt.Errorf("identity not found for PNI %s even after fetching", pni)
		}
	}
	aciIdentity, err := cli.Store.IdentityKeyStore.GetIdentityKey(ctx, sender)
	if err != nil {
		return fmt.Errorf("failed to get identity for ACI %s: %w", sender, err)
	} else if aciIdentity == nil {
		return fmt.Errorf("identity not found for ACI %s", sender)
	}
	if ok, err := pniIdentity.VerifyAlternateIdentity(aciIdentity, msg.GetSignature()); err != nil {
		return fmt.Errorf("signature validation failed: %w", err)
	} else if !ok {
		return fmt.Errorf("signature is invalid")
	}
	zerolog.Ctx(ctx).Debug().
		Stringer("aci", sender.UUID).
		Stringer("pni", pni).
		Msg("Verified ACI-PNI mapping")
	_, err = cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, sender.UUID, pni, nil)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("Failed to update aci/pni mapping in store")
	}
	cli.handleEvent(&events.ACIFound{ACI: sender, PNI: pniServiceID})
	return nil
}

func (cli *Client) incomingEditMessage(
	ctx context.Context,
	editMessage *signalpb.EditMessage,
	messageSenderACI uuid.UUID,
	chatRecipient libsignalgo.ServiceID,
	serverTimestamp uint64,
	isBlocked bool,
) (handlerSuccess, sendDeliveryReceipt bool) {
	// If it's a group message, get the ID and invalidate cache if necessary
	var groupID types.GroupIdentifier
	var groupRevision uint32
	if editMessage.GetDataMessage().GetGroupV2() != nil {
		// Pull out the master key then store it ASAP - we should pass around GroupIdentifier
		groupMasterKeyBytes := editMessage.GetDataMessage().GetGroupV2().GetMasterKey()
		masterKey := masterKeyFromBytes(libsignalgo.GroupMasterKey(groupMasterKeyBytes))
		var err error
		groupID, err = cli.StoreMasterKey(ctx, masterKey)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("StoreMasterKey error")
			return
		}
		groupRevision = editMessage.GetDataMessage().GetGroupV2().GetRevision()
	} else if isBlocked {
		zerolog.Ctx(ctx).Debug().Msg("Dropping direct message from blocked user")
		return true, false
	}
	return cli.handleEvent(&events.ChatEvent{
		Info: events.MessageInfo{
			Sender:          messageSenderACI,
			ChatID:          groupOrUserID(groupID, chatRecipient),
			GroupRevision:   groupRevision,
			ServerTimestamp: serverTimestamp,
		},
		Event: editMessage,
	}), true
}

func (cli *Client) incomingDataMessage(
	ctx context.Context,
	dataMessage *signalpb.DataMessage,
	messageSenderACI uuid.UUID,
	chatRecipient libsignalgo.ServiceID,
	serverTimestamp uint64,
	isBlocked bool,
) (handlerSuccess, sendDeliveryReceipt bool) {
	// If there's a profile key, save it
	if dataMessage.ProfileKey != nil {
		profileKey := libsignalgo.ProfileKey(dataMessage.ProfileKey)
		err := cli.Store.RecipientStore.StoreProfileKey(ctx, messageSenderACI, profileKey)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("StoreProfileKey error")
			return
		}
	}

	// If it's a group message, get the ID and invalidate cache if necessary
	var groupID types.GroupIdentifier
	var groupRevision uint32
	if dataMessage.GetGroupV2() != nil {
		// Pull out the master key then store it ASAP - we should pass around GroupIdentifier
		groupMasterKeyBytes := dataMessage.GetGroupV2().GetMasterKey()
		masterKey := masterKeyFromBytes(libsignalgo.GroupMasterKey(groupMasterKeyBytes))
		var err error
		groupID, err = cli.StoreMasterKey(ctx, masterKey)
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("StoreMasterKey error")
			return
		}
		groupRevision = dataMessage.GetGroupV2().GetRevision()
	} else if isBlocked {
		zerolog.Ctx(ctx).Debug().Msg("Dropping direct message from blocked user")
		return true, false
	}

	evtInfo := events.MessageInfo{
		Sender:          messageSenderACI,
		ChatID:          groupOrUserID(groupID, chatRecipient),
		GroupRevision:   groupRevision,
		ServerTimestamp: serverTimestamp,
	}
	// Hacky special case for group calls to cache the state
	if dataMessage.GroupCallUpdate != nil {
		isRinging := cli.GroupCache.UpdateActiveCall(groupID, dataMessage.GroupCallUpdate.GetEraId())
		return cli.handleEvent(&events.Call{
			Info:      evtInfo,
			Timestamp: dataMessage.GetTimestamp(),
			IsRinging: isRinging,
		}), true
	} else {
		return cli.handleEvent(&events.ChatEvent{
			Info:  evtInfo,
			Event: dataMessage,
		}), true
	}
}

func (cli *Client) sendDeliveryReceipts(ctx context.Context, deliveredTimestamps []uint64, senderUUID uuid.UUID) error {
	// Send delivery receipts
	if len(deliveredTimestamps) > 0 {
		receipt := DeliveredReceiptMessageForTimestamps(deliveredTimestamps)
		result := cli.SendMessage(ctx, libsignalgo.NewACIServiceID(senderUUID), receipt)
		if !result.WasSuccessful {
			return fmt.Errorf("failed to send delivery receipts: %v", result)
		}
	}
	return nil
}
