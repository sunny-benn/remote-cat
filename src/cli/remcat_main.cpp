#include "remcat/source.hpp"
#include "remcat/options.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace remcat;

static void print_usage(const char* prog) {
  std::fprintf(stderr, "usage: %s <URL-or-path> [--bytes N | --head N] [-q|--quiet]\n", prog);
}

static void print_help(const char* prog) {
  std::cout
    << "Usage: " << prog << " <URL-or-path> [--bytes N | --head N] [-q|--quiet]\n"
    << "\nOptions:\n"
    << "  --bytes N         Limit total bytes read to N\n"
    << "  --head N          Read only the first N bytes\n"
    << "  -q, --quiet       Suppress non-fatal diagnostics\n"
    << "  -h, --help        Show this help and exit\n"
    << "\nExamples:\n"
    << "  " << prog << " ./README.md\n"
    << "  " << prog << " tftp://127.0.0.1/test.txt\n"
    << "  " << prog << " --head 16 tftp://127.0.0.1/test.txt\n";
}

static std::optional<std::uint64_t> parse_u64(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 2) { print_usage(argv[0]); return 2; }

  FetchOptions opts;
  std::string url;

  /* Minimal flag parsing */
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    if (a == "--bytes" || a == "--head") {
      if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", argv[i]); return 2; }
      auto val = parse_u64(argv[++i]);
      if (!val) { std::fprintf(stderr, "invalid numeric value: %s\n", argv[i]); return 2; }
      opts.bytes_limit = *val;
    } else if (a == "-q" || a == "--quiet") {
      opts.quiet = true;
    } else if (a == "-h" || a == "--help") {
      print_help(argv[0]);
      return 0;
    } else if (a.rfind("-", 0) == 0) {
      std::fprintf(stderr, "unknown option: %s\n", argv[i]);
      return 2;
    } else if (url.empty()) {
      url = std::string(a);
    } else {
      std::fprintf(stderr, "unexpected argument: %s\n", argv[i]);
      return 2;
    }
  }

  if (url.empty()) { print_usage(argv[0]); return 2; }

  auto src = open_url(url, opts);
  if (!src) {
    if (!opts.quiet) std::fprintf(stderr, "failed to open source: %s\n", url.c_str());
    return 1;
  }

  /* Allocate a 64KB buffer */
  constexpr std::size_t kBufSize = 64 * 1024;
  std::vector<std::byte> buf(kBufSize);
  /* Track emitted bytes */
  std::uint64_t emitted = 0;

  for (;;) {
    std::size_t want = buf.size();
    if (opts.bytes_limit) {
      if (emitted >= *opts.bytes_limit) {
        break;
      }
      auto remain = static_cast<std::size_t>(std::min<std::uint64_t>(*opts.bytes_limit - emitted, buf.size()));
      want = remain;
    }

    std::size_t n = src->read(std::span<std::byte>(buf.data(), want));
    if (n == 0) {
        break;
    }

    std::size_t written = std::fwrite(reinterpret_cast<const void*>(buf.data()), 1, n, stdout);
    if (written != n) {
      if (!opts.quiet) std::fprintf(stderr, "short write to stdout\n");
      return 1;
    }
    emitted += n;
  }

  std::fflush(stdout);
  return 0;
}
