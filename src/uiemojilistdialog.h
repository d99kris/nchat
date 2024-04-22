// uiemojilistdialog.h
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uilistdialog.h"

#include <set>

class UiEmojiListDialog : public UiListDialog
{
public:
  UiEmojiListDialog(const UiDialogParams& p_Params, const std::string& p_DefaultOption = "",
                    bool p_HasNoneOption = false, bool p_HasLimitedEmojis = false);
  virtual ~UiEmojiListDialog();

  std::wstring GetSelectedEmoji(bool p_EmojiEnabled);

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();

private:
  void SetSelectedEmoji(const std::string& p_Emoji);

private:
  std::vector<std::pair<std::string, std::string>> m_TextEmojis;
  std::pair<std::string, std::string> m_SelectedTextEmoji;
  std::set<std::string> m_AvailableEmojis;
  std::string m_DefaultOption;
  bool m_HasNoneOption = false;
  bool m_HasLimitedEmojis = false;
  bool m_HasAvailableEmojisPending = false;
};
