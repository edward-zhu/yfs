#include "utils.h"

#include <iostream>
#include <sstream>

void split(const std::string & str, std::vector<std::string> & vec, char delimiter) {
  size_t start = 0, pos = 0;
  while ((pos = str.find(delimiter, pos)) != std::string::npos) {
    if (pos++ - start > 0) {
      vec.emplace_back(str, start, pos - start);
    }
    start = pos;
  }
  if (!str.empty() && (start - str.length()) > 0) {
    vec.emplace_back(str, start);
  }
}

std::string join(const std::vector<std::string> & vec, char delimiter) {
  if (vec.empty()) return "";

  std::stringstream ss;
  for (auto it = vec.begin(); it != vec.end() - 1; ++it) {
    ss << (*it);
    ss << delimiter;
  }
  ss << vec.back();
  return ss.str();
}
