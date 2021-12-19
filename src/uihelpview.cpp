// uihelpview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uihelpview.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uikeyconfig.h"
#include "uimodel.h"

UiHelpView::UiHelpView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiHelpView::Draw()
{
  if (!m_Enabled || !m_Dirty) return;
  m_Dirty = false;

  static std::wstring otherHelpItem = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem(UiKeyConfig::GetKey("other_commands_help"), "OtherCmd", helpItems);
    return !helpItems.empty() ? L" | " + helpItems.at(0) : std::wstring();
  }();

  static std::vector<std::wstring> listDialogHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem(UiKeyConfig::GetKey("cancel"), "Cancel", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("return"), "Select", helpItems);
    AppendHelpItem('a', "AddFiltr", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("backspace"), "DelFiltr", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> messageDialogHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem(UiKeyConfig::GetKey("cancel"), "Cancel", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("return"), "OK", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> selectHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem(UiKeyConfig::GetKey("send_msg"), "ReplyMsg", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("next_chat"), "NextChat", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("unread_chat"), "JumpUnrd", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("quit"), "Quit", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("select_emoji"), "AddEmoji", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("select_contact"), "AddrBook", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("transfer"), "SendFile", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("up"), "PrevMsg", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("down"), "NextMsg", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("delete_msg"), "DelMsg", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("open"), "OpenFile", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("save"), "SaveFile", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("open_link"), "OpenLink", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("toggle_emoji"), "TgEmoji", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_list"), "TgList", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_top"), "TgTop", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_help"), "TgHelp", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> defaultHelpItems = []()
  {
    std::vector<std::wstring> helpItems;
    AppendHelpItem(UiKeyConfig::GetKey("send_msg"), "SendMsg", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("next_chat"), "NextChat", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("unread_chat"), "JumpUnrd", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("quit"), "Quit", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("select_emoji"), "AddEmoji", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("select_contact"), "AddrBook", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("transfer"), "SendFile", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("up"), "SelectMsg", helpItems);

    AppendHelpItem(UiKeyConfig::GetKey("toggle_emoji"), "TgEmoji", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_list"), "TgList", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_top"), "TgTop", helpItems);
    AppendHelpItem(UiKeyConfig::GetKey("toggle_help"), "TgHelp", helpItems);
    return helpItems;
  }();

  static std::vector<std::wstring> listDialogHelpViews;
  static std::vector<std::wstring> messageDialogHelpViews;
  static std::vector<std::wstring> selectHelpViews;
  static std::vector<std::wstring> defaultHelpViews;

  static int prevW = 0;
  if (m_W != prevW)
  {
    prevW = m_W;

    const int maxW = m_W - 2;
    listDialogHelpViews = GetHelpViews(maxW, listDialogHelpItems, otherHelpItem);
    messageDialogHelpViews = GetHelpViews(maxW, messageDialogHelpItems, otherHelpItem);
    selectHelpViews = GetHelpViews(maxW, selectHelpItems, otherHelpItem);
    defaultHelpViews = GetHelpViews(maxW, defaultHelpItems, otherHelpItem);
  }

  static int colorPair = UiColorConfig::GetColorPair("help_color");
  static int attribute = UiColorConfig::GetAttribute("help_attr");

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::wstring wstr;
  if (m_Model->GetListDialogActive())
  {
    wstr = listDialogHelpViews.at(m_Model->GetHelpOffset() % listDialogHelpViews.size());
  }
  else if (m_Model->GetMessageDialogActive())
  {
    wstr = messageDialogHelpViews.at(m_Model->GetHelpOffset() % messageDialogHelpViews.size());
  }
  else if (m_Model->GetSelectMessage())
  {
    wstr = selectHelpViews.at(m_Model->GetHelpOffset() % selectHelpViews.size());
  }
  else
  {
    wstr = defaultHelpViews.at(m_Model->GetHelpOffset() % defaultHelpViews.size());
  }

  wstr = L" " + wstr + std::wstring(std::max(m_W - (int)wstr.size(), 0), L' ');
  mvwaddnwstr(m_Win, 0, 0, wstr.c_str(), std::min((int)wstr.size(), m_W));

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}

std::vector<std::wstring> UiHelpView::GetHelpViews(const int p_MaxW, const std::vector<std::wstring>& p_HelpItems,
                                                   const std::wstring& p_OtherHelpItem)
{
  std::vector<std::wstring> helpViews;
  std::wstring helpView = StrUtil::Join(p_HelpItems, L" | ");
  if (StrUtil::WStringWidth(helpView) <= p_MaxW)
  {
    helpViews.push_back(helpView);
  }
  else
  {
    helpView.clear();
    for (int i = 0; i < (int)p_HelpItems.size(); ++i)
    {
      if (helpView.empty())
      {
        helpView = p_HelpItems.at(i);
      }
      else
      {
        if (StrUtil::WStringWidth(helpView + L" | " + p_HelpItems.at(i) + p_OtherHelpItem) < p_MaxW)
        {
          helpView += L" | " + p_HelpItems.at(i);
        }
        else
        {
          helpView += p_OtherHelpItem;
          helpViews.push_back(helpView);
          helpView = p_HelpItems.at(i);
        }
      }
    }

    if (!helpView.empty())
    {
      helpView += p_OtherHelpItem;
      helpViews.push_back(helpView);
    }
  }

  return helpViews;
}

void UiHelpView::AppendHelpItem(const int p_Key, const std::string& p_Desc, std::vector<std::wstring>& p_HelpItems)
{
  const std::string keyDisplay = GetKeyDisplay(p_Key);
  if (!keyDisplay.empty())
  {
    const std::string helpItem = keyDisplay + " " + p_Desc;
    p_HelpItems.push_back(StrUtil::ToWString(helpItem));
  }
}

std::string UiHelpView::GetKeyDisplay(int p_Key)
{
  if (p_Key == '\n')
  {
    return "\xe2\x8f\x8e";
  }
  else if (p_Key == KEY_TAB)
  {
    return "Tab";
  }
  else if (p_Key == KEY_BTAB)
  {
    return "STab";
  }
  else if (p_Key == KEY_UP)
  {
    return "\xe2\x86\x91";
  }
  else if (p_Key == KEY_DOWN)
  {
    return "\xe2\x86\x93";
  }
  else if (p_Key == KEY_LEFT)
  {
    return "\xe2\x86\x90";
  }
  else if (p_Key == KEY_RIGHT)
  {
    return "\xe2\x86\x92";
  }
  else if (p_Key == KEY_BACKSPACE)
  {
    return "\xe2\x8c\xab";
  }
  else if (p_Key == 'a')
  {
    return "abc";
  }
  else if ((p_Key >= 0x0) && (p_Key <= 0x1F))
  {
    return "^" + std::string(1, (char)p_Key + 0x40);
  }

  return "";
}
