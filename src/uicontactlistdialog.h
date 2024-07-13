// uicontactlistdialog.h
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "protocol.h"
#include "uilistdialog.h"

struct UiContactListItem
{
  std::string profileId;
  std::string contactId;
  std::string name;
};

class UiContactListDialog : public UiListDialog
{
public:
  UiContactListDialog(const UiDialogParams& p_Params);
  virtual ~UiContactListDialog();

  UiContactListItem GetSelectedContactItem();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();

  void UpdateList();

private:
  std::unordered_map<std::string, std::unordered_map<std::string, ContactInfo>> m_DialogContactInfos;
  int64_t m_DialogContactInfosUpdateTime = 0;
  std::vector<UiContactListItem> m_ContactListItemVec;
  UiContactListItem m_SelectedContactItem;
};
