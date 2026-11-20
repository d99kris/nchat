// uifilelistdialog.h
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <set>
#include <vector>

#include "uilistdialog.h"
#include "fileutil.h"

class UiFileListDialog : public UiListDialog
{
public:
  UiFileListDialog(const UiDialogParams& p_Params, const std::string& p_CurrentDir);
  virtual ~UiFileListDialog();

  std::string GetCurrentDir();
  std::string GetSelectedPath();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();
  void SortDirEntrys();

private:
  std::string m_CurrentDir;
  std::vector<DirEntry> m_DirEntrys;
  std::vector<DirEntry> m_CurrentDirEntrys;
  std::string m_SelectedPath;
};
