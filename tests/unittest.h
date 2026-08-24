// unittest.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define ExpectEqual(t, a, b) ExpectEqualFun<t>(a, b, #a, #b, __FILE__, __LINE__)
#define ExpectTrue(a) ExpectTrueFun(a, #a, __FILE__, __LINE__)
#define ExpectFalse(a) ExpectTrueFun(!(a), "!" #a, __FILE__, __LINE__)

namespace unittest
{
  namespace detail
  {
    inline std::string FileName(const std::string& p_Path)
    {
      const std::size_t slash = p_Path.rfind("/");
      return (slash != std::string::npos) ? p_Path.substr(slash + 1) : p_Path;
    }

    // Wide strings are rendered as ascii with non-ascii chars escaped, so that
    // failure messages are readable regardless of terminal and locale.
    inline std::string ToDisplay(const std::wstring& p_Str)
    {
      std::stringstream ss;
      for (const wchar_t wc : p_Str)
      {
        if ((wc >= 0x20) && (wc < 0x7f))
        {
          ss << (char)wc;
        }
        else
        {
          ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << (int)wc;
        }
      }

      return ss.str();
    }

    inline std::string ToDisplay(const std::vector<std::wstring>& p_Lines)
    {
      std::stringstream ss;
      for (size_t i = 0; i < p_Lines.size(); ++i)
      {
        ss << std::endl << "    [" << i << "] \"" << ToDisplay(p_Lines[i]) << "\"";
      }

      return ss.str();
    }

    template<typename T>
    inline std::string ToDisplay(const T& p_Val)
    {
      std::stringstream ss;
      ss << std::setprecision(std::numeric_limits<long double>::digits10 + 1);
      ss << p_Val;
      return ss.str();
    }
  }

  template<typename T>
  inline void ExpectEqualFun(T p_Test, T p_Ref, const std::string& p_TestName,
                             const std::string& p_RefName, const std::string& p_FilePath, int p_LineNo)
  {
    if (p_Test != p_Ref)
    {
      std::stringstream ss;
      ss << detail::FileName(p_FilePath) << ":" << std::to_string(p_LineNo);
      ss << " ExpectEqual failed: " << p_TestName << " != " << p_RefName << std::endl;
      ss << p_TestName << " = '" << detail::ToDisplay(p_Test) << "'" << std::endl;
      ss << p_RefName << " = '" << detail::ToDisplay(p_Ref) << "'" << std::endl;

      throw std::runtime_error(ss.str());
    }
  }

  inline void ExpectTrueFun(bool p_Test, const std::string& p_TestName,
                            const std::string& p_FilePath, int p_LineNo)
  {
    if (!p_Test)
    {
      std::stringstream ss;
      ss << detail::FileName(p_FilePath) << ":" << std::to_string(p_LineNo);
      ss << " ExpectTrue failed: " << p_TestName << " == false" << std::endl;

      throw std::runtime_error(ss.str());
    }
  }
}
