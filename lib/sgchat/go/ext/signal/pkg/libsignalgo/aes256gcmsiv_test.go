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

// From PublicAPITests.swift:testAesGcmSiv
func TestAES256_GCM_SIV(t *testing.T) {
	plaintext := []byte{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
	expectedCiphertext := []byte{0x1d, 0xe2, 0x29, 0x67, 0x23, 0x7a, 0x81, 0x32, 0x91, 0x21, 0x3f, 0x26, 0x7e, 0x3b, 0x45, 0x2f, 0x02, 0xd0, 0x1a, 0xe3, 0x3e, 0x4e, 0xc8, 0x54}
	associatedData := []byte{0x01}
	key := []byte{
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	}
	nonce := []byte{0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

	gcmSiv, err := libsignalgo.NewAES256_GCM_SIV(key)
	assert.NoError(t, err)

	ciphertext, err := gcmSiv.Encrypt(plaintext, nonce, associatedData)
	assert.NoError(t, err)
	assert.Equal(t, expectedCiphertext, ciphertext)

	recovered, err := gcmSiv.Decrypt(ciphertext, nonce, associatedData)
	assert.NoError(t, err)
	assert.Equal(t, plaintext, recovered)

	_, err = gcmSiv.Decrypt(plaintext, nonce, associatedData)
	assert.Error(t, err)
	_, err = gcmSiv.Decrypt(ciphertext, associatedData, nonce)
	assert.Error(t, err)
}
