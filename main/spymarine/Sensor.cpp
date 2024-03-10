#include "Sensor.hpp"

namespace spymarine {

double sensorValue(SensorType type, Number number) {
  switch (type) {
  case SensorType::charge:
    return number.secondWord() / 16000.0f;
  case SensorType::current:
    return number.firstWord() / 100.0f;
  case SensorType::voltage:
    return number.firstWord() / 1000.0f;
  }
  return 0;
}

} // namespace spymarine
