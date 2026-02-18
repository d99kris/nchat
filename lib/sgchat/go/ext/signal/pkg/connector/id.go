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

package connector

import (
	"fmt"

	"github.com/google/uuid"
	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/networkid"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalid"
)

func (s *SignalClient) makePortalKey(chatID string) networkid.PortalKey {
	key := networkid.PortalKey{ID: networkid.PortalID(chatID)}
	// For non-group chats, add receiver
	if s.Main.Bridge.Config.SplitPortals || len(chatID) != 44 {
		key.Receiver = s.UserLogin.ID
	}
	return key
}

func (s *SignalClient) makeDMPortalKey(serviceID libsignalgo.ServiceID) networkid.PortalKey {
	return networkid.PortalKey{
		ID:       signalid.MakeDMPortalID(serviceID),
		Receiver: s.UserLogin.ID,
	}
}

func (s *SignalClient) makeEventSender(sender uuid.UUID) bridgev2.EventSender {
	return bridgev2.EventSender{
		IsFromMe:    sender == s.Client.Store.ACI,
		SenderLogin: signalid.MakeUserLoginID(sender),
		Sender:      signalid.MakeUserID(sender),
	}
}

func (s *SignalClient) makePNIEventSender(sender uuid.UUID) bridgev2.EventSender {
	return bridgev2.EventSender{
		Sender: signalid.MakeUserIDFromServiceID(libsignalgo.NewPNIServiceID(sender)),
	}
}

func (s *SignalClient) makeEventSenderFromServiceID(serviceID libsignalgo.ServiceID) bridgev2.EventSender {
	switch serviceID.Type {
	case libsignalgo.ServiceIDTypeACI:
		return s.makeEventSender(serviceID.UUID)
	case libsignalgo.ServiceIDTypePNI:
		return s.makePNIEventSender(serviceID.UUID)
	default:
		panic(fmt.Errorf("invalid service ID type %d", serviceID.Type))
	}
}
