// uifilelistdialog.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uifilelistdialog.h"

#include "fileutil.h"
#include "strutil.h"

UiFileListDialog::UiFileListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params, true /*p_ShadeHidden*/)
{
  m_CurrentDir = FileUtil::GetCurrentWorkingDir();
  m_DirEntrys = FileUtil::ListPaths(m_CurrentDir);
  UpdateList();
}

UiFileListDialog::~UiFileListDialog()
{
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
    const DirEntry& dirEntry = *std::next(m_CurrentDirEntrys.begin(), m_Index);
    if (dirEntry.IsDir())
    {
      m_CurrentDir = FileUtil::AbsolutePath(m_CurrentDir + "/" + dirEntry.name);
      m_DirEntrys = FileUtil::ListPaths(m_CurrentDir);
      m_FilterStr.clear();
      UpdateList();
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
  m_DirEntrys = FileUtil::ListPaths(m_CurrentDir);
  m_FilterStr.clear();
  UpdateList();
}

bool UiFileListDialog::OnTimer()
{
  return false;
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
        m_CurrentDirEntrys.insert(dirEntry);
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
