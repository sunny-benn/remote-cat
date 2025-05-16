#include "remcat/source.hpp"
#include "remcat/url.hpp"
#include "remcat/rfc/tftp_codec.hpp"

#include <vector>
#include <string>
#include <optional>
#include <cstring>
#include <stdexcept>
#include <cerrno>
#include <span>
#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <utility>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

namespace remcat {
namespace {

static constexpr uint16_t kTftpPort = 69;

static uint16_t be16(const unsigned char* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

class TftpSource final : public ISource {
public:
  TftpSource(std::string host, uint16_t port, std::string path,
             const FetchOptions& opts)
      : host_(std::move(host)), port_(port), path_(std::move(path)), quiet_(opts.quiet) {
    open_socket();
    start_rrq(true);
  }

  ~TftpSource() override {
    if (sock_ >= 0) ::close(sock_);
  }

  std::size_t read(std::span<std::byte> out) override {
    if (out.empty() || eof_) return 0;

    /* Serve leftover buffer first */
    if (read_pos_ < buffer_.size()) {
      size_t n = std::min(out.size(), buffer_.size() - read_pos_);
      std::memcpy(out.data(), buffer_.data() + read_pos_, n);
      read_pos_ += n;
      return n;
    }

    /* If last block already fully consumed, then done */
    if (last_block_consumed_) {
      eof_ = true;
      return 0;
    }

    /* Fetch next block into buffer */
    if (!recv_next_block()) {
      /* Timeout or error -> treat as EOF for streaming semantics */
      eof_ = true;
      return 0;
    }

    /* Copy out */
    size_t n = std::min(out.size(), buffer_.size());
    std::memcpy(out.data(), buffer_.data(), n);
    read_pos_ = n;
    return n;
  }

  std::optional<std::uint64_t> size_hint() const override { return tsize_hint_; }

private:
  void open_socket() {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    char portstr[16];
    std::snprintf(portstr, sizeof(portstr), "%u", static_cast<unsigned>(port_));
    struct addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host_.c_str(), portstr, &hints, &res);
    if (rc != 0) {
      throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));
    }

    /* Create socket with first result */
    for (auto* p = res; p; p = p->ai_next) {
      sock_ = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (sock_ < 0) continue;
      std::memcpy(&server_addr_, p->ai_addr, p->ai_addrlen);
      server_addr_len_ = p->ai_addrlen;
      // Save the initial well-known address (host:port, usually 69) for RRQ
      std::memcpy(&server_addr_init_, p->ai_addr, p->ai_addrlen);
      server_addr_init_len_ = p->ai_addrlen;
      break;
    }
    freeaddrinfo(res);

    if (sock_ < 0) {
      throw std::runtime_error("socket: failed to create UDP socket");
    }

  }

  void start_rrq(bool with_options) {
    /* Build RRQ packet; when with_options, request blksize and tsize */
    rfc::OptionMap opts;
    if (with_options) {
      opts.emplace_back("blksize", "512");
      opts.emplace_back("tsize", "0");
    }
    auto pkt = rfc::encode_rrq(path_, "octet", opts);

    if (!quiet_) {
      std::fprintf(stderr, "TFTP: sending RRQ for '%s' %s\n", path_.c_str(), with_options ? "(with options)" : "(plain)");
    }

    /* Reset to initial well-known port and clear TID before (re)starting */
    server_addr_ = server_addr_init_;
    server_addr_len_ = server_addr_init_len_;
    have_tid_ = false;

    /* Send to initial well-known port */
    send_to_server(pkt);

    awaiting_first_ = true;
    next_block_ = 1;
    blksize_ = 512;
    rrq_with_options_ = with_options;
  }

  bool recv_next_block() {
    /* Receive loop with simple retransmit on timeout */
    const int timeout_sec = 5;
    int retries = 5;

    while (retries-- > 0) {
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(sock_, &rfds);
      struct timeval tv;
      tv.tv_sec = timeout_sec;
      tv.tv_usec = 0;
      int sel = ::select(sock_ + 1, &rfds, nullptr, nullptr, &tv);
      if (sel < 0) {
        if (errno == EINTR) continue;
        return false;
      } else if (sel == 0) {
        /* timeout -> retransmit last control (RRQ or ACK) */
        if (awaiting_first_) {
          retransmit_rrq();
        } else {
          send_ack((next_block_ == 0) ? 0 : static_cast<uint16_t>(next_block_ - 1));
        }
        continue;
      }

      /* Ready to recvfrom */
      std::vector<unsigned char> rx;
      rx.resize(static_cast<size_t>(4 + blksize_ + 4));
      struct sockaddr_storage from{};
      socklen_t fromlen = sizeof(from);
      ssize_t n = ::recvfrom(sock_, rx.data(), rx.size(), 0, (struct sockaddr*)&from, &fromlen);
      if (n < 4) {
        // too short
        continue;
      }

      /* On first packet, lock TID (server's chosen port) */
      if (!have_tid_) {
        server_addr_ = from;
        server_addr_len_ = fromlen;
        have_tid_ = true;
      } 
      
      /* 
       * If packet from unexpected port, ignore (RFC: should send ERROR 5 Unknown TID)
       * Will ignore silently for simplicity. 
       */
      else {}

      uint16_t op = be16(rx.data());
      if (op == static_cast<uint16_t>(rfc::Opcode::OACK)) {
        // Parse OACK and ACK block 0
        auto opts = rfc::decode_oack(std::span<const std::byte>(reinterpret_cast<const std::byte*>(rx.data()), static_cast<size_t>(n)));
        for (auto& kv : opts) {
          if (kv.first == "blksize") {
            unsigned long v = std::strtoul(kv.second.c_str(), nullptr, 10);
            if (v >= 8 && v <= 65464) blksize_ = static_cast<uint16_t>(v);
          } else if (kv.first == "tsize") {
            unsigned long long v = std::strtoull(kv.second.c_str(), nullptr, 10);
            tsize_hint_ = v;
          }
        }
        send_ack(0);
        awaiting_first_ = false;
        next_block_ = 1;
        continue; // wait for block 1
      }

      if (op == static_cast<uint16_t>(rfc::Opcode::ERROR)) {
        // Error from server
        if (awaiting_first_ && !fallback_tried_) {
          if (!quiet_) std::fprintf(stderr, "TFTP: ERROR on option RRQ, falling back to plain RRQ\n");
          start_rrq(false);
          fallback_tried_ = true;
          continue;
        }
        eof_ = true;
        return false;
      }

      if (op != static_cast<uint16_t>(rfc::Opcode::DATA)) {
        continue; // unexpected
      }

      uint16_t block = be16(rx.data() + 2);

      // Duplicate block -> re-ACK and continue
      if (!awaiting_first_ && block == (next_block_ == 0 ? 65535 : static_cast<uint16_t>(next_block_ - 1))) {
        send_ack(block);
        continue;
      }

      if (block != next_block_) {
        // Out of order -> ignore
        continue;
      }

      size_t data_len = static_cast<size_t>(n) - 4;
      buffer_.resize(data_len);
      if (data_len)
        std::memcpy(buffer_.data(), rx.data() + 4, data_len);
      read_pos_ = 0;

      send_ack(block);
      awaiting_first_ = false;

      // advance block (wrap at 65535 -> 0)
      next_block_ = static_cast<uint16_t>(next_block_ + 1);
      if (next_block_ == 0) next_block_ = 0; // 65536 wraps to 0; next expected after 65535 is 0

      if (data_len < blksize_) {
        // Last block; after consumer empties buffer, signal EOF
        last_block_consumed_ = true;
      }
      return true;
    }

    // Give up
    if (awaiting_first_ && !fallback_tried_) {
      if (!quiet_) std::fprintf(stderr, "TFTP: no response to option RRQ; falling back to plain RRQ\n");
      start_rrq(false);
      fallback_tried_ = true;
      return recv_next_block();
    }
    return false;
  }

  void retransmit_rrq() {
    rfc::OptionMap opts;
    if (rrq_with_options_) {
      opts.emplace_back("blksize", std::to_string(blksize_));
      opts.emplace_back("tsize", "0");
    }
    auto pkt = rfc::encode_rrq(path_, "octet", opts);
    send_to_server(pkt);
  }

  void send_ack(uint16_t block) {
    unsigned char ack[4];
    uint16_t op = static_cast<uint16_t>(rfc::Opcode::ACK);
    ack[0] = static_cast<unsigned char>((op >> 8) & 0xFF);
    ack[1] = static_cast<unsigned char>(op & 0xFF);
    ack[2] = static_cast<unsigned char>((block >> 8) & 0xFF);
    ack[3] = static_cast<unsigned char>(block & 0xFF);
    (void)::sendto(sock_, ack, 4, 0, (struct sockaddr*)&server_addr_, server_addr_len_);
  }

  void send_to_server(const std::vector<std::byte>& pkt) {
    (void)::sendto(sock_, pkt.data(), pkt.size(), 0, (struct sockaddr*)&server_addr_, server_addr_len_);
  }

private:
  std::string host_;
  uint16_t port_ = kTftpPort;
  std::string path_;

  int sock_ = -1;
  struct sockaddr_storage server_addr_{};
  socklen_t server_addr_len_ = 0;
  struct sockaddr_storage server_addr_init_{};
  socklen_t server_addr_init_len_ = 0;
  bool have_tid_ = false;

  bool awaiting_first_ = true;
  uint16_t next_block_ = 1;
  uint16_t blksize_ = 512;

  std::vector<std::byte> buffer_{};
  size_t read_pos_ = 0;
  bool last_block_consumed_ = false;
  bool eof_ = false;
  std::optional<std::uint64_t> tsize_hint_{};

  bool quiet_ = false;
  bool rrq_with_options_ = true;
  bool fallback_tried_ = false;
};

} // namespace

// Factory for TFTP; defined here for convenience if we want to call from open_url later
std::unique_ptr<ISource> make_tftp_source(const ParsedUrl& u, const FetchOptions& options) {
  uint16_t port = u.port.has_value() ? *u.port : kTftpPort;
  return std::make_unique<TftpSource>(u.host, port, u.path, options);
}

} // namespace remcat
