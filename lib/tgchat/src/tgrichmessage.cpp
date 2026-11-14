// tgrichmessage.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "tgrichmessage.h"

#include <algorithm>

#include "strutil.h"

std::string TgRichMessage::ToText(const td::td_api::richMessage& p_RichMessage)
{
  std::string text;
  AppendBlocks(p_RichMessage.blocks_, "", text);

  while (!text.empty() && ((text.back() == '\n') || (text.back() == ' ')))
  {
    text.pop_back();
  }

  // partial message, full one requires getFullRichMessage
  if (!p_RichMessage.is_full_)
  {
    text += text.empty() ? "[Truncated]" : "\n[Truncated]";
  }

  return text;
}

std::string TgRichMessage::GetRichText(const td::td_api::RichText* p_RichText)
{
  if (!p_RichText) return "";

  switch (p_RichText->get_id())
  {
    case td::td_api::richTextPlain::ID:
      return static_cast<const td::td_api::richTextPlain*>(p_RichText)->text_;
    case td::td_api::richTextCustomEmoji::ID:
      return static_cast<const td::td_api::richTextCustomEmoji*>(p_RichText)->alternative_text_;
    case td::td_api::richTextMathematicalExpression::ID:
      return static_cast<const td::td_api::richTextMathematicalExpression*>(p_RichText)->expression_;
    case td::td_api::richTextIcon::ID:
    case td::td_api::richTextAnchor::ID:
      return "";
    case td::td_api::richTexts::ID:
      {
        std::string text;
        for (const auto& sub : static_cast<const td::td_api::richTexts*>(p_RichText)->texts_)
        {
          text += GetRichText(sub.get());
        }
        return text;
      }
    case td::td_api::richTextUrl::ID:
      {
        const auto* richTextUrl = static_cast<const td::td_api::richTextUrl*>(p_RichText);
        const std::string text = GetRichText(richTextUrl->text_.get());
        if (richTextUrl->url_.empty() || (text == richTextUrl->url_)) return text;

        if (text.empty()) return richTextUrl->url_;

        return "[" + text + "](" + richTextUrl->url_ + ")";
      }
    case td::td_api::richTextBold::ID:
      return GetRichText(static_cast<const td::td_api::richTextBold*>(p_RichText)->text_.get());
    case td::td_api::richTextItalic::ID:
      return GetRichText(static_cast<const td::td_api::richTextItalic*>(p_RichText)->text_.get());
    case td::td_api::richTextUnderline::ID:
      return GetRichText(static_cast<const td::td_api::richTextUnderline*>(p_RichText)->text_.get());
    case td::td_api::richTextStrikethrough::ID:
      return GetRichText(static_cast<const td::td_api::richTextStrikethrough*>(p_RichText)->text_.get());
    case td::td_api::richTextSpoiler::ID:
      return GetRichText(static_cast<const td::td_api::richTextSpoiler*>(p_RichText)->text_.get());
    case td::td_api::richTextSubscript::ID:
      return GetRichText(static_cast<const td::td_api::richTextSubscript*>(p_RichText)->text_.get());
    case td::td_api::richTextSuperscript::ID:
      return GetRichText(static_cast<const td::td_api::richTextSuperscript*>(p_RichText)->text_.get());
    case td::td_api::richTextMarked::ID:
      return GetRichText(static_cast<const td::td_api::richTextMarked*>(p_RichText)->text_.get());
    case td::td_api::richTextFixed::ID:
      return GetRichText(static_cast<const td::td_api::richTextFixed*>(p_RichText)->text_.get());
    case td::td_api::richTextDateTime::ID:
      return GetRichText(static_cast<const td::td_api::richTextDateTime*>(p_RichText)->text_.get());
    case td::td_api::richTextMention::ID:
      return GetRichText(static_cast<const td::td_api::richTextMention*>(p_RichText)->text_.get());
    case td::td_api::richTextHashtag::ID:
      return GetRichText(static_cast<const td::td_api::richTextHashtag*>(p_RichText)->text_.get());
    case td::td_api::richTextCashtag::ID:
      return GetRichText(static_cast<const td::td_api::richTextCashtag*>(p_RichText)->text_.get());
    case td::td_api::richTextBotCommand::ID:
      return GetRichText(static_cast<const td::td_api::richTextBotCommand*>(p_RichText)->text_.get());
    case td::td_api::richTextMentionName::ID:
      return GetRichText(static_cast<const td::td_api::richTextMentionName*>(p_RichText)->text_.get());
    case td::td_api::richTextEmailAddress::ID:
      return GetRichText(static_cast<const td::td_api::richTextEmailAddress*>(p_RichText)->text_.get());
    case td::td_api::richTextPhoneNumber::ID:
      return GetRichText(static_cast<const td::td_api::richTextPhoneNumber*>(p_RichText)->text_.get());
    case td::td_api::richTextBankCardNumber::ID:
      return GetRichText(static_cast<const td::td_api::richTextBankCardNumber*>(p_RichText)->text_.get());
    case td::td_api::richTextAnchorLink::ID:
      return GetRichText(static_cast<const td::td_api::richTextAnchorLink*>(p_RichText)->text_.get());
    case td::td_api::richTextReferenceLink::ID:
      return GetRichText(static_cast<const td::td_api::richTextReferenceLink*>(p_RichText)->text_.get());
    case td::td_api::richTextReference::ID:
      return GetRichText(static_cast<const td::td_api::richTextReference*>(p_RichText)->text_.get());
    default:
      return "";
  }
}

std::string TgRichMessage::GetCaptionText(const td::td_api::pageBlockCaption* p_Caption)
{
  if (!p_Caption) return "";

  std::string text = GetRichText(p_Caption->text_.get());
  const std::string credit = GetRichText(p_Caption->credit_.get());
  if (!credit.empty())
  {
    text += text.empty() ? credit : (" - " + credit);
  }

  return text;
}

// append each line of text with indent, trailing spaces trimmed
void TgRichMessage::AppendLines(const std::string& p_Text, const std::string& p_Indent, std::string& p_Out)
{
  size_t pos = 0;
  while (true)
  {
    const size_t end = p_Text.find('\n', pos);
    std::string line = p_Indent + p_Text.substr(pos, (end == std::string::npos) ? end : (end - pos));
    while (!line.empty() && (line.back() == ' '))
    {
      line.pop_back();
    }

    p_Out += line + "\n";
    if (end == std::string::npos) break;

    pos = end + 1;
  }
}

void TgRichMessage::AppendBlocks(const PageBlocks& p_Blocks, const std::string& p_Indent, std::string& p_Out)
{
  for (const auto& block : p_Blocks)
  {
    if (block) AppendBlock(*block, p_Indent, p_Out);
  }
}

void TgRichMessage::AppendBlock(const td::td_api::PageBlock& p_Block, const std::string& p_Indent,
                                std::string& p_Out)
{
  switch (p_Block.get_id())
  {
    case td::td_api::pageBlockTitle::ID:
      AppendLines("# " + GetRichText(static_cast<const td::td_api::pageBlockTitle&>(p_Block).title_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockSubtitle::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockSubtitle&>(p_Block).subtitle_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockAuthorDate::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockAuthorDate&>(p_Block).author_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockHeader::ID:
      AppendLines("## " + GetRichText(static_cast<const td::td_api::pageBlockHeader&>(p_Block).header_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockSubheader::ID:
      AppendLines("### " +
                  GetRichText(static_cast<const td::td_api::pageBlockSubheader&>(p_Block).subheader_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockSectionHeading::ID:
      {
        const auto& heading = static_cast<const td::td_api::pageBlockSectionHeading&>(p_Block);
        const int level = std::min(std::max(heading.size_, 1), 6);
        AppendLines(std::string(level, '#') + " " + GetRichText(heading.text_.get()), p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockKicker::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockKicker&>(p_Block).kicker_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockParagraph::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockParagraph&>(p_Block).text_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockFooter::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockFooter&>(p_Block).footer_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockThinking::ID:
      AppendLines(GetRichText(static_cast<const td::td_api::pageBlockThinking&>(p_Block).text_.get()),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockPreformatted::ID:
      {
        const auto& preformatted = static_cast<const td::td_api::pageBlockPreformatted&>(p_Block);
        AppendLines("```" + preformatted.language_, p_Indent, p_Out);
        AppendLines(GetRichText(preformatted.text_.get()), p_Indent, p_Out);
        AppendLines("```", p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockDivider::ID:
      AppendLines("---", p_Indent, p_Out);
      break;
    case td::td_api::pageBlockMathematicalExpression::ID:
      AppendLines(static_cast<const td::td_api::pageBlockMathematicalExpression&>(p_Block).expression_,
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockList::ID:
      AppendList(static_cast<const td::td_api::pageBlockList&>(p_Block), p_Indent, p_Out);
      break;
    case td::td_api::pageBlockBlockQuote::ID:
      {
        const auto& quote = static_cast<const td::td_api::pageBlockBlockQuote&>(p_Block);
        AppendBlocks(quote.blocks_, p_Indent + "> ", p_Out);
        const std::string credit = GetRichText(quote.credit_.get());
        if (!credit.empty())
        {
          AppendLines("- " + credit, p_Indent + "> ", p_Out);
        }
        break;
      }
    case td::td_api::pageBlockExpandableBlockQuote::ID:
      {
        const auto& quote = static_cast<const td::td_api::pageBlockExpandableBlockQuote&>(p_Block);
        AppendLines(GetRichText(quote.text_.get()), p_Indent + "> ", p_Out);
        const std::string credit = GetRichText(quote.credit_.get());
        if (!credit.empty())
        {
          AppendLines("- " + credit, p_Indent + "> ", p_Out);
        }
        break;
      }
    case td::td_api::pageBlockPullQuote::ID:
      {
        const auto& quote = static_cast<const td::td_api::pageBlockPullQuote&>(p_Block);
        AppendLines("\"" + GetRichText(quote.text_.get()) + "\"", p_Indent, p_Out);
        const std::string credit = GetRichText(quote.credit_.get());
        if (!credit.empty())
        {
          AppendLines("- " + credit, p_Indent, p_Out);
        }
        break;
      }
    case td::td_api::pageBlockTable::ID:
      AppendTable(static_cast<const td::td_api::pageBlockTable&>(p_Block), p_Indent, p_Out);
      break;
    case td::td_api::pageBlockDetails::ID:
      {
        const auto& details = static_cast<const td::td_api::pageBlockDetails&>(p_Block);
        AppendLines(GetRichText(details.header_.get()), p_Indent, p_Out);
        AppendBlocks(details.blocks_, p_Indent + "> ", p_Out);
        break;
      }
    case td::td_api::pageBlockCover::ID:
      {
        const auto& cover = static_cast<const td::td_api::pageBlockCover&>(p_Block);
        if (cover.cover_) AppendBlock(*cover.cover_, p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockCollage::ID:
      {
        const auto& collage = static_cast<const td::td_api::pageBlockCollage&>(p_Block);
        AppendBlocks(collage.blocks_, p_Indent, p_Out);
        const std::string caption = GetCaptionText(collage.caption_.get());
        if (!caption.empty()) AppendLines(caption, p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockSlideshow::ID:
      {
        const auto& slideshow = static_cast<const td::td_api::pageBlockSlideshow&>(p_Block);
        AppendBlocks(slideshow.blocks_, p_Indent, p_Out);
        const std::string caption = GetCaptionText(slideshow.caption_.get());
        if (!caption.empty()) AppendLines(caption, p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockEmbeddedPost::ID:
      {
        const auto& post = static_cast<const td::td_api::pageBlockEmbeddedPost&>(p_Block);
        if (!post.author_.empty()) AppendLines(post.author_ + ":", p_Indent, p_Out);
        AppendBlocks(post.blocks_, p_Indent + "> ", p_Out);
        const std::string caption = GetCaptionText(post.caption_.get());
        if (!caption.empty()) AppendLines(caption, p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockPhoto::ID:
      AppendMedia("[Photo]", static_cast<const td::td_api::pageBlockPhoto&>(p_Block).caption_.get(), p_Indent,
                  p_Out);
      break;
    case td::td_api::pageBlockVideo::ID:
      AppendMedia("[Video]", static_cast<const td::td_api::pageBlockVideo&>(p_Block).caption_.get(), p_Indent,
                  p_Out);
      break;
    case td::td_api::pageBlockAnimation::ID:
      AppendMedia("[Animation]", static_cast<const td::td_api::pageBlockAnimation&>(p_Block).caption_.get(),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockAudio::ID:
      AppendMedia("[Audio]", static_cast<const td::td_api::pageBlockAudio&>(p_Block).caption_.get(), p_Indent,
                  p_Out);
      break;
    case td::td_api::pageBlockDocument::ID:
      AppendMedia("[Document]", static_cast<const td::td_api::pageBlockDocument&>(p_Block).caption_.get(),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockVoiceNote::ID:
      AppendMedia("[VoiceNote]", static_cast<const td::td_api::pageBlockVoiceNote&>(p_Block).caption_.get(),
                  p_Indent, p_Out);
      break;
    case td::td_api::pageBlockMap::ID:
      AppendMedia("[Location]", static_cast<const td::td_api::pageBlockMap&>(p_Block).caption_.get(), p_Indent,
                  p_Out);
      break;
    case td::td_api::pageBlockEmbedded::ID:
      {
        const auto& embedded = static_cast<const td::td_api::pageBlockEmbedded&>(p_Block);
        AppendMedia(embedded.url_.empty() ? "[Embedded]" : ("[Embedded] " + embedded.url_), embedded.caption_.get(),
                    p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockChatLink::ID:
      {
        const auto& chatLink = static_cast<const td::td_api::pageBlockChatLink&>(p_Block);
        const std::string username = chatLink.username_.empty() ? "" : (" @" + chatLink.username_);
        AppendLines("[Chat] " + chatLink.title_ + username, p_Indent, p_Out);
        break;
      }
    case td::td_api::pageBlockRelatedArticles::ID:
      {
        const auto& related = static_cast<const td::td_api::pageBlockRelatedArticles&>(p_Block);
        const std::string header = GetRichText(related.header_.get());
        if (!header.empty()) AppendLines(header, p_Indent, p_Out);
        for (const auto& article : related.articles_)
        {
          if (!article) continue;

          const std::string& title = article->title_.empty() ? article->url_ : article->title_;
          AppendLines("- " + ((title == article->url_) ? title : ("[" + title + "](" + article->url_ + ")")),
                      p_Indent, p_Out);
        }
        break;
      }
    case td::td_api::pageBlockUnsupported::ID:
      AppendLines("[Unsupported]", p_Indent, p_Out);
      break;
    case td::td_api::pageBlockAnchor::ID:
    case td::td_api::pageBlockButtonRow::ID:
    default:
      break;
  }
}

void TgRichMessage::AppendMedia(const std::string& p_Label, const td::td_api::pageBlockCaption* p_Caption,
                                const std::string& p_Indent, std::string& p_Out)
{
  const std::string caption = GetCaptionText(p_Caption);
  AppendLines(caption.empty() ? p_Label : (p_Label + " " + caption), p_Indent, p_Out);
}

// list items with label (or "-") on first line, and following lines indented by label width
void TgRichMessage::AppendList(const td::td_api::pageBlockList& p_List, const std::string& p_Indent,
                               std::string& p_Out)
{
  for (const auto& item : p_List.items_)
  {
    if (!item) continue;

    std::string marker = item->label_.empty() ? "-" : item->label_;
    if (item->has_checkbox_)
    {
      marker += item->is_checked_ ? " [x]" : " [ ]";
    }

    const std::string markerIndent = std::string(StrUtil::WStringWidth(StrUtil::ToWString(marker)) + 1, ' ');
    const std::string itemIndent = p_Indent + markerIndent;
    std::string itemText;
    AppendBlocks(item->blocks_, itemIndent, itemText);
    if (itemText.compare(0, itemIndent.size(), itemIndent) == 0)
    {
      itemText.replace(0, itemIndent.size(), p_Indent + marker + " ");
    }
    else
    {
      itemText = p_Indent + marker + "\n" + itemText;
    }

    p_Out += itemText;
  }
}

// markdown table with cells padded to align columns, header separator only if first row is header
void TgRichMessage::AppendTable(const td::td_api::pageBlockTable& p_Table, const std::string& p_Indent,
                                std::string& p_Out)
{
  static const int32_t maxColspan = 32;

  const std::string caption = GetRichText(p_Table.caption_.get());
  if (!caption.empty()) AppendLines(caption, p_Indent, p_Out);

  std::vector<std::vector<std::string>> rows;
  size_t columns = 0;
  for (const auto& row : p_Table.cells_)
  {
    std::vector<std::string> cells;
    for (const auto& cell : row)
    {
      const std::string text = cell ? GetRichText(cell->text_.get()) : "";
      std::string escaped;
      for (const char c : text)
      {
        if (c == '\n')
        {
          escaped += ' ';
        }
        else if (c == '|')
        {
          escaped += "\\|";
        }
        else
        {
          escaped += c;
        }
      }

      cells.push_back(escaped);

      // keep following columns aligned by padding with empty cells for spanned columns
      const int32_t colspan = cell ? std::min(cell->colspan_, maxColspan) : 1;
      for (int32_t i = 1; i < colspan; ++i)
      {
        cells.push_back("");
      }
    }

    columns = std::max(columns, cells.size());
    rows.push_back(std::move(cells));
  }

  if (columns == 0) return;

  const auto& firstRow = p_Table.cells_.front();
  const bool hasHeader = !firstRow.empty() &&
    std::all_of(firstRow.begin(), firstRow.end(), [](const auto& p_Cell) { return p_Cell && p_Cell->is_header_; });

  std::vector<std::vector<int>> cellWidths;
  std::vector<int> widths(columns, hasHeader ? 3 : 0);
  for (const auto& row : rows)
  {
    cellWidths.emplace_back();
    for (size_t i = 0; i < row.size(); ++i)
    {
      const int width = StrUtil::WStringWidth(StrUtil::ToWString(row[i]));
      cellWidths.back().push_back(width);
      widths[i] = std::max(widths[i], width);
    }
  }

  for (size_t r = 0; r < rows.size(); ++r)
  {
    std::string line = p_Indent + "|";
    for (size_t i = 0; i < columns; ++i)
    {
      const bool hasCell = (i < rows[r].size());
      const std::string& cell = hasCell ? rows[r][i] : "";
      const int width = hasCell ? cellWidths[r][i] : 0;
      line += " " + cell + std::string(widths[i] - width, ' ') + " |";
    }

    p_Out += line + "\n";

    if ((r == 0) && hasHeader)
    {
      std::string separator = p_Indent + "|";
      for (size_t i = 0; i < columns; ++i)
      {
        separator += std::string(widths[i] + 2, '-') + "|";
      }

      p_Out += separator + "\n";
    }
  }
}
