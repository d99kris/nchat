// strutil.h
//
// Copyright (c) 2020-2022 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <sstream>
#include <string>
#include <vector>

class StrUtil
{
public:
  static void DeleteFromMatch(std::wstring& p_Str, int& p_EndPos, const wchar_t p_StartChar);
  static void DeleteToMatch(std::wstring& p_Str, const int p_StartPos, const wchar_t p_EndChar);
  static std::string Emojize(const std::string& p_Str);
  static std::vector<std::string> ExtractUrlsFromStr(const std::string& p_Str);
  static std::string GetPass();
  static bool GetQuotePrefix(const std::wstring& p_String, std::wstring& p_Prefix, std::wstring& p_Line);
  static bool IsInteger(const std::string& p_Str);
  static bool IsValidTextKey(int p_Key);
  static std::string Join(const std::vector<std::string>& p_Lines, const std::string& p_Delim);
  static std::wstring Join(const std::vector<std::wstring>& p_Lines, const std::wstring& p_Delim);
  static std::string NumAddPrefix(const std::string& p_Str, const char p_Ch);
  static bool NumHasPrefix(const std::string& p_Str, const char p_Ch);
  static void ReplaceString(std::string& p_Str, const std::string& p_Search, const std::string& p_Replace);
  static std::vector<std::string> Split(const std::string& p_Str, char p_Sep);
  static std::string StrFromHex(const std::string& p_String);
  static std::string StrToHex(const std::string& p_String);
  static std::string Textize(const std::string& p_Str);
  static long ToInteger(const std::string& p_Str);
  static std::string ToLower(const std::string& p_Str);
  static std::wstring ToLower(const std::wstring& p_WStr);
  static std::string ToString(const std::wstring& p_WStr);
  static std::wstring ToWString(const std::string& p_Str);
  static std::wstring TrimPadWString(const std::wstring& p_Str, int p_Len);
  static std::vector<std::wstring> WordWrap(std::wstring p_Text, unsigned p_LineLength, bool p_ProcessFormatFlowed,
                                            bool p_OutputFormatFlowed, bool p_QuoteWrap, int p_ExpandTabSize);
  static std::vector<std::wstring> WordWrap(std::wstring p_Text, unsigned p_LineLength, bool p_ProcessFormatFlowed,
                                            bool p_OutputFormatFlowed, bool p_QuoteWrap, int p_ExpandTabSize, int p_Pos,
                                            int& p_WrapLine, int& p_WrapPos);
  static int WStringWidth(const std::wstring& p_WStr);

public:
  template<typename T>
  static inline std::string NumToHex(const T& p_Value)
  {
    std::stringstream ss;
    ss << p_Value;
    return StrToHex(ss.str());
  }

  template<typename T>
  static inline T NumFromHex(const std::string& p_Str)
  {
    std::stringstream ss(StrFromHex(p_Str));
    T value = 0;
    ss >> value;
    return value;
  }
};
