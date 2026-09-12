// test002.cpp - config file load, save and external modification handling
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <sys/time.h>

#include "config.h"

#include "unittest.h"

typedef std::map<std::string, std::string> ConfigMap;

static const std::string s_Path = "test002.conf";

static const ConfigMap s_Default =
{
  { "alpha", "1" },
  { "beta", "2" },
};

static void WriteFile(const std::string& p_Path, const std::string& p_Str)
{
  std::ofstream stream;
  stream.open(p_Path, std::ios::binary);
  stream << p_Str;
  stream.close();
}

static std::string ReadFile(const std::string& p_Path)
{
  std::ifstream stream;
  stream.open(p_Path, std::ios::binary);
  std::stringstream ss;
  ss << stream.rdbuf();
  return ss.str();
}

// Ages the file by the specified number of seconds, rather than waiting for
// wall clock time to pass, so that the test is fast and deterministic also on
// file systems with coarse timestamp granularity.
static void AgeFile(const std::string& p_Path, int p_Seconds)
{
  struct stat st { };
  unittest::ExpectEqual(int, stat(p_Path.c_str(), &st), 0);

  struct timeval times[2] { };
#if defined(__APPLE__)
  times[0].tv_sec = st.st_atimespec.tv_sec;
  times[1].tv_sec = st.st_mtimespec.tv_sec - p_Seconds;
#else
  times[0].tv_sec = st.st_atim.tv_sec;
  times[1].tv_sec = st.st_mtim.tv_sec - p_Seconds;
#endif

  unittest::ExpectEqual(int, utimes(p_Path.c_str(), times), 0);
}

static void Cleanup()
{
  remove(s_Path.c_str());
}

static void TestCreateDefault()
{
  Cleanup();

  Config config(s_Path, s_Default);
  unittest::ExpectEqual(std::string, config.Get("alpha"), "1");
  unittest::ExpectEqual(std::string, config.Get("beta"), "2");

  // a missing file is created on load, readable by the owner only
  struct stat st { };
  unittest::ExpectEqual(int, stat(s_Path.c_str(), &st), 0);
  unittest::ExpectEqual(int, (int)(st.st_mode & 0777), 0600);
  unittest::ExpectEqual(std::string, ReadFile(s_Path), "alpha=1\nbeta=2\n");

  Cleanup();
}

static void TestAccessors()
{
  Cleanup();

  Config config(s_Path, s_Default);

  config.Set("alpha", "10");
  unittest::ExpectEqual(std::string, config.Get("alpha"), "10");

  unittest::ExpectTrue(config.Exist("beta"));
  config.Delete("beta");
  unittest::ExpectFalse(config.Exist("beta"));

  const ConfigMap map = config.GetMap();
  unittest::ExpectEqual(size_t, map.size(), 1);
  unittest::ExpectEqual(std::string, map.at("alpha"), "10");

  Cleanup();
}

static void TestParse()
{
  Cleanup();

  // comments, blank lines and surrounding whitespace are ignored, and params
  // not present in the default map are dropped
  WriteFile(s_Path,
            "# comment\n"
            "\n"
            "  alpha  =  11  \n"
            "gamma=3\n");

  Config config(s_Path, s_Default);
  unittest::ExpectEqual(std::string, config.Get("alpha"), "11");
  unittest::ExpectEqual(std::string, config.Get("beta"), "2");
  unittest::ExpectFalse(config.Exist("gamma"));

  Cleanup();
}

static void TestSaveRoundTrip()
{
  Cleanup();

  {
    Config config(s_Path, s_Default);
    config.Set("beta", "20");
    config.Save();
  }

  Config config(s_Path, s_Default);
  unittest::ExpectEqual(std::string, config.Get("alpha"), "1");
  unittest::ExpectEqual(std::string, config.Get("beta"), "20");

  Cleanup();
}

static void TestSaveUnmodified()
{
  Cleanup();

  Config config(s_Path, s_Default);
  config.Set("alpha", "100");

  // the file has not been touched since load, so the save goes through
  config.Save();
  unittest::ExpectEqual(std::string, ReadFile(s_Path), "alpha=100\nbeta=2\n");

  Cleanup();
}

static void TestSkipSaveExternallyModified()
{
  Cleanup();

  WriteFile(s_Path, "# hand written header\nalpha=5\n");

  Config config(s_Path, s_Default);
  unittest::ExpectEqual(std::string, config.Get("alpha"), "5");

  config.Set("alpha", "6");

  // simulate an edit made while the config was loaded
  const std::string external = "# hand written header\nalpha=7\n";
  WriteFile(s_Path, external);
  AgeFile(s_Path, 1);

  // the save is skipped, leaving both the comment and the external value
  config.Save();
  unittest::ExpectEqual(std::string, ReadFile(s_Path), external);

  Cleanup();
}

static void TestSkipSaveDeleted()
{
  Cleanup();

  Config config(s_Path, s_Default);

  // deleting the file counts as an external modification, so it is not
  // recreated on save
  Cleanup();
  config.Save();

  struct stat st { };
  unittest::ExpectEqual(int, stat(s_Path.c_str(), &st), -1);
}

static void TestSaveEpochModTime()
{
  Cleanup();

  WriteFile(s_Path, "alpha=5\n");

  // a file timestamped at the epoch must still be guarded, i.e. the mod time
  // sentinel must not collide with a valid timestamp
  struct timeval times[2] { };
  unittest::ExpectEqual(int, utimes(s_Path.c_str(), times), 0);

  Config config(s_Path, s_Default);
  unittest::ExpectEqual(std::string, config.Get("alpha"), "5");

  const std::string external = "alpha=9\n";
  WriteFile(s_Path, external);
  AgeFile(s_Path, 1);

  config.Save();
  unittest::ExpectEqual(std::string, ReadFile(s_Path), external);

  Cleanup();
}

static void TestSaveOtherPath()
{
  Cleanup();

  const std::string otherPath = "test002-other.conf";
  WriteFile(otherPath, "placeholder\n");
  AgeFile(otherPath, 60);

  Config config(s_Path, s_Default);
  config.Set("alpha", "30");

  // the tracked mod time refers to the loaded path only, so an explicit save
  // to another path must not be skipped, whatever that path's mod time is
  config.Save(otherPath);
  unittest::ExpectEqual(std::string, ReadFile(otherPath), "alpha=30\nbeta=2\n");

  // and it must not have disturbed the tracking of the loaded path
  config.Save();
  unittest::ExpectEqual(std::string, ReadFile(s_Path), "alpha=30\nbeta=2\n");

  remove(otherPath.c_str());
  Cleanup();
}

int main()
{
  int rv = 0;

  try
  {
    TestCreateDefault();
    TestAccessors();
    TestParse();
    TestSaveRoundTrip();
    TestSaveUnmodified();
    TestSkipSaveExternallyModified();
    TestSkipSaveDeleted();
    TestSaveEpochModTime();
    TestSaveOtherPath();
  }
  catch (const std::exception& ex)
  {
    std::cout << "exception: " << ex.what() << std::endl;
    rv = 1;
  }

  Cleanup();

  return rv;
}
