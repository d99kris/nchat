// util.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "util.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <ncursesw/ncurses.h>

#include "loghelp.h"

int Util::m_OrgStdErr = -1;
int Util::m_NewStdErr = -1;

std::string Util::GetConfigDir()
{
  return std::string(getenv("HOME")) + std::string("/.nchat");
}

int Util::GetKeyCode(const std::string& p_KeyName)
{
  static std::map<std::string, int> keyCodes =
    {
      // additional keys
      { "KEY_TAB", 9},
      { "KEY_RETURN", 10},

      // ctrl keys
      { "KEY_CTRL@", 0},
      { "KEY_CTRLA", 1},
      { "KEY_CTRLB", 2},
      { "KEY_CTRLC", 3},
      { "KEY_CTRLD", 4},
      { "KEY_CTRLE", 5},
      { "KEY_CTRLF", 6},
      { "KEY_CTRLG", 7},
      { "KEY_CTRLH", 8},
      { "KEY_CTRLI", 9},
      { "KEY_CTRLJ", 10},
      { "KEY_CTRLK", 11},
      { "KEY_CTRLL", 12},
      { "KEY_CTRLM", 13},
      { "KEY_CTRLN", 14},
      { "KEY_CTRLO", 15},
      { "KEY_CTRLP", 16},
      { "KEY_CTRLQ", 17},
      { "KEY_CTRLR", 18},
      { "KEY_CTRLS", 19},
      { "KEY_CTRLT", 20},
      { "KEY_CTRLU", 21},
      { "KEY_CTRLV", 22},
      { "KEY_CTRLW", 23},
      { "KEY_CTRLX", 24},
      { "KEY_CTRLY", 25},
      { "KEY_CTRLZ", 26},
      { "KEY_CTRL[", 27},
      { "KEY_CTRL\\", 28},
      { "KEY_CTRL]", 29},
      { "KEY_CTRL^", 30},
      { "KEY_CTRL_", 31},

      // ncurses keys
      { "KEY_DOWN", KEY_DOWN },
      { "KEY_UP", KEY_UP },
      { "KEY_LEFT", KEY_LEFT },
      { "KEY_RIGHT", KEY_RIGHT },
      { "KEY_HOME", KEY_HOME },
#ifdef __APPLE__
      { "KEY_BACKSPACE", 127 },
#else
      { "KEY_BACKSPACE", KEY_BACKSPACE },
#endif
      { "KEY_F0", KEY_F0 },
      { "KEY_F1", KEY_F(1) },
      { "KEY_F2", KEY_F(2) },
      { "KEY_F3", KEY_F(3) },
      { "KEY_F4", KEY_F(4) },
      { "KEY_F5", KEY_F(5) },
      { "KEY_F6", KEY_F(6) },
      { "KEY_F7", KEY_F(7) },
      { "KEY_F8", KEY_F(8) },
      { "KEY_F9", KEY_F(9) },
      { "KEY_F10", KEY_F(10) },
      { "KEY_F11", KEY_F(11) },
      { "KEY_F12", KEY_F(12) },
      { "KEY_DL", KEY_DL },
      { "KEY_IL", KEY_IL },
      { "KEY_DC", KEY_DC },
      { "KEY_IC", KEY_IC },
      { "KEY_EIC", KEY_EIC },
      { "KEY_CLEAR", KEY_CLEAR },
      { "KEY_EOS", KEY_EOS },
      { "KEY_EOL", KEY_EOL },
      { "KEY_SF", KEY_SF },
      { "KEY_SR", KEY_SR },
      { "KEY_NPAGE", KEY_NPAGE },
      { "KEY_PPAGE", KEY_PPAGE },
      { "KEY_STAB", KEY_STAB },
      { "KEY_CTAB", KEY_CTAB },
      { "KEY_CATAB", KEY_CATAB },
      { "KEY_ENTER", KEY_ENTER },
      { "KEY_PRINT", KEY_PRINT },
      { "KEY_LL", KEY_LL },
      { "KEY_A1", KEY_A1 },
      { "KEY_A3", KEY_A3 },
      { "KEY_B2", KEY_B2 },
      { "KEY_C1", KEY_C1 },
      { "KEY_C3", KEY_C3 },
      { "KEY_BTAB", KEY_BTAB },
      { "KEY_BEG", KEY_BEG },
      { "KEY_CANCEL", KEY_CANCEL },
      { "KEY_CLOSE", KEY_CLOSE },
      { "KEY_COMMAND", KEY_COMMAND },
      { "KEY_COPY", KEY_COPY },
      { "KEY_CREATE", KEY_CREATE },
      { "KEY_END", KEY_END },
      { "KEY_EXIT", KEY_EXIT },
      { "KEY_FIND", KEY_FIND },
      { "KEY_HELP", KEY_HELP },
      { "KEY_MARK", KEY_MARK },
      { "KEY_MESSAGE", KEY_MESSAGE },
      { "KEY_MOVE", KEY_MOVE },
      { "KEY_NEXT", KEY_NEXT },
      { "KEY_OPEN", KEY_OPEN },
      { "KEY_OPTIONS", KEY_OPTIONS },
      { "KEY_PREVIOUS", KEY_PREVIOUS },
      { "KEY_REDO", KEY_REDO },
      { "KEY_REFERENCE", KEY_REFERENCE },
      { "KEY_REFRESH", KEY_REFRESH },
      { "KEY_REPLACE", KEY_REPLACE },
      { "KEY_RESTART", KEY_RESTART },
      { "KEY_RESUME", KEY_RESUME },
      { "KEY_SAVE", KEY_SAVE },
      { "KEY_SBEG", KEY_SBEG },
      { "KEY_SCANCEL", KEY_SCANCEL },
      { "KEY_SCOMMAND", KEY_SCOMMAND },
      { "KEY_SCOPY", KEY_SCOPY },
      { "KEY_SCREATE", KEY_SCREATE },
      { "KEY_SDC", KEY_SDC },
      { "KEY_SDL", KEY_SDL },
      { "KEY_SELECT", KEY_SELECT },
      { "KEY_SEND", KEY_SEND },
      { "KEY_SEOL", KEY_SEOL },
      { "KEY_SEXIT", KEY_SEXIT },
      { "KEY_SFIND", KEY_SFIND },
      { "KEY_SHELP", KEY_SHELP },
      { "KEY_SHOME", KEY_SHOME },
      { "KEY_SIC", KEY_SIC },
      { "KEY_SLEFT", KEY_SLEFT },
      { "KEY_SMESSAGE", KEY_SMESSAGE },
      { "KEY_SMOVE", KEY_SMOVE },
      { "KEY_SNEXT", KEY_SNEXT },
      { "KEY_SOPTIONS", KEY_SOPTIONS },
      { "KEY_SPREVIOUS", KEY_SPREVIOUS },
      { "KEY_SPRINT", KEY_SPRINT },
      { "KEY_SREDO", KEY_SREDO },
      { "KEY_SREPLACE", KEY_SREPLACE },
      { "KEY_SRIGHT", KEY_SRIGHT },
      { "KEY_SRSUME", KEY_SRSUME },
      { "KEY_SSAVE", KEY_SSAVE },
      { "KEY_SSUSPEND", KEY_SSUSPEND },
      { "KEY_SUNDO", KEY_SUNDO },
      { "KEY_SUSPEND", KEY_SUSPEND },
      { "KEY_UNDO", KEY_UNDO },
      { "KEY_MOUSE", KEY_MOUSE },
      { "KEY_RESIZE", KEY_RESIZE },
      { "KEY_EVENT", KEY_EVENT },
    };

  int keyCode = -1;
  std::map<std::string, int>::iterator it = keyCodes.find(p_KeyName);
  if (it != keyCodes.end())
  {
    keyCode = it->second;
  }
  else if (!p_KeyName.empty() && std::all_of(p_KeyName.begin(), p_KeyName.end(), ::isdigit))
  {
    keyCode = std::stoi(p_KeyName);
  }
  else
  {
    LOG_WARNING("unknown key \"%s\"", p_KeyName.c_str());
  }

  return keyCode;
}

std::vector<std::string> Util::WordWrap(std::string p_Text, unsigned p_LineLength)
{
  std::ostringstream wrapped;

  bool first = true;
  std::string line;
  std::istringstream textss(p_Text);
  while (std::getline(textss, line))
  {
    if (first)
    {
      first = false;
    }
    else
    {
      wrapped << '\n';
    }
    
    std::istringstream words(line);
    std::string word;
  
    if (words >> word)
    {
      if (word.length() > p_LineLength)
      {
        std::string extra = word.substr(p_LineLength);
        std::stringstream wordsExtra;
        wordsExtra << extra;
        wordsExtra << words.rdbuf();
        words = std::istringstream(wordsExtra.str());
        word = word.substr(0, p_LineLength);
      }
      
      wrapped << word;
      size_t space_left = p_LineLength - word.length();
      while (words >> word)
      {
        if (word.length() > p_LineLength)
        {
          std::string extra = word.substr(p_LineLength);
          std::stringstream wordsExtra;
          wordsExtra << extra;
          wordsExtra << words.rdbuf();
          words = std::istringstream(wordsExtra.str());
          word = word.substr(0, p_LineLength);
        }

        if (space_left < word.length() + 1)
        {
          wrapped << '\n' << word;
          space_left = p_LineLength - word.length();
        }
        else
        {
          wrapped << ' ' << word;
          space_left -= word.length() + 1;
        }
      }
    }
  }

  std::stringstream stream(wrapped.str());
  std::vector<std::string> lines;
  while (std::getline(stream, line))
  {
    lines.push_back(line);
  }

  return lines;
}

std::string Util::ToString(const std::wstring& p_WStr)
{
  size_t len = std::wcstombs(nullptr, p_WStr.c_str(), 0) + 1;
  char* cstr = new char[len];
  std::wcstombs(cstr, p_WStr.c_str(), len);
  std::string str(cstr);
  delete[] cstr;
  return str;
}

std::wstring Util::ToWString(const std::string& p_Str)
{
  size_t len = 1 + mbstowcs(nullptr, p_Str.c_str(), 0);
  wchar_t* wcstr = new wchar_t[len];
  std::mbstowcs(wcstr, p_Str.c_str(), len);
  std::wstring wstr(wcstr);
  delete[] wcstr;
  return wstr;
}

std::string Util::GetAppVersion()
{
#ifdef PROJECT_VERSION
  std::string version = "v" PROJECT_VERSION;
#else
  std::string version = "";
#endif
  return version;
}

std::string Util::GetOs()
{
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#elif defined(__linux__)
  return "Linux";
#elif defined(BSD)
  return "BSD";
#else
  return "Unknown OS";
#endif
}

std::string Util::GetCompiler()
{
#if defined(_MSC_VER)
  return "msvc-" + std::to_string(_MSC_VER);
#elif defined(__clang__)
  return "clang-" + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__)
    + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
  return "gcc-" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__)
    + "." + std::to_string(__GNUC_PATCHLEVEL__);
#else
  return "Unknown Compiler";
#endif
}

void Util::RegisterSignalHandler()
{
  signal(SIGABRT, SignalHandler);
  signal(SIGSEGV, SignalHandler);
  signal(SIGBUS, SignalHandler);
  signal(SIGILL, SignalHandler);
  signal(SIGFPE, SignalHandler);
  signal(SIGPIPE, SignalHandler); 
}

void Util::SignalHandler(int p_Signal)
{
  void *callstack[64];
  int size = backtrace(callstack, sizeof(callstack));
  const std::string& callstackStr = "\n" + BacktraceSymbolsStr(callstack, size) + "\n";
  const std::string& logMsg = "unexpected termination: " + std::to_string(p_Signal);
  LOG_ERROR("%s", logMsg.c_str());
  LOG_DUMP(callstackStr.c_str());

  CleanupStdErrRedirect();
  system("reset");
  std::cerr << logMsg << "\n" << callstackStr;
  exit(1);
}

std::string Util::BacktraceSymbolsStr(void* p_Callstack[], int p_Size)
{
  std::stringstream ss;
  for (int i = 0; i < p_Size; ++i)
  {
    ss << std::left << std::setw(2) << std::setfill(' ') << i << "  ";
    ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << std::right
       << (unsigned long long) p_Callstack[i] << "  ";
    
    Dl_info dlinfo;
    if (dladdr(p_Callstack[i], &dlinfo) && dlinfo.dli_sname)
    {
      if (dlinfo.dli_sname[0] == '_')
      {
        int status = -1;
        char* demangled = NULL;
        demangled = abi::__cxa_demangle(dlinfo.dli_sname, NULL, 0, &status);
        if (demangled && (status == 0))
        {
          ss << demangled;
          free(demangled);
        }
        else
        {
          ss << dlinfo.dli_sname;
        }
      }
      else
      {
        ss << dlinfo.dli_sname;
      }
    }
    ss << "\n";
  }

  return ss.str();
}

void Util::InitStdErrRedirect(const std::string& p_Path)
{
  m_NewStdErr = open(p_Path.c_str(), O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  if (m_NewStdErr != -1)
  {
    m_OrgStdErr = dup(fileno(stderr));
    dup2(m_NewStdErr, fileno(stderr));
  }
}

void Util::CleanupStdErrRedirect()
{
  if (m_NewStdErr != -1)
  {
    fflush(stderr);
    close(m_NewStdErr);
    dup2(m_OrgStdErr, fileno(stderr));
    close(m_OrgStdErr);
  }
}
