#include "Parsing.hpp"

#include <algorithm>
#include <cstring>
#include <ostream>
#include <string>

namespace spymarine {

bool operator==(const Header &lhs, const Header &rhs) {
  return lhs.type == rhs.type && lhs.length == rhs.length;
}

bool operator!=(const Header &lhs, const Header &rhs) { return !(lhs == rhs); }

bool operator==(const Message &lhs, const Message &rhs) {
  return lhs.type == rhs.type &&
         std::memcmp(lhs.data.data(), rhs.data.data(),
                     std::min(lhs.data.size(), rhs.data.size())) == 0;
}

bool operator!=(const Message &lhs, const Message &rhs) {
  return !(lhs == rhs);
}

namespace {

uint16_t toUInt16(const std::span<const uint8_t, 2> data) {
  return uint16_t((data[0] << 8) | data[1]);
}

} // namespace

std::optional<Header> parseHeader(const std::span<const uint8_t> data) {
  if (data.size() < headerLength) {
    return std::nullopt;
  }

  if (std::memcmp(data.data(), "\x00\x00\x00\x00\x00\xff", 6) != 0) {
    return std::nullopt;
  }

  if (std::memcmp(data.data() + 7, "\x85\xde\xc3\x46", 4) != 0) {
    return std::nullopt;
  }

  if (data[13] != 0xff) {
    return std::nullopt;
  }

  const auto type = data.data()[6];
  const auto length = toUInt16(data.subspan<11, 2>());

  return Header{type, length};
}

uint16_t crc(const std::span<const uint8_t> bytes) {
  const uint16_t poly = 0x1189;
  uint16_t crc = 0;

  for (auto byte : bytes) {
    crc ^= byte << 8;
    for (int i = 0; i < 8; i++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ poly;
      } else {
        crc <<= 1;
      }
    }
  }

  return crc;
}

std::optional<Message>
parseResponse(const std::span<const uint8_t> rawResponse) {
  const auto header = parseHeader(rawResponse);
  const auto dataLength = rawResponse.size() - headerLength + 1;

  if (header->length != dataLength) {
    return std::nullopt;
  }

  const auto calculatedCrc =
      crc(std::span{rawResponse.begin() + 1, rawResponse.end() - 3});
  const auto receivedCrc =
      toUInt16(std::span<const uint8_t, 2>{rawResponse.end() - 2, 2});

  if (calculatedCrc != receivedCrc) {
    return std::nullopt;
  }

  return Message{
      static_cast<MessageType>(header->type),
      std::span{rawResponse.begin() + headerLength, rawResponse.end() - 2}};
}

} // namespace spymarine
