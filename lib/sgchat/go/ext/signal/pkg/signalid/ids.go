// mautrix-signal - A Matrix-Signal puppeting bridge.
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

package signalid

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/google/uuid"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

func ParseUserID(userID networkid.UserID) (uuid.UUID, error) {
	serviceID, err := ParseUserIDAsServiceID(userID)
	if err != nil {
		return uuid.Nil, err
	} else if serviceID.Type != libsignalgo.ServiceIDTypeACI {
		return uuid.Nil, fmt.Errorf("invalid user ID: expected ACI type")
	} else {
		return serviceID.UUID, nil
	}
}

func ParseUserLoginID(userLoginID networkid.UserLoginID) (uuid.UUID, error) {
	userID, err := uuid.Parse(string(userLoginID))
	if err != nil {
		return uuid.Nil, err
	}
	return userID, nil
}

func toServiceID(id uuid.UUID, err error) (libsignalgo.ServiceID, error) {
	if err != nil {
		return libsignalgo.ServiceID{}, err
	}
	return libsignalgo.NewACIServiceID(id), nil
}

func ParseGhostOrUserLoginID(ghostOrUserLogin bridgev2.GhostOrUserLogin) (libsignalgo.ServiceID, error) {
	switch ghostOrUserLogin := ghostOrUserLogin.(type) {
	case *bridgev2.UserLogin:
		return toServiceID(ParseUserLoginID(ghostOrUserLogin.ID))
	case *bridgev2.Ghost:
		return ParseUserIDAsServiceID(ghostOrUserLogin.ID)
	default:
		return libsignalgo.ServiceID{}, fmt.Errorf("cannot parse ID: unknown type: %T", ghostOrUserLogin)
	}
}

const pniUserIDPrefix = "pni_"
const pniServiceIDPrefix = "PNI:"

func ParseUserIDAsServiceID(userID networkid.UserID) (libsignalgo.ServiceID, error) {
	userIDStr := string(userID)
	if strings.HasPrefix(userIDStr, pniUserIDPrefix) {
		userIDStr = pniServiceIDPrefix + userIDStr[len(pniUserIDPrefix):]
	}
	return libsignalgo.ServiceIDFromString(userIDStr)
}

func ParsePortalID(portalID networkid.PortalID) (userID libsignalgo.ServiceID, groupID types.GroupIdentifier, err error) {
	if len(portalID) == 44 {
		groupID = types.GroupIdentifier(portalID)
	} else {
		userID, err = libsignalgo.ServiceIDFromString(string(portalID))
	}
	return
}

func ParseMessageID(messageID networkid.MessageID) (sender uuid.UUID, timestamp uint64, err error) {
	parts := strings.Split(string(messageID), "|")
	if len(parts) != 2 {
		err = fmt.Errorf("invalid message ID: expected two pipe-separated parts")
		return
	}
	sender, err = uuid.Parse(parts[0])
	if err != nil {
		return
	}
	timestamp, err = strconv.ParseUint(parts[1], 10, 64)
	return
}

func MakeGroupPortalID(groupID types.GroupIdentifier) networkid.PortalID {
	return networkid.PortalID(groupID)
}

func MakeDMPortalID(serviceID libsignalgo.ServiceID) networkid.PortalID {
	return networkid.PortalID(serviceID.String())
}

func MakeMessageID(sender uuid.UUID, timestamp uint64) networkid.MessageID {
	return networkid.MessageID(fmt.Sprintf("%s|%d", sender, timestamp))
}

func MakeUserID(user uuid.UUID) networkid.UserID {
	return networkid.UserID(user.String())
}

func MakeUserIDFromServiceID(user libsignalgo.ServiceID) networkid.UserID {
	switch user.Type {
	case libsignalgo.ServiceIDTypeACI:
		return MakeUserID(user.UUID)
	case libsignalgo.ServiceIDTypePNI:
		return networkid.UserID(pniUserIDPrefix + user.UUID.String())
	default:
		panic(fmt.Errorf("invalid service ID type %d", user.Type))
	}
}

func MakeUserLoginID(user uuid.UUID) networkid.UserLoginID {
	return networkid.UserLoginID(user.String())
}

func MakeMessagePartID(index int) networkid.PartID {
	if index == 0 {
		return ""
	}
	return networkid.PartID(strconv.Itoa(index))
}
