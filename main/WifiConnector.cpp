#include "WifiConnector.hpp"

#include "esp_log.h"
#include "esp_wifi.h"

#include <cstddef>
#include <cstring>
#include <thread>

namespace {

const char* TAG = "wifi";

template <size_t N> void setString(uint8_t (&target)[N], std::string_view str) {
  if (N > str.size() + 1) {
    std::copy(str.begin(), str.end(), target);
    target[str.size()] = '\0';
  } else {
    ESP_LOGE(TAG, "couldn't set string");
  }
}

wifi_config_t createWifiConfig(std::string_view ssid,
                               std::string_view password) {
  wifi_config_t config{};

  setString(config.sta.ssid, ssid);
  setString(config.sta.password, password);

  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  setString(config.sta.sae_h2e_identifier, "");

  return config;
}

} // namespace

WifiConnector::WifiConnector(std::string_view ssid, std::string_view password) {
  mpEspNetIf = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  registerEventHandler();

  auto wifiConfig = createWifiConfig(ssid, password);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi started");
}

WifiConnector::~WifiConnector() {
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());

  esp_netif_destroy_default_wifi(mpEspNetIf);
}

void WifiConnector::waitUntilConnected() {
  mInitialConnectionPromise.get_future().get();
  ESP_LOGI(TAG, "connected to wifi");
}

void WifiConnector::onStationStart() { esp_wifi_connect(); }

void WifiConnector::onStationDisconnected() {
  std::this_thread::sleep_for(kRetryInterval);
  ESP_LOGI(TAG, "retry connecting wifi");
  esp_wifi_connect();
}

void WifiConnector::onStationGotIp() {
  if (!mInitialConnectionReported) {
    mInitialConnectionPromise.set_value();
    mInitialConnectionReported = true;
  }
}

void WifiConnector::registerEventHandler() {
  esp_event_handler_instance_t instanceAnyId;
  esp_event_handler_instance_t instanceGotIp;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &evenHandler, this, &instanceAnyId));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &evenHandler, this, &instanceGotIp));
}

void WifiConnector::evenHandler(void* arg, esp_event_base_t eventBase,
                                int32_t eventId, void* eventData) {
  auto pThis = static_cast<WifiConnector*>(arg);

  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
    pThis->onStationStart();
  } else if (eventBase == WIFI_EVENT &&
             eventId == WIFI_EVENT_STA_DISCONNECTED) {
    pThis->onStationDisconnected();
  } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
    pThis->onStationGotIp();
  }
}
