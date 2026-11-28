// mediaretry.go
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

// Media retry for attachments WhatsApp's media servers no longer serve (HTTP 404/410),
// typically media received through the history sync after linking a device. Like
// WhatsApp Web, ask the phone to re-upload the file and download it from the new path.
// See whatsmeow's Client.SendMediaRetryReceipt.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"sync"
	"time"

	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waMmsRetry"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
)

// how long to wait for the phone to re-upload (it must be online)
const mediaRetryTimeout = 60 * time.Second

type mediaRetryRequest struct {
	connId int
	chatId string
	msgId  string
	fileId string
	info   DownloadInfo
	action int
	timer  *time.Timer
}

var mediaRetryMutex sync.Mutex
var mediaRetryPending = make(map[string]*mediaRetryRequest) // by message id

// last download error per fileId, set by DownloadFromFileId
var mediaDownloadErrors sync.Map

func isMediaGoneError(err error) bool {
	return errors.Is(err, whatsmeow.ErrMediaDownloadFailedWith404) ||
		errors.Is(err, whatsmeow.ErrMediaDownloadFailedWith410)
}

// TryMediaRetry is called after a failed download. It returns true if the phone was asked
// to re-upload the media; the result is then reported asynchronously via
// CWmNewMessageFileNotify.
func TryMediaRetry(connId int, chatId string, msgId string, fileId string, senderId string, fromMe bool, action int) bool {
	errVal, ok := mediaDownloadErrors.LoadAndDelete(fileId)
	if !ok {
		return false
	}
	if err, _ := errVal.(error); !isMediaGoneError(err) {
		return false
	}

	var info DownloadInfo
	if err := json.Unmarshal([]byte(fileId), &info); err != nil || len(info.MediaKey) == 0 {
		LOG_WARNING(fmt.Sprintf("media retry: unusable file id for %s", msgId))
		return false
	}

	client := GetClient(connId)
	if client == nil {
		return false
	}

	chatJid, err := types.ParseJID(chatId)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("media retry: bad chat id %s", chatId))
		return false
	}
	isGroup := chatJid.Server == types.GroupServer
	var senderJid types.JID
	if len(senderId) > 0 {
		senderJid, _ = types.ParseJID(senderId)
	}
	if isGroup && senderJid.IsEmpty() {
		LOG_WARNING(fmt.Sprintf("media retry: unknown sender for group message %s", msgId))
		return false
	}

	msgInfo := &types.MessageInfo{
		MessageSource: types.MessageSource{
			Chat:     chatJid,
			Sender:   senderJid,
			IsFromMe: fromMe,
			IsGroup:  isGroup,
		},
		ID: types.MessageID(msgId),
	}

	req := &mediaRetryRequest{connId: connId, chatId: chatId, msgId: msgId, fileId: fileId, info: info, action: action}
	mediaRetryMutex.Lock()
	if _, exists := mediaRetryPending[msgId]; exists {
		// already waiting for this one
		mediaRetryMutex.Unlock()
		return true
	}
	mediaRetryPending[msgId] = req
	req.timer = time.AfterFunc(mediaRetryTimeout, func() {
		if takeMediaRetry(msgId) != nil {
			LOG_WARNING(fmt.Sprintf("media retry: no answer from phone for %s", msgId))
			CWmNewMessageFileNotify(connId, chatId, msgId, fileId, "", FileStatusDownloadFailed, action)
		}
	})
	mediaRetryMutex.Unlock()

	if err := client.SendMediaRetryReceipt(context.TODO(), msgInfo, info.MediaKey); err != nil {
		LOG_WARNING(fmt.Sprintf("media retry: request failed for %s: %v", msgId, err))
		if r := takeMediaRetry(msgId); r != nil {
			r.timer.Stop()
		}
		return false
	}

	LOG_DEBUG(fmt.Sprintf("media retry: requested re-upload of %s", msgId))
	return true
}

func takeMediaRetry(msgId string) *mediaRetryRequest {
	mediaRetryMutex.Lock()
	defer mediaRetryMutex.Unlock()
	req, ok := mediaRetryPending[msgId]
	if !ok {
		return nil
	}
	delete(mediaRetryPending, msgId)
	return req
}

// HandleMediaRetry handles the phone's answer to a media retry request.
func HandleMediaRetry(connId int, evt *events.MediaRetry) {
	req := takeMediaRetry(string(evt.MessageID))
	if req == nil {
		return
	}
	req.timer.Stop()

	fail := func(reason string) {
		LOG_WARNING(fmt.Sprintf("media retry: %s for %s", reason, req.msgId))
		CWmNewMessageFileNotify(req.connId, req.chatId, req.msgId, req.fileId, "", FileStatusDownloadFailed, req.action)
	}

	retryData, err := whatsmeow.DecryptMediaRetryNotification(evt, req.info.MediaKey)
	if err != nil {
		fail(fmt.Sprintf("decrypt error %v", err))
		return
	}
	if retryData.GetResult() != waMmsRetry.MediaRetryNotification_SUCCESS || len(retryData.GetDirectPath()) == 0 {
		fail(fmt.Sprintf("phone could not re-upload (result %v)", retryData.GetResult()))
		return
	}

	client := GetClient(req.connId)
	if client == nil {
		fail("client is nil")
		return
	}

	req.info.DirectPath = retryData.GetDirectPath()
	req.info.Url = ""
	CWmSetStatus(req.connId, FlagFetching)
	data, err := DownloadFromFileInfo(client, req.info)
	CWmClearStatus(req.connId, FlagFetching)
	if err != nil {
		fail(fmt.Sprintf("download error %v", err))
		return
	}
	if err := os.WriteFile(req.info.TargetPath, data, 0600); err != nil {
		fail(fmt.Sprintf("write error %v", err))
		return
	}

	// store the new direct path, so a later download doesn't need another retry
	newFileId := req.fileId
	if bytes, err := json.Marshal(req.info); err == nil {
		newFileId = string(bytes)
	}

	LOG_DEBUG(fmt.Sprintf("media retry: downloaded %s", req.msgId))
	CWmNewMessageFileNotify(req.connId, req.chatId, req.msgId, newFileId, req.info.TargetPath, FileStatusDownloaded, req.action)
}
