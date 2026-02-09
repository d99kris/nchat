// uimodel.h
//
// Copyright (c) 2019-2025 Kristofer Berggren
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

#include "apputil.h"
#include "owned_mutex.h"
#include "protocol.h"

class UiView;

class UiModel
{
private:
  class Impl
  {
  public:
    Impl(UiModel* p_UiModel);
    virtual ~Impl();

    void Init();
    void Cleanup();

    void AnyUserKeyInput();
    void TerminalResize();
    void OnKeyDecreaseListWidth();
    void OnKeyIncreaseListWidth();
    void OnKeyToggleHelp();
    void OnKeyToggleList();
    void OnKeyToggleTop();
    void OnKeyToggleEmoji();
    void OnKeySendMsg();
    void SendMessage();
    void OnKeyOtherCommandsHelp();
    void EntryKeyHandler(wint_t p_Key);
    void SetTyping(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsTyping);

    void OnKeyNextChat();
    void OnKeyPrevChat();
    void OnKeyUnreadChat();
    void OnKeyPrevPage();
    void OnKeyNextPage();
    bool OnKeyHomeFetchCache();
    void OnKeyHomeFetchNext();
    void OnKeyEnd();
    void HomeFetchNext(const std::string& p_ProfileId, const std::string& p_ChatId, int p_MsgCount);
    void MarkRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                  bool p_WasUnread);
    void OnStatusUpdate(uint32_t p_Status);
    void DownloadAttachment(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                            const std::string& p_FileId, DownloadFileAction p_DownloadFileAction);
    void OnKeyDeleteMsg();
    void OnKeyDeleteChat();
    void OnKeyOpenMsg();
    bool GetMessageAttachmentPath(std::string& p_FilePath, DownloadFileAction p_DownloadFileAction);
    void OnKeyOpenAttachment(std::string p_FilePath = std::string());
    void OpenLink(const std::string& p_Url);
    void OpenAttachment(const std::string& p_Path);
    bool RunCommand(const std::string& p_Cmd, std::string* p_StdOut = nullptr);
    void RunProgram(const std::string& p_Cmd);
    void OnKeyOpenLink();
    std::string OnKeySaveAttachment(std::string p_FilePath = std::string());
    void TransferFile(const std::vector<std::string>& p_FilePaths);
    void InsertEmoji(const std::wstring& p_Emoji);
    void OpenCreateChat(const std::pair<std::string, std::string>& p_Chat);
    void FetchCachedMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                            const std::string& p_MsgId);

    void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
    void AddProtocol(std::shared_ptr<Protocol> p_Protocol);
    std::unordered_map<std::string, std::shared_ptr<Protocol>> GetProtocols();
    bool Process();
    void ProcessTimers();

    std::string GetLastMessageId(const std::string& p_ProfileId, const std::string& p_ChatId);
    void UpdateChatInfoLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId);
    void UpdateChatInfoIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId);
    std::string GetContactName(const std::string& p_ProfileId, const std::string& p_ChatId);
    std::string GetContactNameIncludingSelf(const std::string& p_ProfileId, const std::string& p_ChatId);
    std::string GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_AllowId,
                                   bool p_AllowAlias);
    std::string GetContactPhone(const std::string& p_ProfileId, const std::string& p_ChatId);
    int64_t GetLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId);
    bool GetChatIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId);
    std::string GetChatStatus(const std::string& p_ProfileId, const std::string& p_ChatId);

    std::wstring& GetEntryStr();
    int& GetEntryPos();

    std::vector<std::pair<std::string, std::string>>& GetChatVec();
    std::vector<std::pair<std::string, std::string>>& GetChatVecLock();
    std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> GetContactInfos();
    int64_t GetContactInfosUpdateTime();
    std::pair<std::string, std::string>& GetCurrentChat();
    bool IsCurrentChat(const std::string& p_ProfileId, const std::string& p_ChatId);
    int& GetCurrentChatIndex();

    std::unordered_map<std::string, ChatMessage>& GetMessages(const std::string& p_ProfileId,
                                                              const std::string& p_ChatId);
    std::vector<std::string>& GetMessageVec(const std::string& p_ProfileId, const std::string& p_ChatId);
    int& GetMessageOffset(const std::string& p_ProfileId, const std::string& p_ChatId);

    void SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline);
    void RequestContacts();
    int GetScreenWidth();
    int GetScreenHeight();
    void SetRunning(bool p_Running);

    void SetSelectMessageActive(bool p_SelectMessageActive);
    bool GetSelectMessageActive();

    bool GetListDialogActive();
    void SetListDialogActive(bool p_ListDialogActive);

    bool GetFileListDialogActive();
    void SetFileListDialogActive(bool p_FileListDialogActive);

    bool GetMessageDialogActive();
    void SetMessageDialogActive(bool p_MessageDialogActive);

    bool GetEditMessageActive();
    void SetEditMessageActive(bool p_EditMessageActive);

    bool GetFindMessageActive();
    void SetFindMessageActive(bool p_FindMessageActive);

    void SetHelpOffset(int p_HelpOffset);
    int GetHelpOffset();

    bool GetEmojiEnabled();
    bool GetEmojiEnabledLock();
    void SetTerminalActive(bool p_TerminalActive);

    bool IsMultipleProfiles();
    std::string GetProfileDisplayName(const std::string& p_ProfileId);
    void GetAvailableEmojis(std::set<std::string>& p_AvailableEmojis, bool& p_Pending);
    void OnKeyJumpQuoted();

    void Draw();
    void ReinitView();

    void OnKeyQuit();
    void OnKeyExtEdit();
    void Cut();
    void Copy();
    void Paste();
    void OnKeyCancel();
    bool PreEditMsg(std::string& p_ProfileId, std::string& p_ChatId, std::string& p_MsgId,
                    std::string& p_MsgDialogText);
    void StartEditMsg(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId);
    void OnKeySpell();
    void OnKeyExtCall();
    std::string PreExtCall();
    void StartExtCall(const std::string& p_Phone);

    void GotoChat(const std::pair<std::string, std::string>& p_Chat);
    bool PreReact(std::string& p_ProfileId, std::string& p_ChatId, std::string& p_SenderId,
                  std::string& p_MsgId, std::string& p_SelfEmoji, bool& p_HasLimitedReactions);
    void SetReact(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_SenderId,
                  const std::string& p_MsgId, const std::string& p_SelfEmoji, const std::string& p_Emoji);
    void Find(const std::string& p_FindText);
    void ForwardMessage(const std::pair<std::string, std::string>& p_Chat);
    void PerformForwardMessage(const std::pair<std::string, std::string>& p_Chat);
    bool IsProtocolUiControlActive();
    void HandleProtocolUiControlStart();
    void HandleProtocolUiControlEnd();
    bool AutoCompose();
    bool TranscribeAudio(bool p_ForceRetranscribe);
    std::string GetCurrentTranscriptionLanguage(const std::string& p_ProfileId, const std::string& p_ChatId);
    void UpdateCurrentTranscriptionLanguage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                            const std::string& p_Language);

    static bool IsAttachmentDownloaded(const FileInfo& p_FileInfo);
    static bool IsAttachmentDownloadable(const FileInfo& p_FileInfo);
    static void SanitizeEntryStr(std::string& p_Str);

  private:
    void SortChats();
    void OnCurrentChatChanged();
    void RequestMessagesCurrentChat();
    void RequestMessagesNextChat();
    void RequestMessages(const std::string& p_ProfileId, const std::string& p_ChatId);
    void RequestUserStatusCurrentChat();
    void RequestUserStatusNextChat();
    void RequestUserStatus(const std::pair<std::string, std::string>& p_Chat);
    void ProtocolSetCurrentChat();
    int GetHistoryLines();
    void UpdateList();
    void UpdateStatus();
    void UpdateHelp();
    void UpdateHistory();
    void UpdateEntry();
    void ResetMessageOffset();
    bool IsCurrentChatSet();
    bool SetCurrentChatIndexIfNotSet();
    void DesktopNotify(const std::string& p_Name, const std::string& p_Text);
    void SetHistoryInteraction(bool p_HistoryInteraction);
    std::string GetSelectedMessageText();
    void Clear(bool p_AllowUndo);
    void SaveEditMessage();
    std::string EntryStrToSendStr(const std::wstring& p_EntryStr);
    void CallExternalEdit(const std::string& p_EditorCmd);
    const std::pair<std::string, std::string>& GetNextChat();
    void SendProtocolRequest(const std::string& p_ProfileId, std::shared_ptr<RequestMessage> p_Request);
    bool HasProtocolFeature(const std::string& p_ProfileId, ProtocolFeature p_ProtocolFeature);
    std::string GetSelfId(const std::string& p_ProfileId);
    void Quit();
    void EntryConvertEmojiEnabled();
    void SetProtocolUiControl(const std::string& p_ProfileId, bool& p_IsTakeControl);
    void PerformFindNext(const std::string& p_FindText);
    bool IsChatForceHidden(const std::string& p_ChatId);
    bool IsChatForceMuted(const std::string& p_ChatId);
    void AddQuoteFromSelectedMessage(ChatMessage& p_ChatMessage);

  private:
    bool m_Running = true;
    std::shared_ptr<UiView> m_View;

    std::unordered_map<std::string, std::shared_ptr<Protocol>> m_Protocols;

    std::unordered_map<std::string, std::unordered_set<std::string>> m_ChatSet;
    std::vector<std::pair<std::string, std::string>> m_ChatVec;
    std::unordered_map<std::string, std::unordered_map<std::string, ChatInfo>> m_ChatInfos;
    std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> m_ContactInfos;
    int64_t m_ContactInfosUpdateTime = 0;
    std::unordered_map<std::string, int64_t> m_ConnectTime;
    int64_t m_LastSyncMessageTime = 0;

    std::pair<std::string, std::string> m_CurrentChat;
    int m_CurrentChatIndex = -1;
    static const std::pair<std::string, std::string> s_ChatNone;

    std::string m_EditMessageId;
    std::string m_ProtocolUiControl;

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> m_MessageVec;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::unordered_map<std::string, ChatMessage>>> m_Messages;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> m_MessageOffset;
    std::unordered_map<std::string, std::unordered_map<std::string, std::stack<int>>> m_MessageOffsetStack;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::unordered_set<std::string>>> m_MsgFromIdsRequested;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_FetchedAllCache;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_OldestMessageId;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> m_OldestMessageTime;

    std::unordered_map<std::string, std::unordered_map<std::string, std::wstring>> m_EntryStr;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> m_EntryPos;

    std::unordered_map<std::string, std::unordered_map<std::string, std::wstring>> m_EntryStrCleared;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> m_EntryPosCleared;

    std::unordered_map<std::string, std::unordered_map<std::string, std::set<std::string>>> m_UsersTyping;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_UserOnline;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> m_UserTimeSeen;

    std::unordered_map<std::string, std::unordered_map<std::string, std::set<std::string>>> m_AvailableReactions;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> m_AvailableReactionsPending;

    bool m_SelectMessageActive = false;
    bool m_ListDialogActive = false;
    bool m_FileListDialogActive = false;
    bool m_MessageDialogActive = false;
    bool m_EditMessageActive = false;
    bool m_FindMessageActive = false;

    bool m_TriggerTerminalBell = false;
    bool m_HomeFetchAll = false;
    bool m_TerminalActive = true;
    bool m_HistoryInteraction = false;

    int m_HelpOffset = 0;
  };

public:
  // Methods acquiring model mutex as needed (intended for main, Ui, Ui*Dialog classes)
  UiModel();
  virtual ~UiModel();
  void Init();
  void Cleanup();
  void ReinitView();

  void AddProtocol(std::shared_ptr<Protocol> p_Protocol);
  std::unordered_map<std::string, std::shared_ptr<Protocol>> GetProtocols();
  bool IsMultipleProfiles();

  void Draw();
  void KeyHandler(wint_t p_Key);
  void MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage);
  bool Process();
  void RequestContacts();
  int GetScreenWidth();
  int GetScreenHeight();

  void GetAvailableEmojis(std::set<std::string>& p_AvailableEmojis, bool& p_Pending);
  std::vector<std::pair<std::string, std::string>> GetChatVec();
  std::string GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_AllowId,
                                 bool p_AllowAlias);
  std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> GetContactInfos();
  std::string GetProfileDisplayName(const std::string& p_ProfileId);

  int64_t GetContactInfosUpdateTime();
  bool GetEmojiEnabled();
  int GetHelpOffset();
  void SetHelpOffset(int p_HelpOffset);
  void SetMessageDialogActive(bool p_MessageDialogActive);
  void SetListDialogActive(bool p_ListDialogActive);
  void SetFileListDialogActive(bool p_FileListDialogActive);
  void SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline);
  void SetTerminalActive(bool p_TerminalActive);

  // Locked methods require caller to hold model mutex (intended for Ui*View classes)
  bool GetChatIsUnreadLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetChatStatusLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::vector<std::pair<std::string, std::string>>& GetChatVecLocked();
  std::string GetContactListNameLocked(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_AllowId,
                                       bool p_AllowAlias);
  std::string GetContactNameLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetContactPhoneLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  int GetCurrentChatIndexLocked();
  std::pair<std::string, std::string>& GetCurrentChatLocked();
  bool GetEditMessageActiveLocked();
  bool GetEmojiEnabledLocked();
  int GetEntryPosLocked();
  std::wstring GetEntryStrLocked();
  int GetHelpOffsetLocked();
  int64_t GetLastMessageTimeLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  bool GetListDialogActiveLocked();
  bool GetFileListDialogActiveLocked();
  bool GetMessageDialogActiveLocked();
  int GetMessageOffsetLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::unordered_map<std::string, ChatMessage>& GetMessagesLocked(const std::string& p_ProfileId,
                                                                  const std::string& p_ChatId);
  std::vector<std::string>& GetMessageVecLocked(const std::string& p_ProfileId, const std::string& p_ChatId);
  std::string GetProfileDisplayNameLocked(const std::string& p_ProfileId);
  bool GetSelectMessageActiveLocked();

  void DownloadAttachmentLocked(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                                const std::string& p_FileId, DownloadFileAction p_DownloadFileAction);
  void FetchCachedMessageLocked(const std::string& p_ProfileId, const std::string& p_ChatId,
                                const std::string& p_MsgId);
  bool IsMultipleProfilesLocked();
  void MarkReadLocked(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                      bool p_WasUnread);
  void OnStatusUpdateLocked(uint32_t p_Status);

  // Static methods
  static bool IsAttachmentDownloaded(const FileInfo& p_FileInfo);
  static bool IsAttachmentDownloadable(const FileInfo& p_FileInfo);

private:
  // Encapsulate class data in a nested class using OImpl (Object Implementation, non-opaque
  // variant of PImpl avoiding pointer indirection) for clear separation of internal data access
  // (where ownership of mutex can be assumed) and external interface (managing mutex).
  inline Impl& GetImpl()
  {
#ifndef NCHAT_BUILD_RELEASE
    nc_assert(m_ModelMutex.owns_lock());
#endif
    return m_Impl;
  }

  void OnKeyGotoChat();
  std::vector<std::string> SelectFile();
  void OnKeyTransfer();
  void OnKeySelectEmoji();
  void OnKeySelectContact();
  void OnKeyReact();
  void OnKeyFind();
  void OnKeyFindNext();
  void OnKeyForwardMsg();
  bool MessageDialog(const std::string& p_Title, const std::string& p_Text, float p_WReq, float p_HReq);
  void OnKeyDeleteMsg();
  void OnKeyDeleteChat();
  void OnKeySaveAttachment();
  void OnKeyEditMsg();
  void OnKeyQuit();
  void OnKeyExtCall();
  void OnKeyAutoCompose();
  void OnKeyCut();
  void OnKeyCopy();
  void OnKeyPaste();
  void OnKeyTranscribeAudio();
  void OnKeyRetranscribeAudio();
  void OnKeySetTranscriptionLang();

private:
  Impl m_Impl;
  owned_mutex m_ModelMutex;

  std::string m_FindText;
};
