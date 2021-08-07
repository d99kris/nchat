// uilistdialog.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilistdialog.h"

#include "numutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uicolorconfig.h"
#include "uicontroller.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uiview.h"

UiListDialog::UiListDialog(const UiDialogParams& p_Params)
  : UiDialog(p_Params)
{
  m_Model->SetListDialogActive(true);
  m_View->Draw();
}

UiListDialog::~UiListDialog()
{
  m_Model->SetListDialogActive(false);
}

bool UiListDialog::Run()
{
  Draw();
  int64_t lastTimerEvent = 0;
  while (m_Running)
  {
    wint_t key = UiController::GetKey(50);
    if (key != 0)
    {
      KeyHandler(key);
    }

    int64_t nowTime = TimeUtil::GetCurrentTimeMSec();
    if ((nowTime - lastTimerEvent) > 1000)
    {
      lastTimerEvent = nowTime;
      if (OnTimer())
      {
        Draw();
      }
    }
  }
  return m_Result;
}

void UiListDialog::KeyHandler(wint_t p_Key)
{
  static wint_t keyCancel = UiKeyConfig::GetKey("cancel");
  static wint_t keyQuit = UiKeyConfig::GetKey("quit");
  static wint_t keyOtherCommandsHelp = UiKeyConfig::GetKey("other_commands_help");

  static wint_t keyLeft = UiKeyConfig::GetKey("left");
  static wint_t keyRight = UiKeyConfig::GetKey("right");
  static wint_t keyReturn = UiKeyConfig::GetKey("return");
  static wint_t keyPrevPage = UiKeyConfig::GetKey("prev_page");
  static wint_t keyNextPage = UiKeyConfig::GetKey("next_page");
  static wint_t keyDown = UiKeyConfig::GetKey("down");
  static wint_t keyUp = UiKeyConfig::GetKey("up");
  static wint_t keyEnd = UiKeyConfig::GetKey("end");
  static wint_t keyHome = UiKeyConfig::GetKey("home");
  static wint_t keyBackspace = UiKeyConfig::GetKey("backspace");

  bool isDirty = true;
  if (p_Key == KEY_RESIZE)
  {
    Cleanup();
    m_Model->SetHelpOffset(0);
    m_View->Init();
    m_View->Draw();
    curs_set(0);
    Init();
    UpdateList();
  }
  else if (p_Key == keyCancel)
  {
    m_Result = false;
    m_Running = false;
  }
  else if (p_Key == keyQuit)
  {
    m_Result = false;
    m_Running = false;
  }
  else if ((p_Key == keyRight) || (p_Key == keyReturn))
  {
    OnSelect();
  }
  else if (p_Key == keyOtherCommandsHelp)
  {
    m_Model->SetHelpOffset(m_Model->GetHelpOffset() + 1);
    m_View->Draw();
    curs_set(0);
  }
  else if (p_Key == keyLeft)
  {
    OnBack();
  }
  else if (p_Key == keyPrevPage)
  {
    m_Index -= m_H;
  }
  else if (p_Key == keyNextPage)
  {
    m_Index += m_H;
  }
  else if (p_Key == keyUp)
  {
    m_Index -= 1;
  }
  else if (p_Key == keyDown)
  {
    m_Index += 1;
  }
  else if (p_Key == keyHome)
  {
    m_Index = 0;
  }
  else if (p_Key == keyEnd)
  {
    m_Index = std::numeric_limits<int>::max();
  }
  else if (p_Key == keyBackspace)
  {
    if (m_FilterStr.size() > 0)
    {
      m_FilterStr.pop_back();
      UpdateList();
    }
  }
  else if (StrUtil::IsValidTextKey(p_Key))
  {
    m_FilterStr += p_Key;
    UpdateList();
  }
  else
  {
    isDirty = false;
  }

  m_Index = NumUtil::Bound(0, m_Index, std::max((int)m_Items.size() - 1, 0));

  if (isDirty)
  {
    Draw();
  }
}

void UiListDialog::Draw()
{
  static int colorPair = UiColorConfig::GetColorPair("dialog_color");
  static int attribute = UiColorConfig::GetAttribute("dialog_attr");
  static int attributeSelected = UiColorConfig::GetAttribute("dialog_attr_selected");

  werase(m_Win);
  wbkgd(m_Win, colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  int offset = NumUtil::Bound(0, (m_Index - (m_H / 2)), std::max(0, ((int)m_Items.size() - m_H)));
  for (int i = offset; i < std::min((offset + m_H), (int)m_Items.size()); ++i)
  {
    if (i == m_Index)
    {
      wattroff(m_Win, attribute);
      wattron(m_Win, attributeSelected);
    }

    const std::wstring& wdisp = m_Items.at(i);
    mvwaddnwstr(m_Win, (i - offset), 0, wdisp.c_str(), std::min((int)wdisp.size(), m_W));

    if (i == m_Index)
    {
      wattroff(m_Win, attributeSelected);
      wattron(m_Win, attribute);
    }
  }

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
