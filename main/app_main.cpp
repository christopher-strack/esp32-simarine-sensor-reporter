/* MQTT Mutual Authentication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_log.h"

#include "JsonWriter.hpp"
#include "MqttClient.hpp"
#include "Parsing.hpp"
#include "UdpBroadcastServer.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

static const char *TAG = "mqtts_example";

namespace {

std::string_view
writeSensorValuesJson(const std::span<char> buffer,
                      const std::unordered_map<uint8_t, double> &values) {
  JsonWriter writer{buffer};

  writer.writeChar('[');
  size_t i = 0;
  for (const auto &value : values) {
    writer.writeString(R"({"sensor_id":%d,"value":%g})", value.first,
                       value.second);

    if (i < values.size() - 1) {
      writer.writeChar(',');
    }

    i++;
  }
  writer.writeChar(']');
  writer.writeChar('\0');

  return writer.toString();
}

enum class SensorType {
  charge,
  current,
  voltage,
};

double sensorValue(SensorType type, spymarine::Number number) {
  switch (type) {
  case SensorType::charge:
    return number.toCharge();
  case SensorType::current:
    return number.toCurrent();
  case SensorType::voltage:
    return number.toVoltage();
  }
  return 0;
}

class MovingAverageSensorReporter {
public:
  explicit MovingAverageSensorReporter(
      std::unordered_map<uint8_t, SensorType> definitions)
      : mDefinitions(std::move(definitions)) {}

  void updateValue(uint8_t id, const spymarine::Number number) {
    const auto it = mDefinitions.find(id);
    if (it != mDefinitions.end()) {
      const auto [counter, value] = mCumulativeValues[id];
      mCumulativeValues[id] = {counter + 1,
                               value + sensorValue(it->second, number)};
    }
  }

  const std::unordered_map<uint8_t, double> &createMovingAverage() {
    for (auto &entry : mCumulativeValues) {
      const auto [counter, value] = entry.second;
      mAverageValues[entry.first] = value / counter;
      entry.second = {0, 0.0};
    }
    return mAverageValues;
  }

private:
  std::unordered_map<uint8_t, SensorType> mDefinitions;
  std::unordered_map<uint8_t, std::pair<size_t, double>> mCumulativeValues;
  std::unordered_map<uint8_t, double> mAverageValues;
};

template <typename SensorFunction>
void readSensorValues(
    std::unordered_map<uint8_t, SensorType> sensorDefinitions, size_t udpPort,
    SensorFunction function,
    std::chrono::steady_clock::duration movingAverageInterval) {
  UdpBroadcastServer server;

  while (!server.bind(udpPort)) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  std::vector<uint8_t> recvbuf;
  recvbuf.resize(1024);

  MovingAverageSensorReporter sensorReporter{std::move(sensorDefinitions)};

  auto start = std::chrono::steady_clock::now();
  while (true) {
    if (const auto buffer = server.receive(recvbuf)) {
      if (const auto message = spymarine::parseResponse(*buffer);
          message && message->type == spymarine::MessageType::sensorState) {
        spymarine::parseValues(
            message->data,
            [&](const uint8_t id, const spymarine::Number number) {
              sensorReporter.updateValue(id, number);
            },
            [](const uint8_t, const std::string_view) {});

        const auto delta = std::chrono::steady_clock::now() - start;
        if (delta >= movingAverageInterval) {
          function(sensorReporter.createMovingAverage());
          start = std::chrono::steady_clock::now();
        }
      }
    }
  }
}

} // namespace

static void start(void) {
  const auto kDefaultSimarineUdpPort = 43210;

  MqttClient client;

  std::vector<char> jsonBuffer;
  jsonBuffer.resize(1024);

  readSensorValues(
      {
          // Bulltron
          {26, SensorType::charge},
          {27, SensorType::current},
          // Starter
          {33, SensorType::charge},
          {35, SensorType::voltage},
      },
      kDefaultSimarineUdpPort,
      [&](const std::unordered_map<uint8_t, double> &sensorValues) {
        const auto json = writeSensorValuesJson(jsonBuffer, sensorValues);
        client.publish("/sensors/all", json.data());
      },
      std::chrono::seconds{5});
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes",
           esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
  esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
  esp_log_level_set("transport", ESP_LOG_VERBOSE);
  esp_log_level_set("outbox", ESP_LOG_VERBOSE);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  ESP_ERROR_CHECK(example_connect());

  start();
}
