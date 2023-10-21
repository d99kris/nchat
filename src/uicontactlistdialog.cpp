// uicontactlistdialog.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uicontactlistdialog.h"

#include <algorithm>

#include "log.h"
#include "uimodel.h"
#include "strutil.h"

UiContactListDialog::UiContactListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params)
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

std::pair<std::string, ContactInfo> UiContactListDialog::GetSelectedContact()
{
  return m_SelectedContact;
}

void UiContactListDialog::OnSelect()
{
  if (m_ContactInfosVec.empty()) return;

  m_SelectedContact = m_ContactInfosVec[m_Index];
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
  m_ContactInfosVec.clear();

  std::vector<std::pair<std::string, ContactInfo>> contactInfosVec;
  for (const auto& profileContactInfos : m_DialogContactInfos)
  {
    for (const auto& contactInfo : profileContactInfos.second)
    {
      contactInfosVec.push_back(std::make_pair(profileContactInfos.first, contactInfo.second));
    }
  }

  std::sort(contactInfosVec.begin(), contactInfosVec.end(),
            [&](const std::pair<std::string, ContactInfo>& lhs, const std::pair<std::string, ContactInfo>& rhs) -> bool
  {
    return lhs.second.name < rhs.second.name;
  });

  for (const auto& contactInfo : contactInfosVec)
  {
    std::string name = contactInfo.second.name;
    if (name.empty()) continue;

    if (m_FilterStr.empty() ||
        (StrUtil::ToLower(name).find(StrUtil::ToLower(StrUtil::ToString(m_FilterStr))) != std::string::npos))
    {
      static const bool isMultipleProfiles = m_Model->IsMultipleProfiles();
      std::string displayName = name + (isMultipleProfiles ? " @ " + m_Model->GetProfileDisplayName(contactInfo.first) : "");
      m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(displayName), m_W));
      m_ContactInfosVec.push_back(std::make_pair(contactInfo.first, contactInfo.second));
    }
  }
}
