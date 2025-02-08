// strutil.cpp
//
// Copyright (c) 2020-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "strutil.h"

#include <codecvt>
#include <cstring>
#include <fstream>
#include <iostream>
#include <locale>
#include <regex>
#include <string>

#include <termios.h>
#include <unistd.h>

#include "emojiutil.h"
#include "log.h"

#define HAS_WCSWIDTH_WORDWRAP

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
  std::string str;
  std::cout << "Enter phone number (ex. +6511111111): ";
  std::getline(std::cin, str);
  str.erase(std::remove_if(str.begin(), str.end(),
                           [](const char& ch) { return !std::isdigit(ch) && (ch != '+'); }),
            str.end());
  return str;
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

long StrUtil::ToInteger(const std::string& p_Str)
{
  // positive integers only
  return strtol(p_Str.c_str(), NULL, 10);
}

std::string StrUtil::ToLower(const std::string& p_Str)
{
  std::string lower = p_Str;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower;
}

std::wstring StrUtil::ToLower(const std::wstring& p_WStr)
{
  std::wstring lower = p_WStr;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
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

std::wstring StrUtil::TrimPadWString(const std::wstring& p_Str, int p_Len)
{
  p_Len = std::max(p_Len, 0);
  std::wstring str = p_Str;
  if (WStringWidth(str) > p_Len)
  {
    str = str.substr(0, p_Len);
    int subLen = p_Len;
    while (WStringWidth(str) > p_Len)
    {
      str = str.substr(0, --subLen);
    }
  }
  else if (WStringWidth(str) < p_Len)
  {
    str = str + std::wstring(p_Len - WStringWidth(str), ' ');
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
  std::wostringstream wrapped;
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
    std::wstring prevQuotePrefix;
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

      while (true)
      {
        std::wstring tmpPrefix;
        if (hasQuotePrefix && !GetQuotePrefix(linePart, tmpPrefix, tmpLine))
        {
          linePart = quotePrefix + linePart;
        }

#ifdef HAS_WCSWIDTH_WORDWRAP
        unsigned current_width = 0;
        std::wstring tmpline;
        size_t last_space = std::wstring::npos;
        for (size_t i = 0; i < linePart.size(); ++i)
        {
          wchar_t wc = linePart[i];
          unsigned char_width = std::max(wcwidth(wc), 1);

          // If the character is a space, mark it as a potential wrap point
          if (iswspace(wc))
          {
            last_space = tmpline.size();
          }

          // Check if adding the character exceeds the width
          if (current_width + char_width > wrapLineLength)
          {
            if (last_space != std::wstring::npos)
            {
              // Wrap at the last space
              lines.push_back(tmpline.substr(0, last_space));

              // Start the new line with the remainder after the space
              tmpline = tmpline.substr(std::min(last_space + 1, tmpline.size()));
              current_width = 0;
              for (wchar_t c : tmpline)
              {
                current_width += std::max(wcwidth(c), 1);
              }

              last_space = std::wstring::npos;
            }
            else
            {
              // No space found, wrap at the current position
              lines.push_back(tmpline);
              tmpline.clear();
              current_width = 0;
            }
          }

          // Add the character to the current line and update the width
          if (!tmpline.empty() || !iswspace(wc))
          {
            tmpline += wc;
            current_width += char_width;
          }
        }

        // Add any remaining text
        if (!tmpline.empty())
        {
          lines.push_back(tmpline);
        }

        break;
#else
        if (linePart.size() > wrapLineLength)
        {
          size_t spacePos = linePart.rfind(L' ', wrapLineLength);
          if ((spacePos != std::wstring::npos) && (spacePos > quotePrefixLen))
          {
            lines.push_back(linePart.substr(0, spacePos) + flowedSuffix);
            if (linePart.size() > (spacePos + 1))
            {
              linePart = linePart.substr(spacePos + 1);
            }
            else
            {
              linePart.clear();
            }
          }
          else
          {
            lines.push_back(linePart.substr(0, overflowLineLength));
            if (linePart.size() > overflowLineLength)
            {
              linePart = linePart.substr(overflowLineLength);
            }
            else
            {
              linePart.clear();
            }
          }
        }
        else
        {
          lines.push_back(linePart);
          linePart.clear();
          break;
        }
#endif
      }
    }
  }

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
  int width = wcswidth(p_WStr.c_str(), p_WStr.size());
  return (width != -1) ? width : p_WStr.size();
}
