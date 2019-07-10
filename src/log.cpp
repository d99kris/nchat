// log.cpp
//
// Copyright (c) 2019 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "log.h"

#include <stdarg.h>
#include <sys/time.h>

std::string Log::m_Path;
bool Log::m_DebugEnabled = false;
std::mutex Log::m_Mutex;

void Log::SetPath(const std::string &p_Path)
{
  m_Path = p_Path;
  remove(m_Path.c_str());
}

void Log::SetDebugEnabled(bool p_DebugEnabled)
{
  m_DebugEnabled = p_DebugEnabled;
}

void Log::Debug(const char *p_Format, ...)
{
  if (m_DebugEnabled)
  {
    va_list vaList;
    va_start(vaList, p_Format);
    Write("DEBUG", p_Format, vaList);
    va_end(vaList);
  }
}

void Log::Info(const char *p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write("INFO ", p_Format, vaList);
  va_end(vaList);
}

void Log::Warning(const char *p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write("WARN ", p_Format, vaList);
  va_end(vaList);
}

void Log::Error(const char *p_Format, ...)
{
  va_list vaList;
  va_start(vaList, p_Format);
  Write("ERROR", p_Format, vaList);
  va_end(vaList);
}

void Log::Dump(const char *p_Str)
{
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_Path.empty())
  {
    SetPath("log.txt");
  }

  FILE* file = fopen(m_Path.c_str(), "a");
  if (file != NULL)
  {
    fprintf(file, "%s", p_Str);
    fclose(file);
  }
}

void Log::Write(const char *p_Level, const char *p_Format, va_list p_VaList)
{
  std::lock_guard<std::mutex> lock(m_Mutex);
  if (m_Path.empty())
  {
    SetPath("log.txt");
  }

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
    fprintf(file, "\n");
    fclose(file);
  }
}
