#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>

namespace remcat {

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string path;
  std::optional<uint16_t> port;
};

/* Very small URL parser: supports file://... and bare paths. */
inline ParsedUrl parse_url(std::string_view url) {
  ParsedUrl out;
  auto pos = url.find("://");
  if (pos == std::string_view::npos) {
    /* No scheme, treat entire string as a filesystem path */
    out.scheme.clear();
    out.host.clear();
    out.path = std::string(url);
    out.port.reset();
    return out;
  }

  out.scheme = std::string(url.substr(0, pos));
  auto rest = url.substr(pos + 3);

  if (out.scheme == "file") {
    /* For file://, the rest is a filesystem path (may begin with '/') */
    out.host.clear();
    out.path = std::string(rest);
    out.port.reset();
    return out;
  }

  /* For other schemes, parse authority as host[:port] then optional '/' path */
  std::string_view authority = rest;
  std::string_view path_part;
  auto slash_pos = rest.find('/');
  if (slash_pos != std::string_view::npos) {
    authority = rest.substr(0, slash_pos);
    path_part = rest.substr(slash_pos + 1); // drop leading '/'
  } else {
    authority = rest;
    path_part = std::string_view{};
  }

  // Extract host and optional port
  auto colon_pos = authority.rfind(':');
  if (colon_pos != std::string_view::npos) {
    out.host = std::string(authority.substr(0, colon_pos));
    auto port_sv = authority.substr(colon_pos + 1);
    unsigned long p = 0;
    for (char c : std::string(port_sv)) {
      if (c < '0' || c > '9') { p = 0; break; }
      p = p * 10 + static_cast<unsigned long>(c - '0');
      if (p > 65535) { p = 0; break; }
    }
    if (p > 0 && p <= 65535) out.port = static_cast<uint16_t>(p); else out.port.reset();
  } else {
    out.host = std::string(authority);
    out.port.reset();
  }

  out.path = std::string(path_part);
  return out;
}

}
