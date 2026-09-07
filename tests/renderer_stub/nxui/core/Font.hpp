#pragma once
#include <nxui/core/Types.hpp>
#include <cstdint>
namespace nxui {struct Font {
 Vec2 measure(const std::string& s) const {
  size_t n=0;for(unsigned char c:s)if((c&0xc0)!=0x80)++n;
  return {float(n)*12.f,28.f};
 }
 int ptSize() const {return 24;}
 uint64_t revision() const {return 1;}
};}
