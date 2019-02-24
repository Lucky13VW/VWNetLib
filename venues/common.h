#pragma once

#include <stdint.h>
#include <memory>
#include <string>

  uint16_t ReverseEndian(uint16_t data) noexcept;
  int16_t ReverseEndian(int16_t data) noexcept;
  uint32_t ReverseEndian(uint32_t data) noexcept;
  int32_t ReverseEndian(int32_t data) noexcept;
  uint64_t ReverseEndian(uint64_t data) noexcept;
  int64_t ReverseEndian(int64_t data) noexcept;
  float ReverseEndian(float data) noexcept;
  
  // make_unique is not provided in current g++ compilor
  template<typename T, typename... Args>
    std::unique_ptr<T> _make_unique(Args&&... args)
  {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }
  
  // trim from start (in place)
  void ltrim(std::string &s);
  // trim from end (in place)
  void rtrim(std::string &s);
  // trim from both ends (in place)
  void trim(std::string &s);

#ifndef BSE_LITTLE_ENDIAN
#define FormatData(data) ReverseEndian(data)
#else
#define FormatData(data) data
#endif
