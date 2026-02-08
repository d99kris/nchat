// gosg.go
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"context"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"mime"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"github.com/google/uuid"
	"github.com/mdp/qrterminal"
	"github.com/rs/zerolog"
	"github.com/skip2/go-qrcode"
	"go.mau.fi/util/dbutil"
	"google.golang.org/protobuf/proto"

	_ "github.com/mattn/go-sqlite3"

	"go.mau.fi/mautrix-signal/pkg/libsignalgo"
	"go.mau.fi/mautrix-signal/pkg/signalmeow"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/events"
	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf/backuppb"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/store"
	"go.mau.fi/mautrix-signal/pkg/signalmeow/types"
)

var signalDate int = 20260129

type State int64

const (
	None State = iota
	Connecting
	Connected
	Disconnected
)

var (
	mx          sync.Mutex
	clients     map[int]*signalmeow.Client                     = make(map[int]*signalmeow.Client)
	devices     map[int]*store.Device                          = make(map[int]*store.Device)
	containers  map[int]*store.Container                       = make(map[int]*store.Container)
	paths       map[int]string                                 = make(map[int]string)
	contacts    map[int]map[string]string                      = make(map[int]map[string]string)
	states      map[int]State                                  = make(map[int]State)
	handlers    map[int]*SgEventHandler                        = make(map[int]*SgEventHandler)
	statusChans map[int]chan signalmeow.SignalConnectionStatus = make(map[int]chan signalmeow.SignalConnectionStatus)
	pinnedChats map[int]map[string]bool                        = make(map[int]map[string]bool)
	mutedChats  map[int]map[string]bool                        = make(map[int]map[string]bool)
	recentMsgs  map[int]map[string][]recentMessage             = make(map[int]map[string][]recentMessage)
)

const maxRecentMessages = 5

type recentMessage struct {
	senderACI string
	timestamp uint64
}

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

// keep in sync with enum NotifyType in sgchat.cpp
var NotifyDirect = 0
var NotifyCache = 1
var NotifySendCached = 2

func AddConn(client *signalmeow.Client, device *store.Device, container *store.Container, path string) int {
	mx.Lock()
	var connId int = len(clients)
	clients[connId] = client
	devices[connId] = device
	containers[connId] = container
	paths[connId] = path
	contacts[connId] = make(map[string]string)
	states[connId] = None
	handlers[connId] = &SgEventHandler{connId}
	statusChans[connId] = nil
	pinnedChats[connId] = make(map[string]bool)
	mutedChats[connId] = make(map[string]bool)
	recentMsgs[connId] = make(map[string][]recentMessage)
	mx.Unlock()
	return connId
}

func RemoveConn(connId int) {
	mx.Lock()
	delete(clients, connId)
	delete(devices, connId)
	delete(containers, connId)
	delete(paths, connId)
	delete(contacts, connId)
	delete(states, connId)
	delete(handlers, connId)
	delete(statusChans, connId)
	delete(pinnedChats, connId)
	delete(mutedChats, connId)
	delete(recentMsgs, connId)
	mx.Unlock()
}

func GetClient(connId int) *signalmeow.Client {
	mx.Lock()
	var client *signalmeow.Client = clients[connId]
	mx.Unlock()
	return client
}

func GetDevice(connId int) *store.Device {
	mx.Lock()
	var device *store.Device = devices[connId]
	mx.Unlock()
	return device
}

func GetContainer(connId int) *store.Container {
	mx.Lock()
	var container *store.Container = containers[connId]
	mx.Unlock()
	return container
}

func GetHandler(connId int) *SgEventHandler {
	mx.Lock()
	var handler *SgEventHandler = handlers[connId]
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

func GetStatusChan(connId int) chan signalmeow.SignalConnectionStatus {
	mx.Lock()
	var ch chan signalmeow.SignalConnectionStatus = statusChans[connId]
	mx.Unlock()
	return ch
}

func SetStatusChan(connId int, ch chan signalmeow.SignalConnectionStatus) {
	mx.Lock()
	statusChans[connId] = ch
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

func TrackRecentMessage(connId int, chatId string, senderACI string, timestamp uint64) {
	mx.Lock()
	msgs := recentMsgs[connId][chatId]
	msgs = append(msgs, recentMessage{senderACI: senderACI, timestamp: timestamp})
	if len(msgs) > maxRecentMessages {
		msgs = msgs[len(msgs)-maxRecentMessages:]
	}
	recentMsgs[connId][chatId] = msgs
	mx.Unlock()
}

func GetRecentMessages(connId int, chatId string) []recentMessage {
	mx.Lock()
	msgs := recentMsgs[connId][chatId]
	mx.Unlock()
	return msgs
}

// download info
var downloadInfoVersion = 3 // bump version upon any struct change
type DownloadInfo struct {
	Version       int    `json:"Version_int"`
	TargetPath    string `json:"TargetPath_string"`
	Url           string `json:"Url_string"`
	Key           []byte `json:"Key_arraybyte"`
	Digest        []byte `json:"Digest_arraybyte"`
	Size          uint32 `json:"Size_uint32"`
	CdnId         uint64 `json:"CdnId_uint64"`
	CdnKey        string `json:"CdnKey_string"`
	CdnNumber     uint32 `json:"CdnNumber_uint32"`
	PlaintextHash []byte `json:"PlaintextHash_arraybyte,omitempty"`
}

func AttachmentToFileId(attachment *signalpb.AttachmentPointer, targetPath string) string {
	if attachment == nil {
		return ""
	}

	var info DownloadInfo
	info.Version = downloadInfoVersion
	info.TargetPath = targetPath

	info.CdnId = attachment.GetCdnId()
	info.CdnKey = attachment.GetCdnKey()
	info.CdnNumber = attachment.GetCdnNumber()

	if info.CdnId == 0 && info.CdnKey == "" {
		LOG_WARNING("attachment has no cdn id or key")
		return ""
	}

	info.Key = attachment.GetKey()
	info.Digest = attachment.GetDigest()
	info.Size = attachment.GetSize()

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

	targetPath := info.TargetPath
	filePath := ""
	fileStatus := FileStatusNone

	// download if not yet present
	if _, statErr := os.Stat(targetPath); os.IsNotExist(statErr) {
		LOG_TRACE(fmt.Sprintf("download new %#v", targetPath))
		CSgSetStatus(connId, FlagFetching)

		ctx := context.TODO()
		attachmentPointer := &signalpb.AttachmentPointer{
			Key:       info.Key,
			Digest:    info.Digest,
			Size:      proto.Uint32(info.Size),
			CdnNumber: proto.Uint32(info.CdnNumber),
		}

		if info.CdnId != 0 {
			attachmentPointer.AttachmentIdentifier = &signalpb.AttachmentPointer_CdnId{CdnId: info.CdnId}
		} else if info.CdnKey != "" {
			attachmentPointer.AttachmentIdentifier = &signalpb.AttachmentPointer_CdnKey{CdnKey: info.CdnKey}
		}

		data, err := signalmeow.DownloadAttachmentWithPointer(ctx, attachmentPointer, info.PlaintextHash)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("download error %#v", err))
			fileStatus = FileStatusDownloadFailed
		} else {
			file, err := os.Create(targetPath)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("create error %#v", err))
				fileStatus = FileStatusDownloadFailed
			} else {
				defer file.Close()
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
		CSgClearStatus(connId, FlagFetching)
	} else {
		LOG_TRACE(fmt.Sprintf("download cached %#v", targetPath))
		filePath = targetPath
		fileStatus = FileStatusDownloaded
	}

	return filePath, fileStatus
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

func GetDeviceName() string {
	return "nchat (" + GetOSName() + ")"
}

func GetConfigOrEnvFlag(envVarName string) bool {
	configParamName := strings.ToLower(envVarName)
	isConfigSet := CSgAppConfigGetNum(configParamName)
	if IntToBool(isConfigSet) {
		return true
	}

	_, isEnvSet := os.LookupEnv(envVarName)
	if isEnvSet {
		CSgAppConfigSetNum(configParamName, 1)
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
		file, err := os.CreateTemp("/tmp", "nchat-x11check.*.sh")
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

// Signal uses UUIDs for identifiers
func UUIDToString(id uuid.UUID) string {
	return id.String()
}

func StringToUUID(s string) uuid.UUID {
	id, err := uuid.Parse(s)
	if err != nil {
		return uuid.Nil
	}
	return id
}

func utf16OffsetToByteOffset(s string, utf16Offset int) int {
	units := 0
	for i, r := range s {
		if units >= utf16Offset {
			return i
		}
		if r >= 0x10000 {
			units += 2
		} else {
			units++
		}
	}
	return len(s)
}

func byteOffsetToUtf16Offset(s string, byteOffset int) int {
	units := 0
	for i, r := range s {
		if i >= byteOffset {
			return units
		}
		if r >= 0x10000 {
			units += 2
		} else {
			units++
		}
	}
	return units
}

func styleToMarker(style signalpb.BodyRange_Style) string {
	switch style {
	case signalpb.BodyRange_BOLD:
		return "*"
	case signalpb.BodyRange_ITALIC:
		return "_"
	case signalpb.BodyRange_STRIKETHROUGH:
		return "~"
	case signalpb.BodyRange_MONOSPACE:
		return "`"
	default:
		return ""
	}
}

func backupStyleToMarker(style backuppb.BodyRange_Style) string {
	switch style {
	case backuppb.BodyRange_BOLD:
		return "*"
	case backuppb.BodyRange_ITALIC:
		return "_"
	case backuppb.BodyRange_STRIKETHROUGH:
		return "~"
	case backuppb.BodyRange_MONOSPACE:
		return "`"
	default:
		return ""
	}
}

type textOp struct {
	bytePos    int
	byteLen    int
	insertText string
	priority   int // lower = applied first when same bytePos (after descending sort)
}

func ProcessFormattedText(connId int, text string, bodyRanges []*signalpb.BodyRange) string {
	if len(bodyRanges) == 0 {
		return text
	}

	mentionsQuoted := CSgAppConfigGetNum("mentions_quoted") != 0
	var ops []textOp

	for _, br := range bodyRanges {
		start := int(br.GetStart())
		length := int(br.GetLength())

		mentionAci := br.GetMentionAci()
		if mentionAci != "" {
			// Mention: replace U+FFFC at start position
			bytePos := utf16OffsetToByteOffset(text, start)
			mentionId := UUIDToString(StringToUUID(mentionAci))
			mentionName := GetContactName(connId, mentionId)
			var replacement string
			if mentionsQuoted && strings.Contains(mentionName, " ") {
				replacement = "@[" + mentionName + "]"
			} else {
				replacement = "@" + mentionName
			}
			// U+FFFC is 3 bytes in UTF-8
			ops = append(ops, textOp{bytePos: bytePos, byteLen: 3, insertText: replacement, priority: 1})
			continue
		}

		marker := styleToMarker(br.GetStyle())
		if marker == "" {
			continue
		}

		startByte := utf16OffsetToByteOffset(text, start)
		endByte := utf16OffsetToByteOffset(text, start+length)
		// Insert closing marker at end, opening marker at start
		// Priority: opening (0) before closing (2) at same position after desc sort
		ops = append(ops, textOp{bytePos: endByte, byteLen: 0, insertText: marker, priority: 2})
		ops = append(ops, textOp{bytePos: startByte, byteLen: 0, insertText: marker, priority: 0})
	}

	// Sort descending by bytePos, then ascending by priority
	sort.Slice(ops, func(i, j int) bool {
		if ops[i].bytePos != ops[j].bytePos {
			return ops[i].bytePos > ops[j].bytePos
		}
		return ops[i].priority < ops[j].priority
	})

	// Apply back-to-front
	for _, op := range ops {
		text = text[:op.bytePos] + op.insertText + text[op.bytePos+op.byteLen:]
	}

	return text
}

func ProcessBackupFormattedText(connId int, text string, bodyRanges []*backuppb.BodyRange) string {
	if len(bodyRanges) == 0 {
		return text
	}

	mentionsQuoted := CSgAppConfigGetNum("mentions_quoted") != 0
	var ops []textOp

	for _, br := range bodyRanges {
		start := int(br.GetStart())
		length := int(br.GetLength())

		mentionAci := br.GetMentionAci()
		if len(mentionAci) == 16 {
			bytePos := utf16OffsetToByteOffset(text, start)
			mentionId := UUIDToString(uuid.UUID(mentionAci))
			mentionName := GetContactName(connId, mentionId)
			var replacement string
			if mentionsQuoted && strings.Contains(mentionName, " ") {
				replacement = "@[" + mentionName + "]"
			} else {
				replacement = "@" + mentionName
			}
			ops = append(ops, textOp{bytePos: bytePos, byteLen: 3, insertText: replacement, priority: 1})
			continue
		}

		marker := backupStyleToMarker(br.GetStyle())
		if marker == "" {
			continue
		}

		startByte := utf16OffsetToByteOffset(text, start)
		endByte := utf16OffsetToByteOffset(text, start+length)
		ops = append(ops, textOp{bytePos: endByte, byteLen: 0, insertText: marker, priority: 2})
		ops = append(ops, textOp{bytePos: startByte, byteLen: 0, insertText: marker, priority: 0})
	}

	sort.Slice(ops, func(i, j int) bool {
		if ops[i].bytePos != ops[j].bytePos {
			return ops[i].bytePos > ops[j].bytePos
		}
		return ops[i].priority < ops[j].priority
	})

	for _, op := range ops {
		text = text[:op.bytePos] + op.insertText + text[op.bytePos+op.byteLen:]
	}

	return text
}


func ParseMarkdown(text string) (string, []*signalpb.BodyRange) {
	type markerPair struct {
		openByte  int
		closeByte int
		marker    string
	}

	markerStyles := map[string]signalpb.BodyRange_Style{
		"*": signalpb.BodyRange_BOLD,
		"_": signalpb.BodyRange_ITALIC,
		"~": signalpb.BodyRange_STRIKETHROUGH,
		"`": signalpb.BodyRange_MONOSPACE,
	}

	// Pass 1: find matching marker pairs
	openPositions := make(map[string][]int) // marker -> stack of open byte positions
	var pairs []markerPair
	i := 0
	for i < len(text) {
		r, size := utf8.DecodeRuneInString(text[i:])
		ch := string(r)
		if _, ok := markerStyles[ch]; ok && size == 1 {
			if stack, exists := openPositions[ch]; exists && len(stack) > 0 {
				openPos := stack[len(stack)-1]
				// Only valid if there is content between markers
				if i > openPos+1 {
					pairs = append(pairs, markerPair{openByte: openPos, closeByte: i, marker: ch})
					openPositions[ch] = stack[:len(stack)-1]
				} else {
					// Adjacent markers like ** — treat second as new open
					openPositions[ch] = stack[:len(stack)-1]
					openPositions[ch] = append(openPositions[ch], i)
				}
			} else {
				openPositions[ch] = append(openPositions[ch], i)
			}
		}
		i += size
	}

	if len(pairs) == 0 {
		return text, nil
	}

	// Build set of byte positions to skip (marker positions)
	skipSet := make(map[int]bool)
	for _, p := range pairs {
		skipSet[p.openByte] = true
		skipSet[p.closeByte] = true
	}

	// Pass 2: build stripped text and byte offset mapping
	origToStripped := make(map[int]int)
	var stripped strings.Builder
	strippedPos := 0
	for j := 0; j < len(text); {
		origToStripped[j] = strippedPos
		_, size := utf8.DecodeRuneInString(text[j:])
		if skipSet[j] {
			j += size
			continue
		}
		stripped.WriteString(text[j : j+size])
		strippedPos += size
		j += size
	}
	origToStripped[len(text)] = strippedPos

	strippedText := stripped.String()

	// Create BodyRanges
	var bodyRanges []*signalpb.BodyRange
	for _, p := range pairs {
		startByte := origToStripped[p.openByte]
		endByte := origToStripped[p.closeByte]
		startUtf16 := byteOffsetToUtf16Offset(strippedText, startByte)
		endUtf16 := byteOffsetToUtf16Offset(strippedText, endByte)
		length := endUtf16 - startUtf16
		if length <= 0 {
			continue
		}
		style := markerStyles[p.marker]
		startU32 := uint32(startUtf16)
		lengthU32 := uint32(length)
		bodyRanges = append(bodyRanges, &signalpb.BodyRange{
			Start:  &startU32,
			Length: &lengthU32,
			AssociatedValue: &signalpb.BodyRange_Style_{
				Style: style,
			},
		})
	}

	return strippedText, bodyRanges
}

func SliceIndex(list []string, value string, defaultValue int) int {
	for i, v := range list {
		if v == value {
			return i
		}
	}
	return defaultValue
}

var fallbackMimeExts = map[string]string{
	"audio/aac":       ".aac",
	"audio/ogg":       ".ogg",
	"video/mp4":       ".mp4",
	"image/webp":      ".webp",
	"image/heic":      ".heic",
	"image/heif":      ".heif",
	"audio/mp4":       ".m4a",
	"audio/mpeg":      ".mp3",
	"audio/amr":       ".amr",
	"image/gif":       ".gif",
	"image/png":       ".png",
	"image/jpeg":      ".jpg",
	"application/pdf": ".pdf",
}

func ExtensionByType(mimeType string, defaultExt string) string {
	ext := defaultExt
	exts, extErr := mime.ExtensionsByType(mimeType)
	if extErr == nil && len(exts) > 0 {
		// prefer common extensions over less common (.jpe, etc) returned by mime library
		preferredExts := []string{".jpg", ".jpeg"}
		sort.Slice(exts, func(i, j int) bool {
			return SliceIndex(preferredExts, exts[i], 1000000) < SliceIndex(preferredExts, exts[j], 1000000)
		})
		ext = exts[0]
	} else if fallback, ok := fallbackMimeExts[mimeType]; ok {
		ext = fallback
	}

	return ext
}

// logger
type ncLogger struct {
	level zerolog.Level
}

func (l *ncLogger) Log() *zerolog.Event {
	return nil
}

func (l *ncLogger) WithLevel(level zerolog.Level) *zerolog.Event {
	return nil
}

func (l *ncLogger) Trace() *zerolog.Event {
	return nil
}

func (l *ncLogger) Debug() *zerolog.Event {
	return nil
}

func (l *ncLogger) Info() *zerolog.Event {
	return nil
}

func (l *ncLogger) Warn() *zerolog.Event {
	return nil
}

func (l *ncLogger) Error() *zerolog.Event {
	return nil
}

func (l *ncLogger) Err(err error) *zerolog.Event {
	return nil
}

func (l *ncLogger) Fatal() *zerolog.Event {
	return nil
}

func (l *ncLogger) Panic() *zerolog.Event {
	return nil
}

func (l *ncLogger) With() zerolog.Context {
	return zerolog.Context{}
}

func (l *ncLogger) Level(level zerolog.Level) zerolog.Logger {
	return zerolog.Logger{}
}

func (l *ncLogger) Sample(s zerolog.Sampler) zerolog.Logger {
	return zerolog.Logger{}
}

func (l *ncLogger) Hook(h zerolog.Hook) zerolog.Logger {
	return zerolog.Logger{}
}

func (l *ncLogger) Write(p []byte) (n int, err error) {
	LOG_TRACE(fmt.Sprintf("signalmeow %s", strings.TrimRight(string(p), "\n")))
	return len(p), nil
}

type dbLogger struct{}

func (d *dbLogger) QueryTiming(ctx context.Context, method, query string, args []any, rowCount int, duration time.Duration, err error) {
	LOG_TRACE(fmt.Sprintf("db query %s: %s (rows: %d, duration: %v)", method, query, rowCount, duration))
}

func (d *dbLogger) WarnUnsupportedVersion(current, compat, latest int) {
	LOG_WARNING(fmt.Sprintf("db unsupported version current=%d compat=%d latest=%d", current, compat, latest))
}

func (d *dbLogger) DoUpgrade(from, to int, message string, txn dbutil.TxnMode) {
	LOG_INFO(fmt.Sprintf("db upgrade from %d to %d: %s", from, to, message))
}

func (d *dbLogger) PrepareUpgrade(current, compat, latest int) {
	LOG_INFO(fmt.Sprintf("db prepare upgrade current=%d compat=%d latest=%d", current, compat, latest))
}

func (d *dbLogger) Warn(msg string, args ...any) {
	LOG_WARNING(fmt.Sprintf("db warn: "+msg, args...))
}

func NcDbLogger() dbutil.DatabaseLogger {
	return &dbLogger{}
}

// event handling
type SgEventHandler struct {
	connId int
}

func (handler *SgEventHandler) HandleEvent(evt events.SignalEvent) bool {
	LOG_TRACE(fmt.Sprintf("HandleEvent %T %#v", evt, evt))
	switch e := evt.(type) {
	case *events.ChatEvent:
		return handler.handleChatEvent(e)
	case *events.Receipt:
		return handler.handleReceipt(e)
	case *events.ReadSelf:
		return handler.handleReadSelf(e)
	case *events.ContactList:
		return handler.handleContactList(e)
	case *events.DeleteForMe:
		return handler.handleDeleteForMe(e)
	case *events.LoggedOut:
		return handler.handleLoggedOut(e)
	case *events.DecryptionError:
		LOG_WARNING(fmt.Sprintf("DecryptionError: %v", e.Err))
		return true
	case *events.Call:
		LOG_TRACE(fmt.Sprintf("Call event ignored"))
		return true
	case *events.ACIFound:
		LOG_TRACE(fmt.Sprintf("ACIFound event ignored"))
		return true
	case *events.MessageRequestResponse:
		LOG_TRACE(fmt.Sprintf("MessageRequestResponse event ignored"))
		return true
	case *events.PinnedConversationsChanged:
		return handler.handlePinnedConversationsChanged(e)
	case *events.ChatMuteChanged:
		return handler.handleChatMuteChanged(e)
	case *events.QueueEmpty:
		LOG_TRACE(fmt.Sprintf("QueueEmpty event"))
		return true
	default:
		LOG_TRACE(fmt.Sprintf("Unknown event type: %T", evt))
		return true
	}
}

func (handler *SgEventHandler) handleChatEvent(evt *events.ChatEvent) bool {
	LOG_TRACE(fmt.Sprintf("handleChatEvent"))
	connId := handler.connId
	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return true
	}

	info := evt.Info
	chatId := info.ChatID
	senderUUID := info.Sender
	senderId := UUIDToString(senderUUID)
	device := GetDevice(connId)
	selfId := UUIDToString(device.ACI)
	fromMe := (senderId == selfId)
	timeSent := int(info.ServerTimestamp / 1000) // Convert ms to seconds

	switch content := evt.Event.(type) {
	case *signalpb.DataMessage:
		handler.handleDataMessage(chatId, senderId, fromMe, timeSent, content)
	case *signalpb.TypingMessage:
		handler.handleTypingMessage(chatId, senderId, content)
	case *signalpb.EditMessage:
		handler.handleEditMessage(chatId, senderId, fromMe, timeSent, content)
	default:
		LOG_TRACE(fmt.Sprintf("Unhandled chat event content type: %T", content))
	}

	return true
}

func (handler *SgEventHandler) handleDataMessage(chatId string, senderId string, fromMe bool, timeSent int, msg *signalpb.DataMessage) {
	LOG_TRACE(fmt.Sprintf("handleDataMessage"))
	connId := handler.connId

	if msg.GetReaction() != nil {
		handler.handleReaction(chatId, senderId, fromMe, msg.GetReaction())
		return
	}

	if msg.GetDelete() != nil {
		targetMsgId := fmt.Sprintf("%d", msg.GetDelete().GetTargetSentTimestamp())
		LOG_TRACE(fmt.Sprintf("handleDataMessage delete %s %s", chatId, targetMsgId))
		CSgDeleteMessageNotify(connId, chatId, targetMsgId)
		return
	}

	// Group change messages
	if msg.GetGroupV2() != nil && len(msg.GetGroupV2().GetGroupChange()) > 0 && msg.GetBody() == "" {
		handler.handleGroupChange(chatId, senderId, fromMe, timeSent, msg)
		return
	}

	// Unsupported message type placeholders
	placeholder := ""
	if msg.GetSticker() != nil {
		placeholder = "[Sticker]"
	} else if len(msg.GetContact()) > 0 {
		placeholder = "[Contact]"
	} else if msg.GetPayment() != nil {
		placeholder = "[Payment]"
	} else if msg.GetGiftBadge() != nil {
		placeholder = "[GiftBadge]"
	} else if msg.GetGroupCallUpdate() != nil {
		placeholder = "[GroupCall]"
	} else if msg.GetStoryContext() != nil {
		placeholder = "[Story]"
	} else if msg.GetPollCreate() != nil {
		placeholder = "[Poll]"
	} else if msg.GetPollVote() != nil {
		placeholder = "[PollVote]"
	} else if msg.GetPollTerminate() != nil {
		placeholder = "[PollTerminate]"
	} else if msg.GetFlags()&uint32(signalpb.DataMessage_EXPIRATION_TIMER_UPDATE) != 0 {
		placeholder = "[ExpirationTimerUpdate]"
	} else if msg.GetFlags()&uint32(signalpb.DataMessage_END_SESSION) != 0 {
		placeholder = "[EndSession]"
	} else if msg.GetFlags()&uint32(signalpb.DataMessage_PROFILE_KEY_UPDATE) != 0 {
		return // silent, no need to display
	}

	// Text message
	text := msg.GetBody()
	text = ProcessFormattedText(connId, text, msg.GetBodyRanges())
	if text == "" && placeholder != "" {
		text = placeholder
	}
	quotedId := ""
	if msg.GetQuote() != nil {
		quotedId = fmt.Sprintf("%d", msg.GetQuote().GetId())
	}

	// File handling
	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	attachments := msg.GetAttachments()
	if len(attachments) > 0 {
		attachment := attachments[0]
		ext := ExtensionByType(attachment.GetContentType(), ".bin")
		fileName := attachment.GetFileName()
		if fileName == "" {
			fileName = fmt.Sprintf("%d%s", timeSent, ext)
		}
		var tmpPath string = GetPath(connId) + "/tmp"
		filePath = fmt.Sprintf("%s/%s", tmpPath, fileName)
		fileId = AttachmentToFileId(attachment, filePath)
		fileStatus = FileStatusNotDownloaded
	}

	msgId := fmt.Sprintf("%d", msg.GetTimestamp())
	isRead := fromMe // Messages from self are read
	isEdited := false

	// Reset typing status when receiving a message from someone else
	if !fromMe {
		CSgNewTypingNotify(connId, chatId, senderId, 0)
	}

	// Skip truly empty messages to avoid blank bubbles
	if text == "" && fileId == "" {
		LOG_TRACE(fmt.Sprintf("skipping empty DataMessage %v", msg))
		return
	}

	LOG_TRACE(fmt.Sprintf("Call CSgNewMessagesNotify %s: %s", chatId, text))
	CSgNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))

	TrackRecentMessage(connId, chatId, senderId, msg.GetTimestamp())
}

func decryptGroupMemberUUID(gsp *libsignalgo.GroupSecretParams, encryptedUserId []byte) string {
	if len(encryptedUserId) == 0 {
		return ""
	}
	ciphertext := libsignalgo.UUIDCiphertext(encryptedUserId)
	serviceID, err := gsp.DecryptServiceID(ciphertext)
	if err != nil {
		return ""
	}
	return serviceID.UUID.String()
}

func (handler *SgEventHandler) handleGroupChange(chatId string, senderId string, fromMe bool, timeSent int, msg *signalpb.DataMessage) {
	LOG_TRACE(fmt.Sprintf("handleGroupChange"))
	connId := handler.connId

	// Parse the group change actions from the protobuf without calling DecryptGroupChange
	// (which triggers GroupCache.ApplyUpdate with nil endorsements, causing a Rust panic).
	// We decrypt user IDs via group secret params to produce meaningful messages.
	var texts []string
	groupChangeBytes := msg.GetGroupV2().GetGroupChange()
	masterKeyBytes := msg.GetGroupV2().GetMasterKey()

	// Derive group secret params to decrypt user IDs in the actions
	var gsp *libsignalgo.GroupSecretParams
	if len(masterKeyBytes) == libsignalgo.GroupMasterKeyLength {
		var mk libsignalgo.GroupMasterKey
		copy(mk[:], masterKeyBytes)
		if sp, err := mk.SecretParams(); err == nil {
			gsp = &sp
		}
	}

	outerChange := &signalpb.GroupChange{}
	if err := proto.Unmarshal(groupChangeBytes, outerChange); err == nil {
		actions := &signalpb.GroupChange_Actions{}
		if err := proto.Unmarshal(outerChange.GetActions(), actions); err == nil {
			var addedNames []string
			for _, a := range actions.GetAddMembers() {
				memberId := ""
				if gsp != nil && a.GetAdded() != nil {
					memberId = decryptGroupMemberUUID(gsp, a.GetAdded().GetUserId())
				}
				name := GetContactName(connId, memberId)
				if memberId == senderId {
					texts = append(texts, "[Joined]")
				} else {
					addedNames = append(addedNames, name)
				}
			}
			if len(addedNames) > 0 {
				texts = append(texts, "[Added "+strings.Join(addedNames, ", ")+"]")
			}
			var removedNames []string
			for _, a := range actions.GetDeleteMembers() {
				memberId := ""
				if gsp != nil {
					memberId = decryptGroupMemberUUID(gsp, a.GetDeletedUserId())
				}
				name := GetContactName(connId, memberId)
				if memberId == senderId {
					texts = append(texts, "[Left]")
				} else {
					removedNames = append(removedNames, name)
				}
			}
			if len(removedNames) > 0 {
				texts = append(texts, "[Removed "+strings.Join(removedNames, ", ")+"]")
			}
			if actions.GetModifyTitle() != nil {
				newTitle := ""
				if gsp != nil {
					if decrypted, err := gsp.DecryptBlobWithPadding(actions.GetModifyTitle().GetTitle()); err == nil {
						var blob signalpb.GroupAttributeBlob
						if err := proto.Unmarshal(decrypted, &blob); err == nil {
							newTitle = blob.GetTitle()
						}
					}
				}
				if newTitle != "" {
					texts = append(texts, "[Changed group name to "+newTitle+"]")
					AddContactName(connId, chatId, newTitle)
					CSgNewContactsNotify(connId, chatId, newTitle, "", BoolToInt(false), BoolToInt(false), NotifyDirect)
				} else {
					texts = append(texts, "[Changed group name]")
				}
			}
			if actions.GetModifyDescription() != nil {
				texts = append(texts, "[Changed group description]")
			}
			if actions.GetModifyAvatar() != nil {
				texts = append(texts, "[Changed group avatar]")
			}
			if actions.GetModifyDisappearingMessagesTimer() != nil {
				texts = append(texts, "[Changed disappearing messages timer]")
			}
			for _, a := range actions.GetModifyMemberRoles() {
				memberId := ""
				if gsp != nil {
					memberId = decryptGroupMemberUUID(gsp, a.GetUserId())
				}
				name := GetContactName(connId, memberId)
				texts = append(texts, "[Changed role for "+name+"]")
			}
			for range actions.GetAddPendingMembers() {
				texts = append(texts, "[Invited a member]")
			}
			for range actions.GetDeletePendingMembers() {
				texts = append(texts, "[Invitation revoked]")
			}
			for _, a := range actions.GetPromotePendingMembers() {
				memberId := ""
				if gsp != nil {
					memberId = decryptGroupMemberUUID(gsp, a.GetUserId())
				}
				name := GetContactName(connId, memberId)
				texts = append(texts, "["+name+" accepted invite]")
			}
			for range actions.GetAddRequestingMembers() {
				texts = append(texts, "[Member requested to join]")
			}
			for range actions.GetDeleteRequestingMembers() {
				texts = append(texts, "[Join request denied]")
			}
			for _, a := range actions.GetPromoteRequestingMembers() {
				memberId := ""
				if gsp != nil {
					memberId = decryptGroupMemberUUID(gsp, a.GetUserId())
				}
				name := GetContactName(connId, memberId)
				texts = append(texts, "[Approved "+name+" to join]")
			}
			if actions.GetModifyAnnouncementsOnly() != nil {
				if actions.GetModifyAnnouncementsOnly().GetAnnouncementsOnly() {
					texts = append(texts, "[Enabled announcements only]")
				} else {
					texts = append(texts, "[Disabled announcements only]")
				}
			}
		}
	}

	if len(texts) == 0 {
		texts = append(texts, "[GroupUpdate]")
	}

	text := strings.Join(texts, "\n")
	msgId := fmt.Sprintf("%d", msg.GetTimestamp())
	isRead := fromMe

	CSgNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), "", "", "", FileStatusNone, timeSent,
		BoolToInt(isRead), 0)
	TrackRecentMessage(connId, chatId, senderId, msg.GetTimestamp())
}

func (handler *SgEventHandler) handleTypingMessage(chatId string, senderId string, msg *signalpb.TypingMessage) {
	LOG_TRACE(fmt.Sprintf("handleTypingMessage"))
	connId := handler.connId

	isTyping := msg.GetAction() == signalpb.TypingMessage_STARTED
	CSgNewTypingNotify(connId, chatId, senderId, BoolToInt(isTyping))
}

func (handler *SgEventHandler) handleEditMessage(chatId string, senderId string, fromMe bool, timeSent int, msg *signalpb.EditMessage) {
	LOG_TRACE(fmt.Sprintf("handleEditMessage"))
	connId := handler.connId

	dataMsg := msg.GetDataMessage()
	if dataMsg == nil {
		LOG_WARNING("edit message has no data message")
		return
	}

	text := dataMsg.GetBody()
	text = ProcessFormattedText(connId, text, dataMsg.GetBodyRanges())
	quotedId := ""
	if dataMsg.GetQuote() != nil {
		quotedId = fmt.Sprintf("%d", dataMsg.GetQuote().GetId())
	}

	fileId := ""
	filePath := ""
	fileStatus := FileStatusNone

	msgId := fmt.Sprintf("%d", msg.GetTargetSentTimestamp())
	isRead := fromMe
	isEdited := true

	// Reset typing status when receiving a message from someone else
	if !fromMe {
		CSgNewTypingNotify(connId, chatId, senderId, 0)
	}

	LOG_TRACE(fmt.Sprintf("Call CSgNewMessagesNotify (edit) %s: %s", chatId, text))
	CSgNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, fileId, filePath, fileStatus, timeSent,
		BoolToInt(isRead), BoolToInt(isEdited))
}

func (handler *SgEventHandler) handleReaction(chatId string, senderId string, fromMe bool, reaction *signalpb.DataMessage_Reaction) {
	LOG_TRACE(fmt.Sprintf("handleReaction"))
	connId := handler.connId

	emoji := reaction.GetEmoji()
	if reaction.GetRemove() {
		emoji = ""
	}
	msgId := fmt.Sprintf("%d", reaction.GetTargetSentTimestamp())

	CSgNewMessageReactionNotify(connId, chatId, msgId, senderId, emoji, BoolToInt(fromMe))
}

func (handler *SgEventHandler) handleReceipt(evt *events.Receipt) bool {
	LOG_TRACE(fmt.Sprintf("handleReceipt"))
	connId := handler.connId

	receipt := evt.Content
	if receipt.GetType() == signalpb.ReceiptMessage_READ {
		senderId := UUIDToString(evt.Sender)
		for _, timestamp := range receipt.GetTimestamp() {
			msgId := fmt.Sprintf("%d", timestamp)
			// For read receipts, we need the chatId which we derive from the sender
			chatId := senderId
			isRead := true
			LOG_TRACE(fmt.Sprintf("Call CSgNewMessageStatusNotify read"))
			CSgNewMessageStatusNotify(connId, chatId, msgId, BoolToInt(isRead))
		}
	}
	return true
}

func (handler *SgEventHandler) handleReadSelf(evt *events.ReadSelf) bool {
	LOG_TRACE(fmt.Sprintf("handleReadSelf"))
	connId := handler.connId

	for _, read := range evt.Messages {
		senderACI := read.GetSenderAci()
		if senderACI != "" {
			chatId := senderACI
			msgId := fmt.Sprintf("%d", read.GetTimestamp())
			isRead := true
			LOG_TRACE(fmt.Sprintf("Call CSgNewMessageStatusNotify self-read"))
			CSgNewMessageStatusNotify(connId, chatId, msgId, BoolToInt(isRead))
		}
	}
	return true
}

func (handler *SgEventHandler) handleContactList(evt *events.ContactList) bool {
	LOG_TRACE(fmt.Sprintf("handleContactList"))
	connId := handler.connId

	device := GetDevice(connId)
	selfId := UUIDToString(device.ACI)

	// @todo: try synchronize contact updates in different handlers
	var notify int = NotifyDirect //NotifyCache
	for _, contact := range evt.Contacts {
		//for i, contact := range evt.Contacts {
		//if i == len(evt.Contacts)-1 {
		//	notify = NotifySendCached
		//}

		contactId := UUIDToString(contact.ACI)
		if contactId == selfId {
			continue
		}

		name := contact.ContactName
		if name == "" {
			name = contact.Profile.Name
		}
		if name == "" {
			name = contact.E164
		}
		phone := contact.E164
		isSelf := false
		isAlias := false

		LOG_TRACE(fmt.Sprintf("Call CSgNewContactsNotify %s %s", contactId, name))
		CSgNewContactsNotify(connId, contactId, name, phone, BoolToInt(isSelf), BoolToInt(isAlias), notify)
		AddContactName(connId, contactId, name)
	}

	// Add self as contact
	selfName := "You"
	selfPhone := device.Number
	isSelf := BoolToInt(true)
	isAlias := BoolToInt(false)
	//notify = NotifySendCached
	LOG_TRACE(fmt.Sprintf("Call CSgNewContactsNotify self %s %s", selfId, selfName))
	CSgNewContactsNotify(connId, selfId, selfName, selfPhone, isSelf, isAlias, notify)
	AddContactName(connId, selfId, selfName)

	return true
}

func (handler *SgEventHandler) handleDeleteForMe(evt *events.DeleteForMe) bool {
	LOG_TRACE(fmt.Sprintf("handleDeleteForMe"))
	connId := handler.connId

	// Handle message deletions
	for _, msgDelete := range evt.GetMessageDeletes() {
		conv := msgDelete.GetConversation()
		if conv == nil {
			continue
		}

		var chatIdStr string
		if conv.GetThreadServiceId() != "" {
			chatIdStr = conv.GetThreadServiceId()
		} else if len(conv.GetThreadGroupId()) > 0 {
			chatIdStr = string(conv.GetThreadGroupId())
		} else if conv.GetThreadE164() != "" {
			chatIdStr = conv.GetThreadE164()
		} else {
			continue
		}

		for _, msg := range msgDelete.GetMessages() {
			msgId := fmt.Sprintf("%d", msg.GetSentTimestamp())
			LOG_TRACE(fmt.Sprintf("Call CSgDeleteMessageNotify %s %s", chatIdStr, msgId))
			CSgDeleteMessageNotify(connId, chatIdStr, msgId)
		}
	}

	// Handle conversation/chat deletions
	for _, convDelete := range evt.GetConversationDeletes() {
		conv := convDelete.GetConversation()
		if conv == nil {
			continue
		}

		var chatIdStr string
		if conv.GetThreadServiceId() != "" {
			chatIdStr = conv.GetThreadServiceId()
		} else if len(conv.GetThreadGroupId()) > 0 {
			chatIdStr = string(conv.GetThreadGroupId())
		} else if conv.GetThreadE164() != "" {
			chatIdStr = conv.GetThreadE164()
		} else {
			continue
		}

		// Delete chat and its messages from local BackupStore
		client := GetClient(connId)
		if client != nil && client.Store.BackupStore != nil {
			ctx := context.TODO()
			var backupChat *store.BackupChat
			chatUUID := StringToUUID(chatIdStr)
			if chatUUID != uuid.Nil {
				backupChat, _ = client.Store.BackupStore.GetBackupChatByUserID(ctx, libsignalgo.NewACIServiceID(chatUUID))
			} else {
				backupChat, _ = client.Store.BackupStore.GetBackupChatByGroupID(ctx, types.GroupIdentifier(chatIdStr))
			}
			if backupChat != nil {
				err := client.Store.BackupStore.DeleteBackupChatItems(ctx, backupChat.Id, time.Time{})
				if err != nil {
					LOG_WARNING(fmt.Sprintf("delete backup chat items failed: %v", err))
				}
				err = client.Store.BackupStore.DeleteBackupChat(ctx, backupChat.Id)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("delete backup chat failed: %v", err))
				}
			}
		}

		LOG_TRACE(fmt.Sprintf("Call CSgDeleteChatNotify %s", chatIdStr))
		CSgDeleteChatNotify(connId, chatIdStr)
	}

	return true
}

func (handler *SgEventHandler) handleLoggedOut(evt *events.LoggedOut) bool {
	LOG_INFO("logged out by server, reinit")
	connId := handler.connId

	CSgClearStatus(connId, FlagOnline)
	CSgSetStatus(connId, FlagOffline)

	LOG_TRACE(fmt.Sprintf("Call CSgReinit"))
	CSgReinit(connId)

	return true
}

func (handler *SgEventHandler) handlePinnedConversationsChanged(evt *events.PinnedConversationsChanged) bool {
	LOG_TRACE(fmt.Sprintf("handlePinnedConversationsChanged"))
	connId := handler.connId

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return true
	}

	ctx := context.TODO()

	// Build new pinned set from the event
	newPinned := make(map[string]bool)
	for i, pc := range evt.PinnedConversations {
		chatId := ""
		switch id := pc.GetIdentifier().(type) {
		case *signalpb.AccountRecord_PinnedConversation_Contact_:
			serviceId := id.Contact.GetServiceId()
			if serviceId != "" {
				chatId = serviceId
			}
		case *signalpb.AccountRecord_PinnedConversation_GroupMasterKey:
			masterKeyBytes := id.GroupMasterKey
			if len(masterKeyBytes) == libsignalgo.GroupMasterKeyLength {
				serializedKey := types.SerializedGroupMasterKey(base64.StdEncoding.EncodeToString(masterKeyBytes))
				groupID, err := client.StoreMasterKey(ctx, serializedKey)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("store group master key error: %v", err))
					continue
				}
				chatId = string(groupID)
			}
		case *signalpb.AccountRecord_PinnedConversation_LegacyGroupId:
			LOG_TRACE("skipping legacy group id in pinned conversations")
			continue
		}

		if chatId == "" {
			continue
		}

		newPinned[chatId] = true
		LOG_TRACE(fmt.Sprintf("pinned conversation %d: %s", i, chatId))
	}

	// Get previous pinned set
	mx.Lock()
	oldPinned := pinnedChats[connId]
	if oldPinned == nil {
		oldPinned = make(map[string]bool)
	}
	pinnedChats[connId] = newPinned
	mx.Unlock()

	// Detect newly pinned chats
	order := 1
	for chatId := range newPinned {
		if !oldPinned[chatId] {
			LOG_TRACE(fmt.Sprintf("pin chat %s order %d", chatId, order))
			CSgUpdatePinNotify(connId, chatId, 1, order)
		}
		order++
	}

	// Detect newly unpinned chats
	for chatId := range oldPinned {
		if !newPinned[chatId] {
			LOG_TRACE(fmt.Sprintf("unpin chat %s", chatId))
			CSgUpdatePinNotify(connId, chatId, 0, 0)
		}
	}

	return true
}

func (handler *SgEventHandler) handleChatMuteChanged(evt *events.ChatMuteChanged) bool {
	LOG_TRACE(fmt.Sprintf("handleChatMuteChanged %s muted=%d", evt.ChatID, evt.MutedUntilTimestamp))
	connId := handler.connId
	chatId := evt.ChatID
	isMuted := evt.MutedUntilTimestamp > 0

	mx.Lock()
	wasMuted := mutedChats[connId][chatId]
	mutedChats[connId][chatId] = isMuted
	mx.Unlock()

	if isMuted != wasMuted {
		LOG_TRACE(fmt.Sprintf("mute changed %s: %v -> %v", chatId, wasMuted, isMuted))
		CSgUpdateMuteNotify(connId, chatId, BoolToInt(isMuted))
	}

	return true
}

func SgInit(path string, proxy string) int {
	LOG_DEBUG("init " + filepath.Base(path))

	// create tmp dir
	var tmpPath string = path + "/tmp"
	tmpErr := os.MkdirAll(tmpPath, os.ModePerm)
	if tmpErr != nil {
		LOG_WARNING(fmt.Sprintf("mkdir error %#v", tmpErr))
		return -1
	}

	ctx := context.TODO()
	dbPath := path + "/signal.db"
	sqlAddress := fmt.Sprintf("file:%s?_foreign_keys=on", dbPath)
	db, dbErr := sql.Open("sqlite3", sqlAddress)
	if dbErr != nil {
		LOG_WARNING(fmt.Sprintf("sqlite open error %#v", dbErr))
		return -1
	}

	rawDB, rawErr := dbutil.NewWithDB(db, "sqlite3")
	if rawErr != nil {
		LOG_WARNING(fmt.Sprintf("dbutil error %#v", rawErr))
		return -1
	}

	dbLog := NcDbLogger()
	container := store.NewStore(rawDB, dbLog)

	upgradeErr := container.Upgrade(ctx)
	if upgradeErr != nil {
		LOG_WARNING(fmt.Sprintf("upgrade error %#v", upgradeErr))
		return -1
	}

	// Try to get existing device
	devices, devErr := container.GetAllDevices(ctx)
	if devErr != nil {
		LOG_WARNING(fmt.Sprintf("get devices error %#v", devErr))
		return -1
	}

	var device *store.Device
	if len(devices) > 0 {
		device = devices[0]
		LOG_DEBUG("found existing device")
	}

	// Create logger for signalmeow
	logger := zerolog.New(&ncLogger{}).With().Timestamp().Logger()

	// Create event handler
	eventHandler := func(evt events.SignalEvent) bool {
		return true // Will be replaced after AddConn
	}

	// Create client
	var client *signalmeow.Client
	if device != nil {
		client = signalmeow.NewClient(device, logger, eventHandler)
	}

	// Store connection and get id
	var connId int = AddConn(client, device, container, path)

	// Now set up the real event handler
	if client != nil {
		handler := GetHandler(connId)
		client.EventHandler = handler.HandleEvent
	}

	LOG_DEBUG("connId " + strconv.Itoa(connId))

	return connId
}

func SgLogin(connId int) int {
	LOG_DEBUG("login " + strconv.Itoa(connId) + " signalmeow " + strconv.Itoa(signalDate))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	path := GetPath(connId)
	client := GetClient(connId)
	device := GetDevice(connId)
	container := GetContainer(connId)

	ctx := context.TODO()

	justProvisioned := false

	// Check if we need to provision (new device)
	if device == nil || !device.IsDeviceLoggedIn() {
		LOG_INFO("device not logged in, starting provisioning")

		SetState(connId, Connecting)

		LOG_TRACE(fmt.Sprintf("acquire console"))
		CSgSetProtocolUiControl(connId, 1)

		fmt.Printf("\n")
		fmt.Printf("Open Signal on your phone, go to Settings > Linked devices.\n")
		fmt.Printf("Click on \"Link a device\", unlock the phone and aim its camera at the\n")
		fmt.Printf("QR code displayed on the computer screen.\n")
		fmt.Printf("\n")
		fmt.Printf("Scan the QR code to authenticate, or press CTRL-C to abort.\n")

		deviceName := GetDeviceName()
		allowBackup := true
		provChan := signalmeow.PerformProvisioning(ctx, container, deviceName, allowBackup)

		hasGUI := HasGUI()
		qrDisplayed := false

		for resp := range provChan {
			switch resp.State {
			case signalmeow.StateProvisioningURLReceived:
				LOG_DEBUG("provisioning URL received")
				if hasGUI {
					qrPath := path + "/tmp/qr.png"
					qrcode.WriteFile(resp.ProvisioningURL, qrcode.Medium, 512, qrPath)
					ShowImage(qrPath)
				} else {
					qrterminal.GenerateHalfBlock(resp.ProvisioningURL, qrterminal.L, os.Stdout)
				}
				qrDisplayed = true

			case signalmeow.StateProvisioningDataReceived:
				LOG_DEBUG("provisioning data received")
				// Get the new device
				newDevice, devErr := container.DeviceByACI(ctx, resp.ProvisioningData.ACI)
				if devErr != nil || newDevice == nil {
					LOG_WARNING(fmt.Sprintf("get device error %#v", devErr))
					CSgSetProtocolUiControl(connId, 0)
					return -1
				}

				// Update stored device
				mx.Lock()
				devices[connId] = newDevice
				mx.Unlock()
				device = newDevice

				// Create new client with the device
				logger := zerolog.New(&ncLogger{}).With().Timestamp().Logger()
				handler := GetHandler(connId)
				newClient := signalmeow.NewClient(newDevice, logger, handler.HandleEvent)

				mx.Lock()
				clients[connId] = newClient
				mx.Unlock()
				client = newClient

				LOG_INFO("provisioning successful")

			case signalmeow.StateProvisioningError:
				LOG_WARNING(fmt.Sprintf("provisioning error: %v", resp.Err))
				CSgSetProtocolUiControl(connId, 0)
				SetState(connId, Disconnected)
				return -1
			}
		}

		// delete temporary image file
		if qrDisplayed {
			_ = os.Remove(path + "/tmp/qr.png")
		}

		LOG_TRACE(fmt.Sprintf("release console"))
		CSgSetProtocolUiControl(connId, 0)

		justProvisioned = true
	}

	// Now connect the client
	if client == nil {
		LOG_WARNING("client is nil after provisioning")
		return -1
	}

	SetState(connId, Connecting)
	CSgSetStatus(connId, FlagConnecting)

	// Start websockets
	statusChan, wsErr := client.StartReceiveLoops(ctx)
	if wsErr != nil {
		LOG_WARNING(fmt.Sprintf("start websockets error %#v", wsErr))
		CSgClearStatus(connId, FlagConnecting)
		SetState(connId, Disconnected)
		return -1
	}

	SetStatusChan(connId, statusChan)

	// Wait for connection
	timeoutMs := 30000 // 30 sec timeout
	waitedMs := 0
	connected := false

	for waitedMs < timeoutMs {
		select {
		case status := <-statusChan:
			LOG_DEBUG(fmt.Sprintf("connection status: %v", status.Event))
			if status.Event == signalmeow.SignalConnectionEventConnected {
				connected = true
				break
			} else if status.Event == signalmeow.SignalConnectionEventLoggedOut {
				LOG_WARNING(fmt.Sprintf("connection failed: %v (clearing credentials for re-provisioning)", status.Event))
				if client != nil {
					client.ClearKeysAndDisconnect(ctx)
				}
				CSgClearStatus(connId, FlagConnecting)
				SetState(connId, Disconnected)
				return -1
			} else if status.Event == signalmeow.SignalConnectionEventError ||
				status.Event == signalmeow.SignalConnectionEventFatalError {
				LOG_WARNING(fmt.Sprintf("connection failed: %v", status.Event))
				CSgClearStatus(connId, FlagConnecting)
				SetState(connId, Disconnected)
				return -1
			}
		case <-time.After(100 * time.Millisecond):
			waitedMs += 100
		}
		if connected {
			break
		}
	}

	if !connected {
		LOG_WARNING("connection timeout")
		CSgClearStatus(connId, FlagConnecting)
		SetState(connId, Disconnected)
		return -1
	}

	SetState(connId, Connected)
	CSgSetStatus(connId, FlagOnline)
	CSgClearStatus(connId, FlagConnecting)

	// Monitor connection status in background
	go monitorConnectionStatus(connId, statusChan)

	// Fetch transfer archive after fresh provisioning to populate chat history
	if justProvisioned && client.Store.EphemeralBackupKey != nil {
		LOG_INFO("fetching transfer archive...")
		CSgSetStatus(connId, FlagSyncing)

		transferCtx, transferCancel := context.WithTimeout(ctx, 3*time.Minute)
		meta, waitErr := client.WaitForTransfer(transferCtx)
		transferCancel()

		if waitErr != nil {
			LOG_WARNING(fmt.Sprintf("wait for transfer: %v", waitErr))
		} else if meta != nil && meta.Error == "" {
			fetchErr := client.FetchAndProcessTransfer(ctx, meta)
			if fetchErr != nil {
				LOG_WARNING(fmt.Sprintf("process transfer: %v", fetchErr))
			} else {
				LOG_INFO("transfer archive processed")
			}
		} else if meta != nil {
			LOG_WARNING(fmt.Sprintf("transfer declined: %s", meta.Error))
		}

		CSgClearStatus(connId, FlagSyncing)
	}

	// Request contacts sync
	client.SyncContactsOnConnect = true
	client.SendContactSyncRequest(ctx)

	LOG_DEBUG("login ok")
	return 0
}

func monitorConnectionStatus(connId int, statusChan chan signalmeow.SignalConnectionStatus) {
	for status := range statusChan {
		LOG_DEBUG(fmt.Sprintf("connection status update: %v", status.Event))
		switch status.Event {
		case signalmeow.SignalConnectionEventDisconnected:
			LOG_WARNING("websocket disconnected")
			CSgClearStatus(connId, FlagOnline)
		case signalmeow.SignalConnectionEventConnected:
			LOG_DEBUG("websocket reconnected")
			CSgSetStatus(connId, FlagOnline)
		case signalmeow.SignalConnectionEventLoggedOut:
			LOG_WARNING("logged out while connected")
			CSgClearStatus(connId, FlagOnline)
			ctx := context.TODO()
			client := GetClient(connId)
			if client != nil {
				client.ClearKeysAndDisconnect(ctx)
			}
			return
		case signalmeow.SignalConnectionEventFatalError:
			LOG_WARNING(fmt.Sprintf("fatal connection error: %v", status.Err))
			CSgClearStatus(connId, FlagOnline)
			return
		}
	}
}

func SgLogout(connId int) int {
	LOG_DEBUG("logout " + strconv.Itoa(connId))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	client := GetClient(connId)
	if client != nil {
		client.StopReceiveLoops()
	}

	SetState(connId, Disconnected)

	LOG_DEBUG("logout ok")
	return 0
}

func SgCleanup(connId int) int {
	LOG_DEBUG("cleanup " + strconv.Itoa(connId))
	RemoveConn(connId)
	return 0
}

func SgGetVersion() int {
	return signalDate
}

func SgGetMessages(connId int, chatId string, limit int, fromMsgId string, owner int) int {
	LOG_TRACE("get messages " + strconv.Itoa(connId) + ", " + chatId + ", " + strconv.Itoa(limit) + ", " + fromMsgId)

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	if client.Store.BackupStore == nil {
		LOG_DEBUG("backup store not available")
		return -1
	}

	ctx := context.TODO()
	device := GetDevice(connId)
	selfACI := device.ACI

	// Resolve chatId (UUID string) to backup store chat
	chatUUID := StringToUUID(chatId)
	if chatUUID == uuid.Nil {
		LOG_WARNING(fmt.Sprintf("invalid chat UUID: %s", chatId))
		return -1
	}

	serviceID := libsignalgo.NewACIServiceID(chatUUID)
	backupChat, err := client.Store.BackupStore.GetBackupChatByUserID(ctx, serviceID)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("get backup chat error: %v", err))
		return -1
	}
	if backupChat == nil {
		LOG_DEBUG(fmt.Sprintf("no backup chat for %s", chatId))
		return -1
	}

	// Determine anchor time for pagination
	var anchor time.Time
	if fromMsgId != "" {
		ts, parseErr := strconv.ParseInt(fromMsgId, 10, 64)
		if parseErr == nil {
			anchor = time.UnixMilli(ts)
		}
	}

	// Fetch chat items (backward from anchor, newest first)
	items, err := client.Store.BackupStore.GetBackupChatItems(ctx, backupChat.Id, anchor, false, limit)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("get backup chat items error: %v", err))
		return -1
	}

	LOG_DEBUG(fmt.Sprintf("got %d backup messages for %s", len(items), chatId))

	// Send empty batch if no messages
	if len(items) == 0 {
		CSgNewHistoryMessagesNotify(connId, chatId, "", "", "", 0, "", "", "", FileStatusNone, 0, 1, 0, fromMsgId, NotifySendCached)
		return 0
	}

	// Build recipient ID -> UUID string cache
	recipientCache := make(map[uint64]string)

	for i, item := range items {
		notify := NotifyCache
		if i == len(items)-1 {
			notify = NotifySendCached
		}

		// Resolve author
		senderId := ""
		if cached, ok := recipientCache[item.AuthorId]; ok {
			senderId = cached
		} else {
			recipient, recipErr := client.Store.BackupStore.GetBackupRecipient(ctx, item.AuthorId)
			if recipErr == nil && recipient != nil {
				switch dest := recipient.Destination.(type) {
				case *backuppb.Recipient_Contact:
					aciBytes := dest.Contact.GetAci()
					if len(aciBytes) == 16 {
						senderId = UUIDToString(uuid.UUID(aciBytes))
					}
				case *backuppb.Recipient_Self:
					senderId = UUIDToString(selfACI)
				}
			}
			recipientCache[item.AuthorId] = senderId
		}

		// Determine direction
		fromMe := false
		isRead := true
		switch dir := item.DirectionalDetails.(type) {
		case *backuppb.ChatItem_Incoming:
			fromMe = false
			isRead = dir.Incoming.GetRead()
		case *backuppb.ChatItem_Outgoing:
			fromMe = true
			isRead = true
		}

		// Extract message content
		text := ""
		quotedId := ""
		fileId := ""
		filePath := ""
		fileStatus := FileStatusNone
		switch msg := item.Item.(type) {
		case *backuppb.ChatItem_StandardMessage:
			if msg.StandardMessage.GetText() != nil {
				text = msg.StandardMessage.GetText().GetBody()
				text = ProcessBackupFormattedText(connId, text, msg.StandardMessage.GetText().GetBodyRanges())
			}
			if msg.StandardMessage.GetQuote() != nil && msg.StandardMessage.GetQuote().TargetSentTimestamp != nil {
				quotedId = fmt.Sprintf("%d", msg.StandardMessage.GetQuote().GetTargetSentTimestamp())
			}
			// Handle attachments
			attachments := msg.StandardMessage.GetAttachments()
			if len(attachments) > 0 {
				fp := attachments[0].GetPointer()
				if fp != nil {
					loc := fp.GetLocatorInfo()
					if loc != nil && loc.GetTransitCdnKey() != "" {
						// Convert backup FilePointer to DownloadInfo for download
						ext := ExtensionByType(fp.GetContentType(), ".bin")
						fileName := fp.GetFileName()
						if fileName == "" {
							fileName = fmt.Sprintf("%d%s", item.DateSent, ext)
						}
						tmpPath := GetPath(connId) + "/tmp"
						targetPath := fmt.Sprintf("%s/%s", tmpPath, fileName)

						dlInfo := DownloadInfo{
							Version:       downloadInfoVersion,
							TargetPath:    targetPath,
							Key:           loc.GetKey(),
							Digest:        loc.GetEncryptedDigest(),
							PlaintextHash: loc.GetPlaintextHash(),
							Size:          loc.GetSize(),
							CdnKey:        loc.GetTransitCdnKey(),
							CdnNumber:     loc.GetTransitCdnNumber(),
						}

						bytes, jsonErr := json.Marshal(dlInfo)
						if jsonErr == nil {
							fileId = string(bytes)
							filePath = targetPath
							fileStatus = FileStatusNotDownloaded
						}
					}
				}
			}
		default:
			// Skip non-standard messages (stickers, updates, etc.)
			if notify == NotifySendCached {
				// Still need to send the batch even if last item is skipped
				CSgNewHistoryMessagesNotify(connId, chatId, "", "", "", 0, "", "", "", FileStatusNone, 0, 1, 0, fromMsgId, NotifySendCached)
			}
			continue
		}

		msgId := fmt.Sprintf("%d", item.DateSent)
		timeSent := int(item.DateSent / 1000)
		isEdited := len(item.GetRevisions()) > 0

		CSgNewHistoryMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId,
			fileId, filePath, fileStatus, timeSent, BoolToInt(isRead), BoolToInt(isEdited), fromMsgId, notify)
	}

	return 0
}

func SgSendMessage(connId int, chatId string, text string, quotedId string, quotedText string, quotedSender string, filePath string, fileType string, editMsgId string, editMsgSent int) int {
	LOG_TRACE("send message " + strconv.Itoa(connId) + ", " + chatId + ", " + text + ", " + quotedId + ", " + filePath + ", " + editMsgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()

	// Determine if DM or group
	recipientUUID := StringToUUID(chatId)
	isGroup := recipientUUID == uuid.Nil
	var groupID types.GroupIdentifier
	var recipientServiceID libsignalgo.ServiceID
	if isGroup {
		groupID = types.GroupIdentifier(chatId)
	} else {
		recipientServiceID = libsignalgo.NewACIServiceID(recipientUUID)
	}

	// Create data message
	timestamp := uint64(time.Now().UnixMilli())
	dataMsg := &signalpb.DataMessage{
		Timestamp: proto.Uint64(timestamp),
	}

	// Handle edit vs new message
	if editMsgId != "" {
		targetTimestamp, err := strconv.ParseUint(editMsgId, 10, 64)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("invalid edit msg id: %s", editMsgId))
			return -1
		}

		plainText, mdBodyRanges := ParseMarkdown(text)
		dataMsg.Body = proto.String(plainText)
		if len(mdBodyRanges) > 0 {
			dataMsg.BodyRanges = mdBodyRanges
		}

		editMsg := &signalpb.EditMessage{
			TargetSentTimestamp: proto.Uint64(targetTimestamp),
			DataMessage:         dataMsg,
		}

		content := &signalpb.Content{
			EditMessage: editMsg,
		}

		if isGroup {
			groupResult, err := client.SendGroupMessage(ctx, groupID, content)
			if err != nil || len(groupResult.SuccessfullySentTo) == 0 {
				LOG_WARNING(fmt.Sprintf("send group edit message failed: %v", err))
				return -1
			}
		} else {
			result := client.SendMessage(ctx, recipientServiceID, content)
			if !result.WasSuccessful {
				LOG_WARNING(fmt.Sprintf("send edit message failed"))
				return -1
			}
		}

		// Echo edited message back to self
		device := GetDevice(connId)
		selfId := UUIDToString(device.ACI)
		timeSent := int(timestamp / 1000)
		CSgNewMessagesNotify(connId, chatId, strconv.FormatUint(timestamp, 10), selfId, text, 1, quotedId, "", "", 0, timeSent, 1, 1)
		TrackRecentMessage(connId, chatId, selfId, timestamp)
	} else {
		// Set body
		if text != "" {
			plainText, mdBodyRanges := ParseMarkdown(text)
			dataMsg.Body = proto.String(plainText)
			if len(mdBodyRanges) > 0 {
				dataMsg.BodyRanges = mdBodyRanges
			}
		}

		// Handle quote
		if quotedId != "" {
			quotedTimestamp, err := strconv.ParseUint(quotedId, 10, 64)
			if err == nil {
				quotedAuthorUUID := StringToUUID(quotedSender)
				quotedAuthorACI := ""
				if quotedAuthorUUID != uuid.Nil {
					quotedAuthorACI = quotedAuthorUUID.String()
				}
				dataMsg.Quote = &signalpb.DataMessage_Quote{
					Id:        proto.Uint64(quotedTimestamp),
					AuthorAci: proto.String(quotedAuthorACI),
					Text:      proto.String(quotedText),
				}
			}
		}

		// Handle attachment
		if filePath != "" {
			fileData, readErr := os.ReadFile(filePath)
			if readErr == nil {
				attachment, uploadErr := client.UploadAttachment(ctx, fileData)
				if uploadErr != nil {
					LOG_WARNING(fmt.Sprintf("upload attachment error %#v", uploadErr))
				} else {
					if fileType != "" {
						attachment.ContentType = proto.String(fileType)
					}
					attachment.FileName = proto.String(filepath.Base(filePath))
					dataMsg.Attachments = []*signalpb.AttachmentPointer{attachment}
				}
			} else {
				LOG_WARNING(fmt.Sprintf("file not found: %s", filePath))
			}
		}

		content := &signalpb.Content{
			DataMessage: dataMsg,
		}

		if isGroup {
			groupResult, err := client.SendGroupMessage(ctx, groupID, content)
			if err != nil || len(groupResult.SuccessfullySentTo) == 0 {
				LOG_WARNING(fmt.Sprintf("send group message failed: %v", err))
				return -1
			}
		} else {
			result := client.SendMessage(ctx, recipientServiceID, content)
			if !result.WasSuccessful {
				LOG_WARNING(fmt.Sprintf("send message failed"))
				return -1
			}
		}

		// Echo sent message back to self
		device := GetDevice(connId)
		selfId := UUIDToString(device.ACI)
		timeSent := int(timestamp / 1000)
		echoFileId := ""
		echoFilePath := ""
		echoFileStatus := FileStatusNone
		if len(dataMsg.GetAttachments()) > 0 {
			echoFileId = AttachmentToFileId(dataMsg.GetAttachments()[0], filePath)
			echoFilePath = filePath
			echoFileStatus = FileStatusDownloaded
		}
		CSgNewMessagesNotify(connId, chatId, strconv.FormatUint(timestamp, 10), selfId, text, 1, quotedId, echoFileId, echoFilePath, echoFileStatus, timeSent, 0, 0)
		TrackRecentMessage(connId, chatId, selfId, timestamp)
	}

	LOG_TRACE("send message ok")
	return 0
}

func SgGetContacts(connId int) int {
	LOG_TRACE("get contacts " + strconv.Itoa(connId))

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()
	client.SendContactSyncRequest(ctx)

	return 0
}

func SgGetChats(connId int) int {
	LOG_TRACE("get chats " + strconv.Itoa(connId))

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()
	CSgSetStatus(connId, FlagFetching)
	defer CSgClearStatus(connId, FlagFetching)

	device := GetDevice(connId)
	selfACI := device.ACI

	// Fetch chats from backup store (1:1 and group chats)
	if client.Store.BackupStore != nil {
		chats, err := client.Store.BackupStore.GetBackupChats(ctx)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("get backup chats error: %v", err))
		} else {
			LOG_DEBUG(fmt.Sprintf("got %d backup chats", len(chats)))

			for _, chat := range chats {
				recipient, err := client.Store.BackupStore.GetBackupRecipient(ctx, chat.RecipientId)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("get recipient %d error: %v", chat.RecipientId, err))
					continue
				}
				if recipient == nil {
					continue
				}

				var chatId string
				switch dest := recipient.Destination.(type) {
				case *backuppb.Recipient_Contact:
					aciBytes := dest.Contact.GetAci()
					if len(aciBytes) != 16 {
						continue
					}
					chatId = UUIDToString(uuid.UUID(aciBytes))
				case *backuppb.Recipient_Self:
					chatId = UUIDToString(selfACI)
				case *backuppb.Recipient_Group:
					masterKeyBytes := dest.Group.GetMasterKey()
					if len(masterKeyBytes) == 0 {
						continue
					}
					serializedKey := types.SerializedGroupMasterKey(base64.StdEncoding.EncodeToString(masterKeyBytes))
					groupID, err := client.StoreMasterKey(ctx, serializedKey)
					if err != nil {
						LOG_WARNING(fmt.Sprintf("store group master key error: %v", err))
						continue
					}
					chatId = string(groupID)
				default:
					continue
				}

				if chatId == "" || chatId == uuid.Nil.String() {
					continue
				}

				isUnread := BoolToInt(chat.GetMarkedUnread())
				isMuted := 0
				if chat.GetMuteUntilMs() > 0 {
					isMuted = 1
				}
				isPinned := 0
				if chat.GetPinnedOrder() > 0 {
					isPinned = 1
				}
				lastMessageTime := int(chat.LatestMessageID / 1000)

				LOG_TRACE(fmt.Sprintf("Chat %s: unread=%d muted=%d pinned=%d time=%d", chatId, isUnread, isMuted, isPinned, lastMessageTime))
				CSgNewChatsNotify(connId, chatId, isUnread, isMuted, isPinned, lastMessageTime)
			}
		}
	}

	// Fetch group names from server for all known groups
	groupIDs, err := client.Store.GroupStore.AllGroupIdentifiers(ctx)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("get all group identifiers error: %v", err))
	} else {
		LOG_DEBUG(fmt.Sprintf("got %d groups", len(groupIDs)))
		for _, gid := range groupIDs {
			group, _, err := client.RetrieveGroupByID(ctx, gid, 0)
			if err != nil {
				LOG_WARNING(fmt.Sprintf("retrieve group %s error: %v", gid, err))
				continue
			}
			if group != nil && group.Title != "" {
				chatId := string(gid)
				LOG_TRACE(fmt.Sprintf("Group %s: %s", chatId, group.Title))
				CSgNewContactsNotify(connId, chatId, group.Title, "", BoolToInt(false), BoolToInt(false), NotifyDirect)
				AddContactName(connId, chatId, group.Title)
			}
		}
	}

	return 0
}

func SgMarkMessageRead(connId int, chatId string, senderId string, msgId string) int {
	LOG_TRACE("mark message read " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()

	// Parse sender UUID
	senderUUID := StringToUUID(senderId)
	if senderUUID == uuid.Nil {
		LOG_WARNING(fmt.Sprintf("invalid sender UUID: %s", senderId))
		return -1
	}

	senderServiceID := libsignalgo.NewACIServiceID(senderUUID)

	// Parse message timestamp
	timestamp, err := strconv.ParseUint(msgId, 10, 64)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("invalid msg id: %s", msgId))
		return -1
	}

	// Send read receipt
	receipt := &signalpb.ReceiptMessage{
		Type:      signalpb.ReceiptMessage_READ.Enum(),
		Timestamp: []uint64{timestamp},
	}

	content := &signalpb.Content{
		ReceiptMessage: receipt,
	}

	result := client.SendMessage(ctx, senderServiceID, content)
	if !result.WasSuccessful {
		LOG_WARNING("send read receipt failed")
		return -1
	}

	return 0
}

func chatIdToConversationIdentifier(chatId string) *signalpb.ConversationIdentifier {
	recipientUUID := StringToUUID(chatId)
	if recipientUUID != uuid.Nil {
		// DM chat
		return &signalpb.ConversationIdentifier{
			Identifier: &signalpb.ConversationIdentifier_ThreadServiceId{
				ThreadServiceId: chatId,
			},
		}
	}
	// Group chat - convert base64 group identifier to raw bytes
	groupID := types.GroupIdentifier(chatId)
	rawBytes, err := groupID.Bytes()
	if err != nil {
		LOG_WARNING(fmt.Sprintf("invalid group identifier: %v", err))
		return nil
	}
	return &signalpb.ConversationIdentifier{
		Identifier: &signalpb.ConversationIdentifier_ThreadGroupId{
			ThreadGroupId: rawBytes[:],
		},
	}
}

func SgDeleteMessage(connId int, chatId string, senderId string, msgId string) int {
	LOG_TRACE("delete message " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	conv := chatIdToConversationIdentifier(chatId)
	if conv == nil {
		return -1
	}

	timestamp, err := strconv.ParseUint(msgId, 10, 64)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("invalid msg id: %s", msgId))
		return -1
	}

	ctx := context.TODO()
	device := GetDevice(connId)
	selfACI := device.ACI
	selfServiceID := libsignalgo.NewACIServiceID(selfACI)

	// If the message is from us and within 24 hours, send "delete for everyone"
	// @todo: consider ui warning dialog enabled by ProtocolFeature for deletes > 24h
	ageMs := uint64(time.Now().UnixMilli()) - timestamp
	if senderId == UUIDToString(selfACI) && ageMs <= 24*60*60*1000 {
		LOG_TRACE("delete for everyone: own message")
		deleteContent := &signalpb.Content{
			DataMessage: &signalpb.DataMessage{
				Timestamp: proto.Uint64(uint64(time.Now().UnixMilli())),
				Delete: &signalpb.DataMessage_Delete{
					TargetSentTimestamp: proto.Uint64(timestamp),
				},
			},
		}

		recipientUUID := StringToUUID(chatId)
		if recipientUUID != uuid.Nil {
			result := client.SendMessage(ctx, libsignalgo.NewACIServiceID(recipientUUID), deleteContent)
			if !result.WasSuccessful {
				LOG_WARNING("send delete-for-everyone failed")
				return -1
			}
		} else {
			groupID := types.GroupIdentifier(chatId)
			groupResult, err := client.SendGroupMessage(ctx, groupID, deleteContent)
			if err != nil || len(groupResult.SuccessfullySentTo) == 0 {
				LOG_WARNING(fmt.Sprintf("send group delete-for-everyone failed: %v", err))
				return -1
			}
		}
	}

	// Also send "delete for me" sync to own devices
	syncContent := &signalpb.Content{
		SyncMessage: &signalpb.SyncMessage{
			DeleteForMe: &signalpb.SyncMessage_DeleteForMe{
				MessageDeletes: []*signalpb.SyncMessage_DeleteForMe_MessageDeletes{
					{
						Conversation: conv,
						Messages: []*signalpb.AddressableMessage{
							{
								Author: &signalpb.AddressableMessage_AuthorServiceId{
									AuthorServiceId: senderId,
								},
								SentTimestamp: proto.Uint64(timestamp),
							},
						},
					},
				},
			},
		},
	}

	result := client.SendMessage(ctx, selfServiceID, syncContent)
	if !result.WasSuccessful {
		LOG_WARNING("send delete message sync failed")
		return -1
	}

	return 0
}

func SgDeleteChat(connId int, chatId string) int {
	LOG_TRACE("delete chat " + strconv.Itoa(connId) + ", " + chatId)

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	conv := chatIdToConversationIdentifier(chatId)
	if conv == nil {
		return -1
	}

	ctx := context.TODO()
	device := GetDevice(connId)
	selfACI := device.ACI

	// Fetch up to 5 most recent messages as anchor points for the receiving device
	var mostRecentMessages []*signalpb.AddressableMessage
	var backupChat *store.BackupChat
	if client.Store.BackupStore != nil {
		chatUUID := StringToUUID(chatId)
		if chatUUID != uuid.Nil {
			backupChat, _ = client.Store.BackupStore.GetBackupChatByUserID(ctx, libsignalgo.NewACIServiceID(chatUUID))
		} else {
			backupChat, _ = client.Store.BackupStore.GetBackupChatByGroupID(ctx, types.GroupIdentifier(chatId))
		}

		if backupChat != nil {
			items, err := client.Store.BackupStore.GetBackupChatItems(ctx, backupChat.Id, time.Time{}, false, 5)
			if err == nil {
				recipientCache := make(map[uint64]string)
				for _, item := range items {
					authorACI := ""
					if cached, ok := recipientCache[item.AuthorId]; ok {
						authorACI = cached
					} else {
						recipient, recipErr := client.Store.BackupStore.GetBackupRecipient(ctx, item.AuthorId)
						if recipErr == nil && recipient != nil {
							switch dest := recipient.Destination.(type) {
							case *backuppb.Recipient_Contact:
								aciBytes := dest.Contact.GetAci()
								if len(aciBytes) == 16 {
									authorACI = UUIDToString(uuid.UUID(aciBytes))
								}
							case *backuppb.Recipient_Self:
								authorACI = UUIDToString(selfACI)
							}
						}
						recipientCache[item.AuthorId] = authorACI
					}

					if authorACI != "" {
						mostRecentMessages = append(mostRecentMessages, &signalpb.AddressableMessage{
							Author: &signalpb.AddressableMessage_AuthorServiceId{
								AuthorServiceId: authorACI,
							},
							SentTimestamp: proto.Uint64(uint64(item.DateSent)),
						})
					}
				}
			}
		}
	}

	// Fallback to real-time tracked messages if BackupStore had nothing
	if len(mostRecentMessages) == 0 {
		tracked := GetRecentMessages(connId, chatId)
		for _, msg := range tracked {
			mostRecentMessages = append(mostRecentMessages, &signalpb.AddressableMessage{
				Author: &signalpb.AddressableMessage_AuthorServiceId{
					AuthorServiceId: msg.senderACI,
				},
				SentTimestamp: proto.Uint64(msg.timestamp),
			})
		}
	}

	LOG_TRACE(fmt.Sprintf("delete chat %s with %d anchor messages", chatId, len(mostRecentMessages)))

	content := &signalpb.Content{
		SyncMessage: &signalpb.SyncMessage{
			DeleteForMe: &signalpb.SyncMessage_DeleteForMe{
				ConversationDeletes: []*signalpb.SyncMessage_DeleteForMe_ConversationDelete{
					{
						Conversation:       conv,
						MostRecentMessages: mostRecentMessages,
						IsFullDelete:       proto.Bool(true),
					},
				},
			},
		},
	}

	selfServiceID := libsignalgo.NewACIServiceID(selfACI)
	result := client.SendMessage(ctx, selfServiceID, content)
	if !result.WasSuccessful {
		LOG_WARNING("send delete chat sync failed")
		return -1
	}

	// Delete chat and its messages from local BackupStore
	if client.Store.BackupStore != nil && backupChat != nil {
		err := client.Store.BackupStore.DeleteBackupChatItems(ctx, backupChat.Id, time.Time{})
		if err != nil {
			LOG_WARNING(fmt.Sprintf("delete backup chat items failed: %v", err))
		}
		err = client.Store.BackupStore.DeleteBackupChat(ctx, backupChat.Id)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("delete backup chat failed: %v", err))
		}
	}

	return 0
}

func SgSendTyping(connId int, chatId string, isTyping int) int {
	LOG_TRACE("send typing " + strconv.Itoa(connId) + ", " + chatId + ", " + strconv.Itoa(isTyping))

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()

	// Create typing message
	action := signalpb.TypingMessage_STARTED
	if isTyping == 0 {
		action = signalpb.TypingMessage_STOPPED
	}

	typingMsg := &signalpb.TypingMessage{
		Timestamp: proto.Uint64(uint64(time.Now().UnixMilli())),
		Action:    action.Enum(),
	}

	content := &signalpb.Content{
		TypingMessage: typingMsg,
	}

	// Determine if DM or group
	recipientUUID := StringToUUID(chatId)
	if recipientUUID == uuid.Nil {
		// Group chat
		groupID := types.GroupIdentifier(chatId)
		_, err := client.SendGroupMessage(ctx, groupID, content)
		if err != nil {
			LOG_WARNING(fmt.Sprintf("send group typing failed: %v", err))
			return -1
		}
	} else {
		// DM chat
		recipientServiceID := libsignalgo.NewACIServiceID(recipientUUID)
		result := client.SendMessage(ctx, recipientServiceID, content)
		if !result.WasSuccessful {
			LOG_WARNING("send typing failed")
			return -1
		}
	}

	return 0
}

func SgDownloadFile(connId int, chatId string, msgId string, fileId string, action int) int {
	LOG_TRACE("download file " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	filePath, fileStatus := DownloadFromFileId(connId, fileId)

	LOG_TRACE(fmt.Sprintf("Call CSgNewMessageFileNotify"))
	CSgNewMessageFileNotify(connId, chatId, msgId, filePath, fileStatus, action)

	return 0
}

func SgSendReaction(connId int, chatId string, senderId string, msgId string, emoji string, prevEmoji string) int {
	LOG_TRACE("send reaction " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId + ", " + emoji + ", prev " + prevEmoji)

	client := GetClient(connId)
	if client == nil {
		LOG_WARNING("client is nil")
		return -1
	}

	ctx := context.TODO()

	// Parse target message timestamp
	targetTimestamp, err := strconv.ParseUint(msgId, 10, 64)
	if err != nil {
		LOG_WARNING(fmt.Sprintf("invalid msg id: %s", msgId))
		return -1
	}

	// Parse target author
	targetAuthorUUID := StringToUUID(senderId)
	targetAuthorACI := ""
	if targetAuthorUUID != uuid.Nil {
		targetAuthorACI = targetAuthorUUID.String()
	}

	// Create reaction
	remove := emoji == ""
	reactionEmoji := emoji
	if remove {
		reactionEmoji = prevEmoji
	}
	reaction := &signalpb.DataMessage_Reaction{
		Emoji:               proto.String(reactionEmoji),
		Remove:              proto.Bool(remove),
		TargetAuthorAci:     proto.String(targetAuthorACI),
		TargetSentTimestamp: proto.Uint64(targetTimestamp),
	}

	dataMsg := &signalpb.DataMessage{
		Timestamp: proto.Uint64(uint64(time.Now().UnixMilli())),
		Reaction:  reaction,
	}

	content := &signalpb.Content{
		DataMessage: dataMsg,
	}

	// Determine if DM or group
	recipientUUID := StringToUUID(chatId)
	if recipientUUID == uuid.Nil {
		// Group chat
		groupID := types.GroupIdentifier(chatId)
		groupResult, err := client.SendGroupMessage(ctx, groupID, content)
		if err != nil || len(groupResult.SuccessfullySentTo) == 0 {
			LOG_WARNING(fmt.Sprintf("send group reaction failed: %v", err))
			return -1
		}
	} else {
		// DM chat
		recipientServiceID := libsignalgo.NewACIServiceID(recipientUUID)
		result := client.SendMessage(ctx, recipientServiceID, content)
		if !result.WasSuccessful {
			LOG_WARNING("send reaction failed")
			return -1
		}
	}

	// Echo reaction back to self
	device := GetDevice(connId)
	selfId := UUIDToString(device.ACI)
	CSgNewMessageReactionNotify(connId, chatId, msgId, selfId, emoji, 1)

	return 0
}

func main() {
}
