// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Sumner Evans
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

package libsignalgo_test

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

var nullHash = []byte{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}

func getKeyBytes(t *testing.T) []byte {
	validKey, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	keyBytes, err := validKey.GetPublicKey().Bytes()
	assert.NoError(t, err)
	return keyBytes
}

// From HsmEnclaveTests.swift:testCreateClient
// From HsmEnclaveTests.swift:testCreateClientFailsWithNoHashes
func TestCreateHSMClient(t *testing.T) {
	setupLogging()
	hashes := []byte{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		//
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	}
	t.Run("Succeeds with hashes", func(t *testing.T) {
		client, err := libsignalgo.NewHSMEnclaveClient(getKeyBytes(t), hashes)
		assert.NoError(t, err)

		initialMessage, err := client.InitialRequest()
		assert.NoError(t, err)
		assert.Len(t, initialMessage, 112)
	})

	t.Run("Fails with no hashes", func(t *testing.T) {
		_, err := libsignalgo.NewHSMEnclaveClient(getKeyBytes(t), []byte{})
		assert.Error(t, err)
	})
}

// From HsmEnclaveTests.swift:testCompleteHandshakeWithoutInitialRequest
func TestHSMCompleteHandshakeWithoutInitialRequest(t *testing.T) {
	setupLogging()
	client, err := libsignalgo.NewHSMEnclaveClient(getKeyBytes(t), nullHash)
	assert.NoError(t, err)
	err = client.CompleteHandshake([]byte{0x01, 0x02, 0x03})
	assert.Error(t, err)
}

// From HsmEnclaveTests.swift:testEstablishedSendFailsPriorToEstablishment
func TestHSMEstablishedSendFailsPriorToEstablishment(t *testing.T) {
	setupLogging()
	client, err := libsignalgo.NewHSMEnclaveClient(getKeyBytes(t), nullHash)
	assert.NoError(t, err)
	_, err = client.EstablishedSend([]byte{0x01, 0x02, 0x03})
	assert.Error(t, err)
}

// From HsmEnclaveTests.swift:testEstablishedRecvFailsPriorToEstablishment
func TestHSMEstablishedReceiveFailsPriorToEstablishment(t *testing.T) {
	setupLogging()
	client, err := libsignalgo.NewHSMEnclaveClient(getKeyBytes(t), nullHash)
	assert.NoError(t, err)
	_, err = client.EstablishedReceive([]byte{0x01, 0x02, 0x03})
	assert.Error(t, err)
}
