// gowm.go
//
// Copyright (c) 2021-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"math"
	"mime"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"google.golang.org/protobuf/proto"

	"go.mau.fi/whatsmeow/proto/waCompanionReg"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/proto/waWeb"
	"go.mau.fi/whatsmeow/store"

	"github.com/mdp/qrterminal"

	_ "github.com/mattn/go-sqlite3"
	"github.com/skip2/go-qrcode"
	"go.mau.fi/libsignal/logger"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/appstate"
	"go.mau.fi/whatsmeow/store/sqlstore"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	waLog "go.mau.fi/whatsmeow/util/log"
)

var whatsmeowDate int = 20250212

type JSONMessage []json.RawMessage
type JSONMessageType string

type intString struct {
	i int
	s string
}

type State int64

const (
	None State = iota
	Connecting
	Connected
	Disconnected
	Outdated
)

var (
	mx          sync.Mutex
	clients     map[int]*whatsmeow.Client    = make(map[int]*whatsmeow.Client)
	paths       map[int]string               = make(map[int]string)
	contacts    map[int]map[string]string    = make(map[int]map[string]string)
	states      map[int]State                = make(map[int]State)
	timeReads   map[int]map[string]time.Time = make(map[int]map[string]time.Time)
	expirations map[int]map[string]uint32    = make(map[int]map[string]uint32)
	handlers    map[int]*WmEventHandler      = make(map[int]*WmEventHandler)
	sendTypes   map[int]int                  = make(map[int]int)
)

// keep in sync with enum FileStatus in protocol.h
var FileStatusNone = -1
var FileStatusNotDownloaded = 0
var FileStatusDownloaded = 1
var FileStatusDownloading = 2
var FileStatusDownloadFailed = 3

// keep in sync with enum Flag in status.h
var FlagNone = 0
var FlagOffline = (1 << 0)
var FlagConnecting = (1 << 1)
var FlagOnline = (1 << 2)
var FlagFetching = (1 << 3)
var FlagSending = (1 << 4)
var FlagUpdating = (1 << 5)
var FlagSyncing = (1 << 6)
var FlagAway = (1 << 7)

func AddConn(conn *whatsmeow.Client, path string, sendType int) int {
	mx.Lock()
	var connId int = len(clients)
	clients[connId] = conn
	paths[connId] = path
	contacts[connId] = make(map[string]string)
	states[connId] = None
	timeReads[connId] = make(map[string]time.Time)
	expirations[connId] = make(map[string]uint32)
	handlers[connId] = &WmEventHandler{connId}
	sendTypes[connId] = sendType
	mx.Unlock()
	return connId
}

func RemoveConn(connId int) {
	mx.Lock()
	delete(clients, connId)
	delete(paths, connId)
	delete(contacts, connId)
	delete(states, connId)
	delete(timeReads, connId)
	delete(expirations, connId)
	delete(handlers, connId)
	delete(sendTypes, connId)
	mx.Unlock()
}

func GetClient(connId int) *whatsmeow.Client {
	mx.Lock()
	var client *whatsmeow.Client = clients[connId]
	mx.Unlock()
	return client
}

func GetHandler(connId int) *WmEventHandler {
	mx.Lock()
	var handler *WmEventHandler = handlers[connId]
	mx.Unlock()
	return handler
}

func GetPath(connId int) string {
	mx.Lock()
	var path string = paths[connId]
	mx.Unlock()
	return path
}

func GetSendType(connId int) int {
	mx.Lock()
	var sendType int = sendTypes[connId]
	mx.Unlock()
	return sendType
}

func GetState(connId int) State {
	mx.Lock()
	var state State = states[connId]
	mx.Unlock()
	return state
}

func SetState(connId int, status State) {
	mx.Lock()
	states[connId] = status
	mx.Unlock()
}

func AddContactName(connId int, id string, name string) {
	mx.Lock()
	contacts[connId][id] = name
	mx.Unlock()
}

func GetContactName(connId int, id string) string {
	var name string
	var ok bool
	mx.Lock()
	name, ok = contacts[connId][id]
	mx.Unlock()
	if !ok {
		name = id
	}
	return name
}

func GetTimeRead(connId int, chatId string) time.Time {
	var timeRead time.Time
	var ok bool
	mx.Lock()
	timeRead, ok = timeReads[connId][chatId]
	mx.Unlock()
	if !ok {
		timeRead = time.Time{}
	}
	return timeRead
}

func SetTimeRead(connId int, chatId string, timeRead time.Time) {
	mx.Lock()
	timeReads[connId][chatId] = timeRead
	mx.Unlock()
}

func GetExpiration(connId int, chatId string) uint32 {
	var expiration uint32
	var ok bool
	mx.Lock()
	expiration, ok = expirations[connId][chatId]
	mx.Unlock()
	if !ok {
		expiration = 0
	}
	return expiration
}

func SetExpiration(connId int, chatId string, expiration uint32) {
	mx.Lock()
	expirations[connId][chatId] = expiration
	mx.Unlock()
}

// download info
var downloadInfoVersion = 1 // bump version upon any struct change
type DownloadInfo struct {
	Version    int    `json:"Version_int"`
	Url        string `json:"Url_string"`
	DirectPath string `json:"DirectPath_string"`

	TargetPath string              `json:"TargetPath_string"`
	MediaKey   []byte              `json:"MediaKey_arraybyte"`
	MediaType  whatsmeow.MediaType `json:"MediaType_MediaType"`
	Size       int                 `json:"Size_int"`

	FileEncSha256 []byte `json:"FileEncSha256_arraybyte"`
	FileSha256    []byte `json:"FileSha256_arraybyte"`
}

func DownloadableMessageToFileId(client *whatsmeow.Client, msg whatsmeow.DownloadableMessage, targetPath string) string {
	var info DownloadInfo
	info.Version = downloadInfoVersion

	info.TargetPath = targetPath
	info.MediaKey = msg.GetMediaKey()
	info.Size = whatsmeow.GetDownloadSize(msg)
	info.FileEncSha256 = msg.GetFileEncSHA256()
	info.FileSha256 = msg.GetFileSHA256()

	info.MediaType = whatsmeow.GetMediaType(msg)
	if len(info.MediaType) == 0 {
		LOG_WARNING(fmt.Sprintf("unknown mediatype in msg %+v", msg))
		return ""
	}

	urlable, ok := msg.(whatsmeow.DownloadableMessageWithURL)
	if ok && len(urlable.GetUrl()) > 0 {
		info.Url = urlable.GetUrl()
	} else if len(msg.GetDirectPath()) > 0 {
		info.DirectPath = msg.GetDirectPath()
	} else {
		LOG_WARNING(fmt.Sprintf("url and path not present"))
		return ""
	}

	LOG_TRACE(fmt.Sprintf("fileInfo %#v", info))
	bytes, err := json.Marshal(info)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("json encode failed"))
		return ""
	}

	str := string(bytes)
	LOG_TRACE(fmt.Sprintf("fileId %s", str))

	return str
}

func DownloadFromFileId(client *whatsmeow.Client, fileId string) (string, int) {
	LOG_TRACE(fmt.Sprintf("fileId %s", fileId))
	var info DownloadInfo
	json.Unmarshal([]byte(fileId), &info)
	if info.Version != downloadInfoVersion {
		LOG_WARNING(fmt.Sprintf("unsupported version %d", info.Version))
		return "", FileStatusDownloadFailed
	}

	LOG_TRACE(fmt.Sprintf("fileInfo %#v", info))

	targetPath := info.TargetPath
	filePath := ""
	fileStatus := FileStatusNone

	// download if not yet present
	if _, statErr := os.Stat(targetPath); os.IsNotExist(statErr) {
		LOG_TRACE(fmt.Sprintf("download new %#v", targetPath))
		CWmSetStatus(FlagFetching)

		data, err := DownloadFromFileInfo(client, info)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("download error %#v", err))
			fileStatus = FileStatusDownloadFailed
		} else {
			file, err := os.Create(targetPath)
			defer file.Close()
			if err != nil {
				LOG_WARNING(fmt.Sprintf("create error %#v", err))
				fileStatus = FileStatusDownloadFailed
			} else {
				_, err = file.Write(data)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("write error %#v", err))
					fileStatus = FileStatusDownloadFailed
				} else {
					LOG_TRACE(fmt.Sprintf("download ok"))
					filePath = targetPath
					fileStatus = FileStatusDownloaded
				}
			}
		}
		CWmClearStatus(FlagFetching)
	} else {
		LOG_TRACE(fmt.Sprintf("download cached %#v", targetPath))
		filePath = targetPath
		fileStatus = FileStatusDownloaded
	}

	return filePath, fileStatus
}

func DownloadFromFileInfo(client *whatsmeow.Client, info DownloadInfo) ([]byte, error) {

	if len(info.Url) > 0 {
		LOG_TRACE(fmt.Sprintf("download url: %s", info.Url))
		return client.DownloadMediaWithUrl(info.Url, info.MediaKey, info.MediaType, info.Size, info.FileEncSha256, info.FileSha256)
	} else if len(info.DirectPath) > 0 {
		LOG_TRACE(fmt.Sprintf("download directpath: %s", info.DirectPath))
		return client.DownloadMediaWithPath(info.DirectPath, info.FileEncSha256, info.FileSha256, info.MediaKey, info.Size, info.MediaType, whatsmeow.GetMMSType(info.MediaType))
	} else {
		LOG_WARNING(fmt.Sprintf("url and path not present"))
		return nil, whatsmeow.ErrNoURLPresent
	}
}

// utils
func ShowImage(path string) {
	switch runtime.GOOS {
	case "linux":
		LOG_DEBUG("xdg-open " + path)
		exec.Command("xdg-open", path).Start()
	case "darwin":
		LOG_DEBUG("open " + path)
		exec.Command("open", path).Start()
	default:
		LOG_WARNING(fmt.Sprintf("unsupported os \"%s\"", runtime.GOOS))
	}
}

func HasGUI() bool {
	_, isForceQrTerminalSet := os.LookupEnv("FORCE_QR_TERMINAL")
	if isForceQrTerminalSet {
		return false
	}

	switch runtime.GOOS {
	case "darwin":
		LOG_INFO(fmt.Sprintf("has gui"))
		LOG_DEBUG(fmt.Sprintf("gui check: [darwin default true]"))
		return true

	case "linux":
		_, isDisplaySet := os.LookupEnv("DISPLAY")
		file, err := ioutil.TempFile("/tmp", "nchat-x11check.*.sh")
		if err != nil {
			LOG_WARNING(fmt.Sprintf("create file failed %#v", err))
			return isDisplaySet
		}

		defer os.Remove(file.Name())
		content := "#!/usr/bin/env bash\n\n" +
			"if command -v timeout &> /dev/null; then\n" +
			"  CMD=\"timeout 1s xset q\"\n" +
			"else\n" +
			"  CMD=\"xset q\"\n" +
			"fi\n" +
			"echo \"${CMD}\"\n" +
			"${CMD} > /dev/null\n" +
			"exit ${?}\n"

		_, err = io.WriteString(file, content)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("write file failed %#v", err))
			return isDisplaySet
		}

		err = file.Close()
		if err != nil {
			LOG_WARNING(fmt.Sprintf("close file failed %#v", err))
			return isDisplaySet
		}

		err = os.Chmod(file.Name(), 0777)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("chmod file failed %#v", err))
			return isDisplaySet
		}

		cmdout, err := exec.Command(file.Name()).CombinedOutput()
		if err == nil {
			LOG_INFO(fmt.Sprintf("has gui"))
			LOG_DEBUG(fmt.Sprintf("gui check: %s", strings.TrimSuffix(string(cmdout), "\n")))
			return true
		} else {
			LOG_INFO(fmt.Sprintf("no gui"))
			LOG_DEBUG(fmt.Sprintf("gui check: %s", strings.TrimSuffix(string(cmdout), "\n")))
			return false
		}

	default:
		LOG_INFO(fmt.Sprintf("no gui"))
		LOG_DEBUG(fmt.Sprintf("gui check: [other \"%s\" default false]", runtime.GOOS))
		return false
	}
}

func BoolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

func IntToBool(i int) bool {
	return i != 0
}

func StringToInt(s string) int {
	i, err := strconv.Atoi(s)
	if err != nil {
		return 0
	}
	return i
}

func JidToStr(jid types.JID) string {
	return jid.User + "@" + jid.Server
}

func GetChatId(chatJid types.JID, senderJid types.JID) string {
	if chatJid.Server == "broadcast" {
		if chatJid.User == "status" {
			return JidToStr(chatJid) // status updates
		} else {
			return JidToStr(senderJid) // broadcast messages
		}
	} else {
		return JidToStr(chatJid) // regular messages
	}
}

func IsRead(isSyncRead bool, isSelfChat bool, fromMe bool, timeSent time.Time, timeRead time.Time) bool {
	// consider message read:
	// - during initial sync for chats with no unread messages
	// - in self chat / saved messages
	// - from others and timeSent <= timeRead i.e.. !timeSent.After(timeRead)
	return isSyncRead || isSelfChat || (!fromMe && !timeSent.After(timeRead))
}

func ParseWebMessageInfo(selfJid types.JID, chatJid types.JID, webMsg *waWeb.WebMessageInfo) *types.MessageInfo {
	info := types.MessageInfo{
		MessageSource: types.MessageSource{
			Chat:     chatJid,
			IsFromMe: webMsg.GetKey().GetFromMe(),
			IsGroup:  chatJid.Server == types.GroupServer,
		},
		ID:        webMsg.GetKey().GetId(),
		PushName:  webMsg.GetPushName(),
		Timestamp: time.Unix(int64(webMsg.GetMessageTimestamp()), 0),
	}
	if info.IsFromMe {
		info.Sender = selfJid.ToNonAD()
	} else if webMsg.GetParticipant() != "" {
		info.Sender, _ = types.ParseJID(webMsg.GetParticipant())
	} else if webMsg.GetKey().GetParticipant() != "" {
		info.Sender, _ = types.ParseJID(webMsg.GetKey().GetParticipant())
	} else {
		info.Sender = chatJid
	}
	if info.Sender.IsEmpty() {
		return nil
	}
	return &info
}

func SliceIndex(list []string, value string, defaultValue int) int {
	index := slices.Index(list, value)
	if index == -1 {
		index = defaultValue
	}
	return index
}

func ExtensionByType(mimeType string, defaultExt string) string {
	ext := defaultExt
	exts, extErr := mime.ExtensionsByType(mimeType)
	if extErr == nil && len(exts) > 0 {
		// prefer common extensions over less common (.jpe, etc) returned by mime library
		preferredExts := []string{".jpg", ".jpeg"}
		sort.Slice(exts, func(i, j int) bool {
			return SliceIndex(preferredExts, exts[i], math.MaxInt32) < SliceIndex(preferredExts, exts[j], math.MaxInt32)
		})
		ext = exts[0]
	}

	return ext
}

// logger
type ncLogger struct{}

func (s *ncLogger) Debugf(msg string, args ...interface{}) {
	LOG_DEBUG(fmt.Sprintf("whatsmeow %s", fmt.Sprintf(msg, args...)))
}

func (s *ncLogger) Infof(msg string, args ...interface{}) {
	LOG_INFO(fmt.Sprintf("whatsmeow %s", fmt.Sprintf(msg, args...)))
}

func (s *ncLogger) Warnf(msg string, args ...interface{}) {
	LOG_WARNING(fmt.Sprintf("whatsmeow %s", fmt.Sprintf(msg, args...)))
}

func (s *ncLogger) Errorf(msg string, args ...interface{}) {
	LOG_ERROR(fmt.Sprintf("whatsmeow %s", fmt.Sprintf(msg, args...)))
}

func (s *ncLogger) Sub(mod string) waLog.Logger {
	return s
}

func NcLogger() waLog.Logger {
	return &ncLogger{}
}

// loggable
type ncSignalLogger struct{}

func (s *ncSignalLogger) Debug(caller, msg string) {
	LOG_DEBUG(fmt.Sprintf("whatsmeow %s", fmt.Sprintf("%s %s", caller, msg)))
}

func (s *ncSignalLogger) Info(caller, msg string) {
	LOG_INFO(fmt.Sprintf("whatsmeow %s", fmt.Sprintf("%s %s", caller, msg)))
}

func (s *ncSignalLogger) Warning(caller, msg string) {
	LOG_WARNING(fmt.Sprintf("whatsmeow %s", fmt.Sprintf("%s %s", caller, msg)))
}

func (s *ncSignalLogger) Error(caller, msg string) {
	LOG_ERROR(fmt.Sprintf("whatsmeow %s", fmt.Sprintf("%s %s", caller, msg)))
}

func (s *ncSignalLogger) Configure(ss string) {
}

// event handling
type WmEventHandler struct {
	connId int
}

func (handler *WmEventHandler) HandleEvent(rawEvt interface{}) {
	switch evt := rawEvt.(type) {

	case *events.AppStateSyncComplete:
		// this happens after initial logon via QR code
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		if evt.Name == appstate.WAPatchCriticalBlock {
			LOG_TRACE("AppStateSyncComplete WAPatchCriticalBlock")
			handler.HandleConnected()
		} else if evt.Name == appstate.WAPatchRegular {
			LOG_TRACE("AppStateSyncComplete WAPatchRegular")
			handler.GetContacts()
		}

	case *events.PushNameSetting:
		// send presence when the pushname is changed remotely
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleConnected()

	case *events.PushName:
		// other device changed our friendly name
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.Connected:
		// connected
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleConnected()
		SetState(handler.connId, Connected)
		CWmSetStatus(FlagOnline)
		CWmClearStatus(FlagConnecting)

	case *events.Disconnected:
		// disconnected
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		CWmClearStatus(FlagOnline)

	case *events.StreamReplaced:
		// TODO: find out when exactly this happens and how to handle it
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.Message:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleMessage(evt.Info, evt.Message, false /*isSyncRead*/)

	case *events.Receipt:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleReceipt(evt)

	case *events.Presence:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandlePresence(evt)

	case *events.ChatPresence:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleChatPresence(evt)

	case *events.HistorySync:
		// This happens after initial logon via QR code (after AppStateSyncComplete)
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleHistorySync(evt)

	case *events.AppState:
		LOG_TRACE(fmt.Sprintf("%#v - %#v / %#v", evt, evt.Index, evt.SyncActionValue))

	case *events.LoggedOut:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleLoggedOut()

	case *events.QR:
		// handled in WmLogin
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.PairSuccess:
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.JoinedGroup:
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.OfflineSyncCompleted:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.GetContacts()

	case *events.GroupInfo:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleGroupInfo(evt)

	case *events.DeleteChat:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleDeleteChat(evt)

	case *events.Mute:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleMute(evt)

	case *events.Pin:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandlePin(evt)

	case *events.ClientOutdated:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleClientOutdated()

	case *events.DeleteForMe:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleDeleteForMe(evt)

	default:
		LOG_TRACE(fmt.Sprintf("Event type not handled: %#v", rawEvt))
	}
}

func (handler *WmEventHandler) HandleConnected() {
	LOG_TRACE(fmt.Sprintf("HandleConnected"))
	var client *whatsmeow.Client = GetClient(handler.connId)

	if len(client.Store.PushName) == 0 {
		return
	}
}

func (handler *WmEventHandler) HandleReceipt(receipt *events.Receipt) {
	if receipt.Type == events.ReceiptTypeRead || receipt.Type == events.ReceiptTypeReadSelf {
		LOG_TRACE(fmt.Sprintf("%#v was read by %s at %s", receipt.MessageIDs, receipt.SourceString(), receipt.Timestamp))
		connId := handler.connId
		chatId := receipt.MessageSource.Chat.ToNonAD().String()
		isRead := true
		for _, msgId := range receipt.MessageIDs {
			LOG_TRACE(fmt.Sprintf("Call CWmNewMessageStatusNotify"))
			CWmNewMessageStatusNotify(connId, chatId, msgId, BoolToInt(isRead))
		}
	}
}

func (handler *WmEventHandler) HandlePresence(presence *events.Presence) {
	if presence.From.Server != types.GroupServer {
		connId := handler.connId
		chatId := ""
		userId := presence.From.ToNonAD().String()
		isOnline := !presence.Unavailable
		timeSeen := int(presence.LastSeen.Unix())
		isTyping := false
		LOG_TRACE(fmt.Sprintf("Call CWmNewStatusNotify"))
		CWmNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping), timeSeen)
	}
}

func (handler *WmEventHandler) HandleChatPresence(chatPresence *events.ChatPresence) {
	connId := handler.connId
	chatId := chatPresence.MessageSource.Chat.ToNonAD().String()
	userId := chatPresence.MessageSource.Sender.ToNonAD().String()
	isOnline := true
	isTyping := (chatPresence.State == types.ChatPresenceComposing)
	LOG_TRACE(fmt.Sprintf("Call CWmNewStatusNotify"))
	CWmNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping), -1)
}

func (handler *WmEventHandler) HandleHistorySync(historySync *events.HistorySync) {
	var client *whatsmeow.Client = GetClient(handler.connId)
	selfJid := *client.Store.ID

	LOG_TRACE(fmt.Sprintf("HandleHistorySync SyncType %s Progress %d",
		(*historySync.Data.SyncType).String(), historySync.Data.GetProgress()))

	if historySync.Data.GetProgress() < 98 {
		LOG_TRACE("Set Syncing")
		CWmSetStatus(FlagSyncing)
	}

	conversations := historySync.Data.GetConversations()
	for _, conversation := range conversations {
		LOG_TRACE(fmt.Sprintf("HandleHistorySync Conversation %#v", *conversation))

		chatJid, _ := types.ParseJID(conversation.GetId())

		isUnread := 0
		lastMessageTime := 0

		isSyncRead := (conversation.GetUnreadCount() == 0)
		hasMessages := false
		syncMessages := conversation.GetMessages()
		for _, syncMessage := range syncMessages {
			webMessageInfo := syncMessage.Message
			messageInfo := ParseWebMessageInfo(selfJid, chatJid, webMessageInfo)
			message := webMessageInfo.GetMessage()

			if (messageInfo == nil) || (message == nil) {
				continue
			}

			handler.HandleMessage(*messageInfo, message, isSyncRead)
			hasMessages = true

			messageTime := int(messageInfo.Timestamp.Unix())
			if messageTime > lastMessageTime {
				lastMessageTime = messageTime
			}
		}

		if hasMessages {
			isMuted := false
			isPinned := false
			settings, setErr := client.Store.ChatSettings.GetChatSettings(chatJid)
			if setErr != nil {
				LOG_WARNING(fmt.Sprintf("Get chat settings failed %#v", setErr))
			} else {
				if settings.Found {
					mutedUntil := settings.MutedUntil.Unix()
					isMuted = (mutedUntil == -1) || (mutedUntil > time.Now().Unix())
					isPinned = settings.Pinned
				} else {
					LOG_DEBUG(fmt.Sprintf("Chat settings not found %s", JidToStr(chatJid)))
				}
			}

			LOG_TRACE(fmt.Sprintf("Call CWmNewChatsNotify %s %d %t %t", JidToStr(chatJid), len(syncMessages), isMuted, isPinned))
			CWmNewChatsNotify(handler.connId, JidToStr(chatJid), isUnread, BoolToInt(isMuted), BoolToInt(isPinned), lastMessageTime)
		} else {
			LOG_TRACE(fmt.Sprintf("Skip CWmNewChatsNotify %s %d", JidToStr(chatJid), len(syncMessages)))
		}

	}

	if historySync.Data.GetProgress() == 100 {
		LOG_TRACE("Clear Syncing")
		CWmClearStatus(FlagSyncing)
	}
}

func (handler *WmEventHandler) HandleGroupInfo(groupInfo *events.GroupInfo) {
	connId := handler.connId
	client := GetClient(connId)
	chatId := JidToStr(groupInfo.JID)

	selfJid := *client.Store.ID
	senderJidStr := ""
	if (groupInfo.Sender != nil) && (JidToStr(*groupInfo.Sender) != JidToStr(groupInfo.JID)) {
		senderJidStr = JidToStr(*groupInfo.Sender)
	}

	// text
	text := ""
	if groupInfo.Name != nil {
		// Group name change
		if senderJidStr == "" {
			senderJidStr = JidToStr(groupInfo.JID)
		}

		groupName := *groupInfo.Name
		text = "[Changed group name to " + groupName.Name + "]"
	} else if len(groupInfo.Join) > 0 {
		// Group member joined
		if (len(groupInfo.Join) == 1) && ((senderJidStr == "") || (senderJidStr == JidToStr(groupInfo.Join[0]))) {
			senderJidStr = JidToStr(groupInfo.Join[0])
			text = "[Joined]"
		} else {
			if senderJidStr == "" {
				senderJidStr = JidToStr(groupInfo.JID)
			}

			joined := ""
			for _, jid := range groupInfo.Join {
				if joined != "" {
					joined += ", "
				}

				joined += GetContactName(connId, JidToStr(jid))
			}

			text = "[Added " + joined + "]"
		}
	} else if len(groupInfo.Leave) > 0 {
		// Group member left
		if (len(groupInfo.Leave) == 1) && ((senderJidStr == "") || (senderJidStr == JidToStr(groupInfo.Leave[0]))) {
			senderJidStr = JidToStr(groupInfo.Leave[0])
			text = "[Left]"
		} else {
			if senderJidStr == "" {
				senderJidStr = JidToStr(groupInfo.JID)
			}

			left := ""
			for _, jid := range groupInfo.Leave {
				if left != "" {
					left += ", "
				}

				left += GetContactName(connId, JidToStr(jid))
			}

			text = "[Removed " + left + "]"
		}
	}

	if text == "" {
		LOG_TRACE(fmt.Sprintf("HandleGroupInfo ignore"))
		return
	} else {
		LOG_TRACE(fmt.Sprintf("HandleGroupInfo notify"))
	}

	// context
	quotedId := ""

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	timeSent := int(groupInfo.Timestamp.Unix())
	msgId := strconv.Itoa(timeSent) // group info updates do not have msg id
	fromMe := (senderJidStr == JidToStr(selfJid))
	senderId := senderJidStr
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	isSyncRead := false
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, groupInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleDeleteChat(deleteChat *events.DeleteChat) {
	connId := handler.connId
	chatId := deleteChat.JID.ToNonAD().String()

	LOG_TRACE(fmt.Sprintf("Call CWmDeleteChatNotify %s", chatId))
	CWmDeleteChatNotify(connId, chatId)
}

func (handler *WmEventHandler) HandleMute(mute *events.Mute) {
	connId := handler.connId
	chatId := mute.JID.ToNonAD().String()
	muteAction := mute.Action
	if muteAction == nil {
		LOG_WARNING(fmt.Sprintf("mute event missing mute action"))
		return
	}

	isMuted := *muteAction.Muted

	LOG_TRACE(fmt.Sprintf("Call CWmUpdateMuteNotify %s %s", chatId, strconv.FormatBool(isMuted)))
	CWmUpdateMuteNotify(connId, chatId, BoolToInt(isMuted))
}

func (handler *WmEventHandler) HandlePin(pin *events.Pin) {
	connId := handler.connId
	chatId := pin.JID.ToNonAD().String()
	pinAction := pin.Action
	if pinAction == nil {
		LOG_WARNING(fmt.Sprintf("pin event missing pin action"))
		return
	}

	isPinned := *pinAction.Pinned
	timePinned := int(pin.Timestamp.Unix())

	LOG_TRACE(fmt.Sprintf("Call CWmUpdatePinNotify %s %s %d", chatId, strconv.FormatBool(isPinned), timePinned))
	CWmUpdatePinNotify(connId, chatId, BoolToInt(isPinned), timePinned)
}

func (handler *WmEventHandler) HandleClientOutdated() {
	connId := handler.connId
	LOG_WARNING(fmt.Sprintf("Client Outdated"))
	SetState(connId, Outdated)
}

func (handler *WmEventHandler) HandleDeleteForMe(deleteForMe *events.DeleteForMe) {
	connId := handler.connId
	chatId := JidToStr(deleteForMe.ChatJID)
	msgId := deleteForMe.MessageID
	LOG_TRACE(fmt.Sprintf("Call CWmDeleteMessageNotify %s %s", chatId, msgId))
	CWmDeleteMessageNotify(connId, chatId, msgId)
}

func (handler *WmEventHandler) HandleLoggedOut() {
	LOG_INFO("logged out by server, reinit")
	connId := handler.connId

	LOG_TRACE(fmt.Sprintf("Call CWmReinit"))
	CWmReinit(connId)
}

func GetNameFromContactInfo(contactInfo types.ContactInfo) string {
	if len(contactInfo.FullName) > 0 {
		return contactInfo.FullName
	}

	if len(contactInfo.FirstName) > 0 {
		return contactInfo.FirstName
	}

	if len(contactInfo.PushName) > 0 {
		return contactInfo.PushName
	}

	if len(contactInfo.BusinessName) > 0 {
		return contactInfo.BusinessName
	}

	return ""
}

func PhoneFromUserId(userId string) string {
	phone := ""
	if strings.HasSuffix(userId, "@s.whatsapp.net") {
		phone = strings.Replace(userId, "@s.whatsapp.net", "", 1)
	}

	LOG_TRACE(fmt.Sprintf("user %s phone %s", userId, phone))
	return phone
}

func (handler *WmEventHandler) GetContacts() {
	var client *whatsmeow.Client = GetClient(handler.connId)
	connId := handler.connId
	LOG_TRACE(fmt.Sprintf("GetContacts"))

	CWmSetStatus(FlagFetching)

	// contacts
	contacts, contErr := client.Store.Contacts.GetAllContacts()
	if contErr != nil {
		LOG_WARNING(fmt.Sprintf("get all contacts failed %#v", contErr))
	} else {
		LOG_TRACE(fmt.Sprintf("contacts %#v", contacts))
		for jid, contactInfo := range contacts {
			name := GetNameFromContactInfo(contactInfo)
			if len(name) > 0 {
				userId := JidToStr(jid)
				phone := PhoneFromUserId(userId)
				LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", userId, name))
				CWmNewContactsNotify(connId, userId, name, phone, BoolToInt(false))
				AddContactName(connId, userId, name)
			} else {
				LOG_WARNING(fmt.Sprintf("Skip CWmNewContactsNotify %s %#v", JidToStr(jid), contactInfo))
			}
		}
	}

	// special handling for self
	selfId := JidToStr(*client.Store.ID)
	selfName := "" // overridden by ui
	selfPhone := PhoneFromUserId(selfId)
	LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", selfId, selfName))
	CWmNewContactsNotify(connId, selfId, selfName, selfPhone, BoolToInt(true))
	AddContactName(connId, selfId, selfName)

	// special handling for official whatsapp account
	whatsappId := "0@s.whatsapp.net"
	whatsappName := "WhatsApp"
	whatsappPhone := ""
	LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", whatsappId, whatsappName))
	CWmNewContactsNotify(connId, whatsappId, whatsappName, whatsappPhone, BoolToInt(false))
	AddContactName(connId, whatsappId, whatsappName)

	// special handling for status updates
	statusId := "status@broadcast"
	statusName := "Status Updates"
	statusPhone := ""
	LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", statusId, statusName))
	CWmNewContactsNotify(connId, statusId, statusName, statusPhone, BoolToInt(false))
	AddContactName(connId, statusId, statusName)

	// groups
	groups, groupErr := client.GetJoinedGroups()
	if groupErr != nil {
		LOG_WARNING(fmt.Sprintf("get joined groups failed %#v", groupErr))
	} else {
		LOG_TRACE(fmt.Sprintf("groups %#v", groups))
		for _, group := range groups {
			groupId := JidToStr(group.JID)
			groupName := group.GroupName.Name
			groupPhone := ""
			LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", groupId, groupName))
			CWmNewContactsNotify(connId, groupId, groupName, groupPhone, BoolToInt(false))
			AddContactName(connId, groupId, groupName)

			if group.GroupEphemeral.IsEphemeral {
				SetExpiration(connId, groupId, group.GroupEphemeral.DisappearingTimer)
			}
		}
	}

	CWmClearStatus(FlagFetching)
}

func (handler *WmEventHandler) HandleMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	switch {
	case msg.Conversation != nil || msg.ExtendedTextMessage != nil:
		handler.HandleTextMessage(messageInfo, msg, isSyncRead)

	case msg.ImageMessage != nil:
		handler.HandleImageMessage(messageInfo, msg, isSyncRead)

	case msg.VideoMessage != nil:
		handler.HandleVideoMessage(messageInfo, msg, isSyncRead)

	case msg.AudioMessage != nil:
		handler.HandleAudioMessage(messageInfo, msg, isSyncRead)

	case msg.DocumentMessage != nil:
		handler.HandleDocumentMessage(messageInfo, msg, isSyncRead)

	case msg.StickerMessage != nil:
		handler.HandleStickerMessage(messageInfo, msg, isSyncRead)

	case msg.TemplateMessage != nil:
		handler.HandleTemplateMessage(messageInfo, msg, isSyncRead)

	case msg.ReactionMessage != nil:
		handler.HandleReactionMessage(messageInfo, msg, isSyncRead)

	case msg.ProtocolMessage != nil:
		handler.HandleProtocolMessage(messageInfo, msg, isSyncRead)

	default:
		handler.HandleUnsupportedMessage(messageInfo, msg, isSyncRead)
	}
}

func (handler *WmEventHandler) HandleTextMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("TextMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)
	text := ""

	// text
	quotedId := ""
	if msg.GetExtendedTextMessage() == nil {
		text = msg.GetConversation()
	} else {
		text = msg.GetExtendedTextMessage().GetText()
		ci := msg.GetExtendedTextMessage().GetContextInfo()
		if ci != nil {
			quotedId = ci.GetStanzaId()
		}
	}

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleImageMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("ImageMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get image part
	img := msg.GetImageMessage()
	if img == nil {
		LOG_WARNING(fmt.Sprintf("get image message failed"))
		return
	}

	// get extension
	ext := ExtensionByType(img.GetMimetype(), ".jpg")

	// text
	text := img.GetCaption()

	// context
	quotedId := ""
	ci := img.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, img, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: image", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleVideoMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("VideoMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get video part
	vid := msg.GetVideoMessage()
	if vid == nil {
		LOG_WARNING(fmt.Sprintf("get video message failed"))
		return
	}

	// get extension
	ext := ExtensionByType(vid.GetMimetype(), ".mp4")

	// text
	text := vid.GetCaption()

	// context
	quotedId := ""
	ci := vid.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, vid, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: video", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleAudioMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("AudioMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get audio part
	aud := msg.GetAudioMessage()
	if aud == nil {
		LOG_WARNING(fmt.Sprintf("get audio message failed"))
		return
	}

	// get extension
	ext := ExtensionByType(aud.GetMimetype(), ".ogg")

	// text
	text := ""

	// context
	quotedId := ""
	ci := aud.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, aud, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: audio", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleDocumentMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("DocumentMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get doc part
	doc := msg.GetDocumentMessage()
	if doc == nil {
		LOG_WARNING(fmt.Sprintf("get document message failed"))
		return
	}

	// text
	text := doc.GetCaption()

	// context
	quotedId := ""
	ci := doc.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s-%s", tmpPath, messageInfo.ID, *doc.FileName)
	fileId := DownloadableMessageToFileId(client, doc, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: document", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleStickerMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("StickerMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get sticker part
	sticker := msg.GetStickerMessage()
	if sticker == nil {
		LOG_WARNING(fmt.Sprintf("get sticker message failed"))
		return
	}

	// get extension
	ext := ExtensionByType(sticker.GetMimetype(), ".webp")

	// text
	text := ""

	// context
	quotedId := ""
	ci := sticker.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, sticker, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: sticker", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleTemplateMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("TemplateMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get template part
	tpl := msg.GetTemplateMessage()
	if tpl == nil {
		LOG_WARNING(fmt.Sprintf("get template message failed"))
		return
	}

	// handle hydrated template
	hydtpl := tpl.GetHydratedTemplate()
	if hydtpl == nil {
		LOG_TRACE(fmt.Sprintf("unhandled template type"))
		return
	}

	// text slice
	var texts []string

	// title
	switch hydtitle := hydtpl.GetTitle().(type) {
	case *waE2E.TemplateMessage_HydratedFourRowTemplate_DocumentMessage:
		texts = append(texts, "[Document]")
	case *waE2E.TemplateMessage_HydratedFourRowTemplate_ImageMessage:
		texts = append(texts, "[Image]")
	case *waE2E.TemplateMessage_HydratedFourRowTemplate_VideoMessage:
		texts = append(texts, "[Video]")
	case *waE2E.TemplateMessage_HydratedFourRowTemplate_LocationMessage:
		texts = append(texts, "[Location]")
	case *waE2E.TemplateMessage_HydratedFourRowTemplate_HydratedTitleText:
		if hydtitle.HydratedTitleText != "" {
			texts = append(texts, hydtitle.HydratedTitleText)
		}
	}

	// content
	content := hydtpl.GetHydratedContentText()
	if content != "" {
		texts = append(texts, content)
	}

	// buttons
	buttons := hydtpl.GetHydratedButtons()
	for _, button := range buttons {
		switch hydbutton := button.GetHydratedButton().(type) {
		case *waE2E.HydratedTemplateButton_QuickReplyButton:
			texts = append(texts, fmt.Sprintf("%s", hydbutton.QuickReplyButton.GetDisplayText()))
		case *waE2E.HydratedTemplateButton_CallButton:
			texts = append(texts, fmt.Sprintf("%s: %s", hydbutton.CallButton.GetDisplayText(), hydbutton.CallButton.GetPhoneNumber()))
		}
	}

	// footer
	footer := hydtpl.GetHydratedFooterText()
	if footer != "" {
		texts = append(texts, footer)
	}

	// text
	text := strings.Join(texts, "\n")

	// context
	quotedId := ""
	ci := tpl.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: template", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleReactionMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("ReactionMessage"))

	connId := handler.connId

	// get reaction part
	reaction := msg.GetReactionMessage()
	if reaction == nil {
		LOG_WARNING(fmt.Sprintf("get reaction message failed"))
		return
	}

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := reaction.GetText()
	msgId := *reaction.Key.ID

	CWmNewMessageReactionNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe))

	// @todo: add auto-marking reactions of read, investigate why below does not work
	//reMsgId := messageInfo.ID
	//WmMarkMessageRead(connId, chatId, senderId, reMsgId)
}

func (handler *WmEventHandler) HandleProtocolMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("ProtocolMessage"))

	// get protocol part
	protocol := msg.GetProtocolMessage()
	if protocol == nil {
		LOG_WARNING(fmt.Sprintf("get protocol message failed"))
		return
	}

	if protocol.GetType() == waE2E.ProtocolMessage_MESSAGE_EDIT {
		// handle message edit
		editedMsg := protocol.GetEditedMessage()
		if editedMsg != nil {
			newMessageInfo := messageInfo
			newMessageInfo.ID = protocol.GetKey().GetId()
			handler.HandleMessage(newMessageInfo, editedMsg, isSyncRead)
		} else {
			LOG_WARNING(fmt.Sprintf("get edited message failed"))
		}
	} else if protocol.GetType() == waE2E.ProtocolMessage_REVOKE {
		// handle message revoke
		connId := handler.connId
		chatId := messageInfo.Chat.String()
		msgId := protocol.GetKey().GetId()
		LOG_TRACE(fmt.Sprintf("Call CWmDeleteMessageNotify %s %s", chatId, msgId))
		CWmDeleteMessageNotify(connId, chatId, msgId)
	} else {
		LOG_TRACE(fmt.Sprintf("ProtocolMessage %#v ignore", protocol.GetType()))
	}
}

func (handler *WmEventHandler) HandleUnsupportedMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	// list from type Message struct in def.pb.go
	msgType := "Unknown"
	msgNotify := false
	switch {
	case msg.ImageMessage != nil:
		msgType = "ImageMessage"

	case msg.ExtendedTextMessage != nil:
		msgType = "ExtendedTextMessage"

	case msg.DocumentMessage != nil:
		msgType = "DocumentMessage"

	case msg.AudioMessage != nil:
		msgType = "AudioMessage"

	case msg.VideoMessage != nil:
		msgType = "VideoMessage"

	case msg.StickerMessage != nil:
		msgType = "StickerMessage"

	case msg.SenderKeyDistributionMessage != nil:
		msgType = "SenderKeyDistributionMessage"

	case msg.ContactMessage != nil:
		msgType = "Contact"
		msgNotify = true

	case msg.LocationMessage != nil:
		msgType = "Location"
		msgNotify = true

	case msg.Call != nil:
		msgType = "Call"
		msgNotify = true

	case msg.Chat != nil:
		msgType = "Chat"

	case msg.ContactsArrayMessage != nil:
		msgType = "ContactsArrayMessage"

	case msg.HighlyStructuredMessage != nil:
		msgType = "HighlyStructuredMessage"

	case msg.FastRatchetKeySenderKeyDistributionMessage != nil:
		msgType = "FastRatchetKeySenderKeyDistributionMessage"

	case msg.SendPaymentMessage != nil:
		msgType = "SendPaymentMessage"

	case msg.LiveLocationMessage != nil:
		msgType = "LiveLocation"
		msgNotify = true

	case msg.RequestPaymentMessage != nil:
		msgType = "RequestPaymentMessage"

	case msg.DeclinePaymentRequestMessage != nil:
		msgType = "DeclinePaymentRequestMessage"

	case msg.CancelPaymentRequestMessage != nil:
		msgType = "CancelPaymentRequestMessage"

	case msg.GroupInviteMessage != nil:
		msgType = "GroupInviteMessage"

	case msg.TemplateButtonReplyMessage != nil:
		msgType = "TemplateButtonReplyMessage"

	case msg.ProductMessage != nil:
		msgType = "ProductMessage"

	case msg.DeviceSentMessage != nil:
		msgType = "DeviceSentMessage"

	case msg.MessageContextInfo != nil:
		msgType = "MessageContextInfo"

	case msg.ListMessage != nil:
		msgType = "ListMessage"

	case msg.ViewOnceMessage != nil:
		msgType = "ViewOnceMessage"

	case msg.OrderMessage != nil:
		msgType = "OrderMessage"

	case msg.ListResponseMessage != nil:
		msgType = "ListResponseMessage"

	case msg.EphemeralMessage != nil:
		msgType = "EphemeralMessage"

	case msg.InvoiceMessage != nil:
		msgType = "InvoiceMessage"

	case msg.ButtonsMessage != nil:
		msgType = "ButtonsMessage"

	case msg.ButtonsResponseMessage != nil:
		msgType = "ButtonsResponseMessage"

	case msg.PaymentInviteMessage != nil:
		msgType = "PaymentInviteMessage"

	case msg.InteractiveMessage != nil:
		msgType = "InteractiveMessage"

	case msg.ReactionMessage != nil:
		msgType = "ReactionMessage"

	case msg.StickerSyncRmrMessage != nil:
		msgType = "StickerSyncRmrMessage"

	case msg.InteractiveResponseMessage != nil:
		msgType = "InteractiveResponseMessage"

	case msg.PollCreationMessage != nil:
		msgType = "PollCreationMessage"

	case msg.PollUpdateMessage != nil:
		msgType = "PollUpdateMessage"

	case msg.KeepInChatMessage != nil:
		msgType = "KeepInChatMessage"

	case msg.DocumentWithCaptionMessage != nil:
		msgType = "DocumentWithCaptionMessage"

	case msg.RequestPhoneNumberMessage != nil:
		msgType = "RequestPhoneNumberMessage"

	case msg.ViewOnceMessageV2 != nil:
		msgType = "ViewOnceMessageV2"

	case msg.EncReactionMessage != nil:
		msgType = "EncReactionMessage"

	case msg.EditedMessage != nil:
		msgType = "EditedMessage"

	case msg.ViewOnceMessageV2Extension != nil:
		msgType = "ViewOnceMessageV2Extension"

	case msg.PollCreationMessageV2 != nil:
		msgType = "PollCreationMessageV2"

	case msg.ScheduledCallCreationMessage != nil:
		msgType = "ScheduledCallCreationMessage"

	case msg.GroupMentionedMessage != nil:
		msgType = "GroupMentionedMessage"

	case msg.PollCreationMessageV3 != nil:
		msgType = "PollCreationMessageV3"

	case msg.ScheduledCallEditMessage != nil:
		msgType = "ScheduledCallEditMessage"

	case msg.PtvMessage != nil:
		msgType = "PtvMessage"
	}

	if !msgNotify {
		LOG_TRACE(fmt.Sprintf("%s ignore", msgType))
		return
	} else {
		LOG_TRACE(fmt.Sprintf("%s notify", msgType))
	}

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// text
	text := "[" + msgType + "]"

	// context
	quotedId := ""

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	chatId := GetChatId(messageInfo.Chat, messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	selfId := JidToStr(*client.Store.ID)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	// reset typing if needed
	UpdateTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func UpdateTypingStatus(connId int, chatId string, userId string, fromMe bool, isSyncRead bool) {

	// ignore new messages from self and during initial sync
	if fromMe || isSyncRead {
		return
	}

	LOG_TRACE("update typing status " + strconv.Itoa(connId) + ", " + chatId + ", " + userId)

	// update
	isOnline := true
	isTyping := false

	LOG_TRACE(fmt.Sprintf("Call CWmNewStatusNotify"))
	CWmNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping), -1)
}

func WmInit(path string, proxy string, sendType int) int {

	LOG_DEBUG("init " + filepath.Base(path))

	// create tmp dir
	var tmpPath string = path + "/tmp"
	tmpErr := os.MkdirAll(tmpPath, os.ModePerm)
	if tmpErr != nil {
		LOG_WARNING(fmt.Sprintf("mkdir error %#v", tmpErr))
		return -1
	}

	var ncLogger logger.Loggable = &ncSignalLogger{}
	logger.Setup(&ncLogger)

	dbLog := NcLogger()
	sessionPath := path + "/session.db"
	sqlAddress := fmt.Sprintf("file:%s?_foreign_keys=on", sessionPath)
	container, sqlErr := sqlstore.New("sqlite3", sqlAddress, dbLog)
	if sqlErr != nil {
		LOG_WARNING(fmt.Sprintf("sqlite error %#v", sqlErr))
		return -1
	}

	deviceStore, devErr := container.GetFirstDevice()
	if devErr != nil {
		LOG_WARNING(fmt.Sprintf("dev store error %#v", devErr))
		return -1
	}

	store.DeviceProps.RequireFullSync = proto.Bool(true)
	store.DeviceProps.HistorySyncConfig = &waCompanionReg.DeviceProps_HistorySyncConfig{
		FullSyncDaysLimit:   proto.Uint32(3650),
		FullSyncSizeMbLimit: proto.Uint32(102400),
		StorageQuotaMb:      proto.Uint32(102400),
	}

	store.DeviceProps.PlatformType = waCompanionReg.DeviceProps_FIREFOX.Enum()
	switch runtime.GOOS {
	case "linux":
		store.DeviceProps.Os = proto.String("Linux")
	case "darwin":
		store.DeviceProps.Os = proto.String("Mac OS")
	default:
		store.DeviceProps.Os = proto.String("Linux")
	}

	// create new whatsapp connection
	clientLog := NcLogger()
	client := whatsmeow.NewClient(deviceStore, clientLog)
	if client == nil {
		LOG_WARNING("client error")
		return -1
	}

	// set proxy details
	if len(proxy) > 0 {
		client.SetProxyAddress(proxy)
	}

	// store connection and get id
	var connId int = AddConn(client, path, sendType)

	LOG_DEBUG("connId " + strconv.Itoa(connId))

	return connId
}

func WmLogin(connId int) int {

	LOG_DEBUG("login " + strconv.Itoa(connId) + " whatsmeow " + strconv.Itoa(whatsmeowDate))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get path and conn
	var path string = GetPath(connId)
	var cli *whatsmeow.Client = GetClient(connId)

	// authenticate if needed, otherwise just connect
	SetState(connId, Connecting)
	var timeoutMs int = 10000 // 10 sec timeout by default (regular connect)

	ch, err := cli.GetQRChannel(context.Background())
	if err != nil {
		if errors.Is(err, whatsmeow.ErrQRStoreContainsID) {
			// This error means that we're already logged in, so ignore it.
		} else {
			LOG_WARNING(fmt.Sprintf("failed to get qr channel %#v", err))
			SetState(connId, Disconnected)
		}
	} else {
		timeoutMs = 60000 // 60 sec timeout during setup / qr code scan
		go func() {
			hasGUI := HasGUI()

			LOG_TRACE(fmt.Sprintf("acquire console"))
			CWmSetProtocolUiControl(connId, 1)
			fmt.Printf("Scan the Qr code to authenticate, or press CTRL-C to abort.\n")

			for evt := range ch {
				if evt.Event == whatsmeow.QRChannelEventCode {
					if hasGUI {
						qrPath := path + "/tmp/qr.png"
						qrcode.WriteFile(evt.Code, qrcode.Medium, 512, qrPath)
						ShowImage(qrPath)
					} else {
						qrterminal.GenerateHalfBlock(evt.Code, qrterminal.L, os.Stdout)
					}
				} else if evt == whatsmeow.QRChannelSuccess {
					LOG_DEBUG("qr channel event success")
				} else if evt == whatsmeow.QRChannelClientOutdated {
					LOG_WARNING(fmt.Sprintf("qr channel result %#v", evt.Event))
					SetState(connId, Outdated)
				} else {
					LOG_WARNING(fmt.Sprintf("qr channel result %#v", evt.Event))
					SetState(connId, Disconnected)
				}
			}

			LOG_TRACE(fmt.Sprintf("release console"))
			CWmSetProtocolUiControl(connId, 0)
		}()
	}

	eventHandler := GetHandler(connId)
	cli.AddEventHandler(eventHandler.HandleEvent)
	err = cli.Connect()
	if err != nil {
		LOG_WARNING(fmt.Sprintf("failed to connect %#v", err))
		CWmClearStatus(FlagConnecting)
		return -1
	}

	LOG_DEBUG("connect ok")

	// wait for result (up to timeout, 100 ms at a time)
	LOG_DEBUG("wait start")
	waitedMs := 0
	for (waitedMs < timeoutMs) && (GetState(connId) == Connecting) {
		time.Sleep(100 * time.Millisecond)
		waitedMs += 100
	}
	LOG_DEBUG("wait done")

	// delete temporary image file
	_ = os.Remove(path + "/tmp/qr.png")

	// log error on stdout
	if GetState(connId) != Connected {
		LOG_WARNING(fmt.Sprintf("state not connected %#v", GetState(connId)))

		LOG_TRACE(fmt.Sprintf("acquire console"))
		CWmSetProtocolUiControl(connId, 1)

		fmt.Printf("\n")
		if GetState(connId) == Outdated {
			fmt.Printf("ERROR:\n")
			fmt.Printf("WhatsApp client is outdated, please update nchat to a newer version. See:\n")
			fmt.Printf("https://github.com/d99kris/nchat/blob/master/doc/WMOUTDATED.md\n")
		} else {
			fmt.Printf("ERROR:\n")
			fmt.Printf("Please see the log for details.\n")
		}
		fmt.Printf("\n")

		LOG_TRACE(fmt.Sprintf("release console"))
		CWmSetProtocolUiControl(connId, 0)

		CWmClearStatus(FlagConnecting)
		return -1
	}

	LOG_DEBUG("login ok")
	return 0
}

func WmLogout(connId int) int {

	LOG_DEBUG("logout " + strconv.Itoa(connId))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	var client *whatsmeow.Client = GetClient(connId)

	// disconnect
	client.Disconnect()

	// set state
	SetState(connId, Disconnected)

	LOG_DEBUG("logout ok")

	return 0
}

func WmCleanup(connId int) int {

	LOG_DEBUG("cleanup " + strconv.Itoa(connId))
	RemoveConn(connId)
	return 0
}

func WmGetVersion() int {
	return whatsmeowDate
}

func WmGetMessages(connId int, chatId string, limit int, fromMsgId string, owner int) int {
	// not supported in multi-device
	return -1
}

func WmSendMessage(connId int, chatId string, text string, quotedId string, quotedText string, quotedSender string, filePath string, fileType string, editMsgId string, editMsgSent int) int {

	LOG_TRACE("send message " + strconv.Itoa(connId) + ", " + chatId + ", " + text + ", " + quotedId + ", " + filePath + ", " + editMsgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	var client *whatsmeow.Client = GetClient(connId)

	// local vars
	var sendErr error
	var message waE2E.Message
	var sendResponse whatsmeow.SendResponse

	// recipient
	chatJid, jidErr := types.ParseJID(chatId)
	if jidErr != nil {
		LOG_WARNING(fmt.Sprintf("jid err %#v", jidErr))
		return -1
	}

	isSend := false

	// quote context
	contextInfo := waE2E.ContextInfo{}
	if len(quotedId) > 0 {
		quotedMessage := waE2E.Message{
			Conversation: &quotedText,
		}

		quotedSender = strings.Replace(quotedSender, "@c.us", "@s.whatsapp.net", 1)

		LOG_TRACE("send quoted " + quotedId + ", " + quotedText + ", " + quotedSender)
		contextInfo = waE2E.ContextInfo{
			QuotedMessage: &quotedMessage,
			StanzaID:      &quotedId,
			Participant:   &quotedSender,
		}
	}

	expiration := GetExpiration(connId, chatId)
	if expiration != 0 {
		LOG_TRACE("send expiration " + strconv.FormatUint(uint64(expiration), 10))
		contextInfo.Expiration = &expiration
	}

	// check message type
	if len(filePath) == 0 {

		// text message
		extendedTextMessage := waE2E.ExtendedTextMessage{
			Text:        &text,
			ContextInfo: &contextInfo,
		}

		message.ExtendedTextMessage = &extendedTextMessage

		isSend = true
	} else {

		var isSendType bool = IntToBool(GetSendType(connId))

		mimeType := strings.Split(fileType, "/")[0] // image, text, application, etc.
		if isSendType && (mimeType == "audio") {
			LOG_TRACE("send audio " + fileType)

			data, err := os.ReadFile(filePath)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("read file %s err %#v", filePath, err))
				return -1
			}

			uploaded, upErr := client.Upload(context.Background(), data, whatsmeow.MediaAudio)
			if upErr != nil {
				LOG_WARNING(fmt.Sprintf("upload error %#v", upErr))
				return -1
			}

			audioMessage := waE2E.AudioMessage{
				URL:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSHA256: uploaded.FileEncSHA256,
				FileSHA256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
				ContextInfo:   &contextInfo,
			}

			message.AudioMessage = &audioMessage

			isSend = true
		} else if isSendType && (mimeType == "video") {
			LOG_TRACE("send video " + fileType)

			data, err := os.ReadFile(filePath)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("read file %s err %#v", filePath, err))
				return -1
			}

			uploaded, upErr := client.Upload(context.Background(), data, whatsmeow.MediaVideo)
			if upErr != nil {
				LOG_WARNING(fmt.Sprintf("upload error %#v", upErr))
				return -1
			}

			videoMessage := waE2E.VideoMessage{
				Caption:       proto.String(text),
				URL:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSHA256: uploaded.FileEncSHA256,
				FileSHA256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
				ContextInfo:   &contextInfo,
			}

			message.VideoMessage = &videoMessage

			isSend = true
		} else if isSendType && (mimeType == "image") {
			LOG_TRACE("send image " + fileType)

			data, err := os.ReadFile(filePath)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("read file %s err %#v", filePath, err))
				return -1
			}

			uploaded, upErr := client.Upload(context.Background(), data, whatsmeow.MediaImage)
			if upErr != nil {
				LOG_WARNING(fmt.Sprintf("upload error %#v", upErr))
				return -1
			}

			imageMessage := waE2E.ImageMessage{
				Caption:       proto.String(text),
				URL:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSHA256: uploaded.FileEncSHA256,
				FileSHA256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
				ContextInfo:   &contextInfo,
			}

			message.ImageMessage = &imageMessage

			isSend = true
		} else {
			LOG_TRACE("send document " + fileType)

			data, err := os.ReadFile(filePath)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("read file %s err %#v", filePath, err))
				return -1
			}

			uploaded, upErr := client.Upload(context.Background(), data, whatsmeow.MediaDocument)
			if upErr != nil {
				LOG_WARNING(fmt.Sprintf("upload error %#v", upErr))
				return -1
			}

			fileName := filepath.Base(filePath)

			documentMessage := waE2E.DocumentMessage{
				Caption:       proto.String(text),
				URL:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSHA256: uploaded.FileEncSHA256,
				FileSHA256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
				FileName:      proto.String(fileName),
				ContextInfo:   &contextInfo,
			}

			message.DocumentMessage = &documentMessage

			isSend = true
		}
	}

	if isSend {

		if len(editMsgId) > 0 {
			// edit message
			sendResponse, sendErr =
				client.SendMessage(context.Background(), chatJid, client.BuildEdit(chatJid, editMsgId, &message))

		} else {
			// send message
			sendResponse, sendErr = client.SendMessage(context.Background(), chatJid, &message)

		}
	}

	// log any error
	if sendErr != nil {
		LOG_WARNING(fmt.Sprintf("send message error %#v", sendErr))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("send message ok"))

		// messageInfo
		var messageInfo types.MessageInfo
		messageInfo.Chat = chatJid
		messageInfo.IsFromMe = true
		messageInfo.Sender = *client.Store.ID

		if len(editMsgId) > 0 {
			messageInfo.ID = editMsgId
			messageInfo.Timestamp = time.Unix(int64(editMsgSent), 0)
		} else {
			messageInfo.ID = sendResponse.ID
			messageInfo.Timestamp = sendResponse.Timestamp
		}

		isSyncRead := false
		handler := GetHandler(connId)
		handler.HandleMessage(messageInfo, &message, isSyncRead)
	}

	return 0
}

func WmGetStatus(connId int, userId string) int {

	LOG_TRACE("get status " + strconv.Itoa(connId) + ", " + userId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// get user
	userJid, _ := types.ParseJID(userId)
	if userJid.Server == types.GroupServer {
		// ignore presence requests for groups
		return -1
	}

	// subscribe user presence
	err := client.SubscribePresence(userJid)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("get user status error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("get user status ok"))
	}

	return 0
}

func WmMarkMessageRead(connId int, chatId string, senderId string, msgId string) int {

	LOG_TRACE("mark message read " + strconv.Itoa(connId) + ", " + chatId + ", " + senderId + ", " + msgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// mark read
	msgIds := []types.MessageID{
		msgId,
	}
	timeRead := time.Now()
	chatJid, _ := types.ParseJID(chatId)
	senderJid, _ := types.ParseJID(senderId)
	err := client.MarkRead(msgIds, timeRead, chatJid, senderJid)

	// store time
	SetTimeRead(connId, chatId, timeRead)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("mark message read error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("mark message read ok %#v", msgId))
	}

	return 0
}

func WmDeleteMessage(connId int, chatId string, senderId string, msgId string) int {

	LOG_TRACE("delete message " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	chatJid, _ := types.ParseJID(chatId)
	senderJid, _ := types.ParseJID(senderId)

	// skip deleting messages sent by others in private chat
	selfId := JidToStr(*client.Store.ID)
	isGroup := (chatJid.Server == types.GroupServer)
	isFromSelf := (senderId == selfId)
	if !isFromSelf && !isGroup {
		LOG_TRACE(fmt.Sprintf("delete message isGroup %t isFromSelf %t skip %#v",
			isGroup, isFromSelf, msgId))
		return -1
	}

	// delete message
	_, err := client.SendMessage(context.Background(), chatJid, client.BuildRevoke(chatJid, senderJid, msgId),
		whatsmeow.SendRequestExtra{Peer: false, Timeout: 3 * time.Second})

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("delete message error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("delete message ok %#v", msgId))
	}

	return 0
}

func WmDeleteChat(connId int, chatId string) int {

	LOG_TRACE("delete chat " + strconv.Itoa(connId) + ", " + chatId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// get chat jid
	chatJid, _ := types.ParseJID(chatId)

	// leave / delete
	if chatJid.Server == types.GroupServer {
		// if group, exit it
		err := client.LeaveGroup(chatJid)

		// log any error
		if err != nil {
			LOG_WARNING(fmt.Sprintf("leave group error %s %#v", chatId, err))
			return -1
		} else {
			LOG_TRACE(fmt.Sprintf("leave group ok (but not deleted) %s", chatId))
		}
	} else {
		// if private, log warning (function not supported by underlying library)
		LOG_WARNING(fmt.Sprintf("delete chat not supported %s", chatId))
	}

	return 0
}

func WmSendTyping(connId int, chatId string, isTyping int) int {

	LOG_TRACE("send typing " + strconv.Itoa(connId) + ", " + chatId + ", " + strconv.Itoa(isTyping))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// do not send typing to self chat
	selfId := JidToStr(*client.Store.ID)
	if chatId == selfId {
		return 0
	}

	// set presence
	var chatPresence types.ChatPresence = types.ChatPresencePaused
	if isTyping == 1 {
		chatPresence = types.ChatPresenceComposing
	}

	var chatPresenceMedia types.ChatPresenceMedia = types.ChatPresenceMediaText
	chatJid, _ := types.ParseJID(chatId)
	err := client.SendChatPresence(chatJid, chatPresence, chatPresenceMedia)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("send typing error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("send typing ok"))
	}

	return 0
}

func WmSendStatus(connId int, isOnline int) int {

	LOG_TRACE("send status " + strconv.Itoa(connId) + ", " + strconv.Itoa(isOnline))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// bail out if no push name
	if len(client.Store.PushName) == 0 {
		LOG_WARNING("tmp")
		return -1
	}

	// set presence
	var presence types.Presence = types.PresenceUnavailable
	if isOnline == 1 {
		presence = types.PresenceAvailable
	}

	err := client.SendPresence(presence)
	if err != nil {
		LOG_WARNING("Failed to send presence")
	} else {
		LOG_TRACE("Sent presence ok")
		if isOnline == 1 {
			CWmClearStatus(FlagAway)
		} else {
			CWmSetStatus(FlagAway)
		}
	}

	return 0
}

func WmDownloadFile(connId int, chatId string, msgId string, fileId string, action int) int {

	LOG_TRACE("download file " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId + ", " + fileId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// download file
	filePath, fileStatus := DownloadFromFileId(client, fileId)

	// notify result
	CWmNewMessageFileNotify(connId, chatId, msgId, filePath, fileStatus, action)

	return 0
}

func WmSendReaction(connId int, chatId string, senderId string, msgId string, emoji string) int {

	LOG_TRACE("send reaction " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId + ", \"" + emoji + "\"")

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// send reaction
	chatJid, _ := types.ParseJID(chatId)
	senderJid, _ := types.ParseJID(senderId)
	_, sendErr :=
		client.SendMessage(context.Background(), chatJid, client.BuildReaction(chatJid, senderJid, msgId, emoji))

	if sendErr != nil {
		LOG_WARNING(fmt.Sprintf("send reaction error %#v", sendErr))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("send reaction ok"))
		fromMe := true //messageInfo.IsFromMe
		CWmNewMessageReactionNotify(connId, chatId, msgId, senderId, emoji, BoolToInt(fromMe))
	}

	return 0
}
