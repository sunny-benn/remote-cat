#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "remcat/options.hpp"

namespace remcat {

class ISource {
public:
  virtual ~ISource() = default;
  virtual std::size_t read(std::span<std::byte> out) = 0;
  virtual std::optional<std::uint64_t> size_hint() const { return std::nullopt; }
};

std::unique_ptr<ISource> open_url(std::string_view url, const FetchOptions& options);

}
