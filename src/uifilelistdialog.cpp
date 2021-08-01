// uifilelistdialog.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uifilelistdialog.h"

#include "fileutil.h"
#include "strutil.h"

UiFileListDialog::UiFileListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params)
{
  m_CurrentDir = FileUtil::GetCurrentWorkingDir();
  m_FileInfos = FileUtil::ListPaths(m_CurrentDir);
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
  if (m_CurrentFileInfos.size() == 0) return;

  if (m_Index < (int)m_CurrentFileInfos.size())
  {
    const FileInfo& fileInfo = *std::next(m_CurrentFileInfos.begin(), m_Index);
    if (fileInfo.IsDir())
    {
      m_CurrentDir = FileUtil::AbsolutePath(m_CurrentDir + "/" + fileInfo.name);
      m_FileInfos = FileUtil::ListPaths(m_CurrentDir);
      m_FilterStr.clear();
      UpdateList();
    }
    else
    {
      m_SelectedPath = FileUtil::AbsolutePath(m_CurrentDir + "/" + fileInfo.name);
      m_Result = true;
      m_Running = false;
    }
  }
}

void UiFileListDialog::OnBack()
{
  m_CurrentDir = FileUtil::AbsolutePath(m_CurrentDir + "/..");
  m_FileInfos = FileUtil::ListPaths(m_CurrentDir);
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
    m_CurrentFileInfos = m_FileInfos;
  }
  else
  {
    m_CurrentFileInfos.clear();
    for (const auto& fileInfo : m_FileInfos)
    {
      if (StrUtil::ToLower(fileInfo.name).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos)
      {
        m_CurrentFileInfos.insert(fileInfo);
      }
    }
  }

  int maxNameLen = m_W - 9;
  m_Items.clear();
  for (auto& fileInfo : m_CurrentFileInfos)
  {
    std::wstring name = StrUtil::ToWString(fileInfo.name);
    name = StrUtil::TrimPadWString(name.substr(0, maxNameLen), maxNameLen);

    // add 9 chars for dir / filesize
    if (fileInfo.IsDir())
    {
      name += L"    (dir)";
    }
    else
    {
      // max 7 - ex: "1234 KB"
      std::string size = FileUtil::GetSuffixedSize(fileInfo.size);
      size = std::string(7 - size.size(), ' ') + size;
      name += L"  " + StrUtil::ToWString(size);
    }

    m_Items.push_back(name);
  }

  m_Index = 0;
}
