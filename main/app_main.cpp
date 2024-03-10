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

int16_t firstWord(uint32_t value) {
  return static_cast<int16_t>(value & 0xFFFF);
}

int16_t secondWord(uint32_t value) {
  return static_cast<int16_t>((value >> 16) & 0xFFFF);
}

double toCharge(uint32_t value) { return secondWord(value) / 16000.0f; }

double toCurrent(uint32_t value) { return firstWord(value) / 100.0f; }

double toVoltage(uint32_t value) { return value / 1000.0f; }

void processSensorValues(esp_mqtt_client_handle_t mqttClient) {
  const auto kDefeaultSimarineUdpPort = 43210;

  UdpBroadcastServer server;

  while (!server.bind(kDefeaultSimarineUdpPort)) {
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }

  std::vector<uint8_t> recvbuf;
  recvbuf.resize(1024);

  std::vector<char> jsonBuffer;
  jsonBuffer.resize(1024);

  spymarine::ValueMap valueMap;
  std::unordered_map<uint8_t, double> cumulativeValues;
  cumulativeValues[26] = 0;
  cumulativeValues[27] = 0;
  cumulativeValues[33] = 0;
  cumulativeValues[35] = 0;
  size_t counter = 0;
  auto start = std::chrono::steady_clock::now();

  while (true) {
    if (const auto buffer = server.receive(recvbuf)) {
      if (const auto message = spymarine::parseResponse(*buffer)) {
        spymarine::parseValueMap(message->data, valueMap);

        cumulativeValues[26] += toCharge(valueMap.numbers[26]);
        cumulativeValues[27] += toCurrent(valueMap.numbers[27]);
        cumulativeValues[33] += toCharge(valueMap.numbers[33]);
        cumulativeValues[35] += toVoltage(valueMap.numbers[35]);
        counter++;

        const auto delta = std::chrono::steady_clock::now() - start;

        if (delta >= std::chrono::minutes{1}) {
          cumulativeValues[26] /= counter;
          cumulativeValues[27] /= counter;
          cumulativeValues[33] /= counter;
          cumulativeValues[35] /= counter;

          ESP_LOGI(TAG, "Bulltron Charge %f", cumulativeValues[26]);
          ESP_LOGI(TAG, "Bulltron Current %f", cumulativeValues[27]);
          ESP_LOGI(TAG, "Starter Charge %f", cumulativeValues[33]);
          ESP_LOGI(TAG, "Starter Voltage %f", cumulativeValues[35]);

          const auto length = std::snprintf(
              jsonBuffer.data(), jsonBuffer.size(),
              R"([{"sensor_id":26,"value":%f},{"sensor_id":27,"value":%f},{"sensor_id":33,"value":%f},{"sensor_id":35,"value":%f}])",
              cumulativeValues[26], cumulativeValues[27], cumulativeValues[33],
              cumulativeValues[35]);
          const auto msgId = esp_mqtt_client_publish(
              mqttClient, "/sensors/all", jsonBuffer.data(), length, 0, 0);
          ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msgId);

          cumulativeValues[26] = 0;
          cumulativeValues[27] = 0;
          cumulativeValues[33] = 0;
          cumulativeValues[35] = 0;
          counter = 0;
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
