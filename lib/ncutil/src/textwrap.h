// textwrap.h
//
// Copyright (c) 2019-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

// Text wrapping based on display width. Self-contained, shared with nmail.
class TextWrap
{
public:
  typedef int (* ClusterWidthFunc)(const std::wstring& p_Str, size_t p_Begin, size_t p_End);
  typedef bool (* ClusterExtendFunc)(wchar_t p_Char);

  static void SetClusterWidthFunc(ClusterWidthFunc p_ClusterWidthFunc);
  static void SetClusterExtendFunc(ClusterExtendFunc p_ClusterExtendFunc);
  static size_t GetClusterEnd(const std::wstring& p_Str, size_t p_Pos);
  static int GetClusterWidth(const std::wstring& p_Str, size_t p_Begin, size_t p_End);
  static int GetWidth(const std::wstring& p_Str);
  static size_t GetPosAtColumn(const std::wstring& p_Str, int p_Column);
  static bool GetQuotePrefix(const std::wstring& p_String, std::wstring& p_Prefix, std::wstring& p_Line);
  static std::vector<std::wstring> WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                            bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                            bool p_QuoteWrap, int p_ExpandTabSize);
  static std::vector<std::wstring> WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                            bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                            bool p_QuoteWrap, int p_ExpandTabSize,
                                            int p_Pos, int& p_WrapLine, int& p_WrapPos,
                                            std::vector<int>* p_LineOffsets = nullptr);

private:
  static std::vector<std::wstring> WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                            bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                            bool p_QuoteWrap, int p_ExpandTabSize, bool p_CursorLine,
                                            int p_Pos, int& p_WrapLine, int& p_WrapPos,
                                            std::vector<int>* p_LineOffsets);

private:
  static ClusterWidthFunc m_ClusterWidthFunc;
  static ClusterExtendFunc m_ClusterExtendFunc;
};
