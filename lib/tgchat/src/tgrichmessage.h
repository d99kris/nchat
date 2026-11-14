// tgrichmessage.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include <td/telegram/td_api.h>

// Conversion of Telegram rich messages to text, using markdown for block structure
// (headings, tables, code blocks, lists and quotes) so it stays readable as plain text.
class TgRichMessage
{
public:
  static std::string ToText(const td::td_api::richMessage& p_RichMessage);

private:
  typedef std::vector<td::td_api::object_ptr<td::td_api::PageBlock>> PageBlocks;

  static std::string GetRichText(const td::td_api::RichText* p_RichText);
  static std::string GetCaptionText(const td::td_api::pageBlockCaption* p_Caption);
  static void AppendLines(const std::string& p_Text, const std::string& p_Indent, std::string& p_Out);
  static void AppendBlocks(const PageBlocks& p_Blocks, const std::string& p_Indent, std::string& p_Out);
  static void AppendBlock(const td::td_api::PageBlock& p_Block, const std::string& p_Indent, std::string& p_Out);
  static void AppendMedia(const std::string& p_Label, const td::td_api::pageBlockCaption* p_Caption,
                          const std::string& p_Indent, std::string& p_Out);
  static void AppendList(const td::td_api::pageBlockList& p_List, const std::string& p_Indent, std::string& p_Out);
  static void AppendTable(const td::td_api::pageBlockTable& p_Table, const std::string& p_Indent,
                          std::string& p_Out);
};
