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

package signalmeow

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"io"
	"net/http"
	"slices"
	"strings"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"go.mau.fi/util/exerrors"
	"go.mau.fi/util/ptr"
	"golang.org/x/crypto/hkdf"
	"golang.org/x/exp/maps"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/events"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

func (cli *Client) SyncStorage(ctx context.Context) {
	log := cli.Log.With().Str("action", "sync storage").Logger()
	ctx = log.WithContext(ctx)
	// TODO only fetch changed entries
	update, err := cli.FetchStorage(ctx, cli.Store.MasterKey, 0, nil)
	if err != nil {
		log.Err(err).Msg("Failed to fetch storage")
		return
	}
	err = cli.Store.DoContactTxn(ctx, func(ctx context.Context) error {
		return cli.processStorageInTxn(ctx, update)
	})
	if err != nil {
		log.Err(err).Msg("Failed to process storage update")
	}
}

func (cli *Client) processStorageInTxn(ctx context.Context, update *StorageUpdate) error {
	log := zerolog.Ctx(ctx)
	var changedContacts []*types.Recipient
	for _, record := range update.NewRecords {
		switch data := record.StorageRecord.GetRecord().(type) {
		case *signalpb.StorageRecord_Contact:
			log.Trace().Any("contact_record", data.Contact).Msg("Handling contact record")
			aci, _ := uuid.Parse(data.Contact.Aci)
			pni, _ := uuid.Parse(data.Contact.Pni)
			if aci == uuid.Nil && pni == uuid.Nil {
				log.Warn().
					Str("raw_aci", data.Contact.Aci).
					Str("raw_pni", data.Contact.Pni).
					Str("raw_e164", data.Contact.E164).
					Msg("Storage service has contact record with no ACI or PNI")
				continue
			}
			contact := data.Contact
			topLevelChanged := false
			recipient, err := cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aci, pni, func(recipient *types.Recipient) (changed bool, err error) {
				if len(contact.ProfileKey) == libsignalgo.ProfileKeyLength {
					newProfileKey := libsignalgo.ProfileKey(contact.ProfileKey)
					changed = changed || recipient.Profile.Key != newProfileKey
					recipient.Profile.Key = newProfileKey
				}
				if recipient.Profile.Name == "" && (contact.GivenName != "" || contact.FamilyName != "") {
					changed = true
					recipient.Profile.Name = strings.TrimSpace(fmt.Sprintf("%s %s", contact.GivenName, contact.FamilyName))
				}
				if contact.SystemGivenName != "" || contact.SystemFamilyName != "" {
					changed = true
					recipient.ContactName = strings.TrimSpace(fmt.Sprintf("%s %s", contact.SystemGivenName, contact.SystemFamilyName))
				}
				newNickname := ""
				if contact.Nickname != nil {
					newNickname = strings.TrimSpace(fmt.Sprintf("%s %s", contact.Nickname.Given, contact.Nickname.Family))
				}
				if recipient.Nickname != newNickname {
					changed = true
					recipient.Nickname = newNickname
				}
				if contact.E164 != "" {
					changed = changed || recipient.E164 != contact.E164
					recipient.E164 = contact.E164
				}
				if contact.Blocked != recipient.Blocked {
					changed = true
					recipient.Blocked = contact.Blocked
				}
				if !ptr.Val(recipient.Whitelisted) {
					changed = true
					recipient.Whitelisted = &contact.Whitelisted
				}
				topLevelChanged = changed
				return
			})
			if err != nil {
				return fmt.Errorf("failed to update contact %s/%s: %w", aci, pni, err)
			}
			if topLevelChanged {
				changedContacts = append(changedContacts, recipient)
			}
			if aci != uuid.Nil {
				go cli.handleEvent(&events.ChatMuteChanged{
					ChatID:              aci.String(),
					MutedUntilTimestamp: data.Contact.GetMutedUntilTimestamp(),
				})
			}
		case *signalpb.StorageRecord_GroupV2:
			if len(data.GroupV2.MasterKey) != libsignalgo.GroupMasterKeyLength {
				log.Warn().Msg("Invalid group master key length")
				continue
			}
			masterKey := libsignalgo.GroupMasterKey(data.GroupV2.MasterKey)
			groupID, err := cli.StoreMasterKey(ctx, masterKeyFromBytes(masterKey))
			if err != nil {
				return fmt.Errorf("failed to store group master key for %s: %w", groupID, err)
			}
			log.Debug().Stringer("group_id", groupID).Msg("Stored group master key from storage service")
			go cli.handleEvent(&events.ChatMuteChanged{
				ChatID:              string(groupID),
				MutedUntilTimestamp: data.GroupV2.GetMutedUntilTimestamp(),
			})
		case *signalpb.StorageRecord_Account:
			log.Trace().Any("account_record", data.Account).Msg("Found account record")
			cli.Store.AccountRecord = data.Account
			err := cli.Store.DeviceStore.PutDevice(ctx, &cli.Store.DeviceData)
			if err != nil {
				return fmt.Errorf("failed to save device after receiving account record: %w", err)
			}
			log.Debug().Msg("Saved device after receiving account record")
			go cli.handleEvent(&events.PinnedConversationsChanged{
				PinnedConversations: data.Account.GetPinnedConversations(),
			})
		case *signalpb.StorageRecord_GroupV1, *signalpb.StorageRecord_StoryDistributionList:
			// irrelevant data
		default:
			log.Warn().Type("type", data).Str("item_id", record.StorageID).Msg("Unknown storage record type")
		}
	}
	if len(changedContacts) > 0 {
		go cli.handleEvent(&events.ContactList{
			Contacts: changedContacts,
			IsFromDB: true,
		})
	}
	return nil
}

type StorageUpdate struct {
	Version        uint64
	NewRecords     []*DecryptedStorageRecord
	RemovedRecords []string
	MissingRecords []string
}

func (cli *Client) FetchStorage(ctx context.Context, masterKey []byte, currentVersion uint64, existingKeys []string) (*StorageUpdate, error) {
	storageKey := deriveStorageServiceKey(masterKey)
	manifest, err := cli.fetchStorageManifest(ctx, storageKey, currentVersion)
	if err != nil {
		return nil, err
	} else if manifest == nil {
		return nil, nil
	}
	removedKeys := make([]string, 0)
	newKeys := manifestRecordToMap(manifest.GetIdentifiers())
	slices.Sort(existingKeys)
	existingKeys = slices.Compact(existingKeys)
	for _, key := range existingKeys {
		_, isStillThere := newKeys[key]
		if isStillThere {
			delete(newKeys, key)
		} else {
			removedKeys = append(removedKeys, key)
		}
		delete(newKeys, key)
	}
	newRecords, missingKeys, err := cli.fetchStorageRecords(ctx, storageKey, manifest.GetRecordIkm(), newKeys)
	if err != nil {
		return nil, err
	}
	return &StorageUpdate{
		Version:        manifest.GetVersion(),
		NewRecords:     newRecords,
		RemovedRecords: removedKeys,
		MissingRecords: missingKeys,
	}, nil
}

func manifestRecordToMap(manifest []*signalpb.ManifestRecord_Identifier) map[string]signalpb.ManifestRecord_Identifier_Type {
	manifestMap := make(map[string]signalpb.ManifestRecord_Identifier_Type, len(manifest))
	for _, item := range manifest {
		manifestMap[base64.StdEncoding.EncodeToString(item.GetRaw())] = item.GetType()
	}
	return manifestMap
}

func deriveStorageServiceKey(masterKey []byte) []byte {
	h := hmac.New(sha256.New, masterKey)
	h.Write([]byte("Storage Service Encryption"))
	return h.Sum(nil)
}

func deriveStorageManifestKey(storageKey []byte, version uint64) []byte {
	h := hmac.New(sha256.New, storageKey)
	exerrors.Must(fmt.Fprintf(h, "Manifest_%d", version))
	return h.Sum(nil)
}

const storageServiceItemKeyInfoPrefix = "20240801_SIGNAL_STORAGE_SERVICE_ITEM_"
const storageServiceItemKeyLen = 32

func deriveStorageItemKey(storageKey, recordIKM, rawItemID []byte, b64ItemID string) []byte {
	if recordIKM == nil {
		h := hmac.New(sha256.New, storageKey)
		exerrors.Must(fmt.Fprintf(h, "Item_%s", b64ItemID))
		return h.Sum(nil)
	} else {
		h := hkdf.New(sha256.New, recordIKM, []byte{}, append([]byte(storageServiceItemKeyInfoPrefix), rawItemID...))
		out := make([]byte, storageServiceItemKeyLen)
		exerrors.Must(io.ReadFull(h, out))
		return out
	}
}

// MaxReadStorageRecords is the maximum number of storage records to fetch at once
// from https://github.com/signalapp/Signal-Desktop/blob/v6.44.0/ts/services/storageConstants.ts
const MaxReadStorageRecords = 2500

type DecryptedStorageRecord struct {
	ItemType      signalpb.ManifestRecord_Identifier_Type
	StorageID     string
	StorageRecord *signalpb.StorageRecord
}

func (cli *Client) fetchStorageManifest(ctx context.Context, storageKey []byte, greaterThanVersion uint64) (*signalpb.ManifestRecord, error) {
	storageCreds, err := cli.getStorageCredentials(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch credentials: %w", err)
	}
	path := "/v1/storage/manifest"
	if greaterThanVersion > 0 {
		path += fmt.Sprintf("/version/%d", greaterThanVersion)
	}
	var encryptedManifest signalpb.StorageManifest
	var manifestRecord signalpb.ManifestRecord
	resp, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodGet, path, &web.HTTPReqOpt{
		Username:    &storageCreds.Username,
		Password:    &storageCreds.Password,
		ContentType: web.ContentTypeProtobuf,
	})
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch storage manifest: %w", err)
	} else if resp.StatusCode == http.StatusNoContent {
		// Already up to date
		return nil, nil
	} else if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d fetching storage manifest", resp.StatusCode)
	} else if body, err := io.ReadAll(resp.Body); err != nil {
		return nil, fmt.Errorf("failed to read storage manifest response: %w", err)
	} else if err = proto.Unmarshal(body, &encryptedManifest); err != nil {
		return nil, fmt.Errorf("failed to unmarshal encrypted storage manifest: %w", err)
	} else if decryptedManifestBytes, err := decryptBytes(deriveStorageManifestKey(storageKey, encryptedManifest.GetVersion()), encryptedManifest.GetValue()); err != nil {
		return nil, fmt.Errorf("failed to decrypt storage manifest: %w", err)
	} else if err = proto.Unmarshal(decryptedManifestBytes, &manifestRecord); err != nil {
		return nil, fmt.Errorf("failed to unmarshal decrypted manifest record: %w", err)
	} else {
		return &manifestRecord, nil
	}
}

func (cli *Client) fetchStorageRecords(
	ctx context.Context,
	storageKey []byte,
	recordIKM []byte,
	inputRecords map[string]signalpb.ManifestRecord_Identifier_Type,
) ([]*DecryptedStorageRecord, []string, error) {
	recordKeys := make([][]byte, 0, len(inputRecords))
	for key := range inputRecords {
		decoded, err := base64.StdEncoding.DecodeString(key)
		if err != nil {
			return nil, nil, fmt.Errorf("failed to decode storage key %s: %w", key, err)
		}
		recordKeys = append(recordKeys, decoded)
	}
	items := make([]*signalpb.StorageItem, 0, len(inputRecords))
	for i := 0; i < len(recordKeys); i += MaxReadStorageRecords {
		end := i + MaxReadStorageRecords
		if len(recordKeys) < end {
			end = len(recordKeys)
		}
		keyChunk := recordKeys[i:end]
		itemChunk, err := cli.fetchStorageItemsChunk(ctx, keyChunk)
		if err != nil {
			return nil, nil, err
		}
		items = append(items, itemChunk...)
	}
	records := make([]*DecryptedStorageRecord, 0, len(items))
	log := zerolog.Ctx(ctx)
	for i, encryptedItem := range items {
		base64Key := base64.StdEncoding.EncodeToString(encryptedItem.GetKey())
		itemType, ok := inputRecords[base64Key]
		if !ok {
			log.Warn().Int("item_index", i).Str("item_key", base64Key).Msg("Received unexpected storage item")
			continue
		}
		itemKey := deriveStorageItemKey(storageKey, recordIKM, encryptedItem.GetKey(), base64Key)
		decryptedItemBytes, err := decryptBytes(itemKey, encryptedItem.GetValue())
		if err != nil {
			log.Warn().Err(err).
				Stringer("item_type", itemType).
				Int("item_index", i).
				Str("item_key", base64Key).
				Msg("Failed to decrypt storage item")
			continue
		}
		var decryptedItem signalpb.StorageRecord
		err = proto.Unmarshal(decryptedItemBytes, &decryptedItem)
		if err != nil {
			logEvt := log.Warn().Err(err).
				Stringer("item_type", itemType).
				Int("item_index", i).
				Str("item_key", base64Key)
			if log.GetLevel() == zerolog.TraceLevel {
				logEvt.Str("item_data", base64.StdEncoding.EncodeToString(decryptedItemBytes))
			}
			logEvt.Msg("Failed to unmarshal storage item")
			continue
		}
		delete(inputRecords, base64Key)
		records = append(records, &DecryptedStorageRecord{
			ItemType:      itemType,
			StorageID:     base64Key,
			StorageRecord: &decryptedItem,
		})
	}
	missingKeys := maps.Keys(inputRecords)
	return records, missingKeys, nil
}

func (cli *Client) fetchStorageItemsChunk(ctx context.Context, recordKeys [][]byte) ([]*signalpb.StorageItem, error) {
	storageCreds, err := cli.getStorageCredentials(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch credentials: %w", err)
	}
	body, err := proto.Marshal(&signalpb.ReadOperation{ReadKey: recordKeys})
	if err != nil {
		return nil, fmt.Errorf("failed to marshal read operation: %w", err)
	}
	var storageItems signalpb.StorageItems
	resp, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodPut, "/v1/storage/read", &web.HTTPReqOpt{
		Username:    &storageCreds.Username,
		Password:    &storageCreds.Password,
		Body:        body,
		ContentType: web.ContentTypeProtobuf,
	})
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch storage records: %w", err)
	} else if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d fetching storage records", resp.StatusCode)
	} else if body, err = io.ReadAll(resp.Body); err != nil {
		return nil, fmt.Errorf("failed to read storage manifest response: %w", err)
	} else if err = proto.Unmarshal(body, &storageItems); err != nil {
		return nil, fmt.Errorf("failed to unmarshal encrypted storage manifest: %w", err)
	} else {
		return storageItems.GetItems(), nil
	}
}
