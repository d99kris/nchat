// uidialog.h
//
// Copyright (c) 2019-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

#include <ncurses.h>

class UiModel;
class UiView;

struct UiDialogParams
{
  // Requested geometry (WReq/HReq) may be specified as 0.0-1.0 fraction of screen size, or as
  // integer number (> 1) of columns and rows.
  UiDialogParams(UiView* p_View, UiModel* p_Model, std::string p_Title, float p_WReq, float p_HReq)
    : view(p_View)
    , model(p_Model)
    , title(p_Title)
    , wReq(p_WReq)
    , hReq(p_HReq)
  {
  }

  UiView* view = nullptr;
  UiModel* model = nullptr;
  std::string title;
  float wReq = 0;
  float hReq = 0;
};

class UiDialog
{
public:
  UiDialog(const UiDialogParams& p_Params);
  virtual ~UiDialog();

  void Init();
  void Cleanup();

  void SetFooter(const std::string& p_Footer);

private:
  void DrawBorder();

protected:
  UiView* m_View = nullptr;
  UiModel* m_Model = nullptr;

  int m_X = 0;
  int m_Y = 0;
  int m_W = 0;
  int m_H = 0;

  WINDOW* m_Win = nullptr;

private:
  std::string m_Title;
  float m_WReq = 0;
  float m_HReq = 0;

  std::string m_Footer;

  WINDOW* m_BorderWin = nullptr;
};
