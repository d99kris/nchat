// uimessagedialog.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uimessagedialog.h"

#include "numutil.h"
#include "strutil.h"
#include "timeutil.h"
#include "uicolorconfig.h"
#include "uicontroller.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uiview.h"

UiMessageDialog::UiMessageDialog(const UiDialogParams& p_Params, const std::string& p_Message)
  : UiDialog(p_Params)
  , m_Message(p_Message)
{
  m_Model->SetMessageDialogActive(true);
  m_View->Draw();
  curs_set(0);
}

UiMessageDialog::~UiMessageDialog()
{
  m_Model->SetMessageDialogActive(false);
}

bool UiMessageDialog::Run()
{
  Draw();
  while (m_Running)
  {
    wint_t key = UiController::GetKey(50);
    if (key != 0)
    {
      KeyHandler(key);
    }
  }
  return m_Result;
}

void UiMessageDialog::KeyHandler(wint_t p_Key)
{
  static wint_t keyCancel = UiKeyConfig::GetKey("cancel");
  static wint_t keyQuit = UiKeyConfig::GetKey("quit");
  static wint_t keyOtherCommandsHelp = UiKeyConfig::GetKey("other_commands_help");
  static wint_t keyReturn = UiKeyConfig::GetKey("return");
  static wint_t keyTerminalFocusIn = UiKeyConfig::GetKey("terminal_focus_in");
  static wint_t keyTerminalFocusOut = UiKeyConfig::GetKey("terminal_focus_out");

  bool isDirty = true;
  if (p_Key == KEY_RESIZE)
  {
    Cleanup();
    m_Model->SetHelpOffset(0);
    m_View->Init();
    m_View->Draw();
    curs_set(0);
    Init();
  }
  else if (p_Key == keyTerminalFocusIn)
  {
    m_Model->SetTerminalActive(true);
  }
  else if (p_Key == keyTerminalFocusOut)
  {
    m_Model->SetTerminalActive(false);
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
  else if (p_Key == keyReturn)
  {
    m_Result = true;
    m_Running = false;
  }
  else if (p_Key == keyOtherCommandsHelp)
  {
    m_Model->SetHelpOffset(m_Model->GetHelpOffset() + 1);
    m_View->Draw();
    curs_set(0);
  }
  else
  {
    isDirty = false;
  }

  if (isDirty)
  {
    Draw();
  }
}

void UiMessageDialog::Draw()
{
  static int colorPair = UiColorConfig::GetColorPair("dialog_color");
  static int attribute = UiColorConfig::GetAttribute("dialog_attr");

  werase(m_Win);
  wbkgd(m_Win, colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  std::vector<std::wstring> wlines;
  wlines = StrUtil::WordWrap(StrUtil::ToWString(m_Message), m_W, false, false, false, 2);

  for (int i = 0; i < std::min(m_H - 1, (int)wlines.size()); ++i)
  {
    const std::wstring& wdisp = wlines.at(i);
    int w = std::min((int)wdisp.size(), m_W);
    int x = std::max(0, ((m_W - w) / 2));
    mvwaddnwstr(m_Win, i + 1, x, wdisp.c_str(), w);
  }

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);
}
