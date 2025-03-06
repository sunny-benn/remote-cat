#pragma once
#include <string>
#include <string_view>

namespace remcat {

struct ParsedUrl {
  std::string scheme;
  std::string path;
};

/* Very small URL parser for PR1: supports file://... and bare paths. */
inline ParsedUrl parse_url(std::string_view url) {
  ParsedUrl out;
  auto pos = url.find("://");
  if (pos != std::string_view::npos) {
    out.scheme = std::string(url.substr(0, pos));
    auto rest = url.substr(pos + 3);
    /* For file://, the rest is the path. Allow leading '/'. */
    out.path = std::string(rest);
  } else {
    /* No scheme, treat entire string as path */
    out.scheme.clear();
    out.path = std::string(url);
  }
  return out;
}

}
