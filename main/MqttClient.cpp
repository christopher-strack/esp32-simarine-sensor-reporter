#include "MqttClient.hpp"

#include "esp_log.h"

#include <cstring>

extern const uint8_t clientCertPemStart[] asm("_binary_client_crt_start");
extern const uint8_t clientKeyPemStart[] asm("_binary_client_key_start");
extern const uint8_t
    serverCertPemStart[] asm("_binary_mosquitto_org_crt_start");

namespace {

static const char *TAG = "mqtt";

void logError(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
  }
}

void eventHandler(void *handlerArgs, esp_event_base_t base, int32_t eventId,
                  void *eventData) {
  const auto event = reinterpret_cast<esp_mqtt_event_handle_t>(eventData);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      logError("reported from esp-tls",
               event->error_handle->esp_tls_last_esp_err);
      logError("reported from tls stack",
               event->error_handle->esp_tls_stack_err);
      logError("captured as transport's socket errno",
               event->error_handle->esp_transport_sock_errno);
      ESP_LOGI(TAG, "Last errno string (%s)",
               strerror(event->error_handle->esp_transport_sock_errno));
    }
    break;
  default:
    break;
  }
}

} // namespace

MqttClient::MqttClient() : mClient{nullptr} {
  esp_mqtt_client_config_t config;
  std::memset(&config, 0, sizeof(esp_mqtt_client_config_t));

  config.broker.address.uri =
      "mqtts://a2cn68t41migo6-ats.iot.eu-central-1.amazonaws.com:8883";

  config.broker.verification.certificate =
      reinterpret_cast<const char *>(serverCertPemStart);
  config.credentials.authentication.certificate =
      reinterpret_cast<const char *>(clientCertPemStart);
  config.credentials.authentication.key =
      reinterpret_cast<const char *>(clientKeyPemStart);

  mClient = esp_mqtt_client_init(&config);

  esp_mqtt_client_register_event(
      mClient, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), eventHandler,
      nullptr);

  esp_mqtt_client_start(mClient);
}

MqttClient::~MqttClient() { esp_mqtt_client_stop(mClient); }

void MqttClient::publish(const char *topic, std::string_view data) {
  esp_mqtt_client_publish(mClient, topic, data.data(), data.size(), 0, 0);
}
