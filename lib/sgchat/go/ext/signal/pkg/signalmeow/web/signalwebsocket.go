// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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

package web

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exsync"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/wspb"
)

var WebsocketPingInterval = 30 * time.Second
var WebsocketPingTimeout = 20 * time.Second
var WebsocketPingTimeoutLimit = 5

const WebsocketProvisioningPath = "/v1/websocket/provisioning/"
const WebsocketPath = "/v1/websocket/"

type SimpleResponse struct {
	Status        int
	WriteCallback func(time.Time)
}
type RequestHandlerFunc func(context.Context, *signalpb.WebSocketRequestMessage) (*SimpleResponse, error)

type SignalWebsocket struct {
	ws            atomic.Pointer[websocket.Conn]
	basicAuth     *url.Userinfo
	sendChannel   chan SignalWebsocketSendMessage
	statusChannel chan SignalWebsocketConnectionStatus
	closeLock     sync.RWMutex
	closeEvt      *exsync.Event
	closeCalled   atomic.Bool
	cancel        atomic.Pointer[context.CancelFunc]
	cancelConn    atomic.Pointer[context.CancelCauseFunc]
}

func NewSignalWebsocket(basicAuth *url.Userinfo) *SignalWebsocket {
	return &SignalWebsocket{
		basicAuth:     basicAuth,
		sendChannel:   make(chan SignalWebsocketSendMessage),
		statusChannel: make(chan SignalWebsocketConnectionStatus),
		closeEvt:      exsync.NewEvent(),
	}
}

type SignalWebsocketConnectionEvent int

const (
	SignalWebsocketConnectionEventConnecting SignalWebsocketConnectionEvent = iota // Implicit to catch default value (0), doesn't get sent
	SignalWebsocketConnectionEventConnected
	SignalWebsocketConnectionEventDisconnected
	SignalWebsocketConnectionEventLoggedOut
	SignalWebsocketConnectionEventError
	SignalWebsocketConnectionEventFatalError
	SignalWebsocketConnectionEventCleanShutdown
)

// mapping from SignalWebsocketConnectionEvent to its string representation
var signalWebsocketConnectionEventNames = map[SignalWebsocketConnectionEvent]string{
	SignalWebsocketConnectionEventConnecting:    "SignalWebsocketConnectionEventConnecting",
	SignalWebsocketConnectionEventConnected:     "SignalWebsocketConnectionEventConnected",
	SignalWebsocketConnectionEventDisconnected:  "SignalWebsocketConnectionEventDisconnected",
	SignalWebsocketConnectionEventLoggedOut:     "SignalWebsocketConnectionEventLoggedOut",
	SignalWebsocketConnectionEventError:         "SignalWebsocketConnectionEventError",
	SignalWebsocketConnectionEventFatalError:    "SignalWebsocketConnectionEventFatalError",
	SignalWebsocketConnectionEventCleanShutdown: "SignalWebsocketConnectionEventCleanShutdown",
}

// Implement the fmt.Stringer interface
func (s SignalWebsocketConnectionEvent) String() string {
	return signalWebsocketConnectionEventNames[s]
}

type SignalWebsocketConnectionStatus struct {
	Event SignalWebsocketConnectionEvent
	Err   error
}

func (s *SignalWebsocket) IsConnected() bool {
	return s.ws.Load() != nil
}

func (s *SignalWebsocket) Close() (err error) {
	if s == nil {
		return nil
	}

	s.closeCalled.Store(true)
	if ws := s.ws.Swap(nil); ws != nil {
		err = ws.Close(websocket.StatusNormalClosure, "")
	}
	if cancelLoop := s.cancel.Swap(nil); cancelLoop != nil {
		(*cancelLoop)()
	}
	<-s.closeEvt.GetChan()
	return err
}

func (s *SignalWebsocket) Connect(ctx context.Context, requestHandler RequestHandlerFunc) chan SignalWebsocketConnectionStatus {
	go s.connectLoop(ctx, requestHandler)
	return s.statusChannel
}

func (s *SignalWebsocket) pushStatus(ctx context.Context, status SignalWebsocketConnectionEvent, err error) {
	select {
	case s.statusChannel <- SignalWebsocketConnectionStatus{
		Event: status,
		Err:   err,
	}:
	case <-ctx.Done():
		return
	case <-time.After(5 * time.Second):
		zerolog.Ctx(ctx).Error().Msg("Status channel didn't accept status")
	}
}

func (s *SignalWebsocket) pushOutgoing(ctx context.Context, send SignalWebsocketSendMessage) error {
	if ctx.Err() != nil {
		return ctx.Err()
	}
	s.closeLock.RLock()
	defer s.closeLock.RUnlock()
	if s.sendChannel == nil {
		return errors.New("connection is not open")
	}
	select {
	case s.sendChannel <- send:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	case <-s.closeEvt.GetChan():
		return errors.New("connection closed before send could be queued")
	}
}

var ErrForcedReconnect = errors.New("forced reconnect")

func (s *SignalWebsocket) ForceReconnect() {
	if s == nil {
		return
	}
	cancelFn := s.cancelConn.Load()
	if cancelFn == nil {
		return
	}
	(*cancelFn)(ErrForcedReconnect)
}

func (s *SignalWebsocket) connectLoop(
	ctx context.Context,
	requestHandler RequestHandlerFunc,
) {
	log := zerolog.Ctx(ctx).With().
		Str("loop", "signal_websocket_connect_loop").
		Logger()
	ctx, cancel := context.WithCancel(ctx)
	s.cancel.Store(&cancel)

	incomingRequestChan := make(chan *signalpb.WebSocketRequestMessage, 256)
	defer func() {
		s.closeEvt.Set()
		cancel()

		s.closeLock.Lock()
		defer s.closeLock.Unlock()
		close(incomingRequestChan)
		close(s.statusChannel)
		close(s.sendChannel)
		incomingRequestChan = nil
		s.statusChannel = nil
		s.sendChannel = nil
	}()

	const initialBackoff = 10 * time.Second
	const backoffIncrement = 5 * time.Second
	const maxBackoff = 60 * time.Second

	if s.ws.Load() != nil {
		panic("Already connected")
	}

	// First set up request handler loop. This exists outside of the
	// connection loops because we want to maintain it across reconnections
	go func() {
		for {
			select {
			case <-ctx.Done():
				log.Info().Msg("ctx done, stopping request loop")
				return
			case request, ok := <-incomingRequestChan:
				if !ok {
					// Main connection loop must have closed, so we should stop
					log.Info().Msg("incomingRequestChan closed, stopping request loop")
					return
				}
				if request == nil {
					log.Fatal().Msg("Received nil request")
				}
				if requestHandler == nil {
					log.Fatal().Msg("Received request but no handler")
				}

				// Handle the request with the request handler function
				response, err := requestHandler(ctx, request)

				if err != nil {
					log.Err(err).Uint64("request_id", request.GetId()).Msg("Error handling request")
				} else if response != nil {
					err = s.pushOutgoing(ctx, SignalWebsocketSendMessage{
						RequestMessage:  request,
						ResponseMessage: response,
					})
					if err != nil {
						log.Err(err).Uint64("request_id", request.GetId()).Msg("Error queuing response message")
					}
				} else {
					log.Warn().Uint64("request_id", request.GetId()).Msg("Request handler didn't return a response nor an error")
				}
			}
		}
	}()

	// Main connection loop - if there's a problem with anything just
	// kill everything (including the websocket) and build it all up again
	backoff := initialBackoff
	retrying := false
	errorCount := 0
	isFirstConnect := true
	wsURL := (&url.URL{
		Scheme: "wss",
		Host:   APIHostname,
		Path:   WebsocketPath,
		User:   s.basicAuth,
	}).String()
	for {
		if retrying {
			if backoff > maxBackoff {
				backoff = maxBackoff
			}
			log.Warn().Dur("backoff", backoff).Msg("Failed to connect, waiting to retry...")
			select {
			case <-time.After(backoff):
			case <-ctx.Done():
			}
			backoff += backoffIncrement
		} else if !isFirstConnect && s.basicAuth != nil {
			select {
			case <-time.After(initialBackoff):
			case <-ctx.Done():
			}
		}
		if ctx.Err() != nil {
			log.Info().Msg("ctx done, stopping connection loop")
			return
		}
		isFirstConnect = false

		ws, resp, err := OpenWebsocket(ctx, wsURL)
		if resp != nil {
			if resp.StatusCode != 101 {
				// Server didn't want to open websocket
				if resp.StatusCode >= 500 {
					// We can try again if it's a 5xx
					s.pushStatus(ctx, SignalWebsocketConnectionEventDisconnected, fmt.Errorf("5xx opening websocket: %v", resp.Status))
				} else if resp.StatusCode == 403 {
					// We are logged out, so we should stop trying to reconnect
					s.pushStatus(ctx, SignalWebsocketConnectionEventLoggedOut, fmt.Errorf("403 opening websocket, we are logged out"))
					return // NOT RETRYING, KILLING THE CONNECTION LOOP
				} else if resp.StatusCode > 0 && resp.StatusCode < 500 {
					// Unexpected status code
					s.pushStatus(ctx, SignalWebsocketConnectionEventFatalError, fmt.Errorf("unexpected status opening websocket: %v", resp.Status))
					return // NOT RETRYING, KILLING THE CONNECTION LOOP
				} else {
					// Something is very wrong
					s.pushStatus(ctx, SignalWebsocketConnectionEventError, fmt.Errorf("unexpected error opening websocket: %v", resp.Status))
				}
				// Retry the connection
				retrying = true
				continue
			}
		}
		if err != nil {
			// Unexpected error opening websocket
			if backoff < maxBackoff {
				s.pushStatus(ctx, SignalWebsocketConnectionEventDisconnected, fmt.Errorf("transient error opening websocket: %w", err))
			} else {
				s.pushStatus(ctx, SignalWebsocketConnectionEventError, fmt.Errorf("continuing error opening websocket: %w", err))
			}
			retrying = true
			continue
		}

		// Succssfully connected
		s.pushStatus(ctx, SignalWebsocketConnectionEventConnected, nil)
		s.ws.Store(ws)
		retrying = false
		backoff = initialBackoff

		responseChannels := exsync.NewMap[uint64, chan *signalpb.WebSocketResponseMessage]()
		loopCtx, loopCancel := context.WithCancelCause(ctx)
		s.cancelConn.Store(&loopCancel)
		var wg sync.WaitGroup
		wg.Add(3)

		// Read loop (for reading incoming reqeusts and responses to outgoing requests)
		go func() {
			defer wg.Done()
			err := readLoop(loopCtx, ws, incomingRequestChan, responseChannels)
			// Don't want to put an err into loopCancel if we don't have one
			if err != nil {
				err = fmt.Errorf("error in readLoop: %w", err)
			}
			if s.closeCalled.Load() {
				// Exit during Close() so cancel the reconnect loop as well
				cancel()
			}
			loopCancel(err)
			log.Info().Msg("readLoop exited")
		}()

		// Write loop (for sending outgoing requests and responses to incoming requests)
		go func() {
			defer wg.Done()
			err := writeLoop(loopCtx, ws, s.sendChannel, responseChannels)
			// Don't want to put an err into loopCancel if we don't have one
			if err != nil {
				err = fmt.Errorf("error in writeLoop: %w", err)
			}
			loopCancel(err)
			log.Info().Msg("writeLoop exited")
		}()

		// Ping loop (send a keepalive Ping every 30s)
		go func() {
			defer wg.Done()
			ticker := time.NewTicker(WebsocketPingInterval)
			defer ticker.Stop()

			pingTimeoutCount := 0
			for {
				select {
				case <-ticker.C:
					pingCtx, cancel := context.WithTimeout(loopCtx, WebsocketPingTimeout)
					err := ws.Ping(pingCtx)
					cancel()
					if err != nil {
						pingTimeoutCount++
						log.Err(err).Msg("Failed to send ping")
						if pingTimeoutCount >= WebsocketPingTimeoutLimit {
							log.Warn().Msg("Ping timeout count exceeded, closing websocket")
							err = ws.Close(websocket.StatusNormalClosure, "Ping timeout")
							if err != nil {
								log.Err(err).Msg("Error closing websocket after ping timeout")
							}
							return
						}
					} else if pingTimeoutCount > 0 {
						pingTimeoutCount = 0
						log.Debug().Msg("Recovered from ping error")
					} else {
						log.Trace().Msg("Sent keepalive")
					}
				case <-loopCtx.Done():
					return
				}
			}
		}()

		// Wait for read or write or ping loop to exit (which means there was an error)
		log.Debug().Msg("Finished preparing connection, waiting for loop context to finish")
		<-loopCtx.Done()
		ctxCauseErr := context.Cause(loopCtx)
		log.Debug().AnErr("ctx_cause_err", ctxCauseErr).Msg("Read or write loop exited")
		if ctxCauseErr == nil || errors.Is(ctxCauseErr, context.Canceled) {
			s.pushStatus(ctx, SignalWebsocketConnectionEventCleanShutdown, nil)
		} else {
			errorCount++
			s.pushStatus(ctx, SignalWebsocketConnectionEventDisconnected, ctxCauseErr)
			if errors.Is(ctxCauseErr, ErrForcedReconnect) {
				// Skip the delay for forced reconnects
				// TODO should the delay be lowered globally?
				isFirstConnect = true
			}
		}

		// Clean up
		ws.Close(websocket.StatusGoingAway, "Going away")
		for _, responseChannel := range responseChannels.SwapData(nil) {
			close(responseChannel)
		}
		loopCancel(nil)
		wg.Wait()
		log.Debug().Msg("Finished websocket cleanup")
		if errorCount > 500 {
			// Something is really wrong, we better panic.
			// This is a last defense against a runaway error loop,
			// like the WS continually closing and reconnecting
			log.Fatal().Int("error_count", errorCount).Msg("Too many errors, panicking")
		}
	}
}

func readLoop(
	ctx context.Context,
	ws *websocket.Conn,
	incomingRequestChan chan *signalpb.WebSocketRequestMessage,
	responseChannels *exsync.Map[uint64, chan *signalpb.WebSocketResponseMessage],
) error {
	log := zerolog.Ctx(ctx).With().
		Str("loop", "signal_websocket_read_loop").
		Logger()
	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		msg := &signalpb.WebSocketMessage{}
		//ctx, _ := context.WithTimeout(ctx, 10*time.Second) // For testing
		err := wspb.Read(ctx, ws, msg)
		if err != nil {
			if err == context.Canceled {
				log.Info().Msg("readLoop context canceled")
			} else if websocket.CloseStatus(err) == websocket.StatusNormalClosure {
				log.Info().Msg("readLoop received StatusNormalClosure")
				return nil
			}
			return fmt.Errorf("error reading message: %w", err)
		}
		if msg.Type == nil {
			return errors.New("received message with no type")
		} else if *msg.Type == signalpb.WebSocketMessage_REQUEST {
			if msg.Request == nil {
				return errors.New("received request message with no request")
			}
			log.Trace().
				Uint64("request_id", *msg.Request.Id).
				Str("request_verb", *msg.Request.Verb).
				Str("request_path", *msg.Request.Path).
				Msg("Received WS request")
			incomingRequestChan <- msg.Request
		} else if *msg.Type == signalpb.WebSocketMessage_RESPONSE {
			if msg.Response == nil {
				log.Fatal().Msg("Received response with no response")
			}
			if msg.Response.Id == nil {
				log.Fatal().Msg("Received response with no id")
			}
			responseChannel, ok := responseChannels.Pop(*msg.Response.Id)
			if !ok {
				log.Warn().
					Uint64("response_id", *msg.Response.Id).
					Msg("Received response with unknown id")
				continue
			}
			logEvt := log.Debug().
				Uint64("response_id", msg.Response.GetId()).
				Uint32("response_status", msg.Response.GetStatus()).
				Str("response_message", msg.Response.GetMessage())
			if log.GetLevel() == zerolog.TraceLevel || len(msg.Response.Body) < 256 {
				logEvt.Strs("response_headers", msg.Response.Headers)
				if json.Valid(msg.Response.Body) {
					logEvt.RawJSON("response_body", msg.Response.Body)
				} else {
					logEvt.Str("response_body", base64.StdEncoding.EncodeToString(msg.Response.Body))
				}
			}
			logEvt.Msg("Received WS response")
			responseChannel <- msg.Response
			close(responseChannel)
		} else if *msg.Type == signalpb.WebSocketMessage_UNKNOWN {
			return fmt.Errorf("received message with unknown type: %v", *msg.Type)
		} else {
			return fmt.Errorf("received message with actually unknown type: %v", *msg.Type)
		}
	}
}

type SignalWebsocketSendMessage struct {
	// Populate if we're sending a request:
	RequestTime     time.Time
	ResponseChannel chan *signalpb.WebSocketResponseMessage
	// Populate if we're sending a response:
	ResponseMessage *SimpleResponse
	// Populate this for request AND response
	RequestMessage *signalpb.WebSocketRequestMessage
}

func writeLoop(
	ctx context.Context,
	ws *websocket.Conn,
	sendChannel chan SignalWebsocketSendMessage,
	responseChannels *exsync.Map[uint64, chan *signalpb.WebSocketResponseMessage],
) error {
	log := zerolog.Ctx(ctx).With().
		Str("loop", "signal_websocket_write_loop").
		Logger()
	for i := uint64(1); ; i++ {
		select {
		case <-ctx.Done():
			if ctx.Err() != nil && ctx.Err() != context.Canceled {
				return ctx.Err()
			}
			return nil
		case request, ok := <-sendChannel:
			if !ok {
				return errors.New("send channel closed")
			}
			if request.RequestMessage != nil && request.ResponseChannel != nil {
				msgType := signalpb.WebSocketMessage_REQUEST
				message := &signalpb.WebSocketMessage{
					Type:    &msgType,
					Request: request.RequestMessage,
				}
				request.RequestMessage.Id = &i
				responseChannels.Set(i, request.ResponseChannel)
				if !request.RequestTime.IsZero() {
					elapsed := time.Since(request.RequestTime)
					if elapsed > 1*time.Minute {
						return fmt.Errorf("request too old (%v), not sending", elapsed)
					} else if elapsed > 10*time.Second {
						log.Warn().
							Uint64("request_id", i).
							Str("request_verb", *request.RequestMessage.Verb).
							Str("request_path", *request.RequestMessage.Path).
							Dur("elapsed", elapsed).
							Msg("Sending WS request")
					} else {
						log.Debug().
							Uint64("request_id", i).
							Str("request_verb", *request.RequestMessage.Verb).
							Str("request_path", *request.RequestMessage.Path).
							Dur("elapsed", elapsed).
							Msg("Sending WS request")
					}
				}
				err := wspb.Write(ctx, ws, message)
				if err != nil {
					if ctx.Err() != nil && ctx.Err() != context.Canceled {
						return ctx.Err()
					}
					return fmt.Errorf("error writing request message: %w", err)
				}
			} else if request.RequestMessage != nil && request.ResponseMessage != nil {
				message := CreateWSResponse(ctx, *request.RequestMessage.Id, request.ResponseMessage.Status)
				log.Debug().
					Uint64("request_id", *request.RequestMessage.Id).
					Int("response_status", request.ResponseMessage.Status).
					Msg("Sending WS response")
				writeStartTime := time.Now()
				err := wspb.Write(ctx, ws, message)
				if err != nil {
					return fmt.Errorf("error writing response message: %w", err)
				}
				if request.ResponseMessage.WriteCallback != nil {
					request.ResponseMessage.WriteCallback(writeStartTime)
				}
			} else {
				return fmt.Errorf("invalid request: %+v", request)
			}
		}
	}
}

func (s *SignalWebsocket) SendRequest(
	ctx context.Context,
	method,
	path string,
	body []byte,
	headers http.Header,
) (*signalpb.WebSocketResponseMessage, error) {
	if s == nil {
		return nil, errors.New("websocket is nil")
	}
	headerArray := make([]string, len(headers))
	var hasContentType bool
	for key, values := range headers {
		if strings.ToLower(key) == "content-type" {
			hasContentType = true
		}
		for _, value := range values {
			headerArray = append(headerArray, fmt.Sprintf("%s:%s", strings.ToLower(key), value))
		}
	}
	if !hasContentType && body != nil {
		headerArray = append(headerArray, "content-type:application/json")
	}
	return s.sendRequestInternal(ctx, &signalpb.WebSocketRequestMessage{
		Verb:    &method,
		Path:    &path,
		Body:    body,
		Headers: headerArray,
	}, time.Now(), 0)
}

func (s *SignalWebsocket) sendRequestInternal(
	ctx context.Context,
	request *signalpb.WebSocketRequestMessage,
	startTime time.Time,
	retryCount int,
) (*signalpb.WebSocketResponseMessage, error) {
	if s.basicAuth != nil {
		request.Headers = append(request.Headers, "authorization:Basic "+s.basicAuth.String())
	}
	responseChannel := make(chan *signalpb.WebSocketResponseMessage, 1)
	err := s.pushOutgoing(ctx, SignalWebsocketSendMessage{
		RequestMessage:  request,
		ResponseChannel: responseChannel,
		RequestTime:     startTime,
	})
	if err != nil {
		return nil, err
	}
	response := <-responseChannel

	isSelfDelete := request.GetVerb() == http.MethodDelete && strings.HasPrefix(request.GetPath(), "/v1/devices/")
	if response == nil && !isSelfDelete {
		// If out of retries, return error no matter what
		if retryCount >= 3 {
			// TODO: I think error isn't getting passed in this context (as it's not the one in writeLoop)
			if ctx.Err() != nil {
				return nil, fmt.Errorf("retried 3 times, giving up: %w", ctx.Err())
			} else {
				return nil, errors.New("retried 3 times, giving up")
			}
		}
		if ctx.Err() != nil {
			// if error contains "Took too long" don't retry
			if strings.Contains(ctx.Err().Error(), "Took too long") {
				return nil, ctx.Err()
			}
		}
		zerolog.Ctx(ctx).Warn().Int("retry_count", retryCount).Msg("Received nil response, retrying recursively")
		return s.sendRequestInternal(ctx, request, startTime, retryCount+1)
	}
	return response, nil
}

func OpenWebsocket(ctx context.Context, url string) (*websocket.Conn, *http.Response, error) {
	opt := &websocket.DialOptions{
		HTTPClient: SignalHTTPClient,
		HTTPHeader: make(http.Header, 2),
	}
	opt.HTTPHeader.Set("User-Agent", UserAgent)
	opt.HTTPHeader.Set("X-Signal-Agent", SignalAgent)
	ws, resp, err := websocket.Dial(ctx, url, opt)
	if ws != nil {
		ws.SetReadLimit(1 << 20) // Increase read limit to 1MB from default of 32KB
	}
	return ws, resp, err
}

func CreateWSResponse(ctx context.Context, id uint64, status int) *signalpb.WebSocketMessage {
	if status != 200 && status != 400 {
		// TODO support more responses to Signal? Are there more?
		zerolog.Ctx(ctx).Fatal().Int("status", status).Msg("Error creating response. Non 200/400 not supported yet.")
		return nil
	}
	msg_type := signalpb.WebSocketMessage_RESPONSE
	message := "OK"
	if status == 400 {
		message = "Unknown"
	}
	status32 := uint32(status)
	response := &signalpb.WebSocketMessage{
		Type: &msg_type,
		Response: &signalpb.WebSocketResponseMessage{
			Id:      &id,
			Message: &message,
			Status:  &status32,
			Headers: []string{},
		},
	}
	return response
}

func CreateWSRequest(method string, path string, body []byte, username *string, password *string) *signalpb.WebSocketRequestMessage {
	request := &signalpb.WebSocketRequestMessage{
		Verb: &method,
		Path: &path,
		Body: body,
	}
	request.Headers = []string{}
	request.Headers = append(request.Headers, "content-type:application/json; charset=utf-8")
	if username != nil && password != nil {
		basicAuth := base64.StdEncoding.EncodeToString([]byte(*username + ":" + *password))
		request.Headers = append(request.Headers, "authorization:Basic "+basicAuth)
	}
	return request
}
