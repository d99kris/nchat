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

// From PublicAPITests.swift:testDeviceTransferKey
func TestDeviceTransferKey(t *testing.T) {
	deviceKey, err := libsignalgo.GenerateDeviceTransferKey()
	assert.NoError(t, err)

	/*
		Anything encoded in an ASN.1 SEQUENCE starts with 0x30 when encoded
		as DER. (This test could be better.)
	*/
	key := deviceKey.PrivateKeyMaterial()
	assert.Greater(t, len(key), 0)
	assert.EqualValues(t, 0x30, key[0])

	cert, err := deviceKey.GenerateCertificate("name", 30)
	assert.NoError(t, err)
	assert.Greater(t, len(cert), 0)
	assert.EqualValues(t, 0x30, cert[0])
}
