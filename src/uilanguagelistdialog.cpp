// uilanguagelistdialog.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uilanguagelistdialog.h"

#include "strutil.h"

UiLanguageListDialog::UiLanguageListDialog(const UiDialogParams& p_Params, const std::string& p_CurrentLanguage)
  : UiListDialog(p_Params, false /*p_ShadeHidden*/)
  , m_CurrentLanguage(p_CurrentLanguage)
{
  // Define common languages for transcription
  m_Languages = {
    {"", "Default (from global settings)"},
    {"auto", "Auto-detect"},
    {"en", "English"},
    {"es", "Spanish"},
    {"fr", "French"},
    {"de", "German"},
    {"it", "Italian"},
    {"pt", "Portuguese"},
    {"ru", "Russian"},
    {"uk", "Ukrainian"},
    {"zh", "Chinese"},
    {"ja", "Japanese"},
    {"ko", "Korean"},
    {"ar", "Arabic"},
    {"hi", "Hindi"},
    {"nl", "Dutch"},
    {"pl", "Polish"},
    {"tr", "Turkish"},
    {"sv", "Swedish"},
    {"no", "Norwegian"},
    {"da", "Danish"},
    {"fi", "Finnish"},
  };

  UpdateList();
}

UiLanguageListDialog::~UiLanguageListDialog()
{
}

std::string UiLanguageListDialog::GetSelectedLanguage()
{
  return m_SelectedLanguage;
}

void UiLanguageListDialog::OnSelect()
{
  if ((m_Index >= 0) && (m_Index < (int)m_FilteredIndices.size()))
  {
    int languageIndex = m_FilteredIndices[m_Index];
    m_SelectedLanguage = m_Languages[languageIndex].code;
    m_Result = true;
    m_Running = false;
  }
}

void UiLanguageListDialog::OnBack()
{
  m_Result = false;
  m_Running = false;
}

bool UiLanguageListDialog::OnTimer()
{
  return false;
}

void UiLanguageListDialog::UpdateList()
{
  m_Items.clear();
  m_FilteredIndices.clear();

  std::wstring filterStrLower = StrUtil::ToLower(m_FilterStr);

  for (size_t i = 0; i < m_Languages.size(); ++i)
  {
    const LanguageOption& lang = m_Languages[i];
    std::wstring displayName = StrUtil::ToWString(lang.name);

    // Add indicator if this is the current language
    if (lang.code == m_CurrentLanguage)
    {
      displayName = L"* " + displayName;
    }

    // Filter by name or code
    std::wstring displayNameLower = StrUtil::ToLower(displayName);
    std::wstring codeLower = StrUtil::ToLower(StrUtil::ToWString(lang.code));

    if (filterStrLower.empty() ||
        (displayNameLower.find(filterStrLower) != std::wstring::npos) ||
        (codeLower.find(filterStrLower) != std::wstring::npos))
    {
      m_Items.push_back(displayName);
      m_FilteredIndices.push_back(i);
    }
  }
}
