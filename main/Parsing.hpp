#include <algorithm>
#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

namespace spymarine {

constexpr auto headerLength = 14;

/* Describes the header of a message
 */
struct Header {
  uint8_t type;
  uint16_t length;
};

bool operator==(const Header &lhs, const Header &rhs);
bool operator!=(const Header &lhs, const Header &rhs);

/* The message type described by the header.
 * enum only represents the known subset.
 */
enum class MessageType {
  // Request the number of connected devices
  deviceCount = 0x02,

  // Request information about a device
  deviceInfo = 0x41,

  // Sensor update message (UDP)
  sensorState = 0xb0,
};

/* A message as received by a Simarine device
 */
struct Message {
  MessageType type;
  std::span<const uint8_t> data;
};

bool operator==(const Message &lhs, const Message &rhs);
bool operator!=(const Message &lhs, const Message &rhs);

/* Parses the given bytes. Returns a Header on success.
    Raises ParsingError if the given bytes are not a valid header.
    Note: The header is not fully understood and might not work on
    all Simarine devices.
 */
std::optional<Header> parseHeader(const std::span<const uint8_t> data);

/* Calculate a CRC as accepted by Simarine devices.

  Original source: https://github.com/htool/pico2signalk
  Copyright Erik Bosman / @brainsmoke
*/
uint16_t crc(const std::span<const uint8_t> bytes);

/* Parse a response from a Simarine device. Returns a Message on success.
 * Raises ParsingError if the given data is not a valid or known Simarine
 * Message.
 */
std::optional<Message>
parseResponse(const std::span<const uint8_t> rawResponse);

struct Number {
  std::span<const uint8_t, 4> bytes;

  double toCharge() { return secondWord() / 16000.0f; }
  double toCurrent() { return firstWord() / 100.0f; }
  double toVoltage() { return firstWord() / 1000.0f; }

  int16_t firstWord() const { return (bytes[2] << 8) | bytes[3]; }
  int16_t secondWord() const { return (bytes[0] << 8) | bytes[1]; }
};

namespace detail {
template <typename NumberFunction, typename StringFunction>
std::span<const uint8_t> parseValue(const std::span<const uint8_t> bytes,
                                    NumberFunction numberFunction,
                                    StringFunction stringFunction) {
  if (bytes.size() < 2) {
    return {};
  }

  const auto id = bytes[0];
  const auto type = bytes[1];
  if (type == 1) {
    // TODO check for range
    numberFunction(id, Number{bytes.subspan<2, 4>()});
    return bytes.subspan(7);
  } else if (type == 3) {
    // TODO check for range
    numberFunction(id, Number{bytes.subspan<7, 4>()});
    return bytes.subspan(12);
  } else if (type == 4) {
    const auto data = bytes.subspan(7);
    const auto it = std::find(data.begin(), data.end(), uint8_t{0});
    if (it != data.end()) {
      const auto pos = std::distance(data.begin(), it);

      // It looks like a custom encoding is used for strings. Special
      // characters will unfortunately not works as expected.
      const auto str =
          std::string_view{reinterpret_cast<const char *>(data.data()),
                           static_cast<std::string_view::size_type>(pos)};
      stringFunction(id, str);

      return bytes.subspan(7 + pos + 2);
    }
  }

  return {};
}
} // namespace detail

template <typename NumberFunction, typename StringFunction>
void parseValues(std::span<const uint8_t> bytes, NumberFunction numberFunction,
                 StringFunction stringFunction) {
  while (!bytes.empty()) {
    bytes = detail::parseValue(bytes, numberFunction, stringFunction);
  }
}

} // namespace spymarine
