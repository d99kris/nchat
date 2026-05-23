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
	"bytes"
	"context"
	"crypto/hkdf"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/textproto"
	"sync"

	"go.mau.fi/util/exerrors"
	"go.mau.fi/util/random"
	"golang.org/x/sync/semaphore"
	"google.golang.org/protobuf/proto"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

func DownloadStickerPackManifest(ctx context.Context, packID, packKey []byte) (*signalpb.Pack, error) {
	if len(packID) != 16 {
		return nil, fmt.Errorf("invalid pack ID length: %d", len(packID))
	}
	resp, err := downloadStickerData(ctx, fmt.Sprintf("/stickers/%x/manifest.proto", packID), packKey)
	if err != nil {
		return nil, err
	}
	var pack signalpb.Pack
	err = proto.Unmarshal(resp, &pack)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal decrypted manifest: %w", err)
	}
	return &pack, nil
}

func DownloadStickerPackItem(ctx context.Context, packID, packKey []byte, stickerID uint32) ([]byte, error) {
	if len(packID) != 16 {
		return nil, fmt.Errorf("invalid pack ID length: %d", len(packID))
	}
	return downloadStickerData(ctx, fmt.Sprintf("/stickers/%x/full/%d", packID, stickerID), packKey)
}

func downloadStickerData(ctx context.Context, path string, packKey []byte) ([]byte, error) {
	if len(packKey) != 32 {
		return nil, fmt.Errorf("invalid pack key length: %d", len(packKey))
	}
	var body, decrypted []byte
	resp, err := web.SendHTTPRequest(ctx, web.CDN1Hostname, http.MethodGet, path, nil)
	defer web.CloseBody(resp)
	if err != nil {
		return nil, fmt.Errorf("failed to make request: %w", err)
	} else if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d", resp.StatusCode)
	} else if body, err = io.ReadAll(resp.Body); err != nil {
		return nil, fmt.Errorf("failed to read response: %w", err)
	} else if decrypted, err = decryptSticker(packKey, body); err != nil {
		return nil, fmt.Errorf("failed to decrypt response: %w", err)
	} else {
		return decrypted, nil
	}
}

type stickerUploadAttributes struct {
	ACL        string `json:"acl"`
	Algorithm  string `json:"algorithm"`
	Credential string `json:"credential"`
	Date       string `json:"date"`
	ID         int    `json:"id"`
	Key        string `json:"key"`
	Policy     string `json:"policy"`
	Signature  string `json:"signature"`
}

func (sua *stickerUploadAttributes) makeFormBody(encryptedData []byte) (*web.HTTPReqOpt, error) {
	var buf bytes.Buffer
	writer := multipart.NewWriter(&buf)
	var closed bool
	// This isn't necessary in practice, just do it to avoid linter warnings
	defer func() {
		if !closed {
			_ = writer.Close()
		}
	}()
	fields := map[string]string{
		"key":              sua.Key,
		"acl":              sua.ACL,
		"policy":           sua.Policy,
		"x-amz-algorithm":  sua.Algorithm,
		"x-amz-credential": sua.Credential,
		"x-amz-date":       sua.Date,
		"Content-Type":     "application/octet-stream",
	}
	for key, value := range fields {
		err := writer.WriteField(key, value)
		if err != nil {
			return nil, fmt.Errorf("failed to write multipart field %s: %w", key, err)
		}
	}
	filePart, err := writer.CreatePart(textproto.MIMEHeader{
		"Content-Type":        []string{"application/octet-stream"},
		"Content-Disposition": []string{`form-data; name="file"`},
	})
	if err != nil {
		return nil, fmt.Errorf("failed to create multipart file part: %w", err)
	}
	_, err = filePart.Write(encryptedData)
	if err != nil {
		return nil, fmt.Errorf("failed to write file data to multipart body: %w", err)
	}
	err = writer.Close()
	if err != nil {
		return nil, fmt.Errorf("failed to close multipart writer: %w", err)
	}
	closed = true
	return &web.HTTPReqOpt{
		Body:        buf.Bytes(),
		ContentType: web.ContentType(writer.FormDataContentType()),
	}, nil
}

func (sua *stickerUploadAttributes) upload(ctx context.Context, packKey, fileData []byte) error {
	encryptedData, err := macAndAESEncrypt(fileData, deriveStickerPackKey(packKey))
	if err != nil {
		return fmt.Errorf("failed to encrypt sticker data: %w", err)
	}
	req, err := sua.makeFormBody(encryptedData)
	if err != nil {
		return fmt.Errorf("failed to prepare request: %w", err)
	}
	resp, err := web.SendHTTPRequest(ctx, web.CDN1Hostname, http.MethodPost, "/", req)
	if err != nil {
		return err
	}
	_ = resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("unexpected status code %d", resp.StatusCode)
	}
	return nil
}

func (sua *stickerUploadAttributes) uploadAsync(
	ctx context.Context,
	packKey []byte,
	getFileData func(context.Context) ([]byte, error),
	sema *semaphore.Weighted,
	done func(),
	onError func(error),
) {
	defer done()
	err := sema.Acquire(ctx, 1)
	if err != nil {
		return
	}
	defer sema.Release(1)
	fileData, err := getFileData(ctx)
	if err == nil {
		err = sua.upload(ctx, packKey, fileData)
	}
	if err != nil {
		onError(err)
	}
}

type stickerPackUploadAttributes struct {
	PackID   string                     `json:"packId"`
	Manifest *stickerUploadAttributes   `json:"manifest"`
	Stickers []*stickerUploadAttributes `json:"stickers"`
}

var StickerUploadParallelism = 4

func (cli *Client) UploadStickerPack(ctx context.Context, pack *signalpb.Pack, stickerData []func(context.Context) ([]byte, error)) (packID, packKey []byte, err error) {
	for i, sticker := range pack.Stickers {
		if sticker.GetId() >= uint32(len(stickerData)) {
			return nil, nil, fmt.Errorf("sticker ID %d at index %d is out of bounds, only %d sticker blobs provided", sticker.GetId(), i, len(stickerData))
		}
	}
	marshaledPack, err := proto.Marshal(pack)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to marshal pack: %w", err)
	}
	packKey = random.Bytes(32)
	resp, err := cli.AuthedWS.SendRequest(ctx, http.MethodGet, fmt.Sprintf("/v1/sticker/pack/form/%d", len(stickerData)), nil, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to get upload form: %w", err)
	}
	var packAttributes stickerPackUploadAttributes
	err = web.DecodeWSResponseBody(ctx, &packAttributes, resp)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to decode pack attributes: %w", err)
	}
	if len(packAttributes.Stickers) != len(stickerData) {
		return nil, nil, fmt.Errorf("expected %d sticker upload attribute sets, got %d", len(stickerData), len(packAttributes.Stickers))
	}
	packID, err = hex.DecodeString(packAttributes.PackID)
	if err != nil {
		return nil, nil, fmt.Errorf("invalid pack ID in response: %w", err)
	}
	err = packAttributes.Manifest.upload(ctx, packKey, marshaledPack)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to upload manifest: %w", err)
	}
	var wg sync.WaitGroup
	wg.Add(len(packAttributes.Stickers))
	sema := semaphore.NewWeighted(int64(StickerUploadParallelism))
	var errorList []error
	var errorLock sync.Mutex
	for i, attrs := range packAttributes.Stickers {
		go attrs.uploadAsync(ctx, packKey, stickerData[i], sema, wg.Done, func(err error) {
			errorLock.Lock()
			errorList = append(errorList, fmt.Errorf("failed to upload sticker #%d: %w", i+1, err))
			errorLock.Unlock()
		})
	}
	wg.Wait()
	err = ctx.Err()
	if err == nil {
		err = errors.Join(errorList...)
	}
	return
}

func decryptSticker(packKey, ciphertext []byte) ([]byte, error) {
	return macAndAESDecrypt(ciphertext, deriveStickerPackKey(packKey))
}

func deriveStickerPackKey(key []byte) []byte {
	return exerrors.Must(hkdf.Key(sha256.New, key, make([]byte, 32), "Sticker Pack", 2*32))
}
