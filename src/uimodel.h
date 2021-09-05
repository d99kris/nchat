// uimodel.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <mutex>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "protocol.h"

#define EMOJI_PAD 1

class UiView;

class UiModel
{
public:
  UiModel();
  virtual ~UiModel();

  void KeyHandler(wint_t p_Key);
  void SendMessage();
  void EntryKeyHandler(wint_t p_Key);
  void SetTyping(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsTyping);

  void NextChat();
  void PrevChat();
  void UnreadChat();
  void PrevPage();
  void NextPage();
  void Home();
  void HomeFetchNext(const std::string& p_ProfileId, const std::string& p_ChatId, int p_MsgCount);
  void End();
  void MarkRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId);
  void DeleteMessage();
  void OpenMessageAttachment();
  void SaveMessageAttachment();
  void TransferFile();
  void InsertEmoji();
  void SearchContact();

  void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  void AddProtocol(std::shared_ptr<Protocol> p_Protocol);
  std::unordered_map<std::string, std::shared_ptr<Protocol>>& GetProtocols();
  bool Process();

  void UpdateChatInfoLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId);
  void UpdateChatInfoIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetContactName(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId);
  bool GetChatIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetChatStatus(const std::string& p_ProfileId, const std::string& p_ChatId);

  std::wstring& GetEntryStr();
  int& GetEntryPos();

  std::vector<std::pair<std::string, std::string>>& GetChatVec();
  std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> GetContactInfos();
  int64_t GetContactInfosUpdateTime();
  std::pair<std::string, std::string>& GetCurrentChat();
  int& GetCurrentChatIndex();

  std::unordered_map<std::string, ChatMessage>& GetMessages(const std::string& p_ProfileId,
                                                            const std::string& p_ChatId);
  std::vector<std::string>& GetMessageVec(const std::string& p_ProfileId, const std::string& p_ChatId);
  int& GetMessageOffset(const std::string& p_ProfileId, const std::string& p_ChatId);

  void SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline);
  void RequestContacts();
  void SetRunning(bool p_Running);

  void SetSelectMessage(bool p_SelectMessage);
  bool GetSelectMessage();

  bool GetListDialogActive();
  void SetListDialogActive(bool p_ListDialogActive);

  bool GetMessageDialogActive();
  void SetMessageDialogActive(bool p_MessageDialogActive);

  void SetHelpOffset(int p_HelpOffset);
  int GetHelpOffset();

  bool GetEmojiEnabled();

private:
  void SortChats();
  void OnCurrentChatChanged();
  void RequestMessages();
  int GetHistoryLines();
  void ReinitView();
  void UpdateList();
  void UpdateStatus();
  void UpdateHelp();
  void UpdateHistory();
  void UpdateEntry();
  void NotifyNewUnread();
  void ResetMessageOffset();

private:
  bool m_Running = true;
  std::shared_ptr<UiView> m_View;

  std::mutex m_ModelMutex;
  std::unordered_map<std::string, std::shared_ptr<Protocol>> m_Protocols;

  std::unordered_map<std::string, std::unordered_set<std::string>> m_ChatSet;
  std::vector<std::pair<std::string, std::string>> m_ChatVec;
  std::unordered_map<std::string, std::unordered_map<std::string, ChatInfo>> m_ChatInfos;
  std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> m_ContactInfos;
  int64_t m_ContactInfosUpdateTime = 0;

  std::pair<std::string, std::string> m_CurrentChat;
  int m_CurrentChatIndex = -1;

  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> m_MessageVec;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::unordered_map<std::string, ChatMessage>>> m_Messages;
  std::unordered_map<std::string, std::unordered_map<std::string, int>> m_MessageOffset;
  std::unordered_map<std::string, std::unordered_map<std::string, std::stack<int>>> m_MessageOffsetStack;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::unordered_set<std::string>>> m_MsgFromIdsRequested;
  std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_FetchedAllCache;

  std::unordered_map<std::string, std::unordered_map<std::string, std::wstring>> m_EntryStr;
  std::unordered_map<std::string, std::unordered_map<std::string, int>> m_EntryPos;

  std::unordered_map<std::string, std::unordered_map<std::string, std::set<std::string>>> m_UsersTyping;
  std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_UserOnline;

  bool m_SelectMessage = false;
  bool m_ListDialogActive = false;
  bool m_MessageDialogActive = false;
  bool m_TriggerTerminalBell = false;
  bool m_HomeFetchAll = false;

  int m_HelpOffset = 0;
};
