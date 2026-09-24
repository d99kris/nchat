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

package web

import (
	"context"
	"encoding/base64"
	"errors"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"

	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/account"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/attachments"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/backups"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/call_quality"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/calling"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/challenge"
	scredentials "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/credentials"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/device"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/donations"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/keys"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/login_purchase"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/messages"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/one_time_donations"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/payments"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/product_configuration"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/profile"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/remote_configuration"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/rpc/subscriptions"
)

type GRPCAuthHeader struct {
	value string
}

var _ credentials.PerRPCCredentials = (*GRPCAuthHeader)(nil)

func basicAuth(username, password string) string {
	auth := username + ":" + password
	return base64.StdEncoding.EncodeToString([]byte(auth))
}

func (a *GRPCAuthHeader) GetRequestMetadata(ctx context.Context, uri ...string) (map[string]string, error) {
	return map[string]string{
		"authorization": a.value,
	}, nil
}

func (a *GRPCAuthHeader) RequireTransportSecurity() bool {
	return true
}

type GRPCClient struct {
	AuthConn   *grpc.ClientConn
	UnauthConn *grpc.ClientConn

	Accounts            account.AccountsClient
	Attachments         attachments.AttachmentsClient
	Backups             backups.BackupsClient
	Calling             calling.CallingClient
	Challenge           challenge.ChallengeClient
	Credentials         scredentials.CredentialsClient
	Devices             device.DevicesClient
	Donations           donations.DonationsClient
	Keys                keys.KeysClient
	Messages            messages.MessagesClient
	Payments            payments.PaymentsClient
	Profile             profile.ProfileClient
	RemoteConfiguration remote_configuration.RemoteConfigurationClient

	AccountsAnonymous    account.AccountsAnonymousClient
	BackupsAnonymous     backups.BackupsAnonymousClient
	CredentialsAnonymous scredentials.CredentialsAnonymousClient
	KeysAnonymous        keys.KeysAnonymousClient
	MessagesAnonymous    messages.MessagesAnonymousClient
	ProfileAnonymous     profile.ProfileAnonymousClient
	CallQuality          call_quality.CallQualityClient
	LoginPurchase        login_purchase.LoginPurchaseClient
	OneTimeDonations     one_time_donations.OneTimeDonationsClient
	ProductConfiguration product_configuration.ProductConfigurationClient
	Subscriptions        subscriptions.SubscriptionsClient
}

func (gc *GRPCClient) Close() error {
	if gc == nil {
		return nil
	}
	return errors.Join(
		gc.AuthConn.Close(),
		gc.UnauthConn.Close(),
	)
}

func (gc *GRPCClient) ResetConnectBackoff() {
	if gc == nil {
		return
	}
	gc.AuthConn.ResetConnectBackoff()
	gc.UnauthConn.ResetConnectBackoff()
}

const GRPCTarget = "dns://grpc.chat.signal.org:443"

func NewGRPCClient(username, password string) (*GRPCClient, error) {
	grpcTLSConfig := credentials.NewTLS(SignalTLSConfig)
	authConn, err := grpc.NewClient(
		GRPCTarget,
		grpc.WithUserAgent(UserAgent),
		grpc.WithTransportCredentials(grpcTLSConfig),
		grpc.WithPerRPCCredentials(&GRPCAuthHeader{value: "Basic " + basicAuth(username, password)}),
	)
	if err != nil {
		return nil, err
	}
	unauthConn, err := grpc.NewClient(
		GRPCTarget,
		grpc.WithUserAgent(UserAgent),
		grpc.WithTransportCredentials(grpcTLSConfig),
	)
	if err != nil {
		_ = authConn.Close()
		return nil, err
	}
	return &GRPCClient{
		AuthConn:   authConn,
		UnauthConn: unauthConn,

		Accounts:            account.NewAccountsClient(authConn),
		Attachments:         attachments.NewAttachmentsClient(authConn),
		Backups:             backups.NewBackupsClient(authConn),
		Calling:             calling.NewCallingClient(authConn),
		Challenge:           challenge.NewChallengeClient(authConn),
		Credentials:         scredentials.NewCredentialsClient(authConn),
		Devices:             device.NewDevicesClient(authConn),
		Donations:           donations.NewDonationsClient(authConn),
		Keys:                keys.NewKeysClient(authConn),
		Messages:            messages.NewMessagesClient(authConn),
		Payments:            payments.NewPaymentsClient(authConn),
		Profile:             profile.NewProfileClient(authConn),
		RemoteConfiguration: remote_configuration.NewRemoteConfigurationClient(authConn),

		AccountsAnonymous:    account.NewAccountsAnonymousClient(unauthConn),
		BackupsAnonymous:     backups.NewBackupsAnonymousClient(unauthConn),
		CredentialsAnonymous: scredentials.NewCredentialsAnonymousClient(unauthConn),
		KeysAnonymous:        keys.NewKeysAnonymousClient(unauthConn),
		MessagesAnonymous:    messages.NewMessagesAnonymousClient(unauthConn),
		ProfileAnonymous:     profile.NewProfileAnonymousClient(unauthConn),
		CallQuality:          call_quality.NewCallQualityClient(unauthConn),
		LoginPurchase:        login_purchase.NewLoginPurchaseClient(unauthConn),
		OneTimeDonations:     one_time_donations.NewOneTimeDonationsClient(unauthConn),
		ProductConfiguration: product_configuration.NewProductConfigurationClient(unauthConn),
		Subscriptions:        subscriptions.NewSubscriptionsClient(unauthConn),
	}, nil
}
