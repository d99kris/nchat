// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2024 Tulir Asokan
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

package libsignalgo

/*
#include "./libsignal-ffi.h"
#include <stdlib.h>
*/
import "C"
import (
	"database/sql/driver"
	"fmt"
	"strings"
	"unsafe"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
)

type ServiceIDType byte

const (
	ServiceIDTypeACI ServiceIDType = 0
	ServiceIDTypePNI ServiceIDType = 1
)

func (st ServiceIDType) String() string {
	switch st {
	case ServiceIDTypeACI:
		return "ACI"
	case ServiceIDTypePNI:
		return "PNI"
	default:
		panic(fmt.Sprintf("invalid ServiceIDType: %d", st))
	}
}

func (st ServiceIDType) GoString() string {
	return fmt.Sprintf("libsignalgo.ServiceIDType%s", st.String())
}

type ServiceID struct {
	Type ServiceIDType
	UUID uuid.UUID
}

var EmptyServiceID ServiceID

func NewPNIServiceID(uuid uuid.UUID) ServiceID {
	return ServiceID{
		Type: ServiceIDTypePNI,
		UUID: uuid,
	}
}

func NewACIServiceID(uuid uuid.UUID) ServiceID {
	return ServiceID{
		Type: ServiceIDTypeACI,
		UUID: uuid,
	}
}

func (s ServiceID) ToACIAndPNI() (aci, pni uuid.UUID) {
	if s.Type == ServiceIDTypeACI {
		return s.UUID, uuid.Nil
	} else {
		return uuid.Nil, s.UUID
	}
}

func (s ServiceID) IsEmpty() bool {
	return s.UUID == uuid.Nil
}

func (s ServiceID) Address(deviceID uint) (*Address, error) {
	return newAddress(s.String(), deviceID)
}

func (s ServiceID) Bytes() []byte {
	if s.Type == ServiceIDTypeACI {
		return s.UUID[:]
	}
	return append([]byte{byte(s.Type)}, s.UUID[:]...)
}

func (s ServiceID) Value() (driver.Value, error) {
	return s.String(), nil
}

func (s ServiceID) String() string {
	if s.Type == ServiceIDTypeACI {
		return s.UUID.String()
	}
	return fmt.Sprintf("%s:%s", s.Type, s.UUID)
}

func (s ServiceID) GoString() string {
	return fmt.Sprintf(`libsignalgo.ServiceID{Type: %#v, UUID: uuid.MustParse("%s")}`, s.Type, s.UUID)
}

func (s ServiceID) MarshalText() ([]byte, error) {
	return []byte(s.String()), nil
}

func (s *ServiceID) UnmarshalText(text []byte) error {
	parsed, err := ServiceIDFromString(string(text))
	if err != nil {
		return err
	}
	*s = parsed
	return nil
}

func (s ServiceID) MarshalZerologObject(e *zerolog.Event) {
	e.Stringer("type", s.Type)
	e.Stringer("uuid", s.UUID)
}

type ServiceIDFixedBytes [17]byte

func (s ServiceID) FixedBytes() *ServiceIDFixedBytes {
	var result ServiceIDFixedBytes
	result[0] = byte(s.Type)
	copy(result[1:], s.UUID[:])
	return &result
}

func ServiceIDFromString(val string) (ServiceID, error) {
	if len(val) < 36 {
		return EmptyServiceID, fmt.Errorf("invalid UUID string: %s", val)
	}
	if strings.ToUpper(val[:4]) == "PNI:" {
		parsed, err := uuid.Parse(val[4:])
		if err != nil {
			return EmptyServiceID, err
		}
		return NewPNIServiceID(parsed), nil
	} else {
		parsed, err := uuid.Parse(val)
		if err != nil {
			return EmptyServiceID, err
		}
		return NewACIServiceID(parsed), nil
	}
}

func ServiceIDFromCFixedBytes(serviceID *C.SignalServiceIdFixedWidthBinaryBytes) ServiceID {
	var id ServiceID
	fixedBytes := (*ServiceIDFixedBytes)(unsafe.Pointer(serviceID))
	id.Type = ServiceIDType(fixedBytes[0])
	copy(id.UUID[:], fixedBytes[1:])
	return id
}

func (s ServiceID) CFixedBytes() cPNIType {
	return cPNIType(unsafe.Pointer(s.FixedBytes()))
}
