// cgowa.go
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

// #cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
// #cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
// extern void WaNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, int p_IsSelf);
// extern void WaNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_LastMessageTime);
// extern void WaNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FilePath, int p_TimeSent, int p_IsRead);
// extern void WaNewStatusNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsOnline, int p_IsTyping);
// extern void WaNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
// extern void WaLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WaLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WaLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WaLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WaLogError(char* p_Filename, int p_LineNo, char* p_Message);
import "C"

import (
	"path/filepath"
	"runtime"
)

//export CInit
func CInit(path *C.char) int {
	return Init(C.GoString(path))
}

//export CLogin
func CLogin(connId int) int {
	return Login(connId)
}

//export CLogout
func CLogout(connId int) int {
	return Logout(connId)
}

//export CCleanup
func CCleanup(connId int) int {
	return Cleanup(connId)
}

//export CGetMessages
func CGetMessages(connId int, chatId *C.char, limit int, fromMsgId *C.char, owner int) int {
	return GetMessages(connId, C.GoString(chatId), limit, C.GoString(fromMsgId), owner)
}

//export CSendMessage
func CSendMessage(connId int, chatId *C.char, text *C.char, quotedId *C.char, quotedText *C.char, quotedSender *C.char, filePath *C.char, fileType *C.char) int {
	return SendMessage(connId, C.GoString(chatId), C.GoString(text), C.GoString(quotedId), C.GoString(quotedText), C.GoString(quotedSender), C.GoString(filePath), C.GoString(fileType))
}

//export CMarkMessageRead
func CMarkMessageRead(connId int, chatId *C.char, msgId *C.char) int {
	return MarkMessageRead(connId, C.GoString(chatId), C.GoString(msgId))
}

//export CDeleteMessage
func CDeleteMessage(connId int, chatId *C.char, msgId *C.char) int {
	return DeleteMessage(connId, C.GoString(chatId), C.GoString(msgId))
}

//export CSendTyping
func CSendTyping(connId int, chatId *C.char, isTyping int) int {
	return SendTyping(connId, C.GoString(chatId), isTyping)
}

//export CSetStatus
func CSetStatus(connId int, isOnline int) int {
	return SetStatus(connId, isOnline)
}

func CNewContactsNotify(connId int, chatId string, name string, isSelf int) {
	C.WaNewContactsNotify(C.int(connId), C.CString(chatId), C.CString(name), C.int(isSelf))
}

func CNewChatsNotify(connId int, chatId string, isUnread int, isMuted int, lastMessageTime int) {
	C.WaNewChatsNotify(C.int(connId), C.CString(chatId), C.int(isUnread), C.int(isMuted), C.int(lastMessageTime))
}

func CNewMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, filePath string, timeSent int, isRead int) {
	C.WaNewMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(filePath), C.int(timeSent), C.int(isRead))
}

func CNewStatusNotify(connId int, chatId string, userId string, isOnline int, isTyping int) {
	C.WaNewStatusNotify(C.int(connId), C.CString(chatId), C.CString(userId), C.int(isOnline), C.int(isTyping))
}

func CNewMessageStatusNotify(connId int, chatId string, msgId string, isRead int) {
	C.WaNewMessageStatusNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isRead))
}

func LOG_TRACE(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WaLogTrace(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_DEBUG(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WaLogDebug(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_INFO(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WaLogInfo(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_WARNING(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WaLogWarning(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_ERROR(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WaLogError(C.CString(filename), C.int(lineNo), C.CString(message))
}
