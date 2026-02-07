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

// From PublicAPITests.swift:testHkdfSimple
func TestHKDF_Simple(t *testing.T) {
	setupLogging()
	inputKeyMaterial := []byte{
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	}
	outputKeyMaterial := []byte{0x8d, 0xa4, 0xe7, 0x75}

	derived, err := libsignalgo.HKDFDerive(len(outputKeyMaterial), inputKeyMaterial, []byte{}, []byte{})
	assert.NoError(t, err)
	assert.Equal(t, outputKeyMaterial, derived)
}

// From PublicAPITests.swift:testHkdfUsingRFCExample
func TestHKDF_RFCExample(t *testing.T) {
	setupLogging()

	var inputKeyMaterial, salt, info []byte
	var i byte
	for i = 0; i <= 0x4f; i++ {
		inputKeyMaterial = append(inputKeyMaterial, i)
	}
	for i = 0x60; i <= 0xaf; i++ {
		salt = append(salt, i)
	}
	for i = 0xb0; i < 0xff; i++ {
		info = append(info, i)
	}
	info = append(info, 0xff)

	outputKeyMaterial := []byte{
		0xb1, 0x1e, 0x39, 0x8d, 0xc8, 0x03, 0x27, 0xa1, 0xc8, 0xe7, 0xf7, 0x8c, 0x59, 0x6a, 0x49, 0x34,
		0x4f, 0x01, 0x2e, 0xda, 0x2d, 0x4e, 0xfa, 0xd8, 0xa0, 0x50, 0xcc, 0x4c, 0x19, 0xaf, 0xa9, 0x7c,
		0x59, 0x04, 0x5a, 0x99, 0xca, 0xc7, 0x82, 0x72, 0x71, 0xcb, 0x41, 0xc6, 0x5e, 0x59, 0x0e, 0x09,
		0xda, 0x32, 0x75, 0x60, 0x0c, 0x2f, 0x09, 0xb8, 0x36, 0x77, 0x93, 0xa9, 0xac, 0xa3, 0xdb, 0x71,
		0xcc, 0x30, 0xc5, 0x81, 0x79, 0xec, 0x3e, 0x87, 0xc1, 0x4c, 0x01, 0xd5, 0xc1, 0xf3, 0x43, 0x4f,
		0x1d, 0x87,
	}

	derived, err := libsignalgo.HKDFDerive(len(outputKeyMaterial), inputKeyMaterial, salt, info)
	assert.NoError(t, err)
	assert.Equal(t, outputKeyMaterial, derived)
}
