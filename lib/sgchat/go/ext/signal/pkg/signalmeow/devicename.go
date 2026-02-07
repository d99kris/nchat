// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Tulir Asokan
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
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"net/http"

	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

func hmacSHA256(key, input []byte) []byte {
	hash := hmac.New(sha256.New, key)
	hash.Write(input)
	return hash.Sum(nil)
}

func aes256CTR(key, iv, dst, source []byte) {
	block, _ := aes.NewCipher(key)
	cipher.NewCTR(block, iv).XORKeyStream(dst, source)
}

func (cli *Client) UpdateDeviceName(ctx context.Context, name string) error {
	encryptedName, err := EncryptDeviceName(name, cli.Store.ACIIdentityKeyPair.GetPublicKey())
	if err != nil {
		return fmt.Errorf("failed to encrypt device name: %w", err)
	}
	err = cli.updateDeviceName(ctx, encryptedName)
	if err != nil {
		return fmt.Errorf("failed to update device name: %w", err)
	}
	return nil
}

func (cli *Client) updateDeviceName(ctx context.Context, encryptedName []byte) error {
	reqData, err := json.Marshal(map[string]any{
		"deviceName": encryptedName,
	})
	if err != nil {
		return fmt.Errorf("failed to marshal device name update request: %w", err)
	}
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodPut, "/v1/accounts/name", reqData, nil)
	if err != nil {
		return fmt.Errorf("failed to send device name update request: %w", err)
	}
	if resp.GetStatus() < 200 || resp.GetStatus() >= 300 {
		return fmt.Errorf("device name update request returned status %d", resp.GetStatus())
	}
	return nil
}

func EncryptDeviceName(name string, identityKey *libsignalgo.PublicKey) ([]byte, error) {
	ephemeralPrivKey, err := libsignalgo.GeneratePrivateKey()
	if err != nil {
		return nil, fmt.Errorf("failed to generate ephemeral private key: %w", err)
	}
	ephemeralPubKey, err := ephemeralPrivKey.GetPublicKey()
	if err != nil {
		return nil, fmt.Errorf("failed to generate ephemeral public key: %w", err)
	}
	ephemeralPubKeyBytes, err := ephemeralPubKey.Serialize()
	if err != nil {
		return nil, fmt.Errorf("failed to serialize ephemeral public key: %w", err)
	}
	masterSecret, err := ephemeralPrivKey.Agree(identityKey)
	if err != nil {
		return nil, fmt.Errorf("failed to agree on master secret: %w", err)
	}
	nameBytes := []byte(name)
	key1 := hmacSHA256(masterSecret, []byte("auth"))
	syntheticIV := hmacSHA256(key1, nameBytes)[:16]
	key2 := hmacSHA256(masterSecret, []byte("cipher"))
	cipherKey := hmacSHA256(key2, syntheticIV)
	aes256CTR(cipherKey, make([]byte, 16), nameBytes, nameBytes)
	wrappedData, err := proto.Marshal(&signalpb.DeviceName{
		EphemeralPublic: ephemeralPubKeyBytes,
		SyntheticIv:     syntheticIV,
		Ciphertext:      nameBytes,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to marshal encrypted device name protobuf: %w", err)
	}
	return wrappedData, nil
}

func DecryptDeviceName(wrappedData []byte, identityKey *libsignalgo.PrivateKey) (string, error) {
	var name signalpb.DeviceName
	err := proto.Unmarshal(wrappedData, &name)
	if err != nil {
		return "", fmt.Errorf("failed to unmarshal encrypted device name protobuf: %w", err)
	}
	ephemeralPubKey, err := libsignalgo.DeserializePublicKey(name.EphemeralPublic)
	if err != nil {
		return "", fmt.Errorf("failed to deserialize ephemeral public key: %w", err)
	}
	masterSecret, err := identityKey.Agree(ephemeralPubKey)
	if err != nil {
		return "", fmt.Errorf("failed to agree on master secret: %w", err)
	}
	key2 := hmacSHA256(masterSecret, []byte("cipher"))
	cipherKey := hmacSHA256(key2, name.SyntheticIv)
	decryptedName := make([]byte, len(name.Ciphertext))
	aes256CTR(cipherKey, make([]byte, 16), decryptedName, name.Ciphertext)

	key1 := hmacSHA256(masterSecret, []byte("auth"))
	syntheticIV := hmacSHA256(key1, decryptedName)[:16]
	if !hmac.Equal(key1, hmacSHA256(name.SyntheticIv, syntheticIV)) {
		return "", fmt.Errorf("mismatching synthetic IV")
	}
	return string(decryptedName), nil
}
