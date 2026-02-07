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
	"time"

	"github.com/stretchr/testify/assert"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

type Serializable interface {
	Serialize() ([]byte, error)
}

func testRoundTrip[T Serializable](t *testing.T, name string, obj T, deserializer func([]byte) (T, error)) {
	t.Run(name, func(t *testing.T) {
		serialized, err := obj.Serialize()
		assert.NoError(t, err)

		deserialized, err := deserializer(serialized)
		assert.NoError(t, err)

		deserializedSerialized, err := deserialized.Serialize()
		assert.NoError(t, err)

		assert.Equal(t, serialized, deserializedSerialized)
	})
}

// From PublicAPITests.swift:testSerializationRoundTrip
func TestSenderCertificateSerializationRoundTrip(t *testing.T) {
	keyPair, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)

	testRoundTrip(t, "key pair", keyPair, libsignalgo.DeserializeIdentityKeyPair)
	testRoundTrip(t, "public key", keyPair.GetPublicKey(), libsignalgo.DeserializePublicKey)
	testRoundTrip(t, "private key", keyPair.GetPrivateKey(), libsignalgo.DeserializePrivateKey)
	testRoundTrip(t, "identity key", keyPair.GetIdentityKey(), libsignalgo.NewIdentityKeyFromBytes)

	preKeyRecord, err := libsignalgo.NewPreKeyRecord(7, keyPair.GetPublicKey(), keyPair.GetPrivateKey())
	assert.NoError(t, err)
	testRoundTrip(t, "pre key record", preKeyRecord, libsignalgo.DeserializePreKeyRecord)

	publicKeySerialized, err := keyPair.GetPublicKey().Serialize()
	assert.NoError(t, err)
	signature, err := keyPair.GetPrivateKey().Sign(publicKeySerialized)
	assert.NoError(t, err)

	signedPreKeyRecord, err := libsignalgo.NewSignedPreKeyRecordFromPrivateKey(
		77,
		time.UnixMilli(42000),
		keyPair.GetPrivateKey(),
		signature,
	)
	assert.NoError(t, err)
	testRoundTrip(t, "signed pre key record", signedPreKeyRecord, libsignalgo.DeserializeSignedPreKeyRecord)
}
