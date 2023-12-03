// uihistoryview.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihistoryview.h"

#include "appconfig.h"
#include "apputil.h"
#include "fileutil.h"
#include "protocolutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uicolorconfig.h"
#include "uiconfig.h"
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

  curs_set(0);

  static int colorPairTextSent = UiColorConfig::GetColorPair("history_text_sent_color");
  static int colorPairTextRecv = UiColorConfig::GetColorPair("history_text_recv_color");
  static int colorPairTextQuoted = UiColorConfig::GetColorPair("history_text_quoted_color");
  static int colorPairTextAttachment = UiColorConfig::GetColorPair("history_text_attachment_color");
  static int attributeTextNormal = UiColorConfig::GetAttribute("history_text_attr");
  static int attributeTextSelected = UiColorConfig::GetAttribute("history_text_attr_selected");

  static int colorPairNameSent = UiColorConfig::GetColorPair("history_name_sent_color");
  static int colorPairNameRecv = UiColorConfig::GetColorPair("history_name_recv_color");
  static int attributeNameNormal = UiColorConfig::GetAttribute("history_name_attr");
  static int attributeNameSelected = UiColorConfig::GetAttribute("history_name_attr_selected");

  static std::wstring attachmentIndicator =
    StrUtil::ToWString(UiConfig::GetStr("attachment_indicator") + " ");
  static std::wstring quoteIndicator = L"> ";

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
    bool isSelectedMessage = firstMessage && m_Model->GetSelectMessageActive();
    firstMessage = false;

    ChatMessage& msg = messages[*it];

    int attributeText = isSelectedMessage ? attributeTextSelected : attributeTextNormal;
    int colorPairText = [&]()
    {
      if (msg.isOutgoing) return colorPairTextSent;

      if (msg.senderId == currentChat.second) return colorPairTextRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_text_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_text_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_text_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

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
        if (!quotedIt->second.text.empty())
        {
          quotedText = StrUtil::Split(quotedIt->second.text, '\n').at(0);
          if (!emojiEnabled)
          {
            quotedText = StrUtil::Textize(quotedText);
          }
        }
        else if (!quotedIt->second.fileInfo.empty())
        {
          FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(quotedIt->second.fileInfo);
          quotedText = FileUtil::BaseName(fileInfo.filePath);
        }
      }
      else
      {
        m_Model->FetchCachedMessage(currentChat.first, currentChat.second, msg.quotedId);
        quotedText = "";
      }

      int maxQuoteLen = m_PaddedW - 3;
      std::wstring quote = quoteIndicator + StrUtil::ToWString(quotedText);
      if (StrUtil::WStringWidth(quote) > maxQuoteLen)
      {
        quote = StrUtil::TrimPadWString(quote, maxQuoteLen) + L"...";
      }

      wlines.insert(wlines.begin(), quote);
    }

    if (!msg.fileInfo.empty())
    {
      FileInfo fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);

      // special case handling selection-triggered download, and handling cache's old setting
      static const bool isAttachmentPrefetchAll =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchAll);
      static const bool isAttachmentPrefetchSelected =
        (AppConfig::GetNum("attachment_prefetch") == AttachmentPrefetchSelected);
      if (isAttachmentPrefetchAll || (isSelectedMessage && isAttachmentPrefetchSelected))
      {
        if (!UiModel::IsAttachmentDownloaded(fileInfo) && UiModel::IsAttachmentDownloadable(fileInfo))
        {
          m_Model->DownloadAttachment(currentChat.first, currentChat.second, *it,
                                      fileInfo.fileId, DownloadFileActionNone);
          fileInfo = ProtocolUtil::FileInfoFromHex(msg.fileInfo);
        }
      }

      std::string fileName = FileUtil::BaseName(fileInfo.filePath);
      std::string fileStatus;
      if (fileInfo.fileStatus == FileStatusNone)
      {
        // should not happen
        static const std::string statusNone = " -";
        fileStatus = statusNone;
      }
      else if (fileInfo.fileStatus == FileStatusNotDownloaded)
      {
        static const std::string statusNotDownloaded = " " + UiConfig::GetStr("downloadable_indicator");
        fileStatus = statusNotDownloaded;
      }
      else if (fileInfo.fileStatus == FileStatusDownloaded)
      {
        static const std::string statusDownloaded = "";
        fileStatus = statusDownloaded;
      }
      else if (fileInfo.fileStatus == FileStatusDownloading)
      {
        static const std::string statusDownloading = " " + UiConfig::GetStr("syncing_indicator");
        fileStatus = statusDownloading;
      }
      else if (fileInfo.fileStatus == FileStatusDownloadFailed)
      {
        static const std::string statusDownloadFailed = " " + UiConfig::GetStr("failed_indicator");
        fileStatus = statusDownloadFailed;
      }

      std::wstring fileStr = attachmentIndicator + StrUtil::ToWString(fileName + fileStatus);
      wlines.insert(wlines.begin(), fileStr);
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

      bool isAttachment = (wdisp.rfind(attachmentIndicator, 0) == 0);
      bool isQuote = (wdisp.rfind(quoteIndicator, 0) == 0);

      if (isAttachment)
      {
        wattron(m_PaddedWin, attributeText | colorPairTextAttachment);
      }
      else if (isQuote)
      {
        wattron(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else
      {
        wattron(m_PaddedWin, attributeText | colorPairText);
      }

      mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

      if (isAttachment)
      {
        wattroff(m_PaddedWin, attributeText | colorPairTextAttachment);
      }
      else if (isQuote)
      {
        wattroff(m_PaddedWin, attributeText | colorPairTextQuoted);
      }
      else
      {
        wattroff(m_PaddedWin, attributeText | colorPairText);
      }

      if (--y < 0) break;
    }

    if (y < 0) break;

    int attributeName = isSelectedMessage ? attributeNameSelected : attributeNameNormal;
    int colorPairName = [&]()
    {
      if (msg.isOutgoing) return colorPairNameSent;

      if (msg.senderId == currentChat.second) return colorPairNameRecv;

      static bool isUserColor = UiColorConfig::IsUserColor("history_name_recv_group_color");
      if (!isUserColor)
      {
        static int colorPairGroup = UiColorConfig::GetColorPair("history_name_recv_group_color");
        return colorPairGroup;
      }

      int colorPairGroup = UiColorConfig::GetUserColorPair("history_name_recv_group_color",
                                                           msg.senderId);
      return colorPairGroup;
    }();

    wattron(m_PaddedWin, attributeName | colorPairName);
    std::string name = m_Model->GetContactName(currentChat.first, msg.senderId);
    if (!emojiEnabled)
    {
      name = StrUtil::Textize(name);
    }

    std::wstring wsender = StrUtil::ToWString(name);
    std::wstring wtime;
    if (msg.timeSent != std::numeric_limits<int64_t>::max())
    {
      wtime = L" (" + StrUtil::ToWString(TimeUtil::GetTimeString(msg.timeSent, false /* p_IsExport */)) + L")";
    }

    if (!msg.isOutgoing && !msg.isRead)
    {
      m_Model->MarkRead(currentChat.first, currentChat.second, *it);
    }

    static const std::string readIndicator = " " + UiConfig::GetStr("read_indicator");
    std::wstring wreceipt = StrUtil::ToWString(msg.isRead ? readIndicator : "");
    std::wstring wheader = wsender + wtime + wreceipt;

    static const bool developerMode = AppUtil::GetDeveloperMode();
    if (developerMode)
    {
      wheader = wheader +
        L" msg " + StrUtil::ToWString(msg.id) +
        L" user " + StrUtil::ToWString(msg.senderId);
    }

    std::wstring wdisp = StrUtil::TrimPadWString(wheader, m_PaddedW);
    mvwaddnwstr(m_PaddedWin, y, 0, wdisp.c_str(), std::min((int)wdisp.size(), m_PaddedW));

    wattroff(m_PaddedWin, attributeName | colorPairName);

    ++m_HistoryShowCount;

    if (--y < 0) break;

    if (--y < 0) break;
  }

  wrefresh(m_PaddedWin);
}

int UiHistoryView::GetHistoryShowCount()
{
  return m_HistoryShowCount;
}
