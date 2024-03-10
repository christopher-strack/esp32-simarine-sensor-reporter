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
#include "mqtt_client.h"

#include "Parsing.hpp"
#include "UdpBroadcastServer.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

static const char *TAG = "mqtts_example";

extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");
extern const uint8_t
    server_cert_pem_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_mosquitto_org_crt_end");

static void log_error_if_nonzero(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32,
           base, event_id);
  esp_mqtt_event_handle_t event =
      reinterpret_cast<esp_mqtt_event_handle_t>(event_data);
  switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      log_error_if_nonzero("reported from esp-tls",
                           event->error_handle->esp_tls_last_esp_err);
      log_error_if_nonzero("reported from tls stack",
                           event->error_handle->esp_tls_stack_err);
      log_error_if_nonzero("captured as transport's socket errno",
                           event->error_handle->esp_transport_sock_errno);
      ESP_LOGI(TAG, "Last errno string (%s)",
               strerror(event->error_handle->esp_transport_sock_errno));
    }
    break;
  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}

namespace {

std::span<char>
writeSensorValuesJson(const std::span<char> buffer,
                      const std::unordered_map<uint8_t, double> &values) {
  size_t totalLength = 0;
  auto writeBuffer = buffer;

  const auto writeChar = [&](char c) {
    if (writeBuffer.size() > 0) {
      writeBuffer[0] = c;
      writeBuffer = writeBuffer.subspan(1);
      totalLength++;
    }
  };

  writeChar('[');
  size_t i = 0;
  for (const auto &value : values) {
    const auto length = std::snprintf(writeBuffer.data(), writeBuffer.size(),
                                      R"({"sensor_id":%d,"value":%g})",
                                      value.first, value.second);
    writeBuffer = writeBuffer.subspan(length);
    totalLength += length;

    if (i < values.size() - 1) {
      writeChar(',');
    }

    i++;
  }
  writeChar(']');
  writeChar('\0');

  return buffer.subspan(0, totalLength);
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

void processSensorValues(esp_mqtt_client_handle_t mqttClient) {
  const auto kDefaultSimarineUdpPort = 43210;

  UdpBroadcastServer server;

  while (!server.bind(kDefaultSimarineUdpPort)) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }

  std::vector<uint8_t> recvbuf;
  recvbuf.resize(1024);

  std::vector<char> jsonBuffer;
  jsonBuffer.resize(1024);

  MovingAverageSensorReporter sensorReporter{{
      // Bulltron
      {26, SensorType::charge},
      {27, SensorType::current},
      // Starter
      {33, SensorType::charge},
      {35, SensorType::voltage},
  }};

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

        if (delta >= std::chrono::seconds{5}) {
          const auto json = writeSensorValuesJson(
              jsonBuffer, sensorReporter.createMovingAverage());

          esp_mqtt_client_publish(mqttClient, "/sensors_test/all", json.data(),
                                  json.size(), 0, 0);
          ESP_LOGI(TAG, "sent publish successful, json=%s", json.data());

          start = std::chrono::steady_clock::now();
        }
      }
    }
  }
}

} // namespace

static void mqtt_app_start(void) {
  esp_mqtt_client_config_t mqtt_cfg;
  std::memset(&mqtt_cfg, 0, sizeof(esp_mqtt_client_config_t));

  mqtt_cfg.broker.address.uri =
      "mqtts://a2cn68t41migo6-ats.iot.eu-central-1.amazonaws.com:8883";

  mqtt_cfg.broker.verification.certificate =
      (const char *)server_cert_pem_start;
  mqtt_cfg.credentials.authentication.certificate =
      (const char *)client_cert_pem_start;
  mqtt_cfg.credentials.authentication.key = (const char *)client_key_pem_start;

  ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes",
           esp_get_free_heap_size());
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

  esp_mqtt_client_register_event(
      client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
      mqtt_event_handler, NULL);

  esp_mqtt_client_start(client);

  processSensorValues(client);
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

  mqtt_app_start();
}
