// uitextinputdialog.h
//
// Copyright (c) 2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include "uidialog.h"

class UiTextInputDialog : public UiDialog
{
public:
  UiTextInputDialog(const UiDialogParams& p_Params, const std::string& p_Message, const std::string& p_EntryStr);
  virtual ~UiTextInputDialog();

  bool Run();
  void KeyHandler(wint_t p_Key);
  std::string GetInput();

private:
  void Draw();
  void EntryKeyHandler(wint_t p_Key);

protected:
  bool m_Running = true;
  bool m_Result = false;
  std::string m_Message;
  std::wstring m_EntryStr;
  int m_EntryPos = 0;
};
