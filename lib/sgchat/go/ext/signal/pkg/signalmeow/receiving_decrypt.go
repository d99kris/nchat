// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2023 Scott Weber
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
	"crypto/sha256"
	"errors"
	"fmt"
	"time"

	"github.com/google/uuid"
	"github.com/rs/zerolog"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
)

type DecryptionResult struct {
	SenderAddress  *libsignalgo.Address
	CiphertextHash *[32]byte
	Content        *signalpb.Content
	ContentHint    signalpb.UnidentifiedSenderMessage_Message_ContentHint
	Err            error
	GroupID        *libsignalgo.GroupIdentifier
	Unencrypted    bool

	Retriable      bool
	Ciphertext     []byte
	CiphertextType libsignalgo.CiphertextMessageType
}

func (cli *Client) decryptEnvelope(
	ctx context.Context,
	envelope *signalpb.Envelope,
) DecryptionResult {
	log := zerolog.Ctx(ctx)

	destinationServiceID, err := libsignalgo.ServiceIDFromString(envelope.GetDestinationServiceId())
	if err != nil {
		log.Err(err).Str("destination_service_id", envelope.GetDestinationServiceId()).Msg("Failed to parse destination service ID")
		return DecryptionResult{Err: fmt.Errorf("failed to parse destination service ID: %w", err)}
	}

	switch *envelope.Type {
	case signalpb.Envelope_UNIDENTIFIED_SENDER:
		result, err := cli.decryptUnidentifiedSenderEnvelope(ctx, destinationServiceID, envelope)
		if err != nil {
			result.Err = fmt.Errorf("failed to decrypt unidentified sender envelope: %w", err)
		}
		return result

	case signalpb.Envelope_PREKEY_BUNDLE, signalpb.Envelope_CIPHERTEXT:
		sender, err := libsignalgo.NewUUIDAddressFromString(
			*envelope.SourceServiceId,
			uint(*envelope.SourceDevice),
		)
		if err != nil {
			return DecryptionResult{Err: fmt.Errorf("failed to wrap address: %v", err)}
		}
		var result *DecryptionResult
		var bundleType string
		if *envelope.Type == signalpb.Envelope_PREKEY_BUNDLE {
			result, err = cli.prekeyDecrypt(ctx, destinationServiceID, sender, envelope.Content, envelope.GetServerTimestamp())
			bundleType = "prekey bundle"
		} else {
			result, err = cli.decryptCiphertextEnvelope(ctx, destinationServiceID, sender, envelope.Content, envelope.GetServerTimestamp())
			bundleType = "ciphertext"
		}
		if err != nil {
			return DecryptionResult{
				SenderAddress:  sender,
				Err:            fmt.Errorf("failed to decrypt %s envelope: %w", bundleType, err),
				Retriable:      true, // TODO should these ever be not retriable?
				Ciphertext:     envelope.Content,
				CiphertextType: libsignalgo.CiphertextMessageType(envelope.GetType()),
			}
		}
		return *result

	case signalpb.Envelope_PLAINTEXT_CONTENT:
		addr, err := libsignalgo.NewUUIDAddressFromString(envelope.GetSourceServiceId(), uint(envelope.GetSourceDevice()))
		if err != nil {
			return DecryptionResult{Err: fmt.Errorf("failed to wrap address: %v", err)}
		}
		content, err := stripPadding(envelope.GetContent())
		if err != nil {
			return DecryptionResult{Err: fmt.Errorf("failed to strip padding: %w", err)}
		}
		return DecryptionResult{
			SenderAddress: addr,
			Content:       &signalpb.Content{DecryptionErrorMessage: content},
			Unencrypted:   true,
		}

	case signalpb.Envelope_SERVER_DELIVERY_RECEIPT:
		return DecryptionResult{Err: fmt.Errorf("server delivery receipt envelopes are not yet supported")}

	case signalpb.Envelope_SENDERKEY_MESSAGE:
		return DecryptionResult{Err: fmt.Errorf("senderkey message envelopes are not yet supported")}

	case signalpb.Envelope_UNKNOWN:
		return DecryptionResult{Err: fmt.Errorf("unknown envelope type")}

	default:
		return DecryptionResult{Err: fmt.Errorf("unrecognized envelope type %d", envelope.GetType())}
	}
}

var EventAlreadyProcessed = errors.New("event was already processed")

func (cli *Client) bufferedDecryptTxn(ctx context.Context, ciphertext []byte, serverTimestamp uint64, decrypt func(context.Context) ([]byte, error)) (plaintext []byte, ciphertextHash [32]byte, err error) {
	ciphertextHash = sha256.Sum256(ciphertext)

	var buf *store.BufferedEvent
	buf, err = cli.Store.EventBuffer.GetBufferedEvent(ctx, ciphertextHash)
	if err != nil {
		err = fmt.Errorf("failed to get buffered event: %w", err)
		return
	} else if buf != nil {
		plaintext = buf.Plaintext
		insertTime := time.UnixMilli(buf.InsertTimestamp)
		if plaintext == nil {
			zerolog.Ctx(ctx).Debug().
				Hex("ciphertext_hash", ciphertextHash[:]).
				Time("insertion_time", insertTime).
				Msg("Returning event already processed error")
			err = fmt.Errorf("%w at %s", EventAlreadyProcessed, insertTime.String())
		} else {
			zerolog.Ctx(ctx).Debug().
				Hex("ciphertext_hash", ciphertextHash[:]).
				Time("insertion_time", insertTime).
				Msg("Returning previously decrypted plaintext")
		}
		return
	}

	err = cli.Store.DoDecryptionTxn(ctx, func(ctx context.Context) (innerErr error) {
		plaintext, innerErr = decrypt(ctx)
		if innerErr != nil {
			return
		}
		innerErr = cli.Store.EventBuffer.PutBufferedEvent(ctx, ciphertextHash, plaintext, serverTimestamp)
		if innerErr != nil {
			innerErr = fmt.Errorf("failed to save decrypted event to buffer: %w", innerErr)
		}
		zerolog.Ctx(ctx).Trace().
			Hex("ciphertext_hash", ciphertextHash[:]).
			Msg("Successfully decrypted and saved event")
		return
	})
	return
}

func (cli *Client) prekeyDecrypt(
	ctx context.Context,
	destination libsignalgo.ServiceID,
	sender *libsignalgo.Address,
	encryptedContent []byte,
	serverTimestamp uint64,
) (*DecryptionResult, error) {
	preKeyMessage, err := libsignalgo.DeserializePreKeyMessage(encryptedContent)
	if err != nil {
		return nil, fmt.Errorf("failed to deserialize prekey message: %w", err)
	} else if preKeyMessage == nil {
		return nil, fmt.Errorf("deserializing prekey message returned nil")
	}
	pks := cli.Store.PreKeyStore(destination)
	if pks == nil {
		return nil, fmt.Errorf("no prekey store found for %s", destination)
	}
	ss := cli.Store.SessionStore(destination)
	if ss == nil {
		return nil, fmt.Errorf("no session store found for %s", destination)
	}
	is := cli.Store.IdentityStore(destination)
	if is == nil {
		return nil, fmt.Errorf("no identity store found for %s", destination)
	}

	plaintext, ciphertextHash, err := cli.bufferedDecryptTxn(ctx, encryptedContent, serverTimestamp, func(ctx context.Context) ([]byte, error) {
		return libsignalgo.DecryptPreKey(
			ctx,
			preKeyMessage,
			sender,
			ss,
			is,
			pks,
			pks,
			pks,
		)
	})
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt prekey message: %w", err)
	}
	plaintext, err = stripPadding(plaintext)
	if err != nil {
		return nil, fmt.Errorf("failed to strip padding: %w", err)
	}
	content := &signalpb.Content{}
	err = proto.Unmarshal(plaintext, content)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal decrypted prekey message: %w", err)
	}
	return &DecryptionResult{
		SenderAddress:  sender,
		Content:        content,
		CiphertextHash: &ciphertextHash,
	}, nil
}

func (cli *Client) decryptCiphertextEnvelope(
	ctx context.Context,
	destinationServiceID libsignalgo.ServiceID,
	senderAddress *libsignalgo.Address,
	ciphertext []byte,
	serverTimestamp uint64,
) (*DecryptionResult, error) {
	log := zerolog.Ctx(ctx)
	message, err := libsignalgo.DeserializeMessage(ciphertext)
	if err != nil {
		log.Err(err).Msg("Failed to deserialize ciphertext message")
		return nil, fmt.Errorf("failed to deserialize message: %w", err)
	}
	sessionStore := cli.Store.SessionStore(destinationServiceID)
	if sessionStore == nil {
		return nil, fmt.Errorf("no session store for destination service ID %s", destinationServiceID)
	}
	identityStore := cli.Store.IdentityStore(destinationServiceID)
	if identityStore == nil {
		return nil, fmt.Errorf("no identity store for destination service ID %s", destinationServiceID)
	}
	plaintext, ciphertextHash, err := cli.bufferedDecryptTxn(ctx, ciphertext, serverTimestamp, func(ctx context.Context) ([]byte, error) {
		return libsignalgo.Decrypt(
			ctx,
			message,
			senderAddress,
			sessionStore,
			identityStore,
		)
	})
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt ciphertext message: %w", err)
	}
	plaintext, err = stripPadding(plaintext)
	if err != nil {
		return nil, fmt.Errorf("failed to strip padding: %w", err)
	}
	content := signalpb.Content{}
	err = proto.Unmarshal(plaintext, &content)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal decrypted message: %w", err)
	}
	return &DecryptionResult{
		SenderAddress:  senderAddress,
		Content:        &content,
		CiphertextHash: &ciphertextHash,
	}, nil
}

func (cli *Client) decryptSenderKeyMessage(
	ctx context.Context,
	senderAddress *libsignalgo.Address,
	ciphertext []byte,
	serverTimestamp uint64,
) (*DecryptionResult, error) {
	plaintext, ciphertextHash, err := cli.bufferedDecryptTxn(ctx, ciphertext, serverTimestamp, func(ctx context.Context) ([]byte, error) {
		return libsignalgo.GroupDecrypt(
			ctx,
			ciphertext,
			senderAddress,
			cli.Store.SenderKeyStore,
		)
	})
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt sender key message: %w", err)
	}
	plaintext, err = stripPadding(plaintext)
	if err != nil {
		return nil, fmt.Errorf("failed to strip padding: %w", err)
	}
	content := signalpb.Content{}
	err = proto.Unmarshal(plaintext, &content)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal decrypted sender key message: %w", err)
	}
	return &DecryptionResult{
		SenderAddress:  senderAddress,
		Content:        &content,
		CiphertextHash: &ciphertextHash,
	}, nil
}

func (cli *Client) decryptUnidentifiedSenderEnvelope(ctx context.Context, destinationServiceID libsignalgo.ServiceID, envelope *signalpb.Envelope) (result DecryptionResult, err error) {
	log := zerolog.Ctx(ctx)

	if destinationServiceID != cli.Store.ACIServiceID() {
		log.Warn().Stringer("destination_service_id", destinationServiceID).
			Msg("Received UNIDENTIFIED_SENDER envelope for non-ACI destination")
		return result, fmt.Errorf("received unidentified sender envelope for non-ACI destination")
	}
	usmc, err := libsignalgo.SealedSenderDecryptToUSMC(
		ctx,
		envelope.GetContent(),
		cli.Store.ACIIdentityStore,
	)
	if err != nil {
		return result, fmt.Errorf("failed to decrypt to USMC: %w", err)
	} else if usmc == nil {
		return result, fmt.Errorf("decrypting to USMC returned nil")
	}

	messageType, err := usmc.GetMessageType()
	if err != nil {
		return result, fmt.Errorf("failed to get message type: %w", err)
	}
	senderCertificate, err := usmc.GetSenderCertificate()
	if err != nil {
		return result, fmt.Errorf("failed to get sender certificate: %w", err)
	}
	contentHint, err := usmc.GetContentHint()
	if err != nil {
		return result, fmt.Errorf("failed to get content hint: %w", err)
	}
	result.GroupID, err = usmc.GetGroupID()
	if err != nil {
		return result, fmt.Errorf("failed to get group ID: %w", err)
	}
	result.ContentHint = signalpb.UnidentifiedSenderMessage_Message_ContentHint(contentHint)
	senderUUID, err := senderCertificate.GetSenderUUID()
	if err != nil {
		return result, fmt.Errorf("failed to get sender UUID: %w", err)
	}
	senderDeviceID, err := senderCertificate.GetDeviceID()
	if err != nil {
		return result, fmt.Errorf("failed to get sender device ID: %w", err)
	}
	senderAddress, err := libsignalgo.NewACIServiceID(senderUUID).Address(uint(senderDeviceID))
	if err != nil {
		return result, fmt.Errorf("failed to create sender address: %w", err)
	}
	result.SenderAddress = senderAddress
	senderE164, err := senderCertificate.GetSenderE164()
	if err != nil {
		return result, fmt.Errorf("failed to get sender E164: %w", err)
	}
	usmcContents, err := usmc.GetContents()
	if err != nil {
		return result, fmt.Errorf("failed to get USMC contents: %w", err)
	}
	result.Ciphertext = usmcContents
	result.CiphertextType = messageType
	newLog := log.With().
		Stringer("sender_uuid", senderUUID).
		Stringer("group_id", result.GroupID).
		Uint32("sender_device_id", senderDeviceID).
		Str("sender_e164", senderE164).
		Uint8("sealed_sender_type", uint8(messageType)).
		Logger()
	log = &newLog
	ctx = log.WithContext(ctx)
	log.Trace().Msg("Received SealedSender message")

	if senderE164 != "" {
		_, err = cli.Store.RecipientStore.UpdateRecipientE164(ctx, senderUUID, uuid.Nil, senderE164)
		if err != nil {
			log.Warn().Err(err).Msg("Failed to update sender E164 in recipient store")
		}
	}

	var resultPtr *DecryptionResult
	switch messageType {
	case libsignalgo.CiphertextMessageTypeSenderKey:
		resultPtr, err = cli.decryptSenderKeyMessage(ctx, senderAddress, usmcContents, envelope.GetServerTimestamp())
	case libsignalgo.CiphertextMessageTypePreKey:
		resultPtr, err = cli.prekeyDecrypt(ctx, destinationServiceID, senderAddress, usmcContents, envelope.GetServerTimestamp())
	case libsignalgo.CiphertextMessageTypeWhisper:
		resultPtr, err = cli.decryptCiphertextEnvelope(ctx, destinationServiceID, senderAddress, usmcContents, envelope.GetServerTimestamp())
	case libsignalgo.CiphertextMessageTypePlaintext:
		usmcContents, err = stripPadding(usmcContents)
		if err != nil {
			err = fmt.Errorf("failed to strip padding: %w", err)
		}
		result.Unencrypted = true
		result.Content = &signalpb.Content{
			DecryptionErrorMessage: usmcContents,
		}
		return result, err
	default:
		return result, fmt.Errorf("unsupported sealed sender message type %d", messageType)
	}
	if err != nil {
		result.Retriable = result.ContentHint == signalpb.UnidentifiedSenderMessage_Message_RESENDABLE
		return result, err
	}
	resultPtr.GroupID = result.GroupID
	return *resultPtr, nil
}

func stripPadding(contents []byte) ([]byte, error) {
	for i := len(contents) - 1; i >= 0; i-- {
		if contents[i] == 0x80 {
			contents = contents[:i]
			return contents, nil
		} else if contents[i] != 0x00 {
			return nil, fmt.Errorf("invalid ISO7816 padding")
		}
	}
	return nil, fmt.Errorf("invalid ISO7816 padding (length %d)", len(contents))
}
