// emojiwidthutil.h
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <string>

// Terminal-specific display width of emoji. The terminal is probed at startup, and the
// resulting profile decides both the widths (also given to ncurses through an own
// wcwidth) and how character clusters are drawn.
class EmojiWidthUtil
{
public:
  struct Profile
  {
    bool enabled = false;     // false: plain wcwidth, as before
    int vs16Width = 1;        // width of an emoji-first base + VS16, e.g. heart
    bool clusters = false;    // terminal draws modifier / zwj sequences as one glyph
    int flagWidth = 2;        // width of a regional indicator pair, when clusters
    bool keycapStrip = false; // draw keycaps as their base character
    bool vs16Pad = false;     // pad VS16 emoji with a space (glyph wider than advance)
    bool strip = false;       // draw modifier / zwj sequences as their first emoji
    bool flagPad = false;     // pad flags with a space (Terminal.app)
    std::string terminal;     // xtversion reply or TERM_PROGRAM, for logging
  };

  struct Cluster
  {
    std::wstring chars; // code points to output
    int width = 0;      // display width, including pad
    bool cell = false;  // output as one curses cell (base + combining)
    bool pad = false;   // output a space after
  };

  static void Probe(bool p_Enabled);
  static void Disable();
  static void SetProfile(const Profile& p_Profile); // for tests
  static Profile ProfileFromProbe(int p_Heart, int p_Thumb, int p_Flag, int p_Keycap, const std::string& p_XtVersion,
                                  const std::string& p_Term, const std::string& p_TermProgram, bool p_AlacrittyEnv);
  static const Profile& GetProfile();
  static int CharWidth(wchar_t p_Wc);
  static Cluster GetCluster(const std::wstring& p_Str, size_t p_Pos, size_t p_End);
  static int StringWidth(const std::wstring& p_Str);
  static std::string ProfileStr();

  static const size_t s_MaxCellChars = 5; // ncurses CCHARW_MAX default
};
