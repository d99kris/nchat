// serialization.h
//
// Copyright (c) 2024 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#pragma once

#include <fstream>

#include <cereal/archives/binary.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/string.hpp>

#include "log.h"

class Serialization
{
public:
  template<typename T>
  static std::vector<char> ToBytes(const T& p_Data)
  {
    try
    {
      std::stringstream sstream;
      {
        cereal::BinaryOutputArchive outputArchive(sstream);
        outputArchive(p_Data);
      }
      const std::string& str = sstream.str();
      return std::vector<char>(str.begin(), str.end());
    }
    catch (...)
    {
      LOG_WARNING("failed to serialize to bytes");
    }

    return std::vector<char>();
  }

  template<typename T>
  static T FromBytes(const std::vector<char>& p_Bytes)
  {
    T data;
    if (p_Bytes.empty()) return data;

    try
    {
      std::stringstream sstream(std::string(p_Bytes.begin(), p_Bytes.end()));
      {
        cereal::BinaryInputArchive inputArchive(sstream);
        inputArchive(data);
      }
    }
    catch (...)
    {
      LOG_WARNING("failed to deserialize from bytes");
    }

    return data;
  }

  template<typename T>
  void ToFile(const std::string& p_File, const T& p_Data)
  {
    try
    {
      std::ofstream fstream(p_File, std::ios::binary);
      {
        cereal::BinaryOutputArchive outputArchive(fstream);
        outputArchive(p_Data);
      }
    }
    catch (...)
    {
      LOG_WARNING("failed to serialize to file");
    }
  }

  template<typename T>
  static T FromFile(const std::string& p_File)
  {
    T data;

    try
    {
      std::ifstream fstream(p_File, std::ios::binary);
      {
        cereal::BinaryInputArchive inputArchive(fstream);
        inputArchive(data);
      }
    }
    catch (...)
    {
      LOG_WARNING("failed to deserialize from file");
    }

    return data;
  }

  template<typename T>
  static std::string ToString(const T& p_Data)
  {
    std::string str;

    try
    {
      std::stringstream sstream;
      {
        cereal::BinaryOutputArchive outputArchive(sstream);
        outputArchive(p_Data);
      }

      str = sstream.str();
    }
    catch (...)
    {
      LOG_WARNING("failed to serialize to string");
    }

    return str;
  }

  template<typename T>
  static T FromString(const std::string& p_Str)
  {
    T data;
    if (p_Str.empty()) return data;

    try
    {
      std::stringstream sstream(p_Str);
      {
        cereal::BinaryInputArchive inputArchive(sstream);
        inputArchive(data);
      }
    }
    catch (...)
    {
      LOG_WARNING("failed to deserialize from string");
    }

    return data;
  }
};

template<class Archive>
void serialize(Archive& p_Archive, Reactions& p_Reactions, const uint32_t p_Version)
{
  // For versioning example, see https://github.com/USCiLab/cereal/issues/340
  if (p_Version == 1)
  {
    p_Archive(p_Reactions.senderEmojis, p_Reactions.emojiCounts);
  }
}
CEREAL_CLASS_VERSION(Reactions, 1)
