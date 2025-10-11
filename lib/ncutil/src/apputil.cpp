// apputil.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "apputil.h"

#include <set>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <signal.h>
#include <unistd.h>

#include <sys/resource.h>

#include "appconfig.h"
#include "log.h"
#include "sysutil.h"
#include "version.h"

bool AppUtil::m_DeveloperMode = false;

void AppUtil::AssertionFailed()
{
  static const bool assertAbort = AppConfig::GetBool("assert_abort");;
  if (assertAbort)
  {
    abort();
  }
  else
  {
    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "callstack:\n");
    void* callstack[64] = { 0 };
#ifdef HAVE_EXECINFO_H
    const int size = backtrace(callstack, sizeof(callstack));
#else
    const int size = 0;
#endif
    Log::Callstack(callstack, size, logMsg);
  }
}

std::string AppUtil::GetAppName(bool p_WithVersion)
{
  return std::string("nchat") + (p_WithVersion ? (" " + GetAppVersion()) : "");
}

std::string AppUtil::GetAppVersion()
{
  static std::string version = NCHAT_VERSION;
  return version;
}

void AppUtil::SetDeveloperMode(bool p_DeveloperMode)
{
  m_DeveloperMode = p_DeveloperMode;
}

bool AppUtil::GetDeveloperMode()
{
  return m_DeveloperMode;
}

void AppUtil::InitCoredump()
{
  struct rlimit lim;
  int rv = 0;
  rv = getrlimit(RLIMIT_CORE, &lim);
  if (rv != 0)
  {
    LOG_WARNING("getrlimit failed %d errno %d", rv, errno);
  }
  else
  {
    lim.rlim_cur = lim.rlim_max;
    rv = setrlimit(RLIMIT_CORE, &lim);
    if (rv != 0)
    {
      LOG_WARNING("setrlimit failed %d errno %d", rv, errno);
    }
    else
    {
      LOG_DEBUG("setrlimit cur %llu max %llu", lim.rlim_cur, lim.rlim_max);
    }
  }

#ifdef __APPLE__
  rv = access("/cores", W_OK);
  if (rv == -1)
  {
    LOG_WARNING("/cores is not writable");
  }
#endif
}

void AppUtil::InitSignalHandler()
{
  static const std::set<int> signals =
  {
    // terminating
    SIGABRT,
    SIGBUS,
    SIGFPE,
    SIGILL,
    SIGQUIT,
    SIGSEGV,
    SIGSYS,
    SIGTRAP,
    // user abort (setup)
    SIGINT,
  };

  for (const auto sig : signals)
  {
    signal(sig, SignalHandler);
  }
}

void AppUtil::SignalHandler(int p_Signal)
{
  char logMsg[64];
  if (p_Signal == SIGINT)
  {
    snprintf(logMsg, sizeof(logMsg), "user abort\n");

    // non-signal safe code section
    LOG_INFO("user abort");
    UNUSED(SysUtil::System("reset"));
    UNUSED(write(STDERR_FILENO, logMsg, strlen(logMsg)));
  }
  else
  {
    snprintf(logMsg, sizeof(logMsg), "unexpected termination %d\ncallstack:\n", p_Signal);
    void* callstack[64] = { 0 };
#ifdef HAVE_EXECINFO_H
    const int size = backtrace(callstack, sizeof(callstack));
#else
    const int size = 0;
#endif
    Log::Callstack(callstack, size, logMsg);

    // non-signal safe code section
    UNUSED(SysUtil::System("reset"));
    UNUSED(write(STDERR_FILENO, logMsg, strlen(logMsg)));

#ifdef HAVE_EXECINFO_H
    backtrace_symbols_fd(callstack, size, STDERR_FILENO);
#endif
  }

  signal(p_Signal, SIG_DFL);
  kill(getpid(), p_Signal);
}
