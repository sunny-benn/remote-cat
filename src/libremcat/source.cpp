#include "remcat/source.hpp"
#include "remcat/url.hpp"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace remcat {

// Forward decl for TFTP backend factory (defined in backends/tftp_source.cpp)
std::unique_ptr<ISource> make_tftp_source(const ParsedUrl& u, const FetchOptions& options);

namespace {
class FileSource final : public ISource {
public:
  explicit FileSource(std::string path)
      : path_(std::move(path)), ifs_(path_, std::ios::binary) {
    if (!ifs_) {
      throw std::runtime_error("Failed to open file: " + path_);
    }
    std::error_code ec;
    auto sz = std::filesystem::file_size(path_, ec);
    if (!ec) size_hint_ = sz;
  }

  std::size_t read(std::span<std::byte> out) override {
    if (!ifs_) return 0;
    if (out.empty()) return 0;
    ifs_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<std::size_t>(ifs_.gcount());
  }

  std::optional<std::uint64_t> size_hint() const override { return size_hint_; }

private:
  std::string path_;
  std::ifstream ifs_;
  std::optional<std::uint64_t> size_hint_;
};
}

std::unique_ptr<ISource> open_url(std::string_view url, const FetchOptions& options) {
  ParsedUrl u = parse_url(url);
  try {
    if (u.scheme.empty() || u.scheme == "file") {
      return std::make_unique<FileSource>(u.path);
    } else if (u.scheme == "tftp") {
      return make_tftp_source(u, options);
    }
  } catch (...) {
    return nullptr;
  }
  return nullptr;
}

}
