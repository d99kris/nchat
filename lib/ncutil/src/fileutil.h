// fileutil.h
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <set>
#include <string>

struct DirEntry
{
  DirEntry()
    : name("")
    , size(0)
  {
  }

  DirEntry(const std::string& p_Name, ssize_t p_Size)
    : name(p_Name)
    , size(p_Size)
  {
  }

  inline bool IsDir() const
  {
    return (size == -1);
  }

  inline bool IsHidden() const
  {
    return name.empty() || ((name.at(0) == '.') && (name != ".."));
  }

  std::string name;
  ssize_t size = 0;
};

struct DirEntryCompare
{
  bool operator()(const DirEntry& p_Lhs, const DirEntry& p_Rhs) const
  {
    if (p_Lhs.IsDir() != p_Rhs.IsDir())
    {
      return p_Lhs.IsDir() > p_Rhs.IsDir();
    }
    else if (p_Lhs.IsHidden() != p_Rhs.IsHidden())
    {
      return p_Lhs.IsHidden() < p_Rhs.IsHidden();
    }
    else
    {
      return p_Lhs.name < p_Rhs.name;
    }
  }
};

class FileUtil
{
public:
  static std::string AbsolutePath(const std::string& p_Path);
  static std::string BaseName(const std::string& p_Path);
  static void CopyFile(const std::string& p_SrcPath, const std::string& p_DstPath);
  static std::string DirName(const std::string& p_Path);
  static bool Exists(const std::string& p_Path);
  static std::string GetApplicationDir();
  static std::string GetCurrentWorkingDir();
  static int GetDirVersion(const std::string& p_Dir);
  static std::string GetDownloadsDir();
  static std::string GetFileExt(const std::string& p_Path);
  static std::string GetMimeType(const std::string& p_Path);
  static std::string GetSelfPath();
  static std::string GetLibSuffix();
  static std::string GetSuffixedSize(ssize_t p_Size);
  static void InitDirVersion(const std::string& p_Dir, int p_Version);
  static bool IsDir(const std::string& p_Path);
  static std::set<DirEntry, DirEntryCompare> ListPaths(const std::string& p_Folder);
  static void MkDir(const std::string& p_Path);
  static void Move(const std::string& p_From, const std::string& p_To);
  static std::string ReadFile(const std::string& p_Path);
  static std::string RemoveFileExt(const std::string& p_Path);
  static void RmDir(const std::string& p_Path);
  static void SetApplicationDir(const std::string& p_Path);
  static void WriteFile(const std::string& p_Path, const std::string& p_Str);

private:
  static std::string m_ApplicationDir;
};
