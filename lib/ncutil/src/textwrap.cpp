// textwrap.cpp
//
// Copyright (c) 2019-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "textwrap.h"

#include <algorithm>
#include <regex>
#include <sstream>

#include <wchar.h>

TextWrap::ClusterWidthFunc TextWrap::m_ClusterWidthFunc = nullptr;
TextWrap::ClusterExtendFunc TextWrap::m_ClusterExtendFunc = nullptr;

// Sets a custom function for determining the display width of a character cluster,
// for use when the terminal is known to deviate from wcwidth().
void TextWrap::SetClusterWidthFunc(ClusterWidthFunc p_ClusterWidthFunc)
{
  m_ClusterWidthFunc = p_ClusterWidthFunc;
}

// Sets a custom function for determining additional characters to be kept together with
// the character preceding them.
void TextWrap::SetClusterExtendFunc(ClusterExtendFunc p_ClusterExtendFunc)
{
  m_ClusterExtendFunc = p_ClusterExtendFunc;
}

// Returns the end of the character cluster starting at p_Pos, i.e. the base character
// followed by any code points modifying or joined to it, which must not be separated.
// This is a simplified subset of Unicode grapheme cluster rules (UAX #29), covering
// combining marks, variation selectors, emoji modifiers, keycaps, tags, zero width
// joiner sequences and regional indicator (flag) pairs.
size_t TextWrap::GetClusterEnd(const std::wstring& p_Str, size_t p_Pos)
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
      (prev == 0x200D) || // any code point following a zero width joiner
      (wc == 0x200D) || // zero width joiner
      ((wc >= 0xFE00) && (wc <= 0xFE0F)) || // variation selectors
      ((wc >= 0x1F3FB) && (wc <= 0x1F3FF)) || // emoji skin tone modifiers
      (wc == 0x20E3) || // combining enclosing keycap
      ((wc >= 0xE0020) && (wc <= 0xE007F)) || // tags
      (wcwidth(wc) == 0) || // combining marks
      (wc == L'\r') || // carriage return, of text with crlf line breaks
      ((m_ClusterExtendFunc != nullptr) && m_ClusterExtendFunc(wc)); // custom
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

// Returns the display width of the character cluster at [p_Begin, p_End).
int TextWrap::GetClusterWidth(const std::wstring& p_Str, size_t p_Begin, size_t p_End)
{
  // carriage return takes up no column
  p_End = std::min(p_End, p_Str.size());
  while ((p_End > p_Begin) && (p_Str[p_End - 1] == L'\r'))
  {
    --p_End;
  }

  if (m_ClusterWidthFunc != nullptr) return m_ClusterWidthFunc(p_Str, p_Begin, p_End);

  // non-printable characters are counted as taking up one column
  int width = 0;
  for (size_t i = p_Begin; (i < p_End) && (i < p_Str.size()); ++i)
  {
    const int charWidth = wcwidth(p_Str[i]);
    width += (charWidth >= 0) ? charWidth : 1;
  }

  return width;
}

// Returns the display width of the string, as the sum of its character cluster widths.
int TextWrap::GetWidth(const std::wstring& p_Str)
{
  int width = 0;
  size_t pos = 0;
  while (pos < p_Str.size())
  {
    const size_t end = GetClusterEnd(p_Str, pos);
    width += GetClusterWidth(p_Str, pos, end);
    pos = end;
  }

  return width;
}

// Returns the position in p_Str of the character cluster at display column p_Column, or
// the string length if the string is not that wide.
size_t TextWrap::GetPosAtColumn(const std::wstring& p_Str, int p_Column)
{
  int width = 0;
  size_t pos = 0;
  while (pos < p_Str.size())
  {
    const size_t end = GetClusterEnd(p_Str, pos);
    width += GetClusterWidth(p_Str, pos, end);
    if (width > p_Column) break;

    pos = end;
  }

  return pos;
}

bool TextWrap::GetQuotePrefix(const std::wstring& p_String, std::wstring& p_Prefix, std::wstring& p_Line)
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

std::vector<std::wstring> TextWrap::WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                             bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                             bool p_QuoteWrap, int p_ExpandTabSize)
{
  const bool cursorLine = false;
  int pos = 0;
  int wrapLine = 0;
  int wrapPos = 0;
  return WordWrap(p_Text, p_LineLength, p_ProcessFormatFlowed, p_OutputFormatFlowed, p_QuoteWrap, p_ExpandTabSize,
                  cursorLine, pos, wrapLine, wrapPos, nullptr);
}

// Wraps text for use in an editor. The position p_Pos in p_Text is output as line
// p_WrapLine and position p_WrapPos in that line, and p_LineOffsets (optional) is set to
// the position in p_Text of the start of each line. These refer to p_Text after format
// flowed processing and tab expansion, and are exact when quote wrap is not used.
//
// A line filling the full width, or ending with the space it was wrapped at, is followed
// by an empty line for the position after it.
std::vector<std::wstring> TextWrap::WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                             bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                             bool p_QuoteWrap, int p_ExpandTabSize,
                                             int p_Pos, int& p_WrapLine, int& p_WrapPos,
                                             std::vector<int>* p_LineOffsets /*= nullptr*/)
{
  const bool cursorLine = true;
  return WordWrap(p_Text, p_LineLength, p_ProcessFormatFlowed, p_OutputFormatFlowed, p_QuoteWrap, p_ExpandTabSize,
                  cursorLine, p_Pos, p_WrapLine, p_WrapPos, p_LineOffsets);
}

// Wraps text to lines with a display width of at most p_LineLength, at spaces when possible
// and otherwise between character clusters. Lines wrapped at a space are limited to a width
// of p_LineLength - 1, leaving room for the cursor after the last character.
std::vector<std::wstring> TextWrap::WordWrap(std::wstring p_Text, unsigned p_LineLength,
                                             bool p_ProcessFormatFlowed, bool p_OutputFormatFlowed,
                                             bool p_QuoteWrap, int p_ExpandTabSize, bool p_CursorLine,
                                             int p_Pos, int& p_WrapLine, int& p_WrapPos,
                                             std::vector<int>* p_LineOffsets)
{
  std::vector<std::wstring> lines;
  std::vector<int> lineOffsets;

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

    const std::wstring outText = outss.str();
    p_Text = !outText.empty() ? outText.substr(1) : outText;
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
    size_t lineStart = 0; // position of line in text
    while (std::getline(textss, line))
    {
      std::wstring linePart = line;
      size_t linePos = 0; // position of line part in line

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
        size_t addedPrefixLen = 0;
        std::wstring tmpPrefix;
        if (hasQuotePrefix && !GetQuotePrefix(linePart, tmpPrefix, tmpLine))
        {
          linePart = quotePrefix + linePart;
          addedPrefixLen = quotePrefixLen;
        }

        // determine where to wrap, based on display width
        unsigned width = 0;
        size_t pos = 0;
        size_t spacePos = std::wstring::npos; // last space usable for wrapping
        size_t overflowPos = 0; // end of last character cluster fitting full width
        while (pos < linePart.size())
        {
          if ((linePart[pos] == L' ') && (width <= wrapLineLength) && (pos > quotePrefixLen))
          {
            spacePos = pos;
          }

          const size_t end = GetClusterEnd(linePart, pos);
          width += GetClusterWidth(linePart, pos, end);
          if (width > overflowLineLength) break;

          overflowPos = end;
          pos = end;
        }

        lineOffsets.push_back(lineStart + std::min(linePos, line.size()));

        if (width > wrapLineLength)
        {
          size_t partLen = 0;
          if (spacePos != std::wstring::npos)
          {
            // wrap at space
            lines.push_back(linePart.substr(0, spacePos) + flowedSuffix);
            partLen = spacePos + 1;
          }
          else
          {
            // no space found, wrap between character clusters. at least one after any quote
            // prefix, to ensure progress.
            const size_t minPos = std::min(hasQuotePrefix ? quotePrefixLen : 0, linePart.size() - 1);
            if (overflowPos <= minPos)
            {
              overflowPos = GetClusterEnd(linePart, minPos);
            }

            lines.push_back(linePart.substr(0, overflowPos));
            partLen = overflowPos;
          }

          linePart = (linePart.size() > partLen) ? linePart.substr(partLen) : std::wstring();
          linePos += partLen - std::min(partLen, addedPrefixLen);

          // empty line for the position after the wrapped line, only when wrapping for editor
          if (linePart.empty() && !p_CursorLine) break;
        }
        else
        {
          lines.push_back(linePart);
          linePart.clear();
          break;
        }
      }

      lineStart += line.size() + 1;
    }
  }

  // determine line and position in line, for specified position in text
  const bool isLastLineEmpty = !p_Text.empty() && (p_Text.back() == L'\n');
  if (isLastLineEmpty && (p_Pos >= (int)p_Text.size()))
  {
    // last line is empty, and thus not part of lines
    p_WrapLine = lines.size();
  }
  else if (p_Pos > 0)
  {
    for (size_t i = 0; i < lines.size(); ++i)
    {
      if (lineOffsets[i] > p_Pos) break;

      p_WrapLine = i;
    }

    if (!lines.empty())
    {
      p_WrapPos = std::min(p_Pos - lineOffsets[p_WrapLine], (int)lines[p_WrapLine].size());
    }
  }

  if (p_LineOffsets != nullptr)
  {
    *p_LineOffsets = lineOffsets;
  }

  return lines;
}
