// fileutil.cpp
//
// Copyright (c) 2020-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "fileutil.h"

#include <fstream>

#include <wordexp.h>

#ifdef __APPLE__
#include <libproc.h>
#endif

#include <libgen.h>
#include <magic.h>

#include <path.hpp>

#include "log.h"
#include "strutil.h"

std::string FileUtil::m_ApplicationDir;
std::string FileUtil::m_DownloadsDir;

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

std::string FileUtil::DirName(const std::string& p_Path)
{
  char* buf = strdup(p_Path.c_str());
  std::string rv = std::string(dirname(buf));
  free(buf);
  return rv;
}

bool FileUtil::Exists(const std::string& p_Path)
{
  struct stat sb;
  return (stat(p_Path.c_str(), &sb) == 0);
}

std::string FileUtil::ExpandPath(const std::string& p_Path)
{
  if (p_Path.empty()) return p_Path;

  if ((p_Path.at(0) != '~') && ((p_Path.at(0) != '$'))) return p_Path;

  wordexp_t exp;
  std::string rv;
  if ((wordexp(p_Path.c_str(), &exp, WRDE_NOCMD) == 0) && (exp.we_wordc > 0))
  {
    rv = std::string(exp.we_wordv[0]);
    for (size_t i = 1; i < exp.we_wordc; ++i)
    {
      rv += " " + std::string(exp.we_wordv[i]);
    }
    wordfree(&exp);
  }
  else
  {
    rv = p_Path;
  }

  return rv;
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
  int version = 0;
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
  if (!m_DownloadsDir.empty())
  {
    std::string downloadsDir = FileUtil::ExpandPath(m_DownloadsDir);
    if (FileUtil::IsDir(downloadsDir))
    {
      return downloadsDir;
    }
  }

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

std::string FileUtil::GetSelfPath()
{
#if defined(__APPLE__)
  char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
  if (proc_pidpath(getpid(), pathbuf, sizeof(pathbuf)) > 0)
  {
    return std::string(pathbuf);
  }
#elif defined(__linux__)
  char pathbuf[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", pathbuf, sizeof(pathbuf));
  if (count > 0)
  {
    return std::string(pathbuf, count);
  }
#endif
  return "";
}

std::string FileUtil::GetLibSuffix()
{
#if defined(__APPLE__)
  return ".dylib";
#elif defined(__linux__)
  return ".so";
#endif
  return "";
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

std::set<DirEntry, DirEntryCompare> FileUtil::ListPaths(const std::string& p_Folder)
{
  std::set<DirEntry, DirEntryCompare> fileinfos;
  const std::vector<apathy::Path>& paths = apathy::Path::listdir(p_Folder);
  for (auto& path : paths)
  {
    DirEntry fileinfo(path.filename(), path.is_directory() ? -1 : path.size());
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

void FileUtil::SetDownloadsDir(const std::string& p_DownloadsDir)
{
  m_DownloadsDir = p_DownloadsDir;
}

void FileUtil::WriteFile(const std::string& p_Path, const std::string& p_Str)
{
  std::ofstream file(p_Path, std::ios::binary);
  file << p_Str;
}
