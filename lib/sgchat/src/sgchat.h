// sgchat.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <thread>

#include "config.h"
#include "protocol.h"
#include "sysutil.h"

class SgChat : public Protocol
{
public:
  SgChat();
  virtual ~SgChat();
  static std::string GetName() { return "Signal"; }
  static std::string GetLibName() { return "libsgchat"; }
  static std::string GetCreateFunc() { return "CreateSgChat"; }
  static std::string GetSetupMessage()
  {
    if (SysUtil::IsSupportedLibc())
    {
      return "";
    }
    else
    {
      return "\nUNSUPPORTED PLATFORM:\nThe Signal protocol implementation officially only supports glibc on Linux.\n";
    }
  }

  std::string GetProfileId() const;
  std::string GetProfileDisplayName() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;
  bool IsGroupChat(const std::string& p_ChatId) const;
  std::string GetSelfId() const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

  void SetProtocolUiControl(bool p_IsTakeControl);
  void SetStatus(int p_Flags);
  void ClearStatus(int p_Flags);

  void AddContactInfo(const ContactInfo& p_ContactInfo);
  std::vector<ContactInfo> GetContactInfos();
  void ClearContactInfos();

  void AddHistoryMessage(const ChatMessage& p_ChatMessage);
  std::vector<ChatMessage> GetHistoryMessages();
  void ClearHistoryMessages();

public:
  static void AddInstance(int p_ConnId, SgChat* p_Instance);
  static void RemoveInstance(int p_ConnId);
  static SgChat* GetInstance(int p_ConnId);

private:
  void Init();
  void InitConfig();
  void Cleanup();
  void CleanupConfig();
  void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  std::string GetProxyUrl() const;

private:
  std::string m_ProfileId;
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;

  int m_ConnId = -1;
  std::string m_ProfileDir;
  Config m_Config;
  int m_SignalmeowDate = 0;
  int m_ProfileDirVersion = 0;
  bool m_WasOnline = false;
  bool m_IsSetup = false;

  mutable std::mutex m_Mutex;
  std::vector<ContactInfo> m_ContactInfos;
  std::vector<ChatMessage> m_HistoryMessages;
  std::string m_SelfUserId;

  static std::mutex s_ConnIdMapMutex;
  static std::map<int, SgChat*> s_ConnIdMap;
  static const int s_CacheDirVersion = 0;
};

extern "C" {
void SgNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, char* p_Phone, int p_IsSelf, int p_IsAlias,
                         int p_Notify);
void SgNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_IsPinned,
                      int p_LastMessageTime);
void SgNewGroupMembersNotify(int p_ConnId, char* p_ChatId, char* p_MembersJson);
void SgNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_ReplyId, char* p_FileId, char* p_FilePath, int p_FileStatus, int p_TimeSent,
                         int p_IsRead, int p_IsEdited);
void SgNewHistoryMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe, char* p_QuotedId, char* p_FileId, char* p_FilePath, int p_FileStatus,
                                int p_TimeSent, int p_IsRead, int p_IsEdited, char* p_FromMsgId, int p_Notify);
void SgNewStatusNotify(int p_ConnId, char* p_UserId, int p_IsOnline, int p_TimeSeen);
void SgNewTypingNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsTyping);
void SgNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
void SgNewMessageFileNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_FilePath, int p_FileStatus,
                            int p_Action);
void SgNewMessageReactionNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text,
                                int p_FromMe);
void SgDeleteChatNotify(int p_ConnId, char* p_ChatId);
void SgDeleteMessageNotify(int p_ConnId, char* p_ChatId, char* p_MsgId);
void SgUpdateMuteNotify(int p_ConnId, char* p_ChatId, int p_IsMuted);
void SgUpdatePinNotify(int p_ConnId, char* p_ChatId, int p_IsPinned, int p_TimePinned);
void SgReinit(int p_ConnId);
void SgSetProtocolUiControl(int p_ConnId, int p_IsTakeControl);
void SgSetStatus(int p_ConnId, int p_Flags);
void SgClearStatus(int p_ConnId, int p_Flags);
int SgAppConfigGetNum(char* p_Param);
void SgAppConfigSetNum(char* p_Param, int p_Value);
void SgLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
void SgLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
void SgLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
void SgLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
void SgLogError(char* p_Filename, int p_LineNo, char* p_Message);
}

extern "C" SgChat* CreateSgChat();
