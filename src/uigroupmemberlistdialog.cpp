// uigroupmemberlistdialog.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uigroupmemberlistdialog.h"

#include <algorithm>

#include "apputil.h"
#include "log.h"
#include "uimodel.h"
#include "strutil.h"

UiGroupMemberListDialog::UiGroupMemberListDialog(const UiDialogParams& p_Params,
                                                 const std::string& p_ProfileId,
                                                 const std::string& p_ChatId)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
  , m_ProfileId(p_ProfileId)
  , m_ChatId(p_ChatId)
{
  m_Model->RequestGroupMembers(m_ProfileId, m_ChatId);
  m_GroupMembersUpdateTime = m_Model->GetGroupMembersUpdateTime();
  UpdateList();
}

UiGroupMemberListDialog::~UiGroupMemberListDialog()
{
}

UiGroupMemberListItem UiGroupMemberListDialog::GetSelectedItem()
{
  return m_SelectedItem;
}

void UiGroupMemberListDialog::OnSelect()
{
  if (m_MemberListItemVec.empty()) return;

  m_SelectedItem = m_MemberListItemVec[m_Index];
  m_Result = true;
  m_Running = false;
}

void UiGroupMemberListDialog::OnBack()
{
}

bool UiGroupMemberListDialog::OnTimer()
{
  int64_t modelUpdateTime = m_Model->GetGroupMembersUpdateTime();
  if (m_GroupMembersUpdateTime != modelUpdateTime)
  {
    UpdateList();
    return true;
  }

  return false;
}

void UiGroupMemberListDialog::UpdateList()
{
  int64_t modelUpdateTime = m_Model->GetGroupMembersUpdateTime();
  if (m_GroupMembersUpdateTime != modelUpdateTime)
  {
    m_GroupMembersUpdateTime = modelUpdateTime;
  }

  const bool emojiEnabled = m_Model->GetEmojiEnabled();

  // Get current item based on index
  UiGroupMemberListItem currentItem;
  if (m_Index < (int)m_MemberListItemVec.size())
  {
    currentItem = m_MemberListItemVec[m_Index];
  }

  m_Index = 0;
  m_Items.clear();
  m_MemberListItemVec.clear();

  std::vector<std::string> memberIds = m_Model->GetGroupMembers(m_ProfileId, m_ChatId);

  // Use a local vector which is sorted before populating dialog members
  std::vector<UiGroupMemberListItem> localItemVec;

  for (const auto& memberId : memberIds)
  {
    const std::string& name = m_Model->GetContactListName(m_ProfileId, memberId, false /*p_AllowId*/,
                                                          true /*p_AllowAlias*/);
    if (name.empty()) continue;
    if (name == memberId) continue;
    if (m_Model->IsContactSelf(m_ProfileId, memberId)) continue;

    std::string displayName = name;

    if (m_FilterStr.empty() ||
        (StrUtil::ToLower(displayName).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos))
    {
      if (!emojiEnabled)
      {
        displayName = StrUtil::Textize(displayName);
      }

      static const bool developerMode = AppUtil::GetDeveloperMode();
      if (developerMode)
      {
        displayName += " [" + memberId + "]";
      }

      UiGroupMemberListItem item;
      item.memberId = memberId;
      item.name = displayName;
      localItemVec.push_back(item);
    }
  }

  std::sort(localItemVec.begin(), localItemVec.end(),
            [&](const UiGroupMemberListItem& lhs, const UiGroupMemberListItem& rhs) -> bool
  {
    return lhs.name < rhs.name;
  });

  for (const auto& item : localItemVec)
  {
    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(item.name), m_W));
    m_MemberListItemVec.push_back(item);
  }

  // Set index based on current item
  if (!currentItem.memberId.empty())
  {
    for (auto it = m_MemberListItemVec.begin(); it != m_MemberListItemVec.end(); ++it)
    {
      if (it->memberId == currentItem.memberId)
      {
        m_Index = std::distance(m_MemberListItemVec.begin(), it);
        break;
      }
    }
  }
}
