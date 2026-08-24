// test001.cpp - word wrap and quote prefix handling
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <clocale>
#include <iostream>
#include <string>
#include <vector>

#include <wchar.h>

#include "strutil.h"

#include "unittest.h"

typedef std::vector<std::wstring> WLines;

static void TestGetQuotePrefix()
{
  std::wstring prefix;
  std::wstring line;

  unittest::ExpectTrue(StrUtil::GetQuotePrefix(L"> hello", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L"> ");
  unittest::ExpectEqual(std::wstring, line, L"hello");

  unittest::ExpectTrue(StrUtil::GetQuotePrefix(L">> hello", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L">> ");
  unittest::ExpectEqual(std::wstring, line, L"hello");

  unittest::ExpectTrue(StrUtil::GetQuotePrefix(L"> > hello", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L"> > ");
  unittest::ExpectEqual(std::wstring, line, L"hello");

  unittest::ExpectTrue(StrUtil::GetQuotePrefix(L"  > x", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L"  > ");
  unittest::ExpectEqual(std::wstring, line, L"x");

  unittest::ExpectTrue(StrUtil::GetQuotePrefix(L">", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L">");
  unittest::ExpectEqual(std::wstring, line, L"");

  unittest::ExpectFalse(StrUtil::GetQuotePrefix(L"hello", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L"");
  unittest::ExpectEqual(std::wstring, line, L"hello");

  unittest::ExpectFalse(StrUtil::GetQuotePrefix(L"", prefix, line));
  unittest::ExpectEqual(std::wstring, prefix, L"");
  unittest::ExpectEqual(std::wstring, line, L"");
}

static void TestWordWrap()
{
  // plain text wraps at spaces
  const WLines plain = { L"the quick brown fox", L"jumps over the lazy", L"dog" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"the quick brown fox jumps over the lazy dog", 20,
                                          false, false, false, 2), plain);

  // text without spaces is hard wrapped
  const WLines longWord = { L"abcdefghij", L"klmnopqrst", L"uvwxyz" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"abcdefghijklmnopqrstuvwxyz", 10,
                                          false, false, false, 2), longWord);

  // empty text and empty lines
  unittest::ExpectEqual(WLines, StrUtil::WordWrap(L"", 20, false, false, false, 2), WLines());
  const WLines emptyLines = { L"a", L"", L"b" };
  unittest::ExpectEqual(WLines, StrUtil::WordWrap(L"a\n\nb", 20, false, false, false, 2), emptyLines);

  // tabs are expanded to next tab stop
  const WLines tabs = { L"ab   cd  e" };
  unittest::ExpectEqual(WLines, StrUtil::WordWrap(L"ab\tcd\te", 40, false, false, false, 4), tabs);

  // format flowed input is reflowed before wrapping
  const WLines flowed = { L"one two three four", L"five" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"one two \nthree four\nfive", 20,
                                          true, false, false, 2), flowed);
}

static void TestWordWrapQuoted()
{
  // quote prefix is repeated on each wrapped line
  const WLines quoted = { L"> the quick brown fox jumps over the", L"> lazy dog" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"> the quick brown fox jumps over the lazy dog", 40,
                                          false, false, true, 2), quoted);

  // without quote wrap the prefix is treated as regular text
  const WLines unquoted = { L"> the quick brown fox jumps over the", L"lazy dog" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"> the quick brown fox jumps over the lazy dog", 40,
                                          false, false, false, 2), unquoted);

  // nested quote prefix is normalized and repeated
  const WLines nested = { L">> deeply quoted text that", L">> needs wrapping here" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"> > deeply quoted text that needs wrapping here", 30,
                                          false, false, true, 2), nested);

  // quoted text without spaces must hard wrap, and not wrap at the prefix space
  // (which previously caused an infinite loop, hanging export)
  const WLines quotedUrl =
  {
    L"> https://open.spotify.com/artist/1NwpmqCXeqQ4xrE6WEP4np?si=abcdefgh1234",
    L"> 5678"
  };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(
                          L"> https://open.spotify.com/artist/1NwpmqCXeqQ4xrE6WEP4np?si=abcdefgh12345678", 72,
                          false, false, true, 2), quotedUrl);
}

static void TestWordWrapPos()
{
  // position 25 of the input is on wrapped line 1, at position 5
  int wrapLine = -1;
  int wrapPos = -1;
  const WLines lines = { L"the quick brown fox", L"jumps over the lazy", L"dog" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"the quick brown fox jumps over the lazy dog", 20,
                                          false, false, false, 2, 25, wrapLine, wrapPos), lines);
  unittest::ExpectEqual(int, wrapLine, 1);
  unittest::ExpectEqual(int, wrapPos, 5);
}

static void TestWordWrapWide()
{
  // double width characters are wrapped by display width, not char count
  const WLines wide = { L"你好世界", L"你好世界" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"你好世界你好世界", 10,
                                          false, false, false, 2), wide);

  const WLines wideQuoted = { L"> 你好世", L"> 界你好" };
  unittest::ExpectEqual(WLines,
                        StrUtil::WordWrap(L"> 你好世界你好", 10,
                                          false, false, true, 2), wideQuoted);
}

int main()
{
  int rv = 0;

  setlocale(LC_ALL, "");

  try
  {
    TestGetQuotePrefix();
    TestWordWrap();
    TestWordWrapQuoted();
    TestWordWrapPos();

    // wide character width depends on a utf-8 capable locale being present
    if (wcwidth(L'你') == 2)
    {
      TestWordWrapWide();
    }
    else
    {
      std::cout << "skipping wide character tests, no utf-8 locale" << std::endl;
    }
  }
  catch (const std::exception& ex)
  {
    std::cout << "exception: " << ex.what() << std::endl;
    rv = 1;
  }

  return rv;
}
