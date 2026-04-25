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
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"mime/multipart"
	"net/http"
	"os"

	"github.com/rs/zerolog"
	"go.mau.fi/util/fallocate"
	"go.mau.fi/util/random"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

const (
	attachmentKeyDownloadPath = "/attachments/%s"
	attachmentIDDownloadPath  = "/attachments/%d"
)

func getAttachmentPath(id uint64, key string) string {
	if id != 0 {
		return fmt.Sprintf(attachmentIDDownloadPath, id)
	}
	return fmt.Sprintf(attachmentKeyDownloadPath, key)
}

// ErrInvalidMACForAttachment signals that the downloaded attachment has an invalid MAC.
var ErrInvalidMACForAttachment = errors.New("invalid MAC for attachment")
var ErrInvalidDigestForAttachment = errors.New("invalid digest for attachment")
var ErrAttachmentNotFound = errors.New("attachment not found on server")

func DownloadAttachmentWithPointer(ctx context.Context, a *signalpb.AttachmentPointer, plaintextHash []byte, into *os.File) ([]byte, error) {
	digest := a.GetDigest()
	plaintextDigest := false
	if digest == nil && plaintextHash != nil {
		digest = plaintextHash
		plaintextDigest = true
	}
	return DownloadAttachment(
		ctx, a.GetCdnId(), a.GetCdnKey(), a.GetCdnNumber(), a.Key, digest, plaintextDigest, a.GetSize(), into,
	)
}

func DownloadAttachment(
	ctx context.Context,
	cdnID uint64,
	cdnKey string,
	cdnNumber uint32,
	key, digest []byte,
	plaintextDigest bool,
	size uint32,
	into *os.File,
) ([]byte, error) {
	resp, err := web.GetAttachment(ctx, getAttachmentPath(cdnID, cdnKey), cdnNumber)
	if err != nil {
		return nil, err
	}
	defer func() {
		_ = resp.Body.Close()
	}()

	var body []byte
	var downloadedSize int64
	if resp.StatusCode > 400 {
		body, err = io.ReadAll(io.LimitReader(resp.Body, 4096))
	} else if into == nil {
		if resp.ContentLength > 0 {
			body = make([]byte, resp.ContentLength)
			_, err = io.ReadFull(resp.Body, body)
		} else {
			body, err = io.ReadAll(http.MaxBytesReader(nil, resp.Body, max(int64(size), 32*1024)*2))
		}
	} else {
		err = fallocate.Fallocate(into, int(resp.ContentLength))
		if err != nil {
			return nil, fmt.Errorf("failed to pre-allocate file for attachment: %w", err)
		}
		downloadedSize, err = io.Copy(into, resp.Body)
	}
	if err != nil {
		return nil, err
	}
	if resp.StatusCode > 400 {
		if json.Valid(body) && len(body) < 4096 {
			zerolog.Ctx(ctx).Debug().RawJSON("response_data", body).Msg("Failed download response json")
		} else if len(body) < 1024 {
			zerolog.Ctx(ctx).Debug().Bytes("response_data", body).Msg("Failed download response data")
		}
		if resp.StatusCode == http.StatusNotFound {
			return nil, ErrAttachmentNotFound
		}
		return nil, fmt.Errorf("unexpected status code %d", resp.StatusCode)
	}

	if into != nil {
		if _, err = into.Seek(0, io.SeekStart); err != nil {
			return nil, fmt.Errorf("failed to seek attachment file after downloading: %w", err)
		}
		return nil, decryptAttachmentFile(into, downloadedSize, key, digest, plaintextDigest, size)
	}
	return decryptAttachment(body, key, digest, plaintextDigest, size)
}

const MACLength = 32
const IVLength = 16

func decryptAttachment(body, key, digest []byte, plaintextDigest bool, size uint32) ([]byte, error) {
	if !plaintextDigest {
		hash := sha256.Sum256(body)
		if !hmac.Equal(hash[:], digest) {
			return nil, ErrInvalidDigestForAttachment
		}
	}
	l := len(body) - MACLength
	if !verifyMAC(key[MACLength:], body[:l], body[l:]) {
		return nil, ErrInvalidMACForAttachment
	}

	decrypted, err := aesDecrypt(key[:MACLength], body[:l])
	if err != nil {
		return nil, err
	}
	if len(decrypted) < int(size) {
		return nil, fmt.Errorf("decrypted attachment length %v < expected %v", len(decrypted), size)
	}
	decrypted = decrypted[:size]
	if plaintextDigest {
		hash := sha256.Sum256(decrypted)
		if !hmac.Equal(hash[:], digest) {
			return nil, fmt.Errorf("%w (plaintext hash)", ErrInvalidDigestForAttachment)
		}
	}
	return decrypted, nil
}

func decryptAttachmentFile(file *os.File, downloadedSize int64, key, digest []byte, plaintextDigest bool, size uint32) error {
	if !plaintextDigest {
		hasher := sha256.New()
		if _, err := io.Copy(hasher, file); err != nil {
			return fmt.Errorf("failed to hash attachment file: %w", err)
		} else if !hmac.Equal(hasher.Sum(nil), digest) {
			return ErrInvalidDigestForAttachment
		} else if _, err = file.Seek(0, io.SeekStart); err != nil {
			return fmt.Errorf("failed to seek attachment file after hashing: %w", err)
		}
	}
	mac := make([]byte, MACLength)
	n, err := file.ReadAt(mac, downloadedSize-MACLength)
	if err != nil {
		return fmt.Errorf("failed to read MAC from attachment file: %w", err)
	} else if n != MACLength {
		return fmt.Errorf("unexpected MAC length read from attachment file: %d", n)
	}
	hasher := hmac.New(sha256.New, key[MACLength:])
	_, err = io.CopyN(hasher, file, downloadedSize-MACLength)
	if err != nil {
		return fmt.Errorf("failed to hash attachment file for MAC verification: %w", err)
	} else if !hmac.Equal(hasher.Sum(nil), mac) {
		return ErrInvalidMACForAttachment
	} else if _, err = file.Seek(0, io.SeekStart); err != nil {
		return fmt.Errorf("failed to seek attachment file after verifying mac: %w", err)
	}

	decryptedSize, err := aesDecryptFile(key[:MACLength], file, downloadedSize-MACLength)
	if err != nil {
		return err
	} else if decryptedSize < int64(size) {
		return fmt.Errorf("decrypted attachment length %d < expected %d", decryptedSize, size)
	} else if _, err = file.Seek(0, io.SeekStart); err != nil {
		return fmt.Errorf("failed to seek attachment file after decrypting: %w", err)
	}
	err = file.Truncate(int64(size))
	if err != nil {
		return fmt.Errorf("failed to truncate attachment file to expected size: %w", err)
	}
	if plaintextDigest {
		hasher = sha256.New()
		if _, err = io.Copy(hasher, file); err != nil {
			return fmt.Errorf("failed to hash decrypted attachment file: %w", err)
		} else if !hmac.Equal(hasher.Sum(nil), digest) {
			return fmt.Errorf("%w (plaintext hash)", ErrInvalidDigestForAttachment)
		} else if _, err = file.Seek(0, io.SeekStart); err != nil {
			return fmt.Errorf("failed to seek attachment file after hashing plaintext: %w", err)
		}
	}
	return nil
}

type attachmentV4UploadAttributes struct {
	Cdn                  uint32            `json:"cdn"`
	Key                  string            `json:"key"`
	Headers              map[string]string `json:"headers"`
	SignedUploadLocation string            `json:"signedUploadLocation"`
}

func extend(data []byte, paddedLen int) []byte {
	origLen := len(data)
	if cap(data) >= paddedLen {
		data = data[:paddedLen]
		for i := origLen; i < paddedLen; i++ {
			data[i] = 0
		}
		return data
	} else {
		newData := make([]byte, paddedLen)
		copy(newData, data)
		return newData
	}
}

func (cli *Client) UploadAttachment(ctx context.Context, body []byte) (*signalpb.AttachmentPointer, error) {
	log := zerolog.Ctx(ctx).With().Str("func", "upload attachment").Logger()
	keys := random.Bytes(64) // combined AES and MAC keys
	plaintextLength := uint32(len(body))

	// Padded length uses exponential bracketing
	paddedLen := int(math.Max(541, math.Floor(math.Pow(1.05, math.Ceil(math.Log(float64(len(body)))/math.Log(1.05))))))
	if paddedLen < len(body) {
		log.Panic().
			Int("padded_len", paddedLen).
			Int("len", len(body)).
			Msg("Math error: padded length is less than body length")
	}
	body = extend(body, paddedLen)

	encrypted, err := aesEncrypt(keys[:32], body)
	if err != nil {
		return nil, err
	}
	encryptedWithMAC := appendMAC(keys[32:], encrypted)

	// Get upload attributes from Signal server
	attributesPath := "/v4/attachments/form/upload"
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodGet, attributesPath, nil, nil)
	if err != nil {
		log.Err(err).Msg("Failed to request upload attributes")
		return nil, fmt.Errorf("failed to request upload attributes: %w", err)
	}
	var uploadAttributes attachmentV4UploadAttributes
	err = web.DecodeWSResponseBody(ctx, &uploadAttributes, resp)
	if err != nil {
		log.Err(err).Msg("Failed to decode upload attributes")
		return nil, fmt.Errorf("failed to decode upload attributes: %w", err)
	}
	if uploadAttributes.Cdn == 3 {
		log.Trace().Msg("Using TUS upload")
		err = cli.uploadAttachmentTUS(ctx, uploadAttributes, encryptedWithMAC)
	} else {
		log.Trace().Msg("Using legacy upload")
		err = cli.uploadAttachmentLegacy(ctx, uploadAttributes, encryptedWithMAC)
	}
	if err != nil {
		log.Err(err).Msg("Failed to upload attachment")
		return nil, err
	}

	digest := sha256.Sum256(encryptedWithMAC)

	attachmentPointer := &signalpb.AttachmentPointer{
		AttachmentIdentifier: &signalpb.AttachmentPointer_CdnKey{
			CdnKey: uploadAttributes.Key,
		},
		Key:       keys,
		Digest:    digest[:],
		Size:      &plaintextLength,
		CdnNumber: &uploadAttributes.Cdn,
	}

	return attachmentPointer, nil
}

func (cli *Client) uploadAttachmentLegacy(
	ctx context.Context,
	uploadAttributes attachmentV4UploadAttributes,
	encryptedWithMAC []byte,
) error {
	username, password := cli.Store.BasicAuthCreds()
	// Allocate attachment on CDN
	resp, err := web.SendHTTPRequest(ctx, "", http.MethodPost, "", &web.HTTPReqOpt{
		OverrideURL: uploadAttributes.SignedUploadLocation,
		ContentType: web.ContentTypeOctetStream,
		Headers:     uploadAttributes.Headers,
		Username:    &username,
		Password:    &password,
	})
	web.CloseBody(resp)
	if err != nil {
		return fmt.Errorf("failed to send allocate request: %w", err)
	} else if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("allocate request returned HTTP %d", resp.StatusCode)
	}

	// Upload attachment to CDN
	resp, err = web.SendHTTPRequest(ctx, "", http.MethodPut, "", &web.HTTPReqOpt{
		OverrideURL: resp.Header.Get("Location"),
		Body:        encryptedWithMAC,
		ContentType: web.ContentTypeOctetStream,
		Username:    &username,
		Password:    &password,
	})
	web.CloseBody(resp)
	if err != nil {
		return fmt.Errorf("failed to send upload request: %w", err)
	} else if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("upload request returned HTTP %d", resp.StatusCode)
	}
	return nil
}

func (cli *Client) uploadAttachmentTUS(
	ctx context.Context,
	uploadAttributes attachmentV4UploadAttributes,
	encryptedWithMAC []byte,
) error {
	uploadAttributes.Headers["Tus-Resumable"] = "1.0.0"
	uploadAttributes.Headers["Upload-Length"] = fmt.Sprintf("%d", len(encryptedWithMAC))
	uploadAttributes.Headers["Upload-Metadata"] = "filename " + base64.StdEncoding.EncodeToString([]byte(uploadAttributes.Key))

	resp, err := web.SendHTTPRequest(ctx, "", http.MethodPost, "", &web.HTTPReqOpt{
		OverrideURL: uploadAttributes.SignedUploadLocation,
		Body:        encryptedWithMAC,
		ContentType: web.ContentTypeOffsetOctetStream,
		Headers:     uploadAttributes.Headers,
	})
	web.CloseBody(resp)
	// TODO actually support resuming on error
	if err != nil {
		return fmt.Errorf("failed to send upload request: %w", err)
	} else if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		zerolog.Ctx(ctx).Debug().
			Any("request_headers", uploadAttributes.Headers).
			Any("response_headers", resp.Header).
			Str("location", uploadAttributes.SignedUploadLocation).
			Msg("TUS upload failed")
		return fmt.Errorf("upload request returned HTTP %d", resp.StatusCode)
	}
	return nil
}

func (cli *Client) UploadGroupAvatar(ctx context.Context, avatarBytes []byte, gid types.GroupIdentifier, groupMasterKey types.SerializedGroupMasterKey) (string, error) {
	log := zerolog.Ctx(ctx)
	if groupMasterKey == "" {
		var err error
		groupMasterKey, err = cli.Store.GroupStore.MasterKeyFromGroupIdentifier(ctx, gid)
		if err != nil {
			log.Err(err).Msg("Could not get master key from group id")
			return "", err
		} else if groupMasterKey == "" {
			return "", fmt.Errorf("no master key found for group %s", gid)
		}
	}
	groupAuth, err := cli.GetAuthorizationForToday(ctx, masterKeyToBytes(groupMasterKey))
	if err != nil {
		log.Err(err).Msg("Failed to get Authorization for today")
		return "", err
	}
	groupSecretParams, err := libsignalgo.DeriveGroupSecretParamsFromMasterKey(masterKeyToBytes(groupMasterKey))
	if err != nil {
		log.Err(err).Msg("Could not get groupSecretParams from master key")
		return "", err
	}
	attributeBlob := signalpb.GroupAttributeBlob{Content: &signalpb.GroupAttributeBlob_Avatar{Avatar: avatarBytes}}
	encryptedAvatar, err := encryptBlobIntoGroupProperty(groupSecretParams, &attributeBlob)
	if err != nil {
		log.Err(err).Msg("Could not encrypt avatar into Group Property")
		return "", err
	}

	// Get upload form from Signal server
	formPath := "/v2/groups/avatar/form"
	opts := &web.HTTPReqOpt{Username: &groupAuth.Username, Password: &groupAuth.Password, ContentType: web.ContentTypeProtobuf}
	resp, err := web.SendHTTPRequest(ctx, web.StorageHostname, http.MethodGet, formPath, opts)
	if err != nil {
		log.Err(err).Msg("Error sending request fetching avatar upload form")
		return "", err
	}
	body, err := io.ReadAll(resp.Body)
	web.CloseBody(resp)
	if err != nil {
		log.Err(err).Msg("Error decoding response body fetching upload attributes")
		return "", err
	}
	uploadForm := signalpb.AvatarUploadAttributes{}
	err = proto.Unmarshal(body, &uploadForm)
	if err != nil {
		log.Err(err).Msg("failed to unmarshal group avatar upload form")
		return "", err
	}
	requestBody := &bytes.Buffer{}
	w := multipart.NewWriter(requestBody)
	w.WriteField("key", uploadForm.Key)
	w.WriteField("x-amz-credential", uploadForm.Credential)
	w.WriteField("acl", uploadForm.Acl)
	w.WriteField("x-amz-algorithm", uploadForm.Algorithm)
	w.WriteField("x-amz-date", uploadForm.Date)
	w.WriteField("policy", uploadForm.Policy)
	w.WriteField("x-amz-signature", uploadForm.Signature)
	w.WriteField("Content-Type", "application/octet-stream")
	filewriter, _ := w.CreateFormFile("file", "file")
	filewriter.Write(*encryptedAvatar)
	w.Close()

	// Upload avatar to CDN
	resp, err = web.SendHTTPRequest(ctx, web.CDN1Hostname, http.MethodPost, "", &web.HTTPReqOpt{
		Body:        requestBody.Bytes(),
		ContentType: web.ContentType(w.FormDataContentType()),
	})
	web.CloseBody(resp)
	if err != nil {
		log.Err(err).Msg("Error sending request uploading attachment")
		return "", err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		log.Error().Int("status_code", resp.StatusCode).Msg("Error uploading attachment")
		return "", fmt.Errorf("error uploading attachment: %s", resp.Status)
	}

	return uploadForm.Key, nil
}

func verifyMAC(key, body, mac []byte) bool {
	m := hmac.New(sha256.New, key)
	m.Write(body)
	return hmac.Equal(m.Sum(nil), mac)
}

func aesDecrypt(key, ciphertext []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	if len(ciphertext)%aes.BlockSize != 0 {
		return nil, fmt.Errorf("ciphertext not multiple of AES blocksize (%d extra bytes)", len(ciphertext)%aes.BlockSize)
	}

	iv := ciphertext[:IVLength]
	mode := cipher.NewCBCDecrypter(block, iv)
	mode.CryptBlocks(ciphertext, ciphertext)
	pad := ciphertext[len(ciphertext)-1]
	if pad > aes.BlockSize {
		return nil, fmt.Errorf("pad value (%d) larger than AES blocksize (%d)", pad, aes.BlockSize)
	}
	return ciphertext[aes.BlockSize : len(ciphertext)-int(pad)], nil
}

func aesDecryptFile(key []byte, file *os.File, downloadedSize int64) (int64, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return 0, err
	}
	fileReader := io.LimitReader(file, downloadedSize)

	if downloadedSize%aes.BlockSize != 0 {
		return 0, fmt.Errorf("ciphertext not multiple of AES blocksize (%d extra bytes)", downloadedSize%aes.BlockSize)
	}

	iv := make([]byte, IVLength)
	n, err := fileReader.Read(iv)
	if err != nil {
		return 0, fmt.Errorf("failed to read IV from attachment file: %w", err)
	} else if n != IVLength {
		return 0, fmt.Errorf("unexpected IV length read from attachment file: %d", n)
	}
	mode := cipher.NewCBCDecrypter(block, iv)
	buf := make([]byte, 4096)
	var offset int64
	var pad byte
	for {
		n, err = fileReader.Read(buf)
		if err != nil && !errors.Is(err, io.EOF) {
			return 0, fmt.Errorf("failed to read from attachment file: %w", err)
		}
		if n > 0 {
			mode.CryptBlocks(buf[:n], buf[:n])
			if _, err = file.WriteAt(buf[:n], offset); err != nil {
				return 0, fmt.Errorf("failed to write decrypted data to attachment file: %w", err)
			}
			offset += int64(n)
			pad = buf[n-1]
		}
		if errors.Is(err, io.EOF) {
			break
		}
	}
	if pad > aes.BlockSize {
		return 0, fmt.Errorf("pad value (%d) larger than AES blocksize (%d)", pad, aes.BlockSize)
	}
	return downloadedSize - int64(pad), nil
}

func appendMAC(key, body []byte) []byte {
	m := hmac.New(sha256.New, key)
	m.Write(body)
	return m.Sum(body)
}

func aesEncrypt(key, plaintext []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	pad := aes.BlockSize - len(plaintext)%aes.BlockSize
	plaintext = append(plaintext, bytes.Repeat([]byte{byte(pad)}, pad)...)

	ciphertext := make([]byte, len(plaintext))
	iv := random.Bytes(16)

	mode := cipher.NewCBCEncrypter(block, iv)
	mode.CryptBlocks(ciphertext, plaintext)

	return append(iv, ciphertext...), nil
}
