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

package signalmeow

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	mrand "math/rand/v2"
	"net/http"
	"net/url"
	"time"

	"github.com/coder/websocket"
	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exerrors"
	"go.mau.fi/util/random"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/signalpb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/wspb"
)

type ConfirmDeviceResponse struct {
	ACI      uuid.UUID `json:"uuid"`
	PNI      uuid.UUID `json:"pni,omitempty"`
	DeviceID int       `json:"deviceId"`
}

type ProvisioningState int

const (
	StateProvisioningError ProvisioningState = iota
	StateProvisioningURLReceived
	StateProvisioningDataReceived
)

func (s ProvisioningState) String() string {
	switch s {
	case StateProvisioningError:
		return "StateProvisioningError"
	case StateProvisioningURLReceived:
		return "StateProvisioningURLReceived"
	case StateProvisioningDataReceived:
		return "StateProvisioningDataReceived"
	default:
		return fmt.Sprintf("ProvisioningState(%d)", s)
	}
}

// Enum for the provisioningUrl, ProvisioningMessage, and error
type ProvisioningResponse struct {
	State            ProvisioningState
	ProvisioningURL  string
	ProvisioningData *store.DeviceData
	Err              error
}

type DeviceLinkError struct {
	StatusCode int
	Message    string
}

func (dle DeviceLinkError) Error() string {
	return fmt.Sprintf("non-200 status code (%d) from devices response: %s", dle.StatusCode, dle.Message)
}

func PerformProvisioning(ctx context.Context, deviceStore store.DeviceStore, deviceName string, allowBackup bool) chan ProvisioningResponse {
	log := zerolog.Ctx(ctx).With().Str("action", "perform provisioning").Logger()
	ctx = log.WithContext(ctx)
	c := make(chan ProvisioningResponse, 4)
	go func() {
		defer close(c)

		timeoutCtx, cancel := context.WithTimeout(ctx, 2*time.Minute)
		defer cancel()
		ws, resp, err := web.OpenWebsocket(timeoutCtx, (&url.URL{
			Scheme: "wss",
			Host:   web.APIHostname,
			Path:   web.WebsocketProvisioningPath,
		}).String())
		if err != nil {
			log.Err(err).Any("resp", resp).Msg("Failed to open websocket for provisioning")
			c <- ProvisioningResponse{State: StateProvisioningError, Err: err}
			return
		}
		defer ws.Close(websocket.StatusInternalError, "Websocket StatusInternalError")
		provisioningCipher := NewProvisioningCipher()

		provisioningURL, err := startProvisioning(timeoutCtx, ws, provisioningCipher, allowBackup)
		if err != nil {
			log.Err(err).Msg("Failed to send provisioning start request")
			c <- ProvisioningResponse{State: StateProvisioningError, Err: err}
			return
		}
		c <- ProvisioningResponse{State: StateProvisioningURLReceived, ProvisioningURL: provisioningURL, Err: err}

		provisioningMessage, err := continueProvisioning(timeoutCtx, ws, provisioningCipher)
		if err != nil {
			log.Err(err).Msg("Failed to send provisioning continue request")
			c <- ProvisioningResponse{State: StateProvisioningError, Err: err}
			return
		}
		_ = ws.Close(websocket.StatusNormalClosure, "")

		data, err := finishProvisioning(ctx, deviceStore, provisioningMessage, deviceName)
		if err != nil {
			log.Err(err).Msg("Failed to finish provisioning")
			c <- ProvisioningResponse{State: StateProvisioningError, Err: err}
		} else {
			c <- ProvisioningResponse{State: StateProvisioningDataReceived, ProvisioningData: data}
		}
	}()
	return c
}

func finishProvisioning(
	ctx context.Context,
	deviceStore store.DeviceStore,
	provisioningMessage *signalpb.ProvisionMessage,
	deviceName string,
) (*store.DeviceData, error) {
	aciPublicKey, err := libsignalgo.DeserializePublicKey(provisioningMessage.GetAciIdentityKeyPublic())
	if err != nil {
		return nil, fmt.Errorf("failed to deserialize aci public key: %w", err)
	}
	aciPrivateKey, err := libsignalgo.DeserializePrivateKey(provisioningMessage.GetAciIdentityKeyPrivate())
	if err != nil {
		return nil, fmt.Errorf("failed to deserialize aci private key: %w", err)
	}
	aciIdentityKeyPair, err := libsignalgo.NewIdentityKeyPair(aciPublicKey, aciPrivateKey)
	if err != nil {
		return nil, fmt.Errorf("failed to create aci identity key pair: %w", err)
	}
	profileKey := libsignalgo.ProfileKey(provisioningMessage.GetProfileKey())

	password := random.String(22)
	code := provisioningMessage.ProvisioningCode
	aciRegistrationID := mrand.IntN(16383) + 1
	aciSignedPreKey := GenerateSignedPreKey(1, aciIdentityKeyPair)
	aciPQLastResortPreKey := GenerateKyberPreKeys(1, 1, aciIdentityKeyPair)[0]
	var pniIdentityKeyPair *libsignalgo.IdentityKeyPair
	var pniRegistrationID int
	var pniSignedPreKey *libsignalgo.SignedPreKeyRecord
	var pniPQLastResortPreKey *libsignalgo.KyberPreKeyRecord
	var hasE164 bool
	aci, err := ParseStringOrBinaryServiceID(provisioningMessage.GetAci(), provisioningMessage.GetAciBinary())
	if err != nil {
		return nil, fmt.Errorf("failed to parse aci: %w", err)
	}
	pni, _ := ParseStringOrBinaryUUID(provisioningMessage.GetPni(), provisioningMessage.GetPniBinary())
	if pni != uuid.Nil {
		hasE164 = true

		pniPublicKey, err := libsignalgo.DeserializePublicKey(provisioningMessage.GetPniIdentityKeyPublic())
		if err != nil {
			return nil, fmt.Errorf("failed to deserialize pni public key: %w", err)
		}
		pniPrivateKey, err := libsignalgo.DeserializePrivateKey(provisioningMessage.GetPniIdentityKeyPrivate())
		if err != nil {
			return nil, fmt.Errorf("failed to deserialize pni private key: %w", err)
		}
		pniIdentityKeyPair, err = libsignalgo.NewIdentityKeyPair(pniPublicKey, pniPrivateKey)
		if err != nil {
			return nil, fmt.Errorf("failed to create pni identity key pair: %w", err)
		}

		pniRegistrationID = mrand.IntN(16383) + 1
		pniSignedPreKey = GenerateSignedPreKey(1, pniIdentityKeyPair)
		pniPQLastResortPreKey = GenerateKyberPreKeys(1, 1, pniIdentityKeyPair)[0]
	}
	zerolog.Ctx(ctx).Trace().Any("msg", provisioningMessage).Msg("Provisioning message")
	deviceResponse, err := confirmDevice(
		ctx,
		aci.String(),
		password,
		*code,
		hasE164,
		aciRegistrationID,
		pniRegistrationID,
		aciSignedPreKey,
		pniSignedPreKey,
		aciPQLastResortPreKey,
		pniPQLastResortPreKey,
		aciIdentityKeyPair,
		deviceName,
	)
	if err != nil {
		return nil, fmt.Errorf("failed to confirm device: %w", err)
	}

	deviceId := 1
	if deviceResponse.DeviceID != 0 {
		deviceId = deviceResponse.DeviceID
	}

	data := &store.DeviceData{
		ACIIdentityKeyPair: aciIdentityKeyPair,
		PNIIdentityKeyPair: pniIdentityKeyPair,
		ACIRegistrationID:  aciRegistrationID,
		PNIRegistrationID:  pniRegistrationID,
		ACI:                deviceResponse.ACI,
		PNI:                deviceResponse.PNI,
		DeviceID:           deviceId,
		Number:             provisioningMessage.GetNumber(),
		Password:           password,
		AccountEntropyPool: libsignalgo.AccountEntropyPool(provisioningMessage.GetAccountEntropyPool()),
		EphemeralBackupKey: libsignalgo.BytesToBackupKey(provisioningMessage.GetEphemeralBackupKey()),
		MediaRootBackupKey: libsignalgo.BytesToBackupKey(provisioningMessage.GetMediaRootBackupKey()),
		AuthCredentialSalt: provisioningMessage.GetAuthCredentialSalt(),
	}
	if provisioningMessage.GetAccountEntropyPool() != "" {
		data.MasterKey, err = libsignalgo.AccountEntropyPool(provisioningMessage.GetAccountEntropyPool()).DeriveSVRKey()
		if err != nil {
			zerolog.Ctx(ctx).Err(err).Msg("Failed to derive master key from account entropy pool")
		} else {
			zerolog.Ctx(ctx).Debug().Msg("Derived master key from account entropy pool")
		}
	} else {
		zerolog.Ctx(ctx).Warn().Msg("No account entropy pool in provisioning message")
	}

	// Store the provisioning data
	err = deviceStore.PutDevice(ctx, data)
	if err != nil {
		return nil, fmt.Errorf("failed to store new device: %w", err)
	}

	device, err := deviceStore.DeviceByACI(ctx, data.ACI)
	if err != nil {
		return nil, fmt.Errorf("failed to retrieve stored device: %w", err)
	}

	err = device.ClearDeviceKeys(ctx)
	if err != nil {
		zerolog.Ctx(ctx).Warn().Err(err).Msg("Failed to clear device keys")
	}

	_, err = device.IdentityKeyStore.SaveIdentityKey(ctx, device.ACIServiceID(), device.ACIIdentityKeyPair.GetIdentityKey())
	if err != nil {
		return nil, fmt.Errorf("failed to save aci identity key: %w", err)
	}
	err = device.ACIPreKeyStore.StoreSignedPreKey(ctx, 1, aciSignedPreKey)
	if err != nil {
		return nil, fmt.Errorf("failed to save signed prekey for aci: %w", err)
	}
	err = device.ACIPreKeyStore.StoreLastResortKyberPreKey(ctx, 1, aciPQLastResortPreKey)
	if err != nil {
		return nil, fmt.Errorf("failed to save last resort kyber prekey for aci: %w", err)
	}
	if hasE164 {
		_, err = device.IdentityKeyStore.SaveIdentityKey(ctx, device.PNIServiceID(), device.PNIIdentityKeyPair.GetIdentityKey())
		if err != nil {
			return nil, fmt.Errorf("failed to save pni identity key: %w", err)
		}
		err = device.PNIPreKeyStore.StoreSignedPreKey(ctx, 1, pniSignedPreKey)
		if err != nil {
			return nil, fmt.Errorf("failed to save signed prekey for pni: %w", err)
		}
		err = device.PNIPreKeyStore.StoreLastResortKyberPreKey(ctx, 1, pniPQLastResortPreKey)
		if err != nil {
			return nil, fmt.Errorf("failed to save last resort kyber prekey for pni: %w", err)
		}
	}

	_, err = device.RecipientStore.LoadAndUpdateRecipient(ctx, data.ACI, data.PNI, func(recipient *types.Recipient) (bool, error) {
		recipient.E164 = data.Number
		recipient.Profile.Key = profileKey
		return true, nil
	})
	if err != nil {
		return nil, fmt.Errorf("failed to save profile key: %w", err)
	}

	return data, nil
}

// Returns the provisioningUrl and an error
func startProvisioning(ctx context.Context, ws *websocket.Conn, provisioningCipher *ProvisioningCipher, allowBackup bool) (string, error) {
	pubKey := provisioningCipher.GetPublicKey()

	msg := &signalpb.WebSocketMessage{}
	err := wspb.Read(ctx, ws, msg)
	if err != nil {
		return "", fmt.Errorf("failed to read websocket message: %w", err)
	}

	// Ensure the message is a request and has a valid verb and path
	if msg.GetType() != signalpb.WebSocketMessage_REQUEST || msg.GetRequest().GetVerb() != http.MethodPut || msg.GetRequest().GetPath() != "/v1/address" {
		return "", fmt.Errorf("unexpected websocket message: %v", msg)
	}

	var provisioningBody signalpb.ProvisioningAddress
	err = proto.Unmarshal(msg.GetRequest().GetBody(), &provisioningBody)
	if err != nil {
		return "", fmt.Errorf("failed to unmarshal provisioning UUID: %w", err)
	}

	linkCapabilities := []string{"backup5,nopni2"}
	if !allowBackup {
		linkCapabilities = []string{"nopni2"}
	}
	provisioningURL := (&url.URL{
		Scheme: "sgnl",
		Host:   "linkdevice",
		RawQuery: url.Values{
			"uuid":         []string{provisioningBody.GetAddress()},
			"pub_key":      []string{base64.StdEncoding.EncodeToString(exerrors.Must(pubKey.Serialize()))},
			"capabilities": linkCapabilities,
		}.Encode(),
	}).String()

	// Create and send response
	response := web.CreateWSResponse(ctx, msg.GetRequest().GetId(), 200)
	err = wspb.Write(ctx, ws, response)
	if err != nil {
		return "", fmt.Errorf("failed to write websocket message: %w", err)
	}
	return provisioningURL, nil
}

func continueProvisioning(ctx context.Context, ws *websocket.Conn, provisioningCipher *ProvisioningCipher) (*signalpb.ProvisionMessage, error) {
	envelope := &signalpb.ProvisionEnvelope{}
	msg := &signalpb.WebSocketMessage{}
	err := wspb.Read(ctx, ws, msg)
	if err != nil {
		return nil, fmt.Errorf("failed to read websocket message: %w", err)
	}

	// Wait for provisioning message in a request, then send a response
	if *msg.Type == signalpb.WebSocketMessage_REQUEST &&
		*msg.Request.Verb == http.MethodPut &&
		*msg.Request.Path == "/v1/message" {

		err = proto.Unmarshal(msg.Request.Body, envelope)
		if err != nil {
			return nil, fmt.Errorf("failed to unmarshal provisioning message: %w", err)
		}

		response := web.CreateWSResponse(ctx, *msg.Request.Id, 200)
		err = wspb.Write(ctx, ws, response)
		if err != nil {
			return nil, fmt.Errorf("failed to write websocket message: %w", err)
		}
	} else {
		return nil, fmt.Errorf("invalid provisioning message, type: %v, verb: %v, path: %v", *msg.Type, *msg.Request.Verb, *msg.Request.Path)
	}
	provisioningMessage, err := provisioningCipher.Decrypt(envelope)
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt provisioning message: %w", err)
	}
	return provisioningMessage, nil
}

func getSignalCapabilities(phonenumberless bool) map[string]any {
	return map[string]any{
		"attachmentBackfill":        true,
		"spqr":                      true,
		"usernameChangeSyncMessage": true,
		"optionalPhoneNumber":       phonenumberless,
	}
}

func (cli *Client) RegisterCapabilities(ctx context.Context) error {
	body := exerrors.Must(json.Marshal(getSignalCapabilities(cli.Store.PNI == uuid.Nil)))
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodPut, "/v1/devices/capabilities", body, nil)
	if err != nil {
		return err
	}
	return web.DecodeWSResponseBody(ctx, nil, resp)
}

func (cli *Client) Unlink(ctx context.Context) error {
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodDelete, fmt.Sprintf("/v1/devices/%d", cli.Store.DeviceID), nil, nil)
	if err != nil {
		return err
	}
	return web.DecodeWSResponseBody(ctx, nil, resp)
}

func confirmDevice(
	ctx context.Context,
	username string,
	password string,
	code string,
	hasE164 bool,
	aciRegistrationID int,
	pniRegistrationID int,
	aciSignedPreKey *libsignalgo.SignedPreKeyRecord,
	pniSignedPreKey *libsignalgo.SignedPreKeyRecord,
	aciPQLastResortPreKey *libsignalgo.KyberPreKeyRecord,
	pniPQLastResortPreKey *libsignalgo.KyberPreKeyRecord,
	aciIdentityKeyPair *libsignalgo.IdentityKeyPair,
	deviceName string,
) (*ConfirmDeviceResponse, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "confirm device").Logger()
	ctx = log.WithContext(ctx)
	encryptedDeviceName, err := EncryptDeviceName(deviceName, aciIdentityKeyPair.GetPublicKey())
	if err != nil {
		return nil, fmt.Errorf("failed to encrypt device name: %w", err)
	}

	ws, _, err := web.OpenWebsocket(ctx, (&url.URL{
		Scheme: "wss",
		Host:   web.APIHostname,
		Path:   web.WebsocketPath,
	}).String())
	if err != nil {
		return nil, fmt.Errorf("failed to open websocket: %w", err)
	}
	defer ws.Close(websocket.StatusInternalError, "Websocket StatusInternalError")

	aciSignedPreKeyJSON, err := SignedPreKeyToJSON(aciSignedPreKey)
	if err != nil {
		return nil, fmt.Errorf("failed to convert signed ACI prekey to JSON: %w", err)
	}
	aciPQLastResortPreKeyJSON, err := KyberPreKeyToJSON(aciPQLastResortPreKey)
	if err != nil {
		return nil, fmt.Errorf("failed to convert ACI kyber last resort prekey to JSON: %w", err)
	}

	accountAttributes := map[string]any{
		"fetchesMessages": true,
		"name":            encryptedDeviceName,
		"registrationId":  aciRegistrationID,
		"capabilities":    getSignalCapabilities(!hasE164),
	}
	data := map[string]any{
		"verificationCode":      code,
		"accountAttributes":     accountAttributes,
		"aciSignedPreKey":       aciSignedPreKeyJSON,
		"aciPqLastResortPreKey": aciPQLastResortPreKeyJSON,
	}
	if hasE164 {
		accountAttributes["pniRegistrationId"] = pniRegistrationID
		data["pniSignedPreKey"], err = SignedPreKeyToJSON(pniSignedPreKey)
		if err != nil {
			return nil, fmt.Errorf("failed to convert signed PNI prekey to JSON: %w", err)
		}
		data["pniPqLastResortPreKey"], err = KyberPreKeyToJSON(pniPQLastResortPreKey)
		if err != nil {
			return nil, fmt.Errorf("failed to convert PNI kyber last resort prekey to JSON: %w", err)
		}
	}

	jsonBytes, err := json.Marshal(data)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal JSON: %w", err)
	}

	// Create and send request TODO: Use SignalWebsocket
	request := web.CreateWSRequest(http.MethodPut, "/v1/devices/link", jsonBytes, &username, &password)
	one := uint64(1)
	request.Id = &one
	msg_type := signalpb.WebSocketMessage_REQUEST
	message := &signalpb.WebSocketMessage{
		Type:    &msg_type,
		Request: request,
	}
	err = wspb.Write(ctx, ws, message)
	if err != nil {
		return nil, fmt.Errorf("failed on write protobuf data to websocket: %w", err)
	}

	receivedMsg := &signalpb.WebSocketMessage{}
	err = wspb.Read(ctx, ws, receivedMsg)
	if err != nil {
		return nil, fmt.Errorf("failed to read from websocket after devices call: %w", err)
	}
	zerolog.Ctx(ctx).Trace().Any("response", receivedMsg).Msg("Raw confirm device response")

	status := int(receivedMsg.GetResponse().GetStatus())
	if status < 200 || status >= 300 {
		return nil, DeviceLinkError{StatusCode: status, Message: receivedMsg.GetResponse().GetMessage()}
	}

	deviceResp := ConfirmDeviceResponse{}
	err = json.Unmarshal(receivedMsg.Response.Body, &deviceResp)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal JSON: %w", err)
	}

	return &deviceResp, nil
}
