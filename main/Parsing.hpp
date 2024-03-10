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

struct ValueMap {
  std::unordered_map<uint8_t, std::string_view> strings;
  std::unordered_map<uint8_t, int32_t> numbers;
};

/* Converts bytes received by a Simarine devices to a ValueMap.
   Raises ParsingError in case the given bytes do not contain a valid
   ValueMap.
*/
void parseValueMap(std::span<const uint8_t> bytes, ValueMap &valueMap);

} // namespace spymarine
