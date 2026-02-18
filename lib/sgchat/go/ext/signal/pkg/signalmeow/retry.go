// mautrix-signal - A Matrix-signal puppeting bridge.
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

package signalmeow

import (
	"context"
	"fmt"
	"slices"
	"time"

	"github.com/rs/zerolog"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

type sendCacheKey struct {
	recipient libsignalgo.ServiceID
	groupID   types.GroupIdentifier
	timestamp uint64
}

const RetryRespondMaxAge = 30 * 24 * time.Hour

func (cli *Client) sendRetryRequest(ctx context.Context, result DecryptionResult, originalTS uint64) error {
	serviceID, err := result.SenderAddress.NameServiceID()
	if err != nil {
		return fmt.Errorf("failed to get sender name as service ID: %w", err)
	}
	deviceID, err := result.SenderAddress.DeviceID()
	if err != nil {
		return fmt.Errorf("failed to get sender device ID: %w", err)
	}
	dem, err := libsignalgo.DecryptionErrorMessageForOriginalMessage(result.Ciphertext, result.CiphertextType, originalTS, deviceID)
	if err != nil {
		return fmt.Errorf("failed to create decryption error message: %w", err)
	}
	demBytes, err := dem.Serialize()
	if err != nil {
		return fmt.Errorf("failed to serialize decryption error message: %w", err)
	}
	ptc, err := libsignalgo.PlaintextContentFromDecryptionErrorMessage(dem)
	if err != nil {
		return fmt.Errorf("failed to create plaintext content from decryption error message: %w", err)
	}
	ctm, err := libsignalgo.NewCiphertextMessage(ptc)
	if err != nil {
		return fmt.Errorf("failed to create ciphertext message from plaintext content: %w", err)
	}
	_, err = cli.sendContent(ctx, serviceID, uint64(time.Now().UnixMilli()), &signalpb.Content{
		DecryptionErrorMessage: demBytes,
	}, 0, true, result.GroupID, ctm)
	if err != nil {
		return fmt.Errorf("failed to send decryption error message: %w", err)
	}
	zerolog.Ctx(ctx).Debug().
		Stringer("sender_service_id", serviceID).
		Uint("sender_device_id", deviceID).
		Stringer("group_id", result.GroupID).
		Msg("Sent retry receipt")
	return nil
}

func (cli *Client) handleRetryRequest(
	ctx context.Context,
	result DecryptionResult,
	dem *libsignalgo.DecryptionErrorMessage,
) error {
	destDeviceID, err := dem.GetDeviceID()
	if err != nil {
		return fmt.Errorf("failed to get device ID from decryption error message: %w", err)
	} else if int(destDeviceID) != cli.Store.DeviceID {
		zerolog.Ctx(ctx).Debug().
			Uint32("dest_device_id", destDeviceID).
			Msg("Ignoring decryption error message for another device")
		return nil
	}
	serviceID, err := result.SenderAddress.NameServiceID()
	if err != nil {
		return fmt.Errorf("failed to get sender name as service ID: %w", err)
	}
	deviceID, err := result.SenderAddress.DeviceID()
	if err != nil {
		return fmt.Errorf("failed to get sender device ID: %w", err)
	}
	ts, err := dem.GetTimestamp()
	if err != nil {
		return fmt.Errorf("failed to get timestamp: %w", err)
	}

	cli.encryptionLock.Lock()
	defer cli.encryptionLock.Unlock()
	ctx = context.WithValue(ctx, contextKeyEncryptionLock, true)
	var didArchiveSession bool
	if ratchetKey, err := dem.GetRatchetKey(); err != nil {
		return fmt.Errorf("failed to get ratchet key: %w", err)
	} else if ratchetKey == nil {
		// No need to archive session if no ratchet key is provided, it was probably a sender key decryption error
	} else if session, err := cli.Store.ACISessionStore.LoadSession(ctx, result.SenderAddress); err != nil {
		return fmt.Errorf("failed to load session for sender: %w", err)
	} else if match, err := session.CurrentRatchetKeyMatches(ratchetKey); err != nil {
		return fmt.Errorf("failed to check ratchet key match: %w", err)
	} else if match {
		err = session.ArchiveCurrentState()
		if err != nil {
			return fmt.Errorf("failed to archive current session state: %w", err)
		}
		err = cli.Store.ACISessionStore.StoreSession(ctx, result.SenderAddress, session)
		if err != nil {
			return fmt.Errorf("failed to store archived session: %w", err)
		}
		didArchiveSession = true
	}
	var skdmBytes []byte
	groupID := types.BytesToGroupIdentifier(result.GroupID)
	if groupID != "" {
		ski, err := cli.Store.SenderKeyStore.GetSenderKeyInfo(ctx, groupID)
		if err != nil {
			return fmt.Errorf("failed to get sender key info for group %s: %w", groupID, err)
		}
		myAddress, err := cli.Store.ACIServiceID().Address(uint(cli.Store.DeviceID))
		if err != nil {
			return fmt.Errorf("failed to get own address: %w", err)
		}
		if slices.Contains(ski.SharedWith[serviceID], int(deviceID)) {
			skdm, err := libsignalgo.NewSenderKeyDistributionMessage(ctx, myAddress, ski.DistributionID, cli.Store.SenderKeyStore)
			if err != nil {
				return fmt.Errorf("failed to create sender key distribution message: %w", err)
			}
			skdmBytes, err = skdm.Serialize()
			if err != nil {
				return fmt.Errorf("failed to serialize sender key distribution message: %w", err)
			}
		} else {
			zerolog.Ctx(ctx).Warn().
				Stringer("group_id", result.GroupID).
				Stringer("sender_service_id", serviceID).
				Stringer("distribution_id", ski.DistributionID).
				Uint("sender_device_id", deviceID).
				Ints("shared_with", ski.SharedWith[serviceID]).
				Msg("Sender key distribution list doesn't contain retry receipt sender")
		}
	}
	var retryContent *signalpb.Content
	var cacheHit bool
	if time.Since(time.UnixMilli(int64(ts))) < RetryRespondMaxAge {
		retryContent, cacheHit = cli.sendCache.Get(sendCacheKey{
			groupID:   groupID,
			recipient: serviceID,
			timestamp: ts,
		})
		if !cacheHit {
			// TODO add support for external caches
		}
	}
	if retryContent == nil {
		retryContent = &signalpb.Content{}
	}
	retryContent.SenderKeyDistributionMessage = skdmBytes
	if !cacheHit && skdmBytes == nil {
		if !didArchiveSession {
			zerolog.Ctx(ctx).Debug().
				Uint64("msg_timestamp", ts).
				Stringer("sender_service_id", serviceID).
				Uint("sender_device_id", deviceID).
				Stringer("group_id", result.GroupID).
				Msg("Not responding to decryption error message")
			return nil
		}
		retryContent.NullMessage = &signalpb.NullMessage{}
	}
	responseTimestamp := uint64(time.Now().UnixMilli())
	if cacheHit {
		responseTimestamp = ts
	}
	zerolog.Ctx(ctx).Debug().
		Uint32("dest_device_id", destDeviceID).
		Uint64("requested_msg_timestamp", ts).
		Stringer("sender_service_id", serviceID).
		Uint("sender_device_id", deviceID).
		Stringer("group_id", result.GroupID).
		Bool("did_archive_session", didArchiveSession).
		Bool("found_message_in_cache", cacheHit).
		Bool("including_skdm", skdmBytes != nil).
		Msg("Responding to decryption error message")
	_, err = cli.sendContent(ctx, serviceID, responseTimestamp, retryContent, 0, true, result.GroupID, nil)
	if err != nil {
		return fmt.Errorf("failed to send response: %w", err)
	}
	return nil
}
