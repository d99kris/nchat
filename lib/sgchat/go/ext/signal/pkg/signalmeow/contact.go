// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"google.golang.org/protobuf/proto"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

func (cli *Client) StoreContactDetailsAsContact(ctx context.Context, contactDetails *signalpb.ContactDetails, avatar *[]byte) (*types.Recipient, error) {
	parsedUUID, err := uuid.Parse(contactDetails.GetAci())
	if err != nil {
		return nil, err
	}
	ctx = zerolog.Ctx(ctx).With().
		Str("action", "store contact details as contact").
		Stringer("uuid", parsedUUID).
		Logger().WithContext(ctx)
	return cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, parsedUUID, uuid.Nil, func(recipient *types.Recipient) (bool, error) {
		if contactDetails.GetNumber() != "" {
			recipient.E164 = contactDetails.GetNumber()
		}
		recipient.ContactName = contactDetails.GetName()
		//if profileKeyString := contactDetails.GetProfileKey(); profileKeyString != nil {
		//	profileKey := libsignalgo.ProfileKey(profileKeyString)
		//	recipient.Profile.Key = profileKey
		//}
		if avatar != nil && *avatar != nil && len(*avatar) > 0 {
			rawHash := sha256.Sum256(*avatar)
			avatarHash := hex.EncodeToString(rawHash[:])
			var contentType string
			if avatarDetails := contactDetails.GetAvatar(); avatarDetails != nil && !strings.HasSuffix(avatarDetails.GetContentType(), "/*") {
				contentType = *avatarDetails.ContentType
			} else {
				contentType = http.DetectContentType(*avatar)
			}
			recipient.ContactAvatar = types.ContactAvatar{
				Image:       *avatar,
				ContentType: contentType,
				Hash:        avatarHash,
			}
		}
		return true, nil
	})
}

func (cli *Client) fetchContactThenTryAndUpdateWithProfile(ctx context.Context, aci uuid.UUID, refreshAfter time.Duration) (*types.Recipient, error) {
	log := zerolog.Ctx(ctx).With().
		Str("action", "fetch contact then try and update with profile").
		Stringer("profile_aci", aci).
		Logger()
	ctx = log.WithContext(ctx)

	profile, err := cli.RetrieveProfileByID(ctx, aci, refreshAfter)
	if err != nil {
		log.Debug().Err(err).Msg("Failed to fetch profile")
		// Continue to return contact without profile
	}
	return cli.Store.RecipientStore.LoadAndUpdateRecipient(ctx, aci, uuid.Nil, func(recipient *types.Recipient) (changed bool, err error) {
		if profile != nil {
			// Don't bother saving every fetched timestamp to the database, but save if anything else changed
			if !recipient.Profile.Equals(profile) || recipient.Profile.FetchedAt.IsZero() {
				changed = true
			}
			recipient.Profile = *profile
		}
		return
	})
}

func (cli *Client) ContactByACI(ctx context.Context, aci uuid.UUID) (*types.Recipient, error) {
	return cli.fetchContactThenTryAndUpdateWithProfile(ctx, aci, DefaultProfileRefreshAfter)
}

func (cli *Client) ContactByACIWithRefreshAfter(ctx context.Context, aci uuid.UUID, refreshAfter time.Duration) (*types.Recipient, error) {
	return cli.fetchContactThenTryAndUpdateWithProfile(ctx, aci, refreshAfter)
}

func (cli *Client) ContactByE164(ctx context.Context, e164 string) (*types.Recipient, error) {
	contact, err := cli.Store.RecipientStore.LoadRecipientByE164(ctx, e164)
	if err != nil {
		zerolog.Ctx(ctx).Err(err).Msg("ContactByE164 error loading contact")
		return nil, err
	}
	if contact == nil {
		return nil, nil
	}
	if contact.ACI != uuid.Nil {
		contact, err = cli.fetchContactThenTryAndUpdateWithProfile(ctx, contact.ACI, DefaultProfileRefreshAfter)
	}
	return contact, err
}

// UnmarshalContactDetailsMessages unmarshals a slice of ContactDetails messages from a byte buffer.
func unmarshalContactDetailsMessages(byteStream []byte) ([]*signalpb.ContactDetails, [][]byte, error) {
	var contactDetailsList []*signalpb.ContactDetails
	var avatarList [][]byte
	buf := bytes.NewBuffer(byteStream)

	for {
		// If no more bytes are left to read, break the loop
		if buf.Len() == 0 {
			break
		}

		// Read the length prefix (varint) of the next Protobuf message
		msgLen, err := binary.ReadUvarint(buf)
		if err != nil {
			return nil, nil, fmt.Errorf("Failed to read message length: %v", err)
		}

		// If no more bytes are left to read, break the loop
		if buf.Len() == 0 {
			break
		}

		// Read the Protobuf message using the length obtained
		msgBytes := buf.Next(int(msgLen))

		// Unmarshal the Protobuf message into a ContactDetails object
		contactDetails := &signalpb.ContactDetails{}
		if err := proto.Unmarshal(msgBytes, contactDetails); err != nil {
			return nil, nil, fmt.Errorf("Failed to unmarshal ContactDetails: %v", err)
		}

		// Append the ContactDetails object to the result slice
		contactDetailsList = append(contactDetailsList, contactDetails)

		// If the ContactDetails object has an avatar, read it into a byte slice
		if contactDetails.Avatar != nil && contactDetails.Avatar.Length != nil && *contactDetails.Avatar.Length > 0 {
			avatarBytes := buf.Next(int(*contactDetails.Avatar.Length))
			// TODO why is this making a copy?
			avatarBytesCopy := make([]byte, len(avatarBytes))
			copy(avatarBytesCopy, avatarBytes)
			avatarList = append(avatarList, avatarBytesCopy)
		} else {
			// If there isn't, append nil so the indicies line up
			avatarList = append(avatarList, nil)
		}
	}

	return contactDetailsList, avatarList, nil
}
