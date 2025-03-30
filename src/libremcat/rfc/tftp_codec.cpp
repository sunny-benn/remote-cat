#include "remcat/rfc/tftp_codec.hpp"

namespace remcat::rfc {

std::vector<std::byte> encode_rrq(const std::string& filename,
                                  const std::string& mode,
                                  const OptionMap& options) {
    /* Compute total size: opcode (2) + filename + null + mode + null + options */
    size_t total = 2 + filename.size() + 1 + mode.size() + 1;
    for (auto const& [key, value] : options) {
        total += key.size() + 1 + value.size() + 1;
    }
    std::vector<std::byte> buffer;
    buffer.reserve(total);
    uint16_t op = static_cast<uint16_t>(Opcode::RRQ);
    buffer.push_back(static_cast<std::byte>((op >> 8) & 0xFF));
    buffer.push_back(static_cast<std::byte>(op & 0xFF));
    for (char c : filename) {
        buffer.push_back(static_cast<std::byte>(c));
    }
    buffer.push_back(std::byte{0});
    for (char c : mode) {
        buffer.push_back(static_cast<std::byte>(c));
    }
    buffer.push_back(std::byte{0});
    /* options */
    for (auto const& [key, value] : options) {
        for (char c : key) {
            buffer.push_back(static_cast<std::byte>(c));
        }
        buffer.push_back(std::byte{0});
        for (char c : value) {
            buffer.push_back(static_cast<std::byte>(c));
        }
        buffer.push_back(std::byte{0});
    }
    return buffer;
}

OptionMap decode_oack(std::span<const std::byte> buffer) {
    OptionMap options;
    if (buffer.size() < 2) {
        return options;
    }
    /* skip opcode */
    size_t pos = 2;
    while (pos < buffer.size()) {
        // parse key
        size_t start = pos;
        while (pos < buffer.size() && buffer[pos] != std::byte{0}) {
            pos++;
        }
        if (pos >= buffer.size()) break;
        std::string key(
            reinterpret_cast<const char*>(buffer.data() + start),
            pos - start
        );
        // skip null
        pos++; 

        // parse value
        start = pos;
        while (pos < buffer.size() && buffer[pos] != std::byte{0}) {
            pos++;
        }
        if (pos >= buffer.size()) break;
        std::string value(
            reinterpret_cast<const char*>(buffer.data() + start),
            pos - start
        );
        pos++; // skip null

        options.emplace_back(std::move(key), std::move(value));
    }
    return options;
}

}
