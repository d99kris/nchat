// uilistdialog.h
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include "uidialog.h"

class UiListDialog : public UiDialog
{
public:
  UiListDialog(const UiDialogParams& p_Params, bool p_ShadeHidden);
  virtual ~UiListDialog();

  bool Run();
  void KeyHandler(wint_t p_Key);

protected:
  virtual void OnSelect() = 0;
  virtual void OnBack() = 0;
  virtual bool OnTimer() = 0;
  virtual void UpdateList() = 0;

  void UpdateFooter();

private:
  void Draw();

protected:
  bool m_ShadeHidden = false;
  bool m_Running = true;
  bool m_Result = false;
  std::wstring m_FilterStr;
  std::vector<std::wstring> m_Items;
  int m_Index = 0;
  int m_MaxW = 0;
};
