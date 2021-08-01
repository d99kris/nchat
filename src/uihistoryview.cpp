// uihistoryview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihistoryview.h"

#include "fileutil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"

UiHistoryView::UiHistoryView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
  if (m_Enabled)
  {
    int hpad = (m_X == 0) ? 0 : 1;
    int vpad = 1;
    int paddedY = m_Y + vpad;
    int paddedX = m_X + hpad;
    m_PaddedH = m_H - (vpad * 2);
    m_PaddedW = m_W - (hpad * 2);
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
    static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
    werase(m_Win);
    wbkgd(m_Win, attributeTextNormal | colorPairTextRecv | ' ');
    wrefresh(m_Win);
  }
}

UiHistoryView::~UiHistoryView()
{
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiHistoryView::Draw()
{
  std::unique_lock<std::mutex> lock(m_ViewMutex);

  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  static int colorPairTextSent = UiColorConfig::GetColorPair("history_text_sent_color");
  static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
  static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
  static int attributeTextSelected = UiColorConfig::GetAttribute("history_text_attr_selected");

  static int colorPairNameSent = UiColorConfig::GetColorPair("history_name_sent_color");
  static int colorPairNameRecv = UiColorConfig::GetColorPair("history_name_recv_color");
  static int attributeNameNormal = UiColorConfig::GetAttribute("history_name_attr");
  static int attributeNameSelected = UiColorConfig::GetAttribute("history_name_attr_selected");

  std::pair<std::string, std::string>& currentChat = m_Model->GetCurrentChat();
  const bool emojiEnabled = m_Model->GetEmojiEnabled();

  std::vector<std::string>& messageVec =
    m_Model->GetMessageVec(currentChat.first, currentChat.second);
  std::unordered_map<std::string, ChatMessage>& messages =
    m_Model->GetMessages(currentChat.first, currentChat.second);
  int& messageOffset = m_Model->GetMessageOffset(currentChat.first, currentChat.second);

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attributeTextNormal | colorPairTextRecv | ' ');

  m_HistoryShowCount = 0;

  bool firstMessage = true;
  int y = m_PaddedH - 1;
  for (auto it = std::next(messageVec.begin(), messageOffset); it != messageVec.end(); ++it)
  {
    bool isSelectedMessage = firstMessage && m_Model->GetSelectMessage();
    firstMessage = false;

    ChatMessage& msg = messages[*it];

    int colorPairText = msg.isOutgoing ? colorPairTextSent : colorPairTextRecv;
    int attributeText = isSelectedMessage ? attributeTextSelected : attributeTextNormal;

    wattron(m_PaddedWin, attributeText | colorPairText);
    std::vector<std::wstring> wlines;
    if (!msg.text.empty())
    {
      std::string text = msg.text;
      if (!emojiEnabled)
      {
        text = StrUtil::Textize(text);
      }
      
      wlines = StrUtil::WordWrap(StrUtil::ToWString(text), m_PaddedW, false, false, false, 2);
    }

    if (!msg.quotedId.empty())
    {
      std::string quotedText;
      auto quotedIt = messages.find(msg.quotedId);
      if (quotedIt != messages.end())
      {
        quotedText = quotedIt->second.text;
        if (!emojiEnabled)
        {
          quotedText = StrUtil::Textize(quotedText);
        }
      }
      else
      {
        quotedText = "";
      }

      int maxQuoteLen = m_PaddedW - 3;
      std::wstring quote = L"> " + StrUtil::ToWString(quotedText);
      if (StrUtil::WStringWidth(quote) > maxQuoteLen)
      {
        quote = StrUtil::TrimPadWString(quote, maxQuoteLen) + L"...";
      }

      wlines.insert(wlines.begin(), quote);
    }

    if (!msg.filePath.empty())
    {
      std::wstring fileName = StrUtil::ToWString("\xF0\x9F\x93\x8E " + FileUtil::BaseName(msg.filePath));
      wlines.insert(wlines.begin(), fileName);
    }

    const int maxMessageLines = (m_PaddedH - 1);
    if ((int)wlines.size() > maxMessageLines)
    {
      wlines.resize(maxMessageLines - 1);
      wlines.push_back(L"[...]");
    }

    for (auto wline = wlines.rbegin(); wline != wlines.rend(); ++wline)
    {
      std::wstring wdisp = StrUtil::TrimPadWString(*wline, m_PaddedW);
      mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

      if (--y < 0) break;
    }
    wattroff(m_PaddedWin, attributeText | colorPairText);
    if (y < 0) break;

    int colorPairName = msg.isOutgoing ? colorPairNameSent : colorPairNameRecv;
    int attributeName = isSelectedMessage ? attributeNameSelected : attributeNameNormal;

    wattron(m_PaddedWin, attributeName | colorPairName);
    std::string name = m_Model->GetContactName(currentChat.first, msg.senderId);
    if (!emojiEnabled)
    {
      name = StrUtil::Textize(name);
    }
    
    std::wstring wsender = StrUtil::ToWString(name);
    std::wstring wtime = StrUtil::ToWString(GetTimeString(msg.timeSent));
    std::string receipt = msg.isRead ? "\xe2\x9c\x93" : "";
    std::wstring wreceipt = StrUtil::ToWString(receipt);
    std::wstring wheader = wsender + L" (" + wtime + L") " + wreceipt;

    std::wstring wdisp = StrUtil::TrimPadWString(wheader, m_PaddedW);
    mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

    wattroff(m_PaddedWin, attributeName | colorPairName);

    ++m_HistoryShowCount;
    if (!msg.isOutgoing && !msg.isRead)
    {
      m_Model->MarkRead(currentChat.first, currentChat.second, *it);
    }

    if (--y < 0) break;

    if (--y < 0) break;
  }

  wrefresh(m_PaddedWin);
}

int UiHistoryView::GetHistoryShowCount()
{
  return m_HistoryShowCount;
}

std::string UiHistoryView::GetTimeString(int64_t p_TimeSent)
{
  time_t rawtime = (time_t)(p_TimeSent / 1000);
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

  std::string timestr = (sentdate == nowdate) ? senttime : sentdate + std::string(" ") + senttime;

  return timestr;
}
