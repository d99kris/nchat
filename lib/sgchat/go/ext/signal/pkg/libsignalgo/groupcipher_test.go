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
	"testing"

	"github.com/google/uuid"
	"github.com/stretchr/testify/assert"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
)

// From PublicAPITests.swift:testGroupCipher
func TestGroupCipher(t *testing.T) {
	ctx := context.TODO()

	sender, err := libsignalgo.NewACIServiceID(uuid.New()).Address(4)
	assert.NoError(t, err)

	distributionID, err := uuid.Parse("d1d1d1d1-7000-11eb-b32a-33b8a8a487a6")
	assert.NoError(t, err)

	aliceStore := NewInMemorySignalProtocolStore()

	skdm, err := libsignalgo.NewSenderKeyDistributionMessage(ctx, sender, distributionID, aliceStore)
	assert.NoError(t, err)

	serialized, err := skdm.Serialize()
	assert.NoError(t, err)

	skdmReloaded, err := libsignalgo.DeserializeSenderKeyDistributionMessage(serialized)
	assert.NoError(t, err)

	aliceCiphertextMessage, err := libsignalgo.GroupEncrypt(ctx, []byte{1, 2, 3}, sender, distributionID, aliceStore)
	assert.NoError(t, err)

	aliceCiphertext, err := aliceCiphertextMessage.Serialize()
	assert.NoError(t, err)

	bobStore := NewInMemorySignalProtocolStore()
	err = skdmReloaded.Process(ctx, sender, bobStore)
	assert.NoError(t, err)

	bobPtext, err := libsignalgo.GroupDecrypt(ctx, aliceCiphertext, sender, bobStore)
	assert.NoError(t, err)
	assert.Equal(t, []byte{1, 2, 3}, bobPtext)
}
