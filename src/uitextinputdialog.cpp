// uitextinputdialog.cpp
//
// Copyright (c) 2024-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uitextinputdialog.h"

#include <algorithm>

#include "apputil.h"
#include "numutil.h"
#include "strutil.h"
#include "uicolorconfig.h"
#include "uicontroller.h"
#include "uidrawutil.h"
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
  while (m_Running && !AppUtil::IsTerminateRequested())
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

  UiDrawUtil::AddWStr(m_Win, 1, 1, wdisp, m_W - 2);

  // cursor column is the display width of the text before it
  std::wstring prefix = m_EntryStr.substr(offset, std::max(m_EntryPos - offset, 0));
  prefix.erase(std::remove(prefix.begin(), prefix.end(), EMOJI_PAD), prefix.end());
  const int cursX = promptWidth + StrUtil::WStringWidth(prefix);

  wmove(m_Win, 1, cursX);

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
    entryPos = StrUtil::PrevCursorPos(entryStr, entryPos);
  }
  else if (p_Key == keyRight)
  {
    entryPos = StrUtil::NextCursorPos(entryStr, entryPos);
  }
  else if ((p_Key == keyBackspace) || (p_Key == keyBackspaceAlt))
  {
    if (entryPos > 0)
    {
      const int prevPos = StrUtil::PrevCursorPos(entryStr, entryPos);
      entryStr.erase(prevPos, entryPos - prevPos);
      entryPos = prevPos;
    }
  }
  else if (p_Key == keyDelete)
  {
    if (entryPos < (int)entryStr.size())
    {
      const int nextPos = StrUtil::NextCursorPos(entryStr, entryPos);
      entryStr.erase(entryPos, nextPos - entryPos);
    }
  }
  else if (StrUtil::IsValidTextKey(p_Key))
  {
    entryStr.insert(entryPos++, 1, p_Key);
  }
}
