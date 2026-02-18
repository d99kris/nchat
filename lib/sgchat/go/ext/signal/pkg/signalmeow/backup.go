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
	"bufio"
	"compress/gzip"
	"context"
	"crypto/hmac"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
	"time"

	"github.com/rs/zerolog"
	"google.golang.org/protobuf/proto"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/web"
)

const transferArchiveFetchTimeout = 1 * time.Hour

var (
	ErrNoEphemeralBackupKey = errors.New("no ephemeral backup key")
)

const (
	TransferErrorRelinkRequested       = "RELINK_REQUESTED"
	TransferErrorContinueWithoutUpload = "CONTINUE_WITHOUT_UPLOAD"
)

type TransferArchiveMetadata struct {
	CDN   uint32 `json:"cdn"`
	Key   string `json:"key"`
	Error string `json:"error"` // RELINK_REQUESTED or CONTINUE_WITHOUT_UPLOAD
}

func (cli *Client) FetchAndProcessTransfer(ctx context.Context, meta *TransferArchiveMetadata) error {
	if meta.Error != "" {
		return fmt.Errorf("transfer archive error: %s", meta.Error)
	}
	aesKey, hmacKey, err := cli.deriveTransferKeys()
	if err != nil {
		return fmt.Errorf("failed to derive transfer keys: %w", err)
	}
	file, err := os.CreateTemp("", "signalmeow-transfer-archive-*")
	if err != nil {
		return fmt.Errorf("failed to create temporary file: %w", err)
	}
	defer func() {
		_ = file.Close()
		_ = os.Remove(file.Name())
	}()
	err = downloadTransferArchive(ctx, meta, file)
	if err != nil {
		return err
	}
	_, err = file.Seek(0, io.SeekStart)
	if err != nil {
		return fmt.Errorf("failed to seek to start of file: %w", err)
	}
	stat, err := file.Stat()
	if err != nil {
		return fmt.Errorf("failed to stat file: %w", err)
	}
	ok, err := verifyMACStream(hmacKey, file, stat.Size())
	if err != nil {
		return fmt.Errorf("failed to verify MAC: %w", err)
	} else if !ok {
		return fmt.Errorf("checksum mismatch")
	}
	_, err = file.Seek(0, io.SeekStart)
	if err != nil {
		return fmt.Errorf("failed to seek to start of file: %w", err)
	}
	err = cli.Store.DoContactTxn(ctx, func(ctx context.Context) error {
		err = cli.Store.BackupStore.ClearBackup(ctx)
		if err != nil {
			return fmt.Errorf("failed to clear backup: %w", err)
		}
		err = cli.processTransferArchive(ctx, aesKey, hmacKey, file, stat.Size())
		if err != nil {
			return err
		}
		err = cli.Store.BackupStore.RecalculateChatCounts(ctx)
		if err != nil {
			return fmt.Errorf("failed to calculate message counts: %w", err)
		}
		cli.Store.EphemeralBackupKey = nil
		err = cli.Store.DeviceStore.PutDevice(ctx, &cli.Store.DeviceData)
		if err != nil {
			return fmt.Errorf("failed to save device data after clearing ephemeral backup key: %w", err)
		}
		return nil
	})
	if err != nil {
		return err
	}
	return nil
}

func (cli *Client) processTransferArchive(ctx context.Context, aesKey, hmacKey [32]byte, file io.Reader, size int64) error {
	decrypter := aesDecryptStream(aesKey, hmacKey, file, size)
	bufDecrypted := bufio.NewReader(decrypter)
	decompressor, err := gzip.NewReader(bufDecrypted)
	if err != nil {
		return fmt.Errorf("failed to create gzip reader: %w", err)
	}
	// There's an unknown amount of zero padding after the gzip stream,
	// so tell gzip not to try to read another stream after the first one.
	decompressor.Multistream(false)
	err = splitChunksStream(decompressor, (&archiveChunkProcessor{cli: cli, ctx: ctx}).processChunk)
	if err != nil {
		return err
	}
	err = decompressor.Close()
	if err != nil {
		return fmt.Errorf("failed to close gzip reader: %w", err)
	}
	zeroBuf := make([]byte, 256)
	var n int
	// Validate that the zero padding is really all zeroes. This will also finish the hmac checking.
	for {
		n, err = bufDecrypted.Read(zeroBuf)
		if errors.Is(err, io.EOF) && n == 0 {
			break
		} else if err != nil {
			return fmt.Errorf("failed to read zero buffer: %w", err)
		}
		for i := 0; i < n; i++ {
			if zeroBuf[i] != 0 {
				return fmt.Errorf("unexpected data after decompression")
			}
		}
	}
	err = decrypter.Close()
	if err != nil {
		return fmt.Errorf("failed to close decryption reader: %w", err)
	}
	return nil
}

type archiveChunkProcessor struct {
	cli  *Client
	ctx  context.Context
	info *backuppb.BackupInfo
}

const BackupVersion = 1

func (acp *archiveChunkProcessor) processChunk(buf []byte) error {
	if acp.ctx.Err() != nil {
		return acp.ctx.Err()
	}
	if acp.info == nil {
		acp.info = &backuppb.BackupInfo{}
		err := proto.Unmarshal(buf, acp.info)
		if err != nil {
			return fmt.Errorf("failed to unmarshal backup info: %w", err)
		} else if acp.info.GetVersion() != BackupVersion {
			return fmt.Errorf("unsupported backup version: %d", acp.info.GetVersion())
		} else if !hmac.Equal(acp.info.GetMediaRootBackupKey(), acp.cli.Store.MediaRootBackupKey[:]) {
			return fmt.Errorf("media root backup key mismatch")
		}
		zerolog.Ctx(acp.ctx).Info().Any("backup_info", acp.info).Msg("Received backup info")
		return nil
	}
	var frame backuppb.Frame
	err := proto.Unmarshal(buf, &frame)
	if err != nil {
		return fmt.Errorf("failed to unmarshal frame: %w", err)
	}
	return acp.processFrame(&frame)
}

func (acp *archiveChunkProcessor) processFrame(frame *backuppb.Frame) error {
	acp.cli.Log.Trace().Any("backup_frame", frame).Msg("Processing backup frame")
	switch item := frame.Item.(type) {
	case *backuppb.Frame_Recipient:
		if item.Recipient.Destination == nil {
			zerolog.Ctx(acp.ctx).Debug().Msg("Ignoring recipient frame with no destination")
			return nil
		}
		return acp.cli.Store.BackupStore.AddBackupRecipient(acp.ctx, item.Recipient)
	case *backuppb.Frame_Chat:
		return acp.cli.Store.BackupStore.AddBackupChat(acp.ctx, item.Chat)
	case *backuppb.Frame_ChatItem:
		switch item.ChatItem.Item.(type) {
		case *backuppb.ChatItem_DirectStoryReplyMessage, *backuppb.ChatItem_UpdateMessage, nil:
			zerolog.Ctx(acp.ctx).Debug().
				Uint64("chat_id", item.ChatItem.ChatId).
				Uint64("message_id", item.ChatItem.DateSent).
				Type("frame_type", item).
				Msg("Not saving unsupported chat item type")
			return nil
		}
		return acp.cli.Store.BackupStore.AddBackupChatItem(acp.ctx, item.ChatItem)
	default:
		zerolog.Ctx(acp.ctx).Debug().Type("frame_type", item).Msg("Ignoring backup frame")
		return nil
	}
}

func (cli *Client) deriveTransferKeys() (aesKey, hmacKey [32]byte, err error) {
	var backupID *libsignalgo.BackupID
	var mbk *libsignalgo.MessageBackupKey
	if cli.Store.EphemeralBackupKey == nil {
		err = ErrNoEphemeralBackupKey
	} else if backupID, err = cli.Store.EphemeralBackupKey.DeriveBackupID(cli.Store.ACIServiceID()); err != nil {
		err = fmt.Errorf("failed to derive backup ID: %w", err)
	} else if mbk, err = libsignalgo.MessageBackupKeyFromBackupKeyAndID(cli.Store.EphemeralBackupKey, backupID); err != nil {
		err = fmt.Errorf("failed to derive message backup key: %w", err)
	} else if aesKey, err = mbk.GetAESKey(); err != nil {
		err = fmt.Errorf("failed to get AES key: %w", err)
	} else if hmacKey, err = mbk.GetHMACKey(); err != nil {
		err = fmt.Errorf("failed to get HMAC key: %w", err)
	}
	return
}

func downloadTransferArchive(ctx context.Context, meta *TransferArchiveMetadata, writeTo io.Writer) error {
	resp, err := web.GetAttachment(ctx, getAttachmentPath(0, meta.Key), meta.CDN)
	if err != nil {
		return fmt.Errorf("failed to download transfer archive: %w", err)
	}
	if writeToFile, ok := writeTo.(*os.File); ok {
		fileInfo, err := writeToFile.Stat()
		if err != nil {
			return fmt.Errorf("failed to stat destination file: %w", err)
		}
		if size := fileInfo.Size(); size > 0 {
			zerolog.Ctx(ctx).Debug().Int64("skip_count", size).Msg("Transfer archive already exists, skipping bytes")
			_, err = io.CopyN(io.Discard, resp.Body, size)
			if err != nil {
				return fmt.Errorf("failed to skip existing bytes: %w", err)
			}
		}
	}
	_, err = io.Copy(writeTo, resp.Body)
	if err != nil {
		return fmt.Errorf("failed to write transfer archive to disk: %w", err)
	}
	return nil
}

func (cli *Client) WaitForTransfer(ctx context.Context) (*TransferArchiveMetadata, error) {
	if cli.Store.EphemeralBackupKey == nil {
		return nil, ErrNoEphemeralBackupKey
	}
	timeout := time.Now().Add(transferArchiveFetchTimeout)

	for {
		remainingTime := time.Until(timeout)
		if remainingTime < 0 {
			return nil, fmt.Errorf("timed out")
		}
		reqStart := time.Now()
		reqTimeout := min(remainingTime, 5*time.Minute)
		resp, err := cli.tryRequestTransferArchive(ctx, reqTimeout)
		if resp != nil || err != nil {
			return resp, err
		}
		reqDuration := time.Since(reqStart)
		if reqDuration < reqTimeout-10*time.Second {
			time.Sleep(15 * time.Second)
		}
	}
}

func (cli *Client) tryRequestTransferArchive(ctx context.Context, timeout time.Duration) (respBody *TransferArchiveMetadata, err error) {
	reqCtx, cancel := context.WithTimeout(ctx, timeout+15*time.Second)
	defer cancel()
	path := "/v1/devices/transfer_archive?timeout=" + strconv.Itoa(int(timeout.Seconds()))
	resp, err := cli.AuthedWS.SendRequest(reqCtx, http.MethodGet, path, nil, nil)
	if err != nil {
		return nil, err
	} else if resp.GetStatus() == http.StatusNoContent {
		return nil, nil
	} else if resp.GetStatus() != http.StatusOK {
		return nil, fmt.Errorf("unexpected status code %d", resp.GetStatus())
	} else if err = json.Unmarshal(resp.Body, &respBody); err != nil {
		return nil, fmt.Errorf("failed to decode response: %w", err)
	} else {
		return respBody, nil
	}
}
