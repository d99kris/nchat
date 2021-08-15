// uimodel.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uimodel.h"

#include <algorithm>

#include <ncursesw/ncurses.h>

#include "appconfig.h"
#include "fileutil.h"
#include "log.h"
#include "messagecache.h"
#include "numutil.h"
#include "sethelp.h"
#include "strutil.h"
#include "timeutil.h"
#include "uidialog.h"
#include "uiconfig.h"
#include "uicontactlistdialog.h"
#include "uiemojilistdialog.h"
#include "uifilelistdialog.h"
#include "uikeyconfig.h"
#include "uimessagedialog.h"
#include "uiview.h"

UiModel::UiModel()
{
  m_View = std::make_shared<UiView>(this);
}

UiModel::~UiModel()
{
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

  static wint_t keyOpen = UiKeyConfig::GetKey("open");
  static wint_t keySave = UiKeyConfig::GetKey("save");

  static wint_t keyToggleList = UiKeyConfig::GetKey("toggle_list");
  static wint_t keyToggleTop = UiKeyConfig::GetKey("toggle_top");
  static wint_t keyToggleHelp = UiKeyConfig::GetKey("toggle_help");
  static wint_t keyToggleEmoji = UiKeyConfig::GetKey("toggle_emoji");

  static wint_t keyOtherCommandsHelp = UiKeyConfig::GetKey("other_commands_help");

  if (p_Key == KEY_RESIZE)
  {
    SetHelpOffset(0);
    ReinitView();
  }
  else if (p_Key == keyToggleHelp)
  {
    m_View->SetHelpEnabled(!m_View->GetHelpEnabled());
    ReinitView();
  }
  else if (p_Key == keyToggleList)
  {
    m_View->SetListEnabled(!m_View->GetListEnabled());
    ReinitView();
  }
  else if (p_Key == keyToggleTop)
  {
    m_View->SetTopEnabled(!m_View->GetTopEnabled());
    ReinitView();
  }
  else if (p_Key == keyToggleEmoji)
  {
    m_View->SetEmojiEnabled(!m_View->GetEmojiEnabled());
    UpdateList();
    UpdateStatus();
    UpdateHistory();
    UpdateEntry();
  }
  else if (p_Key == keyNextChat)
  {
    NextChat();
  }
  else if (p_Key == keyPrevChat)
  {
    PrevChat();
  }
  else if (p_Key == keyUnreadChat)
  {
    UnreadChat();
  }
  else if (p_Key == keyPrevPage)
  {
    PrevPage();
  }
  else if (p_Key == keyNextPage)
  {
    NextPage();
  }
  else if (p_Key == keyHome)
  {
    Home();
  }
  else if (p_Key == keyEnd)
  {
    End();
  }
  else if (p_Key == keyQuit)
  {
    m_Running = false;
  }
  else if (p_Key == keySendMsg)
  {
    SendMessage();
  }
  else if (p_Key == keyDeleteMsg)
  {
    DeleteMessage();
  }
  else if (p_Key == keyOpen)
  {
    OpenMessageAttachment();
  }
  else if (p_Key == keySave)
  {
    SaveMessageAttachment();
  }
  else if (p_Key == keyTransfer)
  {
    TransferFile();
  }
  else if (p_Key == keySelectEmoji)
  {
    InsertEmoji();
  }
  else if (p_Key == keySelectContact)
  {
    SearchContact();
  }
  else if (p_Key == keyOtherCommandsHelp)
  {
    SetHelpOffset(GetHelpOffset() + 1);
    m_View->Draw();
  }
  else
  {
    EntryKeyHandler(p_Key);
  }
}

void UiModel::SendMessage()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  std::wstring& entryStr = m_EntryStr[profileId][chatId];
  int& entryPos = m_EntryPos[profileId][chatId];

  if (entryStr.empty()) return;

  std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
  sendMessageRequest->chatId = chatId;
  std::string str;
  if (m_View->GetEmojiEnabled())
  {
    std::wstring wstr = entryStr;
    wstr.erase(std::remove(wstr.begin(), wstr.end(), EMOJI_PAD), wstr.end());
    str = StrUtil::ToString(wstr);
  }
  else
  {
    std::wstring wstr = entryStr;
    str = StrUtil::Emojize(StrUtil::ToString(wstr));
  }
    
  sendMessageRequest->chatMessage.text = str;

  if (GetSelectMessage())
  {
    const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
    const int messageOffset = m_MessageOffset[profileId][chatId];
    std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

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

    sendMessageRequest->chatMessage.quotedId = msg->second.id;
    sendMessageRequest->chatMessage.quotedText = msg->second.text;
    sendMessageRequest->chatMessage.quotedSender = msg->second.senderId;

    SetSelectMessage(false);
  }

  m_Protocols[profileId]->SendRequest(sendMessageRequest);

  entryStr.clear();
  entryPos = 0;

  UpdateEntry();
  ResetMessageOffset();
}

void UiModel::EntryKeyHandler(wint_t p_Key)
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);

  static wint_t keyDown = UiKeyConfig::GetKey("down");
  static wint_t keyUp = UiKeyConfig::GetKey("up");
  static wint_t keyLeft = UiKeyConfig::GetKey("left");
  static wint_t keyRight = UiKeyConfig::GetKey("right");
  static wint_t keyBackspace = UiKeyConfig::GetKey("backspace");
  static wint_t keyAltBackspace = UiKeyConfig::GetKey("alt_backspace");
  static wint_t keyDelete = UiKeyConfig::GetKey("delete");
  static wint_t keyDeleteLine = UiKeyConfig::GetKey("delete_line");

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  int& entryPos = m_EntryPos[profileId][chatId];
  std::wstring& entryStr = m_EntryStr[profileId][chatId];

  const int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];

  if (p_Key == keyUp)
  {
    if (GetSelectMessage())
    {
      messageOffset = std::min(messageOffset + 1, messageCount - 1);
      RequestMessages();
    }
    else
    {
      if ((entryPos == 0) && (messageCount > 0))
      {
        SetSelectMessage(true);
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
    if (GetSelectMessage())
    {
      if (messageOffset > 0)
      {
        messageOffset = messageOffset - 1;
      }
      else
      {
        SetSelectMessage(false);
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
  else if ((p_Key == keyBackspace) || (p_Key == keyAltBackspace))
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
  else if (p_Key == keyDeleteLine)
  {
    StrUtil::DeleteToMatch(entryStr, entryPos, L'\n');
  }
  else if (StrUtil::IsValidTextKey(p_Key))
  {
    entryStr.insert(entryPos++, 1, p_Key);
    SetTyping(profileId, chatId, true);
  }
  else
  {
    return;
  }

  UpdateEntry();
}

void UiModel::SetTyping(const std::string& p_ProfileId, const std::string& p_ChatId, bool p_IsTyping)
{
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
    m_Protocols[lastProfileId]->SendRequest(sendTypingRequest);

    lastProfileId = "";
    lastChatId = "";
    lastIsTyping = false;
    return;
  }

  if (p_IsTyping)
  {
    if ((p_ProfileId == lastProfileId) && (p_ChatId == lastChatId) && (p_IsTyping == lastIsTyping))
    {
      if (m_Protocols[p_ProfileId]->HasFeature(TypingTimeout) && ((nowTime - lastSendTime) > 2500))
      {
        LOG_TRACE("send typing %s refresh", p_ChatId.c_str());

        std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
        sendTypingRequest->chatId = p_ChatId;
        sendTypingRequest->isTyping = true;
        m_Protocols[p_ProfileId]->SendRequest(sendTypingRequest);
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
        m_Protocols[lastProfileId]->SendRequest(sendTypingRequest);
      }

      LOG_TRACE("send typing %s", p_ChatId.c_str());

      std::shared_ptr<SendTypingRequest> sendTypingRequest = std::make_shared<SendTypingRequest>();
      sendTypingRequest->chatId = p_ChatId;
      sendTypingRequest->isTyping = true;
      m_Protocols[p_ProfileId]->SendRequest(sendTypingRequest);
      lastSendTime = nowTime;

      lastProfileId = p_ProfileId;
      lastChatId = p_ChatId;
      lastIsTyping = p_IsTyping;
    }

    lastTypeTime = nowTime;
  }
}

void UiModel::NextChat()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  if (m_ChatVec.empty()) return;

  if (m_CurrentChatIndex < 0)
  {
    m_CurrentChatIndex = 0;
  }

  ++m_CurrentChatIndex;
  if (m_CurrentChatIndex >= (int)m_ChatVec.size())
  {
    m_CurrentChatIndex = 0;
  }

  m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
  OnCurrentChatChanged();
  SetSelectMessage(false);
}

void UiModel::PrevChat()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  if (m_ChatVec.empty()) return;

  --m_CurrentChatIndex;
  if (m_CurrentChatIndex < 0)
  {
    m_CurrentChatIndex = m_ChatVec.size() - 1;
  }

  m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
  OnCurrentChatChanged();
  SetSelectMessage(false);
}

void UiModel::UnreadChat()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  if (m_ChatVec.empty()) return;

  for (size_t i = 0; i < m_ChatVec.size(); ++i)
  {
    const std::pair<std::string, std::string>& chat = m_ChatVec.at(i);
    const ChatInfo& chatInfo = m_ChatInfos[chat.first][chat.second];
    if (chatInfo.isUnread)
    {
      m_CurrentChatIndex = i;
      m_CurrentChat = m_ChatVec.at(m_CurrentChatIndex);
      OnCurrentChatChanged();
      SetSelectMessage(false);
      break;
    }
  }
}

void UiModel::PrevPage()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  int historyShowCount = m_View->GetHistoryShowCount();
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  const int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  int addOffset = std::min(historyShowCount, std::max(messageCount - messageOffset - 1, 0));
  if (addOffset > 0)
  {
    messageOffsetStack.push(addOffset);
    messageOffset += addOffset;
    RequestMessages();
    UpdateHistory();
  }

  SetSelectMessage(false);
}

void UiModel::NextPage()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
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
    messageOffset -= decOffset;
    UpdateHistory();
  }

  SetSelectMessage(false);
}

void UiModel::Home()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  
  bool& fetchedAllCache = m_FetchedAllCache[profileId][chatId];
  if (!fetchedAllCache)
  {
    fetchedAllCache = true;
    std::string fromId = "";
    int limit = std::numeric_limits<int>::max();
    lock.unlock();
    LOG_DEBUG("fetch all");
    bool fetchResult = MessageCache::Fetch(profileId, chatId, fromId, limit, true /* p_Sync */);
    lock.lock();
    fetchedAllCache = fetchResult;
  }

  int messageCount = m_Messages[profileId][chatId].size();
  int& messageOffset = m_MessageOffset[profileId][chatId];
  std::stack<int>& messageOffsetStack = m_MessageOffsetStack[profileId][chatId];

  int addOffset = std::max(messageCount - messageOffset - 1, 0);
  if (addOffset > 0)
  {
    for (int i = 0; i < addOffset; ++i)
    {
      messageOffsetStack.push(1); // @todo: consider building a nicer stack for page down from home
    }
    
    messageOffset += addOffset;
    RequestMessages();
    UpdateHistory();
  }

  SetSelectMessage(false);
}

void UiModel::End()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  ResetMessageOffset();

  SetSelectMessage(false);
}

void UiModel::ResetMessageOffset()
{
  // must be called under lock
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

void UiModel::MarkRead(const std::string& p_ProfileId, const std::string& p_ChatId, const std::string& p_MsgId)
{
  std::shared_ptr<MarkMessageReadRequest> markMessageReadRequest = std::make_shared<MarkMessageReadRequest>();
  markMessageReadRequest->chatId = p_ChatId;
  markMessageReadRequest->msgId = p_MsgId;
  m_Protocols[p_ProfileId]->SendRequest(markMessageReadRequest);

  m_Messages[p_ProfileId][p_ChatId][p_MsgId].isRead = true;
  MessageCache::UpdateIsRead(p_ProfileId, p_ChatId, p_MsgId, true);

  UpdateChatInfoIsUnread(p_ProfileId, p_ChatId);

  UpdateHistory();
  UpdateList();
}

void UiModel::DeleteMessage()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);

  if (!GetSelectMessage()) return;

  static const bool confirmDeletion = UiConfig::GetBool("confirm_deletion");
  if (confirmDeletion)
  {
    UiDialogParams params(m_View.get(), this, "Confirmation", 50, 25);
    std::string dialogText = "Confirm message deletion?";
    UiMessageDialog messageDialog(params, dialogText);
    if (!messageDialog.Run())
    {
      ReinitView();
      return;
    } 

    ReinitView();
  }
 
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];
  int& messageOffset = m_MessageOffset[profileId][chatId];

  auto it = std::next(messageVec.begin(), messageOffset);
  if (it == messageVec.end())
  {
    LOG_WARNING("error finding message id to delete");
    return;
  }

  std::string msgId = *it;
  std::shared_ptr<DeleteMessageRequest> deleteMessageRequest = std::make_shared<DeleteMessageRequest>();
  deleteMessageRequest->chatId = chatId;
  deleteMessageRequest->msgId = msgId;
  m_Protocols[profileId]->SendRequest(deleteMessageRequest);
  messages.erase(*it);
  messageVec.erase(it);
  
  MessageCache::Delete(profileId, chatId, msgId);
}

void UiModel::OpenMessageAttachment()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);

  if (!GetSelectMessage()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

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

  std::string filePath = mit->second.filePath;
  if (filePath.empty() || (filePath == " "))
  {
    LOG_WARNING("message has no attachment");
    return;
  }

#if defined(__APPLE__)
  std::string cmd = "open " + filePath + " &";
  LOG_TRACE("run cmd %s", cmd.c_str());
  system(cmd.c_str());
#elif defined(__linux__)
  std::string cmd = "xdg-open >/dev/null 2>&1 " + filePath + " &";
  LOG_TRACE("run cmd %s", cmd.c_str());
  system(cmd.c_str());
#else
  LOG_WARNING("unsupported os");
#endif
}

void UiModel::SaveMessageAttachment()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);

  if (!GetSelectMessage()) return;

  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;
  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  const int messageOffset = m_MessageOffset[profileId][chatId];
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[profileId][chatId];

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

  std::string filePath = mit->second.filePath;
  if (filePath.empty() || (filePath == " "))
  {
    LOG_WARNING("message has no attachment");
    return;
  }

  std::string srcFileName = FileUtil::BaseName(filePath);
  std::string downloadsDir = FileUtil::GetDownloadsDir();
  std::string dstFileName = srcFileName;
  int i = 1;
  while (FileUtil::Exists(downloadsDir + "/" + dstFileName))
  {
    dstFileName = FileUtil::RemoveFileExt(srcFileName) + "_" + std::to_string(i++) + FileUtil::GetFileExt(srcFileName);
  }

  std::string dstFilePath = downloadsDir + "/" + dstFileName;
  FileUtil::CopyFile(filePath, dstFilePath);

  UiDialogParams params(m_View.get(), this, "Notification", 80, 25);
  std::string dialogText = "File saved in\n" + dstFilePath;
  UiMessageDialog messageDialog(params, dialogText);
  messageDialog.Run();
  ReinitView();
}

void UiModel::TransferFile()
{
  UiDialogParams params(m_View.get(), this, "Select File", 75, 65);
  UiFileListDialog dialog(params);
  if (dialog.Run())
  {
    std::string path = dialog.GetSelectedPath();

    std::unique_lock<std::mutex> lock(m_ModelMutex);

    std::string profileId = m_CurrentChat.first;
    std::string chatId = m_CurrentChat.second;

    std::shared_ptr<SendMessageRequest> sendMessageRequest = std::make_shared<SendMessageRequest>();
    sendMessageRequest->chatId = chatId;
    sendMessageRequest->chatMessage.filePath = path;
    sendMessageRequest->chatMessage.fileType = FileUtil::GetMimeType(path);

    m_Protocols[profileId]->SendRequest(sendMessageRequest);
  }

  std::unique_lock<std::mutex> lock(m_ModelMutex);
  ReinitView();
  ResetMessageOffset();
}

void UiModel::InsertEmoji()
{
  UiDialogParams params(m_View.get(), this, "Insert Emoji", 75, 65);
  UiEmojiListDialog dialog(params);
  if (dialog.Run())
  {
    std::wstring emoji = dialog.GetSelectedEmoji();

    std::unique_lock<std::mutex> lock(m_ModelMutex);

    std::string profileId = m_CurrentChat.first;
    std::string chatId = m_CurrentChat.second;
    int& entryPos = m_EntryPos[profileId][chatId];
    std::wstring& entryStr = m_EntryStr[profileId][chatId];

    entryStr.insert(entryPos, emoji);
    entryPos += emoji.size();

    if (m_View->GetEmojiEnabled() && (StrUtil::WStringWidth(emoji) > 1))
    {
      entryStr.insert(entryPos, std::wstring((wchar_t)EMOJI_PAD, 1));
      entryPos += 1;
    }

    SetTyping(profileId, chatId, true);
    UpdateEntry();
  }

  ReinitView();
}

void UiModel::SearchContact()
{
  UiDialogParams params(m_View.get(), this, "Select Contact", 75, 65);
  UiContactListDialog dialog(params);
  if (dialog.Run())
  {
    std::pair<std::string, ContactInfo> selectedContact = dialog.GetSelectedContact();
    std::string profileId = selectedContact.first;
    std::string userId = selectedContact.second.id;

    LOG_INFO("selected %s contact %s", profileId.c_str(), userId.c_str());

    std::unique_lock<std::mutex> lock(m_ModelMutex);
    std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[profileId];
    if (profileChatInfos.count(userId))
    {
      m_CurrentChatIndex = 0;
      m_CurrentChat.first = profileId;
      m_CurrentChat.second = userId;
      SortChats();
      OnCurrentChatChanged();
      SetSelectMessage(false);
    }
    else
    {
      LOG_TRACE("create chat %s", userId.c_str());
      std::shared_ptr<CreateChatRequest> createChatRequest = std::make_shared<CreateChatRequest>();
      createChatRequest->userId = userId;
      m_Protocols[profileId]->SendRequest(createChatRequest);
    }
  }

  ReinitView();
}

void UiModel::MessageHandler(std::shared_ptr<ServiceMessage> p_ServiceMessage)
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  const std::string profileId = p_ServiceMessage->profileId;
  switch (p_ServiceMessage->GetMessageType())
  {
    case ConnectNotifyType:
      {
        std::shared_ptr<ConnectNotify> connectNotify = std::static_pointer_cast<ConnectNotify>(p_ServiceMessage);
        if (connectNotify->success)
        {
          LOG_TRACE("connected");
          if (!m_Protocols[profileId]->HasFeature(AutoGetChatsOnLogin))
          {
            std::shared_ptr<GetChatsRequest> getChatsRequest = std::make_shared<GetChatsRequest>();
            LOG_TRACE("get chats");
            m_Protocols[profileId]->SendRequest(getChatsRequest);
          }

          SetStatusOnline(profileId, true);
        }
      }
      break;

    case NewContactsNotifyType:
      {
        std::shared_ptr<NewContactsNotify> newContactsNotify = std::static_pointer_cast<NewContactsNotify>(
          p_ServiceMessage);
        const std::vector<ContactInfo>& contactInfos = newContactsNotify->contactInfos;
        for (auto& contactInfo : contactInfos)
        {
          LOG_TRACE("NewContacts");
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
          LOG_TRACE("new chats");
          for (auto& chatInfo : newChatsNotify->chatInfos)
          {
            m_ChatInfos[profileId][chatInfo.id] = chatInfo;

            if (m_ChatSet[profileId].insert(chatInfo.id).second)
            {
              m_ChatVec.push_back(std::make_pair(profileId, chatInfo.id));
            }
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
            LOG_TRACE("new cached messages %s count %d from %s", chatId.c_str(), chatMessages.size(), fromMsgId.c_str());
          }

          if (!newMessagesNotify->cached)
          {
            MessageCache::Add(profileId, chatId, fromMsgId, chatMessages);
          }

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
          }

          if (hasNewMessage)
          {
            std::string currentMessageId;
            int& messageOffset = m_MessageOffset[profileId][chatId];
            if ((profileId == m_CurrentChat.first) && (chatId == m_CurrentChat.second))
            {
              if (GetSelectMessage() && (messageOffset < (int)messageVec.size()))
              {
                currentMessageId = messageVec[messageOffset];
              }
            }

            std::sort(messageVec.begin(), messageVec.end(),
                      [&](const std::string& lhs, const std::string& rhs) -> bool
            {
              return messages.at(lhs).timeSent > messages.at(rhs).timeSent;
            });

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
                RequestMessages();
              }

              UpdateHistory();
            }
          }

          UpdateChatInfoLastMessageTime(profileId, chatId);
          UpdateChatInfoIsUnread(profileId, chatId);
          SortChats();
          UpdateList();
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
          if (GetSelectMessage())
          {
            int& messageOffset = m_MessageOffset[profileId][chatId];
            if (messageVec.empty())
            {
              messageOffset = 0;
              SetSelectMessage(false);
            }
            else
            {
              if ((messageOffset + 1) > (int)messageVec.size())
              {
                messageOffset = (int)messageVec.size() - 1;
              }
            }
          }

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
        m_Messages[profileId][chatId][msgId].isRead = isRead;
        MessageCache::UpdateIsRead(profileId, chatId, msgId, isRead);

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
        std::string filePath = newMessageFileNotify->filePath;
        LOG_TRACE("new file path for %s is %s", msgId.c_str(), filePath.c_str());
        m_Messages[profileId][chatId][msgId].filePath = filePath;
        MessageCache::UpdateFilePath(profileId, chatId, msgId, filePath);

        UpdateHistory();
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
        LOG_TRACE("received user %s is %s", userId.c_str(), (isOnline ? "online" : "away"));
        m_UserOnline[profileId][userId] = isOnline;
        UpdateStatus();
      }
      break;

    case CreateChatNotifyType:
      {
        std::shared_ptr<CreateChatNotify> createChatNotify =
          std::static_pointer_cast<CreateChatNotify>(p_ServiceMessage);
        if (createChatNotify->success)
        {
          LOG_TRACE("chat created %s", profileId.c_str());
          const ChatInfo& chatInfo = createChatNotify->chatInfo;
          m_ChatInfos[profileId][chatInfo.id] = chatInfo;
          if (m_ChatSet[profileId].insert(chatInfo.id).second)
          {
            m_ChatVec.push_back(std::make_pair(profileId, chatInfo.id));
          }

          m_CurrentChat.first = profileId;
          m_CurrentChat.second = chatInfo.id;
          SortChats();
          OnCurrentChatChanged();
          SetSelectMessage(false);
        }
      }
      break;

    default:
      LOG_DEBUG("unknown service message %d", p_ServiceMessage->GetMessageType());
      break;
  }
}


void UiModel::AddProtocol(std::shared_ptr<Protocol> p_Protocol)
{
  m_Protocols[p_Protocol->GetProfileId()] = p_Protocol;
}

std::unordered_map<std::string, std::shared_ptr<Protocol>>& UiModel::GetProtocols()
{
  return m_Protocols;
}

bool UiModel::Process()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  if (m_TriggerTerminalBell)
  {
    m_TriggerTerminalBell = false;
    m_View->TerminalBell();
  }

  SetTyping("", "", false);
  m_View->Draw();
  return m_Running;
}

void UiModel::SortChats()
{
  std::sort(m_ChatVec.begin(), m_ChatVec.end(),
  [&](const std::pair<std::string, std::string>& lhs, const std::pair<std::string, std::string>& rhs) -> bool
  {
    return m_ChatInfos[lhs.first][lhs.second].lastMessageTime > m_ChatInfos[rhs.first][rhs.second].lastMessageTime;
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

void UiModel::UpdateChatInfoLastMessageTime(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
  if (messageVec.empty()) return;

  const std::string& lastMessageId = messageVec.at(0);
  const ChatMessage& lastChatMessage = messages[lastMessageId];

  std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[p_ProfileId];
  if (profileChatInfos.count(p_ChatId))
  {
    profileChatInfos[p_ChatId].lastMessageTime = lastChatMessage.timeSent;
  }
}

void UiModel::UpdateChatInfoIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  std::unordered_map<std::string, ChatMessage>& messages = m_Messages[p_ProfileId][p_ChatId];
  std::vector<std::string>& messageVec = m_MessageVec[p_ProfileId][p_ChatId];
  if (messageVec.empty()) return;

  bool isRead = true;
  std::string messageId = *messageVec.begin();
  const ChatMessage& chatMessage = messages[messageId];
  isRead = chatMessage.isOutgoing ? true : chatMessage.isRead;

  bool isUnread = !isRead;
  bool hasMention = chatMessage.hasMention;
  std::unordered_map<std::string, ChatInfo>& profileChatInfos = m_ChatInfos[p_ProfileId];
  if (profileChatInfos.count(p_ChatId))
  {
    if (!profileChatInfos[p_ChatId].isUnread && isUnread)
    {
      if (!profileChatInfos[p_ChatId].isMuted || hasMention)
      {
        NotifyNewUnread();
      }
    }

    profileChatInfos[p_ChatId].isUnread = isUnread;
  }
}

std::string UiModel::GetContactName(const std::string& p_ProfileId, const std::string& p_ChatId)
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

std::string UiModel::GetContactListName(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ContactInfo& contactInfo = m_ContactInfos[p_ProfileId][p_ChatId];
  const std::string& chatName = contactInfo.name;
  if (contactInfo.isSelf)
  {
    return "Saved Messages";
  }
  else if (chatName.empty())
  {
    return p_ChatId;
  }

  return chatName;
}

bool UiModel::GetChatIsUnread(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const ChatInfo& chatInfo = m_ChatInfos.at(p_ProfileId).at(p_ChatId);
  return chatInfo.isUnread; // @todo: handle isUnreadMention, isMuted
}

std::string UiModel::GetChatStatus(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  const std::set<std::string>& usersTyping = m_UsersTyping[p_ProfileId][p_ChatId];
  if (!usersTyping.empty())
  {
    if (usersTyping.size() > 1)
    {
      std::vector<std::string> userIds = ToVector(usersTyping);
      std::vector<std::string> userNames;
      for (auto& userId : userIds)
      {
        userNames.push_back(GetContactListName(p_ProfileId, userId));
      }

      std::string userNamesJoined = StrUtil::Join(userNames, ", ");
      return "(" + userNamesJoined + " are typing)";
    }
    else
    {
      std::string userId = *usersTyping.begin();
      if (userId == p_ChatId)
      {
        return "(typing)";
      }
      else
      {
        std::string userName = GetContactName(p_ProfileId, userId);
        return "(" + userName + " is typing)";
      }
    }
  }

  if (m_UserOnline[p_ProfileId].count(p_ChatId))
  {
    return m_UserOnline[p_ProfileId][p_ChatId] ? "(online)" : "(away)";
  }

  return "";
}

void UiModel::OnCurrentChatChanged()
{
  LOG_TRACE("current chat %s %s", m_CurrentChat.first.c_str(), m_CurrentChat.second.c_str());
  UpdateList();
  UpdateStatus();
  UpdateHistory();
  UpdateHelp();
  UpdateEntry();
  RequestMessages();
}

void UiModel::RequestMessages()
{
  std::string profileId = m_CurrentChat.first;
  std::string chatId = m_CurrentChat.second;

  const std::vector<std::string>& messageVec = m_MessageVec[profileId][chatId];
  std::string fromId = messageVec.empty() ? "" : *messageVec.rbegin();
  bool fromIsOutgoing = messageVec.empty() ? false : m_Messages[profileId][chatId][fromId].isOutgoing;

  int messageOffset = m_MessageOffset[profileId][chatId];
  const int maxHistory = ((GetHistoryLines() * 2) / 3) + 1;
  const int limit = std::max(0, (messageOffset + maxHistory - (int)messageVec.size()));
  if (limit == 0)
  {
    return;
  }

  std::unordered_set<std::string>& msgFromIdsRequested = m_MsgFromIdsRequested[profileId][chatId];
  if (msgFromIdsRequested.find(fromId) != msgFromIdsRequested.end())
  {
    LOG_TRACE("get messages from %s already requested", fromId.c_str());
    return;
  }
  else
  {
    msgFromIdsRequested.insert(fromId);
  }

  if (fromId.empty() || !MessageCache::Fetch(profileId, chatId, fromId, limit, false /* p_Sync */))
  {
    std::shared_ptr<GetMessagesRequest> getMessagesRequest = std::make_shared<GetMessagesRequest>();
    getMessagesRequest->chatId = chatId;
    getMessagesRequest->fromMsgId = fromId;
    getMessagesRequest->limit = limit;
    getMessagesRequest->fromIsOutgoing = fromIsOutgoing;
    LOG_TRACE("request messages from %s limit %d", fromId.c_str(), limit);
    m_Protocols[m_CurrentChat.first]->SendRequest(getMessagesRequest);
  }
}

void UiModel::SetStatusOnline(const std::string& p_ProfileId, bool p_IsOnline)
{
  std::shared_ptr<SetStatusRequest> setStatusRequest = std::make_shared<SetStatusRequest>();
  setStatusRequest->isOnline = p_IsOnline;
  LOG_TRACE("set status %s online %d", p_ProfileId.c_str(), (int)p_IsOnline);
  m_Protocols[p_ProfileId]->SendRequest(setStatusRequest);
}

int UiModel::GetHistoryLines()
{
  return m_View->GetHistoryLines();
}

void UiModel::RequestContacts()
{
  for (auto& protocol : m_Protocols)
  {
    LOG_TRACE("get contacts %s", protocol.first.c_str());
    std::shared_ptr<GetContactsRequest> getContactsRequest = std::make_shared<GetContactsRequest>();
    protocol.second->SendRequest(getContactsRequest);
  }
}

void UiModel::SetRunning(bool p_Running)
{
  m_Running = p_Running;
}

void UiModel::ReinitView()
{
  m_View->Init();
}

void UiModel::UpdateList()
{
  m_View->SetListDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::UpdateStatus()
{
  m_View->SetStatusDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::UpdateHistory()
{
  m_View->SetHistoryDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::UpdateHelp()
{
  m_View->SetHelpDirty(true);
  m_View->SetEntryDirty(true);
}

void UiModel::UpdateEntry()
{
  m_View->SetEntryDirty(true);
}

void UiModel::NotifyNewUnread()
{
  m_TriggerTerminalBell = true; // @todo: allow disabling terminal bell
}

std::wstring& UiModel::GetEntryStr()
{
  return m_EntryStr[m_CurrentChat.first][m_CurrentChat.second];
}

int& UiModel::GetEntryPos()
{
  return m_EntryPos[m_CurrentChat.first][m_CurrentChat.second];
}

std::vector<std::pair<std::string, std::string>>& UiModel::GetChatVec()
{
  return m_ChatVec;
}

std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> UiModel::GetContactInfos()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  return m_ContactInfos;
}

int64_t UiModel::GetContactInfosUpdateTime()
{
  std::unique_lock<std::mutex> lock(m_ModelMutex);
  return m_ContactInfosUpdateTime;
}

std::pair<std::string, std::string>& UiModel::GetCurrentChat()
{
  return m_CurrentChat;
}

int& UiModel::GetCurrentChatIndex()
{
  return m_CurrentChatIndex;
}

std::unordered_map<std::string, ChatMessage>& UiModel::GetMessages(const std::string& p_ProfileId,
                                                                   const std::string& p_ChatId)
{
  return m_Messages[p_ProfileId][p_ChatId];
}

std::vector<std::string>& UiModel::GetMessageVec(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  return m_MessageVec[p_ProfileId][p_ChatId];
}

int& UiModel::GetMessageOffset(const std::string& p_ProfileId, const std::string& p_ChatId)
{
  return m_MessageOffset[p_ProfileId][p_ChatId];
}

bool UiModel::GetSelectMessage()
{
  return m_SelectMessage;
}

void UiModel::SetSelectMessage(bool p_SelectMessage)
{
  m_SelectMessage = p_SelectMessage;
  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::GetListDialogActive()
{
  return m_ListDialogActive;
}

void UiModel::SetListDialogActive(bool p_ListDialogActive)
{
  m_ListDialogActive = p_ListDialogActive;
  SetHelpOffset(0);
  UpdateHelp();
}

bool UiModel::GetMessageDialogActive()
{
  return m_MessageDialogActive;
}

void UiModel::SetMessageDialogActive(bool p_MessageDialogActive)
{
  m_MessageDialogActive = p_MessageDialogActive;
  SetHelpOffset(0);
  UpdateHelp();
}

void UiModel::SetHelpOffset(int p_HelpOffset)
{
  m_HelpOffset = p_HelpOffset;
  UpdateHelp();
}

int UiModel::GetHelpOffset()
{
  return m_HelpOffset;
}

bool UiModel::GetEmojiEnabled()
{
  return m_View->GetEmojiEnabled();
}
