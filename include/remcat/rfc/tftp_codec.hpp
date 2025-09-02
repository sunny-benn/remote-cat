#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace remcat::rfc {

/* TFTP opcodes (RFC 1350) */
enum class Opcode : uint16_t {
    RRQ   = 1,
    WRQ   = 2,
    DATA  = 3,
    ACK   = 4,
    ERROR = 5,
    OACK  = 6
};

/* Key-value option (option name, value) */
using Option = std::pair<std::string, std::string>;
using OptionMap = std::vector<Option>;

/* Encode RRQ (read request) with options */
std::vector<std::byte> encode_rrq(const std::string& filename,
                                  const std::string& mode,
                                  const OptionMap& options);

/* Decode OACK (option acknowledgment) into OptionMap */
OptionMap decode_oack(std::span<const std::byte> buffer);

}
