// uicontactlistdialog.cpp
//
// Copyright (c) 2019-2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicontactlistdialog.h"

#include <algorithm>

#include "log.h"
#include "uimodel.h"
#include "uiconfig.h"
#include "strutil.h"

UiContactListDialog::UiContactListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
{
  static bool s_ContactsRequested = false;
  if (!s_ContactsRequested)
  {
    m_Model->RequestContacts();
    s_ContactsRequested = true;
  }

  UpdateList();
}

UiContactListDialog::~UiContactListDialog()
{
}

UiContactListItem UiContactListDialog::GetSelectedContactItem()
{
  return m_SelectedContactItem;
}

void UiContactListDialog::OnSelect()
{
  if (m_ContactListItemVec.empty()) return;

  m_SelectedContactItem = m_ContactListItemVec[m_Index];
  m_Result = true;
  m_Running = false;
}

void UiContactListDialog::OnBack()
{
}

bool UiContactListDialog::OnTimer()
{
  int64_t modelContactInfosUpdateTime = m_Model->GetContactInfosUpdateTime();
  if (m_DialogContactInfosUpdateTime != modelContactInfosUpdateTime)
  {
    UpdateList();
    return true;
  }

  return false;
}

void UiContactListDialog::UpdateList()
{
  int64_t modelContactInfosUpdateTime = m_Model->GetContactInfosUpdateTime();
  if (m_DialogContactInfosUpdateTime != modelContactInfosUpdateTime)
  {
    m_DialogContactInfosUpdateTime = modelContactInfosUpdateTime;
    m_DialogContactInfos = m_Model->GetContactInfos();
  }

  m_Index = 0;
  m_Items.clear();
  m_ContactListItemVec.clear();

  // Use a local vector which is sorted before populating dialog members, which need to be in sync
  std::vector<UiContactListItem> localContactListItemVec;

  for (const auto& profileContactInfos : m_DialogContactInfos)
  {
    for (const auto& idContactInfo : profileContactInfos.second)
    {
      const std::string& profileId = profileContactInfos.first;
      const std::string& contactId = idContactInfo.first;
      const std::string& name = m_Model->GetContactListNameLock(profileId, contactId, false /*p_AllowId*/);;

      if (name.empty()) continue;

      if (m_FilterStr.empty() ||
          (StrUtil::ToLower(name).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos))
      {
        static const bool isMultipleProfiles = m_Model->IsMultipleProfiles();
        const std::string displayName = name +
          (isMultipleProfiles ? " @ " + m_Model->GetProfileDisplayName(profileId) : "");

        UiContactListItem contactListItem;
        contactListItem.profileId = profileId;
        contactListItem.contactId = contactId;
        contactListItem.name = displayName;

        localContactListItemVec.push_back(contactListItem);
      }
    }
  }

  std::sort(localContactListItemVec.begin(), localContactListItemVec.end(),
            [&](const UiContactListItem& lhs, const UiContactListItem& rhs) -> bool
  {
    if (UiConfig::GetBool("chat_picker_sorted_alphabetically"))
    {
      return lhs.name < rhs.name;
    }
    else
    {
      ChatInfo* lhsChatInfo = m_Model->GetChatInfo(lhs.profileId, lhs.contactId);
      ChatInfo* rhsChatInfo = m_Model->GetChatInfo(rhs.profileId, rhs.contactId);

      bool bothContactsWithoutChatInfo = (rhsChatInfo == nullptr) && (lhsChatInfo == nullptr);

      if (bothContactsWithoutChatInfo)
      {
        return lhs.name < rhs.name;
      }
      else
      {
        // contacts with chat info first
        if (rhsChatInfo == nullptr) return true;
        if (lhsChatInfo == nullptr) return false;

        // when both contacts have chat info
        // use same criteria as in the chat list
        return m_Model->CompareChats(*lhsChatInfo, *rhsChatInfo);
      }
    }
  });

  for (const auto& contactListItem : localContactListItemVec)
  {
    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(contactListItem.name), m_W));
    m_ContactListItemVec.push_back(contactListItem);
  }
}
