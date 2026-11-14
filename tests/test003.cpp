// test003.cpp - telegram rich message to text conversion
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <clocale>
#include <iostream>
#include <string>
#include <utility>

#include <td/telegram/td_api.h>

#include "tgrichmessage.h"

#include "unittest.h"

using namespace td::td_api;

typedef array<object_ptr<PageBlock>> Blocks;

static object_ptr<RichText> Plain(const std::string& p_Text)
{
  return make_object<richTextPlain>(p_Text);
}

static object_ptr<PageBlock> Paragraph(const std::string& p_Text)
{
  return make_object<pageBlockParagraph>(Plain(p_Text));
}

static object_ptr<pageBlockTableCell> Cell(const std::string& p_Text, bool p_IsHeader = false,
                                           int32 p_Colspan = 1)
{
  return make_object<pageBlockTableCell>(Plain(p_Text), p_IsHeader, p_Colspan, 1, nullptr, nullptr);
}

static object_ptr<pageBlockListItem> Item(const std::string& p_Label, Blocks&& p_Blocks,
                                          bool p_HasCheckbox = false, bool p_IsChecked = false)
{
  return make_object<pageBlockListItem>(p_Label, std::move(p_Blocks), p_HasCheckbox, p_IsChecked, 0, "");
}

static Blocks MakeBlocks(object_ptr<PageBlock>&& p_Block1, object_ptr<PageBlock>&& p_Block2 = nullptr,
                         object_ptr<PageBlock>&& p_Block3 = nullptr)
{
  Blocks blocks;
  blocks.push_back(std::move(p_Block1));
  if (p_Block2) blocks.push_back(std::move(p_Block2));
  if (p_Block3) blocks.push_back(std::move(p_Block3));
  return blocks;
}

static std::string ToText(Blocks&& p_Blocks, bool p_IsFull = true)
{
  richMessage message(std::move(p_Blocks), false, p_IsFull);
  return TgRichMessage::ToText(message);
}

static void TestText()
{
  // headings get markdown prefix, multi-line paragraphs keep each line
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockTitle>(Plain("Title")),
                                          make_object<pageBlockSectionHeading>(Plain("Section"), 3),
                                          Paragraph("line one\nline two"))),
                        "# Title\n### Section\nline one\nline two");

  // links shown in markdown form, unless text is the url itself
  array<object_ptr<RichText>> texts;
  texts.push_back(make_object<richTextUrl>(Plain("nchat"), "https://github.com/d99kris/nchat", false));
  texts.push_back(Plain(" and "));
  texts.push_back(make_object<richTextUrl>(Plain("https://example.com"), "https://example.com", false));
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockParagraph>(make_object<richTexts>(std::move(texts))))),
                        "[nchat](https://github.com/d99kris/nchat) and https://example.com");
}

static void TestTable()
{
  // header row gives separator, cells padded to column width, pipes escaped
  array<array<object_ptr<pageBlockTableCell>>> cells(3);
  cells[0].push_back(Cell("Name", true));
  cells[0].push_back(Cell("Value", true));
  cells[1].push_back(Cell("alpha"));
  cells[1].push_back(Cell("1"));
  cells[2].push_back(Cell("b|c"));
  cells[2].push_back(Cell("22"));
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockTable>(Plain("Caption"), std::move(cells), true, false,
                                                                      false))),
                        "Caption\n"
                        "| Name  | Value |\n"
                        "|-------|-------|\n"
                        "| alpha | 1     |\n"
                        "| b\\|c  | 22    |");

  // no header row gives no separator, null cell is empty, colspan keeps columns aligned
  array<array<object_ptr<pageBlockTableCell>>> cells2(2);
  cells2[0].push_back(Cell("wide", false, 2));
  cells2[0].push_back(Cell("x"));
  cells2[1].push_back(Cell("a"));
  cells2[1].push_back(nullptr);
  cells2[1].push_back(Cell("y"));
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockTable>(nullptr, std::move(cells2), false, false,
                                                                      false))),
                        "| wide |  | x |\n"
                        "| a    |  | y |");
}

static void TestList()
{
  // nested list items indented by label width, without double indent
  Blocks nested = MakeBlocks(make_object<pageBlockList>(
                               [] {
    array<object_ptr<pageBlockListItem>> items;
    items.push_back(Item("", MakeBlocks(Paragraph("inner"))));
    return items;
  }()));

  array<object_ptr<pageBlockListItem>> items;
  items.push_back(Item("1.", MakeBlocks(Paragraph("first"), Paragraph("more"))));
  items.push_back(Item("2.", std::move(nested)));
  items.push_back(Item("", MakeBlocks(Paragraph("todo")), true, false));
  items.push_back(Item("", MakeBlocks(Paragraph("done")), true, true));
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockList>(std::move(items)))),
                        "1. first\n"
                        "   more\n"
                        "2. - inner\n"
                        "- [ ] todo\n"
                        "- [x] done");
}

static void TestBlocks()
{
  // code fenced, quotes prefixed on each line with credit
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockPreformatted>(Plain("int a;\nint b;"), "cpp"),
                                          make_object<pageBlockBlockQuote>(MakeBlocks(Paragraph("quoted\ntext")),
                                                                           Plain("someone")))),
                        "```cpp\nint a;\nint b;\n```\n> quoted\n> text\n> - someone");

  // embedded post and slideshow contents shown (previously misread as collage)
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockEmbeddedPost>("https://t.me/x/1", "Author", nullptr, 0,
                                                                             MakeBlocks(Paragraph("post")), nullptr),
                                          make_object<pageBlockSlideshow>(MakeBlocks(Paragraph("slide")),
                                                                          make_object<pageBlockCaption>(Plain("cap"),
                                                                                                        nullptr)))),
                        "Author:\n> post\nslide\ncap");

  // media shown as placeholder with caption and credit
  unittest::ExpectEqual(std::string,
                        ToText(MakeBlocks(make_object<pageBlockPhoto>(nullptr,
                                                                      make_object<pageBlockCaption>(Plain("A cat"),
                                                                                                    Plain("me")),
                                                                      "", false),
                                          make_object<pageBlockVideo>(nullptr, nullptr, false, false, false))),
                        "[Photo] A cat - me\n[Video]");
}

static void TestTruncated()
{
  // partial message marked as truncated
  unittest::ExpectEqual(std::string, ToText(MakeBlocks(Paragraph("start")), false), "start\n[Truncated]");
  unittest::ExpectEqual(std::string, ToText(Blocks(), false), "[Truncated]");
  unittest::ExpectEqual(std::string, ToText(Blocks(), true), "");
}

int main()
{
  int rv = 0;

  setlocale(LC_ALL, "");

  try
  {
    TestText();
    TestTable();
    TestList();
    TestBlocks();
    TestTruncated();
  }
  catch (const std::exception& ex)
  {
    std::cout << "exception: " << ex.what() << std::endl;
    rv = 1;
  }

  return rv;
}
