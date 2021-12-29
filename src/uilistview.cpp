// uilistview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilistview.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"

UiListView::UiListView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
  if (m_Enabled)
  {
    int paddedY = m_Y + 1;
    int paddedX = m_X + 1;
    m_PaddedH = m_H - 2;
    m_PaddedW = m_W - 2;
    m_PaddedWin = newwin(m_PaddedH, m_PaddedW, paddedY, paddedX);

    static int colorPair = UiColorConfig::GetColorPair("list_color");
    static int attribute = UiColorConfig::GetAttribute("list_attr");

    werase(m_Win);
    wbkgd(m_Win, attribute | colorPair | ' ');
    wrefresh(m_Win);
  }
}

UiListView::~UiListView()
{
  if (m_PaddedWin != nullptr)
  {
    delwin(m_PaddedWin);
    m_PaddedWin = nullptr;
  }
}

void UiListView::Draw()
{
  std::unique_lock<std::mutex> lock(m_ViewMutex);

  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  static int colorPair = UiColorConfig::GetColorPair("list_color");
  static int attribute = UiColorConfig::GetAttribute("list_attr");
  static int attributeSelected = UiColorConfig::GetAttribute("list_attr_selected");

  int index = std::max(0, m_Model->GetCurrentChatIndex());
  const std::vector<std::pair<std::string, std::string>>& p_ChatVec = m_Model->GetChatVec();

  const bool emojiEnabled = m_Model->GetEmojiEnabled();
  std::vector<std::string> names;
  std::vector<bool> unreads;
  for (auto& chatPair : p_ChatVec)
  {
    const std::string& name = m_Model->GetContactListName(chatPair.first, chatPair.second);
    bool isUnread = m_Model->GetChatIsUnread(chatPair.first, chatPair.second);
    names.push_back(name);
    unreads.push_back(isUnread);
  }

  werase(m_PaddedWin);
  wbkgd(m_PaddedWin, attribute | colorPair | ' ');
  wattron(m_PaddedWin, attribute | colorPair);

  if (!names.empty())
  {
    int height = m_PaddedH;
    int count = names.size();
    int offset = std::min(std::max(0, index - ((height - 1) / 2)), std::max(0, count - height));
    int last = std::min((height + offset), count);
    for (int i = offset; i < last; ++i)
    {
      if (i == index)
      {
        wattroff(m_PaddedWin, attribute);
        wattron(m_PaddedWin, attributeSelected);
      }

      int y = i - offset;
      std::string name = names[i];
      if (!emojiEnabled)
      {
        name = StrUtil::Textize(name);
      }

      std::wstring wname = StrUtil::ToWString(name).substr(0, m_PaddedW);
      wname = StrUtil::TrimPadWString(wname, m_PaddedW);

      mvwaddnwstr(m_PaddedWin, y, 0, wname.c_str(), wname.size());

      if (unreads[i])
      {
        mvwprintw(m_PaddedWin, y, (m_PaddedW - 2), " *");
      }

      if (i == index)
      {
        wattroff(m_PaddedWin, attributeSelected);
        wattron(m_PaddedWin, attribute);
      }
    }
  }

  wattroff(m_PaddedWin, attribute | colorPair);
  wrefresh(m_PaddedWin);
}
