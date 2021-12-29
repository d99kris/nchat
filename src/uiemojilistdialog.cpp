// uiemojilistdialog.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiemojilistdialog.h"

#include "emojilist.h"
#include "strutil.h"
#include "uimodel.h"

UiEmojiListDialog::UiEmojiListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params)
{
  UpdateList();
}

UiEmojiListDialog::~UiEmojiListDialog()
{
}

std::wstring UiEmojiListDialog::GetSelectedEmoji()
{
  return m_SelectedEmoji;
}

void UiEmojiListDialog::OnSelect()
{
  if (m_TextEmojis.empty()) return;

  EmojiList::AddUsage(std::next(m_TextEmojis.begin(), m_Index)->first);
  if (m_Model->GetEmojiEnabled())
  {
    m_SelectedEmoji = StrUtil::ToWString(std::next(m_TextEmojis.begin(), m_Index)->second);
  }
  else
  {
    m_SelectedEmoji = StrUtil::ToWString(std::next(m_TextEmojis.begin(), m_Index)->first);
  }

  m_Result = true;
  m_Running = false;
}

void UiEmojiListDialog::OnBack()
{
}

bool UiEmojiListDialog::OnTimer()
{
  return false;
}

void UiEmojiListDialog::UpdateList()
{
  const bool emojiEnabled = m_Model->GetEmojiEnabled();
  m_Index = 0;
  m_Items.clear();
  m_TextEmojis.clear();
  std::vector<std::pair<std::string, std::string>> textEmojis = EmojiList::Get(StrUtil::ToString(m_FilterStr));
  for (auto& textEmoji : textEmojis)
  {
    std::wstring desc = StrUtil::ToWString(textEmoji.first);
    std::wstring item = StrUtil::ToWString(textEmoji.second);
    if (StrUtil::WStringWidth(item) <= 0) continue; // mainly for mac

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
}
