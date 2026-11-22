// emojiwidthutil.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "emojiwidthutil.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <regex>
#include <vector>

#include <dlfcn.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "log.h"
#include "strutil.h"

static EmojiWidthUtil::Profile s_Profile;

static const wchar_t s_Vs16Bases[] =
{
#include "emojiwidthutil_vs16.inc"
};

static bool IsVs16Base(wchar_t p_Wc)
{
  return std::binary_search(std::begin(s_Vs16Bases), std::end(s_Vs16Bases), p_Wc);
}

// Bases in Miscellaneous Symbols and Dingbats are mostly used as emoji (heart, smiley,
// sun), the remaining ones mostly as text (trademark, arrows, play symbol).
static bool IsEmojiFirst(wchar_t p_Wc)
{
  return (p_Wc >= 0x2600) && (p_Wc <= 0x27BF) && (p_Wc != 0x27A1);
}

static bool IsRegionalIndicator(wchar_t p_Wc)
{
  return (p_Wc >= 0x1F1E6) && (p_Wc <= 0x1F1FF);
}

struct WidthRange
{
  wchar_t first;
  wchar_t last;
};

#include "emojiwidthutil_table.inc"

static bool InRanges(const WidthRange* p_Begin, const WidthRange* p_End, wchar_t p_Wc)
{
  const WidthRange* it = std::upper_bound(p_Begin, p_End, p_Wc,
                                          [](wchar_t p_Val, const WidthRange& p_Range) {
    return p_Val < p_Range.first;
  });
  return (it != p_Begin) && (p_Wc <= (it - 1)->last);
}

// Width from the bundled Unicode table, used when libc's wcwidth is not reachable.
static int TableWidth(wchar_t p_Wc)
{
  if (p_Wc == 0) return 0;

  if ((p_Wc < 0x20) || ((p_Wc >= 0x7F) && (p_Wc < 0xA0))) return -1;

  if (InRanges(std::begin(s_ZeroWidth), std::end(s_ZeroWidth), p_Wc)) return 0;

  if (InRanges(std::begin(s_DoubleWidth), std::end(s_DoubleWidth), p_Wc)) return 2;

  return 1;
}

typedef int (* WcwidthFunc)(wchar_t);

// The libc wcwidth, looked up past nchat's own wcwidth (which ncurses uses). Calling
// wcswidth instead is not safe: e.g. static musl implements it with the exported
// wcwidth, i.e. nchat's own, which would recurse. Not available in fully static
// builds, which use the bundled table.
static WcwidthFunc GetLibcWcwidth()
{
  static WcwidthFunc func = []() -> WcwidthFunc
  {
#if defined(RTLD_NEXT)
    return reinterpret_cast<WcwidthFunc>(dlsym(RTLD_NEXT, "wcwidth"));
#else
    return nullptr;
#endif
  }();
  return func;
}

static int LibcWidth(wchar_t p_Wc)
{
  // guard against the lookup finding nchat's own wcwidth (e.g. in static builds)
  static thread_local int depth = 0;
  WcwidthFunc func = GetLibcWcwidth();
  if ((func == nullptr) || (depth > 0)) return TableWidth(p_Wc);

  ++depth;
  const int width = func(p_Wc);
  --depth;
  return width;
}

int EmojiWidthUtil::CharWidth(wchar_t p_Wc)
{
  if (!s_Profile.enabled) return LibcWidth(p_Wc);

  if ((p_Wc == 0x200D) || ((p_Wc >= 0xFE00) && (p_Wc <= 0xFE0F)) || (p_Wc == 0x20E3) || (p_Wc == EMOJI_PAD)) return 0;

  if (s_Profile.clusters && IsRegionalIndicator(p_Wc)) return s_Profile.flagWidth;

  if ((s_Profile.vs16Width == 2) && IsVs16Base(p_Wc) && IsEmojiFirst(p_Wc)) return 2;

  const int width = LibcWidth(p_Wc);
  if ((width < 0) && (p_Wc >= 0x1F000) && (p_Wc <= 0x1FAFF)) return 2; // emoji newer than libc

  return width;
}

EmojiWidthUtil::Cluster EmojiWidthUtil::GetCluster(const std::wstring& p_Str, size_t p_Pos, size_t p_End)
{
  Cluster cluster;
  cluster.chars = p_Str.substr(p_Pos, p_End - p_Pos);
  std::wstring& chars = cluster.chars;
  if (chars.empty()) return cluster;

  if (!s_Profile.enabled)
  {
    const int width = wcswidth(chars.c_str(), chars.size());
    cluster.width = (width != -1) ? width : (int)chars.size();
    return cluster;
  }

  const wchar_t base = chars[0];
  const bool isFlag = (chars.size() == 2) && IsRegionalIndicator(base) && IsRegionalIndicator(chars[1]);
  const bool isKeycap = (chars.find(0x20E3) != std::wstring::npos);

  // one presentation per base: emoji-first bases (e.g. heart) always as emoji, and on
  // terminals drawing VS16 emoji wide (curses gets one width per base) the other bases
  // always as text
  if (IsVs16Base(base) && !isKeycap)
  {
    const bool hasVs16 = (chars.size() > 1) && (chars[1] == 0xFE0F);
    if (IsEmojiFirst(base) && (chars.size() == 1))
    {
      chars += 0xFE0F;
    }
    else if ((s_Profile.vs16Width == 2) && !IsEmojiFirst(base) && hasVs16)
    {
      chars.erase(1, 1);
    }
  }

  if (s_Profile.strip && (chars.size() > 1) && !isFlag)
  {
    // keep base and its variation selector, drop modifiers and zwj tails
    std::wstring stripped(1, base);
    if (!isKeycap && (chars[1] == 0xFE0F)) stripped += 0xFE0F;
    chars = stripped;
  }

  if (s_Profile.clusters)
  {
    if (isKeycap && s_Profile.keycapStrip)
    {
      chars = std::wstring(1, base);
    }

    if (chars.size() > s_MaxCellChars)
    {
      // too long for a curses cell (e.g. family, subdivision flag): keep the first
      // emoji with its modifier / variation selector, which is always a valid glyph
      size_t keep = 1;
      while ((keep < chars.size()) && (keep < s_MaxCellChars) &&
             (((chars[keep] >= 0x1F3FB) && (chars[keep] <= 0x1F3FF)) || (chars[keep] == 0xFE0F)))
      {
        ++keep;
      }
      chars.resize(keep);
    }

    cluster.cell = (chars.size() > 1);
  }

  if (cluster.cell)
  {
    cluster.width = std::max(CharWidth(chars[0]), 0);
  }
  else
  {
    for (wchar_t wc : chars)
    {
      const int width = CharWidth(wc);
      cluster.width += (width >= 0) ? width : 1;
    }
  }

  const bool vs16Emoji = (chars.size() >= 2) && (chars[1] == 0xFE0F) && IsVs16Base(base) && !isKeycap;
  if ((s_Profile.vs16Pad && vs16Emoji && (cluster.width == 1)) || (s_Profile.flagPad && isFlag))
  {
    cluster.pad = true;
    cluster.width += 1;
  }

  return cluster;
}

int EmojiWidthUtil::StringWidth(const std::wstring& p_Str)
{
  int width = 0;
  size_t pos = 0;
  while (pos < p_Str.size())
  {
    const size_t end = StrUtil::GetClusterEnd(p_Str, pos);
    width += GetCluster(p_Str, pos, end).width;
    pos = end;
  }
  return width;
}

const EmojiWidthUtil::Profile& EmojiWidthUtil::GetProfile()
{
  return s_Profile;
}

void EmojiWidthUtil::Disable()
{
  s_Profile.enabled = false;
}

void EmojiWidthUtil::SetProfile(const Profile& p_Profile)
{
  s_Profile = p_Profile;
}

std::string EmojiWidthUtil::ProfileStr()
{
  const Profile& p = s_Profile;
  return "enabled=" + std::to_string(p.enabled) + " vs16=" + std::to_string(p.vs16Width) +
         " clusters=" + std::to_string(p.clusters) + " flag=" + std::to_string(p.flagWidth) +
         " keycapstrip=" + std::to_string(p.keycapStrip) + " vs16pad=" + std::to_string(p.vs16Pad) +
         " strip=" + std::to_string(p.strip) + " flagpad=" + std::to_string(p.flagPad) +
         " terminal='" + p.terminal + "'";
}

// Derives the profile from the probe: the cursor advance of a heart with VS16, a thumbs up
// with skin tone, a flag and a keycap, plus the terminal's xtversion reply (if any).
// Terminal drawing quirks which cannot be measured are keyed on what can be:
// - VS16 emoji drawn wider than their one-cell advance (Terminal.app, VTE) get a pad
// space; except on unidentified terminals advancing a keycap by one cell (st,
// alacritty), which draw them as narrow text glyphs.
// - flags on non-clustering terminals get a pad space (Terminal.app, VTE, st draw them
// wider than two cells; behind tmux the outer terminal is unknown), except on
// alacritty; elsewhere this only adds a gap. Alacritty has no xtversion reply and its
// TERM is xterm-256color when its own terminfo is not installed (e.g. on macOS).
// - TERM_PROGRAM is only used for logging: ssh does not forward it, and terminals
// started from Terminal.app inherit it.
EmojiWidthUtil::Profile EmojiWidthUtil::ProfileFromProbe(int p_Heart, int p_Thumb, int p_Flag, int p_Keycap,
                                                         const std::string& p_XtVersion, const std::string& p_Term,
                                                         const std::string& p_TermProgram, bool p_AlacrittyEnv)
{
  Profile profile;
  profile.enabled = true;
  profile.vs16Width = p_Heart;
  profile.clusters = (p_Thumb == 2);
  profile.flagWidth = p_Flag;
  profile.keycapStrip = profile.clusters && (p_Keycap != 1);
  profile.strip = !profile.clusters;

  const bool isUnidentified = p_XtVersion.empty();
  const bool isAlacritty = isUnidentified && ((p_Term.rfind("alacritty", 0) == 0) || p_AlacrittyEnv);
  profile.vs16Pad = (p_Heart == 1) && !(isUnidentified && (p_Keycap == 1));
  profile.flagPad = !profile.clusters && !isAlacritty;
  profile.terminal = isAlacritty ? "alacritty" : (!isUnidentified ? p_XtVersion : p_TermProgram);
  return profile;
}

// Measures the terminal's cursor advance for a few samples (one round trip: all
// samples and cursor position requests are written at once), before curses starts.
void EmojiWidthUtil::Probe(bool p_Enabled)
{
  s_Profile = Profile();
  if (!p_Enabled) return;

  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;

  struct termios oldTios;
  if (tcgetattr(STDIN_FILENO, &oldTios) != 0) return;

  struct termios rawTios = oldTios;
  rawTios.c_lflag &= ~(ICANON | ECHO);
  rawTios.c_cc[VMIN] = 0;
  rawTios.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &rawTios);

  static const std::vector<std::string> samples =
  {
    "\xe2\x9d\xa4\xef\xb8\x8f",                 // heart + vs16
    "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",         // thumbs up + skin tone
    "\xf0\x9f\x87\xb8\xf0\x9f\x87\xaa",         // flag se
    "1\xef\xb8\x8f\xe2\x83\xa3",                // keycap 1
  };

  std::string query = "\0337\033[?1049h\033[>0q"; // save cursor, alternate screen, xtversion
  for (const auto& sample : samples)
  {
    query += "\r\033[2K" + sample + "\033[6n";
  }
  query += "\r\033[2K\033[?1049l\0338";
  ssize_t written = write(STDOUT_FILENO, query.c_str(), query.size());
  (void)written;

  std::string reply;
  int replies = 0;
  const int timeoutMs = 1000; // one round trip, also over ssh
  int waitedMs = 0;
  while ((replies < (int)samples.size()) && (waitedMs < timeoutMs))
  {
    struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
    if (poll(&pfd, 1, 20) > 0)
    {
      char buf[256];
      ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));
      if (len > 0)
      {
        reply.append(buf, len);
        replies = std::count(reply.begin(), reply.end(), 'R');
      }
    }
    else
    {
      waitedMs += 20;
    }
  }

  if (replies < (int)samples.size())
  {
    // replies arriving later would otherwise be read as key presses
    usleep(250 * 1000);
    tcflush(STDIN_FILENO, TCIFLUSH);
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldTios);

  std::vector<int> cols;
  static const std::regex cprRegex("\033\\[\\d+;(\\d+)R");
  for (auto it = std::sregex_iterator(reply.begin(), reply.end(), cprRegex); it != std::sregex_iterator(); ++it)
  {
    cols.push_back(std::stoi((*it)[1].str()) - 1);
  }

  std::smatch match;
  static const std::regex xtRegex("\033P>\\|([^\033]*)\033\\\\");
  std::string xtversion = std::regex_search(reply, match, xtRegex) ? match[1].str() : "";
  const char* termProgram = getenv("TERM_PROGRAM");
  s_Profile.terminal = !xtversion.empty() ? xtversion : ((termProgram != nullptr) ? termProgram : "");

  if (cols.size() != samples.size())
  {
    LOG_WARNING("emoji width probe failed, got %d of %d replies", (int)cols.size(), (int)samples.size());
    return;
  }

  const int heart = cols[0];
  const int thumb = cols[1];
  const int flag = cols[2];
  const int keycap = cols[3];
  if ((heart < 1) || (heart > 2) || (flag < 1) || (flag > 2))
  {
    LOG_WARNING("emoji width probe unexpected %d %d %d %d", heart, thumb, flag, keycap);
    return;
  }

  const char* term = getenv("TERM");
  const bool alacrittyEnv = (getenv("ALACRITTY_WINDOW_ID") != nullptr) || (getenv("ALACRITTY_SOCKET") != nullptr);
  s_Profile = ProfileFromProbe(heart, thumb, flag, keycap, xtversion, (term != nullptr) ? term : "",
                               (termProgram != nullptr) ? termProgram : "", alacrittyEnv);

  LOG_INFO("emoji width probe %d %d %d %d: %s", heart, thumb, flag, keycap, ProfileStr().c_str());
}
