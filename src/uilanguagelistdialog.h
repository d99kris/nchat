// uilanguagelistdialog.h
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uilistdialog.h"

#include <string>
#include <vector>

class UiLanguageListDialog : public UiListDialog
{
public:
  UiLanguageListDialog(const UiDialogParams& p_Params, const std::string& p_CurrentLanguage = "");
  virtual ~UiLanguageListDialog();

  std::string GetSelectedLanguage();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();

private:
  struct LanguageOption
  {
    std::string code;
    std::string name;
  };

  std::vector<LanguageOption> m_Languages;
  std::vector<int> m_FilteredIndices; // Indices into m_Languages after filtering
  std::string m_SelectedLanguage;
  std::string m_CurrentLanguage;
};
