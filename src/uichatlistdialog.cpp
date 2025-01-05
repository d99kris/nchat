// uichatlistdialog.cpp
//
// Copyright (c) 2024-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uichatlistdialog.h"

#include <algorithm>

#include "apputil.h"
#include "log.h"
#include "strutil.h"
#include "uiconfig.h"
#include "uimodel.h"

UiChatListDialog::UiChatListDialog(const UiDialogParams& p_Params)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
{
  m_ChatVec = m_Model->GetChatVecLock();
  UpdateList();
}

UiChatListDialog::~UiChatListDialog()
{
}

UiChatListItem UiChatListDialog::GetSelectedChatItem()
{
  return m_SelectedChatItem;
}

void UiChatListDialog::OnSelect()
{
  if (m_ChatListItemVec.empty()) return;

  m_SelectedChatItem = m_ChatListItemVec[m_Index];
  m_Result = true;
  m_Running = false;
}

void UiChatListDialog::OnBack()
{
}

bool UiChatListDialog::OnTimer()
{
  return false; // no update
}

void UiChatListDialog::UpdateList()
{
  const bool emojiEnabled = m_Model->GetEmojiEnabled();

  m_Index = 0;
  m_Items.clear();
  m_ChatListItemVec.clear();

  // Use a local vector which is sorted before populating dialog members, which need to be in sync
  std::vector<UiChatListItem> localChatListItemVec;

  for (const auto& profileChat : m_ChatVec)
  {
    const std::string& profileId = profileChat.first;
    const std::string& chatId = profileChat.second;
    const std::string& name = m_Model->GetContactListNameLock(profileId, chatId, true /*p_AllowId*/);;

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
        displayName += " [" + chatId + "]";
      }

      UiChatListItem chatListItem;
      chatListItem.profileId = profileId;
      chatListItem.chatId = chatId;
      chatListItem.name = displayName;

      localChatListItemVec.push_back(chatListItem);
    }
  }

  static const bool chatPickerSortedAlphabetically =
    UiConfig::GetBool("chat_picker_sorted_alphabetically");
  if (chatPickerSortedAlphabetically)
  {
    std::sort(localChatListItemVec.begin(), localChatListItemVec.end(),
              [&](const UiChatListItem& lhs, const UiChatListItem& rhs) -> bool
    {
      return lhs.name < rhs.name;
    });
  }

  for (const auto& chatListItem : localChatListItemVec)
  {
    m_Items.push_back(StrUtil::TrimPadWString(StrUtil::ToWString(chatListItem.name), m_W));
    m_ChatListItemVec.push_back(chatListItem);
  }
}
