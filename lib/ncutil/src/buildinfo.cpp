// buildinfo.cpp
//
// Copyright (c) 2026 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "sysutil.h"

// The volatile build origin/sha/branch are read from the generated buildinfo.h
// here, isolated in this trivial translation unit so that a git sha/branch
// change (which rewrites buildinfo.h on every commit) recompiles only this file
// rather than the heavy units that call these accessors. See buildinfo.h.in /
// lib/ncutil/CMakeLists.txt.
#include "buildinfo.h"

std::string SysUtil::GetBuildOrigin()
{
#if defined(NCHAT_BUILD_ORIGIN)
  const std::string origin = NCHAT_BUILD_ORIGIN;
  return origin.empty() ? "local" : origin;
#else
  return "local";
#endif
}

std::string SysUtil::GetBuildGitSha()
{
#if defined(NCHAT_BUILD_GIT_SHA)
  const std::string sha = NCHAT_BUILD_GIT_SHA;
  return sha.empty() ? "unknown" : sha;
#else
  return "unknown";
#endif
}

std::string SysUtil::GetBuildGitBranch()
{
#if defined(NCHAT_BUILD_GIT_BRANCH)
  return NCHAT_BUILD_GIT_BRANCH;
#else
  return "";
#endif
}
