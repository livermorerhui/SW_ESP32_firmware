#pragma once

#include <cstdint>
#include <string>

using std::uint8_t;
using std::uint32_t;

class String {
public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}

  bool operator==(const char* other) const {
    return value_ == (other ? other : "");
  }

  bool equalsIgnoreCase(const char* other) const {
    const std::string rhs(other ? other : "");
    if (rhs.size() != value_.size()) return false;
    for (size_t i = 0; i < rhs.size(); ++i) {
      char left = value_[i];
      char right = rhs[i];
      if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
      if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
      if (left != right) return false;
    }
    return true;
  }

private:
  std::string value_;
};
