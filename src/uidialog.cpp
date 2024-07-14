// uidialog.cpp
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uidialog.h"

#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"
#include "uiview.h"

UiDialog::UiDialog(const UiDialogParams& p_Params)
  : m_View(p_Params.view)
  , m_Model(p_Params.model)
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
  int screenW = m_View->GetScreenWidth();
  int screenH = m_View->GetScreenHeight();
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

  std::string title = " " + m_Title.substr(0, maxTextWidth) + " ";
  std::wstring wtitle = StrUtil::ToWString(title);
  int titlex = std::max(0, (m_W - (int)wtitle.size()) / 2) + 2;
  int titlew = std::min((int)wtitle.size(), m_W);
  mvwaddnwstr(m_BorderWin, 0, titlex, wtitle.c_str(), titlew);

  if (!m_Footer.empty())
  {
    std::string footer = " " + m_Footer.substr(0, maxTextWidth) + " ";
    std::wstring wfooter = StrUtil::ToWString(footer);
    int footerx = std::max(0, (m_W - (int)wfooter.size()) / 2) + 2;
    int footerw = std::min((int)wfooter.size(), m_W);
    mvwaddnwstr(m_BorderWin, m_H + 1, footerx, wfooter.c_str(), footerw);
  }

  wattroff(m_BorderWin, attribute | colorPair);
  wrefresh(m_BorderWin);
}
