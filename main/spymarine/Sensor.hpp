#pragma once

#include "JsonWriter.hpp"
#include "Parsing.hpp"

#include <cstdint>
#include <unordered_map>

namespace spymarine {

/* Sensor type of a Simarine device.
 * Note that this is only a subset that I needed for
 * personal use.
 */
enum class SensorType {
  charge,
  current,
  voltage,
};

// Identifies a sensor in a sensor state message
using SensorId = uint8_t;

// A map from sensor id to sensor type. The sensor state message
// does unfortunately not contain the information so it needs to be
// extracted upfront.
using SensorDefinition = std::unordered_map<SensorId, SensorType>;

/* Convert the given number and type to a value in the expected unit
 */
double sensorValue(SensorType type, Number number);

/* Convenience function that takes a buffer containing a message and a
 * sensor definition and calls function for every sensor with the sensor
 * id and the sensor value.
 */
template <typename SensorValueFunction>
void parseSensorStateMessage(const std::span<uint8_t> bytes,
                             const SensorDefinition& sensorDefinition,
                             SensorValueFunction function);

template <typename SensorValueFunction>
void parseSensorStateMessage(const std::span<uint8_t> bytes,
                             const SensorDefinition& sensorDefinition,
                             SensorValueFunction function) {
  if (const auto message = parseMessage(bytes);
      message && message->type == MessageType::sensorState) {
    parseValues(
        message->data,
        [&](const SensorId id, const Number number) {
          const auto it = sensorDefinition.find(id);
          if (it != sensorDefinition.end()) {
            function(id, sensorValue(it->second, number));
          }
        },
        [](const SensorId, const std::string_view) {});
  }
}

} // namespace spymarine
