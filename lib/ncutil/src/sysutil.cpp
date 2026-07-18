// sysutil.cpp
//
// Copyright (c) 2024-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "sysutil.h"

#include <cerrno>
#include <sstream>

#include <signal.h>
#include <unistd.h>

#include <sys/wait.h>

#include "fileutil.h"
#include "log.h"
#include "strutil.h"

namespace
{
  // Removes any "(...)" groups, including a single space preceding an opening
  // parenthesis, e.g. "debian gnu/linux 12 (bookworm) arm64" -> "debian gnu/linux 12 arm64".
  std::string StripParentheses(const std::string& p_Str)
  {
    std::string result = p_Str;
    size_t open = std::string::npos;
    while ((open = result.find('(')) != std::string::npos)
    {
      const size_t close = result.find(')', open);
      if (close == std::string::npos)
      {
        break;
      }

      size_t start = open;
      if ((start > 0) && (result[start - 1] == ' '))
      {
        --start;
      }

      result.erase(start, close - start + 1);
    }

    return result;
  }
}

std::string SysUtil::GetBuildInfo()
{
  // Build origin (github / local, or packager-provided). Sourced from the
  // buildinfo.cpp accessor so the volatile generated buildinfo.h stays out of
  // this translation unit -- a git sha/branch change recompiles only buildinfo.cpp.
  std::string origin = GetBuildOrigin();

  // Build type
#if defined(NCHAT_BUILD_RELEASE)
  std::string type = "release";
#elif defined(NCHAT_BUILD_DEBUG)
  std::string type = "debug";
#elif defined(NCHAT_BUILD_RELWITHDEBINFO)
  std::string type = "reldbg";
#else
  std::string type = "unknown";
#endif

  // External linkage
#if defined(NCHAT_BUILD_STATIC_EXTLIBS)
  std::string linkage = "static";
#else
  std::string linkage = "dynamic";
#endif

  // Build git sha (from the buildinfo.cpp accessor, see origin note above)
  std::string sha = GetBuildGitSha();

  return StrUtil::ToLower(origin + " " + type + " " + linkage + " " + sha);
}

std::string SysUtil::GetCompiler(bool p_Verbose)
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
#elif defined(NCHAT_BUILD_MUSL)
  // musl provides no version macro; NCHAT_BUILD_MUSL is set by CMake from the
  // compiler target triple (see CMakeLists.txt).
  std::string libc = "musl";
#elif defined(__ANDROID__)
  std::string libc = "bionic";
#else
  std::string libc = "unknown";
#endif
#else
  // macOS libc (part of libSystem) has no conventional short name; leave empty.
  std::string libc;
#endif

  std::string compilerLibc = compiler + (!libc.empty() ? " " + libc : "");
  if (!p_Verbose)
  {
    compilerLibc = StripParentheses(compilerLibc);
  }

  return StrUtil::ToLower(compilerLibc);
}

std::string SysUtil::GetGo(const std::string& p_GoVersion)
{
  return StrUtil::ToLower("go " + (p_GoVersion.empty() ? "n/a" : p_GoVersion));
}

std::string SysUtil::GetOsArch(bool p_Verbose)
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

  std::string osArch = os + " " + arch;
  if (!p_Verbose)
  {
    osArch = StripParentheses(osArch);
  }

  return StrUtil::ToLower(osArch);
}

bool SysUtil::IsSupportedLibc()
{
  // glibc and macOS are always supported. On musl the Go-based protocols crash
  // with a stock Go toolchain (nchat issue #204 / Go PR #69325), so they are
  // only built with a patched toolchain; NCHAT_MUSL_GO_PATCHED (set by CMake
  // when -DHAS_MUSL_GO_PATCHED=ON, as in the official musl release builds)
  // marks such a build as supported. See doc/MUSLGO.md.
#if defined(__APPLE__) || defined(__GLIBC__) || defined(NCHAT_MUSL_GO_PATCHED)
  return true;
#else
  return false;
#endif
}

bool SysUtil::RunCommand(const std::string& p_Cmd, std::string* p_StdOut /*= nullptr*/)
{
  const bool logStdErr = true;

  std::string stdoutPath = "/dev/null";
  if (p_StdOut != nullptr)
  {
    stdoutPath = FileUtil::GetTempDir() + "/stdout.txt";
  }

  std::string stderrPath = "/dev/null";
  if (logStdErr)
  {
    stderrPath = FileUtil::GetTempDir() + "/stderr.txt";
  }

  const std::string cmdPrefix = "{ ";
  const std::string cmdSuffix = " ; } >'" + stdoutPath + "' 2>'" + stderrPath + "'";
  const std::string cmd = cmdPrefix + p_Cmd + cmdSuffix;

  // run command
  LOG_TRACE("cmd \"%s\" start", cmd.c_str());
  const int rv = SysUtil::System(cmd);
  if (rv != 0)
  {
    LOG_WARNING("cmd \"%s\" failed (%d)", cmd.c_str(), rv);
  }

  // stdout
  if ((p_StdOut != nullptr) && FileUtil::Exists(stdoutPath))
  {
    std::string str = FileUtil::ReadFile(stdoutPath);
    FileUtil::RmFile(stdoutPath);

    // trim trailing linebreak
    if (!str.empty() && str.back() == '\n')
    {
      str = str.substr(0, str.length() - 1);
    }

    *p_StdOut = str;
  }

  // stderr
  if (logStdErr && FileUtil::Exists(stderrPath))
  {
    const std::string stderrStr = FileUtil::ReadFile(stderrPath);
    FileUtil::RmFile(stderrPath);
    if (!stderrStr.empty())
    {
      LOG_WARNING("cmd \"%s\" stderr:", cmd.c_str());
      Log::Dump(stderrStr.c_str());
    }
  }

  return (rv == 0);
}

int SysUtil::System(const std::string& p_Cmd)
{
#if defined(HAVE_TERMUX)
  static const std::string shPath = "/data/data/com.termux/files/usr/bin/sh";
#else
  static const std::string shPath = "/bin/sh";
#endif

  // Block SIGCHLD and ignore SIGINT/SIGQUIT in parent per POSIX system() spec
  struct sigaction saIgnore;
  struct sigaction saOrigInt;
  struct sigaction saOrigQuit;
  saIgnore.sa_handler = SIG_IGN;
  sigemptyset(&saIgnore.sa_mask);
  saIgnore.sa_flags = 0;
  sigaction(SIGINT, &saIgnore, &saOrigInt);
  sigaction(SIGQUIT, &saIgnore, &saOrigQuit);

  sigset_t blockChld;
  sigset_t origMask;
  sigemptyset(&blockChld);
  sigaddset(&blockChld, SIGCHLD);
  sigprocmask(SIG_BLOCK, &blockChld, &origMask);

  pid_t pid = fork();
  if (pid == 0)
  {
    // Child: restore original signal dispositions and mask
    sigaction(SIGINT, &saOrigInt, nullptr);
    sigaction(SIGQUIT, &saOrigQuit, nullptr);
    sigprocmask(SIG_SETMASK, &origMask, nullptr);

    execl(shPath.c_str(), "sh", "-c", p_Cmd.c_str(), (char*)nullptr);
    _exit(127);
  }

  int status = -1;
  if (pid > 0)
  {
    // Retry waitpid on EINTR (e.g. from SIGWINCH during terminal resize)
    while (waitpid(pid, &status, 0) < 0)
    {
      if (errno != EINTR)
      {
        status = -1;
        break;
      }
    }
  }

  // Restore original signal dispositions and mask
  sigaction(SIGINT, &saOrigInt, nullptr);
  sigaction(SIGQUIT, &saOrigQuit, nullptr);
  sigprocmask(SIG_SETMASK, &origMask, nullptr);

  return status;
}
