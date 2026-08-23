// log.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "log.h"

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

#include <sys/time.h>

#include "sysutil.h"

std::string Log::m_Path;
int Log::m_VerboseLevel = 0;
std::mutex Log::m_Mutex;
int Log::m_LogFd = -1;
bool Log::m_HadWarnErr = false;

void Log::Init(const std::string& p_Path)
{
  m_Path = p_Path;
  const std::string archivePath = m_Path + ".1";
  remove(archivePath.c_str());
  rename(m_Path.c_str(), archivePath.c_str());
  Dump("");
  m_LogFd = open(m_Path.c_str(), O_WRONLY | O_APPEND);
}

void Log::Cleanup(bool p_IsLogdumpEnabled)
{
  if (m_LogFd != -1)
  {
    close(m_LogFd);
  }

  if (p_IsLogdumpEnabled && m_HadWarnErr)
  {
    const std::string cmd = "grep -e '| ERROR |' -e '| WARN  |' " + m_Path;
    int rv = SysUtil::System(cmd);
    if (rv != 0)
    {
      printf("log dump command failed: %s\n", cmd.c_str());
    }
  }
}

void Log::SetVerboseLevel(int p_Level)
{
  m_VerboseLevel = p_Level;
}

void Log::Trace(const char* p_Filename, int p_LineNo, const char* p_Format, ...)
{
  if (m_VerboseLevel >= TRACE_LEVEL)
  {
    va_list vaList;
    va_start(vaList, p_Format);
    Write(p_Filename, p_LineNo, "TRACE", p_Format, vaList);
    va_end(vaList);
  }
}

void Log::Debug(const char* p_Filename, int p_LineNo, const char* p_Format, ...)
{
  if (m_VerboseLevel >= DEBUG_LEVEL)
  {
    va_list vaList;
    va_start(vaList, p_Format);
    Write(p_Filename, p_LineNo, "DEBUG", p_Format, vaList);
    va_end(vaList);
  }
}

void Log::Info(const char* p_Filename, int p_LineNo, const char* p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write(p_Filename, p_LineNo, "INFO ", p_Format, vaList);
  va_end(vaList);
}

void Log::Warning(const char* p_Filename, int p_LineNo, const char* p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write(p_Filename, p_LineNo, "WARN ", p_Format, vaList);
  va_end(vaList);
  m_HadWarnErr = true;
}

void Log::Error(const char* p_Filename, int p_LineNo, const char* p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write(p_Filename, p_LineNo, "ERROR", p_Format, vaList);
  va_end(vaList);
  m_HadWarnErr = true;
}

void Log::Dump(const char* p_Str)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  if (m_Path.empty()) return;

  FILE* file = fopen(m_Path.c_str(), "a");
  if (file != NULL)
  {
    fprintf(file, "%s", p_Str);
    fclose(file);
  }
}

void Log::Callstack(void* const* p_Callstack, int p_Size, const char* p_LogMsg)
{
  if (m_LogFd != -1)
  {
    UNUSED(write(m_LogFd, p_LogMsg, strlen(p_LogMsg)));
    WriteCallstackToFd(m_LogFd, p_Callstack, p_Size);
  }
}

void Log::WriteCallstackToFd(int p_Fd, void* const* p_Callstack, int p_Size)
{
  if (p_Fd == -1) return;

#if defined(HAVE_EXECINFO_H)
  // glibc/macOS: symbolicate inline against the (dynamic) symbol table.
  backtrace_symbols_fd(p_Callstack, p_Size, p_Fd);
#elif defined(NCHAT_BUILD_MUSL)
  // musl: no backtrace_symbols; the static binary is stripped and carries no
  // symbol table to resolve against anyway. Emit raw addresses (absolute, since
  // the musl binary is linked -no-pie) for offline `addr2line -e nchat.debug`.
  for (int i = 0; i < p_Size; ++i)
  {
    char line[24]; // "0x" + up to 16 hex digits + "\n" + nul
    const int len = snprintf(line, sizeof(line), "%p\n", p_Callstack[i]);
    if (len > 0)
    {
      UNUSED(write(p_Fd, line, (size_t)len));
    }
  }
#else
  // Other execinfo-less platforms (Termux/Android, OpenBSD, ...): the frame-pointer
  // walk in AppUtil::GetCallstack is scoped to musl only, so no callstack is
  // captured here. Emit an explicit marker so the empty output is self-explanatory.
  UNUSED(p_Callstack);
  UNUSED(p_Size);
  static const char unsupportedMsg[] = "(unsupported platform)\n";
  UNUSED(write(p_Fd, unsupportedMsg, sizeof(unsupportedMsg) - 1));
#endif
}

void Log::Write(const char* p_Filename, int p_LineNo, const char* p_Level,
                const char* p_Format, va_list p_VaList)
{
  std::unique_lock<std::mutex> lock(m_Mutex);
  if (m_Path.empty()) return;

  FILE* file = fopen(m_Path.c_str(), "a");
  if (file != NULL)
  {
    char timestamp[26];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tminfo = localtime(&tv.tv_sec);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tminfo);
    long msec = tv.tv_usec / 1000;
    fprintf(file, "%s.%03ld | %s | ", timestamp, msec, p_Level);
    vfprintf(file, p_Format, p_VaList);
    fprintf(file, "  (%s:%d)", p_Filename, p_LineNo);
    fprintf(file, "\n");
    fclose(file);
  }
}
