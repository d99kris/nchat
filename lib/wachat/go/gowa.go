// gowa.go
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

import (
	"encoding/json"
	"fmt"
	"github.com/Rhymen/go-whatsapp"
	"github.com/Rhymen/go-whatsapp/binary/proto"
	"github.com/skip2/go-qrcode"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

type JSONMessage []json.RawMessage
type JSONMessageType string

type intString struct {
	i int
	s string
}

var (
	mx         sync.Mutex
	conns      map[int]*whatsapp.Conn = make(map[int]*whatsapp.Conn)
	paths      map[int]string         = make(map[int]string)
	timeUnread map[intString]int      = make(map[intString]int)
)

func AddConnPath(conn *whatsapp.Conn, path string) int {
	mx.Lock()
	var connId int = len(conns)
	conns[connId] = conn
	paths[connId] = path
	mx.Unlock()
	return connId
}

func GetConn(connId int) *whatsapp.Conn {
	mx.Lock()
	var conn *whatsapp.Conn = conns[connId]
	mx.Unlock()
	return conn
}

func GetPath(connId int) string {
	mx.Lock()
	var path string = paths[connId]
	mx.Unlock()
	return path
}

func RemoveConn(connId int) {
	mx.Lock()
	delete(conns, connId)
	delete(paths, connId)
	mx.Unlock()
}

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

type eventHandler struct {
	conn   *whatsapp.Conn
	connId int
}

func (handler *eventHandler) HandleError(err error) {
	str := err.Error()
	lines := strings.Split(str, "\n")
	LOG_DEBUG(fmt.Sprintf("Error: %+v", lines[0])) // @todo: LOG_TRACE remaining lines
}

func (handler *eventHandler) HandleTextMessage(message whatsapp.TextMessage) {
	LOG_TRACE(fmt.Sprintf("TextMessage"))

	connId := handler.connId
	chatId := message.Info.RemoteJid
	msgId := message.Info.Id
	text := message.Text
	fromMe := message.Info.FromMe
	senderId := message.Info.RemoteJid
	if message.Info.Source.Participant != nil {
		senderId = *message.Info.Source.Participant
	} else if fromMe {
		senderId = handler.conn.Info.Wid
	}

	quotedId := message.ContextInfo.QuotedMessageID
	filePath := ""

	timeSent := int(message.Info.Timestamp)

	isSeen := (message.Info.Status == 4)
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, filePath, timeSent, BoolToInt(isRead))
}

func (handler *eventHandler) HandleImageMessage(message whatsapp.ImageMessage) {
	LOG_TRACE(fmt.Sprintf("ImageMessage"))

	// get temp file path
	connId := handler.connId
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%v/%v.%v", tmpPath, message.Info.Id, "jpg")

	if _, statErr := os.Stat(filePath); os.IsNotExist(statErr) {
		LOG_TRACE(fmt.Sprintf("ImageMessage new %v", filePath))
		data, err := message.Download()
		if err != nil {
			LOG_WARNING(fmt.Sprintf("download error %+v", err))
			filePath = "  "
		} else {
			file, err := os.Create(filePath)
			defer file.Close()
			if err != nil {
				LOG_WARNING(fmt.Sprintf("create error %+v", err))
				filePath = "  "
			} else {
				_, err = file.Write(data)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("write error %+v", err))
					filePath = "  "
				}
			}
		}
	} else {
		LOG_TRACE(fmt.Sprintf("ImageMessage cached %v", filePath))
	}

	chatId := message.Info.RemoteJid
	msgId := message.Info.Id
	fromMe := message.Info.FromMe
	text := message.Caption
	senderId := message.Info.RemoteJid
	if message.Info.Source.Participant != nil {
		senderId = *message.Info.Source.Participant
	} else if fromMe {
		senderId = handler.conn.Info.Wid
	}

	quotedId := message.ContextInfo.QuotedMessageID

	timeSent := int(message.Info.Timestamp)

	isSeen := (message.Info.Status == 4)
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, filePath, timeSent, BoolToInt(isRead))
}

func (handler *eventHandler) HandleVideoMessage(message whatsapp.VideoMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[VideoMessage]")
}

func (handler *eventHandler) HandleAudioMessage(message whatsapp.AudioMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[AudioMessage]")
}

func (handler *eventHandler) HandleDocumentMessage(message whatsapp.DocumentMessage) {
	LOG_TRACE(fmt.Sprintf("DocumentMessage"))

	// get temp file path
	connId := handler.connId
	var tmpPath string = GetPath(connId) + "/tmp"
	filePath := fmt.Sprintf("%v/%v-%v", tmpPath, message.Info.Id, message.FileName)

	if _, statErr := os.Stat(filePath); os.IsNotExist(statErr) {
		LOG_TRACE(fmt.Sprintf("DocumentMessage new %v", filePath))
		data, err := message.Download()
		if err != nil {
			LOG_WARNING(fmt.Sprintf("download error %+v", err))
			filePath = "  "
		} else {
			file, err := os.Create(filePath)
			defer file.Close()
			if err != nil {
				LOG_WARNING(fmt.Sprintf("create error %+v", err))
				filePath = "  "
			} else {
				_, err = file.Write(data)
				if err != nil {
					LOG_WARNING(fmt.Sprintf("write error %+v", err))
					filePath = "  "
				}
			}
		}
	} else {
		LOG_TRACE(fmt.Sprintf("DocumentMessage cached %v", filePath))
	}

	chatId := message.Info.RemoteJid
	msgId := message.Info.Id
	text := ""
	fromMe := message.Info.FromMe
	senderId := message.Info.RemoteJid
	if message.Info.Source.Participant != nil {
		senderId = *message.Info.Source.Participant
	} else if fromMe {
		senderId = handler.conn.Info.Wid
	}

	quotedId := message.ContextInfo.QuotedMessageID
	timeSent := int(message.Info.Timestamp)

	isSeen := (message.Info.Status == 4)
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])
	isRead := (fromMe && isSeen) || (!fromMe && isOld)

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, filePath, timeSent, BoolToInt(isRead))
}

func (handler *eventHandler) HandleLiveLocationMessage(message whatsapp.LiveLocationMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[LiveLocationMessage]")
}

func (handler *eventHandler) HandleLocationMessage(message whatsapp.LocationMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[LocationMessage]")
}

func (handler *eventHandler) HandleStickerMessage(message whatsapp.StickerMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[StickerMessage]")
}

func (handler *eventHandler) HandleContactMessage(message whatsapp.ContactMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[ContactMessage]")
}

func (handler *eventHandler) HandleJsonMessage(message string) {
	LOG_TRACE(fmt.Sprintf("JsonMessage"))

	connId := handler.connId

	msg := JSONMessage{}
	err := json.Unmarshal([]byte(message), &msg)
	if err != nil || len(msg) < 2 {
		return
	}

	var msgType JSONMessageType
	json.Unmarshal(msg[0], &msgType)

	if msgType == "Presence" {
		var jsonMap map[string]interface{}
		err = json.Unmarshal([]byte(msg[1]), &jsonMap)
		if err != nil {
			return
		}

		chatId, ok := jsonMap["id"].(string)
		if !ok {
			return
		}

		userId, ok := jsonMap["participant"].(string)
		if !ok {
			userId = chatId
		}

		presence, ok := jsonMap["type"].(string)
		if !ok {
			return
		}

		chatId = strings.Replace(chatId, "@c.us", "@s.whatsapp.net", 1)
		userId = strings.Replace(userId, "@c.us", "@s.whatsapp.net", 1)

		isOnline := false
		isTyping := false
		if presence == "unavailable" {
			isOnline = false
			isTyping = false
		} else if presence == "available" {
			isOnline = true
			isTyping = false
		} else if presence == "composing" {
			isOnline = true
			isTyping = true
		}

		CNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping))

	} else if (msgType == "Msg") || (msgType == "MsgInfo") {

		var jsonMap map[string]interface{}
		err = json.Unmarshal([]byte(msg[1]), &jsonMap)
		if err != nil {
			return
		}

		cmd, ok := jsonMap["cmd"].(string)
		if !ok || (cmd != "ack") {
			return
		}

		msgId, ok := jsonMap["id"].(string)
		if !ok {
			return
		}

		chatId, ok := jsonMap["to"].(string)
		if !ok {
			return
		}

		chatId = strings.Replace(chatId, "@c.us", "@s.whatsapp.net", 1)

		ack, ok := jsonMap["ack"].(float64)
		if !ok {
			return
		}

		isRead := (ack == 3)

		CNewMessageStatusNotify(connId, chatId, msgId, BoolToInt(isRead))
	}
}

func (handler *eventHandler) HandleRawMessage(message *proto.WebMessageInfo) {
	LOG_TRACE(fmt.Sprintf("RawMessage"))
}

func (handler *eventHandler) HandleContactList(contacts []whatsapp.Contact) {
	LOG_TRACE(fmt.Sprintf("ContactList"))

	connId := handler.connId

	for _, contact := range contacts {
		CNewContactsNotify(connId, contact.Jid, contact.Name, BoolToInt(false))
	}

	// special handling for self
	selfId := handler.conn.Info.Wid
	selfName := "" // overridden by ui
	CNewContactsNotify(connId, selfId, selfName, BoolToInt(true))
}

func (handler *eventHandler) HandleChatList(chats []whatsapp.Chat) {
	LOG_TRACE(fmt.Sprintf("ChatList"))

	connId := handler.connId

	for _, chat := range chats {
		isUnread := StringToInt(chat.Unread)
		isMuted := StringToInt(chat.IsMuted)
		lastMessageTime := StringToInt(chat.LastMessageTime)
		if IntToBool(isUnread) {
			lastMessageTime = lastMessageTime - 1
		}
		timeUnread[intString{i: connId, s: chat.Jid}] = lastMessageTime

		CNewChatsNotify(connId, chat.Jid, isUnread, isMuted, lastMessageTime)
		CNewContactsNotify(connId, chat.Jid, chat.Name, BoolToInt(false))
	}
}

func (handler *eventHandler) HandleBaseMessage(message whatsapp.BaseMessage) {
	HandleUnsupportedMessage(handler, message.Info, "[UnknownMessage]")
}

func HandleUnsupportedMessage(handler *eventHandler, messageInfo whatsapp.MessageInfo, text string) {
	LOG_TRACE(fmt.Sprintf("UnsupportedMessage"))

	connId := handler.connId
	chatId := messageInfo.RemoteJid
	msgId := messageInfo.Id
	fromMe := messageInfo.FromMe
	timeSent := int(messageInfo.Timestamp)
	senderId := messageInfo.RemoteJid
	if messageInfo.Source.Participant != nil {
		senderId = *messageInfo.Source.Participant
	} else if fromMe {
		senderId = handler.conn.Info.Wid
	}

	quotedId := ""
	filePath := ""

	isRead := true
	isOld := (timeSent <= timeUnread[intString{i: connId, s: chatId}])

	UpdateTypingStatus(connId, chatId, senderId, fromMe, isOld)

	CNewMessagesNotify(connId, chatId, msgId, senderId, text, BoolToInt(fromMe), quotedId, filePath, timeSent, BoolToInt(isRead))
}

func UpdateTypingStatus(connId int, chatId string, userId string, fromMe bool, isOld bool) {

	// only handle new messages from others
	if fromMe || isOld {
		return
	}

	LOG_TRACE("update typing status " + strconv.Itoa(connId) + ", " + chatId + ", " + userId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
	}

	// update
	isOnline := true
	isTyping := false
	CNewStatusNotify(connId, chatId, userId, BoolToInt(isOnline), BoolToInt(isTyping))
}

func Init(path string) int {

	LOG_DEBUG("init " + filepath.Base(path))

	// create tmp dir
	var tmpPath string = path + "/tmp"
	tmpErr := os.MkdirAll(tmpPath, os.ModePerm)
	if tmpErr != nil {
		LOG_WARNING(fmt.Sprintf("mkdir error %+v", tmpErr))
		return -1
	}

	// create new whatsapp connection
	conn, connErr := whatsapp.NewConn(5 * time.Second)
	if connErr != nil {
		LOG_WARNING(fmt.Sprintf("conn error %+v", connErr))
		return -1
	}

	// store connection and get id
	var connId int = AddConnPath(conn, path)

	// register event handler
	conn.AddHandler(&eventHandler{conn, connId})

	// set client info
	conn.SetClientName("Firefox", "Firefox", "76.0.1")

	LOG_DEBUG("connId " + strconv.Itoa(connId))

	return connId
}

func Login(connId int) int {

	LOG_DEBUG("login " + strconv.Itoa(connId))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get path and conn
	var path string = GetPath(connId)
	var conn *whatsapp.Conn = GetConn(connId)

	// first attempt to read a stored session
	fileBytes, fileErr := ioutil.ReadFile(path + "/session.json")
	if fileErr == nil {
		var session whatsapp.Session
		jsonErr := json.Unmarshal(fileBytes, &session)
		if jsonErr == nil {
			var sessErr error
			session, sessErr = conn.RestoreWithSession(session)
			if sessErr == nil {
				LOG_DEBUG("login ok (restore)")
				return 0
			}
		}
	}

	// second option is to create a new session
	qr := make(chan string)
	go func() {
		qrcode.WriteFile(<-qr, qrcode.Medium, 256, path+"/qr.png")
		ShowImage(path + "/qr.png")
	}()
	session, err := conn.Login(qr)
	if err != nil {
		LOG_WARNING("login failed (qr code)")
		return -1
	}

	// delete temporary image file
	_ = os.Remove(path + "/qr.png")

	// store session
	jsonBytes, jsonErr := json.Marshal(session)
	if jsonErr != nil {
		LOG_WARNING("login failed (restore session)")
		return -1
	}
	ioutil.WriteFile(path+"/session.json", jsonBytes, 0600)

	LOG_DEBUG("login ok (new)")

	return 0
}

func Logout(connId int) int {

	LOG_DEBUG("logout " + strconv.Itoa(connId))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get path and conn
	var conn *whatsapp.Conn = GetConn(connId)

	// disconnect
	_, err := conn.Disconnect()
	if err != nil {
		LOG_WARNING(fmt.Sprintf("disconnect error %+v", err))
		return -1
	}

	LOG_DEBUG("logout ok")

	return 0
}

func Cleanup(connId int) int {

	LOG_DEBUG("cleanup " + strconv.Itoa(connId))
	RemoveConn(connId)
	return 0
}

func GetMessages(connId int, chatId string, limit int, fromMsgId string, owner int) int {

	LOG_TRACE("get messages " + strconv.Itoa(connId) + ", " + chatId + ", " + strconv.Itoa(limit) + ", " + fromMsgId + ", " + strconv.Itoa(owner))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// get messages
	isOwner := false
	if owner == 1 {
		isOwner = true
	}
	after := false

	// hack to reduce likelehood of issue with messages in wrong timestamp order (or multiple messages with same timestamp)
	if limit < 5 {
		limit = 5
	}

	cnt, err := conn.LoadChatMessages(chatId, limit, fromMsgId, isOwner, after)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("get messages error %+v", err))
		return -1
	}

	return cnt
}

func SendMessage(connId int, chatId string, text string, quotedId string, quotedText string, quotedSender string, filePath string, fileType string) int {

	LOG_TRACE("send message " + strconv.Itoa(connId) + ", " + chatId + ", " + text)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// local vars
	var sendErr error
	var msgId string

	// check message type
	if len(filePath) == 0 {

		// text message
		contextInfo := whatsapp.ContextInfo{}
		quotedMessage := proto.Message{
			Conversation: &quotedText,
		}
		if len(quotedId) > 0 {
			if quotedSender == conn.Info.Wid {
				//quotedSender = chatId
			}

			quotedSender = strings.Replace(quotedSender, "@c.us", "@s.whatsapp.net", 1)

			LOG_TRACE("send quoted " + quotedId + ", " + quotedText + ", " + quotedSender)
			contextInfo = whatsapp.ContextInfo{
				QuotedMessage:   &quotedMessage,
				QuotedMessageID: quotedId,
				Participant:     quotedSender,
			}
		}

		textMessage := whatsapp.TextMessage{
			Info: whatsapp.MessageInfo{
				RemoteJid: chatId,
			},
			Text:        text,
			ContextInfo: contextInfo,
		}

		// send message
		msgId, sendErr = conn.Send(textMessage)

	} else {

		// read file
		fileReader, fileErr := os.Open(filePath)
		if fileErr != nil {
			LOG_WARNING(fmt.Sprintf("send file error %+v", fileErr))
			return -1
		}

		mimeType := strings.Split(fileType, "/")[0] // image, text, application, etc.
		if mimeType == "image" {

			LOG_TRACE("send image " + fileType)

			// image message
			imageMessage := whatsapp.ImageMessage{
				Info: whatsapp.MessageInfo{
					RemoteJid: chatId,
				},
				Type: fileType,
				//Caption: "Hello",
				Content: fileReader,
			}

			// send message
			msgId, sendErr = conn.Send(imageMessage)

		} else {

			fileName := filepath.Base(filePath)
			fileNameNoExt := strings.TrimSuffix(fileName, filepath.Ext(fileName))

			LOG_TRACE("send document " + fileType + ", " + fileName + ", " + fileNameNoExt)

			// document message
			documentMessage := whatsapp.DocumentMessage{
				Info: whatsapp.MessageInfo{
					RemoteJid: chatId,
				},
				Title:    fileNameNoExt,
				Type:     fileType,
				FileName: fileName,
				//Caption: "Hello",
				Content: fileReader,
			}

			// send message
			msgId, sendErr = conn.Send(documentMessage)
		}
	}

	// log any error
	if sendErr != nil {
		LOG_WARNING(fmt.Sprintf("send message error %+v", sendErr))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("send message ok %+v", msgId))
	}

	return 0
}

func MarkMessageRead(connId int, chatId string, msgId string) int {

	LOG_TRACE("mark message read " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// mark read
	_, err := conn.Read(chatId, msgId)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("mark message read error %+v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("mark message read ok %+v", msgId))
	}

	return 0
}

func DeleteMessage(connId int, chatId string, msgId string) int {

	LOG_TRACE("delete message " + strconv.Itoa(connId) + ", " + chatId + ", " + msgId)

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// delete message
	fromMe := true
	_, err := conn.RevokeMessage(chatId, msgId, fromMe)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("delete message error %+v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("delete message ok %+v", msgId))
	}

	return 0
}

func SendTyping(connId int, chatId string, isTyping int) int {

	LOG_TRACE("send typing " + strconv.Itoa(connId) + ", " + chatId + ", " + strconv.Itoa(isTyping))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// set presence
	var presence whatsapp.Presence = whatsapp.PresencePaused
	if isTyping == 1 {
		presence = whatsapp.PresenceComposing
	}

	_, err := conn.Presence(chatId, presence)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("send typing error %+v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("send typing ok"))
	}

	return 0
}

func SetStatus(connId int, isOnline int) int {

	LOG_TRACE("set status " + strconv.Itoa(connId) + ", " + strconv.Itoa(isOnline))

	// sanity check arg
	if connId == -1 {
		LOG_WARNING("invalid connId")
		return -1
	}

	// get conn
	conn := GetConn(connId)

	// set presence
	var presence whatsapp.Presence = whatsapp.PresenceUnavailable
	if isOnline == 1 {
		presence = whatsapp.PresenceAvailable
	}

	chatId := "" // not used for available/unavailable
	_, err := conn.Presence(chatId, presence)

	// log any error
	if err != nil {
		LOG_WARNING(fmt.Sprintf("set status error %+v", err))
		return -1
	} else {
		LOG_TRACE(fmt.Sprintf("set status ok"))
	}

	return 0
}
