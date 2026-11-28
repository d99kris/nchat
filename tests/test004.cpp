// test004.cpp - emoji width profiles
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <clocale>
#include <iostream>
#include <string>
#include <vector>

#include <wchar.h>

#include "termutil.h"
#include "strutil.h"

#include "unittest.h"

typedef std::vector<std::wstring> WLines;

static const std::wstring s_Heart = L"❤️";
static const std::wstring s_HeartBare = L"❤";
static const std::wstring s_Thumb = L"\U0001F44D\U0001F3FD";
static const std::wstring s_Biker = L"\U0001F6B4\U0001F3FF‍♂️";
static const std::wstring s_Family = L"\U0001F468‍\U0001F469‍\U0001F467‍\U0001F466";
static const std::wstring s_Flag = L"\U0001F1F8\U0001F1EA";
static const std::wstring s_Keycap = L"1️⃣";
static const std::wstring s_TradeMark = L"™️";

static TermUtil::Cluster Cluster(const std::wstring& p_Str)
{
  return TermUtil::GetCluster(p_Str, 0, StrUtil::GetClusterEnd(p_Str, 0));
}

// profile measured on kitty, foot, tmux 3.8 (clusters, VS16 wide)
static TermUtil::Profile ClusterProfile()
{
  TermUtil::Profile profile;
  profile.enabled = true;
  profile.vs16Width = 2;
  profile.clusters = true;
  profile.flagWidth = 2;
  profile.keycapStrip = true;
  return profile;
}

// profile measured on Terminal.app (no clusters, VS16 narrow, wide glyphs)
static TermUtil::Profile AppleTerminalProfile()
{
  TermUtil::Profile profile;
  profile.enabled = true;
  profile.vs16Width = 1;
  profile.clusters = false;
  profile.vs16Pad = true;
  profile.strip = true;
  profile.flagPad = true;
  return profile;
}

static void TestDisabled()
{
  TermUtil::SetProfile(TermUtil::Profile());
  unittest::ExpectEqual(int, StrUtil::WStringWidth(s_Heart), wcswidth(s_Heart.c_str(), s_Heart.size()));
  unittest::ExpectEqual(int, TermUtil::CharWidth(0x2764), wcwidth(0x2764));
  unittest::ExpectEqual(std::wstring, Cluster(s_Biker).chars, s_Biker);
  unittest::ExpectFalse(Cluster(s_Biker).cell);
  unittest::ExpectFalse(Cluster(s_Heart).pad);
  unittest::ExpectFalse(TermUtil::CanDrawFlags());
}

static void TestClusterProfile()
{
  TermUtil::SetProfile(ClusterProfile());

  // curses gets the same widths through wcwidth
  unittest::ExpectEqual(int, TermUtil::CharWidth(0x2764), 2);
  unittest::ExpectEqual(int, TermUtil::CharWidth(0x2122), 1); // text-first base
  unittest::ExpectEqual(int, TermUtil::CharWidth(0xFE0F), 0);
  unittest::ExpectEqual(int, TermUtil::CharWidth(0x200D), 0);

  unittest::ExpectEqual(int, Cluster(s_Heart).width, 2);
  unittest::ExpectEqual(std::wstring, Cluster(s_HeartBare).chars, s_Heart); // always emoji
  unittest::ExpectEqual(int, Cluster(s_Thumb).width, 2);
  unittest::ExpectTrue(Cluster(s_Thumb).cell);
  unittest::ExpectEqual(int, Cluster(s_Biker).width, 2);
  unittest::ExpectTrue(Cluster(s_Biker).cell);
  unittest::ExpectEqual(std::wstring, Cluster(s_Family).chars, std::wstring(L"\U0001F468")); // too long for a cell
  unittest::ExpectEqual(int, Cluster(s_Family).width, 2);
  unittest::ExpectEqual(int, Cluster(s_Flag).width, 2);
  unittest::ExpectTrue(TermUtil::CanDrawFlags());
  unittest::ExpectEqual(std::wstring, Cluster(s_Keycap).chars, std::wstring(L"1"));
  unittest::ExpectEqual(std::wstring, Cluster(s_TradeMark).chars, std::wstring(L"™")); // always text
  unittest::ExpectEqual(int, StrUtil::WStringWidth(L"Bob" + s_Heart), 5);

  // trimming and wrapping never split clusters and use the same widths
  unittest::ExpectEqual(std::wstring, StrUtil::TrimWString(L"Bob" + s_Heart, 4), std::wstring(L"Bob"));
  unittest::ExpectEqual(int, StrUtil::WStringWidth(StrUtil::TrimPadWString(s_Biker + L" Bike Group", 8)), 8);
  const WLines wrapped = { L"ab" + s_Biker, s_Heart + L"cd" };
  unittest::ExpectEqual(WLines, StrUtil::WordWrap(L"ab" + s_Biker + s_Heart + L"cd", 5, false, false, false, 2),
                        wrapped);
}

static void TestAppleTerminalProfile()
{
  TermUtil::SetProfile(AppleTerminalProfile());

  unittest::ExpectEqual(int, TermUtil::CharWidth(0x2764), 1);
  unittest::ExpectTrue(Cluster(s_Heart).pad);
  unittest::ExpectEqual(int, Cluster(s_Heart).width, 2);
  unittest::ExpectEqual(std::wstring, Cluster(s_Thumb).chars, std::wstring(L"\U0001F44D")); // stripped
  unittest::ExpectEqual(int, Cluster(s_Thumb).width, 2);
  unittest::ExpectEqual(std::wstring, Cluster(s_Biker).chars, std::wstring(L"\U0001F6B4"));
  unittest::ExpectEqual(std::wstring, Cluster(s_Family).chars, std::wstring(L"\U0001F468"));
  unittest::ExpectTrue(Cluster(s_Flag).pad);
  unittest::ExpectEqual(int, Cluster(s_Flag).width, 3);
  unittest::ExpectEqual(int, Cluster(s_Flag + std::wstring(1, (wchar_t)EMOJI_PAD)).width, 3); // entry padding
  unittest::ExpectFalse(TermUtil::CanDrawFlags()); // misaligned text after flags
  unittest::ExpectFalse(Cluster(s_Biker).cell);
}

static void TestCursor()
{
  const std::wstring pad(1, (wchar_t)EMOJI_PAD);

  // disabled: by character, skipping entry emoji padding (as before)
  TermUtil::SetProfile(TermUtil::Profile());
  const std::wstring padded = L"a\U0001F600" + pad + L"b";
  unittest::ExpectEqual(int, StrUtil::NextCursorPos(padded, 1), 3);
  unittest::ExpectEqual(int, StrUtil::PrevCursorPos(padded, 3), 1);
  unittest::ExpectEqual(int, StrUtil::SnapCursorPos(padded, 2), 1);
  unittest::ExpectEqual(int, StrUtil::NextCursorPos(L"a" + s_Heart, 1), 2); // inside the cluster

  // enabled: by character cluster
  TermUtil::SetProfile(ClusterProfile());
  const std::wstring text = L"a" + s_Biker + s_Heart + pad + L"b";
  const int bikerEnd = 1 + (int)s_Biker.size();
  const int heartEnd = bikerEnd + (int)s_Heart.size() + 1;
  unittest::ExpectEqual(int, StrUtil::NextCursorPos(text, 1), bikerEnd);
  unittest::ExpectEqual(int, StrUtil::NextCursorPos(text, bikerEnd), heartEnd);
  unittest::ExpectEqual(int, StrUtil::PrevCursorPos(text, heartEnd), bikerEnd);
  unittest::ExpectEqual(int, StrUtil::PrevCursorPos(text, bikerEnd), 1);
  unittest::ExpectEqual(int, StrUtil::SnapCursorPos(text, 3), 1);
  unittest::ExpectEqual(int, StrUtil::PrevCursorPos(L"x\n" + s_Heart, 4), 2); // per line
}

struct ProbeCase
{
  const char* name;
  int heart, thumb, flag, keycap;
  const char* xtversion;
  const char* term;
  const char* termProgram;
  bool alacrittyEnv;
  // expected
  bool clusters, vs16Pad, flagPad, strip, keycapStrip;
};

// probe results measured on real terminals (W37, 2026-11-21/22)
static const ProbeCase s_ProbeCases[] =
{
  { "Terminal.app", 1, 4, 2, 2, "", "xterm-256color", "Apple_Terminal", false, false, true, true, true, false },
  { "Terminal.app over ssh", 1, 4, 2, 2, "", "xterm-256color", "", false, false, true, true, true, false },
  { "alacritty macOS", 1, 4, 2, 1, "", "xterm-256color", "Apple_Terminal", true, false, false, false, true, false },
  { "alacritty linux", 1, 4, 2, 1, "", "alacritty", "", false, false, false, false, true, false },
  { "st", 1, 4, 2, 1, "", "st-256color", "", false, false, false, true, true, false },
  { "gnome-terminal", 1, 4, 2, 1, "VTE(8400)", "xterm-256color", "", false, false, true, true, true, false },
  { "xterm", 1, 4, 2, 1, "XTerm(407)", "xterm", "", false, false, true, true, true, false },
  { "kitty", 2, 2, 2, 2, "kitty(0.46.2)", "xterm-kitty", "", false, true, false, false, false, true },
  { "foot", 2, 2, 2, 2, "foot(1.25.0)", "foot", "", false, true, false, false, false, true },
  { "konsole", 2, 2, 2, 2, "Konsole 25.12.3", "xterm-256color", "", false, true, false, false, false, true },
  { "iTerm2 3.4", 1, 2, 1, 1, "iTerm2 3.4.23", "xterm-256color", "iTerm.app", false, true, true, false, false, false },
  { "tmux 3.6", 2, 4, 2, 2, "tmux 3.6", "tmux-256color", "", false, false, false, true, true, false },
  { "tmux 3.8", 2, 2, 2, 2, "tmux 3.8", "tmux-256color", "", false, true, false, false, false, true },
};

static void TestProfileFromProbe()
{
  for (const ProbeCase& c : s_ProbeCases)
  {
    const TermUtil::Profile profile =
      TermUtil::ProfileFromProbe(c.heart, c.thumb, c.flag, c.keycap, c.xtversion, c.term, c.termProgram,
                                 c.alacrittyEnv);
    const std::string name = c.name;
    unittest::ExpectEqual(std::string, name + (profile.clusters ? " clusters" : ""),
                          name + (c.clusters ? " clusters" : ""));
    unittest::ExpectEqual(std::string, name + (profile.vs16Pad ? " vs16pad" : ""),
                          name + (c.vs16Pad ? " vs16pad" : ""));
    unittest::ExpectEqual(std::string, name + (profile.flagPad ? " flagpad" : ""),
                          name + (c.flagPad ? " flagpad" : ""));
    unittest::ExpectEqual(std::string, name + (profile.strip ? " strip" : ""), name + (c.strip ? " strip" : ""));
    unittest::ExpectEqual(std::string, name + (profile.keycapStrip ? " keycapstrip" : ""),
                          name + (c.keycapStrip ? " keycapstrip" : ""));
  }
}

int main()
{
  int rv = 0;

  setlocale(LC_ALL, "");

  try
  {
    // emoji widths depend on a utf-8 capable locale being present
    if (wcwidth(0x1F44D) == 2)
    {
      TestDisabled();
      TestClusterProfile();
      TestAppleTerminalProfile();
      TestCursor();
      TestProfileFromProbe();
    }
    else
    {
      std::cout << "skipping emoji width tests, no utf-8 locale" << std::endl;
    }
  }
  catch (const std::exception& ex)
  {
    std::cout << "exception: " << ex.what() << std::endl;
    rv = 1;
  }

  TermUtil::SetProfile(TermUtil::Profile());
  return rv;
}
