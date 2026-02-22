// uigroupmemberlistdialog.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>
#include <vector>

#include "uilistdialog.h"

struct UiGroupMemberListItem
{
  std::string memberId;
  std::string name;
};

class UiGroupMemberListDialog : public UiListDialog
{
public:
  UiGroupMemberListDialog(const UiDialogParams& p_Params,
                          const std::string& p_ProfileId,
                          const std::string& p_ChatId);
  virtual ~UiGroupMemberListDialog();

  UiGroupMemberListItem GetSelectedItem();

protected:
  virtual void OnSelect();
  virtual void OnBack();
  virtual bool OnTimer();
  virtual void UpdateList();

private:
  std::string m_ProfileId;
  std::string m_ChatId;
  std::vector<UiGroupMemberListItem> m_MemberListItemVec;
  UiGroupMemberListItem m_SelectedItem;
  int64_t m_GroupMembersUpdateTime = 0;
};
