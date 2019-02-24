#include "common.h"
#include <algorithm>
#include <cctype>

  #define swap_int16(data) ((data & 0xff00) >>8) | \
                           ((data & 0x00ff) << 8)
  #define swap_int32(data) ((data & 0xff000000) >> 24) | \
                           ((data & 0x00ff0000) >> 8) | \
                           ((data & 0x0000ff00) << 8)| \
                           ((data & 0x000000ff) << 24)
  #define swap_int64(data) ((data & 0xff00000000000000) >> 56) | \
                           ((data & 0x00ff000000000000) >> 40) | \
                           ((data & 0x0000ff0000000000) >> 24) | \
                           ((data & 0x000000ff00000000) >> 8) | \
                           ((data & 0x00000000ff000000) << 8) | \
                           ((data & 0x0000000000ff0000) << 24) | \
                           ((data & 0x000000000000ff00) << 40) | \
                           ((data & 0x00000000000000ff) << 56)
  
  uint16_t ReverseEndian(uint16_t data) noexcept
  {
    return swap_int16(data);
  }

  int16_t ReverseEndian(int16_t data) noexcept
  {
    return swap_int16(data);
  }

  uint32_t ReverseEndian(uint32_t data) noexcept
  {
    return swap_int32(data);
  }

  int32_t ReverseEndian(int32_t data) noexcept
  {
    return swap_int32(data);
  }

  uint64_t ReverseEndian(uint64_t data) noexcept
  {
    return swap_int64(data);
  }

  int64_t ReverseEndian(int64_t data) noexcept
  {
    return swap_int64(data);
  }

  union FloatData
  {
    int32_t byte;
    float value;
  };
    
  float ReverseEndian(float data) noexcept
  {
    FloatData fd;
    fd.value = data;
    fd.byte = ReverseEndian(fd.byte);
    return fd.value;
  }

  void ltrim(std::string &s)
  {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                       [](int ch){ return !std::isspace(ch);})
            );
  }

  void rtrim(std::string &s)
  {
    s.erase(std::find_if(s.rbegin(), s.rend(),
            [](int ch){ return !std::isspace(ch); }).base(),
            s.end());
  }

  void trim(std::string &s)
  {
    ltrim(s);
    rtrim(s);
  }

