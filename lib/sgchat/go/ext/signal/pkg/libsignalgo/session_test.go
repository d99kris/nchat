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
	"context"
	"fmt"
	"runtime"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

func initializeSessions(t *testing.T, aliceStore, bobStore *InMemorySignalProtocolStore, bobAddress *libsignalgo.Address) {
	ctx := context.TODO()

	bobPreKey, err := libsignalgo.GeneratePrivateKey()
	assert.NoError(t, err)
	bobPreKeyPublicKey, err := bobPreKey.GetPublicKey()
	assert.NoError(t, err)

	bobSignedPreKey, err := libsignalgo.GeneratePrivateKey()
	assert.NoError(t, err)

	bobSignedPreKeyPublicKey, err := bobSignedPreKey.GetPublicKey()
	assert.NoError(t, err)
	bobSignedPreKeyPublicSerialized, err := bobSignedPreKeyPublicKey.Serialize()
	assert.NoError(t, err)

	bobKyberPreKey, err := libsignalgo.KyberKeyPairGenerate()
	assert.NoError(t, err)
	bobKyberPreKeyPublicKey, err := bobKyberPreKey.GetPublicKey()
	assert.NoError(t, err)
	bobKyberPreKeyPublicSerialized, err := bobKyberPreKeyPublicKey.Serialize()
	assert.NoError(t, err)

	bobIdentityKeyPair, err := bobStore.GetIdentityKeyPair(ctx)
	assert.NoError(t, err)
	bobSignedPreKeySignature, err := bobIdentityKeyPair.GetPrivateKey().Sign(bobSignedPreKeyPublicSerialized)
	assert.NoError(t, err)
	bobKyberPreKeySignature, err := bobIdentityKeyPair.GetPrivateKey().Sign(bobKyberPreKeyPublicSerialized)
	assert.NoError(t, err)

	bobPublicIdentityKey := bobIdentityKeyPair.GetPublicKey()
	bobIdentityKey, err := libsignalgo.NewIdentityKeyFromPublicKey(bobPublicIdentityKey)
	assert.NoError(t, err)

	var prekeyID uint32 = 4570
	var signedPreKeyID uint32 = 3006
	var kyberPreKeyID uint32 = 8008

	bobRegistrationID, err := bobStore.GetLocalRegistrationID(ctx)
	assert.NoError(t, err)
	bobBundle, err := libsignalgo.NewPreKeyBundle(
		bobRegistrationID,
		9,
		prekeyID,
		bobPreKeyPublicKey,
		signedPreKeyID,
		bobSignedPreKeyPublicKey,
		bobSignedPreKeySignature,
		kyberPreKeyID,
		bobKyberPreKeyPublicKey,
		bobKyberPreKeySignature,
		bobIdentityKey,
	)
	assert.NoError(t, err)

	// Alice processes the bundle
	err = libsignalgo.ProcessPreKeyBundle(ctx, bobBundle, bobAddress, aliceStore, aliceStore)
	assert.NoError(t, err)

	record, err := aliceStore.LoadSession(ctx, bobAddress)
	assert.NoError(t, err)
	assert.NotNil(t, record)

	hasCurrentState, err := record.HasCurrentState()
	assert.NoError(t, err)
	assert.True(t, hasCurrentState)

	remoteRegistrationID, err := record.GetRemoteRegistrationID()
	assert.NoError(t, err)
	assert.Equal(t, bobRegistrationID, remoteRegistrationID)

	// Bob processes the bundle
	preKeyRecord, err := libsignalgo.NewPreKeyRecordFromPrivateKey(prekeyID, bobPreKey)
	assert.NoError(t, err)
	err = bobStore.StorePreKey(ctx, prekeyID, preKeyRecord)
	assert.NoError(t, err)

	signedPreKeyRecord, err := libsignalgo.NewSignedPreKeyRecordFromPrivateKey(signedPreKeyID, time.UnixMilli(42000), bobSignedPreKey, bobSignedPreKeySignature)
	assert.NoError(t, err)
	err = bobStore.StoreSignedPreKey(ctx, signedPreKeyID, signedPreKeyRecord)
	require.NoError(t, err)

	kyberPreKeyRecord, err := libsignalgo.NewKyberPreKeyRecord(kyberPreKeyID, time.UnixMilli(42000), bobKyberPreKey, bobKyberPreKeySignature)
	require.NoError(t, err)
	err = bobStore.StoreKyberPreKey(ctx, kyberPreKeyID, kyberPreKeyRecord)
	require.NoError(t, err)
}

// From SessionTests.swift:testSessionCipher
func TestSessionCipher(t *testing.T) {
	ctx := context.TODO()
	aliceACI := uuid.New()
	bobACI := uuid.New()

	aliceAddress, err := libsignalgo.NewACIServiceID(aliceACI).Address(1)
	assert.NoError(t, err)
	bobAddress, err := libsignalgo.NewACIServiceID(bobACI).Address(1)
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()
	bobStore := NewInMemorySignalProtocolStore()

	initializeSessions(t, aliceStore, bobStore, bobAddress)

	alicePlaintext := []byte{8, 6, 7, 5, 3, 0, 9}

	aliceCiphertext, err := libsignalgo.Encrypt(ctx, alicePlaintext, bobAddress, aliceStore, aliceStore)
	assert.NoError(t, err)
	aliceCiphertextMessageType, err := aliceCiphertext.MessageType()
	assert.NoError(t, err)
	assert.Equal(t, libsignalgo.CiphertextMessageTypePreKey, aliceCiphertextMessageType)

	aliceCiphertextSerialized, err := aliceCiphertext.Serialize()
	assert.NoError(t, err)
	bobCiphertext, err := libsignalgo.DeserializePreKeyMessage(aliceCiphertextSerialized)
	assert.NoError(t, err)

	bobPlaintext, err := libsignalgo.DecryptPreKey(ctx, bobCiphertext, aliceAddress, bobStore, bobStore, bobStore, bobStore, bobStore)
	assert.NoError(t, err)
	assert.Equal(t, alicePlaintext, bobPlaintext)

	bobPlaintext2 := []byte{23}

	bobCiphertext2, err := libsignalgo.Encrypt(ctx, bobPlaintext2, aliceAddress, bobStore, bobStore)
	assert.NoError(t, err)
	bobCiphertext2MessageType, err := bobCiphertext2.MessageType()
	assert.NoError(t, err)
	assert.Equal(t, libsignalgo.CiphertextMessageTypeWhisper, bobCiphertext2MessageType)

	bobCiphertext2Serialized, err := bobCiphertext2.Serialize()
	assert.NoError(t, err)
	aliceCiphertext2, err := libsignalgo.DeserializeMessage(bobCiphertext2Serialized)
	assert.NoError(t, err)
	alicePlaintext2, err := libsignalgo.Decrypt(ctx, aliceCiphertext2, bobAddress, aliceStore, aliceStore)
	assert.NoError(t, err)
	assert.Equal(t, bobPlaintext2, alicePlaintext2)
}

// From SessionTests.swift:testSessionCipherWithBadStore
func TestSessionCipherWithBadStore(t *testing.T) {
	ctx := context.TODO()

	aliceACI := uuid.New()
	bobACI := uuid.New()

	aliceAddress, err := libsignalgo.NewACIServiceID(aliceACI).Address(1)
	assert.NoError(t, err)
	bobAddress, err := libsignalgo.NewACIServiceID(bobACI).Address(1)
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()
	bobStore := &BadInMemorySignalProtocolStore{NewInMemorySignalProtocolStore()}

	initializeSessions(t, aliceStore, bobStore.InMemorySignalProtocolStore, bobAddress)

	alicePlaintext := []byte{8, 6, 7, 5, 3, 0, 9}

	aliceCiphertext, err := libsignalgo.Encrypt(ctx, alicePlaintext, bobAddress, aliceStore, aliceStore)
	assert.NoError(t, err)
	aliceCiphertextMessageType, err := aliceCiphertext.MessageType()
	assert.NoError(t, err)
	assert.Equal(t, libsignalgo.CiphertextMessageTypePreKey, aliceCiphertextMessageType)

	aliceCiphertextSerialized, err := aliceCiphertext.Serialize()
	assert.NoError(t, err)
	bobCiphertext, err := libsignalgo.DeserializePreKeyMessage(aliceCiphertextSerialized)
	assert.NoError(t, err)
	t.Skip("This test is broken") // TODO fix
	_, err = libsignalgo.DecryptPreKey(ctx, bobCiphertext, aliceAddress, bobStore, bobStore, bobStore, bobStore, bobStore)
	require.Error(t, err)
	assert.Equal(t, "Test error", err.Error())
}

func TestSealedSenderEncrypt_Repeated(t *testing.T) {
	ctx := context.TODO()

	setupLogging()

	aliceAddress, err := libsignalgo.NewUUIDAddressFromString("9d0652a3-dcc3-4d11-975f-74d61598733f", 1)
	assert.NoError(t, err)
	bobAddress, err := libsignalgo.NewUUIDAddressFromString("6838237D-02F6-4098-B110-698253D15961", 1)
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()
	bobStore := NewInMemorySignalProtocolStore()

	initializeSessions(t, aliceStore, bobStore, bobAddress)

	trustRoot, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	serverKeys, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	serverCert, err := libsignalgo.NewServerCertificate(1, serverKeys.GetPublicKey(), trustRoot.GetPrivateKey())
	assert.NoError(t, err)
	aliceName, err := aliceAddress.Name()
	assert.NoError(t, err)
	senderAddress := libsignalgo.NewSealedSenderAddress("+14151111111", uuid.MustParse(aliceName), 1)

	aliceIdentityKeyPair, err := aliceStore.GetIdentityKeyPair(ctx)
	require.NoError(t, err)
	senderCert, err := libsignalgo.NewSenderCertificate(senderAddress, aliceIdentityKeyPair.GetPublicKey(), time.UnixMilli(31337), serverCert, serverKeys.GetPrivateKey())
	assert.NoError(t, err)

	go func() {
		for {
			runtime.GC()
			time.Sleep(500 * time.Microsecond)
		}
	}()
	for i := 0; i < 100; i++ {
		message := []byte(fmt.Sprintf("%04d vision", i))
		ciphertext, err := libsignalgo.SealedSenderEncryptPlaintext(ctx, message, libsignalgo.UnidentifiedSenderMessageContentHintDefault, bobAddress, senderCert, aliceStore, aliceStore, nil)
		require.NoError(t, err)
		assert.NotNil(t, ciphertext)
	}
}

// From SessionTests.swift:testArchiveSession
func TestArchiveSession(t *testing.T) {
	ctx := context.TODO()
	setupLogging()

	bobACI := uuid.New()

	bobAddress, err := libsignalgo.NewACIServiceID(bobACI).Address(1)
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()
	bobStore := NewInMemorySignalProtocolStore()

	initializeSessions(t, aliceStore, bobStore, bobAddress)

	session, err := aliceStore.LoadSession(ctx, bobAddress)
	assert.NoError(t, err)
	assert.NotNil(t, session)

	hasCurrentState, err := session.HasCurrentState()
	assert.NoError(t, err)
	assert.True(t, hasCurrentState)

	newIdentityKeyPair, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	matches, err := session.CurrentRatchetKeyMatches(newIdentityKeyPair.GetPublicKey())
	assert.NoError(t, err)
	assert.False(t, matches)

	err = session.ArchiveCurrentState()
	assert.NoError(t, err)

	hasCurrentState, err = session.HasCurrentState()
	assert.NoError(t, err)
	assert.False(t, hasCurrentState)
	newIdentityKeyPair, err = libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	matches, err = session.CurrentRatchetKeyMatches(newIdentityKeyPair.GetPublicKey())
	assert.NoError(t, err)
	assert.False(t, matches)

	// A redundant archive shouldn't break anything.
	err = session.ArchiveCurrentState()
	assert.NoError(t, err)

	hasCurrentState, err = session.HasCurrentState()
	assert.NoError(t, err)
	assert.False(t, hasCurrentState)
}

// From SessionTests.swift:testSealedSenderGroupCipher
// TODO: this is not implemented yet
/*
func TestSealedSenderGroupCipher(t *testing.T) {
	setupLogging()
	ctx := libsignalgo.NewEmptyCallbackContext()

	aliceAddress, err := libsignalgo.NewUUIDAddressFromString("9d0652a3-dcc3-4d11-975f-74d61598733f", 1)
	assert.NoError(t, err)
	bobAddress, err := libsignalgo.NewUUIDAddressFromString("6838237D-02F6-4098-B110-698253D15961", 1)
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()
	aliceIdentityKeyPair, err := aliceStore.GetIdentityKeyPair(ctx.Ctx)
	assert.NoError(t, err)

	bobStore := NewInMemorySignalProtocolStore()

	initializeSessions(t, aliceStore, bobStore, bobAddress)

	trustRoot, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	serverKeys, err := libsignalgo.GenerateIdentityKeyPair()
	assert.NoError(t, err)
	serverCert, err := libsignalgo.NewServerCertificate(1, serverKeys.GetPublicKey(), trustRoot.GetPrivateKey())
	assert.NoError(t, err)
	aliceName, err := aliceAddress.Name()
	assert.NoError(t, err)
	senderAddress := libsignalgo.NewSealedSenderAddress("+14151111111", uuid.MustParse(aliceName), 1)
	senderCert, err := libsignalgo.NewSenderCertificate(
		senderAddress,
		aliceIdentityKeyPair.GetPublicKey(),
		time.UnixMilli(31337),
		serverCert,
		serverKeys.GetPrivateKey(),
	)
	assert.NoError(t, err)

	distributionID := uuid.MustParse("d1d1d1d1-7000-11eb-b32a-33b8a8a487a6")

	skdm, err := libsignalgo.NewSenderKeyDistributionMessage(aliceAddress, distributionID, aliceStore, ctx)
	assert.NoError(t, err)
	skdmBytes, err := skdm.Serialize()
	assert.NoError(t, err)

	skdmR, err := libsignalgo.DeserializeSenderKeyDistributionMessage(skdmBytes)
	assert.NoError(t, err)

	err = libsignalgo.ProcessSenderKeyDistributionMessage(skdmR, aliceAddress, bobStore, ctx)
	assert.NoError(t, err)

	aMessage, err := libsignalgo.GroupEncrypt([]byte{1, 2, 3}, aliceAddress, distributionID, aliceStore, ctx)
	assert.NoError(t, err)

	aUSMC, err := libsignalgo.NewUnidentifiedSenderMessageContent(aMessage, senderCert, libsignalgo.UnidentifiedSenderMessageContentHintDefault, []byte{42})
	assert.NoError(t, err)

	aCtext, err := libsignalgo.SealedSenderMultiRecipientEncrypt(aUSMC, []*libsignalgo.Address{bobAddress}, aliceStore, aliceStore, ctx)
	assert.NoError(t, err)

	// TODO: finish
	assert.NotNil(t, aCtext)
}
*/
