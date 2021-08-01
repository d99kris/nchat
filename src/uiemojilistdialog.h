// uiemojilistdialog.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include "uilistdialog.h"

class UiEmojiListDialog : public UiListDialog
{
public:
  UiEmojiListDialog(const UiDialogParams& p_Params);
  virtual ~UiEmojiListDialog();

  std::wstring GetSelectedEmoji();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();

private:
  std::vector<std::pair<std::string, std::string>> m_TextEmojis;
  std::wstring m_SelectedEmoji;
};
