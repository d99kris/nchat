// uicommon.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilite.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <sstream>

#include <locale.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <emoji.h>
#include <ncursesw/ncurses.h>
#include <path.hpp>

#include "chat.h"
#include "config.h"
#include "message.h"
#include "protocol.h"
#include "util.h"

#define EMOJI_PAD   1

UiCommon::UiCommon(const std::string& p_Name)
  : m_Name(p_Name)
{
}

UiCommon::~UiCommon()
{
}

void UiCommon::Init()
{
  // Config
  const std::map<std::string, std::string> commonConfig =
  {
    // keys
    {"key_next_chat", "KEY_TAB"},
    {"key_prev_chat", "KEY_BTAB"},
    {"key_next_page", "KEY_NPAGE"},
    {"key_prev_page", "KEY_PPAGE"},
    {"key_curs_up", "KEY_UP"},
    {"key_curs_down", "KEY_DOWN"},
    {"key_curs_left", "KEY_LEFT"},
    {"key_curs_right", "KEY_RIGHT"},
    {"key_backspace", "KEY_BACKSPACE"},
    {"key_delete", "KEY_DC"},
    {"key_linebreak", "KEY_RETURN"},
    {"key_send", "KEY_CTRLX"},
    {"key_next_unread", "KEY_CTRLU"},
    {"key_exit", "KEY_CTRLQ"},
    {"key_toggle_emoji", "KEY_CTRLE"},
    {"key_transmit_file", "KEY_CTRLT"},
    {"key_receive_file", "KEY_CTRLR"},
    {"key_exit", "KEY_CTRLQ"},
    // layout
    {"input_rows", "3"},
  };

  std::map<std::string, std::string> defaultConfig = GetPrivateConfig();
  defaultConfig.insert(commonConfig.begin(), commonConfig.end());

  const std::string configPath(Util::GetConfigDir() + std::string("/") + m_Name + std::string(".conf"));
  m_Config = Config(configPath, defaultConfig);

  m_KeyNextChat = Util::GetKeyCode(m_Config.Get("key_next_chat"));
  m_KeyPrevChat = Util::GetKeyCode(m_Config.Get("key_prev_chat"));
  m_KeyNextPage = Util::GetKeyCode(m_Config.Get("key_next_page"));
  m_KeyPrevPage = Util::GetKeyCode(m_Config.Get("key_prev_page"));
  m_KeyCursUp = Util::GetKeyCode(m_Config.Get("key_curs_up"));
  m_KeyCursDown = Util::GetKeyCode(m_Config.Get("key_curs_down"));
  m_KeyCursLeft = Util::GetKeyCode(m_Config.Get("key_curs_left"));
  m_KeyCursRight = Util::GetKeyCode(m_Config.Get("key_curs_right"));
  m_KeyBackspace = Util::GetKeyCode(m_Config.Get("key_backspace"));
  m_KeyDelete = Util::GetKeyCode(m_Config.Get("key_delete"));
  m_KeyLinebreak = Util::GetKeyCode(m_Config.Get("key_linebreak"));
  m_KeySend = Util::GetKeyCode(m_Config.Get("key_send"));
  m_KeyNextUnread = Util::GetKeyCode(m_Config.Get("key_next_unread"));
  m_KeyExit = Util::GetKeyCode(m_Config.Get("key_exit"));
  m_KeyToggleEmoji = Util::GetKeyCode(m_Config.Get("key_toggle_emoji"));
  m_KeyTransmitFile = Util::GetKeyCode(m_Config.Get("key_transmit_file"));
  m_KeyReceiveFile = Util::GetKeyCode(m_Config.Get("key_receive_file"));

  m_HighlightBold = (m_Config.Get("highlight_bold") == "1");
  m_ShowEmoji = (m_Config.Get("show_emoji") == "1");

  m_InHeight = std::stoi(m_Config.Get("input_rows"));

  PrivateInit();
  
  pipe(m_Sockets);
  
  // Init screen
  setlocale(LC_ALL, "");
  initscr();
  noecho();
  cbreak();
  raw();
  keypad(stdscr, TRUE);

  SetupWin();
}

std::string UiCommon::GetName()
{
  return m_Name;
}

void UiCommon::Cleanup()
{
  m_Config.Set("show_emoji", std::to_string(m_ShowEmoji));  
  m_Config.Save();
  CleanupWin();
  wclear(stdscr);
  endwin();  
}

void UiCommon::AddProtocol(Protocol* p_Protocol)
{
  const std::string& name = p_Protocol->GetName();

  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_Protocols.find(name) == m_Protocols.end())
  {
    m_Protocols[name] = p_Protocol;
  }
}

void UiCommon::RemoveProtocol(Protocol* p_Protocol)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  m_Protocols.erase(p_Protocol->GetName());
}

void UiCommon::UpdateChat(Chat p_Chat)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  m_Chats[p_Chat.GetUniqueId()] = p_Chat;

  RequestRedraw(m_ContactWinId | m_InputWinId);
}

void UiCommon::UpdateChats(std::vector<Chat> p_Chats)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  for (auto& chat : p_Chats)
  {
    m_Chats[chat.GetUniqueId()] = chat;

    if (m_CurrentChat.empty())
    {
      SetCurrentChat(chat.GetUniqueId());
    }
  }

  RequestRedraw(m_ContactWinId);
}

void UiCommon::UpdateMessages(std::vector<Message> p_Messages, bool p_ClearChat /* = false */)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (p_ClearChat && (!p_Messages.empty()))
  {
    m_Messages[p_Messages.at(0).GetUniqueChatId()].clear();
  }

  for (auto& message : p_Messages)
  {
    m_Messages[message.GetUniqueChatId()][message.m_Id] = message;
  }

  RequestRedraw(m_OutputWinId | m_InputWinId);

  std::set<std::string> chatIds;
  for (auto& message : p_Messages)
  {
    chatIds.insert(message.GetUniqueChatId());
  }
  
  for (auto& chatId : chatIds)
  {
    if (m_Chats.find(chatId) != m_Chats.end())
    {
      const Chat& chat = m_Chats.at(chatId);
      chat.m_Protocol->RequestChatUpdate(chat.m_Id);
    }
  }    
}

void UiCommon::Run()
{
  m_Running = true;
  
  while (m_Running)
  {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    FD_SET(m_Sockets[0], &fds);
    int maxfd = std::max(STDIN_FILENO, m_Sockets[0]);
    struct timeval tv = {1, 0};
    int rv = select(maxfd + 1, &fds, NULL, NULL, &tv);

    if (rv == 0) continue;

    if (FD_ISSET(m_Sockets[0], &fds))
    {
      char mask = 0;
      char buf[128];
      int len = read(m_Sockets[0], buf, sizeof(buf));
      for (int i = 0; i < len; ++i)
      {
        mask |= buf[i];
      }

      if (mask & m_OutputWinId)
      {
        RedrawOutputWin();
      }

      if (mask & m_ContactWinId)
      {
        RedrawContactWin();
      }      

      if (mask & m_InputWinId)
      {
        RedrawInputWin();
      }
    }

    if (FD_ISSET(STDIN_FILENO, &fds))
    {
      wint_t ch;
      get_wch(&ch);
      int key = (int)ch;

      if (key == KEY_RESIZE)
      {
        CleanupWin();
        SetupWin();
        RequestRedraw(m_ContactWinId | m_InputWinId | m_OutputWinId);
      }
      else if (key == m_KeyNextChat)
      {
        NextChat(1);
      }
      else if (key == m_KeyPrevChat)
      {
        NextChat(-1);
      }
      else if (key == m_KeyNextPage)
      {
        NextPage(1);
      }
      else if (key == m_KeyPrevPage)
      {
        NextPage(-1);
      }
      else if ((key == m_KeyCursUp) || (key == m_KeyCursDown) || (key == m_KeyCursLeft) ||
               (key == m_KeyCursRight))
      {
        MoveInputCursor(key);
      }
      else if (key == m_KeyBackspace)
      {
        Backspace();
      }
      else if (key == m_KeyDelete)
      {
        Delete();
      }
      else if (key == m_KeyLinebreak)
      {
        InputBuf((wint_t)10);
      }
      else if (key == m_KeySend)
      {
        Send();
      }
      else if (key == m_KeyNextUnread)
      {
        NextUnread();
      }
      else if (key == m_KeyExit)
      {
        Exit();
      }
      else if (key == m_KeyToggleEmoji)
      {
        ToggleEmoji();
      }
      else if (key == m_KeyTransmitFile)
      {
        TransmitFile();
      }
      else if (key == m_KeyReceiveFile)
      {
        ReceiveFile();
      }
      else if (key == 16 /* CTRL_P */)
      {
        ObfuscateChatNames();
      }
      else
      {
        InputBuf(ch);
      }
    }
  }
}

void UiCommon::RequestRedraw(char p_WinId)
{
  write(m_Sockets[1], &p_WinId, 1);
}

void UiCommon::RedrawInputWin()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  std::wstring input = m_Input[m_CurrentChat];  
  const size_t inputPos = m_InputCursorPos[m_CurrentChat];
  std::wstring line;
  std::vector<std::wstring> lines;
  size_t x = 0;
  size_t y = 0;
  size_t cx = 0;
  size_t cy = 0;
  
  for (size_t i = 0; i <= input.size(); ++i)
  {
    if (i == inputPos)
    {
      cx = x;
      cy = y;
    }

    if (i < input.size())
    {
      wchar_t wcr = input.at(i);

      if (wcr != 10)
      {
        line.push_back(wcr);
        ++x;
      }
      else
      {
        x = 0;
        ++y;
        lines.push_back(line);
        line.clear();      
      }

      if (x == m_InWidth)
      {
        x = 0;
        ++y;
        lines.push_back(line);
        line.clear();
      }
    }
  }

  if (line.size() > 0)
  {
    lines.push_back(line);
    line.clear();
  }

  size_t yoffs = (cy < (m_InHeight - 1)) ? 0 : (cy - (m_InHeight - 1));

  werase(m_InWin);  
  for (size_t i = 0; i < m_InHeight; ++i)
  {
    if ((i + yoffs) < lines.size())
    {
      line = lines.at(i + yoffs).c_str();      

      line.erase(std::remove(line.begin(), line.end(), EMOJI_PAD), line.end());

      mvwaddwstr(m_InWin, i, 0, line.c_str());
    }
  }
  
  wmove(m_InWin, cy - yoffs, cx);
  wrefresh(m_InWin);

  m_InputLines[m_CurrentChat] = lines;
  m_InputCursorX[m_CurrentChat] = cx;
  m_InputCursorY[m_CurrentChat] = cy;
}

void UiCommon::RedrawOutputWin()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  werase(m_OutWin);

  if (m_ShowMsgIdBefore[m_CurrentChat].empty()) return;
  
  if (m_Messages.find(m_CurrentChat) == m_Messages.end()) return;

  const std::map<std::int64_t, Message>& chatMessages = m_Messages.at(m_CurrentChat);
  int messageWidth = m_OutWidth;
  int messageY = m_OutHeight - 2;

  std::vector<std::int64_t> viewedMessageIds;
  for (auto it = chatMessages.rbegin(); it != chatMessages.rend(); ++it)
  {
    if ((m_ShowMsgIdBefore[m_CurrentChat].top() != 0) &&
        (it->second.m_Id >= m_ShowMsgIdBefore[m_CurrentChat].top())) continue;
    
    const std::string& rawText = it->second.m_Content;
    const std::string& text = m_ShowEmoji ? rawText : emojicpp::textize(rawText);
    const std::vector<std::string>& lines = Util::WordWrap(text, messageWidth);
    for (auto line = lines.rbegin(); line != lines.rend(); ++line)
    {
      if (messageY < 0) break;

      mvwprintw(m_OutWin, messageY, 0, "%s", line->c_str());
      messageY--;
    }

    if (it->second.m_ReplyToId != 0)
    {
      auto replyIt = chatMessages.find(it->second.m_ReplyToId);
      if (replyIt != chatMessages.end())
      {
        const std::string& rawReplyText = replyIt->second.m_Content;
        const std::string& replyText = m_ShowEmoji ? rawReplyText : emojicpp::textize(rawReplyText);
        const std::vector<std::string>& replyLines = Util::WordWrap(replyText, messageWidth - 2);
        for (auto replyLine = replyLines.rbegin(); replyLine != replyLines.rend(); ++replyLine)
        {
          if (messageY < 0) break;

          mvwprintw(m_OutWin, messageY, 0, "| %s", replyLine->c_str());
          messageY--;
        }        
      }
      else
      {
        if (messageY < 0) break;

        mvwprintw(m_OutWin, messageY, 0, "| [Non-cached message]");
        messageY--;
      }
    }
    
    if (messageY < 0) break;

    time_t rawtime = it->second.m_TimeSent;
    struct tm* timeinfo;
    timeinfo = localtime(&rawtime);

    char senttimestr[64];
    strftime(senttimestr, sizeof(senttimestr), "%H:%M", timeinfo);
    std::string senttime(senttimestr);

    char sentdatestr[64];
    strftime(sentdatestr, sizeof(sentdatestr), "%Y-%m-%d", timeinfo);
    std::string sentdate(sentdatestr);

    time_t nowtime = time(NULL);
    struct tm* nowtimeinfo = localtime(&nowtime);
    char nowdatestr[64];
    strftime(nowdatestr, sizeof(nowdatestr), "%Y-%m-%d", nowtimeinfo);
    std::string nowdate(nowdatestr);
    
    std::string sender = it->second.m_Sender;
    sender.erase(sender.find_last_not_of(" \n\r\t") + 1);

    std::string timestr = (sentdate == nowdate) ? senttime : sentdate + std::string(" ") + senttime;
    wattron(m_OutWin, m_HighlightBold ? A_BOLD : A_NORMAL);
    mvwprintw(m_OutWin, messageY, 0, "%s (%s):", sender.c_str(), timestr.c_str());
    wattroff(m_OutWin, m_HighlightBold ? A_BOLD : A_NORMAL);
    messageY -= 2;

    viewedMessageIds.push_back(it->second.m_Id);
  }

  if ((messageY < 0) && (viewedMessageIds.size() > 0))
  {
    m_LowestMsgIdShown[m_CurrentChat] = viewedMessageIds.back();
  }
  
  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    const Chat& chat = m_Chats.at(m_CurrentChat);
    chat.m_Protocol->MarkRead(chat.m_Id, viewedMessageIds);
    chat.m_Protocol->RequestChatUpdate(chat.m_Id);
  }

  wrefresh(m_OutWin);
}

void UiCommon::NextPage(int p_Offset)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (p_Offset < 0)
  {
    if (!m_ShowMsgIdBefore[m_CurrentChat].empty() &&
        (m_ShowMsgIdBefore[m_CurrentChat].top() != m_LowestMsgIdShown[m_CurrentChat]))
    {
      m_ShowMsgIdBefore[m_CurrentChat].push(m_LowestMsgIdShown[m_CurrentChat]);
    }
  }
  else if (p_Offset > 0)
  {
    if (m_ShowMsgIdBefore[m_CurrentChat].size() > 1)
    {
      m_ShowMsgIdBefore[m_CurrentChat].pop();
    }
  }
  else
  {
    return;
  }

  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    const Chat& chat = m_Chats.at(m_CurrentChat);
    const int maxMsg = (m_OutHeight / 3) + 1;
    chat.m_Protocol->RequestMessages(chat.m_Id, m_ShowMsgIdBefore[m_CurrentChat].top(), maxMsg);
  }
}

void UiCommon::MoveInputCursor(int p_Key)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  bool needRedraw = false;

  std::vector<std::wstring>& lines = m_InputLines[m_CurrentChat];
  size_t& cx = m_InputCursorX[m_CurrentChat];
  size_t& cy = m_InputCursorY[m_CurrentChat];
  size_t& pos = m_InputCursorPos[m_CurrentChat];
  
  if (p_Key == m_KeyCursUp)
  {
    if (cy > 0)
    {
      pos -= cx + 1 + ((lines.at(cy - 1).size() > cx) ? (lines.at(cy - 1).size() - cx) : 0);
    }
    else
    {
      pos = 0;
    }
    
    needRedraw = true;
  }
  else if (p_Key == m_KeyCursDown)
  {
    if ((cy + 1) < lines.size())
    {
      pos += lines.at(cy).size() + 1;
      pos = std::min(pos, m_Input[m_CurrentChat].size());
    }
    else
    {
      pos = m_Input[m_CurrentChat].size();
    }

    needRedraw = true;
  }
  else if (p_Key == m_KeyCursLeft)
  {
    if (m_InputCursorPos[m_CurrentChat] > 0)
    {
      m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] - 1;
      needRedraw = true;
    }
  }
  else if (p_Key == m_KeyCursRight)
  {
    if (m_InputCursorPos[m_CurrentChat] < m_Input[m_CurrentChat].size())
    {
      m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] + 1;
      needRedraw = true;
    }
  }

  if (needRedraw)
  {
    if (m_ShowEmoji)
    {
      if (m_InputCursorPos[m_CurrentChat] < m_Input[m_CurrentChat].size())
      {
        if (m_Input[m_CurrentChat].at(m_InputCursorPos[m_CurrentChat]) == EMOJI_PAD)
        {
          if (p_Key == KEY_RIGHT)
          {
            m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] + 1;
          }
          else if (m_InputCursorPos[m_CurrentChat] > 0)
          {
            m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] - 1;
          }
        }
      }
    }
    
    RequestRedraw(m_InputWinId);
  }
}

void UiCommon::NextChat(int p_Offset)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  auto currentIt = m_Chats.find(m_CurrentChat);

  while (p_Offset > 0)
  {
    ++currentIt;
    --p_Offset;
    if (currentIt == m_Chats.end())
    {
      currentIt = m_Chats.begin();
    }
  }

  while (p_Offset < 0)
  {
    if (currentIt == m_Chats.begin())
    {
      currentIt = m_Chats.end();
    }

    --currentIt;
    ++p_Offset;
  }

  SetCurrentChat(currentIt->first);
}

void UiCommon::Backspace()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_InputCursorPos[m_CurrentChat] > 0)
  {
    if (m_ShowEmoji)
    {
      const bool wasPad = (m_Input[m_CurrentChat].at(m_InputCursorPos[m_CurrentChat] - 1) == EMOJI_PAD);
      m_Input[m_CurrentChat].erase(m_InputCursorPos[m_CurrentChat] - 1, 1);
      m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] - 1;
      if (wasPad)
      {
        m_Input[m_CurrentChat].erase(m_InputCursorPos[m_CurrentChat] - 1, 1);
        m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] - 1;
      }
    }
    else
    {
      m_Input[m_CurrentChat].erase(m_InputCursorPos[m_CurrentChat] - 1, 1);
      m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] - 1;
    }
              
    RequestRedraw(m_InputWinId);
  }
}

void UiCommon::Delete()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_Input[m_CurrentChat].size() > m_InputCursorPos[m_CurrentChat])
  {
    m_Input[m_CurrentChat].erase(m_InputCursorPos[m_CurrentChat], 1);

    if (m_ShowEmoji && (m_Input[m_CurrentChat].size() > m_InputCursorPos[m_CurrentChat]))
    {
      if (m_Input[m_CurrentChat].at(m_InputCursorPos[m_CurrentChat]) == EMOJI_PAD)
      {
        m_Input[m_CurrentChat].erase(m_InputCursorPos[m_CurrentChat], 1);
      }
    }

    RequestRedraw(m_InputWinId);
  }
}

void UiCommon::Send()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    if (!m_Input[m_CurrentChat].empty())
    {
      const Chat& chat = m_Chats.at(m_CurrentChat);
      std::wstring wstr = m_Input[m_CurrentChat];

      if (m_ShowEmoji)
      {
        wstr.erase(std::remove(wstr.begin(), wstr.end(), EMOJI_PAD), wstr.end());
      }
      else
      {
        wstr = Util::ToWString(emojicpp::emojize(Util::ToString(m_Input[m_CurrentChat])));
      }

      std::string str = Util::ToString(wstr);              
      chat.m_Protocol->SendMessage(chat.m_Id, str);
      m_Input[m_CurrentChat].clear();
      m_InputCursorPos[m_CurrentChat] = 0;
      RequestRedraw(m_InputWinId);
    }
  }
}

void UiCommon::NextUnread()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  for (auto& chat : m_Chats)
  {
    if (chat.second.m_IsUnread)
    {
      SetCurrentChat(chat.first);
      break;
    }
  }
}

void UiCommon::Exit()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  m_Running = false;
}

void UiCommon::SetCurrentChat(const std::string& p_Chat)
{
  if (p_Chat != m_CurrentChat)
  {
    m_CurrentChat = p_Chat;

    m_ShowMsgIdBefore[m_CurrentChat] = std::stack<std::int64_t>();
    m_ShowMsgIdBefore[m_CurrentChat].push(0);
    m_LowestMsgIdShown[m_CurrentChat] = std::numeric_limits<std::int64_t>::max();

    const Chat& chat = m_Chats.at(m_CurrentChat);
    const int maxMsg = (m_OutHeight / 3) + 1;
    chat.m_Protocol->RequestMessages(chat.m_Id, m_ShowMsgIdBefore[m_CurrentChat].top(), maxMsg);

    RequestRedraw(m_ContactWinId | m_InputWinId);
  }
}

void UiCommon::InputBuf(wint_t ch)
{
  std::lock_guard<std::mutex> lock(m_Lock);

  m_Input[m_CurrentChat].insert(m_InputCursorPos[m_CurrentChat], 1, ch);
  m_InputCursorPos[m_CurrentChat] = m_InputCursorPos[m_CurrentChat] + 1;

  if ((m_ShowEmoji) && (ch == L':'))
  {
    std::wstring before = m_Input[m_CurrentChat];
    std::wstring after = Util::ToWString(emojicpp::emojize(Util::ToString(m_Input[m_CurrentChat])));
    if (before != after)
    {
      m_InputCursorPos[m_CurrentChat] += after.size() - before.size();
      after.insert(m_InputCursorPos[m_CurrentChat], std::wstring(1, EMOJI_PAD));
      m_Input[m_CurrentChat] = after;
      m_InputCursorPos[m_CurrentChat] += 1;
    }
  }
          
  RequestRedraw(m_InputWinId);
}

void UiCommon::ToggleEmoji()
{
  m_ShowEmoji = !m_ShowEmoji;
  RequestRedraw(m_InputWinId | m_OutputWinId);
}

void UiCommon::TransmitFile()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    if (!m_Input[m_CurrentChat].empty())
    {
      const Chat& chat = m_Chats.at(m_CurrentChat);
      std::wstring wstr = m_Input[m_CurrentChat];
      std::string str = Util::ToString(wstr);

      if (apathy::Path(str).exists())
      {
        chat.m_Protocol->SendFile(chat.m_Id, str);
        m_Input[m_CurrentChat].clear();
        m_InputCursorPos[m_CurrentChat] = 0;
        RequestRedraw(m_InputWinId);
      }
    }
  }
}

void UiCommon::ReceiveFile()
{
  std::lock_guard<std::mutex> lock(m_Lock);

  if (m_Chats.find(m_CurrentChat) != m_Chats.end())
  {
    if (!m_Input[m_CurrentChat].empty())
    {
      const Chat& chat = m_Chats.at(m_CurrentChat);
      std::wstring wstr = m_Input[m_CurrentChat];
      std::string str = Util::ToString(wstr);

      chat.m_Protocol->DownloadFile(chat.m_Id, str);
      m_Input[m_CurrentChat].clear();
      m_InputCursorPos[m_CurrentChat] = 0;
      RequestRedraw(m_InputWinId);
    }
  }
}

void UiCommon::ObfuscateChatNames()
{
  std::vector<std::string> dummyNames =
  {
    "Alice", "Bob", "Chuck", "Dave", "Eve", "Frank", "Grace", "Heidi", "Ivan",
    "Judy", "Karl", "Lars", "Mallory", "Niaj", "Olivia", "Pat", "Quentin",
    "Rupert", "Sybil", "Trent", "Ulf", "Victor", "Walter", "Xavier", "Yuki",
    "Zeke",
  };
  std::lock_guard<std::mutex> lock(m_Lock);
  int i = 0;
  for (auto& chat : m_Chats)
  {
    if (chat.first != m_CurrentChat)
    {
      chat.second.m_Name = dummyNames[i % dummyNames.size()];
    }
    ++i;
  }

  RequestRedraw(m_ContactWinId | m_InputWinId);
}
