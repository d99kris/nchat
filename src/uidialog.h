// uidialog.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <ncursesw/ncurses.h>

class UiModel;
class UiView;

struct UiDialogParams
{
  UiDialogParams(UiView* p_View, UiModel* p_Model, std::string p_Title, int p_WPerc, int p_HPerc)
    : view(p_View)
    , model(p_Model)
    , title(p_Title)
    , wPerc(p_WPerc)
    , hPerc(p_HPerc)
  {
  }

  UiView* view = nullptr;
  UiModel* model = nullptr;
  std::string title;
  int wPerc = 0;
  int hPerc = 0;
};

class UiDialog
{
public:
  UiDialog(const UiDialogParams& p_Params);
  virtual ~UiDialog();

  void Init();
  void Cleanup();

protected:
  UiView* m_View = nullptr;
  UiModel* m_Model = nullptr;
  std::string m_Title;
  int m_WPerc = 0;
  int m_HPerc = 0;

  int m_X = 0;
  int m_Y = 0;
  int m_W = 0;
  int m_H = 0;

  WINDOW* m_BorderWin = nullptr;
  WINDOW* m_Win = nullptr;
};
