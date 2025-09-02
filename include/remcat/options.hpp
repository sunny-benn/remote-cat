#pragma once
#include <cstdint>
#include <optional>

namespace remcat {

struct FetchOptions {
  /* If set, limit total bytes read from the source. */
  std::optional<std::uint64_t> bytes_limit;
  bool quiet = false;
};

}
