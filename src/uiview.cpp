// uiview.cpp
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uiview.h"

#include "log.h"
#include "uiconfig.h"
#include "uientryview.h"
#include "uihelpview.h"
#include "uihistoryview.h"
#include "uikeyconfig.h"
#include "uilistborderview.h"
#include "uilistview.h"
#include "uiscreen.h"
#include "uistatusview.h"
#include "uitopview.h"
#include "uiviewbase.h"

UiView::UiView(UiModel* p_UiModel)
  : m_UiModel(p_UiModel)
{
  m_EmojiEnabled = UiConfig::GetBool("emoji_enabled");
  m_HelpEnabled = UiConfig::GetBool("help_enabled");
  m_ListEnabled = UiConfig::GetBool("list_enabled");
  m_TopEnabled = UiConfig::GetBool("top_enabled");
  m_ListWidth = UiConfig::GetNum("list_width");
  Init();
}

UiView::~UiView()
{
  UiConfig::SetBool("emoji_enabled", m_EmojiEnabled);
  UiConfig::SetBool("help_enabled", m_HelpEnabled);
  UiConfig::SetBool("list_enabled", m_ListEnabled);
  UiConfig::SetBool("top_enabled", m_TopEnabled);
  UiConfig::SetNum("list_width", m_ListWidth);
}

void UiView::Init()
{
  m_UiScreen = std::make_shared<UiScreen>();

  {
    int w = m_UiScreen->W();
    int h = 1;
    int x = 0;
    int y = 0;
    UiViewParams params(x, y, w, h, m_TopEnabled, m_UiModel);
    m_UiTopView = std::make_shared<UiTopView>(params);
  }

  {
    int w = m_UiScreen->W();
    int h = 1;
    int x = 0;
    int y = m_UiScreen->H() - h;
    UiViewParams params(x, y, w, h, m_HelpEnabled, m_UiModel);
    m_UiHelpView = std::make_shared<UiHelpView>(params);
  }

  {
    int w = m_UiScreen->W();
    int h = 4;
    int x = 0;
    int y = m_UiScreen->H() - m_UiHelpView->H() - h;
    UiViewParams params(x, y, w, h, m_EntryEnabled, m_UiModel);
    m_UiEntryView = std::make_shared<UiEntryView>(params);
  }

  {
    int w = m_UiScreen->W();
    int h = 1;
    int x = 0;
    int y = m_UiScreen->H() - m_UiHelpView->H() - m_UiEntryView->H() - h;
    UiViewParams params(x, y, w, h, m_StatusEnabled, m_UiModel);
    m_UiStatusView = std::make_shared<UiStatusView>(params);
  }

  {
    int w = m_ListWidth;
    int h = m_UiScreen->H() - m_UiTopView->H() - m_UiHelpView->H() -
      m_UiEntryView->H() - m_UiStatusView->H();
    int x = 0;
    int y = m_UiTopView->H();
    UiViewParams params(x, y, w, h, m_ListEnabled, m_UiModel);
    m_UiListView = std::make_shared<UiListView>(params);
  }

  {
    int w = 1;
    int h = m_UiScreen->H() - m_UiTopView->H() - m_UiHelpView->H() -
      m_UiEntryView->H() - m_UiStatusView->H();
    int x = m_UiListView->X() + m_UiListView->W();
    int y = m_UiTopView->H();
    UiViewParams params(x, y, w, h, m_ListEnabled && m_ListWidth, m_UiModel);
    m_UiListBorderView = std::make_shared<UiListBorderView>(params);
  }

  {
    int x = m_UiListBorderView->X() + m_UiListBorderView->W();
    int w = m_UiScreen->W() - x;
    int h = m_UiScreen->H() - m_UiTopView->H() - m_UiHelpView->H() -
      m_UiEntryView->H() - m_UiStatusView->H();
    int y = m_UiTopView->H();
    UiViewParams params(x, y, w, h, m_HistoryEnabled, m_UiModel);
    m_UiHistoryView = std::make_shared<UiHistoryView>(params);
  }
}

void UiView::Draw()
{
  m_UiTopView->Draw();
  m_UiHelpView->Draw();
  m_UiStatusView->Draw();
  m_UiListView->Draw();
  m_UiListBorderView->Draw();
  m_UiHistoryView->Draw();
  m_UiEntryView->Draw();
}

void UiView::TerminalBell()
{
  LOG_DEBUG("bell");
  beep();
}

void UiView::SetEmojiEnabled(bool p_Enabled)
{
  m_EmojiEnabled = p_Enabled;
}

bool UiView::GetEmojiEnabled()
{
  return m_EmojiEnabled;
}

void UiView::SetTopEnabled(bool p_Enabled)
{
  m_TopEnabled = p_Enabled;
}

bool UiView::GetTopEnabled()
{
  return m_TopEnabled;
}

void UiView::SetHelpEnabled(bool p_Enabled)
{
  m_HelpEnabled = p_Enabled;
}

bool UiView::GetHelpEnabled()
{
  return m_HelpEnabled;
}

void UiView::SetListEnabled(bool p_Enabled)
{
  m_ListEnabled = p_Enabled;
}

bool UiView::GetListEnabled()
{
  return m_ListEnabled;
}

void UiView::SetListDirty(bool p_Dirty)
{
  m_UiListView->SetDirty(p_Dirty);
}

void UiView::SetStatusDirty(bool p_Dirty)
{
  m_UiStatusView->SetDirty(p_Dirty);
}

void UiView::SetHistoryDirty(bool p_Dirty)
{
  m_UiHistoryView->SetDirty(p_Dirty);
}

void UiView::SetHelpDirty(bool p_Dirty)
{
  m_UiHelpView->SetDirty(p_Dirty);
}

void UiView::SetEntryDirty(bool p_Dirty)
{
  m_UiEntryView->SetDirty(p_Dirty);
}

int UiView::GetHistoryShowCount()
{
  return m_UiHistoryView->GetHistoryShowCount();
}

int UiView::GetHistoryLines()
{
  return m_UiHistoryView->H();
}

int UiView::GetEntryWidth()
{
  return m_UiEntryView->W();
}

int UiView::GetScreenWidth()
{
  return m_UiScreen->W();
}

int UiView::GetScreenHeight()
{
  return m_UiScreen->H();
}

void UiView::NarrowList()
{
  if (m_ListWidth > 0)
  {
    --m_ListWidth;
  }
}

void UiView::EnlargeList()
{
  if (m_ListWidth < m_UiScreen->W())
  {
    ++m_ListWidth;
  }
}
