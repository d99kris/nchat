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
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"net/url"
	"path"
	"sync"
	"time"

	"github.com/coder/websocket"
	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"github.com/tidwall/gjson"
	"go.mau.fi/util/exerrors"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

const ProdContactDiscoveryServer = "cdsi.signal.org"
const ProdContactDiscoveryMrenclave = "ee9503070127120074612b6688e593b67e486b1541449f54d71e387484eb40a3"
const ContactDiscoveryAuthTTL = 23 * time.Hour

const rateLimitCloseCode = websocket.StatusCode(4008)

var prodContactDiscoveryMrenclaveBytes = exerrors.Must(hex.DecodeString(ProdContactDiscoveryMrenclave))

type ContactDiscoveryRateLimitError struct {
	RetryAfter time.Duration
}

func (cdrle ContactDiscoveryRateLimitError) Error() string {
	return fmt.Sprintf("contact discovery rate limited for %s", cdrle.RetryAfter)
}

type ContactDiscoveryClient struct {
	CDS *libsignalgo.SGXClientState
	WS  *websocket.Conn

	Token     []byte
	Response  ContactDiscoveryResponse
	stateLock sync.Mutex
}

type ContactDiscoveryResponse map[uint64]CDSResponseEntry

type CDSResponseEntry struct {
	ACI uuid.UUID
	PNI uuid.UUID
}

func (cli *Client) LookupPhone(ctx context.Context, e164s ...uint64) (ContactDiscoveryResponse, error) {
	if len(e164s) == 0 {
		return nil, nil
	}
	requestData := make([]byte, len(e164s)*8)
	for i, e164 := range e164s {
		binary.BigEndian.PutUint64(requestData[i*8:(i+1)*8], e164)
	}
	ctx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	resp, token, err := cli.doContactDiscovery(ctx, &signalpb.CDSClientRequest{
		// TODO figure out if tokens are useful
		//      (it's meant for old_e164s)
		//Token:    cli.cdToken,
		NewE164S: requestData,
	})
	if token != nil {
		cli.cdToken = token
	}
	return resp, err
}

func (cli *Client) doContactDiscovery(ctx context.Context, req *signalpb.CDSClientRequest) (ContactDiscoveryResponse, []byte, error) {
	creds, err := cli.getContactDiscoveryCredentials(ctx)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to fetch contact discovery auth: %w", err)
	}
	log := zerolog.Ctx(ctx).With().
		Str("websocket_type", "contact").
		Str("username", creds.Username).
		Logger()
	log.Trace().Any("creds", creds).Msg("Got contact discovery credentials")
	ctx = log.WithContext(ctx)
	addr := (&url.URL{
		Scheme: "wss",
		Host:   ProdContactDiscoveryServer,
		User:   url.UserPassword(creds.Username, creds.Password),
		Path:   path.Join("v1", ProdContactDiscoveryMrenclave, "discovery"),
	}).String()
	log.Trace().Msg("Connecting to contact discovery websocket")
	ws, _, err := web.OpenWebsocket(ctx, addr)
	if err != nil {
		var closeErr websocket.CloseError
		if errors.As(err, &closeErr) && closeErr.Code == rateLimitCloseCode {
			retryAfter := gjson.Get(closeErr.Reason, "retry_after")
			if retryAfter.Type == gjson.Number {
				retryAfterDuration := time.Duration(retryAfter.Int()) * time.Second
				return nil, nil, ContactDiscoveryRateLimitError{RetryAfter: retryAfterDuration}
			}
		}
		return nil, nil, fmt.Errorf("failed to open contact discovery websocket: %w", err)
	}
	defer func() {
		_ = ws.CloseNow()
	}()
	cdc := &ContactDiscoveryClient{
		WS: ws,
	}
	log.Trace().Msg("Doing contact discovery websocket handshake")
	err = cdc.Handshake(ctx)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to handshake with contact discovery server: %w", err)
	}
	log.Trace().Msg("Contact discovery websocket connected")
	err = cdc.SendRequest(ctx, req)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to send contact discovery request: %w", err)
	}
	log.Trace().Any("request", req).Msg("Contact discovery request sent")
	err = cdc.ReadResponse(ctx)
	if err != nil {
		return nil, nil, err
	}
	log.Trace().Any("response", cdc.Response).Msg("Contact discovery response received")
	err = cdc.WS.Close(3000, "Normal")
	if err != nil {
		log.Trace().Err(err).Msg("Error closing contact discovery websocket cleanly")
	}
	return cdc.Response, cdc.Token, nil
}

func (cdc *ContactDiscoveryClient) Handshake(ctx context.Context) error {
	msgType, attestationMsg, err := cdc.WS.Read(ctx)
	if err != nil {
		return fmt.Errorf("failed to read attestation message: %w", err)
	} else if msgType != websocket.MessageBinary {
		return fmt.Errorf("expected binary message, got %s", msgType.String())
	}
	cdsClient, err := libsignalgo.NewCDS2ClientState(prodContactDiscoveryMrenclaveBytes, attestationMsg, time.Now())
	if err != nil {
		return fmt.Errorf("failed to initialize CDS2 client state: %w", err)
	}
	initReq, err := cdsClient.InitialRequest()
	if err != nil {
		return fmt.Errorf("failed to generate initial request: %w", err)
	}
	err = cdc.WS.Write(ctx, websocket.MessageBinary, initReq)
	if err != nil {
		return fmt.Errorf("failed to write initial request: %w", err)
	}
	msgType, handshakeFinishMsg, err := cdc.WS.Read(ctx)
	if err != nil {
		return fmt.Errorf("failed to read handshake finish message: %w", err)
	} else if msgType != websocket.MessageBinary {
		return fmt.Errorf("expected binary message, got %s", msgType.String())
	}
	err = cdsClient.CompleteHandshake(handshakeFinishMsg)
	if err != nil {
		return fmt.Errorf("failed to complete handshake: %w", err)
	}
	cdc.CDS = cdsClient
	return nil
}

func (cdc *ContactDiscoveryClient) SendRequest(ctx context.Context, req *signalpb.CDSClientRequest) error {
	plaintext, err := proto.Marshal(req)
	if err != nil {
		return fmt.Errorf("failed to marshal request: %w", err)
	}
	ciphertext, err := cdc.CDS.EstablishedSend(plaintext)
	if err != nil {
		return fmt.Errorf("failed to encrypt request: %w", err)
	}
	err = cdc.WS.Write(ctx, websocket.MessageBinary, ciphertext)
	if err != nil {
		return fmt.Errorf("failed to write request: %w", err)
	}
	return nil
}

func (cdc *ContactDiscoveryClient) ReadResponse(ctx context.Context) error {
	for cdc.Response == nil {
		msgType, msg, err := cdc.WS.Read(ctx)
		if err != nil {
			return fmt.Errorf("failed to read contact discovery message: %w", err)
		} else if msgType != websocket.MessageBinary {
			return fmt.Errorf("unexpected contact discovery message type: %w", err)
		}
		err = cdc.handleResponse(ctx, msg)
		if err != nil {
			return fmt.Errorf("failed to handle contact discovery message: %w", err)
		}
	}
	return nil
}

func (cdc *ContactDiscoveryClient) handleResponse(ctx context.Context, msg []byte) error {
	decrypted, err := cdc.CDS.EstablishedReceive(msg)
	if err != nil {
		return fmt.Errorf("failed to decrypt message: %w", err)
	}
	var cdsClientResp signalpb.CDSClientResponse
	err = proto.Unmarshal(decrypted, &cdsClientResp)
	if err != nil {
		return fmt.Errorf("failed to unmarshal message: %w", err)
	}
	if cdsClientResp.Token != nil {
		cdc.Token = cdsClientResp.Token
		err = cdc.SendRequest(ctx, &signalpb.CDSClientRequest{
			TokenAck: proto.Bool(true),
		})
		if err != nil {
			return fmt.Errorf("failed to send token ack request: %w", err)
		}
	}
	if cdsClientResp.E164PniAciTriples != nil {
		const tripleSize = 8 + 16 + 16
		triples := cdsClientResp.E164PniAciTriples
		pairCount := len(triples) / tripleSize
		if pairCount*tripleSize != len(triples) {
			return fmt.Errorf("invalid response size %d (not divisible by 40)", len(triples))
		}
		resp := make(ContactDiscoveryResponse, pairCount)
		for i := 0; i < pairCount; i++ {
			data := triples[i*tripleSize : (i+1)*tripleSize]
			e164 := binary.BigEndian.Uint64(data[:8])
			pni := uuid.UUID(data[8:24])
			aci := uuid.UUID(data[24:40])
			// If some entries were not found, the server will return all zeros
			if e164 != 0 {
				resp[e164] = CDSResponseEntry{PNI: pni, ACI: aci}
			}
		}
		cdc.Response = resp
	}
	return nil
}
