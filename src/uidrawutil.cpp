// uidrawutil.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uidrawutil.h"

#include <cstring>

#include "emojiwidthutil.h"
#include "log.h"
#include "strutil.h"

#ifdef HAS_EMOJIWIDTH
#ifdef wcwidth
#undef wcwidth // defined as a macro on some platforms, the function itself is replaced here
#endif

// Own wcwidth, used by ncurses instead of the libc one: through symbol interposition on
// Linux, and through static linking of ncurses on macOS. Gives curses the same widths
// as nchat's layout.
extern "C" int wcwidth(wchar_t p_Wc)
{
  return EmojiWidthUtil::CharWidth(p_Wc);
}
#endif

int UiDrawUtil::AddWStr(WINDOW* p_Win, int p_Y, int p_X, const std::wstring& p_Str, int p_MaxWidth)
{
  wmove(p_Win, p_Y, p_X);
  if (!EmojiWidthUtil::GetProfile().enabled)
  {
    std::wstring str = (p_MaxWidth >= 0) ? StrUtil::TrimWString(p_Str, p_MaxWidth) : p_Str;
    waddnwstr(p_Win, str.c_str(), str.size());
    return StrUtil::WStringWidth(str);
  }

  attr_t attrs = 0;
  short pair = 0;
  wattr_get(p_Win, &attrs, &pair, nullptr);

  int width = 0;
  size_t pos = 0;
  while (pos < p_Str.size())
  {
    const size_t end = StrUtil::GetClusterEnd(p_Str, pos);
    const EmojiWidthUtil::Cluster cluster = EmojiWidthUtil::GetCluster(p_Str, pos, end);
    pos = end;
    if ((p_MaxWidth >= 0) && ((width + cluster.width) > p_MaxWidth)) break;

    if (cluster.cell && (cluster.chars.size() <= CCHARW_MAX))
    {
      // whole cluster in one cell, so curses reserves the width of its base only
      cchar_t cc;
      memset(&cc, 0, sizeof(cc));
      wchar_t base[2] = { cluster.chars[0], 0 };
      setcchar(&cc, base, attrs, pair, nullptr);
      for (size_t i = 1; i < cluster.chars.size(); ++i)
      {
        cc.chars[i] = cluster.chars[i];
      }
      wadd_wch(p_Win, &cc);
    }
    else
    {
      waddnwstr(p_Win, cluster.chars.c_str(), cluster.chars.size());
    }

    if (cluster.pad)
    {
      waddch(p_Win, ' ');
    }

    width += cluster.width;
  }

  return width;
}

void UiDrawUtil::SelfCheck()
{
  if (!EmojiWidthUtil::GetProfile().enabled) return;

  // a heart with vs16 must take the probed width in curses
  WINDOW* pad = newpad(1, 8);
  const std::wstring heart = L"❤️";
  const int expected = StrUtil::WStringWidth(heart);
  const int drawn = AddWStr(pad, 0, 0, heart);
  const int curx = getcurx(pad);
  delwin(pad);
  if ((curx != expected) || (drawn != expected))
  {
    LOG_WARNING("emoji width self check failed (%d %d %d), disabled", curx, drawn, expected);
    EmojiWidthUtil::Disable();
  }
  else
  {
    LOG_DEBUG("emoji width self check ok (%d)", curx);
  }
}
