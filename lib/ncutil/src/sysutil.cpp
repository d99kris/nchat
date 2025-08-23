// sysutil.cpp
//
// Copyright (c) 2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "sysutil.h"

#include <sstream>

#include <unistd.h>

#include "fileutil.h"
#include "strutil.h"

std::string SysUtil::GetCompiler()
{
#if defined(__VERSION__)
#if !defined(__clang__) && defined(__GNUC__)
  std::string compiler = "gcc " __VERSION__;
#else
  std::string compiler = __VERSION__;
#endif
#else
  std::string compiler = "unknown compiler";
#endif

#if defined(__linux__)
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__)
  std::stringstream sslibc;
  sslibc << "glibc " << __GLIBC__ << "." << __GLIBC_MINOR__;
  std::string libc = sslibc.str();
#else
  std::string libc = "non-glibc";
#endif
#else
  std::string libc;
#endif

  return StrUtil::ToLower(compiler + (!libc.empty() ? " " + libc : ""));
}

std::string SysUtil::GetGo(const std::string& p_GoVersion)
{
  return StrUtil::ToLower("go " + (p_GoVersion.empty() ? "n/a" : p_GoVersion));
}

std::string SysUtil::GetOsArch()
{
  static const std::string os = []()
  {
#if defined(__linux__)
    std::string str = FileUtil::ReadFile("/etc/os-release");
    std::string prettyName = StrUtil::ExtractString(str, "PRETTY_NAME=\"", "\"");
    return prettyName.empty() ? "Linux" : prettyName;
#elif defined(__APPLE__)
    std::string str = FileUtil::ReadFile("/System/Library/CoreServices/SystemVersion.plist");
    std::string name = StrUtil::ExtractString(str, "<key>ProductName</key>\n\t<string>", "</string>");
    std::string ver = StrUtil::ExtractString(str, "<key>ProductVersion</key>\n\t<string>", "</string>");
    return name + " " + ver;
#else
    return "Unknown";
#endif
  }();

  static const std::string arch = []()
  {
#if defined(__arm__)
    return "arm";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__) || defined(__amd64__)
    return "x86_64";
#elif defined(__i386__)
    return "i386";
#else
    return std::to_string(sizeof(void*) * 8) + "-bit";
#endif
  }();

  static const std::string osArch = os + " " + arch;
  return StrUtil::ToLower(osArch);
}

bool SysUtil::IsSupportedLibc()
{
#if defined(__APPLE__) || defined(__GLIBC__)
  return true;
#else
  return false;
#endif
}
