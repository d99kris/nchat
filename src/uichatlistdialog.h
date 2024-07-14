// uichatlistdialog.h
//
// Copyright (c) 2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "protocol.h"
#include "uilistdialog.h"

struct UiChatListItem
{
  std::string profileId;
  std::string chatId;
  std::string name;
};

class UiChatListDialog : public UiListDialog
{
public:
  UiChatListDialog(const UiDialogParams& p_Params);
  virtual ~UiChatListDialog();

  UiChatListItem GetSelectedChatItem();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();

private:
  std::vector<std::pair<std::string, std::string>> m_ChatVec;
  std::vector<UiChatListItem> m_ChatListItemVec;
  UiChatListItem m_SelectedChatItem;
};
