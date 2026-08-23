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
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#ifdef __linux__
#include <ucontext.h>
#endif

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
    const int size = GetCallstack(callstack, (int)(sizeof(callstack) / sizeof(callstack[0])), nullptr);
    Log::Callstack(callstack, size, logMsg);
  }
}

std::string AppUtil::GetAppName(bool p_WithVersion, bool p_WithBranch /*= false*/)
{
  std::string name = "nchat";
  if (p_WithVersion)
  {
    name += " " + GetAppVersion();
  }

  if (p_WithBranch)
  {
    const std::string branch = SysUtil::GetBuildGitBranch();
    if (!branch.empty() && (branch != "master"))
    {
      name += " " + branch;
    }
  }

  return name;
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

  // Install via sigaction with SA_SIGINFO so the handler receives the ucontext_t.
  // It is unused by the execinfo path (glibc/macOS, which walk the stack via
  // backtrace()) and by the SIGINT user-abort path, but the musl crash path needs
  // it to seed a frame-pointer walk from the interrupted register state -- see
  // GetCallstack() / FramePointerBacktrace().
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = SignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO;

  for (const auto sig : signals)
  {
    sigaction(sig, &action, nullptr);
  }
}

#if !defined(HAVE_EXECINFO_H) && defined(NCHAT_BUILD_MUSL)
// Stack backtrace for platforms without execinfo/backtrace() (musl).
//
// Deliberately scoped to musl (NCHAT_BUILD_MUSL, set by CMake from the target
// triple) rather than to !HAVE_EXECINFO_H at large: the walk relies on musl's
// ucontext layout, the -no-pie / -fno-omit-frame-pointer flags CMake pairs with
// musl, and the __executable_start/_etext bounds, and has only been tested there.
// Other execinfo-less platforms (Termux/Android, OpenBSD, ...) would run untested
// register/stack reads inside the crash handler, so they get no callstack instead.
//
// Two constraints drive this: (1) glibc's backtrace() is absent on musl and the
// libexecinfo package was dropped from Alpine, and (2) libgcc's
// _Unwind_Backtrace cannot step past musl's signal-return trampoline
// (__restore_rt), which lacks the DWARF CFI glibc's vdso trampoline has -- so an
// unwinder started inside the handler only ever sees the handler's own frames.
//
// We instead recover the pre-signal frames directly, in two tiers seeded from the
// interrupted register state in the signal ucontext_t (or the current frame for
// the non-signal, assertion path):
//
//   1. A precise frame-record walk down the frame-pointer chain. Exact and
//      ordered, but only where a frame pointer is maintained -- nchat's own code
//      is built -fno-omit-frame-pointer (CMakeLists.txt), the static deps
//      (OpenSSL, ...) and musl itself are not. When an async signal lands in that
//      frame-pointer-less code (or in a thread's entry frame, fp=0), the chain
//      breaks at the first hop and the walk yields only the faulting pc -- this is
//      why an x86_64 crash during setup logged a single address.
//   2. A fallback raw-stack scan, used when the frame-pointer walk barely advanced
//      (< kMinFpFrames). Every call pushes a return address regardless of frame
//      pointers, so scanning the thread's stack for words that point into nchat's
//      own text recovers the chain across frame-pointer-less frames. It is
//      heuristic -- stale return addresses left on the stack can show as extra or
//      out-of-order frames -- so it is only a fallback, never used when the precise
//      walk succeeds.
//
// Both tiers filter return addresses to nchat's own text range
// [__executable_start, _etext); with the musl binary linked -no-pie that range is
// fixed at link time and the logged addresses resolve directly with
// `addr2line -e nchat.debug <addr>`, no ASLR rebasing.

// Linker-provided bounds of the (non-PIE) executable image; absolute addresses.
extern "C" char __executable_start[];
extern "C" char _etext[];

// Below this many frames the frame-pointer walk is assumed to have broken at a
// frame-pointer-less frame, triggering the raw-stack-scan fallback. A successful
// hop past the faulting pc yields >= 2, so 1 (pc only) always falls back.
static const int kMinFpFrames = 3;

// End address of the /proc/self/maps region containing p_Sp (0 if none/unreadable).
// Async-signal-safe: raw open/read/close plus a hand-rolled parse over a fixed
// buffer with a small line accumulator -- no heap, stdio or sscanf. Used to bound
// the stack scan to the thread's own stack mapping so the reads cannot fault.
static uintptr_t StackRegionEnd(uintptr_t p_Sp)
{
  const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;

  char buf[4096];
  char line[256];
  size_t lineLen = 0;
  uintptr_t result = 0;
  ssize_t got = 0;
  while ((result == 0) && ((got = read(fd, buf, sizeof(buf))) > 0))
  {
    for (ssize_t i = 0; (i < got) && (result == 0); ++i)
    {
      const char c = buf[i];
      if (c != '\n')
      {
        if (lineLen < (sizeof(line) - 1)) line[lineLen++] = c;
        continue;
      }
      line[lineLen] = '\0';
      lineLen = 0;

      // Parse the "loHex-hiHex " address range at the start of the line.
      uintptr_t lo = 0;
      uintptr_t hi = 0;
      const char* q = line;
      while (*q == ' ') ++q;
      while (((*q >= '0') && (*q <= '9')) || ((*q >= 'a') && (*q <= 'f')))
      {
        lo = (lo << 4) + ((*q <= '9') ? (uintptr_t)(*q - '0') : (uintptr_t)(*q - 'a' + 10));
        ++q;
      }
      if (*q != '-') continue;
      ++q;
      while (((*q >= '0') && (*q <= '9')) || ((*q >= 'a') && (*q <= 'f')))
      {
        hi = (hi << 4) + ((*q <= '9') ? (uintptr_t)(*q - '0') : (uintptr_t)(*q - 'a' + 10));
        ++q;
      }
      if ((p_Sp >= lo) && (p_Sp < hi)) result = hi;
    }
  }

  close(fd);
  return result;
}

static int FramePointerBacktrace(void** p_Callstack, int p_MaxSize, void* p_Context)
{
  uintptr_t pc = 0;
  uintptr_t fp = 0;
  uintptr_t sp = 0;
  if (p_Context != nullptr)
  {
    ucontext_t* uc = static_cast<ucontext_t*>(p_Context);
#if defined(__aarch64__)
    pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
    fp = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
    sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
#elif defined(__x86_64__)
    pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    fp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
    sp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
#endif
  }
  else
  {
    // non-signal caller (assertion): walk from our own frame
    fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    sp = fp;
  }

  const uintptr_t wordSize = sizeof(uintptr_t);
  const uintptr_t textLo = reinterpret_cast<uintptr_t>(__executable_start);
  const uintptr_t textHi = reinterpret_cast<uintptr_t>(_etext);

  int size = 0;

  // The faulting instruction itself is always captured (independent of the frame
  // chain), so a crash in frame-pointer-less code still logs at least its pc.
  if ((pc != 0) && (size < p_MaxSize))
  {
    p_Callstack[size++] = reinterpret_cast<void*>(pc);
  }

  // Tier 1 -- precise frame-record walk: at [fp] the caller's saved fp, at
  // [fp + wordsize] the return address. Layout is identical on aarch64 and
  // x86_64. The guards (alignment, in-text return address, monotonically
  // increasing fp, bounded count) keep a corrupt chain from looping, reading far
  // afield, or trailing garbage past the outermost (crt0) frame.
  while ((fp != 0) && ((fp & (wordSize - 1)) == 0) && (size < p_MaxSize))
  {
    const uintptr_t nextFp = *reinterpret_cast<uintptr_t*>(fp);
    const uintptr_t retAddr = *reinterpret_cast<uintptr_t*>(fp + wordSize);
    if ((retAddr < textLo) || (retAddr >= textHi)) break;
    p_Callstack[size++] = reinterpret_cast<void*>(retAddr);
    if (nextFp <= fp) break; // the stack unwinds toward higher addresses
    fp = nextFp;
  }

  // Tier 2 -- raw-stack-scan fallback: the frame-pointer walk broke early (signal
  // landed in frame-pointer-less code). Scan the thread's stack, bounded to its
  // own mapping so reads cannot fault, for words pointing into nchat's text. If
  // the stack mapping is unreadable we keep the pc rather than scan a blind window.
  if ((size < kMinFpFrames) && (sp != 0))
  {
    const uintptr_t top = StackRegionEnd(sp);
    if (top != 0)
    {
      size = 0;
      if ((pc != 0) && (size < p_MaxSize))
      {
        p_Callstack[size++] = reinterpret_cast<void*>(pc);
      }

      for (uintptr_t a = sp; ((a + wordSize) <= top) && (size < p_MaxSize); a += wordSize)
      {
        const uintptr_t w = *reinterpret_cast<uintptr_t*>(a);
        if ((w >= textLo) && (w < textHi))
        {
          p_Callstack[size++] = reinterpret_cast<void*>(w);
        }
      }
    }
  }

  return size;
}
#endif // !HAVE_EXECINFO_H && NCHAT_BUILD_MUSL

int AppUtil::GetCallstack(void** p_Callstack, int p_MaxSize, void* p_Context)
{
#if defined(HAVE_EXECINFO_H)
  UNUSED(p_Context);
  return backtrace(p_Callstack, p_MaxSize);
#elif defined(NCHAT_BUILD_MUSL)
  return FramePointerBacktrace(p_Callstack, p_MaxSize, p_Context);
#else
  UNUSED(p_Callstack);
  UNUSED(p_MaxSize);
  UNUSED(p_Context);
  return 0;
#endif
}

void AppUtil::SignalHandler(int p_Signal, siginfo_t* p_SigInfo, void* p_Context)
{
  UNUSED(p_SigInfo);
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
    const int size = GetCallstack(callstack, (int)(sizeof(callstack) / sizeof(callstack[0])), p_Context);
    Log::Callstack(callstack, size, logMsg);

    // non-signal safe code section
    UNUSED(SysUtil::System("reset"));
    UNUSED(write(STDERR_FILENO, logMsg, strlen(logMsg)));

    Log::WriteCallstackToFd(STDERR_FILENO, callstack, size);
  }

  signal(p_Signal, SIG_DFL);
  kill(getpid(), p_Signal);
}
