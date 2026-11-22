// uidialog.cpp
//
// Copyright (c) 2019-2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uidialog.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uidrawutil.h"
#include "uimodel.h"
#include "uiview.h"

UiDialog::UiDialog(const UiDialogParams& p_Params)
  : m_Model(p_Params.model)
  , m_Title(p_Params.title)
  , m_WReq(p_Params.wReq)
  , m_HReq(p_Params.hReq)
{
  Init();
  curs_set(0);
}

UiDialog::~UiDialog()
{
  Cleanup();
  curs_set(1);
}

void UiDialog::Init()
{
  int screenW = m_Model->GetScreenWidth();
  int screenH = m_Model->GetScreenHeight();
  int w = (m_WReq > 1.0f) ? m_WReq : (screenW * m_WReq);
  int h = (m_HReq > 1.0f) ? m_HReq : (screenH * m_HReq);
  int x = (screenW - w) / 2;
  int y = (screenH - h) / 3;

  m_BorderWin = newwin(h, w, y, x);

  m_W = w - 4;
  m_H = h - 2;
  m_X = x + 2;
  m_Y = y + 1;

  DrawBorder();

  m_Win = newwin(m_H, m_W, m_Y, m_X);
}

void UiDialog::Cleanup()
{
  delwin(m_Win);
  delwin(m_BorderWin);
}

void UiDialog::SetFooter(const std::string& p_Footer)
{
  m_Footer = p_Footer;
  DrawBorder();
}

void UiDialog::DrawBorder()
{
  static int colorPair = UiColorConfig::GetColorPair("dialog_color");
  static int attribute = UiColorConfig::GetAttribute("dialog_attr");

  werase(m_BorderWin);
  wbkgd(m_BorderWin, colorPair | ' ');
  wattron(m_BorderWin, attribute | colorPair);

  wborder(m_BorderWin, 0, 0, 0, 0, 0, 0, 0, 0);
  const int maxTextWidth = m_W - 2;

  std::wstring wtitle = L" " + StrUtil::TrimWString(StrUtil::ToWString(m_Title), maxTextWidth) + L" ";
  int titlex = std::max(0, (m_W - StrUtil::WStringWidth(wtitle)) / 2) + 2;
  UiDrawUtil::AddWStr(m_BorderWin, 0, titlex, wtitle, m_W);

  if (!m_Footer.empty())
  {
    std::wstring wfooter = L" " + StrUtil::TrimWString(StrUtil::ToWString(m_Footer), maxTextWidth) + L" ";
    int footerx = std::max(0, (m_W - StrUtil::WStringWidth(wfooter)) / 2) + 2;
    UiDrawUtil::AddWStr(m_BorderWin, m_H + 1, footerx, wfooter, m_W);
  }

  wattroff(m_BorderWin, attribute | colorPair);
  wrefresh(m_BorderWin);
}
