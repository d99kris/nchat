// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2025 Tulir Asokan
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
	"errors"

	"maunium.net/go/mautrix/bridgev2"
	"maunium.net/go/mautrix/bridgev2/commands"
	"maunium.net/go/mautrix/bridgev2/networkid"

	"go.mau.fi/mautrix-signal/pkg/signalid"
)

var CmdDiscardSenderKey = &commands.FullHandler{
	Func: fnDiscardSenderKey,
	Name: "discard-sender-key",
	Help: commands.HelpMeta{
		Section:     commands.HelpSectionChats,
		Description: "Discard the Signal-side sender key in the current group",
		Args:        "[_login ID_]",
	},
	RequiresPortal: true,
	RequiresLogin:  true,
}

func fnDiscardSenderKey(ce *commands.Event) {
	_, groupID, _ := signalid.ParsePortalID(ce.Portal.ID)
	if groupID == "" {
		ce.Reply("This command can only be used in group chat portals")
		return
	}
	var login *bridgev2.UserLogin
	if len(ce.Args) > 0 {
		login = ce.Bridge.GetCachedUserLoginByID(networkid.UserLoginID(ce.Args[0]))
		if login == nil || login.UserMXID != ce.User.MXID {
			ce.Reply("Login not found")
			return
		}
	} else {
		var err error
		login, _, err = ce.Portal.FindPreferredLogin(ce.Ctx, ce.User, false)
		if errors.Is(err, bridgev2.ErrNotLoggedIn) {
			ce.Reply("You're not logged in in this portal")
			return
		} else if err != nil {
			ce.Log.Err(err).Msg("Failed to find preferred login for portal")
			ce.Reply("Failed to find preferred login for portal")
			return
		}
	}
	distributionID, err := login.Client.(*SignalClient).Client.ResetSenderKey(ce.Ctx, groupID)
	if err != nil {
		ce.Log.Err(err).Msg("Failed to reset sender key")
		ce.Reply("Failed to reset sender key")
	} else {
		ce.Reply("Reset sender key with distribution ID %s", distributionID)
	}
}
