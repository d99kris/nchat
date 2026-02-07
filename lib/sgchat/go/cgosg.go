// cgosg.go
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

package main

// #cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
// #cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup
// extern void SgNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias, int p_Notify);
// extern void SgNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned, int p_LastMessageTime);
// extern void SgNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead, int p_IsEdited);
// extern void SgNewHistoryMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent, int p_IsRead, int p_IsEdited, char* p_FromMsgId, int p_Notify);
// extern void SgNewStatusNotify(int p_ConnId, char* p_UserId, int p_IsOnline, int p_TimeSeen);
// extern void SgNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping);
// extern void SgNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
// extern void SgNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FilePath, int p_FileStatus, int p_Action);
// extern void SgNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe);
// extern void SgDeleteChatNotify(int p_ConnId, char* p_ChatId);
// extern void SgDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId);
// extern void SgUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted);
// extern void SgUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned);
// extern void SgReinit(int p_ConnId);
// extern void SgSetProtocolUiControl(int p_ConnId, int p_IsTakeControl);
// extern void SgSetStatus(int p_ConnId, int p_Flags);
// extern void SgClearStatus(int p_ConnId, int p_Flags);
// extern int SgAppConfigGetNum(char* p_Param);
// extern void SgAppConfigSetNum(char* p_Param, int p_Value);
// extern void SgLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
// extern void SgLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
// extern void SgLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
// extern void SgLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
// extern void SgLogError(char* p_Filename, int p_LineNo, char* p_Message);
import "C"

import (
	"path/filepath"
	"runtime"
)

//export CSgInit
func CSgInit(path *C.char, proxy *C.char) int {
	return SgInit(C.GoString(path), C.GoString(proxy))
}

//export CSgLogin
func CSgLogin(connId int) int {
	return SgLogin(connId)
}

//export CSgLogout
func CSgLogout(connId int) int {
	return SgLogout(connId)
}

//export CSgCleanup
func CSgCleanup(connId int) int {
	return SgCleanup(connId)
}

//export CSgGetVersion
func CSgGetVersion() int {
	return SgGetVersion()
}

//export CSgGetMessages
func CSgGetMessages(connId int, chatId *C.char, limit int, fromMsgId *C.char, owner int) int {
	return SgGetMessages(connId, C.GoString(chatId), limit, C.GoString(fromMsgId), owner)
}

//export CSgSendMessage
func CSgSendMessage(connId int, chatId *C.char, text *C.char, quotedId *C.char, quotedText *C.char, quotedSender *C.char, filePath *C.char, fileType *C.char, editMsgId *C.char, editMsgSent int) int {
	return SgSendMessage(connId, C.GoString(chatId), C.GoString(text), C.GoString(quotedId), C.GoString(quotedText), C.GoString(quotedSender), C.GoString(filePath), C.GoString(fileType), C.GoString(editMsgId), editMsgSent)
}

//export CSgGetContacts
func CSgGetContacts(connId int) int {
	return SgGetContacts(connId)
}

//export CSgGetChats
func CSgGetChats(connId int) int {
	return SgGetChats(connId)
}

//export CSgMarkMessageRead
func CSgMarkMessageRead(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return SgMarkMessageRead(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CSgDeleteMessage
func CSgDeleteMessage(connId int, chatId *C.char, senderId *C.char, msgId *C.char) int {
	return SgDeleteMessage(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId))
}

//export CSgDeleteChat
func CSgDeleteChat(connId int, chatId *C.char) int {
	return SgDeleteChat(connId, C.GoString(chatId))
}

//export CSgSendTyping
func CSgSendTyping(connId int, chatId *C.char, isTyping int) int {
	return SgSendTyping(connId, C.GoString(chatId), isTyping)
}

//export CSgDownloadFile
func CSgDownloadFile(connId int, chatId *C.char, msgId *C.char, fileId *C.char, action int) int {
	return SgDownloadFile(connId, C.GoString(chatId), C.GoString(msgId), C.GoString(fileId), action)
}

//export CSgSendReaction
func CSgSendReaction(connId int, chatId *C.char, senderId *C.char, msgId *C.char, emoji *C.char, prevEmoji *C.char) int {
	return SgSendReaction(connId, C.GoString(chatId), C.GoString(senderId), C.GoString(msgId), C.GoString(emoji), C.GoString(prevEmoji))
}

func CSgNewContactsNotify(connId int, chatId string, name string, phone string, isSelf int, isAlias int, notify int) {
	C.SgNewContactsNotify(C.int(connId), C.CString(chatId), C.CString(name), C.CString(phone), C.int(isSelf), C.int(isAlias), C.int(notify))
}

func CSgNewChatsNotify(connId int, chatId string, isUnread int, isMuted int, isPinned int, lastMessageTime int) {
	C.SgNewChatsNotify(C.int(connId), C.CString(chatId), C.int(isUnread), C.int(isMuted), C.int(isPinned), C.int(lastMessageTime))
}

func CSgNewMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int, isEdited int) {
	C.SgNewMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead), C.int(isEdited))
}

func CSgNewHistoryMessagesNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int, quotedId string, fileId string, filePath string, fileStatus int, timeSent int, isRead int, isEdited int, fromMsgId string, notify int) {
	C.SgNewHistoryMessagesNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe), C.CString(quotedId), C.CString(fileId), C.CString(filePath), C.int(fileStatus), C.int(timeSent), C.int(isRead), C.int(isEdited), C.CString(fromMsgId), C.int(notify))
}

func CSgNewStatusNotify(connId int, userId string, isOnline int, timeSeen int) {
	C.SgNewStatusNotify(C.int(connId), C.CString(userId), C.int(isOnline), C.int(timeSeen))
}

func CSgNewTypingNotify(connId int, chatId string, userId string, isTyping int) {
	C.SgNewTypingNotify(C.int(connId), C.CString(chatId), C.CString(userId), C.int(isTyping))
}

func CSgNewMessageStatusNotify(connId int, chatId string, msgId string, isRead int) {
	C.SgNewMessageStatusNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.int(isRead))
}

func CSgNewMessageFileNotify(connId int, chatId string, msgId string, filePath string, fileStatus int, action int) {
	C.SgNewMessageFileNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(filePath), C.int(fileStatus), C.int(action))
}

func CSgNewMessageReactionNotify(connId int, chatId string, msgId string, senderId string, text string, fromMe int) {
	C.SgNewMessageReactionNotify(C.int(connId), C.CString(chatId), C.CString(msgId), C.CString(senderId), C.CString(text), C.int(fromMe))
}

func CSgDeleteChatNotify(connId int, chatId string) {
	C.SgDeleteChatNotify(C.int(connId), C.CString(chatId))
}

func CSgDeleteMessageNotify(connId int, chatId string, msgId string) {
	C.SgDeleteMessageNotify(C.int(connId), C.CString(chatId), C.CString(msgId))
}

func CSgUpdateMuteNotify(connId int, chatId string, isMuted int) {
	C.SgUpdateMuteNotify(C.int(connId), C.CString(chatId), C.int(isMuted))
}

func CSgUpdatePinNotify(connId int, chatId string, isPinned int, timePinned int) {
	C.SgUpdatePinNotify(C.int(connId), C.CString(chatId), C.int(isPinned), C.int(timePinned))
}

func CSgReinit(connId int) {
	C.SgReinit(C.int(connId))
}

func CSgSetProtocolUiControl(connId int, isTakeControl int) {
	C.SgSetProtocolUiControl(C.int(connId), C.int(isTakeControl))
}

func CSgSetStatus(connId int, flags int) {
	C.SgSetStatus(C.int(connId), C.int(flags))
}

func CSgClearStatus(connId int, flags int) {
	C.SgClearStatus(C.int(connId), C.int(flags))
}

func CSgAppConfigGetNum(param string) int {
	return int(C.SgAppConfigGetNum(C.CString(param)))
}

func CSgAppConfigSetNum(param string, value int) {
	C.SgAppConfigSetNum(C.CString(param), C.int(value))
}

func LOG_TRACE(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.SgLogTrace(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_DEBUG(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.SgLogDebug(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_INFO(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.SgLogInfo(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_WARNING(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.SgLogWarning(C.CString(filename), C.int(lineNo), C.CString(message))
}

func LOG_ERROR(message string) {
	_, filename, lineNo, ok := runtime.Caller(1)
	if ok {
		filename = filepath.Base(filename)
	} else {
		filename = "???"
		lineNo = 0
	}

	C.SgLogError(C.CString(filename), C.int(lineNo), C.CString(message))
}
