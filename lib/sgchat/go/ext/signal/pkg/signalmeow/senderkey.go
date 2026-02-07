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
	"encoding/json"
	"errors"
	"fmt"
	"maps"
	"net/http"
	"slices"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exslices"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

const SenderKeyMaxAge = 14 * 24 * time.Hour

type contextKey int

const (
	contextKeyEncryptionLock contextKey = iota
)

func (cli *Client) ResetSenderKey(ctx context.Context, groupID types.GroupIdentifier) (uuid.UUID, error) {
	cli.encryptionLock.Lock()
	defer cli.encryptionLock.Unlock()
	info, err := cli.Store.SenderKeyStore.GetSenderKeyInfo(ctx, groupID)
	if err != nil {
		return uuid.Nil, fmt.Errorf("failed to get sender key info: %w", err)
	} else if info == nil {
		return uuid.Nil, nil
	} else if myAddress, err := cli.Store.ACIServiceID().Address(uint(cli.Store.DeviceID)); err != nil {
		return uuid.Nil, fmt.Errorf("failed to get own address: %w", err)
	} else if err = cli.Store.SenderKeyStore.DeleteSenderKey(ctx, myAddress, info.DistributionID); err != nil {
		return info.DistributionID, fmt.Errorf("failed to delete sender key: %w", err)
	} else if err = cli.Store.SenderKeyStore.DeleteSenderKeyInfo(ctx, groupID); err != nil {
		return info.DistributionID, fmt.Errorf("failed to delete sender key info: %w", err)
	}
	return info.DistributionID, nil
}

func (cli *Client) sendToGroupWithSenderKey(
	ctx context.Context,
	groupID *libsignalgo.GroupIdentifier,
	allRecipients []libsignalgo.ServiceID,
	sec SendEndorsementCache,
	content *signalpb.Content,
	messageTimestamp uint64,
	retries int,
) (*GroupMessageSendResult, error) {
	if retries >= 3 {
		return cli.sendToGroup(ctx, allRecipients, content, messageTimestamp, nil, groupID)
	}
	myAddress, err := cli.Store.ACIServiceID().Address(uint(cli.Store.DeviceID))
	if err != nil {
		return nil, fmt.Errorf("failed to get own address: %w", err)
	}
	log := zerolog.Ctx(ctx)

	cli.encryptionLock.Lock()
	unlocked := false
	doUnlock := func() {
		if !unlocked {
			unlocked = true
			cli.encryptionLock.Unlock()
		}
	}
	defer doUnlock()
	ctx = context.WithValue(ctx, contextKeyEncryptionLock, true)
	result := &GroupMessageSendResult{
		SuccessfullySentTo: make([]SuccessfulSendResult, 0),
		FailedToSendTo:     make([]FailedSendResult, 0),
	}

	groupIDStr := types.GroupIdentifier(groupID.String())
	deviceIDs, senderKeyRecipients, fallbackRecipients := cli.getDevicesIDs(ctx, allRecipients, sec, result)
	if len(senderKeyRecipients) == 0 {
		doUnlock()
		log.Debug().Msg("No sender key recipients, falling back to normal send")
		return cli.sendToGroup(ctx, allRecipients, content, messageTimestamp, result, groupID)
	}
	ski, err := cli.Store.SenderKeyStore.GetSenderKeyInfo(ctx, groupIDStr)
	if err != nil {
		return nil, fmt.Errorf("failed to get sender key info: %w", err)
	} else if ski == nil || time.Since(ski.CreatedAt) > SenderKeyMaxAge {
		if ski != nil && time.Since(ski.CreatedAt) > SenderKeyMaxAge {
			log.Debug().Any("old_sender_key_info", ski).Msg("Sender key expired, creating new one")
			err = cli.Store.SenderKeyStore.DeleteSenderKey(ctx, myAddress, ski.DistributionID)
			if err != nil {
				return nil, fmt.Errorf("failed to delete old sender key: %w", err)
			}
		} else {
			log.Debug().Msg("No existing sender key, creating new one")
		}
		ski = &store.SenderKeyInfo{
			DistributionID: uuid.New(),
			CreatedAt:      time.Now(),
			SharedWith:     make(map[libsignalgo.ServiceID][]int),
		}
	} else {
		log.Debug().Any("sender_key_info", ski).Msg("Reusing existing sender key")
	}
	xak, devicesAddedTo, removedDevices := diffRecipients(ski.SharedWith, deviceIDs)
	if len(removedDevices) > 0 {
		log.Debug().
			Any("removed_devices", removedDevices).
			Msg("Resetting sender key due to recipient device changes")
		devicesAddedTo = slices.Collect(maps.Keys(deviceIDs))
		err = cli.Store.SenderKeyStore.DeleteSenderKey(ctx, myAddress, ski.DistributionID)
		if err != nil {
			return nil, fmt.Errorf("failed to delete old sender key: %w", err)
		}
	}
	if len(devicesAddedTo) > 0 {
		log.Debug().
			Any("devices_added_to", devicesAddedTo).
			Msg("Sending sender key distribution message to users with new devices")
		skdm, err := libsignalgo.NewSenderKeyDistributionMessage(ctx, myAddress, ski.DistributionID, cli.Store.SenderKeyStore)
		if err != nil {
			return nil, fmt.Errorf("failed to create sender key distribution message: %w", err)
		}
		skdmBytes, err := skdm.Serialize()
		if err != nil {
			return nil, fmt.Errorf("failed to serialize sender key distribution message: %w", err)
		}
		var needsRetry bool
		for _, recipient := range devicesAddedTo {
			log := log.With().Str("subaction", "skdm").Stringer("recipient_id", recipient).Logger()
			_, err = cli.sendContent(log.WithContext(ctx), recipient, messageTimestamp, &signalpb.Content{
				SenderKeyDistributionMessage: skdmBytes,
			}, 0, true, groupID, nil)
			if errors.Is(err, ErrDevicesChanged) || errors.Is(err, ErrUnregisteredUser) {
				log.Warn().Err(err).Msg("Failed to send sender key distribution message due to device changes, will retry")
				needsRetry = true
			} else if err != nil {
				log.Err(err).Msg("Failed to send sender key distribution message")
				fallbackRecipients = append(fallbackRecipients, recipient)
				delete(deviceIDs, recipient)
				senderKeyRecipients = slices.DeleteFunc(senderKeyRecipients, func(tuple store.SessionAddressTuple) bool {
					return tuple.ServiceID == recipient
				})
			} else {
				log.Debug().Msg("Successfully sent sender key distribution message")
				ski.SharedWith[recipient] = deviceIDs[recipient].DeviceIDs
			}
		}
		err = cli.Store.SenderKeyStore.PutSenderKeyInfo(ctx, groupIDStr, ski)
		if err != nil {
			return nil, fmt.Errorf("failed to store updated sender key info: %w", err)
		}
		if needsRetry {
			doUnlock()
			return cli.sendToGroupWithSenderKey(ctx, groupID, allRecipients, sec, content, messageTimestamp, retries+1)
		}
	}
	ssCiphertext, err := cli.encryptWithSenderKey(ctx, groupID, ski.DistributionID, myAddress, senderKeyRecipients, content)
	if err != nil {
		return nil, err
	}
	for recipientID := range ski.SharedWith {
		cli.addSendCache(recipientID, groupIDStr, messageTimestamp, content)
	}
	header := http.Header{}
	header.Set("Content-Type", string(web.ContentTypeMultiRecipientMessage))
	if sec.SendEndorsement != nil {
		wantedEndorsements := make([]libsignalgo.GroupSendEndorsement, 0, len(deviceIDs))
		for serviceID := range deviceIDs {
			endorsement, ok := sec.MemberEndorsements[serviceID]
			if !ok {
				return nil, fmt.Errorf("missing group send endorsement for service ID %s", serviceID.String())
			}
			wantedEndorsements = append(wantedEndorsements, endorsement)
		}
		combinedEndorsement, err := libsignalgo.GroupSendEndorsementCombine(wantedEndorsements...)
		if err != nil {
			return nil, fmt.Errorf("failed to combine group send endorsements: %w", err)
		}
		groupSendToken, err := sec.GetTokenWith(combinedEndorsement)
		if err != nil {
			return nil, fmt.Errorf("failed to create group send full token: %w", err)
		}
		header.Set("Group-Send-Token", groupSendToken.String())
	} else {
		header.Set("Unidentified-Access-Key", xak.String())
	}
	path := fmt.Sprintf(
		"/v1/messages/multi_recipient?ts=%d&urgent=%t&online=false",
		messageTimestamp, isUrgent(content),
	)
	log.Debug().
		Any("recipients", ski.SharedWith).
		Any("fallback_recipients", fallbackRecipients).
		Msg("Sending multi-recipient message with sender key")
	resp, err := cli.UnauthedWS.SendRequest(ctx, http.MethodPut, path, ssCiphertext, header)
	switch resp.GetStatus() {
	case 200:
		var respData MultiRecipient200Response
		err = json.Unmarshal(resp.Body, &respData)
		if err != nil {
			return nil, fmt.Errorf("failed to unmarshal 200 response: %w", err)
		}
		log.Debug().
			Any("response_data", respData).
			Msg("Got successful multi-recipient send response")
		for serviceID := range deviceIDs {
			if slices.Contains(respData.UUIDs404, serviceID) {
				err = cli.Store.ACISessionStore.RemoveAllSessionsForServiceID(ctx, serviceID)
				if err != nil {
					log.Err(err).Stringer("recipient_id", serviceID).
						Msg("Failed to remove sessions after 404")
				}
				cli.Store.RecipientStore.MarkUnregistered(ctx, serviceID, true)
				result.FailedToSendTo = append(result.FailedToSendTo, FailedSendResult{
					Recipient: serviceID,
					Error:     fmt.Errorf("multi-recipient send 404"),
				})
			} else {
				result.SuccessfullySentTo = append(result.SuccessfullySentTo, SuccessfulSendResult{
					Recipient:    serviceID,
					Unidentified: true,
				})
			}
		}
		doUnlock()
		// Send with fallback for any recipients that couldn't do sender key, plus our own sync copy
		return cli.sendToGroup(ctx, fallbackRecipients, content, messageTimestamp, result, groupID)
	case 401, 404:
		log.Warn().Uint32("status_code", resp.GetStatus()).
			Msg("Multi-recipient send failed, falling back to normal send")
		doUnlock()
		// Fall back to normal send for all recipients
		return cli.sendToGroup(ctx, allRecipients, content, messageTimestamp, nil, groupID)
	case 409, 410:
		log.Warn().Uint32("status_code", resp.GetStatus()).
			Msg("Multi-recipient send failed due to outdated device list, refreshing and retrying")
		err = cli.handleMultiRecipient409410Response(ctx, resp)
		if err != nil {
			return nil, err
		}
		doUnlock()
		// Retry recursively after fixing device lists
		return cli.sendToGroupWithSenderKey(ctx, groupID, allRecipients, sec, content, messageTimestamp, retries+1)
	default:
		return nil, fmt.Errorf("unexpected status code %d in multi-recipient send", resp.GetStatus())
	}
}

func (cli *Client) encryptWithSenderKey(
	ctx context.Context,
	groupID *libsignalgo.GroupIdentifier,
	distributionID uuid.UUID,
	myAddress *libsignalgo.Address,
	senderKeyRecipients []store.SessionAddressTuple,
	content *signalpb.Content,
) ([]byte, error) {
	plaintext, err := proto.Marshal(content)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal content: %w", err)
	}
	plaintext, err = addPadding(3, plaintext)
	if err != nil {
		return nil, fmt.Errorf("failed to add padding: %w", err)
	}
	ciphertext, err := libsignalgo.GroupEncrypt(ctx, plaintext, myAddress, distributionID, cli.Store.SenderKeyStore)
	if err != nil {
		return nil, fmt.Errorf("failed to encrypt group message: %w", err)
	}
	cert, err := cli.senderCertificate(ctx, false)
	if err != nil {
		return nil, fmt.Errorf("failed to get sender certificate: %w", err)
	}
	usmc, err := libsignalgo.NewUnidentifiedSenderMessageContent(ciphertext, cert, getContentHint(content), groupID)
	if err != nil {
		return nil, fmt.Errorf("failed to create unidentified sender message content: %w", err)
	}
	ssCiphertext, err := libsignalgo.SealedSenderMultiRecipientEncrypt(ctx, usmc, senderKeyRecipients, cli.Store.ACIIdentityStore)
	if err != nil {
		return nil, fmt.Errorf("failed to create sealed sender multi-recipient message: %w", err)
	}
	return ssCiphertext, nil
}

func diffRecipients(
	prevDevices map[libsignalgo.ServiceID][]int,
	newDevices map[libsignalgo.ServiceID]senderKeySendMeta,
) (
	xak *libsignalgo.AccessKey,
	devicesAddedTo []libsignalgo.ServiceID,
	globalRemovedDevices map[libsignalgo.ServiceID][]int,
) {
	collector := make(map[libsignalgo.ServiceID]uint8, max(len(prevDevices), len(newDevices)))
	for key := range prevDevices {
		collector[key] |= 0b01
	}
	for key := range newDevices {
		collector[key] |= 0b10
	}
	globalRemovedDevices = make(map[libsignalgo.ServiceID][]int)
	for serviceID, mask := range collector {
		if mask != 0b01 {
			xak = xak.Xor(newDevices[serviceID].AccessKey)
		}
		switch mask {
		case 0b01:
			// Someone left the group
			globalRemovedDevices[serviceID] = prevDevices[serviceID]
		case 0b10:
			// Someone was added to the group
			devicesAddedTo = append(devicesAddedTo, serviceID)
		case 0b11:
			removedDevices, addedDevices := exslices.Diff(prevDevices[serviceID], newDevices[serviceID].DeviceIDs)
			if len(removedDevices) > 0 {
				// Device was removed
				globalRemovedDevices[serviceID] = removedDevices
			} else if len(addedDevices) > 0 {
				// User got new devices
				devicesAddedTo = append(devicesAddedTo, serviceID)
			}
		}
	}
	return
}

type senderKeySendMeta struct {
	DeviceIDs []int
	AccessKey *libsignalgo.AccessKey
}

func (cli *Client) getDevicesIDs(
	ctx context.Context,
	recipients []libsignalgo.ServiceID,
	sendEndorsement SendEndorsementCache,
	result *GroupMessageSendResult,
) (
	map[libsignalgo.ServiceID]senderKeySendMeta,
	[]store.SessionAddressTuple,
	[]libsignalgo.ServiceID,
) {
	log := zerolog.Ctx(ctx)
	out := make(map[libsignalgo.ServiceID]senderKeySendMeta)
	senderKeyRecipients := make([]store.SessionAddressTuple, 0, len(recipients))
	fallbackRecipients := make([]libsignalgo.ServiceID, 0)
	for _, recipient := range recipients {
		if recipient == cli.Store.ACIServiceID() {
			// We'll send a sync copy to ourselves, not sender key and no need to include in fallback recipients either
			continue
		}
		fallbackRecipients = append(fallbackRecipients, recipient)
		if recipient.Type != libsignalgo.ServiceIDTypeACI {
			continue
		}
		_, hasEndorsement := sendEndorsement.MemberEndorsements[recipient]
		if !hasEndorsement {
			continue
		}
		profileKey, err := cli.Store.RecipientStore.LoadProfileKey(ctx, recipient.UUID)
		if err != nil {
			log.Err(err).Stringer("recipient_id", recipient.UUID).Msg("Failed to get profile key")
			continue
		} else if profileKey == nil {
			log.Debug().Stringer("recipient_id", recipient.UUID).Msg("No profile key for recipient")
			continue
		}
		accessKey, err := profileKey.DeriveAccessKey()
		if err != nil {
			log.Err(err).Stringer("recipient_id", recipient.UUID).Msg("Failed to derive access key")
			continue
		}
		sessions, err := cli.Store.ACISessionStore.AllSessionsForServiceID(ctx, recipient)
		if err == nil && len(sessions) == 0 {
			// No sessions, make one with prekey
			err = cli.FetchAndProcessPreKey(ctx, recipient, -1)
			if errors.Is(err, ErrUnregisteredUser) {
				fallbackRecipients = fallbackRecipients[:len(fallbackRecipients)-1]
				result.FailedToSendTo = append(result.FailedToSendTo, FailedSendResult{
					Recipient: recipient,
					Error:     err,
				})
				log.Debug().
					Stringer("recipient_id", recipient).
					Msg("Recipient is not registered, won't try to send")
				continue
			} else if err != nil {
				log.Warn().Err(err).Stringer("recipient_id", recipient.UUID).Msg("Failed to fetch keys for recipient")
				continue
			}
			sessions, err = cli.Store.ACISessionStore.AllSessionsForServiceID(ctx, recipient)
		}
		if err != nil {
			log.Err(err).Stringer("recipient_id", recipient.UUID).Msg("Failed to get sessions for recipient")
			continue
		} else if len(sessions) == 0 {
			log.Debug().Stringer("recipient_id", recipient.UUID).Msg("No sessions for recipient after fetching keys")
			continue
		}
		fallbackRecipients = fallbackRecipients[:len(fallbackRecipients)-1]
		out[recipient] = senderKeySendMeta{
			DeviceIDs: exslices.CastFunc(sessions, func(from store.SessionAddressTuple) int {
				return from.DeviceID
			}),
			AccessKey: accessKey,
		}
		senderKeyRecipients = append(senderKeyRecipients, sessions...)
	}
	return out, senderKeyRecipients, fallbackRecipients
}
