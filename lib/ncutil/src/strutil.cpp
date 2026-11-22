// strutil.cpp
//
// Copyright (c) 2020-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "strutil.h"

#include <algorithm>
#include <codecvt>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <locale>
#include <regex>
#include <string>

#include <termios.h>
#include <unistd.h>

#include "emojiutil.h"
#include "termutil.h"
#include "log.h"

void StrUtil::DeleteToNextMatch(std::wstring& p_Str, int& p_Pos, int p_Offs, std::wstring p_Chars)
{
  int searchPos = std::max(0, (p_Pos + p_Offs));
  size_t nextMatchPos = p_Str.find_first_of(p_Chars, searchPos);
  if (nextMatchPos != std::string::npos)
  {
    p_Str.erase(p_Pos, (nextMatchPos - searchPos + 1));
  }
  else
  {
    p_Str.erase(p_Pos);
  }

  p_Pos = std::min(p_Pos, (int)p_Str.size());
}

void StrUtil::DeleteToPrevMatch(std::wstring& p_Str, int& p_Pos, int p_Offs, std::wstring p_Chars)
{
  int searchPos = std::max(0, (p_Pos + p_Offs));
  size_t prevMatchPos = p_Str.find_last_of(p_Chars, searchPos);
  if (prevMatchPos == std::string::npos)
  {
    prevMatchPos = 0;
  }

  p_Str.erase(prevMatchPos, p_Pos - prevMatchPos);
  p_Pos = std::min((int)prevMatchPos, (int)p_Str.size());
}

std::string StrUtil::Emojize(const std::string& p_Str, bool p_Pad /*= false*/)
{
  return EmojiUtil::Emojize(p_Str, p_Pad);
}

std::string StrUtil::EscapeRawUrls(const std::string& p_Str)
{
  std::string str = p_Str;
  std::string rv;
  std::regex rg("\\(?\\[?(http|https):\\/\\/([^\\s]+)");
  std::smatch sm;
  while (regex_search(str, sm, rg))
  {
    rv += sm.prefix().str();

    std::string url = sm.str();
    if (url.size() >= 2)
    {
      if ((url.front() == '(') || (url.front() == '['))
      {
        rv += url;
      }
      else
      {
        rv += "[" + url + "]";
      }
    }
    str = sm.suffix();
  }

  rv += str;

  return rv;
}

std::string StrUtil::EscapeSingleQuote(const std::string& p_Str)
{
  // Make a string safe for use inside a single-quoted shell argument by
  // replacing each ' with the sequence '\'' (close quote, escaped quote, reopen quote).
  std::string str = p_Str;
  ReplaceString(str, "'", "'\\''");
  return str;
}

std::string StrUtil::ExtractString(const std::string& p_Str, const std::string& p_Prefix, const std::string& p_Suffix)
{
  std::size_t prefixPos = p_Str.find(p_Prefix);
  if (prefixPos != std::string::npos)
  {
    std::size_t suffixPos = p_Str.find(p_Suffix, prefixPos + p_Prefix.size());
    std::size_t len = (suffixPos != std::string::npos) ? (suffixPos - prefixPos - p_Prefix.size()) : std::string::npos;
    return p_Str.substr(prefixPos + p_Prefix.size(), len);
  }

  return "";
}

std::vector<std::string> StrUtil::ExtractUrlsFromStr(const std::string& p_Str)
{
  std::string str = p_Str;
  std::vector<std::string> rv;
  std::regex rg("\\(?(http|https):\\/\\/([^\\s]+)");
  std::smatch sm;
  while (regex_search(str, sm, rg))
  {
    std::string url = sm.str();
    if ((url.size() >= 2) && (url.front() == '('))
    {
      size_t closeParenthesis = url.find(')');
      if (closeParenthesis != std::string::npos)
      {
        url = url.substr(1, closeParenthesis - 1);
      }
    }
    rv.push_back(url);
    str = sm.suffix();
  }

  return rv;
}

std::string StrUtil::GetPass()
{
  std::string pass;
  struct termios told, tnew;

  if (tcgetattr(STDIN_FILENO, &told) == 0)
  {
    memcpy(&tnew, &told, sizeof(struct termios));
    tnew.c_lflag &= ~ECHO;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tnew) == 0)
    {
      std::getline(std::cin, pass);
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &told);
      std::cout << std::endl;
    }
  }

  return pass;
}

std::string StrUtil::GetPhoneNumber()
{
  // returns empty string if valid, otherwise the reason it is not
  auto checkPhoneNumber = [](const std::string& p_Str) -> std::string
  {
    if (p_Str.empty()) return "Phone number is required.";

    if (p_Str[0] != '+') return "Phone number must start with + and country code.";

    if ((p_Str.size() < 2) ||
        !std::all_of(p_Str.begin() + 1, p_Str.end(), [](const char& ch) {
      return std::isdigit(static_cast<unsigned char>(ch));
    }))
    {
      return "Phone number may only contain digits after +.";
    }

    return "";
  };

  // remove whitespace, e.g. spaces in "+65 1111 1111" or a pasted line ending
  auto cleanPhoneNumber = [](std::string p_Str)
  {
    p_Str.erase(std::remove_if(p_Str.begin(), p_Str.end(),
                               [](const char& ch) { return std::isspace(static_cast<unsigned char>(ch)); }),
                p_Str.end());
    return p_Str;
  };

  std::string defaultPhoneNumber;
  const char* envPhoneNumber = getenv("NCHAT_PHONE_NUMBER");
  if (envPhoneNumber != nullptr)
  {
    defaultPhoneNumber = cleanPhoneNumber(envPhoneNumber);
    if (!checkPhoneNumber(defaultPhoneNumber).empty())
    {
      LOG_WARNING("ignoring invalid NCHAT_PHONE_NUMBER \"%s\"", envPhoneNumber);
      defaultPhoneNumber.clear();
    }
  }

  std::cout << "\n";
  while (true)
  {
    if (defaultPhoneNumber.empty())
    {
      std::cout << "Phone number (ex. +6511111111): ";
    }
    else
    {
      std::cout << "Phone number (" << defaultPhoneNumber << "): ";
    }

    std::string str;
    if (!std::getline(std::cin, str))
    {
      std::cout << "\n";
      return "";
    }

    str = cleanPhoneNumber(str);
    if (str.empty() && !defaultPhoneNumber.empty())
    {
      return defaultPhoneNumber;
    }

    const std::string error = checkPhoneNumber(str);
    if (error.empty())
    {
      return str;
    }

    std::cout << error << "\n";
  }
}

std::string StrUtil::GetProtocolName(const std::string& p_ProfileId)
{
  std::vector<std::string> parts = StrUtil::Split(p_ProfileId, '_');
  return parts.empty() ? p_ProfileId : parts.at(0);
}

bool StrUtil::GetQuotePrefix(const std::wstring& p_String, std::wstring& p_Prefix, std::wstring& p_Line)
{
  std::wsmatch sm;
  std::wregex re(L"^(( *> *)+)");
  if (std::regex_search(p_String, sm, re))
  {
    p_Prefix = sm.str();
    p_Line = sm.suffix();
    return true;
  }
  else
  {
    p_Prefix.clear();
    p_Line = p_String;
    return false;
  }
}

bool StrUtil::IsInteger(const std::string& p_Str)
{
  return !p_Str.empty() && (p_Str.find_first_not_of("0123456789") == std::string::npos);
}

bool StrUtil::IsValidTextKey(int p_Key)
{
  return (p_Key >= 0x20);
}

std::string StrUtil::Join(const std::vector<std::string>& p_Lines, const std::string& p_Delim)
{
  std::string str;
  bool first = true;
  for (auto& line : p_Lines)
  {
    if (!first)
    {
      str += p_Delim;
    }
    else
    {
      first = false;
    }

    str += line;
  }
  return str;
}

std::wstring StrUtil::Join(const std::vector<std::wstring>& p_Lines, const std::wstring& p_Delim)
{
  std::wstring str;
  bool first = true;
  for (auto& line : p_Lines)
  {
    if (!first)
    {
      str += p_Delim;
    }
    else
    {
      first = false;
    }

    str += line;
  }
  return str;
}

void StrUtil::JumpToNextMatch(std::wstring& p_Str, int& p_Pos, int p_Offs, std::wstring p_Chars)
{
  int searchPos = std::max(0, (p_Pos + p_Offs));
  size_t nextMatchPos = p_Str.find_first_of(p_Chars, searchPos);
  if (nextMatchPos != std::string::npos)
  {
    p_Pos = std::min((int)nextMatchPos, (int)p_Str.size());
  }
  else
  {
    p_Pos = p_Str.size();
  }
}

void StrUtil::JumpToPrevMatch(std::wstring& p_Str, int& p_Pos, int p_Offs, std::wstring p_Chars)
{
  int searchPos = std::max(0, (p_Pos + p_Offs));
  size_t prevMatchPos = p_Str.find_last_of(p_Chars, searchPos);
  if (prevMatchPos != std::string::npos)
  {
    p_Pos = std::min((int)prevMatchPos + 1, (int)p_Str.size());
  }
  else
  {
    p_Pos = 0;
  }
}

std::string StrUtil::NumAddPrefix(const std::string& p_Str, const char p_Ch)
{
  std::string s = std::string(1, p_Ch);
  return StrToHex(s) + p_Str;
}

bool StrUtil::NumHasPrefix(const std::string& p_Str, const char p_Ch)
{
  std::string s = StrFromHex(p_Str);
  return (!s.empty() && (s.at(0) == p_Ch));
}

void StrUtil::ReplaceString(std::string& p_Str, const std::string& p_Search, const std::string& p_Replace)
{
  size_t pos = 0;
  while ((pos = p_Str.find(p_Search, pos)) != std::string::npos)
  {
    p_Str.replace(pos, p_Search.length(), p_Replace);
    pos += p_Replace.length();
  }
}

void StrUtil::SanitizeMessageStr(std::string& p_Str)
{
  p_Str.erase(std::remove(p_Str.begin(), p_Str.end(), '\r'), p_Str.end());
}

std::vector<std::string> StrUtil::Split(const std::string& p_Str, char p_Sep)
{
  std::vector<std::string> vec;
  std::stringstream ss(p_Str);
  while (ss.good())
  {
    std::string str;
    getline(ss, str, p_Sep);
    vec.push_back(str);
  }
  return vec;
}

bool StrUtil::StartsWith(const std::string& p_String, const std::string& p_Prefix)
{
  return (p_String.rfind(p_Prefix, 0) == 0);
}

std::string StrUtil::StrFromHex(const std::string& p_String)
{
  std::string result;
  std::istringstream iss(p_String);
  char buf[3] = { 0 };
  while (iss.read(buf, 2))
  {
    result += static_cast<char>(strtol(buf, NULL, 16) & 0xff);
  }

  return result;
}

std::string StrUtil::StrFromOct(const std::string& p_String)
{
  std::string rv;
  std::vector<std::string> parts = Split(p_String, '\\');
  for (auto& part : parts)
  {
    if (part.empty()) continue;

    int val = 0;
    std::istringstream(part) >> std::oct >> val;
    rv += (char)val;
  }

  return rv;
}

std::string StrUtil::StrToHex(const std::string& p_String)
{
  std::ostringstream oss;
  for (const char& ch : p_String)
  {
    char buf[3] = { 0 };
    snprintf(buf, sizeof(buf), "%02X", (unsigned char)ch);
    oss << buf;
  }

  return oss.str();
}

std::string StrUtil::Textize(const std::string& p_Str)
{
  return EmojiUtil::Textize(p_Str);
}

// Base letter folding table for Latin-1 Supplement (U+00C0 - U+00FF) and Latin Extended-A
// (U+0100 - U+017F), mapping each code point to its lowercase ascii base letter(s). Empty
// string means the code point is left as-is (the two math symbols in the range). Limited to
// these two blocks by design, leaving cjk, greek, cyrillic, etc untouched by base folding.
static const char* const s_LatinFold[] =
{
  // U+00C0 - U+00DF
  "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
  "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",
  // U+00E0 - U+00FF
  "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
  "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y",
  // U+0100 - U+011F
  "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
  "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
  // U+0120 - U+013F
  "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",
  "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",
  // U+0140 - U+015F
  "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",
  "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
  // U+0160 - U+017F
  "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",
  "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
};

std::string StrUtil::ToFold(const std::string& p_Str)
{
  static const wchar_t foldFirst = 0x00C0;
  static const wchar_t foldLast = foldFirst + (sizeof(s_LatinFold) / sizeof(*s_LatinFold)) - 1;

  // Fast path for ascii-only strings, which need neither decoding nor base letter folding
  if (std::none_of(p_Str.begin(), p_Str.end(),
                   [](char ch) { return static_cast<unsigned char>(ch) >= 0x80; }))
  {
    return ToLower(p_Str);
  }

  const std::wstring wstr = ToWString(p_Str);
  std::wstring fold;
  fold.reserve(wstr.size());
  for (const wchar_t wch : wstr)
  {
    if ((wch >= foldFirst) && (wch <= foldLast) && (s_LatinFold[wch - foldFirst][0] != '\0'))
    {
      for (const char* base = s_LatinFold[wch - foldFirst]; *base != '\0'; ++base)
      {
        fold += static_cast<wchar_t>(*base);
      }
    }
    else
    {
      // Handles case folding for scripts outside the table (greek, cyrillic, etc), based on
      // the locale set up by the ui. Code points without case mapping are left as-is.
      fold += static_cast<wchar_t>(towlower(static_cast<wint_t>(wch)));
    }
  }

  return ToString(fold);
}

long StrUtil::ToInteger(const std::string& p_Str)
{
  // positive integers only
  return strtol(p_Str.c_str(), NULL, 10);
}

// Ascii-only, as it operates on single bytes / code points. Use ToFold() for search matching.
std::string StrUtil::ToLower(const std::string& p_Str)
{
  std::string lower = p_Str;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](char ch) { return static_cast<char>(::tolower(static_cast<unsigned char>(ch))); });
  return lower;
}

std::wstring StrUtil::ToLower(const std::wstring& p_WStr)
{
  std::wstring lower = p_WStr;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](wchar_t wch) { return static_cast<wchar_t>(towlower(static_cast<wint_t>(wch))); });
  return lower;
}

std::string StrUtil::ToString(const std::wstring& p_WStr)
{
  try
  {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>{ }.to_bytes(p_WStr);
  }
  catch (...)
  {
    LOG_WARNING("failed to convert from utf-16");
    std::wstring wstr = p_WStr;
    wstr.erase(std::remove_if(wstr.begin(), wstr.end(), [](wchar_t wch) { return !isascii(wch); }), wstr.end());
    std::string str = std::string(wstr.begin(), wstr.end());
    return str;
  }
}

std::wstring StrUtil::ToWString(const std::string& p_Str)
{
  try
  {
    return std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>{ }.from_bytes(p_Str);
  }
  catch (...)
  {
    LOG_WARNING("failed to convert from utf-8");
    std::string str = p_Str;
    str.erase(std::remove_if(str.begin(), str.end(), [](unsigned char ch) { return !isascii(ch); }), str.end());
    std::wstring wstr = std::wstring(str.begin(), str.end());
    return wstr;
  }
}

void StrUtil::Trim(std::string& p_Str)
{
  static const std::string space = " ";
  const auto strBegin = p_Str.find_first_not_of(space);
  if (strBegin != std::string::npos)
  {
    const auto strEnd = p_Str.find_last_not_of(space);
    const auto strRange = strEnd - strBegin + 1;
    p_Str = p_Str.substr(strBegin, strRange);
  }
  else
  {
    p_Str.clear();
  }
}

// Returns the end of the character cluster starting at p_Pos, i.e. the base character
// followed by any code points modifying or joined to it, which must not be separated.
// This is a simplified subset of Unicode grapheme cluster rules (UAX #29), covering
// combining marks, variation selectors, emoji modifiers, keycaps, tags, zero width
// joiner sequences and regional indicator (flag) pairs.
size_t StrUtil::GetClusterEnd(const std::wstring& p_Str, size_t p_Pos)
{
  const size_t size = p_Str.size();
  if (p_Pos >= size) return size;

  auto isRegionalIndicator = [](wchar_t p_Wc) { return (p_Wc >= 0x1F1E6) && (p_Wc <= 0x1F1FF); };
  int regionalIndicators = isRegionalIndicator(p_Str[p_Pos]) ? 1 : 0;
  size_t pos = p_Pos + 1;
  while (pos < size)
  {
    const wchar_t wc = p_Str[pos];
    const wchar_t prev = p_Str[pos - 1];
    const bool isExtend =
      (wc == EMOJI_PAD) || // nchat entry padding after an inserted emoji
      (prev == 0x200D) || // any code point following a zero width joiner
      (wc == 0x200D) || // zero width joiner
      ((wc >= 0xFE00) && (wc <= 0xFE0F)) || // variation selectors
      ((wc >= 0x1F3FB) && (wc <= 0x1F3FF)) || // emoji skin tone modifiers
      (wc == 0x20E3) || // combining enclosing keycap
      ((wc >= 0xE0020) && (wc <= 0xE007F)) || // tags
      (wcwidth(wc) == 0); // combining marks
    if (isExtend)
    {
      ++pos;
      continue;
    }

    if (isRegionalIndicator(wc) && (regionalIndicators == 1))
    {
      regionalIndicators = 2; // second regional indicator completes the flag
      ++pos;
      continue;
    }

    break;
  }

  return pos;
}

// Returns the start of the character cluster containing p_Pos.
size_t StrUtil::GetClusterStart(const std::wstring& p_Str, size_t p_Pos)
{
  if (p_Pos >= p_Str.size()) return p_Str.size();

  const size_t lineStart = (p_Pos > 0) ? p_Str.rfind(L'\n', p_Pos - 1) : std::wstring::npos;
  size_t pos = (lineStart != std::wstring::npos) ? (lineStart + 1) : 0;
  while (true)
  {
    const size_t end = GetClusterEnd(p_Str, pos);
    if (end > p_Pos) return pos;

    pos = end;
  }
}

// Cursor movement in text entry. With emoji width handling enabled the cursor moves by
// character clusters (base, variation selector, modifiers, entry emoji padding),
// otherwise by character, skipping entry emoji padding.
int StrUtil::NextCursorPos(const std::wstring& p_Str, int p_Pos)
{
  const int size = (int)p_Str.size();
  if (p_Pos >= size) return size;

  if (TermUtil::GetProfile().enabled) return (int)GetClusterEnd(p_Str, std::max(p_Pos, 0));

  int pos = std::min(p_Pos + 1, size);
  if ((pos < size) && (p_Str.at(pos) == (wchar_t)EMOJI_PAD))
  {
    pos = std::min(pos + 1, size);
  }
  return pos;
}

int StrUtil::PrevCursorPos(const std::wstring& p_Str, int p_Pos)
{
  if (p_Pos <= 0) return 0;

  if (TermUtil::GetProfile().enabled) return (int)GetClusterStart(p_Str, std::min(p_Pos, (int)p_Str.size()) - 1);

  int pos = std::min(p_Pos, (int)p_Str.size()) - 1;
  if ((pos > 0) && (p_Str.at(pos) == (wchar_t)EMOJI_PAD))
  {
    pos = pos - 1;
  }
  return pos;
}

// Moves a cursor position that is inside a character cluster (or on entry emoji
// padding) to its start.
int StrUtil::SnapCursorPos(const std::wstring& p_Str, int p_Pos)
{
  if ((p_Pos <= 0) || (p_Pos >= (int)p_Str.size())) return std::max(0, std::min(p_Pos, (int)p_Str.size()));

  if (TermUtil::GetProfile().enabled) return (int)GetClusterStart(p_Str, p_Pos);

  return (p_Str.at(p_Pos) == (wchar_t)EMOJI_PAD) ? (p_Pos - 1) : p_Pos;
}

// Returns the longest prefix of p_Str with a width of at most p_Len, never
// splitting a character cluster.
std::wstring StrUtil::TrimWString(const std::wstring& p_Str, int p_Len)
{
  p_Len = std::max(p_Len, 0);
  int width = 0;
  size_t pos = 0;
  while (pos < p_Str.size())
  {
    const size_t end = GetClusterEnd(p_Str, pos);
    width += TermUtil::GetCluster(p_Str, pos, end).width;
    if (width > p_Len) break;

    pos = end;
  }

  return p_Str.substr(0, pos);
}

std::wstring StrUtil::TrimPadWString(const std::wstring& p_Str, int p_Len)
{
  p_Len = std::max(p_Len, 0);
  std::wstring str = TrimWString(p_Str, p_Len);
  const int width = WStringWidth(str);
  if (width < p_Len)
  {
    str += std::wstring(p_Len - width, ' ');
  }
  return str;
}

std::vector<std::wstring> StrUtil::WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                            bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                            bool p_QuoteWrap, int p_ExpandTabSize)
{
  int pos = 0;
  int wrapLine = 0;
  int wrapPos = 0;
  return WordWrap(p_Text, p_LineLength, p_ProcessFormatFlowed, p_OutputFormatFlowed, p_QuoteWrap, p_ExpandTabSize, pos,
                  wrapLine,
                  wrapPos);
}

std::vector<std::wstring> StrUtil::WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                            bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                            bool p_QuoteWrap, int p_ExpandTabSize,
                                            int p_Pos, int& p_WrapLine, int& p_WrapPos)
{
  std::vector<std::wstring> lines;

  p_WrapLine = 0;
  p_WrapPos = 0;

  const unsigned wrapLineLength = p_LineLength - 1; // lines with spaces allowed to width - 1
  const unsigned overflowLineLength = p_LineLength; // overflowing lines allowed to full width

  if (p_ProcessFormatFlowed)
  {
    bool prevLineFlowed = false;
    std::wstring line;
    std::wstring prevQuotePrefix;
    std::wstring quotePrefix;
    std::wstring prevUnquotedLine;
    std::wstring unquotedLine;
    std::wistringstream textss(p_Text);
    std::wostringstream outss;
    bool reflowUnquoted = true;
    while (std::getline(textss, line))
    {
      line.erase(std::remove(line.begin(), line.end(), L'\r'), line.end());

      if (!GetQuotePrefix(line, quotePrefix, unquotedLine))
      {
        if (reflowUnquoted)
        {
          if ((quotePrefix != prevQuotePrefix) || !prevLineFlowed)
          {
            outss << L"\n" << line;
          }
          else
          {
            if (!prevLineFlowed)
            {
              outss << L" ";
            }
            outss << line;
          }

          size_t unquotedLen = unquotedLine.size();
          prevLineFlowed = ((unquotedLen > 0) && (unquotedLine[unquotedLen - 1] == L' '));
        }
        else
        {
          outss << L"\n" << line;
        }
      }
      else
      {
        quotePrefix.erase(std::remove(quotePrefix.begin(), quotePrefix.end(), L' '), quotePrefix.end());

        if (quotePrefix != prevQuotePrefix)
        {
          outss << L"\n" << quotePrefix << L" " << unquotedLine;
        }
        else
        {
          if (unquotedLine.empty())
          {
            outss << L"\n" << quotePrefix << L" ";
          }
          else
          {
            if (prevUnquotedLine.empty())
            {
              outss << L"\n" << quotePrefix << L" ";
            }
            else
            {
              size_t prevUnquotedLen = prevUnquotedLine.size();
              if (prevUnquotedLine[prevUnquotedLen - 1] != L' ')
              {
                outss << L" ";
              }
            }

            outss << unquotedLine;
          }
        }
      }

      prevQuotePrefix = quotePrefix;
      prevUnquotedLine = unquotedLine;
    }

    p_Text = outss.str().substr(1);
  }

  if (p_ExpandTabSize > 0)
  {
    size_t pos = 0;
    const std::wstring wsearch = L"\t";
    while ((pos = p_Text.find(wsearch, pos)) != std::wstring::npos)
    {
      size_t lineStart = p_Text.rfind(L'\n', pos);
      if (lineStart == std::wstring::npos)
      {
        lineStart = 0;
      }

      const size_t tabColumn = pos - lineStart - 1;
      const int tabSpaces = (p_ExpandTabSize - (tabColumn % p_ExpandTabSize));
      std::wstring replace(tabSpaces, L' ');

      p_Text.replace(pos, wsearch.length(), replace);
      pos += replace.length();
    }
  }

  if (true)
  {
    std::wstring line;
    std::wistringstream textss(p_Text);
    const std::wstring flowedSuffix = p_OutputFormatFlowed ? L" " : L"";
    const size_t quotePrefixMaxLen = p_LineLength / 2;

    while (std::getline(textss, line))
    {
      std::wstring linePart = line;
      std::wstring quotePrefix;
      std::wstring tmpLine;
      size_t quotePrefixLen = 0;
      const bool hasQuotePrefix = p_QuoteWrap && GetQuotePrefix(linePart, quotePrefix, tmpLine);

      if (hasQuotePrefix)
      {
        quotePrefix.erase(std::remove(quotePrefix.begin(), quotePrefix.end(), L' '), quotePrefix.end());
        quotePrefix += L' ';
        quotePrefixLen = quotePrefix.size();
        if (quotePrefixLen > quotePrefixMaxLen)
        {
          quotePrefix = quotePrefix.substr(quotePrefixLen - quotePrefixMaxLen);
          quotePrefixLen = quotePrefix.size();
        }
        linePart = quotePrefix + tmpLine;
      }

      while (!linePart.empty() || line.empty()) // Ensure we handle empty lines
      {
        unsigned current_width = 0;
        std::wstring tmpline;
        size_t last_space = std::wstring::npos;
        bool lineWrapped = false;

        std::wstring tmpPrefix;
        if (hasQuotePrefix && !GetQuotePrefix(linePart, tmpPrefix, tmpLine))
        {
          linePart = quotePrefix + linePart;
        }

        // with emoji width enabled, a cluster's width is on its first char
        const bool emojiWidth = TermUtil::GetProfile().enabled;
        size_t clusterEnd = 0;
        for (size_t i = 0; i < linePart.size(); ++i)
        {
          wchar_t wc = linePart[i];
          unsigned char_width = std::max(wcwidth(wc), 1);
          if (emojiWidth)
          {
            if (i >= clusterEnd)
            {
              clusterEnd = GetClusterEnd(linePart, i);
              char_width = TermUtil::GetCluster(linePart, i, clusterEnd).width;
            }
            else
            {
              char_width = 0;
            }
          }

          // Track the most recent space for wrapping, ignoring spaces in the quote prefix
          if ((wc == L' ') && (i >= quotePrefixLen))
          {
            last_space = i;
          }

          // Check if adding this character exceeds the wrap length
          if (current_width + char_width > wrapLineLength)
          {
            if (last_space != std::wstring::npos)
            {
              // Wrap at the last space found
              lines.push_back(linePart.substr(0, last_space) + flowedSuffix);
              linePart = linePart.substr(last_space + 1);
            }
            else
            {
              // No space found, hard wrap after current character (single width char) or
              // before it (double width char). At least one char, to ensure progress.
              const size_t splitPos = std::max<size_t>(((char_width == 1) ? (emojiWidth ? clusterEnd : (i + 1)) : i),
                                                       1);
              lines.push_back(linePart.substr(0, splitPos));
              linePart = linePart.substr(splitPos);
            }

            // Reset for the next segment of the same original line
            lineWrapped = true;
            break;
          }

          tmpline += wc;
          current_width += char_width;
        }

        if (!lineWrapped)
        {
          // Add the final remaining part of the line, if we didn't wrap / split the line
          lines.push_back(linePart);
          linePart.clear();
        }

        if (linePart.empty()) break;

        if (line.empty()) break; // Handle original empty lines
      }
    }
  }

  // Update wrap position metadata
  for (auto& line : lines)
  {
    if (p_Pos > 0)
    {
      int lineLength = std::min((unsigned)line.size() + 1, overflowLineLength);
      if (lineLength <= p_Pos)
      {
        p_Pos -= lineLength;
        ++p_WrapLine;
      }
      else
      {
        p_WrapPos = p_Pos;
        p_Pos = 0;
      }
    }
  }

  return lines;
}

int StrUtil::WStringWidth(const std::wstring& p_WStr)
{
  if (TermUtil::GetProfile().enabled) return TermUtil::StringWidth(p_WStr);

  int width = wcswidth(p_WStr.c_str(), p_WStr.size());
  return (width != -1) ? width : p_WStr.size();
}
