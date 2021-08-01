// wachat.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <thread>

#include "protocol.h"

class WaChat : public Protocol
{
public:
  WaChat();
  virtual ~WaChat();
  std::string GetProfileId() const;
  bool HasFeature(ProtocolFeature p_ProtocolFeature) const;

  bool SetupProfile(const std::string& p_ProfilesDir, std::string& p_ProfileId);
  bool LoadProfile(const std::string& p_ProfilesDir, const std::string& p_ProfileId);
  bool CloseProfile();

  bool Login();
  bool Logout();

  void Process();

  void SendRequest(std::shared_ptr<RequestMessage> p_RequestMessage);
  void SetMessageHandler(const std::function<void(std::shared_ptr<ServiceMessage>)>& p_MessageHandler);

public:
  static void AddInstance(int p_ConnId, WaChat* p_Instance);
  static void RemoveInstance(int p_ConnId);
  static WaChat* GetInstance(int p_ConnId);

private:
  void CallMessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void PerformRequest(std::shared_ptr<RequestMessage> p_RequestMessage);

private:
  std::string m_ProfileId = "WhatsApp";
  std::function<void(std::shared_ptr<ServiceMessage>)> m_MessageHandler;

  bool m_Running = false;
  std::thread m_Thread;
  std::deque<std::shared_ptr<RequestMessage>> m_RequestsQueue;
  std::mutex m_ProcessMutex;
  std::condition_variable m_ProcessCondVar;

  static std::mutex s_ConnIdMapMutex;
  static std::map<int, WaChat*> s_ConnIdMap;
  int m_ConnId = -1;
  std::string m_ProfileDir;
};

extern "C" {
void WaNewContactsNotify(int p_ConnId, char* p_ChatId, char* p_Name, int p_IsSelf);
void WaNewChatsNotify(int p_ConnId, char* p_ChatId, int p_IsUnread, int p_IsMuted, int p_LastMessageTime);
void WaNewMessagesNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, char* p_SenderId, char* p_Text, int p_FromMe,
                         char* p_ReplyId, char* p_FilePath, int p_TimeSent, int p_IsRead);
void WaNewStatusNotify(int p_ConnId, char* p_ChatId, char* p_UserId, int p_IsOnline, int p_IsTyping);
void WaNewMessageStatusNotify(int p_ConnId, char* p_ChatId, char* p_MsgId, int p_IsRead);
void WaLogTrace(char* p_Filename, int p_LineNo, char* p_Message);
void WaLogDebug(char* p_Filename, int p_LineNo, char* p_Message);
void WaLogInfo(char* p_Filename, int p_LineNo, char* p_Message);
void WaLogWarning(char* p_Filename, int p_LineNo, char* p_Message);
void WaLogError(char* p_Filename, int p_LineNo, char* p_Message);
}
