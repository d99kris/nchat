// cgowm.go
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

// #cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
// #cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
// extern void WmNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias, int p_Notify);
// extern void WmNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned, int p_LastMessageTime);
// extern void WmNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead, int p_IsEdited);
// extern void WmNewStatusNotify(int p_ConnId, char* p_UserId, int p_IsOnline, int p_TimeSeen);
// extern void WmNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping);
// extern void WmNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
// extern void WmNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FilePath, int p_FileStatus, int p_Action);
// extern void WmNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe);
// extern void WmDeleteChatNotify(int p_ConnId, char* p_ChatId);
// extern void WmDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId);
// extern void WmUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted);
// extern void WmUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned);
// extern void WmReinit(int p_ConnId);
// extern void WmSetProtocolUiControl(int p_ConnId, int p_IsTakeControl);
// extern void WmSetStatus(int p_ConnId, int p_Flags);
// extern void WmClearStatus(int p_ConnId, int p_Flags);
// extern int WmAppConfigGetNum(char* p_Param);
// extern void WmAppConfigSetNum(char* p_Param, int p_Value);
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
func CWmInit(path *C.char, proxy *C.char, sendType int) int {
	return WmInit(C.GoString(path), C.GoString(proxy), sendType)
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

//export CWmGetVersion
func CWmGetVersion() int {
	return WmGetVersion()
}

//export CWmGetMessages
func CWmGetMessages(connId int, chatId *C.char, limit int, fromMsgId *C.char, owner int) int {
	return WmGetMessages(connId, C.GoString(chatId), limit, C.GoString(fromMsgId), owner)
}

//export CWmSendMessage
func CWmSendMessage(connId int, chatId *C.char, text *C.char, quotedId *C.char, quotedText *C.char, quotedSender *C.char, filePath *C.char, fileType *C.char, editMsgId *C.char, editMsgSent int) int {
	return WmSendMessage(connId, C.GoString(chatId), C.GoString(text), C.GoString(quotedId), C.GoString(quotedText), C.GoString(quotedSender), C.GoString(filePath), C.GoString(fileType), C.GoString(editMsgId), editMsgSent)
}

//export CWmGetContacts
func CWmGetContacts(connId int) int {
	return WmGetContacts(connId)
}

//export CWmGetStatus
func CWmGetStatus(connId int, userId *C.char) int {
	return WmGetStatus(connId, C.GoString(userId))
}

//export CWmMarkMessageRead
func CWmMarkMessageRead(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return WmMarkMessageRead(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CWmDeleteMessage
func CWmDeleteMessage(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return WmDeleteMessage(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CWmDeleteChat
func CWmDeleteChat(connId int, chatId *C.char) int {
	return WmDeleteChat(connId, C.GoString(chatId))
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

//export CWmSendReaction
func CWmSendReaction(connId int, chatId *C.char, senderId *C.char, msgId *C.char, emoji *C.char) int {
	return WmSendReaction(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId), C.GoString(emoji))
}

func CWmNewContactsNotify(connId int, chatId string, name string, phone string, isSelf int, isAlias int, notify int) {
	C.WmNewContactsNotify(C.int(connId), C.CString(chatId), C.CString(name), C.CString(phone), C.int(isSelf), C.int(isAlias), C.int(notify))
}

func CWmNewChatsNotify(connId int, chatId string, isUnread int, isMuted int, isPinned int, lastMessageTime int) {
	C.WmNewChatsNotify(C.int(connId), C.CString(chatId), C.int(isUnread), C.int(isMuted), C.int(isPinned), C.int(lastMessageTime))
}

func CWmNewMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int, isEdited int) {
	C.WmNewMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead), C.int(isEdited))
}

func CWmNewStatusNotify(connId int, userId string, isOnline int, timeSeen int) {
	C.WmNewStatusNotify(C.int(connId), C.CString(userId), C.int(isOnline), C.int(timeSeen))
}

func CWmNewTypingNotify(connId int, chatId string, userId string, isTyping int) {
	C.WmNewTypingNotify(C.int(connId), C.CString(chatId), C.CString(userId), C.int(isTyping))
}

func CWmNewMessageStatusNotify(connId int, chatId string, msgId string, isRead int) {
	C.WmNewMessageStatusNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isRead))
}

func CWmNewMessageFileNotify(connId int, chatId string, msgId string, filePath string, fileStatus int, action int) {
	C.WmNewMessageFileNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(filePath), C.int(fileStatus), C.int(action))
}

func CWmNewMessageReactionNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int) {
	C.WmNewMessageReactionNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe))
}

func CWmDeleteChatNotify(connId int, chatId string) {
	C.WmDeleteChatNotify(C.int(connId), C.CString(chatId))
}

func CWmDeleteMessageNotify(connId int, chatId string, msgId string) {
	C.WmDeleteMessageNotify(C.int(connId), C.CString(chatId), C.CString(msgId))
}

func CWmUpdateMuteNotify(connId int, chatId string, isMuted int) {
	C.WmUpdateMuteNotify(C.int(connId), C.CString(chatId), C.int(isMuted))
}

func CWmUpdatePinNotify(connId int, chatId string, isPinned int, timePinned int) {
	C.WmUpdatePinNotify(C.int(connId), C.CString(chatId), C.int(isPinned), C.int(timePinned))
}

func CWmReinit(connId int) {
	C.WmReinit(C.int(connId))
}

func CWmSetProtocolUiControl(connId int, isTakeControl int) {
	C.WmSetProtocolUiControl(C.int(connId), C.int(isTakeControl))
}

func CWmSetStatus(connId int, flags int) {
	C.WmSetStatus(C.int(connId), C.int(flags))
}

func CWmClearStatus(connId int, flags int) {
	C.WmClearStatus(C.int(connId), C.int(flags))
}

func CWmAppConfigGetNum(param string) int {
	return int(C.WmAppConfigGetNum(C.CString(param)))
}

func CWmAppConfigSetNum(param string, value int) {
	C.WmAppConfigSetNum(C.CString(param), C.int(value))
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
