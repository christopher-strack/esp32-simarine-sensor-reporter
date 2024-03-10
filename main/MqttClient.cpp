#include "MqttClient.hpp"

#include "esp_log.h"

#include <cstdint>

namespace {

static const char* TAG = "mqtt";

void logError(const char* message, int errorCode) {
  if (errorCode != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, errorCode);
  }
}

void eventHandler(void* handlerArgs, esp_event_base_t base, int32_t eventId,
                  void* eventData) {
  const auto event = reinterpret_cast<esp_mqtt_event_handle_t>(eventData);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT client connected");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT client disconnected");
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT client error");
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

MqttClient::MqttClient(const char* brokerUri, const char* rootCaCertificate,
                       const char* deviceCertificate,
                       const char* devicePrivateKey)
    : mClient{nullptr} {
  esp_mqtt_client_config_t config{};

  config.broker.address.uri = brokerUri;
  config.broker.verification.certificate = rootCaCertificate;
  config.credentials.authentication.certificate = deviceCertificate;
  config.credentials.authentication.key = devicePrivateKey;

  mClient = esp_mqtt_client_init(&config);

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(mClient, MQTT_EVENT_ANY,
                                                 eventHandler, nullptr));

  ESP_ERROR_CHECK(esp_mqtt_client_start(mClient));
}

MqttClient::~MqttClient() {
  ESP_ERROR_CHECK(
      esp_mqtt_client_unregister_event(mClient, MQTT_EVENT_ANY, eventHandler));
  ESP_ERROR_CHECK(esp_mqtt_client_stop(mClient));
}

void MqttClient::publish(const char* topic, std::string_view data) {
  if (esp_mqtt_client_publish(mClient, topic, data.data(), data.size(), 0, 0) <
      0) {
    ESP_LOGE(TAG, "Couldn't publish message");
  }
}
