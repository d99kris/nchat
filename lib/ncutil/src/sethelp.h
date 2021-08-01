// sethelp.h
//
// Copyright (c) 2019-2021 Kristofer Berggren
// All rights reserved.
//
// nmail is distributed under the MIT license, see LICENSE for details.

#pragma once

template<typename T>
std::set<T> operator+(std::set<T> p_Lhs, const std::set<T>& p_Rhs)
{
  p_Lhs.insert(p_Rhs.begin(), p_Rhs.end());
  return p_Lhs;
}

template<typename T>
std::set<T> operator-(std::set<T> p_Lhs, const std::set<T>& p_Rhs)
{
  for (typename std::set<T>::iterator it = p_Lhs.begin(); it != p_Lhs.end(); /* inc in loop */)
  {
    it = (p_Rhs.find(*it) != p_Rhs.end()) ? p_Lhs.erase(it) : std::next(it);
  }
  return p_Lhs;
}

template<typename T>
std::vector<T> ToVector(const std::set<T>& p_Set)
{
  std::vector<T> vec(p_Set.size());
  std::copy(p_Set.begin(), p_Set.end(), vec.begin());
  return vec;
}

template<typename T>
std::set<T> ToSet(const std::vector<T>& p_Vector)
{
  return std::set<T>(p_Vector.begin(), p_Vector.end());
}
