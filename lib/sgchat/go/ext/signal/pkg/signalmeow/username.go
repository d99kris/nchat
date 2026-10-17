// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2026 Tulir Asokan
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

package signalmeow

import (
	"context"
	"encoding/base64"
	"fmt"
	"regexp"
	"strings"

	"github.com/google/uuid"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/account"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/common"
)

const SignalUserLinkPrefix = "https://signal.me/#eu/"

var SignalUsernameRegex = regexp.MustCompile(`^@?([A-Za-z0-9_]{3,32})\.(\d{2,9})$`)

func (cli *Client) ResolveUsernameLink(ctx context.Context, link string) (string, error) {
	link = strings.TrimPrefix(link, SignalUserLinkPrefix)
	handle, err := base64.RawURLEncoding.DecodeString(link)
	if err != nil {
		return "", fmt.Errorf("failed to decode link handle: %w", err)
	} else if len(handle) != 48 {
		return "", fmt.Errorf("invalid link handle length: %d", len(handle))
	}
	resp, err := cli.GRPC.AccountsAnonymous.LookupUsernameLink(ctx, &account.LookupUsernameLinkRequest{
		UsernameLinkHandle: handle[32:],
	})
	if err != nil {
		return "", err
	}
	switch respData := resp.Response.(type) {
	case *account.LookupUsernameLinkResponse_NotFound:
		return "", nil
	case *account.LookupUsernameLinkResponse_UsernameCiphertext:
		return libsignalgo.DecryptUsernameLink(handle[:32], respData.UsernameCiphertext)
	default:
		return "", fmt.Errorf("unexpected response type %T", respData)
	}
}

func ParseGRPCServiceID(serviceID *common.ServiceIdentifier) libsignalgo.ServiceID {
	if serviceID == nil || len(serviceID.GetUuid()) != 16 {
		return libsignalgo.EmptyServiceID
	}
	internalUUID := uuid.UUID(serviceID.GetUuid())
	switch serviceID.GetIdentityType() {
	case common.IdentityType_IDENTITY_TYPE_ACI:
		return libsignalgo.NewACIServiceID(internalUUID)
	case common.IdentityType_IDENTITY_TYPE_PNI:
		return libsignalgo.NewPNIServiceID(internalUUID)
	default:
		return libsignalgo.EmptyServiceID
	}
}

func (cli *Client) ResolveUsername(ctx context.Context, username string) (libsignalgo.ServiceID, error) {
	username = strings.TrimPrefix(username, "@")
	hash, err := libsignalgo.HashUsername(username)
	if err != nil {
		return libsignalgo.EmptyServiceID, err
	}
	resp, err := cli.GRPC.AccountsAnonymous.LookupUsernameHash(ctx, &account.LookupUsernameHashRequest{
		UsernameHash: hash[:],
	})
	if err != nil {
		return libsignalgo.EmptyServiceID, err
	}
	switch respData := resp.Response.(type) {
	case *account.LookupUsernameHashResponse_NotFound:
		return libsignalgo.EmptyServiceID, nil
	case *account.LookupUsernameHashResponse_ServiceIdentifier:
		return ParseGRPCServiceID(respData.ServiceIdentifier), nil
	default:
		return libsignalgo.EmptyServiceID, fmt.Errorf("unexpected response type %T", respData)
	}
}
