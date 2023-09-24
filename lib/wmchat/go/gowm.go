// gowm.go
//
// Copyright (c) 2021-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"mime"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"google.golang.org/protobuf/proto"

	waProto "go.mau.fi/whatsmeow/binary/proto"
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

var whatsmeowDate = "20230916"

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
)

var (
	mx         sync.Mutex
	clients    map[int]*whatsmeow.Client = make(map[int]*whatsmeow.Client)
	paths      map[int]string            = make(map[int]string)
	contacts   map[int]map[string]string = make(map[int]map[string]string)
	states     map[int]State             = make(map[int]State)
	timeUnread map[intString]int         = make(map[intString]int)
	handlers   map[int]*WmEventHandler   = make(map[int]*WmEventHandler)
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
var FlagOnline = (1 << 1)
var FlagFetching = (1 << 2)
var FlagSending = (1 << 3)
var FlagUpdating = (1 << 4)
var FlagSyncing = (1 << 5)
var FlagMax = FlagSyncing

func AddConn(conn *whatsmeow.Client, path string) int {
	mx.Lock()
	var connId int = len(clients)
	clients[connId] = conn
	paths[connId] = path
	contacts[connId] = make(map[string]string)
	states[connId] = None
	handlers[connId] = &WmEventHandler{connId}
	mx.Unlock()
	return connId
}

func RemoveConn(connId int) {
	mx.Lock()
	delete(clients, connId)
	delete(paths, connId)
	delete(contacts, connId)
	delete(states, connId)
	delete(handlers, connId)
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
	info.FileEncSha256 = msg.GetFileEncSha256()
	info.FileSha256 = msg.GetFileSha256()

	info.MediaType = whatsmeow.GetMediaType(msg)
	if len(info.MediaType) == 0 {
		LOG_WARNING(fmt.Sprintf("unknown mediatype '%s'", string(msg.ProtoReflect().Descriptor().Name())))
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
		LOG_WARNING("unsupported os")
	}
}

func HasGUI() bool {
	switch runtime.GOOS {
	case "linux":
		_, displaySet := os.LookupEnv("DISPLAY")
		return displaySet
	case "darwin":
		return true
	default:
		return true
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

func ParseWebMessageInfo(selfJid types.JID, chatJid types.JID, webMsg *waProto.WebMessageInfo) *types.MessageInfo {
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
		CWmClearStatus(FlagOffline)

	case *events.Disconnected:
		// disconnected
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		CWmSetStatus(FlagOffline)
		CWmClearStatus(FlagOnline)

	case *events.StreamReplaced:
		// TODO: find out when exactly this happens and how to handle it
		LOG_TRACE(fmt.Sprintf("%#v", evt))

	case *events.Message:
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		handler.HandleMessage(evt.Info, evt.Message, false)

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
		// logged out, need re-init?
		LOG_TRACE(fmt.Sprintf("%#v", evt))

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
		chatId := receipt.SourceString()
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

	LOG_TRACE(fmt.Sprintf("HandleHistorySync SyncType %#v", *historySync.Data.SyncType))

	CWmSetStatus(FlagSyncing)

	pushnames := historySync.Data.GetPushnames()
	for _, pushname := range pushnames {
		if pushname.Id != nil && pushname.Pushname != nil {
			LOG_TRACE(fmt.Sprintf("HandleHistorySync Pushname %s %s", *pushname.Id, *pushname.Pushname))
		}
	}

	conversations := historySync.Data.GetConversations()
	for _, conversation := range conversations {
		LOG_TRACE(fmt.Sprintf("HandleHistorySync Conversation %#v", *conversation))

		chatJid, _ := types.ParseJID(conversation.GetId())

		isUnread := 0
		isMuted := 0
		lastMessageTime := 0

		hasMessages := false
		syncMessages := conversation.GetMessages()
		for _, syncMessage := range syncMessages {
			webMessageInfo := syncMessage.Message
			messageInfo := ParseWebMessageInfo(selfJid, chatJid, webMessageInfo)
			message := webMessageInfo.GetMessage()

			if (messageInfo == nil) || (message == nil) {
				continue
			}

			handler.HandleMessage(*messageInfo, message, true)
			hasMessages = true
		}

		if (hasMessages) {
			LOG_TRACE(fmt.Sprintf("Call CWmNewChatsNotify %s %d", JidToStr(chatJid), len(syncMessages)))
			CWmNewChatsNotify(handler.connId, JidToStr(chatJid), isUnread, isMuted, lastMessageTime)
		} else {
			LOG_TRACE(fmt.Sprintf("Skip CWmNewChatsNotify %s %d", JidToStr(chatJid), len(syncMessages)))
		}

	}

	CWmClearStatus(FlagSyncing)
}

func (handler *WmEventHandler) HandleGroupInfo(groupInfo *events.GroupInfo) {
	connId := handler.connId
	client := GetClient(connId)
	chatId := JidToStr(groupInfo.JID)

	quotedId := ""
	selfJid := *client.Store.ID
	senderJidStr := ""
	if ((groupInfo.Sender != nil) && (JidToStr(*groupInfo.Sender) != JidToStr(groupInfo.JID))) {
		senderJidStr = JidToStr(*groupInfo.Sender)
	}

	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	text := ""
	if (groupInfo.Name != nil) {
		// Group name change
		if (senderJidStr == "") {
			senderJidStr = JidToStr(groupInfo.JID)
		}

		groupName := *groupInfo.Name
		text = "[Changed group name to " + groupName.Name + "]"
	} else if (len(groupInfo.Join) > 0) {
		// Group member joined
		if ((len(groupInfo.Join) == 1) && ((senderJidStr == "") || (senderJidStr == JidToStr(groupInfo.Join[0])))) {
			senderJidStr = JidToStr(groupInfo.Join[0])
			text = "[Joined]"
		} else {
			if (senderJidStr == "") {
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
	} else if (len(groupInfo.Leave) > 0) {
		// Group member left
		if ((len(groupInfo.Leave) == 1) && ((senderJidStr == "") || (senderJidStr == JidToStr(groupInfo.Leave[0])))) {
			senderJidStr = JidToStr(groupInfo.Leave[0])
			text = "[Left]"
		} else {
			if (senderJidStr == "") {
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

	fromMe := (senderJidStr == JidToStr(selfJid))
	senderId := senderJidStr

	timeSent := int(groupInfo.Timestamp.Unix())
	isSeen := false
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	msgId := strconv.Itoa(timeSent) // group info updates do not have msg id

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
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
				LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", userId, name))
				CWmNewContactsNotify(connId, userId, name, BoolToInt(false))
				AddContactName(connId, userId, name)
			} else {
				LOG_WARNING(fmt.Sprintf("Skip CWmNewContactsNotify %s %#v", JidToStr(jid), contactInfo))
			}
		}
	}

	// special handling for self
	selfId := JidToStr(*client.Store.ID)
	selfName := "" // overridden by ui
	LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", selfId, selfName))
	CWmNewContactsNotify(connId, selfId, selfName, BoolToInt(true))
	AddContactName(connId, selfId, selfName)

	// special handling for official whatsapp account
	whatsappId := "0@s.whatsapp.net"
	whatsappName := "WhatsApp"
	LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", whatsappId, whatsappName))
	CWmNewContactsNotify(connId, whatsappId, whatsappName, BoolToInt(false))
	AddContactName(connId, whatsappId, whatsappName)

	// groups
	groups, groupErr := client.GetJoinedGroups()
	if groupErr != nil {
		LOG_WARNING(fmt.Sprintf("get joined groups failed %#v", groupErr))
	} else {
		LOG_TRACE(fmt.Sprintf("groups %#v", groups))
		for _, group := range groups {
			groupId := JidToStr(group.JID)
			groupName := group.GroupName.Name
			LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", groupId, groupName))
			CWmNewContactsNotify(connId, groupId, groupName, BoolToInt(false))
			AddContactName(connId, groupId, groupName)
		}
	}

	CWmClearStatus(FlagFetching)
}

func (handler *WmEventHandler) HandleMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
	switch {
	case msg.Conversation != nil || msg.ExtendedTextMessage != nil:
		handler.HandleTextMessage(messageInfo, msg, isSync)

	case msg.ImageMessage != nil:
		handler.HandleImageMessage(messageInfo, msg, isSync)

	case msg.VideoMessage != nil:
		handler.HandleVideoMessage(messageInfo, msg, isSync)

	case msg.AudioMessage != nil:
		handler.HandleAudioMessage(messageInfo, msg, isSync)

	case msg.DocumentMessage != nil:
		handler.HandleDocumentMessage(messageInfo, msg, isSync)

	case msg.StickerMessage != nil:
		handler.HandleStickerMessage(messageInfo, msg, isSync)

	case msg.TemplateMessage != nil:
		handler.HandleTemplateMessage(messageInfo, msg, isSync)

	default:
		handler.HandleUnsupportedMessage(messageInfo, msg, isSync)
	}
}

func (handler *WmEventHandler) HandleTextMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
	LOG_TRACE(fmt.Sprintf("TextMessage"))

	connId := handler.connId
	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	text := ""

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

	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleImageMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
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
	ext := "jpg"
	exts, extErr := mime.ExtensionsByType(img.GetMimetype())
	if extErr != nil && len(exts) > 0 {
		ext = exts[0]
	}

	// context
	quotedId := ""
	ci := img.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// get temp file path
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s.%s", tmpPath, messageInfo.ID, ext)

	// file id and status
	fileId := DownloadableMessageToFileId(client, img, filePath)
	fileStatus := FileStatusNotDownloaded

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := img.GetCaption()

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleVideoMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
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
	ext := "mp4"
	exts, extErr := mime.ExtensionsByType(vid.GetMimetype())
	if extErr != nil && len(exts) > 0 {
		ext = exts[0]
	}

	// context
	quotedId := ""
	ci := vid.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// get temp file path
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s.%s", tmpPath, messageInfo.ID, ext)

	// file id and status
	fileId := DownloadableMessageToFileId(client, vid, filePath)
	fileStatus := FileStatusNotDownloaded

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := vid.GetCaption()

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleAudioMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
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
	ext := "ogg"
	exts, extErr := mime.ExtensionsByType(aud.GetMimetype())
	if extErr != nil && len(exts) > 0 {
		ext = exts[0]
	}

	// context
	quotedId := ""
	ci := aud.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// get temp file path
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s.%s", tmpPath, messageInfo.ID, ext)

	// file id and status
	fileId := DownloadableMessageToFileId(client, aud, filePath)
	fileStatus := FileStatusNotDownloaded

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := ""

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleDocumentMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
	LOG_TRACE(fmt.Sprintf("DocumentMessage"))

	connId := handler.connId
	var client *whatsmeow.Client = GetClient(handler.connId)

	// get doc part
	doc := msg.GetDocumentMessage()
	if doc == nil {
		LOG_WARNING(fmt.Sprintf("get document message failed"))
		return
	}

	// context
	quotedId := ""
	ci := doc.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// get temp file path
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s-%s", tmpPath, messageInfo.ID, *doc.FileName)

	// file id and status
	fileId := DownloadableMessageToFileId(client, doc, filePath)
	fileStatus := FileStatusNotDownloaded

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := ""

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleStickerMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
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
	ext := "webp"
	exts, extErr := mime.ExtensionsByType(sticker.GetMimetype())
	if extErr != nil && len(exts) > 0 {
		ext = exts[0]
	}

	// context
	quotedId := ""
	ci := sticker.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	// get temp file path
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s.%s", tmpPath, messageInfo.ID, ext)

	// file id and status
	fileId := DownloadableMessageToFileId(client, sticker, filePath)
	fileStatus := FileStatusNotDownloaded

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	text := "[Sticker]"

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleTemplateMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
	LOG_TRACE(fmt.Sprintf("TemplateMessage"))

	connId := handler.connId

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
	case *waProto.TemplateMessage_HydratedFourRowTemplate_DocumentMessage:
		texts = append(texts, "[Document]")
	case *waProto.TemplateMessage_HydratedFourRowTemplate_ImageMessage:
		texts = append(texts, "[Image]")
	case *waProto.TemplateMessage_HydratedFourRowTemplate_VideoMessage:
		texts = append(texts, "[Video]")
	case *waProto.TemplateMessage_HydratedFourRowTemplate_LocationMessage:
		texts = append(texts, "[Location]")
	case *waProto.TemplateMessage_HydratedFourRowTemplate_HydratedTitleText:
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
		case *waProto.HydratedTemplateButton_QuickReplyButton:
			texts = append(texts, fmt.Sprintf("%s", hydbutton.QuickReplyButton.GetDisplayText()))
		case *waProto.HydratedTemplateButton_UrlButton:
			texts = append(texts, fmt.Sprintf("%s: %s", hydbutton.UrlButton.GetDisplayText(), hydbutton.UrlButton.GetUrl()))
		case *waProto.HydratedTemplateButton_CallButton:
			texts = append(texts, fmt.Sprintf("%s: %s", hydbutton.CallButton.GetDisplayText(), hydbutton.CallButton.GetPhoneNumber()))
		}
	}

	// footer
	footer := hydtpl.GetHydratedFooterText()
	if footer != "" {
		texts = append(texts, footer)
	}

	// combined text
	text := strings.Join(texts, "\n")

	// context
	quotedId := ""
	ci := tpl.GetContextInfo()
	if ci != nil {
		quotedId = ci.GetStanzaId()
	}

	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func (handler *WmEventHandler) HandleUnsupportedMessage(messageInfo types.MessageInfo, msg *waProto.Message, isSync bool) {
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

	case msg.ProtocolMessage != nil:
		msgType = "ProtocolMessage"

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
	chatId := JidToStr(messageInfo.Chat)
	msgId := messageInfo.ID
	text := "[" + msgType + "]"

	quotedId := ""
	fromMe := messageInfo.IsFromMe
	senderId := JidToStr(messageInfo.Sender)
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	timeSent := int(messageInfo.Timestamp.Unix())
	isSeen := isSync
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent, BoolToInt(isRead))
}

func UpdateTypingStatus(connId int, chatId string, userId string, fromMe bool, isOld bool) {

	// only handle new messages from others
	if fromMe || isOld {
		return
	}

	LOG_TRACE("update typing status " + strconv.Itoa(connId) + ", " + chatId + ", " + userId)

	// update
	isOnline := true
	isTyping := false

	LOG_TRACE(fmt.Sprintf("Call CWmNewStatusNotify"))
	CWmNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping), -1)
}

func WmInit(path string, proxy string) int {

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

	requireFullSync := true
	store.DeviceProps.RequireFullSync = &requireFullSync
	store.DeviceProps.PlatformType = waProto.DeviceProps_FIREFOX.Enum()
	switch runtime.GOOS {
	case "linux":
		store.DeviceProps.Os = proto.String("Linux")
	case "darwin":
		store.DeviceProps.Os = proto.String("Mac OS")
	default:
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
	var connId int = AddConn(client, path)

	LOG_DEBUG("connId " + strconv.Itoa(connId))

	return connId
}

func WmLogin(connId int) int {

	LOG_DEBUG("login " + strconv.Itoa(connId) + " whatsmeow " + whatsmeowDate)

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
		// This error means that we're already logged in, so ignore it.
		if !errors.Is(err, whatsmeow.ErrQRStoreContainsID) {
			LOG_WARNING(fmt.Sprintf("failed to get qr channel %#v", err))
		}
	} else {
		timeoutMs = 60000 // 60 sec timeout during setup / qr code scan
		go func() {
			for evt := range ch {
				if evt.Event == "code" {
					if HasGUI() {
						qrPath := path + "/tmp/qr.png"
						qrcode.WriteFile(evt.Code, qrcode.Medium, 512, qrPath)
						ShowImage(qrPath)
					} else {
						qrterminal.GenerateHalfBlock(evt.Code, qrterminal.L, os.Stdout)
					}
				} else {
					LOG_WARNING(fmt.Sprintf("qr channel result %#v", evt.Event))
				}
			}
		}()
	}

	eventHandler := GetHandler(connId)
	cli.AddEventHandler(eventHandler.HandleEvent)
	err = cli.Connect()
	if err != nil {
		LOG_WARNING(fmt.Sprintf("failed to connect %#v", err))
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

	if GetState(connId) != Connected {
		LOG_WARNING(fmt.Sprintf("state not connected %#v", GetState(connId)))
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

func WmGetMessages(connId int, chatId string, limit int, fromMsgId string, owner int) int {
	// not supported in multi-device
	return -1
}

func WmSendMessage(connId int, chatId string, text string, quotedId string, quotedText string, quotedSender string, filePath string, fileType string, editMsgId string, editMsgSent int) int {

	LOG_TRACE("send message " + strconv.Itoa(connId) + ", " + chatId + ", " + text)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	var client *whatsmeow.Client = GetClient(connId)

	// local vars
	var sendErr error
	var message waProto.Message
	var sendResponse whatsmeow.SendResponse

	// recipient
	chatJid, jidErr := types.ParseJID(chatId)
	if jidErr != nil {
		LOG_WARNING(fmt.Sprintf("jid err %#v", jidErr))
		return -1
	}

	isSend := false

	// check message type
	if len(filePath) == 0 {

		// text message

		if len(quotedId) > 0 {
			contextInfo := waProto.ContextInfo{}
			quotedMessage := waProto.Message{
				Conversation: &quotedText,
			}

			selfId := JidToStr(*client.Store.ID)
			if quotedSender == selfId {
				quotedSender = chatId
			}

			quotedSender = strings.Replace(quotedSender, "@c.us", "@s.whatsapp.net", 1)

			LOG_TRACE("send quoted " + quotedId + ", " + quotedText + ", " + quotedSender)
			contextInfo = waProto.ContextInfo{
				QuotedMessage: &quotedMessage,
				StanzaId:      &quotedId,
				Participant:   &quotedSender,
			}

			extendedTextMessage := waProto.ExtendedTextMessage{
				Text:        &text,
				ContextInfo: &contextInfo,
			}

			message.ExtendedTextMessage = &extendedTextMessage
		} else {
			message.Conversation = &text
		}

		isSend = true
	} else {

		mimeType := strings.Split(fileType, "/")[0] // image, text, application, etc.
		if mimeType == "image" {

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

			imageMessage := waProto.ImageMessage{
				Caption:       proto.String(text),
				Url:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSha256: uploaded.FileEncSHA256,
				FileSha256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
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

			documentMessage := waProto.DocumentMessage{
				Url:           proto.String(uploaded.URL),
				DirectPath:    proto.String(uploaded.DirectPath),
				MediaKey:      uploaded.MediaKey,
				Mimetype:      proto.String(fileType),
				FileEncSha256: uploaded.FileEncSHA256,
				FileSha256:    uploaded.FileSHA256,
				FileLength:    proto.Uint64(uint64(len(data))),
				FileName:      proto.String(fileName),
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

		handler := GetHandler(connId)
		handler.HandleMessage(messageInfo, &message, false)
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

func WmMarkMessageRead(connId int, chatId string, msgId string) int {

	LOG_TRACE("mark message read " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

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
	timeNow := time.Now()
	selfJid := *client.Store.ID
	chatJid, _ := types.ParseJID(chatId)
	err := client.MarkRead(msgIds, timeNow, chatJid, selfJid)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("mark message read error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("mark message read ok %#v", msgId))
	}

	return 0
}

func WmDeleteMessage(connId int, chatId string, msgId string) int {

	LOG_TRACE("delete message " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// delete message
	chatJid, _ := types.ParseJID(chatId)
	_, err := client.RevokeMessage(chatJid, msgId)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("delete message error %#v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("delete message ok %#v", msgId))
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
