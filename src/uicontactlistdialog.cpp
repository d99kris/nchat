// uicontactlistdialog.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicontactlistdialog.h"

#include <algorithm>

#include "apputil.h"
#include "log.h"
#include "uimodel.h"
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

  const bool emojiEnabled = m_Model->GetEmojiEnabled();

  // Get current item based on index
  UiContactListItem currentContactItem;
  if (m_Index < (int)m_ContactListItemVec.size())
  {
    currentContactItem = m_ContactListItemVec[m_Index];
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
      const std::string& name = m_Model->GetContactListName(profileId, contactId, false /*p_AllowId*/);;

      if (name.empty()) continue;

      if (m_FilterStr.empty() ||
          (StrUtil::ToLower(name).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos))
      {
        static const bool isMultipleProfiles = m_Model->IsMultipleProfiles();
        std::string displayName = name +
          (isMultipleProfiles ? " @ " + m_Model->GetProfileDisplayName(profileId) : "");

        if (!emojiEnabled)
        {
          displayName = StrUtil::Textize(displayName);
        }

        static const bool developerMode = AppUtil::GetDeveloperMode();
        if (developerMode)
        {
          displayName += " [" + contactId + "]";
        }

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
    if (lhs.isStarred > rhs.isStarred) return true;
    if (lhs.isStarred < rhs.isStarred) return false;

    return lhs.name < rhs.name;
  });

  for (const auto& contactListItem : localContactListItemVec)
  {
    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(contactListItem.name), m_W));
    m_ContactListItemVec.push_back(contactListItem);
  }

  // Set index based on current item
  if (!currentContactItem.profileId.empty() && !currentContactItem.contactId.empty())
  {
    for (auto it = m_ContactListItemVec.begin(); it != m_ContactListItemVec.end(); ++it)
    {
      if ((it->contactId == currentContactItem.contactId) &&
          (it->profileId == currentContactItem.profileId))
      {
        m_Index = std::distance(m_ContactListItemVec.begin(), it);
        break;
      }
    }
  }
}
