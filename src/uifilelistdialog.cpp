// uifilelistdialog.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uifilelistdialog.h"

#include <algorithm>

#include "fileutil.h"
#include "strutil.h"
#include "uiconfig.h"
#include "uimodel.h"

UiFileListDialog::UiFileListDialog(const UiDialogParams& p_Params, const std::string& p_CurrentDir)
  : UiListDialog(p_Params, true /*p_ShadeHidden*/)
  , m_CurrentDir(p_CurrentDir)
{
  m_Model->SetFileListDialogActive(true);
  std::set<DirEntry, DirEntryCompare> dirEntrys = FileUtil::ListPaths(m_CurrentDir);
  m_DirEntrys = std::vector<DirEntry>(dirEntrys.begin(), dirEntrys.end());
  SortDirEntrys();
  UpdateList();
}

UiFileListDialog::~UiFileListDialog()
{
  m_Model->SetFileListDialogActive(false);
}

std::string UiFileListDialog::GetCurrentDir()
{
  return m_CurrentDir;
}

std::string UiFileListDialog::GetSelectedPath()
{
  return m_SelectedPath;
}

void UiFileListDialog::OnSelect()
{
  if (m_CurrentDirEntrys.size() == 0) return;

  if (m_Index < (int)m_CurrentDirEntrys.size())
  {
    const DirEntry& dirEntry = m_CurrentDirEntrys[m_Index];
    if (dirEntry.IsDir())
    {
      m_CurrentDir = FileUtil::AbsolutePath(m_CurrentDir + "/" + dirEntry.name);
      std::set<DirEntry, DirEntryCompare> dirEntrys = FileUtil::ListPaths(m_CurrentDir);
      m_DirEntrys = std::vector<DirEntry>(dirEntrys.begin(), dirEntrys.end());
      SortDirEntrys();
      m_FilterStr.clear();
      UpdateList();
      UpdateFooter();
    }
    else
    {
      m_SelectedPath = FileUtil::AbsolutePath(m_CurrentDir + "/" + dirEntry.name);
      m_Result = true;
      m_Running = false;
    }
  }
}

void UiFileListDialog::OnBack()
{
  m_CurrentDir = FileUtil::AbsolutePath(m_CurrentDir + "/..");
  std::set<DirEntry, DirEntryCompare> dirEntrys = FileUtil::ListPaths(m_CurrentDir);
  m_DirEntrys = std::vector<DirEntry>(dirEntrys.begin(), dirEntrys.end());
  SortDirEntrys();
  m_FilterStr.clear();
  UpdateList();
  UpdateFooter();
}

bool UiFileListDialog::OnTimer()
{
  return false;
}

void UiFileListDialog::SortDirEntrys()
{
  int sortMode = UiConfig::GetNum("file_picker_sort_mode");
  
  if (sortMode == 0)
  {
    // Default: dirs first, then by name (already sorted by ListPaths)
    return;
  }
  else if (sortMode == 1)
  {
    // Dirs first, then by mtime (newest first)
    std::sort(m_DirEntrys.begin(), m_DirEntrys.end(), 
      [](const DirEntry& a, const DirEntry& b) {
        if (a.IsHidden() != b.IsHidden())
          return a.IsHidden() < b.IsHidden();
        else if (a.IsDir() != b.IsDir())
          return a.IsDir() > b.IsDir();
        else if (a.mtime != b.mtime)
          return a.mtime > b.mtime; // newest first
        else
          return a.name < b.name;
      });
  }
  else if (sortMode == 2)
  {
    // All by mtime (newest first), dirs and files mixed
    std::sort(m_DirEntrys.begin(), m_DirEntrys.end(), 
      [](const DirEntry& a, const DirEntry& b) {
        if (a.IsHidden() != b.IsHidden())
          return a.IsHidden() < b.IsHidden();
        else if (a.mtime != b.mtime)
          return a.mtime > b.mtime; // newest first
        else
          return a.name < b.name;
      });
  }
}

void UiFileListDialog::UpdateList()
{
  if (m_FilterStr.empty())
  {
    m_CurrentDirEntrys = m_DirEntrys;
  }
  else
  {
    m_CurrentDirEntrys.clear();
    for (const auto& dirEntry : m_DirEntrys)
    {
      if (StrUtil::ToLower(dirEntry.name).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos)
      {
        m_CurrentDirEntrys.push_back(dirEntry);
      }
    }
  }

  int maxNameLen = m_W - 9;
  m_Items.clear();
  for (auto& dirEntry : m_CurrentDirEntrys)
  {
    std::wstring name = StrUtil::ToWString(dirEntry.name);
    name = StrUtil::TrimPadWString(name.substr(0, maxNameLen), maxNameLen);

    // add 9 chars for dir / filesize
    if (dirEntry.IsDir())
    {
      name += L"    (dir)";
    }
    else
    {
      // max 7 - ex: "1234 KB"
      std::string size = FileUtil::GetSuffixedSize(dirEntry.size);
      size = std::string(7 - size.size(), ' ') + size;
      name += L"  " + StrUtil::ToWString(size);
    }

    m_Items.push_back(name);
  }

  m_Index = 0;
}
