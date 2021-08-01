// fileutil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "fileutil.h"

#include <fstream>

#include <libgen.h>
#include <magic.h>

#include <path.hpp>

#include "log.h"
#include "strutil.h"

std::string FileUtil::m_ApplicationDir;

std::string FileUtil::AbsolutePath(const std::string& p_Path)
{
  return apathy::Path(p_Path).absolute().sanitize().string();
}

std::string FileUtil::BaseName(const std::string& p_Path)
{
  char* path = strdup(p_Path.c_str());
  char* bname = basename(path);
  std::string rv(bname);
  free(path);
  return rv;
}

void FileUtil::CopyFile(const std::string& p_SrcPath, const std::string& p_DstPath)
{
  std::ifstream srcFile(p_SrcPath, std::ios::binary);
  std::ofstream dstFile(p_DstPath, std::ios::binary);
  dstFile << srcFile.rdbuf();
}

bool FileUtil::Exists(const std::string& p_Path)
{
  struct stat sb;
  return (stat(p_Path.c_str(), &sb) == 0);
}

std::string FileUtil::GetApplicationDir()
{
  return m_ApplicationDir;
}

std::string FileUtil::GetCurrentWorkingDir()
{
  return apathy::Path::cwd().absolute().sanitize().string();
}

int FileUtil::GetDirVersion(const std::string& p_Dir)
{
  int version = -1;
  if (FileUtil::Exists(p_Dir))
  {
    std::string versionPath = p_Dir + "/version";
    try
    {
      std::string str = StrUtil::StrFromHex(FileUtil::ReadFile(versionPath));
      if (StrUtil::IsInteger(str))
      {
        version = StrUtil::ToInteger(str);
      }
    }
    catch (...)
    {
      LOG_DEBUG("failed to read %s", versionPath.c_str());
    }
  }
  else
  {
    LOG_DEBUG("dir not present %s", p_Dir.c_str());
  }

  return version;
}

std::string FileUtil::GetDownloadsDir()
{
  std::string homeDir = std::string(getenv("HOME"));
  std::string downloadsDir = homeDir + "/Downloads";
  if (FileUtil::IsDir(downloadsDir))
  {
    return downloadsDir;
  }

  return homeDir;
}

std::string FileUtil::GetFileExt(const std::string& p_Path)
{
  size_t lastPeriod = p_Path.find_last_of(".");
  if (lastPeriod == std::string::npos) return "";

  return p_Path.substr(lastPeriod);
}

std::string FileUtil::GetMimeType(const std::string& p_Path)
{
  int flags = MAGIC_MIME_TYPE;
  magic_t cookie = magic_open(flags);
  if (cookie == NULL) return "";

  std::string mime;
  if (magic_load(cookie, NULL) == 0)
  {
    std::string buf = ReadFile(p_Path);
    const char* rv = magic_buffer(cookie, buf.c_str(), buf.size());
    if (rv != NULL)
    {
      mime = std::string(rv);
    }
  }

  magic_close(cookie);
  return mime;
}

std::string FileUtil::GetSuffixedSize(ssize_t p_Size)
{
  std::vector<std::string> suffixes({ "B", "KB", "MB", "GB", "TB", "PB" });
  size_t i = 0;
  for (i = 0; (i < suffixes.size()) && (p_Size >= 1024); i++, (p_Size /= 1024))
  {
  }

  return std::to_string(p_Size) + " " + suffixes.at(i);
}

void FileUtil::InitDirVersion(const std::string& p_Dir, int p_Version)
{
  int storedVersion = GetDirVersion(p_Dir);
  if (storedVersion != p_Version)
  {
    LOG_DEBUG("init dir %s version %d", p_Dir.c_str(), p_Version);
    FileUtil::RmDir(p_Dir);
    FileUtil::MkDir(p_Dir);
    std::string versionPath = p_Dir + "/version";
    FileUtil::WriteFile(versionPath, StrUtil::StrToHex(std::to_string(p_Version)));
  }
}

bool FileUtil::IsDir(const std::string& p_Path)
{
  return apathy::Path(p_Path).is_directory();
}

std::set<FileInfo, FileInfoCompare> FileUtil::ListPaths(const std::string& p_Folder)
{
  std::set<FileInfo, FileInfoCompare> fileinfos;
  const std::vector<apathy::Path>& paths = apathy::Path::listdir(p_Folder);
  for (auto& path : paths)
  {
    FileInfo fileinfo(path.filename(), path.is_directory() ? -1 : path.size());
    fileinfos.insert(fileinfo);
  }
  return fileinfos;
}

void FileUtil::MkDir(const std::string& p_Path)
{
  apathy::Path::makedirs(p_Path);
}

void FileUtil::Move(const std::string& p_From, const std::string& p_To)
{
  apathy::Path::move(p_From, p_To);
}

std::string FileUtil::ReadFile(const std::string& p_Path)
{
  std::ifstream file(p_Path, std::ios::binary);
  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

std::string FileUtil::RemoveFileExt(const std::string& p_Path)
{
  size_t lastPeriod = p_Path.find_last_of(".");
  if (lastPeriod == std::string::npos) return p_Path;

  return p_Path.substr(0, lastPeriod);
}

void FileUtil::RmDir(const std::string& p_Path)
{
  if (!p_Path.empty())
  {
    apathy::Path::rmdirs(apathy::Path(p_Path));
  }
}

void FileUtil::SetApplicationDir(const std::string& p_Path)
{
  m_ApplicationDir = p_Path;
}

void FileUtil::WriteFile(const std::string& p_Path, const std::string& p_Str)
{
  std::ofstream file(p_Path, std::ios::binary);
  file << p_Str;
}
