// cgowm.go
//
// Copyright (c) 2020-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

// #cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
// #cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
// extern void WmNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, int p_IsSelf);
// extern void WmNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_LastMessageTime);
// extern void WmNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead);
// extern void WmNewStatusNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsOnline, int p_IsTyping, int p_TimeSeen);
// extern void WmNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
// extern void WmNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FilePath, int p_FileStatus, int p_Action);
// extern void WmSetStatus(int p_Flags);
// extern void WmClearStatus(int p_Flags);
// extern void WmLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WmLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WmLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WmLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
// extern void WmLogError(char* p_Filename, int p_LineNo, char* p_Message);
import "C"

import (
	"path/filepath"
	"runtime"
)

//export CWmInit
func CWmInit(path *C.char, proxy *C.char) int {
	return WmInit(C.GoString(path), C.GoString(proxy))
}

//export CWmLogin
func CWmLogin(connId int) int {
	return WmLogin(connId)
}

//export CWmLogout
func CWmLogout(connId int) int {
	return WmLogout(connId)
}

//export CWmCleanup
func CWmCleanup(connId int) int {
	return WmCleanup(connId)
}

//export CWmGetMessages
func CWmGetMessages(connId int, chatId *C.char, limit int, fromMsgId *C.char, owner int) int {
	return WmGetMessages(connId, C.GoString(chatId), limit, C.GoString(fromMsgId), owner)
}

//export CWmSendMessage
func CWmSendMessage(connId int, chatId *C.char, text *C.char, quotedId *C.char, quotedText *C.char, quotedSender *C.char, filePath *C.char, fileType *C.char, editMsgId *C.char, editMsgSent int) int {
	return WmSendMessage(connId, C.GoString(chatId), C.GoString(text), C.GoString(quotedId), C.GoString(quotedText), C.GoString(quotedSender), C.GoString(filePath), C.GoString(fileType), C.GoString(editMsgId), editMsgSent)
}

//export CWmGetStatus
func CWmGetStatus(connId int, userId *C.char) int {
	return WmGetStatus(connId, C.GoString(userId))
}

//export CWmMarkMessageRead
func CWmMarkMessageRead(connId int, chatId *C.char, msgId *C.char) int {
	return WmMarkMessageRead(connId, C.GoString(chatId), C.GoString(msgId))
}

//export CWmDeleteMessage
func CWmDeleteMessage(connId int, chatId *C.char, msgId *C.char) int {
	return WmDeleteMessage(connId, C.GoString(chatId), C.GoString(msgId))
}

//export CWmSendTyping
func CWmSendTyping(connId int, chatId *C.char, isTyping int) int {
	return WmSendTyping(connId, C.GoString(chatId), isTyping)
}

//export CWmSendStatus
func CWmSendStatus(connId int, isOnline int) int {
	return WmSendStatus(connId, isOnline)
}

//export CWmDownloadFile
func CWmDownloadFile(connId int, chatId *C.char, msgId *C.char, fileId *C.char, action int) int {
	return WmDownloadFile(connId, C.GoString(chatId), C.GoString(msgId), C.GoString(fileId), action)
}

func CWmNewContactsNotify(connId int, chatId string, name string, isSelf int) {
	C.WmNewContactsNotify(C.int(connId), C.CString(chatId), C.CString(name), C.int(isSelf))
}

func CWmNewChatsNotify(connId int, chatId string, isUnread int, isMuted int, lastMessageTime int) {
	C.WmNewChatsNotify(C.int(connId), C.CString(chatId), C.int(isUnread), C.int(isMuted), C.int(lastMessageTime))
}

func CWmNewMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int) {
	C.WmNewMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead))
}

func CWmNewStatusNotify(connId int, chatId string, userId string, isOnline int, isTyping int, timeSeen int) {
	C.WmNewStatusNotify(C.int(connId), C.CString(chatId), C.CString(userId), C.int(isOnline), C.int(isTyping), C.int(timeSeen))
}

func CWmNewMessageStatusNotify(connId int, chatId string, msgId string, isRead int) {
	C.WmNewMessageStatusNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isRead))
}

func CWmNewMessageFileNotify(connId int, chatId string, msgId string, filePath string, fileStatus int, action int) {
	C.WmNewMessageFileNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(filePath), C.int(fileStatus), C.int(action))
}

func CWmSetStatus(flags int) {
	C.WmSetStatus(C.int(flags))
}

func CWmClearStatus(flags int) {
	C.WmClearStatus(C.int(flags))
}

func LOG_TRACE(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WmLogTrace(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_DEBUG(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WmLogDebug(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_INFO(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WmLogInfo(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_WARNING(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WmLogWarning(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_ERROR(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.WmLogError(C.CString(filename), C.int(lineNo), C.CString(message))
}
