#include "Config.hpp"
#include "JsonWriter.hpp"
#include "MqttClient.hpp"
#include "UdpBroadcastServer.hpp"
#include "WifiConnector.hpp"
#include "spymarine/Parsing.hpp"
#include "spymarine/Sensor.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>

static const char* TAG = "sensor_reporter";

namespace {

using SensorValuesMap = std::unordered_map<spymarine::SensorId, double>;

std::string_view writeSensorValuesJson(JsonBuffer& buffer,
                                       const SensorValuesMap& values) {
  JsonWriter writer{buffer};

  writer.startArray();

  for (const auto& value : values) {
    writer.startObject();
    writer.addObjectKey("sensor_id");
    writer.addInt(value.first);
    writer.addObjectKey("value");
    writer.addDouble(value.second);
    writer.endObject();
  }

  writer.endArray();

  return writer.string();
};

class MovingAverageCalculator {
public:
  void updateValue(spymarine::SensorId id, const double newValue) {
    const auto [counter, value] = mCumulativeValues[id];
    mCumulativeValues[id] = {counter + 1, value + newValue};
  }

  const SensorValuesMap& calculateMovingAverage() {
    for (auto& entry : mCumulativeValues) {
      const auto [counter, value] = entry.second;
      mAverageValues[entry.first] = value / counter;
      entry.second = {0, 0.0};
    }
    return mAverageValues;
  }

private:
  std::unordered_map<spymarine::SensorId, std::pair<size_t, double>>
      mCumulativeValues;
  SensorValuesMap mAverageValues;
};

template <typename SensorFunction>
void readSensorValues(size_t udpPort,
                      const spymarine::SensorDefinition& sensorDefinition,
                      std::chrono::steady_clock::duration movingAverageInterval,
                      SensorFunction function) {
  UdpBroadcastServer server;
  if (server.bind(udpPort)) {
    MovingAverageCalculator movingAverageCalculator;

    std::vector<uint8_t> recvbuf;
    recvbuf.resize(1024);

    auto start = std::chrono::steady_clock::now();
    while (true) {
      if (const auto buffer = server.receive(recvbuf)) {
        parseSensorStateMessage(*buffer, sensorDefinition,
                                [&](spymarine::SensorId id, double value) {
                                  movingAverageCalculator.updateValue(id,
                                                                      value);
                                });

        const auto delta = std::chrono::steady_clock::now() - start;
        if (delta >= movingAverageInterval) {
          function(movingAverageCalculator.calculateMovingAverage());
          start = std::chrono::steady_clock::now();
        }
      } else {
        // receive should never fail for our UDP server, let's restart
        // to try and recover from this state.
        esp_restart();
      }
    }
  }
}

} // namespace

static void start(void) {
  WifiConnector wifiConnector{kWifiSsid, kWifiPassword};
  wifiConnector.waitUntilConnected();

  MqttClient client{kMqttBrokerUri, kMqttRootCaCertificate,
                    kMqttDeviceCertificate, kMqttDevicePrivateKey};
  JsonBuffer jsonBuffer;

  readSensorValues(kSimarineUdpPort, kSensorDefinition, kSensorUpdateInterval,
                   [&](const SensorValuesMap& sensorValues) {
                     client.publish(
                         "/sensors/all",
                         writeSensorValuesJson(jsonBuffer, sensorValues));
                   });
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

  start();
}
