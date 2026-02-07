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
	"errors"
	"fmt"
	"math/rand/v2"
	"net/http"
	"strings"
	"time"

	"github.com/rs/zerolog"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/events"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

const PREKEY_BATCH_SIZE = 100

type GeneratedPreKeys struct {
	PreKeys      []*libsignalgo.PreKeyRecord
	KyberPreKeys []*libsignalgo.KyberPreKeyRecord
	IdentityKey  []uint8
}

func (cli *Client) RegisterAllPreKeys(ctx context.Context, pks store.PreKeyStore) error {
	var identityKeyPair *libsignalgo.IdentityKeyPair
	var pni bool
	if pks.GetServiceID().Type == libsignalgo.ServiceIDTypePNI {
		pni = true
		identityKeyPair = cli.Store.PNIIdentityKeyPair
	} else {
		identityKeyPair = cli.Store.ACIIdentityKeyPair
	}

	// Get all prekeys and kyber prekeys from the database
	preKeys, err := pks.AllPreKeys(ctx)
	if err != nil {
		return fmt.Errorf("failed to get all prekeys: %w", err)
	}
	kyberPreKeys, err := pks.AllNormalKyberPreKeys(ctx)
	if err != nil {
		return fmt.Errorf("failed to get all kyber prekeys: %w", err)
	}

	// We need to have some keys to upload
	if len(preKeys) == 0 && len(kyberPreKeys) == 0 {
		return fmt.Errorf("no prekeys to upload")
	}

	identityKey, err := identityKeyPair.GetPublicKey().Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize identity key: %w", err)
	}

	generatedPreKeys := GeneratedPreKeys{
		PreKeys:      preKeys,
		KyberPreKeys: kyberPreKeys,
		IdentityKey:  identityKey,
	}
	zerolog.Ctx(ctx).Debug().
		Int("num_prekeys", len(preKeys)).
		Int("num_kyber_prekeys", len(kyberPreKeys)).
		Msg("Registering all prekeys")
	err = cli.RegisterPreKeys(ctx, &generatedPreKeys, pni)
	if err != nil {
		return fmt.Errorf("failed to register prekeys: %w", err)
	}

	return err
}

func (cli *Client) GenerateAndSaveNextPreKeyBatch(ctx context.Context, pks store.PreKeyStore, serverCount int) (bool, error) {
	storeCount, nextPreKeyID, err := pks.GetNextPreKeyID(ctx)
	if err != nil {
		return false, fmt.Errorf("failed to get next prekey ID: %w", err)
	}
	if serverCount < PREKEY_BATCH_SIZE/2 {
		if storeCount >= PREKEY_BATCH_SIZE {
			zerolog.Ctx(ctx).Warn().
				Int("server_count", serverCount).
				Uint32("store_count", storeCount).
				Msg("Store is full, but server is not, reuploading EC prekeys without generating more")
		} else {
			zerolog.Ctx(ctx).Info().
				Int("server_count", serverCount).
				Uint32("store_count", storeCount).
				Msg("Generating and uploading EC prekeys")
		}
	} else if uint32(serverCount) > storeCount {
		zerolog.Ctx(ctx).Warn().
			Int("server_count", serverCount).
			Uint32("store_count", storeCount).
			Msg("Server has more EC prekeys than store, reuploading")
	} else {
		zerolog.Ctx(ctx).Debug().
			Int("server_count", serverCount).
			Uint32("store_count", storeCount).
			Msg("EC prekey count is good")
		return false, nil
	}
	if storeCount < PREKEY_BATCH_SIZE {
		preKeys := GeneratePreKeys(nextPreKeyID, PREKEY_BATCH_SIZE-storeCount)
		for _, preKey := range preKeys {
			err = pks.StorePreKey(ctx, 0, preKey)
			if err != nil {
				return false, fmt.Errorf("failed to save prekey: %w", err)
			}
		}
	}
	return true, nil
}

func (cli *Client) GenerateAndSaveNextKyberPreKeyBatch(ctx context.Context, pks store.PreKeyStore, serverCount int) (bool, error) {
	var identityKeyPair *libsignalgo.IdentityKeyPair
	if pks.GetServiceID().Type == libsignalgo.ServiceIDTypePNI {
		identityKeyPair = cli.Store.PNIIdentityKeyPair
	} else {
		identityKeyPair = cli.Store.ACIIdentityKeyPair
	}
	storeCount, nextKyberPreKeyID, err := pks.GetNextKyberPreKeyID(ctx)
	if err != nil {
		return false, fmt.Errorf("failed to get next kyber prekey ID: %w", err)
	}
	if serverCount < PREKEY_BATCH_SIZE/2 {
		if storeCount >= PREKEY_BATCH_SIZE {
			zerolog.Ctx(ctx).Warn().
				Int("server_count", serverCount).
				Uint32("store_count", storeCount).
				Msg("Store is full, but server is not, reuploading kyber prekeys without generating more")
		} else {
			zerolog.Ctx(ctx).Info().
				Int("server_count", serverCount).
				Uint32("store_count", storeCount).
				Msg("Generating and uploading kyber prekeys")
		}
	} else if uint32(serverCount) > storeCount {
		zerolog.Ctx(ctx).Warn().
			Int("server_count", serverCount).
			Uint32("store_count", storeCount).
			Msg("Server has more kyber prekeys than store, reuploading")
	} else {
		zerolog.Ctx(ctx).Debug().
			Int("server_count", serverCount).
			Uint32("store_count", storeCount).
			Msg("Kyber prekey count is good")
		return false, nil
	}
	if storeCount < PREKEY_BATCH_SIZE {
		kyberPreKeys := GenerateKyberPreKeys(nextKyberPreKeyID, PREKEY_BATCH_SIZE-storeCount, identityKeyPair)
		for _, kyberPreKey := range kyberPreKeys {
			err = pks.StoreKyberPreKey(ctx, 0, kyberPreKey)
			if err != nil {
				return false, fmt.Errorf("failed to save kyber prekey: %w", err)
			}
		}
	}
	return true, nil
}

func GeneratePreKeys(startKeyID uint32, count uint32) []*libsignalgo.PreKeyRecord {
	if count > PREKEY_BATCH_SIZE {
		panic("count must be less than or equal to PREKEY_BATCH_SIZE")
	}
	generatedPreKeys := make([]*libsignalgo.PreKeyRecord, 0, count)
	for keyID := startKeyID; keyID < startKeyID+count; keyID++ {
		privateKey, err := libsignalgo.GeneratePrivateKey()
		if err != nil {
			panic(fmt.Errorf("error generating private key: %w", err))
		}
		preKey, err := libsignalgo.NewPreKeyRecordFromPrivateKey(keyID, privateKey)
		if err != nil {
			panic(fmt.Errorf("error creating prekey record: %w", err))
		}
		generatedPreKeys = append(generatedPreKeys, preKey)
	}
	return generatedPreKeys
}

func GenerateKyberPreKeys(startKeyID uint32, count uint32, identityKeyPair *libsignalgo.IdentityKeyPair) []*libsignalgo.KyberPreKeyRecord {
	if count > PREKEY_BATCH_SIZE {
		panic("count must be less than or equal to PREKEY_BATCH_SIZE")
	}
	generatedKyberPreKeys := make([]*libsignalgo.KyberPreKeyRecord, 0, count)
	for keyID := startKeyID; keyID < startKeyID+count; keyID++ {
		kyberPreKeyPair, err := libsignalgo.KyberKeyPairGenerate()
		if err != nil {
			panic(fmt.Errorf("error generating kyber key pair: %w", err))
		}
		publicKey, err := kyberPreKeyPair.GetPublicKey()
		if err != nil {
			panic(fmt.Errorf("error getting kyber public key: %w", err))
		}
		serializedPublicKey, err := publicKey.Serialize()
		if err != nil {
			panic(fmt.Errorf("error serializing kyber public key: %w", err))
		}
		signature, err := identityKeyPair.GetPrivateKey().Sign(serializedPublicKey)
		if err != nil {
			panic(fmt.Errorf("error signing kyber public key: %w", err))
		}
		preKey, err := libsignalgo.NewKyberPreKeyRecord(keyID, time.Now(), kyberPreKeyPair, signature)
		if err != nil {
			panic(fmt.Errorf("error creating kyber prekey record: %w", err))

		}
		generatedKyberPreKeys = append(generatedKyberPreKeys, preKey)
	}
	return generatedKyberPreKeys
}

func GenerateSignedPreKey(startSignedKeyId uint32, identityKeyPair *libsignalgo.IdentityKeyPair) *libsignalgo.SignedPreKeyRecord {
	// Generate a signed prekey
	privateKey, err := libsignalgo.GeneratePrivateKey()
	if err != nil {
		panic(fmt.Errorf("error generating private key: %w", err))
	}
	timestamp := time.Now()
	publicKey, err := privateKey.GetPublicKey()
	if err != nil {
		panic(fmt.Errorf("error getting public key: %w", err))
	}
	serializedPublicKey, err := publicKey.Serialize()
	if err != nil {
		panic(fmt.Errorf("error serializing public key: %w", err))
	}
	signature, err := identityKeyPair.GetPrivateKey().Sign(serializedPublicKey)
	if err != nil {
		panic(fmt.Errorf("error signing public key: %w", err))
	}
	signedPreKey, err := libsignalgo.NewSignedPreKeyRecordFromPrivateKey(startSignedKeyId, timestamp, privateKey, signature)
	if err != nil {
		panic(fmt.Errorf("error creating signed prekey record: %w", err))
	}

	return signedPreKey
}

func PreKeyToJSON(preKey *libsignalgo.PreKeyRecord) (map[string]interface{}, error) {
	id, err := preKey.GetID()
	if err != nil {
		return nil, fmt.Errorf("failed to get ID: %w", err)
	}
	publicKey, err := preKey.GetPublicKey()
	if err != nil {
		return nil, fmt.Errorf("failed to get public key: %w", err)
	}
	serializedKey, err := publicKey.Serialize()
	if err != nil {
		return nil, fmt.Errorf("failed to serialize public key: %w", err)
	}
	preKeyJson := map[string]interface{}{
		"keyId":     id,
		"publicKey": base64.StdEncoding.EncodeToString(serializedKey),
	}
	return preKeyJson, nil
}

func SignedPreKeyToJSON(signedPreKey *libsignalgo.SignedPreKeyRecord) (map[string]interface{}, error) {
	id, err := signedPreKey.GetID()
	if err != nil {
		return nil, fmt.Errorf("failed to get ID: %w", err)
	}
	publicKey, err := signedPreKey.GetPublicKey()
	if err != nil {
		return nil, fmt.Errorf("failed to get public key: %w", err)
	}
	serializedKey, err := publicKey.Serialize()
	if err != nil {
		return nil, fmt.Errorf("failed to serialize public key: %w", err)
	}
	signature, err := signedPreKey.GetSignature()
	if err != nil {
		return nil, fmt.Errorf("failed to get signature: %w", err)
	}
	signedPreKeyJson := map[string]interface{}{
		"keyId":     id,
		"publicKey": base64.StdEncoding.EncodeToString(serializedKey),
		"signature": base64.StdEncoding.EncodeToString(signature),
	}
	return signedPreKeyJson, nil
}

func KyberPreKeyToJSON(kyberPreKey *libsignalgo.KyberPreKeyRecord) (map[string]interface{}, error) {
	id, err := kyberPreKey.GetID()
	if err != nil {
		return nil, fmt.Errorf("failed to get ID: %w", err)
	}
	publicKey, err := kyberPreKey.GetPublicKey()
	if err != nil {
		return nil, fmt.Errorf("failed to get public key: %w", err)
	}
	serializedKey, err := publicKey.Serialize()
	if err != nil {
		return nil, fmt.Errorf("failed to serialize public key: %w", err)
	}
	signature, err := kyberPreKey.GetSignature()
	if err != nil {
		return nil, fmt.Errorf("failed to get signature: %w", err)
	}
	kyberPreKeyJson := map[string]interface{}{
		"keyId":     id,
		"publicKey": base64.StdEncoding.EncodeToString(serializedKey),
		"signature": base64.StdEncoding.EncodeToString(signature),
	}
	return kyberPreKeyJson, nil
}

var errPrekeyUpload422 = errors.New("http 422 while registering prekeys")

func (cli *Client) RegisterPreKeys(ctx context.Context, generatedPreKeys *GeneratedPreKeys, pni bool) error {
	log := zerolog.Ctx(ctx).With().Str("action", "register prekeys").Logger()
	// Convert generated prekeys to JSON
	preKeysJson := []map[string]any{}
	kyberPreKeysJson := []map[string]any{}
	for _, preKey := range generatedPreKeys.PreKeys {
		preKeyJson, err := PreKeyToJSON(preKey)
		if err != nil {
			return fmt.Errorf("failed to convert prekey to JSON: %w", err)
		}
		preKeysJson = append(preKeysJson, preKeyJson)
	}
	for _, kyberPreKey := range generatedPreKeys.KyberPreKeys {
		kyberPreKeyJson, err := KyberPreKeyToJSON(kyberPreKey)
		if err != nil {
			return fmt.Errorf("failed to convert kyber prekey to JSON: %w", err)
		}
		kyberPreKeysJson = append(kyberPreKeysJson, kyberPreKeyJson)
	}

	identityKey := generatedPreKeys.IdentityKey
	registerJSON := map[string]any{
		"preKeys":     preKeysJson,
		"pqPreKeys":   kyberPreKeysJson,
		"identityKey": base64.StdEncoding.EncodeToString(identityKey),
	}

	// Send request
	jsonBytes, err := json.Marshal(registerJSON)
	if err != nil {
		log.Err(err).Msg("Error marshalling register JSON")
		return err
	}
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodPut, keysPath(pni), jsonBytes, nil)
	if err != nil {
		log.Err(err).Msg("Error sending request")
		return err
	}
	if resp.GetStatus() == 422 {
		return errPrekeyUpload422
	}
	return web.DecodeWSResponseBody(ctx, nil, resp)
}

type prekeyResponse struct {
	IdentityKey string         `json:"identityKey"`
	Devices     []prekeyDevice `json:"devices"`
}

type preKeyCountResponse struct {
	Count   int `json:"count"`
	PQCount int `json:"pqCount"`
}

type prekeyDevice struct {
	DeviceID       int           `json:"deviceId"`
	RegistrationID int           `json:"registrationId"`
	SignedPreKey   prekeyDetail  `json:"signedPreKey"`
	PreKey         *prekeyDetail `json:"preKey"`
	PQPreKey       *prekeyDetail `json:"pqPreKey"`
}

type prekeyDetail struct {
	KeyID     int    `json:"keyId"`
	PublicKey string `json:"publicKey"`
	Signature string `json:"signature,omitempty"` // 'omitempty' since this field isn't always present
}

func addBase64PaddingAndDecode(data string) ([]byte, error) {
	padding := len(data) % 4
	if padding > 0 {
		data += strings.Repeat("=", 4-padding)
	}
	return base64.StdEncoding.DecodeString(data)
}

var (
	ErrUnregisteredUser = errors.New("user is unregistered")
	ErrDevicesChanged   = errors.New("device list changed while sending skdm")
)

func (cli *Client) FetchAndProcessPreKey(ctx context.Context, theirServiceID libsignalgo.ServiceID, specificDeviceID int) error {
	if cli.Store.RecipientStore.IsUnregistered(ctx, theirServiceID) {
		return fmt.Errorf("%w (cached)", ErrUnregisteredUser)
	}
	// Fetch prekey
	deviceIDPath := "/*"
	if specificDeviceID >= 0 {
		deviceIDPath = "/" + fmt.Sprint(specificDeviceID)
	}
	// TODO this should be done via the unauthed websocket if possible
	path := "/v2/keys/" + theirServiceID.String() + deviceIDPath + "?pq=true"
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodGet, path, nil, nil)
	if err != nil {
		return fmt.Errorf("error sending request: %w", err)
	} else if resp.GetStatus() == 404 {
		cli.Store.RecipientStore.MarkUnregistered(ctx, theirServiceID, true)
		return fmt.Errorf("%w (404 while querying keys)", ErrUnregisteredUser)
	}
	var respData prekeyResponse
	err = web.DecodeWSResponseBody(ctx, &respData, resp)
	if err != nil {
		return fmt.Errorf("error decoding response body: %w", err)
	}

	rawIdentityKey, err := addBase64PaddingAndDecode(respData.IdentityKey)
	if err != nil {
		return fmt.Errorf("error decoding identity key: %w", err)
	}
	identityKey, err := libsignalgo.DeserializeIdentityKey([]byte(rawIdentityKey))
	if err != nil {
		return fmt.Errorf("error deserializing identity key: %w", err)
	}
	if identityKey == nil {
		return fmt.Errorf("deserializing identity key returned nil with no error")
	}

	// Process each prekey in response (should only be one at the moment)
	for _, d := range respData.Devices {
		var publicKey *libsignalgo.PublicKey
		var preKeyID uint32
		if d.PreKey != nil {
			preKeyID = uint32(d.PreKey.KeyID)
			rawPublicKey, err := addBase64PaddingAndDecode(d.PreKey.PublicKey)
			if err != nil {
				return fmt.Errorf("error decoding public key: %w", err)
			}
			publicKey, err = libsignalgo.DeserializePublicKey(rawPublicKey)
			if err != nil {
				return fmt.Errorf("error deserializing public key: %w", err)
			}
		}

		rawSignedPublicKey, err := addBase64PaddingAndDecode(d.SignedPreKey.PublicKey)
		if err != nil {
			return fmt.Errorf("error decoding signed public key: %w", err)
		}
		signedPublicKey, err := libsignalgo.DeserializePublicKey(rawSignedPublicKey)
		if err != nil {
			return fmt.Errorf("error deserializing signed public key: %w", err)
		}

		var kyberPublicKey *libsignalgo.KyberPublicKey
		var kyberPreKeyID uint32
		var kyberPreKeySignature []byte
		if d.PQPreKey != nil {
			kyberPreKeyID = uint32(d.PQPreKey.KeyID)
			rawKyberPublicKey, err := addBase64PaddingAndDecode(d.PQPreKey.PublicKey)
			if err != nil {
				return fmt.Errorf("error decoding kyber public key: %w", err)
			}
			kyberPublicKey, err = libsignalgo.DeserializeKyberPublicKey(rawKyberPublicKey)
			if err != nil {
				return fmt.Errorf("error deserializing kyber public key: %w", err)
			}
			kyberPreKeySignature, err = addBase64PaddingAndDecode(d.PQPreKey.Signature)
			if err != nil {
				return fmt.Errorf("error decoding kyber prekey signature: %w", err)
			}
		}

		rawSignature, err := addBase64PaddingAndDecode(d.SignedPreKey.Signature)
		if err != nil {
			return fmt.Errorf("error decoding signature: %w", err)
		}

		preKeyBundle, err := libsignalgo.NewPreKeyBundle(
			uint32(d.RegistrationID),
			uint32(d.DeviceID),
			preKeyID,
			publicKey,
			uint32(d.SignedPreKey.KeyID),
			signedPublicKey,
			rawSignature,
			kyberPreKeyID,
			kyberPublicKey,
			kyberPreKeySignature,
			identityKey,
		)
		if err != nil {
			return fmt.Errorf("error creating prekey bundle: %w", err)
		}
		address, err := theirServiceID.Address(uint(d.DeviceID))
		if err != nil {
			return fmt.Errorf("error creating address: %w", err)
		}
		err = libsignalgo.ProcessPreKeyBundle(
			ctx,
			preKeyBundle,
			address,
			cli.Store.ACISessionStore,
			cli.Store.ACIIdentityStore,
		)
		if err != nil {
			return fmt.Errorf("error processing prekey bundle: %w", err)
		}
	}

	return err
}

const (
	aciKeysPath = "/v2/keys?identity=aci"
	pniKeysPath = "/v2/keys?identity=pni"
)

func keysPath(pni bool) string {
	if pni {
		return pniKeysPath
	}
	return aciKeysPath
}

func (cli *Client) GetMyKeyCounts(ctx context.Context, pni bool) (int, int, error) {
	log := zerolog.Ctx(ctx).With().Str("action", "get my key counts").Logger()
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodGet, keysPath(pni), nil, nil)
	if err != nil {
		log.Err(err).Msg("Error sending request")
		return 0, 0, err
	}
	var respData preKeyCountResponse
	err = web.DecodeWSResponseBody(ctx, &respData, resp)
	if err != nil {
		log.Err(err).Msg("Fetching prekey counts, error with response body")
		return 0, 0, err
	}
	return respData.Count, respData.PQCount, err
}

func (cli *Client) CheckAndUploadNewPreKeys(ctx context.Context, pks store.PreKeyStore) error {
	log := zerolog.Ctx(ctx).With().Str("action", "check and upload new prekeys").Logger()
	// Check if we need to upload prekeys
	preKeyCount, kyberPreKeyCount, err := cli.GetMyKeyCounts(ctx, pks.GetServiceID().Type == libsignalgo.ServiceIDTypePNI)
	if err != nil {
		log.Err(err).Msg("Error getting prekey counts")
		return err
	}
	doECUpload, err := cli.GenerateAndSaveNextPreKeyBatch(ctx, pks, preKeyCount)
	if err != nil {
		log.Err(err).Msg("Error generating and saving next prekey batch")
		return err
	}
	doKyberUpload, err := cli.GenerateAndSaveNextKyberPreKeyBatch(ctx, pks, kyberPreKeyCount)
	if err != nil {
		log.Err(err).Msg("Error generating and saving next kyber prekey batch")
		return err
	}
	if !doECUpload && !doKyberUpload {
		log.Debug().Msg("No new prekeys to upload")
		return nil
	}
	err = cli.RegisterAllPreKeys(ctx, pks)
	if err != nil {
		log.Err(err).Msg("Error registering prekey batches")
		return err
	}
	return nil
}

func (cli *Client) keyCheckLoop(ctx context.Context) {
	log := zerolog.Ctx(ctx).With().Str("action", "start key check loop").Logger()

	// Do the initial check in 5-10 minutes after starting the loop
	windowStart := 0
	windowSize := 1
	firstRun := true
	for {
		randomMinutesInWindow := rand.IntN(windowSize) + windowStart
		checkTime := time.Duration(randomMinutesInWindow) * time.Minute
		if firstRun {
			checkTime = 0
			firstRun = false
		} else {
			log.Debug().Dur("check_time", checkTime).Msg("Waiting to check for new prekeys")
		}

		select {
		case <-ctx.Done():
			return
		case <-time.After(checkTime):
			err := cli.CheckAndUploadNewPreKeys(ctx, cli.Store.ACIPreKeyStore)
			if err != nil {
				log.Err(err).Msg("Error checking and uploading new prekeys for ACI identity")
				// Retry within half an hour
				windowStart = 5
				windowSize = 25
				continue
			}
			err = cli.CheckAndUploadNewPreKeys(ctx, cli.Store.PNIPreKeyStore)
			if err != nil {
				if errors.Is(err, errPrekeyUpload422) {
					log.Err(err).Msg("Got 422 error while uploading PNI prekeys, deleting session")
					disconnectErr := cli.ClearKeysAndDisconnect(ctx)
					if disconnectErr != nil {
						log.Err(disconnectErr).Msg("ClearKeysAndDisconnect error")
					}
					cli.handleEvent(&events.LoggedOut{Error: err})
					return
				}
				log.Err(err).Msg("Error checking and uploading new prekeys for PNI identity")
				// Retry within half an hour
				windowStart = 5
				windowSize = 25
				continue
			}
			// After a successful check, check again in 36 to 60 hours
			windowStart = 36 * 60
			windowSize = 24 * 60
		}
	}
}
