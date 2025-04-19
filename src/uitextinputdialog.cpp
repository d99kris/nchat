// uitextinputdialog.cpp
//
// Copyright (c) 2024-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uitextinputdialog.h"

#include "numutil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uicontroller.h"
#include "uikeyconfig.h"
#include "uimodel.h"
#include "uiview.h"

UiTextInputDialog::UiTextInputDialog(const UiDialogParams& p_Params, const std::string& p_Message,
                                     const std::string& p_EntryStr)
  : UiDialog(p_Params)
  , m_Message(p_Message)
  , m_EntryStr(StrUtil::ToWString(p_EntryStr))
{
  m_EntryPos = m_EntryStr.size();
  m_Model->SetMessageDialogActive(true);
  m_Model->Draw();
  curs_set(0);
}

UiTextInputDialog::~UiTextInputDialog()
{
  m_Model->SetMessageDialogActive(false);
}

bool UiTextInputDialog::Run()
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

void UiTextInputDialog::KeyHandler(wint_t p_Key)
{
  static wint_t keyCancel = UiKeyConfig::GetKey("cancel");
  static wint_t keyQuit = UiKeyConfig::GetKey("quit");
  static wint_t keyOtherCommandsHelp = UiKeyConfig::GetKey("other_commands_help");
  static wint_t keyOk = UiKeyConfig::GetKey("ok");
  static wint_t keyTerminalFocusIn = UiKeyConfig::GetKey("terminal_focus_in");
  static wint_t keyTerminalFocusOut = UiKeyConfig::GetKey("terminal_focus_out");
  static wint_t keyTerminalResize = UiKeyConfig::GetKey("terminal_resize");

  bool isDirty = true;
  if (p_Key == keyTerminalResize)
  {
    Cleanup();
    m_Model->SetHelpOffset(0);
    m_Model->ReinitView();
    m_Model->Draw();
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
  else if (p_Key == keyOk)
  {
    m_Result = true;
    m_Running = false;
  }
  else if (p_Key == keyOtherCommandsHelp)
  {
    m_Model->SetHelpOffset(m_Model->GetHelpOffset() + 1);
    m_Model->Draw();
  }
  else
  {
    EntryKeyHandler(p_Key);
  }

  if (isDirty)
  {
    Draw();
  }
}

std::string UiTextInputDialog::GetInput()
{
  return StrUtil::ToString(m_EntryStr);
}

void UiTextInputDialog::Draw()
{
  static int colorPair = UiColorConfig::GetColorPair("dialog_color");
  static int attribute = UiColorConfig::GetAttribute("dialog_attr");

  curs_set(0);

  werase(m_Win);
  wbkgd(m_Win, colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  const int promptWidth = StrUtil::WStringWidth(StrUtil::ToWString(m_Message)) + 1;
  const int maxEntryDisplay = m_W - StrUtil::WStringWidth(StrUtil::ToWString(m_Message)) - 2;

  int offset = 0;
  std::wstring wdisp = StrUtil::ToWString(m_Message);
  if (m_EntryPos < maxEntryDisplay)
  {
    wdisp += m_EntryStr;
  }
  else
  {
    offset = m_EntryPos - maxEntryDisplay + 1;
    wdisp += m_EntryStr.substr(offset, maxEntryDisplay + 0);
  }

  int w = std::min((int)wdisp.size(), m_W - 2);
  mvwaddnwstr(m_Win, 1, 1, wdisp.c_str(), w);
  wmove(m_Win, 1, promptWidth + m_EntryPos - offset);

  wattroff(m_Win, attribute | colorPair);
  wrefresh(m_Win);

  curs_set(1);
}

void UiTextInputDialog::EntryKeyHandler(wint_t p_Key)
{
  static wint_t keyLeft = UiKeyConfig::GetKey("left");
  static wint_t keyRight = UiKeyConfig::GetKey("right");
  static wint_t keyBackspace = UiKeyConfig::GetKey("backspace");
  static wint_t keyBackspaceAlt = UiKeyConfig::GetKey("backspace_alt");
  static wint_t keyDelete = UiKeyConfig::GetKey("delete");

  std::wstring& entryStr = m_EntryStr;
  int& entryPos = m_EntryPos;

  if (p_Key == keyLeft)
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
    }
  }
  else if (StrUtil::IsValidTextKey(p_Key))
  {
    entryStr.insert(entryPos++, 1, p_Key);
  }
}
