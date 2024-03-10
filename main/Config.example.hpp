#pragma once

#include "spymarine/Sensor.hpp"

#include <chrono>

// The sensor definition contains an entry for each sensor that should be
// reported over MQTT. It's a mapping from sensor ID to sensor type and is
// stable unless the hardware setup or configuration changes. The sensor
// information can be read using the spymarine Python library
// https://github.com/christopher-strack/spymarine. Note that the sensor ID is
// referred to as "state_index" there.
const spymarine::SensorDefinition kSensorDefinition{
    {26, spymarine::SensorType::charge},
    {27, spymarine::SensorType::current},
    {33, spymarine::SensorType::charge},
    {35, spymarine::SensorType::voltage},
};

// UDP port used by the Simarine device
constexpr auto kSimarineUdpPort = 43210;

// Interval on how often the sensor values are reported over MQTT
constexpr auto kSensorUpdateInterval = std::chrono::minutes{1};

constexpr auto kWifiSsid = "[INSERT WIFI SSID]";
constexpr auto kWifiPassword = "[INSERT WIFI PASSWORD]";

// Insert MQTT broker URI, for example
// mqtts://iot.eu-central-1.amazonaws.com:8883
constexpr auto kMqttBrokerUri = "[INSERT BROKER URI]";

constexpr auto kMqttDeviceCertificate = R"(
[INSERT DEVICE CERTIFICATE]
)";

constexpr auto kMqttDevicePrivateKey = R"(
[INSERT DEVICE PRIVATE KEY]
)";

constexpr auto kMqttRootCaCertificate = R"(
[INSERT ROOT CA CERTIFICATE]
)";
