// uiemojilistdialog.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiemojilistdialog.h"

#include "emojilist.h"
#include "log.h"
#include "strutil.h"
#include "uimodel.h"

static std::pair<std::string, std::string> s_NoneOptionTextEmoji("[none]", "");

UiEmojiListDialog::UiEmojiListDialog(const UiDialogParams& p_Params, const std::string& p_DefaultOption /*= ""*/,
                                     bool p_HasNoneOption /*= false*/, bool p_HasLimitedEmojis /*= false*/)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
  , m_DefaultOption(p_DefaultOption)
  , m_HasNoneOption(p_HasNoneOption)
  , m_HasLimitedEmojis(p_HasLimitedEmojis)
  , m_HasAvailableEmojisPending(p_HasLimitedEmojis)
{
  if (m_HasLimitedEmojis)
  {
    m_Model->GetAvailableEmojis(m_AvailableEmojis, m_HasAvailableEmojisPending);
  }

  UpdateList();
  SetSelectedEmoji(m_DefaultOption);
}

UiEmojiListDialog::~UiEmojiListDialog()
{
}

std::wstring UiEmojiListDialog::GetSelectedEmoji(bool p_EmojiEnabled)
{
  return StrUtil::ToWString(p_EmojiEnabled ? m_SelectedTextEmoji.second : m_SelectedTextEmoji.first);
}

void UiEmojiListDialog::SetSelectedEmoji(const std::string& p_Emoji)
{
  if (p_Emoji.empty()) return;

  for (auto it = m_TextEmojis.begin(); it != m_TextEmojis.end(); ++it)
  {
    if (it->second == p_Emoji)
    {
      m_Index = std::distance(m_TextEmojis.begin(), it);
      break;
    }
  }
}

void UiEmojiListDialog::OnSelect()
{
  if (m_TextEmojis.empty()) return;

  m_SelectedTextEmoji = *std::next(m_TextEmojis.begin(), m_Index); // ex: (":thumbsup:", 0xf0 0x9f 0x91 0x8d)

  if (m_SelectedTextEmoji != s_NoneOptionTextEmoji)
  {
    EmojiList::AddUsage(std::next(m_TextEmojis.begin(), m_Index)->first);
  }

  m_Result = true;
  m_Running = false;
}

void UiEmojiListDialog::OnBack()
{
}

bool UiEmojiListDialog::OnTimer()
{
  bool rv = false;
  if (m_HasLimitedEmojis && m_HasAvailableEmojisPending)
  {
    m_Model->GetAvailableEmojis(m_AvailableEmojis, m_HasAvailableEmojisPending);
    if (!m_HasAvailableEmojisPending)
    {
      UpdateList();
      SetSelectedEmoji(m_DefaultOption);
      rv = true;
    }
  }

  return rv;
}

void UiEmojiListDialog::UpdateList()
{
  std::string selectedEmoji;
  if (!m_TextEmojis.empty())
  {
    selectedEmoji = std::next(m_TextEmojis.begin(), m_Index)->second;
  }

  m_Index = 0;
  m_Items.clear();
  m_TextEmojis.clear();
  std::vector<std::pair<std::string, std::string>> textEmojis = EmojiList::Get(StrUtil::ToString(m_FilterStr));

  if (m_HasNoneOption)
  {
    textEmojis.insert(textEmojis.begin(), s_NoneOptionTextEmoji);
    if (!m_FilterStr.empty())
    {
      m_Index = 1;
    }
  }

  const bool emojiEnabled = m_Model->GetEmojiEnabled();
  for (auto& textEmoji : textEmojis)
  {
    if (m_HasLimitedEmojis)
    {
      if (!m_AvailableEmojis.count(textEmoji.second) && !textEmoji.second.empty()) continue;
    }

    std::wstring desc = StrUtil::ToWString(textEmoji.first); // ex: :thumbsup:
    std::wstring item = StrUtil::ToWString(textEmoji.second); // ex: 0xf0 0x9f 0x91 0x8d
    if ((StrUtil::WStringWidth(item) <= 0) && (!item.empty())) continue; // mainly for mac

    if (emojiEnabled)
    {
#ifdef __APPLE__
      // work-around wcswidth issue on mac
      item = StrUtil::TrimPadWString(desc, (m_W * 5) / 6) + L" " + item;
#else
      item = StrUtil::TrimPadWString(item, 4) + desc;
#endif
    }
    else
    {
      item = desc;
    }

    m_Items.push_back(StrUtil::TrimPadWString(item, m_W));
    m_TextEmojis.push_back(textEmoji);
  }

  SetSelectedEmoji(selectedEmoji);
}
