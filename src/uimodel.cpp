// uimodel.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uimodel.h"

#include <algorithm>

#include <ncurses.h>

#include "appconfig.h"
#include "apputil.h"
#include "clipboard.h"
#include "fileutil.h"
#include "log.h"
#include "numutil.h"
#include "protocolutil.h"
#include "sethelp.h"
#include "status.h"
#include "strutil.h"
#include "sysutil.h"
#include "timeutil.h"
#include "uidialog.h"
#include "uichatlistdialog.h"
#include "uiconfig.h"
#include "uicontactlistdialog.h"
#include "uicontroller.h"
#include "uiemojilistdialog.h"
#include "uifilelistdialog.h"
#include "uikeyconfig.h"
#include "uikeyinput.h"
#include "uimessagedialog.h"
#include "uitextinputdialog.h"
#include "uiview.h"

// ---------------------------------------------------------------------
// UiModel::Impl
// ---------------------------------------------------------------------

const std::pair<std::string, std::string> UiModel::Impl::s_ChatNone;

UiModel::Impl::Impl(UiModel* p_UiModel)
{
  m_View = std::make_shared<UiView>(p_UiModel);
}

UiModel::Impl::~Impl()
{
}

void UiModel::Impl::Init()
{
  m_View->Init();
}

void UiModel::Impl::Cleanup()
{
}

void UiModel::Impl::AnyUserKeyInput()
{
  // set current chat upon any user interaction
  if (SetCurrentChatIndexIfNotSet())
  {
    LOG_TRACE("set current chat on key input");
  }

  if (m_HomeFetchAll)
  {
    LOG_TRACE("home fetch stopped");
    m_HomeFetchAll = false;
  }
}

void UiModel::Impl::TerminalResize()
{
  SetHelpOffset(0);
  ReinitView();
}

void UiModel::Impl::OnKeyDecreaseListWidth()
{
  AnyUserKeyInput();
  m_View->DecreaseListWidth();
  ReinitView();
}

void UiModel::Impl::OnKeyIncreaseListWidth()
{
  AnyUserKeyInput();
  m_View->IncreaseListWidth();
  ReinitView();
}

void UiModel::Impl::OnKeyToggleHelp()
{
  AnyUserKeyInput();
  m_View->SetHelpEnabled(!m_View->GetHelpEnabled());
  ReinitView();
}

void UiModel::Impl::OnKeyToggleList()
{
  AnyUserKeyInput();
  m_View->SetListEnabled(!m_View->GetListEnabled());
  ReinitView();
}

void UiModel::Impl::OnKeyToggleTop()
{
  AnyUserKeyInput();
  m_View->SetTopEnabled(!m_View->GetTopEnabled());
  ReinitView();
}

void UiModel::Impl::OnKeyToggleEmoji()
{
  AnyUserKeyInput();
  m_View->SetEmojiEnabled(!m_View->GetEmojiEnabled());
  EntryConvertEmojiEnabled();
  UpdateList();
  UpdateStatus();
  UpdateHistory();
  UpdateEntry();
}

void UiModel::Impl::OnKeySendMsg()
{
  AnyUserKeyInput();
  bool editMessageActive = GetEditMessageActive();
  if (editMessageActive)
  {
    SaveEditMessage();
  }
  else
  {
    SendMessage();
  }
}

void UiModel::Impl::SendMessage()
{
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  std::wstring& entryStr = m_EntryStr[profileId][chatId];
  int& entryPos = m_EntryPos[profileId][chatId];

  if (entryStr.empty()) return;

  std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
  sendMessageRequest->chatId = chatId;
  sendMessageRequest->chatMessage.text = EntryStrToSendStr(entryStr);

  if (GetSelectMessageActive())
  {
    AddQuoteFromSelectedMessage(sendMessageRequest->chatMessage);
    SetSelectMessageActive(false);
  }

  SendProtocolRequest(profileId, sendMessageRequest);

  entryStr.clear();
  entryPos = 0;

  UpdateEntry();
  ResetMessageOffset();
  SetHistoryInteraction(true);
}

void UiModel::Impl::OnKeyOtherCommandsHelp()
{
  AnyUserKeyInput();
  SetHelpOffset(GetHelpOffset() + 1);
  m_View->Draw();
}

void UiModel::Impl::EntryKeyHandler(wint_t p_Key)
{
  AnyUserKeyInput();

  static wint_t keyDown = UiKeyConfig::GetKey("down");
  static wint_t keyUp = UiKeyConfig::GetKey("up");
  static wint_t keyLeft = UiKeyConfig::GetKey("left");
  static wint_t keyRight = UiKeyConfig::GetKey("right");
  static wint_t keyBackspace = UiKeyConfig::GetKey("backspace");
  static wint_t keyBackspaceAlt = UiKeyConfig::GetKey("backspace_alt");
  static wint_t keyDelete = UiKeyConfig::GetKey("delete");
  static wint_t keyDeleteLineAfterCursor = UiKeyConfig::GetKey("delete_line_after_cursor");
  static wint_t keyDeleteLineBeforeCursor = UiKeyConfig::GetKey("delete_line_before_cursor");
  static wint_t keyBeginLine = UiKeyConfig::GetKey("begin_line");
  static wint_t keyEndLine = UiKeyConfig::GetKey("end_line");
  static wint_t keyBackwardWord = UiKeyConfig::GetKey("backward_word");
  static wint_t keyForwardWord = UiKeyConfig::GetKey("forward_word");
  static wint_t keyBackwardKillWord = UiKeyConfig::GetKey("backward_kill_word");
  static wint_t keyKillWord = UiKeyConfig::GetKey("kill_word");
  static wint_t keyClear = UiKeyConfig::GetKey("clear");
  static wint_t keyLinebreak = UiKeyConfig::GetKey("linebreak");

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];

  const int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];

  if (p_Key == keyUp)
  {
    if (GetSelectMessageActive() && !GetEditMessageActive())
    {
      messageOffset = std::min(messageOffset + 1, messageCount - 1);
      RequestMessagesCurrentChat();
    }
    else
    {
      if ((entryPos == 0) && (messageCount > 0) && !GetEditMessageActive())
      {
        SetSelectMessageActive(true);
      }
      else
      {
        int cx = 0;
        int cy = 0;
        int width = m_View->GetEntryWidth();
        std::vector<std::wstring> lines =
          StrUtil::WordWrap(entryStr, width, false, false, false, 2, entryPos, cy, cx);
        if (cy > 0)
        {
          int stepsBack = 0;
          int prevLineLen = lines.at(cy - 1).size();
          if (prevLineLen > cx)
          {
            stepsBack = prevLineLen + 1;
          }
          else
          {
            stepsBack = cx + 1;
          }

          stepsBack = std::min(stepsBack, width);
          entryPos = NumUtil::Bound(0, entryPos - stepsBack, (int)entryStr.size());

          if ((entryPos < (int)entryStr.size()) && (entryStr.at(entryPos) == (wchar_t)EMOJI_PAD))
          {
            entryPos = NumUtil::Bound(0, entryPos - 1, (int)entryStr.size());
          }
        }
        else
        {
          entryPos = 0;
        }
      }
    }

    UpdateHistory();
  }
  else if (p_Key == keyDown)
  {
    if (GetSelectMessageActive() && !GetEditMessageActive())
    {
      if (messageOffset > 0)
      {
        messageOffset = messageOffset - 1;
      }
      else
      {
        SetSelectMessageActive(false);
      }
    }
    else
    {
      if (entryPos < (int)entryStr.size())
      {
        int cx = 0;
        int cy = 0;
        int width = m_View->GetEntryWidth();
        std::vector<std::wstring> lines =
          StrUtil::WordWrap(entryStr, width, false, false, false, 2, entryPos, cy, cx);

        int stepsForward = (int)lines.at(cy).size() - cx + 1;
        if ((cy + 1) < (int)lines.size())
        {
          if ((int)lines.at(cy + 1).size() > cx)
          {
            stepsForward += cx;
          }
          else
          {
            stepsForward += lines.at(cy + 1).size();
          }
        }

        stepsForward = std::min(stepsForward, width);
        entryPos = NumUtil::Bound(0, entryPos + stepsForward, (int)entryStr.size());

        if ((entryPos < (int)entryStr.size()) && (entryStr.at(entryPos) == (wchar_t)EMOJI_PAD))
        {
          entryPos = NumUtil::Bound(0, entryPos - 1, (int)entryStr.size());
        }
      }
    }

    UpdateHistory();
  }
  else if (p_Key == keyLeft)
  {
    entryPos = NumUtil::Bound(0, entryPos - 1, (int)entryStr.size());
    if ((entryPos < (int)entryStr.size()) && (entryStr.at(entryPos) == (wchar_t)EMOJI_PAD))
    {
      entryPos = NumUtil::Bound(0, entryPos - 1, (int)entryStr.size());
    }
  }
  else if (p_Key == keyRight)
  {
    entryPos = NumUtil::Bound(0, entryPos + 1, (int)entryStr.size());
    if ((entryPos < (int)entryStr.size()) && (entryStr.at(entryPos) == (wchar_t)EMOJI_PAD))
    {
      entryPos = NumUtil::Bound(0, entryPos + 1, (int)entryStr.size());
    }
  }
  else if ((p_Key == keyBackspace) || (p_Key == keyBackspaceAlt))
  {
    if (entryPos > 0)
    {
      bool wasPad = (entryStr.at(entryPos - 1) == (wchar_t)EMOJI_PAD);
      entryStr.erase(--entryPos, 1);
      if (wasPad)
      {
        entryStr.erase(--entryPos, 1);
      }
      SetTyping(profileId, chatId, true);
    }
  }
  else if (p_Key == keyDelete)
  {
    if (entryPos < (int)entryStr.size())
    {
      entryStr.erase(entryPos, 1);
      if ((entryPos < (int)entryStr.size()) && (entryStr.at(entryPos) == (wchar_t)EMOJI_PAD))
      {
        entryStr.erase(entryPos, 1);
      }
      SetTyping(profileId, chatId, true);
    }
  }
  else if (p_Key == keyDeleteLineAfterCursor)
  {
    StrUtil::DeleteToNextMatch(entryStr, entryPos, 0, L"\n");
    SetTyping(profileId, chatId, true);
  }
  else if (p_Key == keyDeleteLineBeforeCursor)
  {
    StrUtil::DeleteToPrevMatch(entryStr, entryPos, -1, L"\n");
    SetTyping(profileId, chatId, true);
  }
  else if (p_Key == keyBeginLine)
  {
    StrUtil::JumpToPrevMatch(entryStr, entryPos, -1, L"\n");
  }
  else if (p_Key == keyEndLine)
  {
    StrUtil::JumpToNextMatch(entryStr, entryPos, 0, L"\n");
  }
  else if (p_Key == keyBackwardWord)
  {
    StrUtil::JumpToPrevMatch(entryStr, entryPos, -2, L" \n");
  }
  else if (p_Key == keyForwardWord)
  {
    StrUtil::JumpToNextMatch(entryStr, entryPos, 1, L" \n");
  }
  else if (p_Key == keyBackwardKillWord)
  {
    StrUtil::DeleteToPrevMatch(entryStr, entryPos, -1, L" \n");
    SetTyping(profileId, chatId, true);
  }
  else if (p_Key == keyKillWord)
  {
    StrUtil::DeleteToNextMatch(entryStr, entryPos, 0, L" \n");
    SetTyping(profileId, chatId, true);
  }
  else if (p_Key == keyClear)
  {
    entryStr.clear();
    entryPos = 0;
    SetTyping(profileId, chatId, true);
  }
  else if (p_Key == keyLinebreak)
  {
    wint_t keyLF = 0xA;
    entryStr.insert(entryPos++, 1, keyLF);
    SetTyping(profileId, chatId, true);
  }
  else if (StrUtil::IsValidTextKey(p_Key))
  {
    entryStr.insert(entryPos++, 1, p_Key);
    if (p_Key > 0xff)
    {
      if (StrUtil::WStringWidth(std::wstring(1, p_Key)) > 1)
      {
        entryStr.insert(entryPos++, std::wstring(1, (wchar_t)EMOJI_PAD));
      }
    }

    SetTyping(profileId, chatId, true);
  }
  else
  {
    return;
  }

  UpdateEntry();
}

void UiModel::Impl::SetTyping(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsTyping)
{
  static const bool typingStatusShare = UiConfig::GetBool("typing_status_share");
  if (!typingStatusShare) return;

  static std::string lastProfileId;
  static std::string lastChatId;
  static bool lastIsTyping = false;
  static int64_t lastTypeTime = 0;
  static int64_t lastSendTime = 0;

  if (!p_IsTyping && !lastIsTyping)
  {
    return;
  }

  const int64_t nowTime = TimeUtil::GetCurrentTimeMSec();
  if (((nowTime - lastTypeTime) > 3000) && !p_IsTyping && lastIsTyping)
  {
    LOG_TRACE("send stop typing %s", lastChatId.c_str());

    std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
    sendTypingRequest->chatId = lastChatId;
    sendTypingRequest->isTyping = false;
    SendProtocolRequest(lastProfileId, sendTypingRequest);

    lastProfileId = "";
    lastChatId = "";
    lastIsTyping = false;
    return;
  }

  if (p_IsTyping)
  {
    if ((p_ProfileId == lastProfileId) && (p_ChatId == lastChatId) && (p_IsTyping == lastIsTyping))
    {
      if (HasProtocolFeature(p_ProfileId, FeatureTypingTimeout) && ((nowTime - lastSendTime) > 2500))
      {
        LOG_TRACE("send typing %s refresh", p_ChatId.c_str());

        std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
        sendTypingRequest->chatId = p_ChatId;
        sendTypingRequest->isTyping = true;
        SendProtocolRequest(p_ProfileId, sendTypingRequest);
        lastSendTime = nowTime;
      }
      else
      {
        LOG_TRACE("no typing update");
      }
    }
    else
    {
      if (!lastProfileId.empty() && !lastChatId.empty() && lastIsTyping)
      {
        LOG_TRACE("send stop typing %s", lastChatId.c_str());

        std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
        sendTypingRequest->chatId = lastChatId;
        sendTypingRequest->isTyping = false;
        SendProtocolRequest(lastProfileId, sendTypingRequest);
      }

      LOG_TRACE("send typing %s", p_ChatId.c_str());

      std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
      sendTypingRequest->chatId = p_ChatId;
      sendTypingRequest->isTyping = true;
      SendProtocolRequest(p_ProfileId, sendTypingRequest);
      lastSendTime = nowTime;

      lastProfileId = p_ProfileId;
      lastChatId = p_ChatId;
      lastIsTyping = p_IsTyping;
    }

    lastTypeTime = nowTime;
  }
}

void UiModel::Impl::OnKeyNextChat()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  if (m_ChatVec.empty()) return;

  ++m_CurrentChatIndex;
  if (m_CurrentChatIndex >= (int)m_ChatVec.size())
  {
    m_CurrentChatIndex = 0;
  }

  m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
  OnCurrentChatChanged();
  SetSelectMessageActive(false);
}

void UiModel::Impl::OnKeyPrevChat()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  if (m_ChatVec.empty()) return;

  --m_CurrentChatIndex;
  if (m_CurrentChatIndex < 0)
  {
    m_CurrentChatIndex = m_ChatVec.size() - 1;
  }

  m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
  OnCurrentChatChanged();
  SetSelectMessageActive(false);
}

void UiModel::Impl::OnKeyUnreadChat()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  if (m_ChatVec.empty()) return;

  std::vector<int> unreadVec;
  bool unreadIsSelected = false;

  for (size_t i = 0; i < m_ChatVec.size(); ++i)
  {
    const std::pair<std::string, std::string>& chat = m_ChatVec.at(i);
    const bool isUnread = GetChatIsUnread(chat.first, chat.second);
    if (isUnread)
    {
      unreadVec.push_back(i);
      if (m_CurrentChatIndex == (int)i)
      {
        unreadIsSelected = true;
      }
    }
  }

  if (!unreadVec.empty())
  {
    if (!unreadIsSelected)
    {
      m_CurrentChatIndex = unreadVec.at(0);
    }
    else if (unreadVec.size() > 1)
    {
      auto it = std::find(unreadVec.begin(), unreadVec.end(), m_CurrentChatIndex);
      if (it != unreadVec.end())
      {
        size_t idx = std::distance(unreadVec.begin(), it);
        ++idx;
        if (idx >= unreadVec.size())
        {
          idx = 0;
        }

        m_CurrentChatIndex = unreadVec.at(idx);
      }
    }

    m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
    OnCurrentChatChanged();
    SetSelectMessageActive(false);
  }
}

void UiModel::Impl::OnKeyPrevPage()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  int historyShowCount = m_View->GetHistoryShowCount();
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  const int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  int addOffset = std::min(historyShowCount, std::max(messageCount - messageOffset - 1, 0));
  LOG_TRACE("count %d offset %d addoffset %d", messageCount, messageOffset, addOffset);
  if (addOffset > 0)
  {
    messageOffsetStack.push(addOffset);
    messageOffset += addOffset;
    RequestMessagesCurrentChat();
    UpdateHistory();
  }

  SetSelectMessageActive(false);
}

void UiModel::Impl::OnKeyNextPage()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  int decOffset = 0;
  if (!messageOffsetStack.empty())
  {
    decOffset = messageOffsetStack.top();
    messageOffsetStack.pop();
  }
  else if (messageOffset > 0)
  {
    decOffset = messageOffset;
  }

  if (decOffset > 0)
  {
    messageOffset = std::max((messageOffset - decOffset), 0);
    UpdateHistory();
  }
  else
  {
    SetHistoryInteraction(true);
  }

  SetSelectMessageActive(false);
}

bool UiModel::Impl::OnKeyHomeFetchCache()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return false;

  static const bool homeFetchAll = UiConfig::GetBool("home_fetch_all");
  if (homeFetchAll)
  {
    m_HomeFetchAll = true;
    LOG_TRACE("home fetch start");
  }

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  bool& fetchedAllCache = m_FetchedAllCache[profileId][chatId];
  if (!fetchedAllCache)
  {
    fetchedAllCache = true;
    std::string fromId = GetLastMessageId(profileId, chatId);
    int limit = std::numeric_limits<int>::max();
    LOG_DEBUG("fetch all");
    std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::make_shared<GetMessagesRequest>();
    getMessagesRequest->chatId = chatId;
    getMessagesRequest->fromMsgId = fromId;
    getMessagesRequest->limit = limit;
    LOG_TRACE("request messages from %s limit %d", fromId.c_str(), limit);
    SendProtocolRequest(m_CurrentChat.first, getMessagesRequest);
    return true;
  }

  return false;
}

void UiModel::Impl::OnKeyHomeFetchNext()
{
  if (GetEditMessageActive()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  const int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  int addOffset = std::max(messageCount - messageOffset - 1, 0);
  LOG_TRACE("count %d offset %d addoffset %d", messageCount, messageOffset, addOffset);
  if (addOffset > 0)
  {
    for (int i = 0; i < addOffset; ++i)
    {
      messageOffsetStack.push(1); // @todo: consider building a nicer stack for page down from home
    }

    messageOffset += addOffset;
    RequestMessagesCurrentChat();
    UpdateHistory();
  }

  SetSelectMessageActive(false);
}

void UiModel::Impl::HomeFetchNext(const std::string& p_ProfileId, const std::string& p_ChatId, int p_MsgCount)
{
  if (m_HomeFetchAll)
  {
    if ((p_ProfileId == m_CurrentChat.first) && (p_ChatId == m_CurrentChat.second))
    {
      if (p_MsgCount > 0)
      {
        const int messageCount = m_Messages[p_ProfileId][p_ChatId].size();
        int& messageOffset = m_MessageOffset[p_ProfileId][p_ChatId];
        std::stack<int>& messageOffsetStack = m_MessageOffsetStack[p_ProfileId][p_ChatId];

        if ((p_MsgCount == 1) && ((messageCount % 8) != 0))
        {
          LOG_TRACE("home fetch skip");
          return;
        }

        int addOffset = std::max(messageCount - messageOffset - 1, 0);
        if (addOffset > 0)
        {
          for (int i = 0; i < addOffset; ++i)
          {
            messageOffsetStack.push(1);
          }

          messageOffset += addOffset;
        }

        LOG_TRACE("home fetch offset + %d = %d", addOffset, messageOffset);
        RequestMessagesCurrentChat();
        UpdateHistory();
      }
      else
      {
        LOG_TRACE("home fetch complete");
        m_HomeFetchAll = false;
      }
    }
  }
}

void UiModel::Impl::OnKeyEnd()
{
  AnyUserKeyInput();

  if (GetEditMessageActive()) return;

  ResetMessageOffset();

  SetHistoryInteraction(true);
  SetSelectMessageActive(false);
}

void UiModel::Impl::ResetMessageOffset()
{
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  messageOffset = 0;
  while (!messageOffsetStack.empty())
  {
    messageOffsetStack.pop();
  }

  UpdateHistory();
}

void UiModel::Impl::MarkRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                             bool p_WasUnread)
{
  // Generally only mark read if message was unread, except if protocol needs to know every view
  const bool markReadEveryView = HasProtocolFeature(p_ProfileId, FeatureMarkReadEveryView);
  if (!markReadEveryView && !p_WasUnread) return;

  // mark_read_on_view must be enabled (default), or user has performed view end of history action; page down, end, etc
  static const bool markReadOnView = UiConfig::GetBool("mark_read_on_view");
  if (!markReadOnView && !m_HistoryInteraction) return;

  // Terminal must be active (default), unless mark_read_when_inactive is enabled
  static const bool markReadWhenInactive = UiConfig::GetBool("mark_read_when_inactive");
  if (!(m_TerminalActive || markReadWhenInactive)) return;

  // Current chat must be set (default), unless mark_read_any_chat is enabled
  static const bool markReadAnyChat = UiConfig::GetBool("mark_read_any_chat");
  if ((m_CurrentChatIndex < 0) || markReadAnyChat) return;

  std::string senderId;
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  auto mit = messages.find(p_MsgId);
  if (mit != messages.end())
  {
    if (p_WasUnread)
    {
      mit->second.isRead = true;
    }
    senderId = mit->second.senderId;
  }

  std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest = std::make_shared<MarkMessageReadRequest>();
  markMessageReadRequest->chatId = p_ChatId;
  markMessageReadRequest->msgId = p_MsgId;
  markMessageReadRequest->senderId = senderId;
  SendProtocolRequest(p_ProfileId, markMessageReadRequest);

  UpdateChatInfoIsUnread(p_ProfileId, p_ChatId);

  UpdateList();
}

void UiModel::Impl::OnStatusUpdate(uint32_t p_Status)
{
  if (!m_Running) return;

  const bool isOnline = (p_Status & Status::FlagOnline);

  // Ignore first transition to online status
  static bool wasEverOnline = false;
  if (!wasEverOnline)
  {
    if (isOnline)
    {
      LOG_TRACE("status online");
      wasEverOnline = true;
      m_LastSyncMessageTime = TimeUtil::GetCurrentTimeMSec();
    }

    return;
  }

  // Notify on online status change
  static bool lastOnline = true;
  if (isOnline != lastOnline)
  {
    static const bool desktopNotifyConnectivity = UiConfig::GetBool("desktop_notify_connectivity");
    if (desktopNotifyConnectivity)
    {
      DesktopNotify("Connection", isOnline ? "Online" : "Offline");
    }

    LOG_TRACE("status %s", isOnline ? "online" : "offline");

    lastOnline = isOnline;
  }
}

void UiModel::Impl::DownloadAttachment(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const std::string& p_MsgId, const std::string& p_FileId,
                                       DownloadFileAction p_DownloadFileAction)
{
  std::shared_ptr<DownloadFileRequest> downloadFileRequest = std::make_shared<DownloadFileRequest>();
  downloadFileRequest->chatId = p_ChatId;
  downloadFileRequest->msgId = p_MsgId;
  downloadFileRequest->fileId = p_FileId;
  downloadFileRequest->downloadFileAction = p_DownloadFileAction;

  SendProtocolRequest(p_ProfileId, downloadFileRequest);

  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  auto mit = messages.find(p_MsgId);
  if (mit == messages.end()) return;

  if (mit->second.fileInfo.empty())
  {
    LOG_WARNING("message has no attachment");
    return;
  }

  FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(mit->second.fileInfo);
  fileInfo.fileStatus = FileStatusDownloading;
  mit->second.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
}

void UiModel::Impl::OnKeyDeleteMsg()
{
  AnyUserKeyInput();

  const std::string& profileId = m_CurrentChat.first;
  const std::string& chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  int& messageOffset = m_MessageOffset[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id to delete");
    return;
  }

  std::string senderId;
  const std::string msgId = *it;
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
  auto mit = messages.find(msgId);
  if (mit != messages.end())
  {
    senderId = mit->second.senderId;
  }
  else
  {
    LOG_WARNING("error finding message");
  }

  std::shared_ptr<DeleteMessageRequest> deleteMessageRequest = std::make_shared<DeleteMessageRequest>();
  deleteMessageRequest->chatId = chatId;
  deleteMessageRequest->senderId = senderId;
  deleteMessageRequest->msgId = msgId;
  SendProtocolRequest(profileId, deleteMessageRequest);
}

void UiModel::Impl::OnKeyDeleteChat()
{
  AnyUserKeyInput();

  const std::string& profileId = m_CurrentChat.first;
  const std::string& chatId = m_CurrentChat.second;

  std::shared_ptr<DeleteChatRequest> deleteChatRequest = std::make_shared<DeleteChatRequest>();
  deleteChatRequest->chatId = chatId;
  SendProtocolRequest(profileId, deleteChatRequest);
}

void UiModel::Impl::OnKeyOpenMsg()
{
  AnyUserKeyInput();

  if (!GetSelectMessageActive() || GetEditMessageActive()) return;

  static const std::string openCmd = []()
  {
    std::string messageOpenCommand = UiConfig::GetStr("message_open_command");
    if (messageOpenCommand.empty())
    {
      messageOpenCommand = std::string(getenv("PAGER") ? getenv("PAGER") : "less");
    }

    return messageOpenCommand;
  }();

  const std::string profileId = m_CurrentChat.first;
  const std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end()) return;

  const std::string messageId = *it;
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
  const ChatMessage& chatMessage = messages.at(messageId);

  endwin();
  std::string tempPath = FileUtil::GetTempDir() + "/view.txt";
  FileUtil::WriteFile(tempPath, chatMessage.text);

  const std::string cmd = openCmd + " " + tempPath;
  LOG_DEBUG("launching external pager: %s", cmd.c_str());
  int rv = SysUtil::System(cmd);
  if (rv == 0)
  {
    LOG_DEBUG("external pager exited successfully");
  }
  else
  {
    LOG_WARNING("external pager exited with %d", rv);
  }

  FileUtil::RmFile(tempPath);
  refresh();
  wint_t key = 0;
  while (UiKeyInput::GetWch(&key) != ERR)
  {
    // Discard any remaining input
  }
}

bool UiModel::Impl::GetMessageAttachmentPath(std::string& p_FilePath, DownloadFileAction p_DownloadFileAction)
{

  if (!GetSelectMessageActive()) return false;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id");
    return false;
  }

  std::string msgId = *it;
  auto mit = messages.find(msgId);
  if (mit == messages.end())
  {
    LOG_WARNING("error finding message");
    return false;
  }

  if (mit->second.fileInfo.empty())
  {
    LOG_WARNING("message has no attachment");
    return false;
  }

  FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(mit->second.fileInfo);
  if (UiModel::Impl::IsAttachmentDownloaded(fileInfo))
  {
    p_FilePath = fileInfo.filePath;
    return true;
  }
  else if (UiModel::Impl::IsAttachmentDownloadable(fileInfo))
  {
    DownloadAttachment(profileId, chatId, msgId, fileInfo.fileId, p_DownloadFileAction);
    UpdateHistory();
    LOG_DEBUG("message attachment %s download started", fileInfo.fileId.c_str());
  }

  return false;
}

void UiModel::Impl::OnKeyOpenAttachment(std::string p_FilePath /*= std::string()*/)
{
  AnyUserKeyInput();
  if (p_FilePath.empty())
  {
    // user-triggered call
    if (!GetMessageAttachmentPath(p_FilePath, DownloadFileActionOpen)) return;
  }
  else
  {
    // protocol-triggered call
    LOG_TRACE("download file action open %s", p_FilePath.c_str());
  }

  OpenAttachment(p_FilePath);
}

void UiModel::Impl::OpenLink(const std::string& p_Url)
{
  static const std::string cmdTemplate = []()
  {
    std::string linkOpenCommand = UiConfig::GetStr("link_open_command");
    if (linkOpenCommand.empty())
    {
#if defined(__APPLE__)
      linkOpenCommand = "open '%1' &";
#else
      linkOpenCommand = "xdg-open >/dev/null 2>&1 '%1' &";
#endif
    }

    return linkOpenCommand;
  }();

  std::string cmd = cmdTemplate;
  StrUtil::ReplaceString(cmd, "%1", p_Url);

  RunProgram(cmd);
}

void UiModel::Impl::OpenAttachment(const std::string& p_Path)
{
  static const std::string cmdTemplate = []()
  {
    std::string attachmentOpenCommand = UiConfig::GetStr("attachment_open_command");
    if (attachmentOpenCommand.empty())
    {
#if defined(__APPLE__)
      attachmentOpenCommand = "open '%1' &";
#else
      attachmentOpenCommand = "xdg-open >/dev/null 2>&1 '%1' &";
#endif
    }

    return attachmentOpenCommand;
  }();

  std::string cmd = cmdTemplate;
  StrUtil::ReplaceString(cmd, "%1", p_Path);

  RunProgram(cmd);
}

// Used when taking over terminal is disallowed or when output needs to be captured
bool UiModel::Impl::RunCommand(const std::string& p_Cmd, std::string* p_StdOut /*= nullptr*/)
{
  return SysUtil::RunCommand(p_Cmd, p_StdOut);
}

// Used when taking over terminal is allowed
void UiModel::Impl::RunProgram(const std::string& p_Cmd)
{
  bool isBackground = (p_Cmd.back() == '&');

  if (!isBackground)
  {
    endwin();
  }

  // run command
  LOG_TRACE("cmd \"%s\" start", p_Cmd.c_str());
  int rv = SysUtil::System(p_Cmd);
  if (rv != 0)
  {
    LOG_WARNING("cmd \"%s\" failed (%d)", p_Cmd.c_str(), rv);
  }

  if (!isBackground)
  {
    refresh();
    wint_t key = 0;
    while (UiKeyInput::GetWch(&key) != ERR)
    {
      // Discard any remaining input
    }
  }
}

void UiModel::Impl::OnKeyOpenLink()
{
  AnyUserKeyInput();

  if (!GetSelectMessageActive()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id");
    return;
  }

  std::string msgId = *it;
  auto mit = messages.find(msgId);
  if (mit == messages.end())
  {
    LOG_WARNING("error finding message");
    return;
  }

  std::vector<std::string> msgUrls = StrUtil::ExtractUrlsFromStr(mit->second.text);
  const std::string linkChatId = mit->second.link;
  if (!linkChatId.empty())
  {
    LOG_DEBUG("create chat %s", linkChatId.c_str());
    std::shared_ptr<CreateChatRequest> createChatRequest = std::make_shared<CreateChatRequest>();
    createChatRequest->userId = linkChatId;
    SendProtocolRequest(profileId, createChatRequest);
    SetSelectMessageActive(false);
  }
  else if (!msgUrls.empty())
  {
    for (const auto& msgUrl : msgUrls)
    {
      LOG_DEBUG("open url %s", msgUrl.c_str());
      OpenLink(msgUrl);
    }
  }
  else
  {
    LOG_WARNING("message does not contain a link");
  }
}

std::string UiModel::Impl::OnKeySaveAttachment(std::string p_FilePath /*= std::string()*/)
{
  AnyUserKeyInput();
  if (p_FilePath.empty())
  {
    // user-triggered call
    if (!GetMessageAttachmentPath(p_FilePath, DownloadFileActionSave)) return "";
  }
  else
  {
    // protocol-triggered call
    LOG_TRACE("download file action save %s", p_FilePath.c_str());
  }

  std::string srcFileName = FileUtil::BaseName(p_FilePath);
  std::string downloadsDir = FileUtil::GetDownloadsDir();
  std::string dstFileName = srcFileName;
  int i = 1;
  while (FileUtil::Exists(downloadsDir + "/" + dstFileName))
  {
    dstFileName = FileUtil::RemoveFileExt(srcFileName) + "_" + std::to_string(i++) +
      FileUtil::GetFileExt(srcFileName);
  }

  std::string dstFilePath = downloadsDir + "/" + dstFileName;
  FileUtil::CopyFile(p_FilePath, dstFilePath);
  return dstFilePath;
}

void UiModel::Impl::TransferFile(const std::vector<std::string>& p_FilePaths)
{
  AnyUserKeyInput();

  if (!p_FilePaths.empty())
  {
    std::string profileId = m_CurrentChat.first;
    std::string chatId = m_CurrentChat.second;

    for (auto it = p_FilePaths.begin(); it != p_FilePaths.end(); ++it)
    {
      const std::string& filePath = *it;

      FileInfo fileInfo;
      fileInfo.filePath = filePath;
      fileInfo.fileType = FileUtil::GetMimeType(filePath);

      std::shared_ptr<SendMessageRequest> sendMessageRequest =
        std::make_shared<SendMessageRequest>();
      sendMessageRequest->chatId = chatId;
      sendMessageRequest->chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);

      static const bool transferSendCaption = UiConfig::GetBool("transfer_send_caption");
      if (transferSendCaption)
      {
        // add caption to first file
        if (it == p_FilePaths.begin())
        {
          std::wstring& entryStr = m_EntryStr[profileId][chatId];
          int& entryPos = m_EntryPos[profileId][chatId];

          sendMessageRequest->chatMessage.text = EntryStrToSendStr(entryStr);

          entryStr.clear();
          entryPos = 0;

          UpdateEntry();
        }
      }

      if (GetSelectMessageActive())
      {
        // add quote for first file
        if (it == p_FilePaths.begin())
        {
          AddQuoteFromSelectedMessage(sendMessageRequest->chatMessage);
          SetSelectMessageActive(false);
        }
      }

      SendProtocolRequest(profileId, sendMessageRequest);
    }
  }

  ReinitView();
  ResetMessageOffset();
  SetHistoryInteraction(true);
}

void UiModel::Impl::InsertEmoji(const std::wstring& p_Emoji)
{
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];

  entryStr.insert(entryPos, p_Emoji);
  entryPos += p_Emoji.size();

  if (m_View->GetEmojiEnabled() && (StrUtil::WStringWidth(p_Emoji) > 1))
  {
    entryStr.insert(entryPos, std::wstring(1, (wchar_t)EMOJI_PAD));
    entryPos += 1;
  }

  SetTyping(profileId, chatId, true);
  UpdateEntry();

  ReinitView();
}

void UiModel::Impl::OpenCreateChat(const std::pair<std::string, std::string>& p_Chat)
{
  std::string profileId = p_Chat.first;
  std::string userId = p_Chat.second;

  LOG_TRACE("selected %s contact %s", profileId.c_str(), userId.c_str());

  std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[profileId];
  if (profileChatInfos.count(userId))
  {
    m_CurrentChatIndex = 0;
    m_CurrentChat.first = profileId;
    m_CurrentChat.second = userId;
    SortChats();
    OnCurrentChatChanged();
    SetSelectMessageActive(false);
  }
  else
  {
    LOG_TRACE("create chat %s", userId.c_str());
    std::shared_ptr<CreateChatRequest> createChatRequest = std::make_shared<CreateChatRequest>();
    createChatRequest->userId = userId;
    SendProtocolRequest(profileId, createChatRequest);
  }

  ReinitView();
}

void UiModel::Impl::FetchCachedMessage(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const std::string& p_MsgId)
{
  static std::map<std::string, std::map<std::string, std::set<std::string>>> fetchedCache;
  std::set<std::string>& msgIdFetchedCache = fetchedCache[p_ProfileId][p_ChatId];
  if (msgIdFetchedCache.find(p_MsgId) != msgIdFetchedCache.end())
  {
    return;
  }
  else
  {
    std::shared_ptr<GetMessageRequest> getMessageRequest = std::make_shared<GetMessageRequest>();
    getMessageRequest->chatId = p_ChatId;
    getMessageRequest->msgId = p_MsgId;
    getMessageRequest->cached = true;
    LOG_TRACE("request message %s in %s", p_MsgId.c_str(), p_ChatId.c_str());
    SendProtocolRequest(p_ProfileId, getMessageRequest);

    msgIdFetchedCache.insert(p_MsgId);
  }
}

void UiModel::Impl::MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  const std::string profileId = p_ServiceMessage->profileId;
  switch (p_ServiceMessage->GetMessageType())
  {
    case ConnectNotifyType:
      {
        std::shared_ptr<ConnectNotify> connectNotify = std::static_pointer_cast<ConnectNotify>(p_ServiceMessage);
        LOG_TRACE("connect notify %d", connectNotify->success);
        if (!HasProtocolFeature(profileId, FeatureAutoGetChatsOnLogin))
        {
          std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
          LOG_TRACE("get chats");
          SendProtocolRequest(profileId, getChatsRequest);
        }

        if (connectNotify->success)
        {
          SetStatusOnline(profileId, true);
        }

        m_ConnectTime[profileId] = TimeUtil::GetCurrentTimeMSec();
        LOG_TRACE("connect time %lld", m_ConnectTime[profileId]);
      }
      break;

    case NewContactsNotifyType:
      {
        std::shared_ptr<NewContactsNotify> newContactsNotify = std::static_pointer_cast<NewContactsNotify>(
          p_ServiceMessage);
        const bool fullSync = newContactsNotify->fullSync;
        if (fullSync)
        {
          m_ContactInfos[profileId].clear();
        }

        const std::vector<ContactInfo>& contactInfos = newContactsNotify->contactInfos;
        for (auto& contactInfo : contactInfos)
        {
          LOG_TRACE("NewContacts %s", contactInfo.id.c_str());
          m_ContactInfos[profileId][contactInfo.id] = contactInfo;
        }

        m_ContactInfosUpdateTime = TimeUtil::GetCurrentTimeMSec();

        UpdateList();
        UpdateStatus();
        UpdateHistory();
      }
      break;

    case NewChatsNotifyType:
      {
        std::shared_ptr<NewChatsNotify> newChatsNotify = std::static_pointer_cast<NewChatsNotify>(p_ServiceMessage);
        if (newChatsNotify->success)
        {
          LOG_TRACE("new chats %d", newChatsNotify->chatInfos.size());
          for (auto& chatInfo : newChatsNotify->chatInfos)
          {
            LOG_TRACE("new chats %s", chatInfo.id.c_str());
            if (IsChatForceHidden(chatInfo.id)) continue;

            if (IsChatForceMuted(chatInfo.id))
            {
              chatInfo.isMuted = true;
            }

            m_ChatInfos[profileId][chatInfo.id] = chatInfo;

            if (m_ChatSet[profileId].insert(chatInfo.id).second)
            {
              m_ChatVec.push_back(std::make_pair(profileId, chatInfo.id));
            }

            UpdateChatInfoLastMessageTime(profileId, chatInfo.id);
            UpdateChatInfoIsUnread(profileId, chatInfo.id);
          }

          SortChats();
          UpdateList();
          UpdateStatus();
        }
      }
      break;

    case NewMessagesNotifyType:
      {
        std::shared_ptr<NewMessagesNotify> newMessagesNotify = std::static_pointer_cast<NewMessagesNotify>(
          p_ServiceMessage);
        if (newMessagesNotify->success)
        {
          bool hasNewMessage = false;
          const std::string& chatId = newMessagesNotify->chatId;
          if (IsChatForceHidden(chatId)) return;

          std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
          std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
          const std::vector<ChatMessage>& chatMessages = newMessagesNotify->chatMessages;
          const std::string& fromMsgId = newMessagesNotify->fromMsgId;

          if (!newMessagesNotify->cached)
          {
            LOG_TRACE("new messages %s count %d from %s", chatId.c_str(), chatMessages.size(), fromMsgId.c_str());
          }
          else
          {
            LOG_TRACE("new cached messages %s count %d from %s", chatId.c_str(), chatMessages.size(),
                      fromMsgId.c_str());
          }

          std::string& oldestMessageId = m_OldestMessageId[profileId][chatId];
          int64_t& oldestMessageTime = m_OldestMessageTime[profileId][chatId];

          for (auto& chatMessage : chatMessages)
          {
            hasNewMessage = true;
            if (messages.insert({ chatMessage.id, chatMessage }).second)
            {
              messageVec.push_back(chatMessage.id);
            }
            else
            {
              messages[chatMessage.id] = chatMessage;
            }

            if (newMessagesNotify->sequence)
            {
              int64_t messageTime = chatMessage.timeSent;
              if ((messageTime < oldestMessageTime) || (oldestMessageTime == 0))
              {
                oldestMessageTime = messageTime;
                oldestMessageId = chatMessage.id;
                LOG_TRACE("oldest %s at %lld", oldestMessageId.c_str(), oldestMessageTime);
              }
            }
          }

          if (hasNewMessage)
          {
            std::string currentMessageId;
            int& messageOffset = m_MessageOffset[profileId][chatId];
            if ((profileId == m_CurrentChat.first) && (chatId == m_CurrentChat.second))
            {
              if (GetSelectMessageActive() && (messageOffset < (int)messageVec.size()))
              {
                currentMessageId = messageVec[messageOffset];
              }
            }

            // *INDENT-OFF*
            std::sort(messageVec.begin(), messageVec.end(),
                      [&](const std::string& lhs, const std::string& rhs) -> bool
            {
              return messages.at(lhs).timeSent > messages.at(rhs).timeSent;
            });
            // *INDENT-ON*

            if ((profileId == m_CurrentChat.first) && (chatId == m_CurrentChat.second))
            {
              if (!currentMessageId.empty())
              {
                auto it = std::find(messageVec.begin(), messageVec.end(), currentMessageId);
                if (it != messageVec.end())
                {
                  messageOffset = it - messageVec.begin();
                }
              }

              if (!newMessagesNotify->cached)
              {
                RequestMessagesCurrentChat();
              }

              UpdateHistory();
            }

            SetHistoryInteraction(false);
          }

          const std::pair<std::string, std::string>& nextChat = GetNextChat();
          if ((profileId == nextChat.first) && (chatId == nextChat.second))
          {
            if (!newMessagesNotify->cached)
            {
              RequestMessagesNextChat();
            }
          }

          std::unordered_map<std::string, ChatInfo>& chatInfos = m_ChatInfos[profileId];
          if (!chatInfos.count(chatId))
          {
            LOG_TRACE("new message in unknown chat, get chat %s", chatId.c_str());
            std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
            getChatsRequest->chatIds.insert(chatId);
            SendProtocolRequest(profileId, getChatsRequest);
          }

          UpdateChatInfoLastMessageTime(profileId, chatId);
          UpdateChatInfoIsUnread(profileId, chatId);
          SortChats();
          UpdateList();
          HomeFetchNext(profileId, chatId, (int)chatMessages.size());
        }
      }
      break;

    case SendMessageNotifyType:
      {
        std::shared_ptr<SendMessageNotify> sendMessageNotify = std::static_pointer_cast<SendMessageNotify>(
          p_ServiceMessage);
        LOG_TRACE(sendMessageNotify->success ? "send ok" : "send failed");
      }
      break;

    case MarkMessageReadNotifyType:
      {
        std::shared_ptr<MarkMessageReadNotify> markMessageReadNotify = std::static_pointer_cast<MarkMessageReadNotify>(
          p_ServiceMessage);
        LOG_TRACE(markMessageReadNotify->success ? "mark read ok" : "mark read failed");
      }
      break;

    case DeleteMessageNotifyType:
      {
        std::shared_ptr<DeleteMessageNotify> deleteMessageNotify = std::static_pointer_cast<DeleteMessageNotify>(
          p_ServiceMessage);
        LOG_TRACE(deleteMessageNotify->success ? "delete ok" : "delete failed");
        if (deleteMessageNotify->success)
        {
          std::string chatId = deleteMessageNotify->chatId;
          std::string msgId = deleteMessageNotify->msgId;

          std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
          messageVec.erase(std::remove(messageVec.begin(), messageVec.end(), msgId), messageVec.end());

          std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
          messages.erase(msgId);

          if (GetSelectMessageActive())
          {
            int& messageOffset = m_MessageOffset[profileId][chatId];
            if (messageVec.empty())
            {
              messageOffset = 0;
              SetSelectMessageActive(false);
            }
            else
            {
              if ((messageOffset + 1) > (int)messageVec.size())
              {
                messageOffset = (int)messageVec.size() - 1;
              }
            }
          }

          UpdateChatInfoLastMessageTime(profileId, chatId);
          UpdateChatInfoIsUnread(profileId, chatId);
          SortChats();
          UpdateList();
          UpdateHistory();
        }
      }
      break;

    case SendTypingNotifyType:
      {
        std::shared_ptr<SendTypingNotify> sendTypingNotify =
          std::static_pointer_cast<SendTypingNotify>(p_ServiceMessage);
        LOG_TRACE(sendTypingNotify->success ? "send typing ok" : "send typing failed");
      }
      break;

    case SetStatusNotifyType:
      {
        std::shared_ptr<SetStatusNotify> setStatusNotify = std::static_pointer_cast<SetStatusNotify>(p_ServiceMessage);
        LOG_TRACE(setStatusNotify->success ? "set status ok" : "set status failed");
      }
      break;

    case NewMessageStatusNotifyType:
      {
        std::shared_ptr<NewMessageStatusNotify> newMessageStatusNotify =
          std::static_pointer_cast<NewMessageStatusNotify>(p_ServiceMessage);
        std::string chatId = newMessageStatusNotify->chatId;
        std::string msgId = newMessageStatusNotify->msgId;
        bool isRead = newMessageStatusNotify->isRead;
        LOG_TRACE("new read status %s is %s", msgId.c_str(), (isRead ? "read" : "unread"));
        std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
        auto mit = messages.find(msgId);
        if (mit != messages.end())
        {
          mit->second.isRead = isRead;
        }

        UpdateChatInfoIsUnread(profileId, chatId);
        UpdateHistory();
        UpdateList();
      }
      break;

    case NewMessageFileNotifyType:
      {
        std::shared_ptr<NewMessageFileNotify> newMessageFileNotify = std::static_pointer_cast<NewMessageFileNotify>(
          p_ServiceMessage);
        std::string chatId = newMessageFileNotify->chatId;
        std::string msgId = newMessageFileNotify->msgId;
        std::string fileInfoStr = newMessageFileNotify->fileInfo;
        DownloadFileAction downloadFileAction = newMessageFileNotify->downloadFileAction;
        LOG_TRACE("new file info for %s is %s", msgId.c_str(), fileInfoStr.c_str());
        std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
        auto mit = messages.find(msgId);
        if (mit != messages.end())
        {
          mit->second.fileInfo = fileInfoStr;
        }

        if (downloadFileAction == DownloadFileActionOpen)
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(fileInfoStr);
          if (!fileInfo.filePath.empty())
          {
            OnKeyOpenAttachment(fileInfo.filePath);
          }
        }
        else if (downloadFileAction == DownloadFileActionSave)
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(fileInfoStr);
          if (!fileInfo.filePath.empty())
          {
            OnKeySaveAttachment(fileInfo.filePath);
          }
        }

        UpdateHistory();
      }
      break;

    case NewMessageReactionsNotifyType:
      {
        std::shared_ptr<NewMessageReactionsNotify> newMessageReactionsNotify =
          std::static_pointer_cast<NewMessageReactionsNotify>(p_ServiceMessage);
        if (!newMessageReactionsNotify->reactions.needConsolidationWithCache)
        {
          std::string chatId = newMessageReactionsNotify->chatId;
          std::string msgId = newMessageReactionsNotify->msgId;

          LOG_TRACE("new reactions for %s in %s count %d", msgId.c_str(), chatId.c_str(),
                    newMessageReactionsNotify->reactions.emojiCounts.size());
          std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
          auto mit = messages.find(msgId);
          if (mit != messages.end())
          {
            mit->second.reactions = newMessageReactionsNotify->reactions;
          }

          UpdateHistory();
        }
      }
      break;

    case ReceiveTypingNotifyType:
      {
        std::shared_ptr<ReceiveTypingNotify> receiveTypingNotify = std::static_pointer_cast<ReceiveTypingNotify>(
          p_ServiceMessage);
        bool isTyping = receiveTypingNotify->isTyping;
        std::string chatId = receiveTypingNotify->chatId;
        std::string userId = receiveTypingNotify->userId;
        LOG_TRACE("received user %s in chat %s is %s", userId.c_str(), chatId.c_str(), (isTyping ? "typing" : "idle"));
        if (isTyping)
        {
          m_UsersTyping[profileId][chatId].insert(userId);
        }
        else
        {
          m_UsersTyping[profileId][chatId].erase(userId);
        }
        UpdateStatus();
      }
      break;

    case ReceiveStatusNotifyType:
      {
        std::shared_ptr<ReceiveStatusNotify> receiveStatusNotify = std::static_pointer_cast<ReceiveStatusNotify>(
          p_ServiceMessage);
        std::string userId = receiveStatusNotify->userId;
        bool isOnline = receiveStatusNotify->isOnline;
        int64_t timeSeen = receiveStatusNotify->timeSeen;
        LOG_TRACE("received user %s is %s seen %lld", userId.c_str(),
                  (isOnline ? "online" : "away"), timeSeen);
        m_UserOnline[profileId][userId] = isOnline;
        if (timeSeen != -1)
        {
          m_UserTimeSeen[profileId][userId] = timeSeen;
        }
        UpdateStatus();
      }
      break;

    case CreateChatNotifyType:
      {
        std::shared_ptr<CreateChatNotify> createChatNotify =
          std::static_pointer_cast<CreateChatNotify>(p_ServiceMessage);
        if (createChatNotify->success)
        {
          const ChatInfo& chatInfo = createChatNotify->chatInfo;
          LOG_TRACE("chat created %s", chatInfo.id.c_str());
          m_ChatInfos[profileId][chatInfo.id] = chatInfo;
          if (m_ChatSet[profileId].insert(chatInfo.id).second)
          {
            m_ChatVec.push_back(std::make_pair(profileId, chatInfo.id));
          }

          m_CurrentChatIndex = 0;
          m_CurrentChat.first = profileId;
          m_CurrentChat.second = chatInfo.id;
          SortChats();
          OnCurrentChatChanged();
          SetSelectMessageActive(false);
        }
      }
      break;

    case DeleteChatNotifyType:
      {
        std::shared_ptr<DeleteChatNotify> deleteChatNotify = std::static_pointer_cast<DeleteChatNotify>(
          p_ServiceMessage);
        if (deleteChatNotify->success)
        {
          std::string chatId = deleteChatNotify->chatId;
          LOG_TRACE("chat deleted %s", chatId.c_str());

          for (auto it = m_ChatVec.begin(); it != m_ChatVec.end(); /* incremented in loop */)
          {
            if ((it->first == profileId) && (it->second == chatId))
            {
              m_ChatSet[profileId].erase(it->second);
              it = m_ChatVec.erase(it);
            }
            else
            {
              ++it;
            }
          }

          m_ChatInfos[profileId].erase(chatId);

          if ((m_CurrentChat.first == profileId) && (m_CurrentChat.second == chatId))
          {
            m_CurrentChat = m_ChatVec.at(NumUtil::Bound(0, m_CurrentChatIndex, ((int)m_ChatVec.size() - 1)));
            SortChats();
            OnCurrentChatChanged();
            SetSelectMessageActive(false);
          }
          else
          {
            SortChats();
            OnCurrentChatChanged();
          }
        }
      }
      break;

    case UpdateMuteNotifyType:
      {
        std::shared_ptr<UpdateMuteNotify> updateMuteNotify = std::static_pointer_cast<UpdateMuteNotify>(
          p_ServiceMessage);
        bool isMuted = updateMuteNotify->isMuted;
        std::string chatId = updateMuteNotify->chatId;
        LOG_TRACE("mute notify %s is %s", chatId.c_str(), (isMuted ? "muted" : "unmuted"));
        m_ChatInfos[profileId][chatId].isMuted = isMuted;
        UpdateChatInfoLastMessageTime(profileId, chatId);
        SortChats();
        UpdateList();
        UpdateStatus();
      }
      break;

    case UpdatePinNotifyType:
      {
        std::shared_ptr<UpdatePinNotify> updatePinNotify = std::static_pointer_cast<UpdatePinNotify>(
          p_ServiceMessage);
        std::string chatId = updatePinNotify->chatId;
        bool isPinned = updatePinNotify->isPinned;
        int64_t lastMessageTime = updatePinNotify->timePinned;
        LOG_TRACE("pin notify %s is %s", chatId.c_str(), (isPinned ? "pinned" : "unpinned"));
        m_ChatInfos[profileId][chatId].isPinned = isPinned;
        if (isPinned)
        {
          m_ChatInfos[profileId][chatId].lastMessageTime = lastMessageTime;
        }
        UpdateChatInfoLastMessageTime(profileId, chatId);
        SortChats();
        UpdateList();
        UpdateStatus();
      }
      break;

    case ProtocolUiControlNotifyType:
      {
        std::shared_ptr<ProtocolUiControlNotify> protocolUiControlNotify =
          std::static_pointer_cast<ProtocolUiControlNotify>(p_ServiceMessage);
        SetProtocolUiControl(profileId, protocolUiControlNotify->isTakeControl);
      }
      break;

    case RequestAppExitNotifyType:
      {
        std::shared_ptr<RequestAppExitNotify> requestAppExitNotify =
          std::static_pointer_cast<RequestAppExitNotify>(p_ServiceMessage);
        Quit();
      }
      break;

    case AvailableReactionsNotifyType:
      {
        std::shared_ptr<AvailableReactionsNotify> availableReactionsNotify =
          std::static_pointer_cast<AvailableReactionsNotify>(p_ServiceMessage);
        std::string chatId = availableReactionsNotify->chatId;
        // ignore msgId for now
        m_AvailableReactions[profileId][chatId] = availableReactionsNotify->emojis;
        m_AvailableReactionsPending[profileId][chatId] = false;
        LOG_TRACE("available reactions notify %s count %d", chatId.c_str(), availableReactionsNotify->emojis.size());
      }
      break;

    case FindMessageNotifyType:
      {
        SetFindMessageActive(false);
        std::shared_ptr<FindMessageNotify> findMessageNotify =
          std::static_pointer_cast<FindMessageNotify>(p_ServiceMessage);
        bool success = findMessageNotify->success;
        std::string chatId = findMessageNotify->chatId;
        std::string msgId = findMessageNotify->msgId;
        LOG_TRACE("find message notify %s %s", (success ? "found" : "not found"), msgId.c_str());
        if (!msgId.empty())
        {
          const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
          int& messageOffset = m_MessageOffset[profileId][chatId];
          std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

          auto it = std::find(messageVec.begin(), messageVec.end(), msgId);
          if (it != messageVec.end())
          {
            int newMessageOffset = std::distance(messageVec.begin(), it);
            int addOffset = newMessageOffset - messageOffset;
            if (addOffset > 0)
            {
              messageOffsetStack.push(addOffset);
            }

            SetSelectMessageActive(true);
            messageOffset = newMessageOffset;
            RequestMessagesCurrentChat();
            UpdateHistory();
          }
          else
          {
            LOG_WARNING("message %s not available in ui", msgId.c_str());
          }
        }
        else
        {
          m_TriggerTerminalBell = true;
        }
      }
      break;

    default:
      LOG_DEBUG("unknown service message %d", p_ServiceMessage->GetMessageType());
      break;
  }
}

void UiModel::Impl::AddProtocol(std::shared_ptr<Protocol> p_Protocol)
{
  const std::string profileId = p_Protocol->GetProfileId();
  m_Protocols[profileId] = p_Protocol;
}

std::unordered_map<std::string, std::shared_ptr<Protocol>> UiModel::Impl::GetProtocols()
{
  return m_Protocols;
}

bool UiModel::Impl::Process()
{
  if (m_TriggerTerminalBell)
  {
    m_TriggerTerminalBell = false;
    m_View->TerminalBell();
  }

  ProcessTimers();

  SetTyping("", "", false);
  m_View->Draw();
  return m_Running;
}

void UiModel::Impl::ProcessTimers()
{
  static int64_t lastTimeMs = TimeUtil::GetCurrentTimeMSec();
  int64_t nowTimeMs = TimeUtil::GetCurrentTimeMSec();
  int64_t elapsedMs = nowTimeMs - lastTimeMs;

  if (elapsedMs > 200)
  {
    lastTimeMs = nowTimeMs;

    static const int autoSelectChatTimeoutSec = UiConfig::GetNum("auto_select_chat_timeout_sec");
    if (!IsCurrentChatSet() && (autoSelectChatTimeoutSec != 0) && (m_LastSyncMessageTime != 0))
    {
      int64_t elapsedSinceLastSyncMessageSec = (nowTimeMs - m_LastSyncMessageTime) / 1000;
      if (elapsedSinceLastSyncMessageSec >= autoSelectChatTimeoutSec)
      {
        if (SetCurrentChatIndexIfNotSet())
        {
          LOG_TRACE("set current chat on old msg sync timeout");
        }
      }
    }
  }
}

void UiModel::Impl::SortChats()
{
  static const bool mutedPositionByTimestamp = UiConfig::GetBool("muted_position_by_timestamp");
  std::sort(m_ChatVec.begin(), m_ChatVec.end(),
            [&](const std::pair<std::string, std::string>& lhs, const std::pair<std::string, std::string>& rhs) -> bool
  {
    const ChatInfo& lhsChatInfo = m_ChatInfos[lhs.first][lhs.second];
    const ChatInfo& rhsChatInfo = m_ChatInfos[rhs.first][rhs.second];

    // pinned are listed first
    if (lhsChatInfo.isPinned > rhsChatInfo.isPinned) return true;
    if (lhsChatInfo.isPinned < rhsChatInfo.isPinned) return false;

    if (!mutedPositionByTimestamp)
    {
      // non-muted are listed first
      if (lhsChatInfo.isMuted < rhsChatInfo.isMuted) return true;
      if (lhsChatInfo.isMuted > rhsChatInfo.isMuted) return false;
    }

    // greater (=newer) message time are listed first
    if (lhsChatInfo.lastMessageTime > rhsChatInfo.lastMessageTime) return true;
    if (lhsChatInfo.lastMessageTime < rhsChatInfo.lastMessageTime) return false;

    // lower chat ids are listed first
    if (lhsChatInfo.id < rhsChatInfo.id) return true;
    if (lhsChatInfo.id > rhsChatInfo.id) return false;

    return false;
  });

  if (!m_ChatVec.empty())
  {
    if (m_CurrentChatIndex == -1)
    {
      m_CurrentChat = m_ChatVec.at(0);
      OnCurrentChatChanged();
    }
    else
    {
      for (size_t i = 0; i < m_ChatVec.size(); ++i)
      {
        if (m_ChatVec.at(i) == m_CurrentChat)
        {
          m_CurrentChatIndex = i;
        }
      }
    }
  }
}

std::string UiModel::Impl::GetLastMessageId(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  const std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
  if (messageVec.empty()) return std::string();

  std::string lastMessageId;
  for (const auto& messageId : messageVec)
  {
    auto messageIt = messages.find(messageId);
    if (messageIt == messages.end()) continue;

    const ChatMessage& lastChatMessage = messageIt->second;
    if (lastChatMessage.timeSent != std::numeric_limits<int64_t>::max()) // skip sponsored messages
    {
      lastMessageId = messageId;
      break;
    }
  }

  return lastMessageId;
}

void UiModel::Impl::UpdateChatInfoLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  const std::string lastMessageId = GetLastMessageId(p_ProfileId, p_ChatId);
  if (lastMessageId.empty()) return;

  const int64_t lastMessageTimeSent = messages.at(lastMessageId).timeSent;
  std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[p_ProfileId];
  if (profileChatInfos.count(p_ChatId))
  {
    if (!profileChatInfos[p_ChatId].isPinned)
    {
      profileChatInfos[p_ChatId].lastMessageTime = lastMessageTimeSent;
    }
  }

  // If message is received after connection time, ensure current chat is set
  if ((m_ConnectTime.count(p_ProfileId) > 0) && (lastMessageTimeSent > m_ConnectTime[p_ProfileId]))
  {
    if (SetCurrentChatIndexIfNotSet())
    {
      LOG_TRACE("set current chat on new msg %lld", lastMessageTimeSent);
    }
  }
  else
  {
    if (!IsCurrentChatSet() && (m_LastSyncMessageTime != 0))
    {
      m_LastSyncMessageTime = TimeUtil::GetCurrentTimeMSec();
    }

    if (m_CurrentChatIndex < 0)
    {
      LOG_TRACE("dont set current chat on old msg %lld", lastMessageTimeSent);
    }
  }
}

void UiModel::Impl::UpdateChatInfoIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  const std::string lastMessageId = GetLastMessageId(p_ProfileId, p_ChatId);
  if (lastMessageId.empty()) return;

  bool isRead = true;
  const ChatMessage& chatMessage = messages.at(lastMessageId);
  isRead = chatMessage.isOutgoing ? true : chatMessage.isRead;

  bool isUnread = !isRead;
  bool hasMention = chatMessage.hasMention;
  std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[p_ProfileId];
  if (profileChatInfos.count(p_ChatId))
  {
    static const bool mutedNotifyUnread = UiConfig::GetBool("muted_notify_unread");
    if (mutedNotifyUnread || !profileChatInfos[p_ChatId].isMuted || hasMention)
    {
      static const bool notifyEveryUnread = UiConfig::GetBool("notify_every_unread");
      if (isUnread && (!profileChatInfos[p_ChatId].isUnread || notifyEveryUnread))
      {
        const bool receivedAfterConnect = (m_ConnectTime.count(p_ProfileId) > 0) &&
          (chatMessage.timeSent > m_ConnectTime[p_ProfileId]);
        if (receivedAfterConnect)
        {
          static const bool terminalBellActive = UiConfig::GetBool("terminal_bell_active");
          static const bool terminalBellInactive = UiConfig::GetBool("terminal_bell_inactive");
          bool terminalBell = m_TerminalActive ? terminalBellActive : terminalBellInactive;
          if (terminalBell)
          {
            m_TriggerTerminalBell = true;
          }

          bool desktopNotify = false;
          if (m_TerminalActive)
          {
            const bool isCurrentChat = IsCurrentChat(p_ProfileId, p_ChatId);
            if (isCurrentChat)
            {
              static const bool desktopNotifyActiveCurrent = UiConfig::GetBool("desktop_notify_active_current");
              desktopNotify = desktopNotifyActiveCurrent;
            }
            else
            {
              static const bool desktopNotifyActiveNoncurrent = UiConfig::GetBool("desktop_notify_active_noncurrent");
              desktopNotify = desktopNotifyActiveNoncurrent;
            }
          }
          else
          {
            static const bool desktopNotifyInactive = UiConfig::GetBool("desktop_notify_inactive");
            desktopNotify = desktopNotifyInactive;
          }

          if (desktopNotify)
          {
            const std::string name = (chatMessage.senderId == p_ChatId)
              ? GetContactName(p_ProfileId, chatMessage.senderId)
              : GetContactName(p_ProfileId, p_ChatId) + " - " + GetContactName(p_ProfileId, chatMessage.senderId);
            DesktopNotify(name, chatMessage.text);
          }
        }
      }
    }

    profileChatInfos[p_ChatId].isUnread = isUnread;
  }
}

std::string UiModel::Impl::GetContactName(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  const std::string& chatName = contactInfo.name;
  if (contactInfo.isSelf)
  {
    return "You";
  }
  else if (chatName.empty())
  {
    return p_ChatId;
  }

  return chatName;
}

std::string UiModel::Impl::GetContactNameIncludingSelf(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  const std::string& chatName = contactInfo.name;
  if (chatName.empty())
  {
    if (contactInfo.isSelf)
    {
      return "You";
    }
    else
    {
      return p_ChatId;
    }
  }

  return chatName;
}

std::string UiModel::Impl::GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId,
                                              bool p_AllowId)
{
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  const std::string& chatName = contactInfo.name;
  if (contactInfo.isSelf)
  {
    return "Saved Messages";
  }
  else if (p_AllowId && chatName.empty())
  {
    return p_ChatId;
  }

  return chatName;
}

std::string UiModel::Impl::GetContactPhone(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  return contactInfo.phone.empty() ? "" : "+" + contactInfo.phone;
}

int64_t UiModel::Impl::GetLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ChatInfo& chatInfo = m_ChatInfos[p_ProfileId][p_ChatId];
  return chatInfo.lastMessageTime;
}

bool UiModel::Impl::GetChatIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ChatInfo& chatInfo = m_ChatInfos[p_ProfileId][p_ChatId];
  static const bool mutedIndicateUnread = UiConfig::GetBool("muted_indicate_unread");
  const bool indicateUnread = chatInfo.isUnread && (mutedIndicateUnread || !chatInfo.isMuted);
  return indicateUnread; // @todo: handle isUnreadMention
}

std::string UiModel::Impl::GetChatStatus(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  std::string chatStatus;
  const std::set<std::string>& usersTyping = m_UsersTyping[p_ProfileId][p_ChatId];
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  const ChatInfo& chatInfo = m_ChatInfos[p_ProfileId][p_ChatId];

  if (!usersTyping.empty())
  {
    if (usersTyping.size() > 1)
    {
      std::vector<std::string> userIds = ToVector(usersTyping);
      std::vector<std::string> userNames;
      for (auto& userId : userIds)
      {
        userNames.push_back(GetContactName(p_ProfileId, userId));
      }

      std::string userNamesJoined = StrUtil::Join(userNames, ", ");
      chatStatus = userNamesJoined + " are typing";
    }
    else
    {
      std::string userId = *usersTyping.begin();
      if (userId == p_ChatId)
      {
        chatStatus = "typing";
      }
      else
      {
        std::string userName = GetContactName(p_ProfileId, userId);
        chatStatus = userName + " is typing";
      }
    }
  }
  else if (m_UserOnline[p_ProfileId].count(p_ChatId) && !contactInfo.isSelf)
  {
    if (m_UserOnline[p_ProfileId][p_ChatId])
    {
      chatStatus = "online";
    }
    else
    {
      int64_t timeSeen = m_UserTimeSeen[p_ProfileId].count(p_ChatId) ? m_UserTimeSeen[p_ProfileId][p_ChatId] : -1;
      switch (timeSeen)
      {
        case TimeSeenNone:
          chatStatus = "away";
          break;

        case TimeSeenLastMonth:
          chatStatus = "seen last month";
          break;

        case TimeSeenLastWeek:
          chatStatus = "seen last week";
          break;

        default:
          chatStatus = "seen " + TimeUtil::GetTimeString(timeSeen, false /* p_IsExport */);
          break;
      }
    }
  }
  else
  {
    chatStatus = "";
  }

  if (chatInfo.isMuted)
  {
    if (chatStatus.empty())
    {
      chatStatus = "muted";
    }
    else
    {
      chatStatus = chatStatus + ", muted";
    }
  }

  if (chatInfo.isPinned)
  {
    if (chatStatus.empty())
    {
      chatStatus = "pinned";
    }
    else
    {
      chatStatus = chatStatus + ", pinned";
    }
  }

  if (chatStatus.empty())
  {
    return "";
  }
  else
  {
    return " (" + chatStatus + ")";
  }
}

void UiModel::Impl::OnCurrentChatChanged()
{
  LOG_TRACE("current chat %s %s", m_CurrentChat.first.c_str(), m_CurrentChat.second.c_str());
  SetHistoryInteraction(false);
  UpdateList();
  UpdateStatus();
  UpdateHistory();
  UpdateHelp();
  UpdateEntry();
  RequestMessagesCurrentChat();
  RequestMessagesNextChat();
  RequestUserStatusCurrentChat();
  RequestUserStatusNextChat();
  ProtocolSetCurrentChat();
}

void UiModel::Impl::RequestMessagesCurrentChat()
{
  if (m_CurrentChat == s_ChatNone) return;

  const std::string& profileId = m_CurrentChat.first;
  const std::string& chatId = m_CurrentChat.second;
  RequestMessages(profileId, chatId);
}

void UiModel::Impl::RequestMessagesNextChat()
{
  const std::pair<std::string, std::string>& nextChat = GetNextChat();
  if (nextChat == s_ChatNone) return;

  const std::string& profileId = nextChat.first;
  const std::string& chatId = nextChat.second;
  RequestMessages(profileId, chatId);
}

void UiModel::Impl::RequestMessages(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  std::unordered_set<std::string>& msgFromIdsRequested = m_MsgFromIdsRequested[p_ProfileId][p_ChatId];
  const std::string& oldestMessageId = m_OldestMessageId[p_ProfileId][p_ChatId];
  std::string fromId = (msgFromIdsRequested.empty() || oldestMessageId.empty()) ? "" : oldestMessageId;

  int historySize = 0;
  if (!oldestMessageId.empty())
  {
    const std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
    historySize = messageVec.size();
    for (auto msgIt = messageVec.rbegin(); msgIt != messageVec.rend(); ++msgIt)
    {
      if (*msgIt == oldestMessageId)
      {
        break;
      }

      --historySize;
    }
  }

  int messageOffset = m_MessageOffset[p_ProfileId][p_ChatId];
  const int maxHistory = m_HomeFetchAll ? 8 : (((GetHistoryLines() * 2) / 3) + 1);
  const int limit = std::max(0, (messageOffset + 1 + maxHistory - historySize));
  if (limit == 0)
  {
    LOG_TRACE("no message to request %d + %d - %d >= %d",
              messageOffset, maxHistory, historySize, limit);
    return;
  }

  if (msgFromIdsRequested.find(fromId) != msgFromIdsRequested.end())
  {
    LOG_TRACE("get messages from %s already requested", fromId.c_str());
    return;
  }
  else
  {
    msgFromIdsRequested.insert(fromId);
  }

  const int minLimit = 12; // hack: up to 10 tgchat messages may share same timestamp
  std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::make_shared<GetMessagesRequest>();
  getMessagesRequest->chatId = p_ChatId;
  getMessagesRequest->fromMsgId = fromId;
  getMessagesRequest->limit = std::max(limit, minLimit);
  LOG_TRACE("request messages in %s from %s limit %d", p_ChatId.c_str(), fromId.c_str(), getMessagesRequest->limit);
  SendProtocolRequest(p_ProfileId, getMessagesRequest);
}

void UiModel::Impl::RequestUserStatusCurrentChat()
{
  if (m_CurrentChat == s_ChatNone) return;

  RequestUserStatus(m_CurrentChat);
}

void UiModel::Impl::RequestUserStatusNextChat()
{
  const std::pair<std::string, std::string>& nextChat = GetNextChat();
  if (nextChat == s_ChatNone) return;

  RequestUserStatus(nextChat);
}

void UiModel::Impl::RequestUserStatus(const std::pair<std::string, std::string>& p_Chat)
{
  static std::set<std::pair<std::string, std::string>> requestedStatuses;
  if (requestedStatuses.count(p_Chat)) return;

  requestedStatuses.insert(p_Chat);

  const std::string& profileId = p_Chat.first;
  const std::string& chatId = p_Chat.second;

  std::shared_ptr<GetStatusRequest> getStatusRequest = std::make_shared<GetStatusRequest>();
  getStatusRequest->userId = chatId;
  LOG_TRACE("get status %s", chatId.c_str());
  SendProtocolRequest(profileId, getStatusRequest);
}

void UiModel::Impl::ProtocolSetCurrentChat()
{
  static std::pair<std::string, std::string> lastCurrentChat;
  if (lastCurrentChat != m_CurrentChat)
  {
    lastCurrentChat = m_CurrentChat;

    const std::string& profileId = m_CurrentChat.first;
    const std::string& chatId = m_CurrentChat.second;
    std::shared_ptr<SetCurrentChatRequest> setCurrentChatRequest = std::make_shared<SetCurrentChatRequest>();
    setCurrentChatRequest->chatId = chatId;
    LOG_TRACE("notify current chat %s", chatId.c_str());
    SendProtocolRequest(profileId, setCurrentChatRequest);
  }
}

void UiModel::Impl::SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline)
{
  static const bool onlineStatusShare = UiConfig::GetBool("online_status_share");
  if (!onlineStatusShare) return;

  std::shared_ptr<SetStatusRequest> setStatusRequest = std::make_shared<SetStatusRequest>();
  setStatusRequest->isOnline = p_IsOnline;
  LOG_TRACE("set status %s online %d", p_ProfileId.c_str(), (int)p_IsOnline);
  SendProtocolRequest(p_ProfileId, setStatusRequest);
}

int UiModel::Impl::GetHistoryLines()
{
  return m_View->GetHistoryLines();
}

void UiModel::Impl::RequestContacts()
{
  for (auto& protocol : m_Protocols)
  {
    LOG_TRACE("get contacts %s", protocol.first.c_str());
    std::shared_ptr<GetContactsRequest> getContactsRequest = std::make_shared<GetContactsRequest>();
    protocol.second->SendRequest(getContactsRequest);
  }
}

int UiModel::Impl::GetScreenWidth()
{
  return m_View->GetScreenWidth();
}

int UiModel::Impl::GetScreenHeight()
{
  return m_View->GetScreenHeight();
}

void UiModel::Impl::SetRunning(bool p_Running)
{
  m_Running = p_Running;
}

void UiModel::Impl::ReinitView()
{
  m_View->Init();
}

void UiModel::Impl::UpdateList()
{
  m_View->SetListDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::Impl::UpdateStatus()
{
  m_View->SetStatusDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::Impl::UpdateHistory()
{
  m_View->SetHistoryDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::Impl::UpdateHelp()
{
  m_View->SetHelpDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::Impl::UpdateEntry()
{
  m_View->SetEntryDirty(true);
}

std::wstring& UiModel::Impl::GetEntryStr()
{
  return m_EntryStr[m_CurrentChat.first][m_CurrentChat.second];
}

int& UiModel::Impl::GetEntryPos()
{
  return m_EntryPos[m_CurrentChat.first][m_CurrentChat.second];
}

std::vector<std::pair<std::string, std::string>>& UiModel::Impl::GetChatVec()
{
  return m_ChatVec;
}

std::vector<std::pair<std::string, std::string>>& UiModel::Impl::GetChatVecLock()
{
  return GetChatVec();
}

std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> UiModel::Impl::GetContactInfos()
{
  return m_ContactInfos;
}

int64_t UiModel::Impl::GetContactInfosUpdateTime()
{
  return m_ContactInfosUpdateTime;
}

std::pair<std::string, std::string>& UiModel::Impl::GetCurrentChat()
{
  return m_CurrentChat;
}

bool UiModel::Impl::IsCurrentChat(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  return ((p_ProfileId == m_CurrentChat.first) && (p_ChatId == m_CurrentChat.second));
}

int& UiModel::Impl::GetCurrentChatIndex()
{
  return m_CurrentChatIndex;
}

std::unordered_map<std::string, ChatMessage>& UiModel::Impl::GetMessages(const std::string& p_ProfileId,
                                                                         const std::string& p_ChatId)
{
  return m_Messages[p_ProfileId][p_ChatId];
}

std::vector<std::string>& UiModel::Impl::GetMessageVec(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  return m_MessageVec[p_ProfileId][p_ChatId];
}

int& UiModel::Impl::GetMessageOffset(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  return m_MessageOffset[p_ProfileId][p_ChatId];
}

bool UiModel::Impl::GetSelectMessageActive()
{
  return m_SelectMessageActive;
}

void UiModel::Impl::SetSelectMessageActive(bool p_SelectMessageActive)
{
  m_SelectMessageActive = p_SelectMessageActive;
  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::Impl::GetListDialogActive()
{
  return m_ListDialogActive;
}

void UiModel::Impl::SetListDialogActive(bool p_ListDialogActive)
{
  m_ListDialogActive = p_ListDialogActive;
  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::Impl::GetMessageDialogActive()
{
  return m_MessageDialogActive;
}

void UiModel::Impl::SetMessageDialogActive(bool p_MessageDialogActive)
{
  m_MessageDialogActive = p_MessageDialogActive;
  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::Impl::GetEditMessageActive()
{
  return m_EditMessageActive;
}

void UiModel::Impl::SetEditMessageActive(bool p_EditMessageActive)
{
  m_EditMessageActive = p_EditMessageActive;
  if (!m_EditMessageActive)
  {
    m_EditMessageId.clear();
  }

  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::Impl::GetFindMessageActive()
{
  return m_FindMessageActive;
}

void UiModel::Impl::SetFindMessageActive(bool p_FindMessageActive)
{
  m_FindMessageActive = p_FindMessageActive;
}

void UiModel::Impl::SetHelpOffset(int p_HelpOffset)
{
  m_HelpOffset = p_HelpOffset;
  UpdateHelp();
}

int UiModel::Impl::GetHelpOffset()
{
  return m_HelpOffset;
}

bool UiModel::Impl::GetEmojiEnabled()
{
  return m_View->GetEmojiEnabled();
}

bool UiModel::Impl::GetEmojiEnabledLock()
{
  return GetEmojiEnabled();
}

bool UiModel::Impl::IsCurrentChatSet()
{
  return (m_CurrentChatIndex >= 0);
}

bool UiModel::Impl::SetCurrentChatIndexIfNotSet()
{
  if ((m_CurrentChatIndex >= 0) || (m_ChatVec.empty())) return false;

  m_CurrentChatIndex = 0;
  m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);

  UpdateHistory();
  return true;
}

void UiModel::Impl::SetTerminalActive(bool p_TerminalActive)
{
  if (p_TerminalActive != m_TerminalActive)
  {
    m_TerminalActive = p_TerminalActive;
    LOG_TRACE("set terminal active %d", m_TerminalActive);

    static const bool onlineStatusDynamic = UiConfig::GetBool("online_status_dynamic");
    if (onlineStatusDynamic)
    {
      for (auto& protocol : m_Protocols)
      {
        const std::string& profileId = protocol.first;
        SetStatusOnline(profileId, m_TerminalActive);
      }
    }

    if (m_TerminalActive)
    {
      UpdateHistory(); // refresh history as we may need to mark messages as read
    }
  }
}

void UiModel::Impl::DesktopNotify(const std::string& p_Name, const std::string& p_Text)
{
  static const bool desktopNotifyEnabled = UiConfig::GetBool("desktop_notify_enabled");
  if (!desktopNotifyEnabled) return;

  static const std::string cmdTemplate = [this]()
  {
    std::string desktopNotifyCommand = UiConfig::GetStr("desktop_notify_command");
    if (desktopNotifyCommand.empty())
    {
#if defined(__APPLE__)
      const std::string command = "osascript";
#else
      const std::string command = "notify-send";
#endif
      std::string output;
      if (RunCommand("which " + command + " | head -1", &output))
      {
        if (!output.empty())
        {
          if (output.find("/osascript") != std::string::npos)
          {
            desktopNotifyCommand = "osascript -e 'display notification \"%1: %2\" with title \"nchat\"'";
          }
          else if (output.find("/notify-send") != std::string::npos)
          {
            desktopNotifyCommand = "notify-send 'nchat' '%1: %2'";
          }
        }
      }

      if (desktopNotifyCommand.empty())
      {
        LOG_WARNING("command '%s' not found", command.c_str());
      }
    }

    return desktopNotifyCommand;
  }();

  if (cmdTemplate.empty()) return;

  // clean up sender name and text
  std::string name = p_Name;
  std::string text = p_Text;
  name.erase(remove_if(name.begin(), name.end(),
                       [](char c) { return ((c == '\'') || (c == '%') || (c == '"')); }),
             name.end());
  text.erase(remove_if(text.begin(), text.end(),
                       [](char c) { return ((c == '\'') || (c == '%') || (c == '"')); }),
             text.end());

  // insert sender name and text into command
  std::string cmd = cmdTemplate;
  StrUtil::ReplaceString(cmd, "%1", name);
  StrUtil::ReplaceString(cmd, "%2", text);

  // run command
  RunCommand(cmd);
}

void UiModel::Impl::SetHistoryInteraction(bool p_HistoryInteraction)
{
  static const bool markReadOnView = UiConfig::GetBool("mark_read_on_view");
  if (!markReadOnView && !m_HistoryInteraction && p_HistoryInteraction)
  {
    UpdateHistory();
  }

  m_HistoryInteraction = p_HistoryInteraction;
}

bool UiModel::Impl::IsAttachmentDownloaded(const FileInfo& p_FileInfo)
{
  if (p_FileInfo.fileStatus == FileStatusNone)
  {
    LOG_WARNING("message attachment has invalid status");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloading)
  {
    LOG_DEBUG("message attachment is downloading");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloadFailed)
  {
    LOG_WARNING("message attachment download failed");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusNotDownloaded)
  {
    LOG_DEBUG("message attachment is not downloaded");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloaded)
  {
    std::string filePath = p_FileInfo.filePath;
    if (filePath.empty())
    {
      LOG_WARNING("message attachment %s empty path", filePath.c_str());
      return false;
    }
    else if (filePath.at(0) != '/')
    {
      LOG_WARNING("message attachment %s is not an absolute path", filePath.c_str());
      return false;
    }
    else if (!FileUtil::Exists(filePath))
    {
      LOG_WARNING("message attachment %s does not exist", filePath.c_str());
      return false;
    }
    else
    {
      LOG_DEBUG("message attachment %s exists", filePath.c_str());
      return true;
    }
  }

  LOG_WARNING("message attachment unexpected state");
  return false;
}

bool UiModel::Impl::IsAttachmentDownloadable(const FileInfo& p_FileInfo)
{
  const bool hasFileId = !p_FileInfo.fileId.empty();

  if (p_FileInfo.fileStatus == FileStatusNone)
  {
    LOG_WARNING("message attachment has invalid status");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloading)
  {
    LOG_DEBUG("message attachment is already downloading");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloadFailed)
  {
    LOG_WARNING("message attachment download failed");
    return false;
  }
  else if (p_FileInfo.fileStatus == FileStatusNotDownloaded)
  {
    LOG_DEBUG("message attachment is not downloaded %d", hasFileId);
    return hasFileId;
  }
  else if (p_FileInfo.fileStatus == FileStatusDownloaded)
  {
    std::string filePath = p_FileInfo.filePath;
    if (filePath.empty())
    {
      LOG_WARNING("message attachment %s empty path %d", filePath.c_str(), hasFileId);
      return hasFileId;
    }
    else if (filePath.at(0) != '/')
    {
      LOG_WARNING("message attachment %s is not an absolute path %d", filePath.c_str(), hasFileId);
      return hasFileId;
    }
    else if (!FileUtil::Exists(filePath))
    {
      LOG_WARNING("message attachment %s does not exist %d", filePath.c_str(), hasFileId);
      return hasFileId;
    }
    else
    {
      LOG_WARNING("message attachment %s exists", filePath.c_str());
      return false;
    }
  }

  LOG_WARNING("message attachment unexpected state");
  return false;
}

std::string UiModel::Impl::GetSelectedMessageText()
{
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id");
    return "";
  }

  std::string msgId = *it;
  auto mit = messages.find(msgId);
  if (mit == messages.end())
  {
    LOG_WARNING("error finding message");
    return "";
  }

  return mit->second.text;
}

void UiModel::Impl::Cut()
{
  AnyUserKeyInput();

  {
    std::string profileId = m_CurrentChat.first;
    std::string chatId = m_CurrentChat.second;
    int& entryPos = m_EntryPos[profileId][chatId];
    std::wstring& entryStr = m_EntryStr[profileId][chatId];

    std::string text = StrUtil::ToString(entryStr);
    Clipboard::SetText(text);

    entryStr.clear();
    entryPos = 0;

    SetTyping(profileId, chatId, true);
    UpdateEntry();
  }
}

void UiModel::Impl::Copy()
{
  AnyUserKeyInput();

  if (GetSelectMessageActive())
  {
    std::string text = UiModel::Impl::GetSelectedMessageText();
    Clipboard::SetText(text);
  }
  else
  {
    std::string profileId = m_CurrentChat.first;
    std::string chatId = m_CurrentChat.second;
    std::wstring& entryStr = m_EntryStr[profileId][chatId];

    std::string text = StrUtil::ToString(entryStr);
    Clipboard::SetText(text);
  }
}

void UiModel::Impl::Paste()
{
  AnyUserKeyInput();

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];

  std::string text = Clipboard::GetText();
  text = StrUtil::Textize(text);
  if (m_View->GetEmojiEnabled())
  {
    text = StrUtil::Emojize(text, true /*p_Pad*/);
  }

  std::wstring wtext = StrUtil::ToWString(text);
  entryStr.insert(entryPos, wtext);
  entryPos += wtext.size();

  SetTyping(profileId, chatId, true);
  UpdateEntry();
}

void UiModel::Impl::Clear()
{

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];
  entryStr.clear();
  entryPos = 0;

  UpdateEntry();
}

bool UiModel::Impl::PreEditMsg(std::string& p_ProfileId, std::string& p_ChatId, std::string& p_MsgId,
                               std::string& p_MsgDialogText)
{
  p_ProfileId = m_CurrentChat.first;
  if (!HasProtocolFeature(p_ProfileId, FeatureEditMessagesWithinTwoDays) &&
      !HasProtocolFeature(p_ProfileId, FeatureEditMessagesWithinFifteenMins))
  {
    p_MsgDialogText = "Protocol does not support editing.";
    return false;
  }

  p_ChatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
  const int messageOffset = m_MessageOffset[p_ProfileId][p_ChatId];
  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id");
    return false;
  }

  p_MsgId = *it;
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  const ChatMessage& chatMessage = messages.at(p_MsgId);
  if (!chatMessage.isOutgoing)
  {
    p_MsgDialogText = "Received messages cannot be edited.";
    return false;
  }

  const time_t timeNow = time(NULL);
  const time_t timeSent = (time_t)(chatMessage.timeSent / 1000);
  const time_t messageAgeSec = timeNow - timeSent;
  static const time_t twoDaysSec = 48 * 3600;
  static const time_t fifteenMinsSec = 15 * 60;

  if (HasProtocolFeature(p_ProfileId, FeatureEditMessagesWithinTwoDays) &&
      (messageAgeSec >= twoDaysSec))
  {
    p_MsgDialogText = "Messages older than 48 hours cannot be edited.";
    return false;
  }
  else if (HasProtocolFeature(p_ProfileId, FeatureEditMessagesWithinFifteenMins) &&
           (messageAgeSec >= fifteenMinsSec))
  {
    p_MsgDialogText = "Messages older than 15 minutes cannot be edited.";
    return false;
  }

  return true;
}

void UiModel::Impl::StartEditMsg(const std::string& p_ProfileId, const std::string& p_ChatId,
                                 const std::string& p_MsgId)
{
  AnyUserKeyInput();

  m_EditMessageId = p_MsgId;
  SetEditMessageActive(true);

  std::string text = GetSelectedMessageText();
  text = StrUtil::Textize(text);
  if (m_View->GetEmojiEnabled())
  {
    text = StrUtil::Emojize(text, true /*p_Pad*/);
  }

  int& entryPos = m_EntryPos[p_ProfileId][p_ChatId];
  std::wstring& entryStr = m_EntryStr[p_ProfileId][p_ChatId];

  std::wstring wtext = StrUtil::ToWString(text);
  entryStr = wtext;
  entryPos = wtext.size();

  UpdateEntry();
}

void UiModel::Impl::SaveEditMessage()
{
  if (!GetEditMessageActive()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  std::wstring& entryStr = m_EntryStr[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
  const ChatMessage& chatMessage = messages.at(m_EditMessageId);

  if (entryStr.empty()) return;

  std::shared_ptr<EditMessageRequest> editMessageRequest =
    std::make_shared<EditMessageRequest>();
  editMessageRequest->chatId = chatId;
  editMessageRequest->msgId = m_EditMessageId;

  // copy original message (time sent, quote id, etc)
  editMessageRequest->chatMessage = chatMessage;

  // prepare a new FileInfo struct if original message has a file
  if (!chatMessage.fileInfo.empty())
  {
    FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(chatMessage.fileInfo);
    fileInfo.fileType = FileUtil::GetMimeType(fileInfo.filePath);
    editMessageRequest->chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }

  // update text content
  editMessageRequest->chatMessage.text = EntryStrToSendStr(entryStr);
  SendProtocolRequest(profileId, editMessageRequest);

  SetEditMessageActive(false);

  Clear();
}

void UiModel::Impl::OnKeyCancel()
{
  AnyUserKeyInput();
  bool editMessageActive = GetEditMessageActive();
  if (editMessageActive)
  {
    SetEditMessageActive(false);
  }

  Clear();
}

std::string UiModel::Impl::EntryStrToSendStr(const std::wstring& p_EntryStr)
{
  std::string str;
  if (m_View->GetEmojiEnabled())
  {
    std::wstring wstr = p_EntryStr;
    wstr.erase(std::remove(wstr.begin(), wstr.end(), EMOJI_PAD), wstr.end());
    str = StrUtil::ToString(wstr);
  }
  else
  {
    std::wstring wstr = p_EntryStr;
    str = StrUtil::Emojize(StrUtil::ToString(wstr));
  }

  return str;
}

void UiModel::Impl::OnKeySpell()
{
  AnyUserKeyInput();

  static const std::string cmd = [this]()
  {
    std::string spellCheckCommand = UiConfig::GetStr("spell_check_command");
    if (spellCheckCommand.empty())
    {
      std::string output;
      if (RunCommand("which aspell ispell | head -1", &output))
      {
        if (!output.empty())
        {
          if (output.find("/aspell") != std::string::npos)
          {
            spellCheckCommand = "aspell -c";
          }
          else if (output.find("/ispell") != std::string::npos)
          {
            spellCheckCommand = "ispell -o -x";
          }
        }
      }

      if (spellCheckCommand.empty())
      {
        LOG_WARNING("command 'aspell' or 'ispell' not found");
      }
    }

    return spellCheckCommand;
  }();

  if (!cmd.empty())
  {
    CallExternalEdit(cmd);
  }
}

void UiModel::Impl::OnKeyExtEdit()
{
  AnyUserKeyInput();
  static const std::string editorCmd = []()
  {
    std::string messageEditCommand = UiConfig::GetStr("message_edit_command");
    if (messageEditCommand.empty())
    {
      messageEditCommand = std::string(getenv("EDITOR") ? getenv("EDITOR") : "nano");
    }

    return messageEditCommand;
  }();

  CallExternalEdit(editorCmd);
}

void UiModel::Impl::CallExternalEdit(const std::string& p_EditorCmd)
{

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  std::wstring& entryStr = m_EntryStr[profileId][chatId];
  int& entryPos = m_EntryPos[profileId][chatId];

  endwin();
  std::string tempPath = FileUtil::GetTempDir() + "/compose.txt";
  std::string composeStr = StrUtil::ToString(entryStr);
  FileUtil::WriteFile(tempPath, composeStr);
  const std::string cmd = p_EditorCmd + " " + tempPath;
  LOG_DEBUG("launching external editor: %s", cmd.c_str());
  int rv = SysUtil::System(cmd);
  if (rv == 0)
  {
    LOG_DEBUG("external editor exited successfully");
    std::string str = FileUtil::ReadFile(tempPath);

    // trim trailing linebreak
    if (!str.empty() && str.back() == '\n')
    {
      str = str.substr(0, str.length() - 1);
    }

    entryStr = StrUtil::ToWString(str);
    entryPos = (int)entryStr.size();
  }
  else
  {
    LOG_WARNING("external editor exited with %d", rv);
  }

  FileUtil::RmFile(tempPath);
  refresh();
  wint_t key = 0;
  while (UiKeyInput::GetWch(&key) != ERR)
  {
    // Discard any remaining input
  }

  UpdateEntry();
}

const std::pair<std::string, std::string>& UiModel::Impl::GetNextChat()
{
  if (m_ChatVec.empty()) return s_ChatNone;

  const int chatIndex = std::max(m_CurrentChatIndex, 0);
  int nextChatIndex = chatIndex + 1;
  if (nextChatIndex >= (int)m_ChatVec.size())
  {
    nextChatIndex = 0;
  }

  const std::pair<std::string, std::string>& nextChat = m_ChatVec.at(nextChatIndex);
  return nextChat;
}

std::string UiModel::Impl::PreExtCall()
{
  AnyUserKeyInput();
  const std::string phone = GetContactPhone(m_CurrentChat.first, m_CurrentChat.second);
  return phone;
}

void UiModel::Impl::StartExtCall(const std::string& p_Phone)
{
  static const std::string cmdTemplate = []()
  {
    std::string callCommand = UiConfig::GetStr("call_command");
    if (callCommand.empty())
    {
#if defined(__APPLE__)
      callCommand = "open 'tel://%1' &";
#else
      callCommand = "xdg-open >/dev/null 2>&1 'tel://%1' &";
#endif
    }

    return callCommand;
  }();

  std::string cmd = cmdTemplate;
  StrUtil::ReplaceString(cmd, "%1", p_Phone);

  RunProgram(cmd);
}

void UiModel::Impl::SendProtocolRequest(const std::string& p_ProfileId, std::shared_ptr<RequestMessage> p_Request)
{
  if (!m_Protocols.count(p_ProfileId))
  {
    LOG_WARNING("no profile \"%s\"", p_ProfileId.c_str());
    return;
  }

  m_Protocols[p_ProfileId]->SendRequest(p_Request);
}

bool UiModel::Impl::HasProtocolFeature(const std::string& p_ProfileId, ProtocolFeature p_ProtocolFeature)
{
  if (!m_Protocols.count(p_ProfileId))
  {
    LOG_WARNING("no profile \"%s\"", p_ProfileId.c_str());
    return false;
  }

  return m_Protocols[p_ProfileId]->HasFeature(p_ProtocolFeature);
}

std::string UiModel::Impl::GetSelfId(const std::string& p_ProfileId)
{
  if (!m_Protocols.count(p_ProfileId))
  {
    LOG_WARNING("no profile \"%s\"", p_ProfileId.c_str());
    return "";
  }

  return m_Protocols[p_ProfileId]->GetSelfId();
}

bool UiModel::Impl::IsMultipleProfiles()
{
  static bool isMultipleProfiles = (m_Protocols.size() > 1);
  return isMultipleProfiles;
}

std::string UiModel::Impl::GetProfileDisplayName(const std::string& p_ProfileId)
{
  static std::map<std::string, std::string> s_DisplayNames = [&]()
  {
    std::map<std::string, int> protocolCount;
    for (const auto& protocol : m_Protocols)
    {
      protocolCount[StrUtil::GetProtocolName(protocol.first)]++;
    }

    std::map<std::string, std::string> displayNames;
    for (const auto& protocol : m_Protocols)
    {
      std::string displayName = protocol.second->GetProfileDisplayName();
      if (displayName.empty())
      {
        std::string protocolName = StrUtil::GetProtocolName(protocol.first);
        if (protocolCount[protocolName] > 1)
        {
          // use full profile name
          displayName = protocol.first;
        }
        else
        {
          // use only protocol name
          displayName = protocolName;
        }
      }

      displayNames[protocol.first] = displayName;
    }

    return displayNames;
  }();

  return s_DisplayNames[p_ProfileId];
}

void UiModel::Impl::OnKeyQuit()
{
  AnyUserKeyInput();
  Quit();
}

void UiModel::Impl::Quit()
{
  m_Running = false;
}

void UiModel::Impl::EntryConvertEmojiEnabled()
{
  const bool emojiEnabled = GetEmojiEnabled();
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];

  if (!entryStr.empty())
  {
    if (!emojiEnabled)
    {
      std::wstring wstr = entryStr;
      wstr.erase(std::remove(wstr.begin(), wstr.end(), EMOJI_PAD), wstr.end());
      entryStr = StrUtil::ToWString(StrUtil::Textize(StrUtil::ToString(wstr)));
    }
    else
    {
      entryStr = StrUtil::ToWString(StrUtil::Emojize(StrUtil::ToString(entryStr), true));
    }

    entryPos = entryStr.size();
  }
}

void UiModel::Impl::SetProtocolUiControl(const std::string& p_ProfileId, bool& p_IsTakeControl)
{
  if (p_IsTakeControl)
  {
    if (m_ProtocolUiControl.empty())
    {
      m_ProtocolUiControl = p_ProfileId;
      LOG_TRACE("set protocol ui control ok \"%s\"", m_ProtocolUiControl.c_str());
    }
    else if (m_ProtocolUiControl == p_ProfileId)
    {
      LOG_TRACE("set protocol ui control ok duplicate \"%s\"", m_ProtocolUiControl.c_str());
    }
    else
    {
      p_IsTakeControl = false; // indicate failure to caller, as no point waiting under lock
      LOG_TRACE("set protocol ui control failed, owned by \"%s\"", m_ProtocolUiControl.c_str());
    }
  }
  else
  {
    if (m_ProtocolUiControl == p_ProfileId)
    {
      m_ProtocolUiControl.clear();
      LOG_TRACE("set protocol ui control cleared ok");
    }
    else
    {
      LOG_WARNING("set protocol ui control invalid state \"%s\" != \"%s\"",
                  m_ProtocolUiControl.c_str(), p_ProfileId.c_str());
    }
  }
}

bool UiModel::Impl::IsProtocolUiControlActive()
{
  return !m_ProtocolUiControl.empty();
}

void UiModel::Impl::HandleProtocolUiControlStart()
{
  LOG_TRACE("handle protocol ui control start");
  endwin();
}

void UiModel::Impl::HandleProtocolUiControlEnd()
{
  LOG_TRACE("handle protocol ui control end");
  refresh();

  wint_t key = 0;
  while (UiKeyInput::GetWch(&key) != ERR)
  {
    // Discard any remaining input
  }
}

bool UiModel::Impl::PreReact(std::string& p_ProfileId, std::string& p_ChatId, std::string& p_SenderId,
                             std::string& p_MsgId, std::string& p_SelfEmoji, bool& p_HasLimitedReactions)
{
  p_ProfileId = m_CurrentChat.first;
  p_ChatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
  const int messageOffset = m_MessageOffset[p_ProfileId][p_ChatId];
  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end()) return false;

  p_MsgId = *it;
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  auto mit = messages.find(p_MsgId);
  if (mit == messages.end())
  {
    LOG_WARNING("message %s missing", p_MsgId.c_str());
    return false;
  }
  else
  {
    p_SenderId = mit->second.senderId;
    auto sit = mit->second.reactions.senderEmojis.find(s_ReactionsSelfId);
    if (sit != mit->second.reactions.senderEmojis.end())
    {
      p_SelfEmoji = sit->second;
    }
  }

  p_HasLimitedReactions = HasProtocolFeature(p_ProfileId, FeatureLimitedReactions);
  if (p_HasLimitedReactions)
  {
    LOG_TRACE("request available reactions");
    m_AvailableReactionsPending[p_ProfileId][p_ChatId] = true;
    std::shared_ptr<GetAvailableReactionsRequest> getAvailableReactionsRequest =
      std::make_shared<GetAvailableReactionsRequest>();
    getAvailableReactionsRequest->chatId = p_ChatId;
    getAvailableReactionsRequest->msgId = p_MsgId;
    SendProtocolRequest(p_ProfileId, getAvailableReactionsRequest);
  }

  return true;
}

void UiModel::Impl::SetReact(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_SenderId,
                             const std::string& p_MsgId, const std::string& p_SelfEmoji, const std::string& p_Emoji)
{
  if (p_Emoji != p_SelfEmoji)
  {
    std::shared_ptr<SendReactionRequest> sendReactionRequest = std::make_shared<SendReactionRequest>();
    sendReactionRequest->chatId = p_ChatId;
    sendReactionRequest->senderId = p_SenderId;
    sendReactionRequest->msgId = p_MsgId;
    sendReactionRequest->emoji = p_Emoji;
    sendReactionRequest->prevEmoji = p_SelfEmoji;
    SendProtocolRequest(p_ProfileId, sendReactionRequest);

    UpdateHistory();
  }

  ReinitView();
}

void UiModel::Impl::GetAvailableEmojis(std::set<std::string>& p_AvailableEmojis, bool& p_Pending)
{

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  p_AvailableEmojis.clear();
  p_Pending = m_AvailableReactionsPending[profileId][chatId];
  auto it = m_AvailableReactions[profileId].find(chatId);
  if (it != m_AvailableReactions[profileId].end())
  {
    p_AvailableEmojis = it->second;
  }

  LOG_DEBUG("get available reactions %d pending %d", p_AvailableEmojis.size(), p_Pending);
}

void UiModel::Impl::OnKeyJumpQuoted()
{
  AnyUserKeyInput();

  if (!GetSelectMessageActive() || GetEditMessageActive()) return;

  std::string quotedId;
  const std::string profileId = m_CurrentChat.first;
  const std::string chatId = m_CurrentChat.second;
  std::string& oldestMessageId = m_OldestMessageId[profileId][chatId];

  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  int& messageOffset = m_MessageOffset[profileId][chatId];
  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end()) return;

  std::string msgId = *it;
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
  auto mit = messages.find(msgId);
  if (mit == messages.end())
  {
    LOG_WARNING("message %s not fetched by ui", msgId.c_str());
    return;
  }
  else
  {
    quotedId = mit->second.quotedId;
  }

  if (quotedId.empty())
  {
    LOG_WARNING("message %s has no quoted message", msgId.c_str());
    return;
  }

  std::shared_ptr<FindMessageRequest> findMessageRequest = std::make_shared<FindMessageRequest>();
  findMessageRequest->chatId = chatId;
  findMessageRequest->lastMsgId = oldestMessageId;
  findMessageRequest->findMsgId = quotedId;
  SendProtocolRequest(profileId, findMessageRequest);
}

void UiModel::Impl::Find(const std::string& p_FindText)
{
  if (!p_FindText.empty())
  {
    PerformFindNext(p_FindText);
  }

  ReinitView();
}

void UiModel::Impl::PerformFindNext(const std::string& p_FindText)
{
  const std::string profileId = m_CurrentChat.first;
  const std::string chatId = m_CurrentChat.second;
  std::string& oldestMessageId = m_OldestMessageId[profileId][chatId];

  std::string fromMsgId;
  if (GetSelectMessageActive())
  {
    const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
    const int messageOffset = m_MessageOffset[profileId][chatId];
    auto it = std::next(messageVec.begin(), messageOffset);
    if (it != messageVec.end())
    {
      fromMsgId = *it;
    }
  }

  std::shared_ptr<FindMessageRequest> findMessageRequest = std::make_shared<FindMessageRequest>();
  findMessageRequest->chatId = chatId;
  findMessageRequest->fromMsgId = fromMsgId;
  findMessageRequest->lastMsgId = oldestMessageId;
  findMessageRequest->findText = p_FindText;
  SendProtocolRequest(profileId, findMessageRequest);
}

void UiModel::Impl::ForwardMessage(const std::pair<std::string, std::string>& p_Chat)
{
  PerformForwardMessage(p_Chat);
  ReinitView();
}

void UiModel::Impl::PerformForwardMessage(const std::pair<std::string, std::string>& p_Chat)
{
  ChatMessage message;
  std::string profileId;
  std::string chatId;

  {
    profileId = m_CurrentChat.first;
    chatId = m_CurrentChat.second;
    const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
    const int messageOffset = m_MessageOffset[profileId][chatId];
    const std::unordered_map<std::string, ChatMessage>& messages =
      m_Messages[profileId][chatId];

    auto it = std::next(messageVec.begin(), messageOffset);
    if (it == messageVec.end())
    {
      LOG_WARNING("error finding selected message id");
      return;
    }

    auto msg = messages.find(*it);
    if (msg == messages.end())
    {
      LOG_WARNING("error finding selected message content");
      return;
    }

    message = msg->second;
  }

  // switch current chat to recipient
  bool found = false;
  for (size_t i = 0; i < m_ChatVec.size(); ++i)
  {
    if (m_ChatVec.at(i) == p_Chat)
    {
      m_CurrentChatIndex = i;
      m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
      found = true;
      break;
    }
  }

  if (found)
  {
    // fetch recipient messages before send, to avoid fetching outgoing message with temporary id
    OnCurrentChatChanged();
  }
  else
  {
    LOG_WARNING("recipient %s %s not found", p_Chat.first.c_str(), p_Chat.second.c_str());
    return;
  }

  std::shared_ptr<SendMessageRequest> sendMessageRequest =
    std::make_shared<SendMessageRequest>();

  // prepare a new FileInfo struct if original message has a file
  if (!message.fileInfo.empty())
  {
    FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(message.fileInfo);
    fileInfo.fileType = FileUtil::GetMimeType(fileInfo.filePath);
    sendMessageRequest->chatMessage.fileInfo = ProtocolUtil::FileInfoToHex(fileInfo);
  }

  // copy text and use selected chat
  sendMessageRequest->chatMessage.text = message.text;
  sendMessageRequest->chatId = p_Chat.second;

  SendProtocolRequest(p_Chat.first, sendMessageRequest);

  // reset message offset and selection mode
  m_MessageOffset[profileId][chatId] = 0;
  SetSelectMessageActive(false);
}

bool UiModel::Impl::IsChatForceHidden(const std::string& p_ChatId)
{
  static const bool statusBroadcastHidden = (UiConfig::GetNum("status_broadcast") == 0);
  return statusBroadcastHidden && (p_ChatId == "status@broadcast");
}

bool UiModel::Impl::IsChatForceMuted(const std::string& p_ChatId)
{
  static const bool statusBroadcastMuted = (UiConfig::GetNum("status_broadcast") == 1);
  return statusBroadcastMuted && (p_ChatId == "status@broadcast");
}

void UiModel::Impl::GotoChat(const std::pair<std::string, std::string>& p_Chat)
{
  // switch current chat
  bool found = false;
  for (size_t i = 0; i < m_ChatVec.size(); ++i)
  {
    if (m_ChatVec.at(i) == p_Chat)
    {
      m_CurrentChatIndex = i;
      m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
      found = true;
      break;
    }
  }

  if (found)
  {
    OnCurrentChatChanged();
  }
  else
  {
    LOG_WARNING("chat %s %s not found", p_Chat.first.c_str(), p_Chat.second.c_str());
  }

  ReinitView();
}

void UiModel::Impl::AddQuoteFromSelectedMessage(ChatMessage& p_ChatMessage)
{
  const std::string profileId = m_CurrentChat.first;
  const std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding selected message id");
    return;
  }

  auto msg = messages.find(*it);
  if (msg == messages.end())
  {
    LOG_WARNING("error finding selected message content");
    return;
  }

  p_ChatMessage.quotedId = msg->second.id;
  p_ChatMessage.quotedText = msg->second.text;
  p_ChatMessage.quotedSender = msg->second.senderId;
}

void UiModel::Impl::Draw()
{
  m_View->Draw();
}

bool UiModel::Impl::AutoCompose()
{
  AnyUserKeyInput();

  const std::string& profileId = m_CurrentChat.first;
  const std::string& chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

  const int messageOffset = GetSelectMessageActive() ? m_MessageOffset[profileId][chatId] : 0;
  const int editOffset = GetEditMessageActive() ? 1 : 0;
  const int offset = messageOffset + editOffset;

  auto it = std::next(messageVec.begin(), offset);
  if (it == messageVec.end())
  {
    LOG_WARNING("end of message history");
    return false;
  }

  std::string historyStr;
  static const int autoComposeHistoryCount = UiConfig::GetNum("auto_compose_history_count");
  for (int i = 0; (i < autoComposeHistoryCount) && (it != messageVec.end()); ++i, ++it)
  {
    auto msgIt = messages.find(*it);
    if (msgIt == messages.end())
    {
      LOG_WARNING("error finding message");
      continue;
    }

    const ChatMessage& msg = msgIt->second;
    std::string text = msg.text;
    StrUtil::ReplaceString(text, "\n", " ");
    if (text.empty())
    {
      LOG_TRACE("skip empty msg text");
      continue;
    }

    std::string name = GetContactNameIncludingSelf(profileId, msg.senderId);
    StrUtil::ReplaceString(name, ":", "");
    StrUtil::ReplaceString(name, "\n", " ");
    if (name.empty())
    {
      LOG_TRACE("skip empty sender name");
      continue;
    }

    std::string line = name + ": " + text + "\n";
    historyStr = line + historyStr;
  }

  if (historyStr.empty())
  {
    LOG_WARNING("no message history extracted");
    return false;
  }

  std::string selfName = GetContactNameIncludingSelf(profileId, GetSelfId(profileId));
  StrUtil::ReplaceString(selfName, ":", "");
  StrUtil::ReplaceString(selfName, "\n", " ");
  historyStr += selfName + ":\n";

  std::string tempPath = FileUtil::GetTempDir() + "/history.txt";
  FileUtil::WriteFile(tempPath, historyStr);

  static const std::string cmdTemplate = []()
  {
    std::string autoComposeCommand = UiConfig::GetStr("auto_compose_command");
    if (autoComposeCommand.empty())
    {
      autoComposeCommand = FileUtil::DirName(FileUtil::GetSelfPath()) +
        "/../" CMAKE_INSTALL_LIBEXECDIR "/nchat/compose -c '%1'";
    }

    return autoComposeCommand;
  }();

  std::string str;
  std::string cmd = cmdTemplate;
  StrUtil::ReplaceString(cmd, "%1", tempPath);
  const bool rv = RunCommand(cmd, &str);
  if (rv)
  {
    int& entryPos = m_EntryPos[profileId][chatId];
    std::wstring& entryStr = m_EntryStr[profileId][chatId];

    if (!m_View->GetEmojiEnabled())
    {
      str = StrUtil::Textize(str);
    }

    entryStr = StrUtil::ToWString(str);
    entryPos = (int)entryStr.size();
  }

  FileUtil::RmFile(tempPath);
  UpdateEntry();
  return rv;
}

// ---------------------------------------------------------------------
// UiModel
// ---------------------------------------------------------------------

UiModel::UiModel()
  : m_Impl(this)
{
}

UiModel::~UiModel()
{
}

void UiModel::Init()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().Init();
}

void UiModel::Cleanup()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().Cleanup();
}

void UiModel::ReinitView()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().ReinitView();
}

void UiModel::AddProtocol(std::shared_ptr<Protocol> p_Protocol)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().AddProtocol(p_Protocol);
}

std::unordered_map<std::string, std::shared_ptr<Protocol>> UiModel::GetProtocols()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetProtocols();
}

bool UiModel::IsMultipleProfiles()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().IsMultipleProfiles();
}

void UiModel::Draw()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().Draw();
}

void UiModel::KeyHandler(wint_t p_Key)
{
  static wint_t keyPrevPage = UiKeyConfig::GetKey("prev_page");
  static wint_t keyNextPage = UiKeyConfig::GetKey("next_page");
  static wint_t keyEnd = UiKeyConfig::GetKey("end");
  static wint_t keyHome = UiKeyConfig::GetKey("home");

  static wint_t keySendMsg = UiKeyConfig::GetKey("send_msg");
  static wint_t keyNextChat = UiKeyConfig::GetKey("next_chat");
  static wint_t keyPrevChat = UiKeyConfig::GetKey("prev_chat");
  static wint_t keyUnreadChat = UiKeyConfig::GetKey("unread_chat");

  static wint_t keyQuit = UiKeyConfig::GetKey("quit");
  static wint_t keySelectEmoji = UiKeyConfig::GetKey("select_emoji");
  static wint_t keySelectContact = UiKeyConfig::GetKey("select_contact");
  static wint_t keyTransfer = UiKeyConfig::GetKey("transfer");
  static wint_t keyDeleteMsg = UiKeyConfig::GetKey("delete_msg");
  static wint_t keyDeleteChat = UiKeyConfig::GetKey("delete_chat");
  static wint_t keyEditMsg = UiKeyConfig::GetKey("edit_msg");
  static wint_t keyCancel = UiKeyConfig::GetKey("cancel");

  static wint_t keyOpen = UiKeyConfig::GetKey("open");
  static wint_t keyOpenLink = UiKeyConfig::GetKey("open_link");
  static wint_t keyOpenMsg = UiKeyConfig::GetKey("open_msg");
  static wint_t keySave = UiKeyConfig::GetKey("save");

  static wint_t keyCut = UiKeyConfig::GetKey("cut");
  static wint_t keyCopy = UiKeyConfig::GetKey("copy");
  static wint_t keyPaste = UiKeyConfig::GetKey("paste");

  static wint_t keyReact = UiKeyConfig::GetKey("react");
  static wint_t keySpell = UiKeyConfig::GetKey("spell");

  static wint_t keyJumpQuoted = UiKeyConfig::GetKey("jump_quoted");
  static wint_t keyFind = UiKeyConfig::GetKey("find");
  static wint_t keyFindNext = UiKeyConfig::GetKey("find_next");

  static wint_t keyForwardMsg = UiKeyConfig::GetKey("forward_msg");
  static wint_t keyGotoChat = UiKeyConfig::GetKey("goto_chat");

  static wint_t keyToggleList = UiKeyConfig::GetKey("toggle_list");
  static wint_t keyToggleTop = UiKeyConfig::GetKey("toggle_top");
  static wint_t keyToggleHelp = UiKeyConfig::GetKey("toggle_help");
  static wint_t keyToggleEmoji = UiKeyConfig::GetKey("toggle_emoji");

  static wint_t keyDecreaseListWidth = UiKeyConfig::GetKey("decrease_list_width");
  static wint_t keyIncreaseListWidth = UiKeyConfig::GetKey("increase_list_width");

  static wint_t keyExtEdit = UiKeyConfig::GetKey("ext_edit");
  static wint_t keyExtCall = UiKeyConfig::GetKey("ext_call");

  static wint_t keyOtherCommandsHelp = UiKeyConfig::GetKey("other_commands_help");
  static wint_t keyTerminalFocusIn = UiKeyConfig::GetKey("terminal_focus_in");
  static wint_t keyTerminalFocusOut = UiKeyConfig::GetKey("terminal_focus_out");
  static wint_t keyTerminalResize = UiKeyConfig::GetKey("terminal_resize");

  static wint_t keyAutoCompose = UiKeyConfig::GetKey("auto_compose");

  if (p_Key == keyTerminalResize)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().TerminalResize();
  }
  else if (p_Key == keyTerminalFocusIn)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().SetTerminalActive(true);
  }
  else if (p_Key == keyTerminalFocusOut)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().SetTerminalActive(false);
  }
  else if (p_Key == keyToggleHelp)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyToggleHelp();
  }
  else if (p_Key == keyToggleList)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyToggleList();
  }
  else if (p_Key == keyToggleTop)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyToggleTop();
  }
  else if (p_Key == keyToggleEmoji)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyToggleEmoji();
  }
  else if (p_Key == keyNextChat)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyNextChat();
  }
  else if (p_Key == keyPrevChat)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyPrevChat();
  }
  else if (p_Key == keyUnreadChat)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyUnreadChat();
  }
  else if (p_Key == keyPrevPage)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyPrevPage();
  }
  else if (p_Key == keyNextPage)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyNextPage();
  }
  else if (p_Key == keyHome)
  {
    bool performedFetchAllCache = false;
    {
      std::unique_lock<owned_mutex> lock(m_ModelMutex);
      performedFetchAllCache = GetImpl().OnKeyHomeFetchCache();
    }

    if (performedFetchAllCache)
    {
      TimeUtil::Sleep(0.2); // @todo: wait for request completion, with timeout
    }

    {
      std::unique_lock<owned_mutex> lock(m_ModelMutex);
      GetImpl().OnKeyHomeFetchNext();
    }
  }
  else if (p_Key == keyEnd)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyEnd();
  }
  else if (p_Key == keyQuit)
  {
    OnKeyQuit();
  }
  else if (p_Key == keySendMsg)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeySendMsg();
  }
  else if (p_Key == keyExtEdit)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyExtEdit();
  }
  else if (p_Key == keyDeleteMsg)
  {
    OnKeyDeleteMsg();
  }
  else if (p_Key == keyDeleteChat)
  {
    OnKeyDeleteChat();
  }
  else if (p_Key == keyOpen)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyOpenAttachment();
  }
  else if (p_Key == keyOpenLink)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyOpenLink();
  }
  else if (p_Key == keySave)
  {
    OnKeySaveAttachment();
  }
  else if (p_Key == keyTransfer)
  {
    OnKeyTransfer();
  }
  else if (p_Key == keySelectEmoji)
  {
    OnKeySelectEmoji();
  }
  else if (p_Key == keySelectContact)
  {
    OnKeySelectContact();
  }
  else if (p_Key == keyOtherCommandsHelp)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyOtherCommandsHelp();
  }
  else if (p_Key == keyCut)
  {
    OnKeyCut();
  }
  else if (p_Key == keyCopy)
  {
    OnKeyCopy();
  }
  else if (p_Key == keyPaste)
  {
    OnKeyPaste();
  }
  else if (p_Key == keyReact)
  {
    OnKeyReact();
  }
  else if (p_Key == keySpell)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeySpell();
  }
  else if (p_Key == keyEditMsg)
  {
    OnKeyEditMsg();
  }
  else if (p_Key == keyCancel)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyCancel();
  }
  else if (p_Key == keyDecreaseListWidth)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyDecreaseListWidth();
  }
  else if (p_Key == keyIncreaseListWidth)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyIncreaseListWidth();
  }
  else if (p_Key == keyOpenMsg)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyOpenMsg();
  }
  else if (p_Key == keyExtCall)
  {
    OnKeyExtCall();
  }
  else if (p_Key == keyJumpQuoted)
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OnKeyJumpQuoted();
  }
  else if (p_Key == keyFind)
  {
    OnKeyFind();
  }
  else if (p_Key == keyFindNext)
  {
    OnKeyFindNext();
  }
  else if (p_Key == keyForwardMsg)
  {
    OnKeyForwardMsg();
  }
  else if (p_Key == keyGotoChat)
  {
    OnKeyGotoChat();
  }
  else if (p_Key == keyAutoCompose)
  {
    OnKeyAutoCompose();
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().EntryKeyHandler(p_Key);
  }
}

void UiModel::MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().MessageHandler(p_ServiceMessage);
}

bool UiModel::Process()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  if (GetImpl().IsProtocolUiControlActive())
  {
    GetImpl().HandleProtocolUiControlStart();

    while (GetImpl().IsProtocolUiControlActive())
    {
      lock.unlock();
      TimeUtil::Sleep(0.050); // match GetKey timeout
      lock.lock();
    }

    GetImpl().HandleProtocolUiControlEnd();
  }

  return GetImpl().Process();
}

void UiModel::RequestContacts()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().RequestContacts();
}

int UiModel::GetScreenWidth()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetScreenWidth();
}

int UiModel::GetScreenHeight()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetScreenHeight();
}

void UiModel::GetAvailableEmojis(std::set<std::string>& p_AvailableEmojis, bool& p_Pending)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().GetAvailableEmojis(p_AvailableEmojis, p_Pending);
}

std::vector<std::pair<std::string, std::string>> UiModel::GetChatVec()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetChatVec();
}

std::string UiModel::GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_AllowId)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetContactListName(p_ProfileId, p_ChatId, p_AllowId);
}

std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> UiModel::GetContactInfos()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetContactInfos();
}

std::string UiModel::GetProfileDisplayName(const std::string& p_ProfileId)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetProfileDisplayName(p_ProfileId);
}

int64_t UiModel::GetContactInfosUpdateTime()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetContactInfosUpdateTime();
}

bool UiModel::GetEmojiEnabled()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetEmojiEnabled();
}

int UiModel::GetHelpOffset()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  return GetImpl().GetHelpOffset();
}

void UiModel::SetHelpOffset(int p_HelpOffset)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().SetHelpOffset(p_HelpOffset);
}

void UiModel::SetMessageDialogActive(bool p_MessageDialogActive)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().SetMessageDialogActive(p_MessageDialogActive);
}

void UiModel::SetListDialogActive(bool p_ListDialogActive)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().SetListDialogActive(p_ListDialogActive);
}

void UiModel::SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().SetStatusOnline(p_ProfileId, p_IsOnline);
}

void UiModel::SetTerminalActive(bool p_TerminalActive)
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().SetTerminalActive(p_TerminalActive);
}

bool UiModel::GetChatIsUnreadLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetChatIsUnread(p_ProfileId, p_ChatId);
}

std::string UiModel::GetChatStatusLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetChatStatus(p_ProfileId, p_ChatId);
}

std::vector<std::pair<std::string, std::string>>& UiModel::GetChatVecLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetChatVec();
}

std::string UiModel::GetContactListNameLocked(const std::string& p_ProfileId, const std::string& p_ChatId,
                                              bool p_AllowId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetContactListName(p_ProfileId, p_ChatId, p_AllowId);
}

std::string UiModel::GetContactNameLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetContactName(p_ProfileId, p_ChatId);
}

std::string UiModel::GetContactPhoneLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetContactPhone(p_ProfileId, p_ChatId);
}

int UiModel::GetCurrentChatIndexLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetCurrentChatIndex();
}

std::pair<std::string, std::string>& UiModel::GetCurrentChatLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetCurrentChat();
}

bool UiModel::GetEditMessageActiveLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetEditMessageActive();
}

bool UiModel::GetEmojiEnabledLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetEmojiEnabled();
}

int UiModel::GetEntryPosLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetEntryPos();
}

std::wstring UiModel::GetEntryStrLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetEntryStr();
}

int UiModel::GetHelpOffsetLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetHelpOffset();
}

int64_t UiModel::GetLastMessageTimeLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetLastMessageTime(p_ProfileId, p_ChatId);
}

bool UiModel::GetListDialogActiveLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetListDialogActive();
}

bool UiModel::GetMessageDialogActiveLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetMessageDialogActive();
}

int UiModel::GetMessageOffsetLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetMessageOffset(p_ProfileId, p_ChatId);
}

std::unordered_map<std::string, ChatMessage>& UiModel::GetMessagesLocked(const std::string& p_ProfileId,
                                                                         const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetMessages(p_ProfileId, p_ChatId);
}

std::vector<std::string>& UiModel::GetMessageVecLocked(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetMessageVec(p_ProfileId, p_ChatId);
}

std::string UiModel::GetProfileDisplayNameLocked(const std::string& p_ProfileId)
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetProfileDisplayName(p_ProfileId);
}

bool UiModel::GetSelectMessageActiveLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().GetSelectMessageActive();
}

void UiModel::DownloadAttachmentLocked(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const std::string& p_MsgId,
                                       const std::string& p_FileId, DownloadFileAction p_DownloadFileAction)
{
  nc_assert(m_ModelMutex.owns_lock());
  GetImpl().DownloadAttachment(p_ProfileId, p_ChatId, p_MsgId, p_FileId, p_DownloadFileAction);
}

void UiModel::FetchCachedMessageLocked(const std::string& p_ProfileId, const std::string& p_ChatId,
                                       const std::string& p_MsgId)
{
  nc_assert(m_ModelMutex.owns_lock());
  GetImpl().FetchCachedMessage(p_ProfileId, p_ChatId, p_MsgId);
}

bool UiModel::IsMultipleProfilesLocked()
{
  nc_assert(m_ModelMutex.owns_lock());
  return GetImpl().IsMultipleProfiles();
}

void UiModel::MarkReadLocked(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId,
                             bool p_WasUnread)
{
  nc_assert(m_ModelMutex.owns_lock());
  GetImpl().MarkRead(p_ProfileId, p_ChatId, p_MsgId, p_WasUnread);
}

void UiModel::OnStatusUpdateLocked(uint32_t p_Status)
{
  nc_assert(m_ModelMutex.owns_lock());
  GetImpl().OnStatusUpdate(p_Status);
}

void UiModel::OnKeyGotoChat()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Go to Chat", 0.75, 0.65);
  UiChatListDialog dialog(params);
  if (dialog.Run())
  {
    UiChatListItem selectedChatListItem = dialog.GetSelectedChatItem();
    std::pair<std::string, std::string> newCurrentChat =
      std::make_pair(selectedChatListItem.profileId, selectedChatListItem.chatId);

    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().GotoChat(newCurrentChat);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

std::vector<std::string> UiModel::SelectFile()
{
  std::vector<std::string> filePaths;
  static const std::string filePickerCommand = UiConfig::GetStr("file_picker_command");
  if (!filePickerCommand.empty())
  {
    endwin();
    std::string outPath = FileUtil::GetTempDir() + "/filepicker.txt";
    std::string cmd = "2>&1 " + filePickerCommand;
    StrUtil::ReplaceString(cmd, "%1", outPath);

    // run command
    LOG_TRACE("cmd \"%s\" start", cmd.c_str());
    int rv = SysUtil::System(cmd);
    if (rv == 0)
    {
      std::string filesStr = FileUtil::ReadFile(outPath);
      if (!filesStr.empty())
      {
        filePaths = StrUtil::Split(filesStr, '\n');
        std::set<std::string> filePathsSet = ToSet(filePaths); // hack to handle nnn's duplicate results
        filePathsSet.erase(""); // remove empty element
        filePaths = ToVector(filePathsSet);
      }
    }
    else
    {
      LOG_WARNING("cmd \"%s\" failed (%d)", cmd.c_str(), rv);
    }

    FileUtil::RmFile(outPath);

    refresh();
    wint_t key = 0;
    while (UiKeyInput::GetWch(&key) != ERR)
    {
      // Discard any remaining input
    }
  }
  else
  {
    static std::string currentDir;
    static const bool filePickerPersistDir = UiConfig::GetBool("file_picker_persist_dir");
    if (!filePickerPersistDir || currentDir.empty())
    {
      currentDir = FileUtil::GetCurrentWorkingDir();
    }

    UiDialogParams params(this, "Send File", 0.75, 0.65);
    UiFileListDialog dialog(params, currentDir);
    if (dialog.Run())
    {
      std::string filePath = dialog.GetSelectedPath();
      filePaths = std::vector<std::string>({ filePath });
      currentDir = dialog.GetCurrentDir();
    }
  }

  return filePaths;
}

void UiModel::OnKeyTransfer()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  std::vector<std::string> filePaths = SelectFile();

  // Perform transfer
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().TransferFile(filePaths);
}

void UiModel::OnKeySelectEmoji()
{
  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Insert Emoji", 0.75, 0.65);
  UiEmojiListDialog dialog(params);
  if (dialog.Run())
  {
    const std::wstring emoji = dialog.GetSelectedEmoji(GetEmojiEnabled());
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().InsertEmoji(emoji);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

void UiModel::OnKeySelectContact()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Open Chat", 0.75, 0.65);
  UiContactListDialog dialog(params);
  if (dialog.Run())
  {
    UiContactListItem selectedContact = dialog.GetSelectedContactItem();
    std::string profileId = selectedContact.profileId;
    std::string userId = selectedContact.contactId;
    std::pair<std::string, std::string> newChat = std::make_pair(profileId, userId);
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().OpenCreateChat(newChat);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

void UiModel::OnKeyReact()
{
  // Pre-req
  std::string profileId;
  std::string chatId;
  std::string senderId;
  std::string msgId;
  std::string selfEmoji;
  bool hasLimitedReactions = false;
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (!GetImpl().GetSelectMessageActive() || GetImpl().GetEditMessageActive()) return;

    bool messageExists = GetImpl().PreReact(profileId, chatId, senderId, msgId, selfEmoji, hasLimitedReactions);
    if (!messageExists) return;
  }

  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Set Reaction", 0.75, 0.65);
  UiEmojiListDialog dialog(params, selfEmoji, true /*p_HasNone*/, hasLimitedReactions);
  if (dialog.Run())
  {
    std::string emoji = StrUtil::ToString(dialog.GetSelectedEmoji(true /*p_EmojiEnabled*/));
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().SetReact(profileId, chatId, senderId, msgId, selfEmoji, emoji);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

void UiModel::OnKeyFind()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetEditMessageActive()) return;

    GetImpl().SetFindMessageActive(true);
  }

  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Find", 0.5, 5);
  UiTextInputDialog textInputDialog(params, "Text: ", m_FindText);
  if (textInputDialog.Run())
  {
    m_FindText = textInputDialog.GetInput();
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().Find(m_FindText);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

void UiModel::OnKeyFindNext()
{
  if (m_FindText.empty())
  {
    OnKeyFind();
    return;
  }

  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetEditMessageActive() || GetImpl().GetFindMessageActive()) return;

    GetImpl().SetFindMessageActive(true);
  }

  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().Find(m_FindText);
  }
}

void UiModel::OnKeyForwardMsg()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (!GetImpl().GetSelectMessageActive() || GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  UiDialogParams params(this, "Forward to Chat", 0.75, 0.65);
  UiChatListDialog dialog(params);

  if (dialog.Run())
  {
    UiChatListItem selectedChatListItem = dialog.GetSelectedChatItem();
    std::pair<std::string, std::string> selectedChat =
      std::make_pair(selectedChatListItem.profileId, selectedChatListItem.chatId);

    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ForwardMessage(selectedChat);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().ReinitView();
  }
}

bool UiModel::MessageDialog(const std::string& p_Title, const std::string& p_Text, float p_WReq, float p_HReq)
{
  UiDialogParams params(this, p_Title, p_WReq, p_HReq);
  UiMessageDialog messageDialog(params, p_Text);
  bool rv = messageDialog.Run();
  ReinitView();
  return rv;
}

void UiModel::OnKeyDeleteMsg()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (!GetImpl().GetSelectMessageActive() || GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  static const bool confirmDeletion = UiConfig::GetBool("confirm_deletion");
  if (confirmDeletion)
  {
    if (!MessageDialog("Confirmation", "Confirm message deletion?", 0.5, 5))
    {
      return;
    }
  }

  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().OnKeyDeleteMsg();
}

void UiModel::OnKeyDeleteChat()
{
  // Pre-req
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (GetImpl().GetSelectMessageActive() || GetImpl().GetEditMessageActive()) return;
  }

  // Open modal dialog without model mutex held
  static const bool confirmDeletion = UiConfig::GetBool("confirm_deletion");
  if (confirmDeletion)
  {
    if (!MessageDialog("Confirmation", "Confirm chat deletion?", 0.5, 5))
    {
      return;
    }
  }

  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().OnKeyDeleteChat();
}

void UiModel::OnKeySaveAttachment()
{
  std::string dstFilePath;
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    dstFilePath = GetImpl().OnKeySaveAttachment();
  }

  // Open modal dialog without model mutex held
  if (!dstFilePath.empty())
  {
    MessageDialog("Notification", "File saved in\n" + dstFilePath, 0.8, 6);
  }
}

void UiModel::OnKeyEditMsg()
{
  // Pre-req
  bool isAllowed = false;
  std::string profileId;
  std::string chatId;
  std::string msgId;
  std::string msgDialogText;
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    if (!GetImpl().GetSelectMessageActive() || GetImpl().GetEditMessageActive()) return;

    isAllowed = GetImpl().PreEditMsg(profileId, chatId, msgId, msgDialogText);
  }

  // Open modal dialog without model mutex held
  if (!isAllowed)
  {
    if (!msgDialogText.empty())
    {
      float wReq = (msgDialogText.size() < 40) ? 0.7 : 0.8;
      MessageDialog("Warning", msgDialogText, wReq, 5);
    }
    return;
  }

  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().StartEditMsg(profileId, chatId, msgId);
}

void UiModel::OnKeyQuit()
{
  if (Status::Get(Status::FlagSyncing) & Status::FlagSyncing)
  {
    if (!MessageDialog("Confirmation", "Syncing in progress, confirm exit?", 0.75, 5))
    {
      return;
    }
  }

  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().OnKeyQuit();
}

void UiModel::OnKeyExtCall()
{
  std::string phone;
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    phone = GetImpl().PreExtCall();
  }

  if (phone.empty())
  {
    MessageDialog("Warning", "Contact phone number unknown.", 0.7, 5);
    return;
  }

  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().StartExtCall(phone);
}

void UiModel::OnKeyAutoCompose()
{
  static const bool autoComposeEnabled = [&]()
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    return UiConfig::GetBool("auto_compose_enabled");
  }();

  if (!autoComposeEnabled)
  {
    MessageDialog("Warning", "Auto-compose not enabled.", 0.7, 5);
    return;
  }

  bool rv = false;
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    rv = GetImpl().AutoCompose();
  }

  if (!rv)
  {
    MessageDialog("Warning", "Auto-compose failed.", 0.7, 5);
  }
}

void UiModel::OnKeyCut()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().Cut();
}

void UiModel::OnKeyCopy()
{
  std::unique_lock<owned_mutex> lock(m_ModelMutex);
  GetImpl().Copy();
}

void UiModel::OnKeyPaste()
{
  if (Clipboard::HasImage())
  {
    // Open modal dialog without model mutex held
    static const bool confirmSendPastedImage = UiConfig::GetBool("confirm_send_pasted_image");
    if (confirmSendPastedImage)
    {
      if (!MessageDialog("Confirmation", "Send pasted image?", 0.5, 5))
      {
        return;
      }
    }

    static int index = 0;
    ++index;
    const std::string tempPath = FileUtil::GetTempDir() + "/clipboard-" + std::to_string(index) + ".png";
    if (!Clipboard::GetImage(tempPath))
    {
      MessageDialog("Warning", "Failed getting clipboard image.", 0.7, 5);
      return;
    }

    const std::vector<std::string> tempPaths = { tempPath };

    {
      std::unique_lock<owned_mutex> lock(m_ModelMutex);
      GetImpl().TransferFile(tempPaths);
    }

    // Delete temp clipboard images after some time, allowing for it to be sent async
    FileUtil::RmFilesByAge(FileUtil::GetTempDir(), "clipboard-*.png", 60);
  }
  else
  {
    std::unique_lock<owned_mutex> lock(m_ModelMutex);
    GetImpl().Paste();
  }
}

bool UiModel::IsAttachmentDownloaded(const FileInfo& p_FileInfo)
{
  return UiModel::Impl::IsAttachmentDownloaded(p_FileInfo);
}

bool UiModel::IsAttachmentDownloadable(const FileInfo& p_FileInfo)
{
  return UiModel::Impl::IsAttachmentDownloadable(p_FileInfo);
}
