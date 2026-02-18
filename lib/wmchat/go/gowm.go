// gowm.go
//
// Copyright (c) 2021-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"context"
	"encoding/gob"
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
	"runtime/debug"
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

var whatsmeowDate int = 20260116

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
	senders     map[int]map[string]string    = make(map[int]map[string]string)
	states      map[int]State                = make(map[int]State)
	timeReads   map[int]map[string]time.Time = make(map[int]map[string]time.Time)
	expirations map[int]map[string]uint32    = make(map[int]map[string]uint32)
	handlers    map[int]*WmEventHandler      = make(map[int]*WmEventHandler)
	sendTypes   map[int]int                  = make(map[int]int)
	namesSynced map[int]bool                 = make(map[int]bool)
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

// keep in sync with enum NotifyType in wmchat.cpp
var NotifyDirect = 0
var NotifyCache = 1
var NotifySendCached = 2

func SaveMap(path string, m map[string]string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return gob.NewEncoder(f).Encode(m)
}

func LoadMap(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return make(map[string]string), err
	}
	defer f.Close()
	var m map[string]string
	if err := gob.NewDecoder(f).Decode(&m); err != nil {
		return make(map[string]string), err
	}
	return m, nil
}

func GetContactsStorePath(connPath string) string {
	return connPath + "/contacts.dat"
}

func GetSendersStorePath(connPath string) string {
	return connPath + "/senders.dat"
}

func AddConn(conn *whatsmeow.Client, path string, sendType int) int {
	mx.Lock()
	var connId int = len(clients)
	clients[connId] = conn
	paths[connId] = path
	contacts[connId], _ = LoadMap(GetContactsStorePath(path))
	senders[connId], _ = LoadMap(GetSendersStorePath(path))
	states[connId] = None
	timeReads[connId] = make(map[string]time.Time)
	expirations[connId] = make(map[string]uint32)
	handlers[connId] = &WmEventHandler{connId}
	sendTypes[connId] = sendType
	namesSynced[connId] = false
	mx.Unlock()
	return connId
}

func RemoveConn(connId int) {
	mx.Lock()
	SaveMap(GetContactsStorePath(paths[connId]), contacts[connId])
	SaveMap(GetSendersStorePath(paths[connId]), senders[connId])
	delete(clients, connId)
	delete(paths, connId)
	delete(contacts, connId)
	delete(senders, connId)
	delete(states, connId)
	delete(timeReads, connId)
	delete(expirations, connId)
	delete(handlers, connId)
	delete(sendTypes, connId)
	delete(namesSynced, connId)
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

func GetNamesSynced(connId int) bool {
	mx.Lock()
	var isNamesSynced bool = namesSynced[connId]
	mx.Unlock()
	return isNamesSynced
}

func SetNamesSynced(connId int, synced bool) {
	mx.Lock()
	namesSynced[connId] = synced
	mx.Unlock()
}

func HasContact(connId int, id string) bool {
	var ok bool
	mx.Lock()
	_, ok = contacts[connId][id]
	mx.Unlock()
	return ok
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

func HasSender(connId int, id string) bool {
	var ok bool
	mx.Lock()
	_, ok = senders[connId][id]
	mx.Unlock()
	return ok
}

func AddSender(connId int, id string, name string) {
	mx.Lock()
	senders[connId][id] = name
	mx.Unlock()
}

func GetAllSenders(connId int) map[string]string {
	allSenders := make(map[string]string)
	mx.Lock()
	allSenders = senders[connId]
	mx.Unlock()
	return allSenders
}

func RemoveSender(connId int, id string) {
	mx.Lock()
	delete(senders[connId], id)
	mx.Unlock()
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

func DownloadFromFileId(connId int, fileId string) (string, int) {
	LOG_TRACE(fmt.Sprintf("fileId %s", fileId))
	var info DownloadInfo
	json.Unmarshal([]byte(fileId), &info)
	if info.Version != downloadInfoVersion {
		LOG_WARNING(fmt.Sprintf("unsupported version %d", info.Version))
		return "", FileStatusDownloadFailed
	}

	LOG_TRACE(fmt.Sprintf("fileInfo %#v", info))

	// get client
	client := GetClient(connId)

	targetPath := info.TargetPath
	filePath := ""
	fileStatus := FileStatusNone

	// download if not yet present
	if _, statErr := os.Stat(targetPath); os.IsNotExist(statErr) {
		LOG_TRACE(fmt.Sprintf("download new %#v", targetPath))
		CWmSetStatus(connId, FlagFetching)

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
		CWmClearStatus(connId, FlagFetching)
	} else {
		LOG_TRACE(fmt.Sprintf("download cached %#v", targetPath))
		filePath = targetPath
		fileStatus = FileStatusDownloaded
	}

	return filePath, fileStatus
}

func DownloadFromFileInfo(client *whatsmeow.Client, info DownloadInfo) ([]byte, error) {
	ctx := context.TODO()
	if len(info.Url) > 0 {
		LOG_TRACE(fmt.Sprintf("download url: %s", info.Url))
		return client.DownloadMediaWithUrl(ctx, info.Url, info.MediaKey, info.MediaType, info.Size, info.FileEncSha256, info.FileSha256)
	} else if len(info.DirectPath) > 0 {
		LOG_TRACE(fmt.Sprintf("download directpath: %s", info.DirectPath))
		return client.DownloadMediaWithPath(ctx, info.DirectPath, info.FileEncSha256, info.FileSha256, info.MediaKey, info.Size, info.MediaType, whatsmeow.GetMMSType(info.MediaType))
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

func GetOSName() string {
	switch runtime.GOOS {
	case "linux":
		return "Linux"
	case "darwin":
		return "Mac OS"
	default:
		return "Linux"
	}
}

func GetClientDisplayName() string {
	return "Firefox (" + GetOSName() + ")"
}

func GetPhoneNumberFromPath(path string) string {
	profileDirName := filepath.Base(filepath.Clean(path)) // e.g. "WhatsAppMd_+6511111111"
	lastUnderscoreIndex := strings.LastIndex(profileDirName, "_")
	if lastUnderscoreIndex != -1 && lastUnderscoreIndex+1 < len(profileDirName) {
		return profileDirName[lastUnderscoreIndex+1:]
	}
	return ""
}

func GetConfigOrEnvFlag(envVarName string) bool {
	configParamName := strings.ToLower(envVarName)
	isConfigSet := CWmAppConfigGetNum(configParamName)
	if IntToBool(isConfigSet) {
		return true
	}

	_, isEnvSet := os.LookupEnv(envVarName)
	if isEnvSet {
		CWmAppConfigSetNum(configParamName, 1)
		return true
	}

	return false
}

func HasGUI() bool {
	useQrTerminal := GetConfigOrEnvFlag("USE_QR_TERMINAL")
	if useQrTerminal {
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

func SanitizeName(text string) string {
	newText := strings.ReplaceAll(text, "\n", " ")
	if newText != text {
		LOG_DEBUG(fmt.Sprintf("sanitized '%s' -> '%s'", text, newText))
	}
	return newText
}

// Convert Jid to string without any mapping, use with care!
func StrFromJid(jid types.JID) string {
	return jid.User + "@" + jid.Server
}

// Check if self id
func IsSelfId(client *whatsmeow.Client, userId string) bool {
	return (StrFromJid(*client.Store.ID) == userId) || (StrFromJid(client.Store.LID) == userId)
}

// Get self id - @todo: deprecate and use IsSelfId() instead
func GetSelfId(client *whatsmeow.Client) string {
	return StrFromJid(*client.Store.ID)
}

// Get chat id
func GetChatId(client *whatsmeow.Client, chatJid *types.JID, senderJid *types.JID) string {
	if chatJid == nil {
		LOG_WARNING(fmt.Sprintf("chatJid is nil\n%s", string(debug.Stack())))
		return ""
	} else if chatJid.Server == types.BroadcastServer && chatJid.User == "status" {
		// place status messages under a dedicated chat
		return StrFromJid(*chatJid)
	} else if chatJid.Server == types.BroadcastServer && chatJid.User != "status" {
		if senderJid != nil {
			userId := GetUserId(client, nil, senderJid)
			if userId == GetSelfId(client) {
				// place broadcast message from self under the broadcast list chat
				return StrFromJid(*chatJid)
			} else {
				// place broadcast messages under corresponding sender, not a dedicated 'broadcast' chat
				// i.e. intentional fall-through to second argument
				return userId
			}
		} else {
			LOG_WARNING(fmt.Sprintf("senderJid is nil\n%s", string(debug.Stack())))
		}
	} else if chatJid.Server == types.HiddenUserServer {
		// try map lid chat id to phone number based
		ctx := context.TODO()
		if pChatJid, _ := client.Store.LIDs.GetPNForLID(ctx, *chatJid); !pChatJid.IsEmpty() {
			return StrFromJid(pChatJid)
		}
	}

	// fall back to plain chatJid
	return StrFromJid(*chatJid)
}

// Get user id
func GetUserId(client *whatsmeow.Client, chatJid *types.JID, userJid *types.JID) string {
	if userJid == nil {
		LOG_WARNING(fmt.Sprintf("userJid is nil\n%s", string(debug.Stack())))
		return ""
	} else if chatJid != nil && chatJid.Server == types.GroupServer {
		// use sender jid as-is in group
		return StrFromJid(*userJid)
	} else if userJid.Server == types.HiddenUserServer {
		// try map lid sender id to phone number based
		ctx := context.TODO()
		if pUserJid, _ := client.Store.LIDs.GetPNForLID(ctx, *userJid); !pUserJid.IsEmpty() {
			return StrFromJid(pUserJid)
		}
	}

	// fall back to plain senderJid
	return StrFromJid(*userJid)
}

// Check if read
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
		ID:        webMsg.GetKey().GetID(),
		PushName:  webMsg.GetPushName(),
		Timestamp: time.Unix(int64(webMsg.GetMessageTimestamp()), 0),
	}
	if info.IsFromMe {
		info.Sender = selfJid
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
	LOG_TRACE(fmt.Sprintf("whatsmeow %s", fmt.Sprintf(msg, args...)))
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
			handler.HandleSyncContacts()
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
		CWmSetStatus(handler.connId, FlagOnline)
		CWmClearStatus(handler.connId, FlagConnecting)

	case *events.Disconnected:
		// disconnected
		LOG_TRACE(fmt.Sprintf("%#v", evt))
		CWmClearStatus(handler.connId, FlagOnline)

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
		handler.HandleSyncContacts()

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
		var client *whatsmeow.Client = GetClient(connId)
		chatId := GetChatId(client, &receipt.MessageSource.Chat, nil)
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
		var client *whatsmeow.Client = GetClient(connId)
		userId := GetUserId(client, nil, &presence.From)
		isOnline := !presence.Unavailable
		timeSeen := int(presence.LastSeen.Unix())
		LOG_TRACE(fmt.Sprintf("Call CWmNewStatusNotify"))
		CWmNewStatusNotify(connId, userId, BoolToInt(isOnline), timeSeen)
	}
}

func (handler *WmEventHandler) HandleChatPresence(chatPresence *events.ChatPresence) {
	connId := handler.connId
	var client *whatsmeow.Client = GetClient(connId)
	chatId := GetChatId(client, &chatPresence.MessageSource.Chat, &chatPresence.MessageSource.Sender)
	userId := GetUserId(client, &chatPresence.MessageSource.Chat, &chatPresence.MessageSource.Sender)
	isTyping := (chatPresence.State == types.ChatPresenceComposing)
	LOG_TRACE(fmt.Sprintf("Call CWmNewTypingNotify"))
	CWmNewTypingNotify(connId, chatId, userId, BoolToInt(isTyping))
}

func (handler *WmEventHandler) HandleHistorySync(historySync *events.HistorySync) {
	var client *whatsmeow.Client = GetClient(handler.connId)
	selfJid := *client.Store.ID

	LOG_TRACE(fmt.Sprintf("HandleHistorySync SyncType %s Progress %d",
		(*historySync.Data.SyncType).String(), historySync.Data.GetProgress()))

	if historySync.Data.GetProgress() < 98 {
		LOG_TRACE("Set Syncing")
		CWmSetStatus(handler.connId, FlagSyncing)
	}

	conversations := historySync.Data.GetConversations()
	for _, conversation := range conversations {
		LOG_TRACE(fmt.Sprintf("HandleHistorySync Conversation %#v", *conversation))

		chatJid, _ := types.ParseJID(conversation.GetID())

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

		chatId := GetChatId(client, &chatJid, nil)
		if hasMessages {
			isMuted := false
			isPinned := false
			ctx := context.TODO()
			settings, setErr := client.Store.ChatSettings.GetChatSettings(ctx, chatJid)
			if setErr != nil {
				LOG_WARNING(fmt.Sprintf("Get chat settings failed %#v", setErr))
			} else {
				if settings.Found {
					mutedUntil := settings.MutedUntil.Unix()
					isMuted = (mutedUntil == -1) || (mutedUntil > time.Now().Unix())
					isPinned = settings.Pinned
				} else {
					LOG_DEBUG(fmt.Sprintf("Chat settings not found %s", chatId))
				}
			}

			LOG_TRACE(fmt.Sprintf("Call CWmNewChatsNotify %s %d %t %t", chatId, len(syncMessages), isMuted, isPinned))
			CWmNewChatsNotify(handler.connId, chatId, isUnread, BoolToInt(isMuted), BoolToInt(isPinned), lastMessageTime)
		} else {
			LOG_TRACE(fmt.Sprintf("Skip CWmNewChatsNotify %s %d", chatId, len(syncMessages)))
		}

	}

	if historySync.Data.GetProgress() == 100 {
		LOG_TRACE("Clear Syncing")
		CWmClearStatus(handler.connId, FlagSyncing)
	}
}

func (handler *WmEventHandler) HandleGroupInfo(groupInfo *events.GroupInfo) {
	connId := handler.connId
	client := GetClient(connId)
	chatId := GetChatId(client, &groupInfo.JID, nil)
	userId := GetUserId(client, &groupInfo.JID, groupInfo.Sender)

	senderJidStr := ""
	if userId != chatId {
		senderJidStr = userId
	}

	// text
	text := ""
	if groupInfo.Name != nil {
		// Group name change
		if senderJidStr == "" {
			senderJidStr = chatId
		}

		groupName := *groupInfo.Name
		text = "[Changed group name to " + groupName.Name + "]"
	} else if len(groupInfo.Join) > 0 {
		// Group member joined
		if (len(groupInfo.Join) == 1) && ((senderJidStr == "") || (senderJidStr == GetUserId(client, &groupInfo.JID, &groupInfo.Join[0]))) {
			senderJidStr = GetUserId(client, &groupInfo.JID, &groupInfo.Join[0])
			text = "[Joined]"
		} else {
			if senderJidStr == "" {
				senderJidStr = GetChatId(client, &groupInfo.JID, nil)
			}

			joined := ""
			for _, jid := range groupInfo.Join {
				if joined != "" {
					joined += ", "
				}

				joined += GetContactName(connId, GetUserId(client, nil, &jid))
			}

			text = "[Added " + joined + "]"
		}
	} else if len(groupInfo.Leave) > 0 {
		// Group member left
		if (len(groupInfo.Leave) == 1) && ((senderJidStr == "") || (senderJidStr == GetUserId(client, &groupInfo.JID, &groupInfo.Leave[0]))) {
			senderJidStr = GetUserId(client, &groupInfo.JID, &groupInfo.Leave[0])
			fromMe := (senderJidStr == GetSelfId(client))
			if !fromMe {
				text = "[Left]"
			}
		} else {
			if senderJidStr == "" {
				senderJidStr = GetChatId(client, &groupInfo.JID, nil)
			}

			left := ""
			for _, jid := range groupInfo.Leave {
				if left != "" {
					left += ", "
				}

				left += GetContactName(connId, GetUserId(client, nil, &jid))
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
	fromMe := (senderJidStr == GetSelfId(client))
	senderId := senderJidStr
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	isSyncRead := false
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, groupInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := false

	// reset typing if needed
	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
}

func (handler *WmEventHandler) HandleDeleteChat(deleteChat *events.DeleteChat) {
	connId := handler.connId
	var client *whatsmeow.Client = GetClient(connId)
	chatId := GetChatId(client, &deleteChat.JID, nil)

	LOG_TRACE(fmt.Sprintf("Call CWmDeleteChatNotify %s", chatId))
	CWmDeleteChatNotify(connId, chatId)
}

func (handler *WmEventHandler) HandleMute(mute *events.Mute) {
	connId := handler.connId
	var client *whatsmeow.Client = GetClient(connId)
	chatId := GetChatId(client, &mute.JID, nil)
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
	var client *whatsmeow.Client = GetClient(connId)
	chatId := GetChatId(client, &pin.JID, nil)
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
	client := GetClient(connId)
	chatId := GetChatId(client, &deleteForMe.ChatJID, nil)
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
		return SanitizeName(contactInfo.FullName)
	}

	if len(contactInfo.FirstName) > 0 {
		return SanitizeName(contactInfo.FirstName)
	}

	if len(contactInfo.PushName) > 0 {
		return SanitizeName(contactInfo.PushName)
	}

	if len(contactInfo.BusinessName) > 0 {
		return SanitizeName(contactInfo.BusinessName)
	}

	if len(contactInfo.RedactedPhone) > 0 {
		return SanitizeName(contactInfo.RedactedPhone)
	}

	return ""
}

func GetGroupDisplayName(connId int, groupInfo *types.GroupInfo) string {
	// Use group name if set
	if len(groupInfo.GroupName.Name) > 0 {
		return groupInfo.GroupName.Name
	}

	// Otherwise build group name from participants
	const maxParticipants = 6
	names := []string{}
	client := GetClient(connId)
	for _, participant := range groupInfo.Participants {
		if len(names) >= maxParticipants {
			break
		}

		userId := StrFromJid(participant.JID)
		if IsSelfId(client, userId) {
			continue
		}

		name := GetContactName(connId, userId)
		if len(name) == 0 {
			name = participant.DisplayName
		}

		names = append(names, name)
	}

	if len(names) == 0 {
		return "Unnamed Group"
	}

	if len(names) == 1 {
		return names[0]
	}

	if len(names) == 2 {
		return names[0] + " & " + names[1]
	}

	return strings.Join(names[:len(names)-1], ", ") + " & " + names[len(names)-1]
}

func PhoneFromUserId(userId string) string {
	phone := ""
	if strings.HasSuffix(userId, "@s.whatsapp.net") {
		phone = strings.Replace(userId, "@s.whatsapp.net", "", 1)
	}

	LOG_TRACE(fmt.Sprintf("user %s phone %s", userId, phone))
	return phone
}

func (handler *WmEventHandler) HandleSyncContacts() {
	LOG_TRACE(fmt.Sprintf("HandleSyncContacts"))
	GetContacts(handler.connId)
}

func GetContacts(connId int) {
	LOG_TRACE(fmt.Sprintf("GetContacts"))
	CWmSetStatus(connId, FlagFetching)

	var client *whatsmeow.Client = GetClient(connId)

	// common
	var notify int = NotifyCache // defer notification until last contact

	// special handling for self (if not in contacts)
	{
		selfId := StrFromJid(*client.Store.ID)
		selfName := client.Store.PushName // used for mentions
		selfPhone := PhoneFromUserId(StrFromJid(*client.Store.ID))
		isSelf := BoolToInt(true) // self
		isAlias := BoolToInt(false)

		LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", selfId, selfName))
		CWmNewContactsNotify(connId, selfId, selfName, selfPhone, isSelf, isAlias, notify)
		AddContactName(connId, selfId, selfName)

		selfLid := StrFromJid(client.Store.LID)
		isAlias = BoolToInt(true)
		LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", selfLid, selfName))
		CWmNewContactsNotify(connId, selfLid, selfName, selfPhone, isSelf, isAlias, notify)
		AddContactName(connId, selfLid, selfName)
	}

	// contacts
	ctx := context.TODO()
	contacts, contErr := client.Store.Contacts.GetAllContacts(ctx)
	if contErr != nil {
		LOG_WARNING(fmt.Sprintf("get all contacts failed %#v", contErr))
	} else {
		LOG_TRACE(fmt.Sprintf("contacts %#v", contacts))
		var userIdPhones map[string]string = make(map[string]string)      // phone
		var userIdNames map[string]string = make(map[string]string)       // contacts
		var aliasUserIdNames map[string]string = make(map[string]string)  // public
		var senderUserIdNames map[string]string = make(map[string]string) // public/phone

		// add regular data first (address book names)
		for jid, contactInfo := range contacts {
			if jid.Server != types.HiddenUserServer {
				name := GetNameFromContactInfo(contactInfo)
				userId := StrFromJid(jid)
				if len(name) > 0 {
					userIdNames[userId] = name
					userIdPhones[userId] = PhoneFromUserId(userId)

					// add lid alias if available
					if lid, _ := client.Store.LIDs.GetLIDForPN(ctx, jid); !lid.IsEmpty() {
						userLid := StrFromJid(lid)
						aliasUserIdNames[userLid] = name
						userIdPhones[userLid] = userIdPhones[userId]
					}
				} else {
					LOG_WARNING(fmt.Sprintf("Skip empty name %s %#v", userId, contactInfo))
				}
			}
		}

		// add lid-based data second (public names)
		for jid, contactInfo := range contacts {
			if jid.Server == types.HiddenUserServer {
				name := GetNameFromContactInfo(contactInfo)
				userId := StrFromJid(jid)
				if len(name) > 0 {
					// check for phone-number based id
					if pid, _ := client.Store.LIDs.GetPNForLID(ctx, jid); !pid.IsEmpty() {
						// check that it's present in the regular userIdNames map
						userPid := StrFromJid(pid)
						if pname, ok := userIdNames[userPid]; ok {
							// if found, treat this jid as an alias, using phone-number based name
							aliasUserIdNames[userId] = pname
							userIdPhones[userId] = userIdPhones[userPid]
						} else {
							// if not found, use only phone number, keep name from lid
							userIdNames[userId] = name
							userIdPhones[userId] = PhoneFromUserId(userPid)
						}

						continue
					}

					// otherwise, treat it as non-alias, and use lid-based name
					userIdNames[userId] = name
					userIdPhones[userId] = ""
				} else {
					LOG_WARNING(fmt.Sprintf("Skip empty name %s %#v", userId, contactInfo))
				}
			}
		}

		// add alias names based on local cache
		senders := GetAllSenders(connId)
		for userId, name := range senders {
			if _, ok := userIdNames[userId]; ok {
				RemoveSender(connId, userId)
			} else if _, ok := aliasUserIdNames[userId]; ok {
				RemoveSender(connId, userId)
			} else {
				senderUserIdNames[userId] = name
				userIdPhones[userId] = ""
			}
		}

		// propagate regular names
		for userId, name := range userIdNames {
			isSelf := IsSelfId(client, userId)
			if !isSelf {
				phone := userIdPhones[userId]
				isAlias := BoolToInt(false)
				LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify regular %s %s", userId, name))
				CWmNewContactsNotify(connId, userId, name, phone, BoolToInt(isSelf), isAlias, notify)
				AddContactName(connId, userId, name)
			}
		}

		// propagate alias names
		for userId, name := range aliasUserIdNames {
			isSelf := IsSelfId(client, userId)
			if !isSelf {
				phone := userIdPhones[userId]
				isAlias := BoolToInt(true)
				LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify alias %s %s", userId, name))
				CWmNewContactsNotify(connId, userId, name, phone, BoolToInt(isSelf), isAlias, notify)
				AddContactName(connId, userId, name)
			}
		}

		// propagate sender names
		for userId, name := range senderUserIdNames {
			isSelf := IsSelfId(client, userId)
			if !isSelf {
				phone := userIdPhones[userId]
				isAlias := BoolToInt(true)
				LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify sender %s %s", userId, name))
				CWmNewContactsNotify(connId, userId, name, phone, BoolToInt(isSelf), isAlias, notify)
				AddContactName(connId, userId, name)
			}
		}
	}

	// groups
	groups, groupErr := client.GetJoinedGroups(ctx)
	if groupErr != nil {
		LOG_WARNING(fmt.Sprintf("get joined groups failed %#v", groupErr))
	} else {
		LOG_TRACE(fmt.Sprintf("groups %#v", groups))
		for _, group := range groups {
			if group == nil {
				continue
			}

			groupId := StrFromJid(group.JID)
			groupName := GetGroupDisplayName(connId, group)
			groupPhone := ""
			isSelf := BoolToInt(false)
			isAlias := BoolToInt(false)
			LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", groupId, groupName))
			CWmNewContactsNotify(connId, groupId, groupName, groupPhone, isSelf, isAlias, notify)
			AddContactName(connId, groupId, groupName)

			if group.GroupEphemeral.IsEphemeral {
				SetExpiration(connId, groupId, group.GroupEphemeral.DisappearingTimer)
			}
		}
	}

	// special handling for official whatsapp account
	{
		whatsappId := "0@s.whatsapp.net"
		whatsappName := "WhatsApp"
		whatsappPhone := ""
		isSelf := BoolToInt(false)
		isAlias := BoolToInt(false)
		LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", whatsappId, whatsappName))
		CWmNewContactsNotify(connId, whatsappId, whatsappName, whatsappPhone, isSelf, isAlias, notify)
		AddContactName(connId, whatsappId, whatsappName)
	}

	// special handling for status updates
	{
		statusId := "status@broadcast"
		statusName := "Status Updates"
		statusPhone := ""
		isSelf := BoolToInt(false)
		isAlias := BoolToInt(false)
		notify = NotifySendCached // perform notification upon last contact
		LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify %s %s", statusId, statusName))
		CWmNewContactsNotify(connId, statusId, statusName, statusPhone, isSelf, isAlias, notify)
		AddContactName(connId, statusId, statusName)
	}

	SetNamesSynced(connId, true)
	CWmClearStatus(connId, FlagFetching)
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

func (handler *WmEventHandler) ProcessContextInfo(contextInfo *waE2E.ContextInfo, quotedId *string, text *string) {
	if contextInfo != nil {
		if quotedId != nil {
			*quotedId = contextInfo.GetStanzaID()
		}

		if (contextInfo.MentionedJID != nil) && (text != nil) {
			connId := handler.connId
			for _, mentionedStr := range contextInfo.MentionedJID {
				mentionedStrParts := strings.SplitN(mentionedStr, "@", 2)
				mentionedJid, _ := types.ParseJID(mentionedStr)      // ex: 121874109111111@lid
				mentionedId := StrFromJid(mentionedJid)              // ex: 121874109111111@lid (skip phone mapping, whatsapp mentions only in groups)
				mentionedName := GetContactName(connId, mentionedId) // ex: Michael Scott
				mentionedOrigText := "@" + mentionedStrParts[0]      // ex: @121874109111111
				mentionedQuoted := CWmAppConfigGetNum("mentions_quoted") != 0
				mentionedHasSpace := strings.Contains(mentionedName, " ")
				var mentionedNewText string
				if mentionedQuoted && mentionedHasSpace {
					mentionedNewText = "@[" + mentionedName + "]" // ex: @[Michael Scott]
				} else {
					mentionedNewText = "@" + mentionedName // ex: @Michael Scott
				}
				LOG_TRACE(fmt.Sprintf("mention jid %s id %s name %s", StrFromJid(mentionedJid), mentionedId, mentionedName)) // @todo: remove
				*text = strings.ReplaceAll(*text, mentionedOrigText, mentionedNewText)
			}
		}
	}
}

func (handler *WmEventHandler) ProcessMessageInfo(messageInfo types.MessageInfo) {
	connId := handler.connId
	var client *whatsmeow.Client = GetClient(connId)
	userId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	if messageInfo.Sender.Server != types.HiddenUserServer {
		return
	}

	if HasContact(connId, userId) || HasSender(connId, userId) {
		return
	}

	// check for phone-number based id
	ctx := context.TODO()
	if pid, _ := client.Store.LIDs.GetPNForLID(ctx, messageInfo.Sender); !pid.IsEmpty() {
		userPid := StrFromJid(pid)
		name := PhoneFromUserId(userPid)
		LOG_TRACE(fmt.Sprintf("add sender %s %s", userId, name))
		AddSender(connId, userId, name)

		if GetNamesSynced(connId) {
			phone := ""
			isSelf := BoolToInt(false)
			isAlias := BoolToInt(true)
			var notify int = NotifyDirect // notify without cache
			LOG_TRACE(fmt.Sprintf("Call CWmNewContactsNotify sender %s %s", userId, name))
			CWmNewContactsNotify(connId, userId, name, phone, isSelf, isAlias, notify)
			AddContactName(connId, userId, name)
		}
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
		handler.ProcessContextInfo(ci, &quotedId, &text)
	}

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := (messageInfo.Edit == "1")

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	isEdited := (messageInfo.Edit == "1")

	// context
	quotedId := ""
	ci := img.GetContextInfo()
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file path, id and status
	filePath := ""
	fileId := ""
	fileStatus := FileStatusNotDownloaded
	if !isEdited {
		var tmpPath string = GetPath(connId) + "/tmp"
		filePath = fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
		fileId = DownloadableMessageToFileId(client, img, filePath)
	}

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: image", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	isEdited := (messageInfo.Edit == "1")

	// context
	quotedId := ""
	ci := vid.GetContextInfo()
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file path, id and status
	filePath := ""
	fileId := ""
	fileStatus := FileStatusNotDownloaded
	if !isEdited {
		var tmpPath string = GetPath(connId) + "/tmp"
		filePath = fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
		fileId = DownloadableMessageToFileId(client, vid, filePath)
	}

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: video", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, aud, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := (messageInfo.Edit == "1")

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: audio", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	isEdited := (messageInfo.Edit == "1")

	// context
	quotedId := ""
	ci := doc.GetContextInfo()
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file path, id and status
	filePath := ""
	fileId := ""
	fileStatus := FileStatusNotDownloaded
	if !isEdited {
		var tmpPath string = GetPath(connId) + "/tmp"
		filePath = fmt.Sprintf("%s/%s-%s", tmpPath, messageInfo.ID, *doc.FileName)
		fileId = DownloadableMessageToFileId(client, doc, filePath)
	}

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: document", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file id, path and status
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%s/%s%s", tmpPath, messageInfo.ID, ext)
	fileId := DownloadableMessageToFileId(client, sticker, filePath)
	fileStatus := FileStatusNotDownloaded

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := (messageInfo.Edit == "1")

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: sticker", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
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
	handler.ProcessContextInfo(ci, &quotedId, &text)

	// file id, path and status
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := (messageInfo.Edit == "1")

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: template", chatId))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
}

func (handler *WmEventHandler) HandleReactionMessage(messageInfo types.MessageInfo, msg *waE2E.Message, isSyncRead bool) {
	LOG_TRACE(fmt.Sprintf("ReactionMessage"))

	connId := handler.connId
	client := GetClient(connId)

	// get reaction part
	reaction := msg.GetReactionMessage()
	if reaction == nil {
		LOG_WARNING(fmt.Sprintf("get reaction message failed"))
		return
	}

	// general
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
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
			newMessageInfo.ID = protocol.GetKey().GetID()
			handler.HandleMessage(newMessageInfo, editedMsg, isSyncRead)
		} else {
			LOG_WARNING(fmt.Sprintf("get edited message failed"))
		}
	} else if protocol.GetType() == waE2E.ProtocolMessage_REVOKE {
		// handle message revoke
		connId := handler.connId
		var client *whatsmeow.Client = GetClient(connId)
		chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
		msgId := protocol.GetKey().GetID()
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
	chatId := GetChatId(client, &messageInfo.Chat, &messageInfo.Sender)
	msgId := messageInfo.ID
	fromMe := messageInfo.IsFromMe
	senderId := GetUserId(client, &messageInfo.Chat, &messageInfo.Sender)
	selfId := GetSelfId(client)
	isSelfChat := (chatId == selfId)
	timeSent := int(messageInfo.Timestamp.Unix())
	isRead := IsRead(isSyncRead, isSelfChat, fromMe, messageInfo.Timestamp, GetTimeRead(connId, chatId))
	isEdited := (messageInfo.Edit == "1")

	ResetTypingStatus(connId, chatId, senderId, fromMe, isSyncRead)
	handler.ProcessMessageInfo(messageInfo)

	LOG_TRACE(fmt.Sprintf("Call CWmNewMessagesNotify %s: %s", chatId, text))
	CWmNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
}

func ResetTypingStatus(connId int, chatId string, userId string, fromMe bool, isSyncRead bool) {

	// ignore new messages from self and during initial sync
	if fromMe || isSyncRead {
		return
	}

	LOG_TRACE("reset typing status " + strconv.Itoa(connId) + ", " + chatId + ", " + userId)

	// update
	isTyping := false

	LOG_TRACE(fmt.Sprintf("Call CWmNewTypingNotify %t %s %s", isTyping, chatId, userId))
	CWmNewTypingNotify(connId, chatId, userId, BoolToInt(isTyping))
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

	ctx := context.TODO()
	dbLog := NcLogger()
	sessionPath := path + "/session.db"
	sqlAddress := fmt.Sprintf("file:%s?_foreign_keys=on", sessionPath)
	container, sqlErr := sqlstore.New(ctx, "sqlite3", sqlAddress, dbLog)
	if sqlErr != nil {
		LOG_WARNING(fmt.Sprintf("sqlite error %#v", sqlErr))
		return -1
	}

	deviceStore, devErr := container.GetFirstDevice(ctx)
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
	store.DeviceProps.Os = proto.String(GetOSName())

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
			usePairingCode := GetConfigOrEnvFlag("USE_PAIRING_CODE")

			LOG_TRACE(fmt.Sprintf("acquire console"))
			CWmSetProtocolUiControl(connId, 1)

			if usePairingCode {
				fmt.Printf("\n")
				fmt.Printf("Open the WhatsApp notification \"Enter code to link new device\" on your phone,\n")
				fmt.Printf("click \"Confirm\" and enter below pairing code on your phone, or press CTRL-C\n")
				fmt.Printf("to abort.\n")
				fmt.Printf("\n")
			} else {
				fmt.Printf("\n")
				fmt.Printf("Open WhatsApp on your phone, click the menu bar and select \"Linked devices\".\n")
				fmt.Printf("Click on \"Link a device\", unlock the phone and aim its camera at the\n")
				fmt.Printf("Qr code displayed on the computer screen.\n")
				fmt.Printf("\n")
				fmt.Printf("Scan the Qr code to authenticate, or press CTRL-C to abort.\n")
			}

			for evt := range ch {
				if evt.Event == whatsmeow.QRChannelEventCode {
					if usePairingCode {
						ctx := context.TODO()
						phoneNumber := GetPhoneNumberFromPath(path)
						showPushNotification := true
						pairCode, pairErr := cli.PairPhone(ctx, phoneNumber, showPushNotification, whatsmeow.PairClientFirefox, GetClientDisplayName())
						if pairErr != nil {
							LOG_WARNING(fmt.Sprintf("pair phone error %#v", pairErr))
							SetState(connId, Disconnected)
						} else {
							fmt.Printf("Code: %s\n", pairCode)
							fmt.Printf("\n")
						}
					} else {
						if hasGUI {
							qrPath := path + "/tmp/qr.png"
							qrcode.WriteFile(evt.Code, qrcode.Medium, 512, qrPath)
							ShowImage(qrPath)
						} else {
							qrterminal.GenerateHalfBlock(evt.Code, qrterminal.L, os.Stdout)
						}
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
		CWmClearStatus(connId, FlagConnecting)
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

		CWmClearStatus(connId, FlagConnecting)
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
	isEdited := false

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

			videoMessage := waE2E.VideoMessage{}

			if len(editMsgId) > 0 {

				LOG_TRACE("edit video caption " + fileType)
				videoMessage = waE2E.VideoMessage{
					Caption: proto.String(text),
				}
				isEdited = true

			} else {

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

				videoMessage = waE2E.VideoMessage{
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
			}

			message.VideoMessage = &videoMessage
			isSend = true

		} else if isSendType && (mimeType == "image") {

			imageMessage := waE2E.ImageMessage{}

			if len(editMsgId) > 0 {

				LOG_TRACE("edit image caption " + fileType)
				imageMessage = waE2E.ImageMessage{
					Caption: proto.String(text),
				}
				isEdited = true

			} else {

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

				imageMessage = waE2E.ImageMessage{
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
			}

			message.ImageMessage = &imageMessage
			isSend = true

		} else {

			documentMessage := waE2E.DocumentMessage{}

			if len(editMsgId) > 0 {

				LOG_TRACE("edit document caption " + fileType)
				documentMessage = waE2E.DocumentMessage{
					Caption: proto.String(text),
				}
				isEdited = true

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

				documentMessage = waE2E.DocumentMessage{
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
		if isEdited {
			messageInfo.Edit = "1"
		}

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

func WmGetContacts(connId int) int {

	LOG_TRACE("get contacts " + strconv.Itoa(connId))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get client
	client := GetClient(connId)

	// sync contacts
	err := client.FetchAppState(context.TODO(), appstate.WAPatchCriticalUnblockLow, true, false)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("fetch contacts app state failed %#v", err))
	}

	// get contacts
	GetContacts(connId)

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

	// ignore presence requests before connected
	if GetState(connId) != Connected {
		return -1
	}

	// ignore presence requests for groups
	userJid, _ := types.ParseJID(userId)
	if userJid.Server == types.GroupServer {
		return -1
	}

	// ignore presence requests for self
	selfId := GetSelfId(client)
	if userId == selfId {
		return -1
	}

	// subscribe user presence
	ctx := context.TODO()
	err := client.SubscribePresence(ctx, userJid)

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
	ctx := context.TODO()
	err := client.MarkRead(ctx, msgIds, timeRead, chatJid, senderJid)

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
	selfId := GetSelfId(client)
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
		ctx := context.TODO()
		err := client.LeaveGroup(ctx, chatJid)

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
	selfId := GetSelfId(client)
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
	ctx := context.TODO()
	err := client.SendChatPresence(ctx, chatJid, chatPresence, chatPresenceMedia)

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

	ctx := context.TODO()
	err := client.SendPresence(ctx, presence)
	if err != nil {
		LOG_WARNING("Failed to send presence")
	} else {
		LOG_TRACE("Sent presence ok")
		if isOnline == 1 {
			CWmClearStatus(connId, FlagAway)
		} else {
			CWmSetStatus(connId, FlagAway)
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

	// download file
	filePath, fileStatus := DownloadFromFileId(connId, fileId)

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
