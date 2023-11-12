// uikeydump.cpp
//
// Copyright (c) 2022-2023 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uikeydump.h"

#include <locale.h>
#include <unistd.h>

#include <sys/select.h>

#include <ncurses.h>

#include "uikeyconfig.h"

void UiKeyDump::Run()
{
  setlocale(LC_ALL, "");
  initscr();
  noecho();
  cbreak();
  raw();
  keypad(stdscr, TRUE);
  curs_set(0);
  timeout(0);

  printw("key code dump mode - press ctrl-c or 'q' to exit\n");
  refresh();

  bool running = true;
  while (running)
  {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    int maxfd = STDIN_FILENO;
    struct timeval tv = {1, 0};
    select(maxfd + 1, &fds, NULL, NULL, &tv);
    if (FD_ISSET(STDIN_FILENO, &fds))
    {
      int y = 0;
      int x = 0;
      int maxy = 0;
      int maxx = 0;
      getyx(stdscr, y, x);
      getmaxyx(stdscr, maxy, maxx);
      if (y == (maxy - 1))
      {
        clear();
        refresh();
      }

      int count = 0;
      wint_t key = 0;
      wint_t keyOk = 0;
      while (get_wch(&key) != ERR)
      {
        keyOk = key;
        ++count;
        printw("\\%o", key);

        if ((key == 3) || (key == 'q'))
        {
          running = false;
          break;
        }
      }

      if ((keyOk != 0) && (count == 1))
      {
        std::string keyName = UiKeyConfig::GetKeyName(keyOk);
        if (!keyName.empty())
        {
          printw(" %s", keyName.c_str());
        }
      }

      printw("\n");
      refresh();
    }
  }

  wclear(stdscr);
  endwin();
}
