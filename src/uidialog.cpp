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

  static int colorPair = UiColorConfig::GetColorPair("dialog_color");
  static int attribute = UiColorConfig::GetAttribute("dialog_attr");

  m_BorderWin = newwin(h, w, y, x);

  werase(m_BorderWin);
  wbkgd(m_BorderWin, colorPair | ' ');
  wattron(m_BorderWin, attribute | colorPair);

  wborder(m_BorderWin, 0, 0, 0, 0, 0, 0, 0, 0);
  std::wstring wtitle = L" " + StrUtil::ToWString(m_Title) + L" ";
  int titlex = std::max(1, (w - (int)wtitle.size()) / 2);
  int titlew = std::min((int)wtitle.size(), w - 2);
  mvwaddnwstr(m_BorderWin, 0, titlex, wtitle.c_str(), titlew);

  wattroff(m_BorderWin, attribute | colorPair);
  wrefresh(m_BorderWin);

  m_W = w - 4;
  m_H = h - 2;
  m_X = x + 2;
  m_Y = y + 1;

  m_Win = newwin(m_H, m_W, m_Y, m_X);
}

void UiDialog::Cleanup()
{
  delwin(m_Win);
  delwin(m_BorderWin);
}
