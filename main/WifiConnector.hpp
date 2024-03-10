#pragma once

#include "esp_event_base.h"
#include "esp_wifi_default.h"

#include <chrono>
#include <future>
#include <string_view>

/*! Connect to the given Wifi network.
 *
 *  Attempts to reconnect repeatedly in case the Wifi network
 *  disconnects.
 */
class WifiConnector {
public:
  static constexpr std::chrono::seconds kRetryInterval{10};

  WifiConnector(std::string_view ssid, std::string_view password);
  ~WifiConnector();

  void waitUntilConnected();

private:
  void registerEventHandler();

  void onStationStart();
  void onStationDisconnected();
  void onStationGotIp();

  static void evenHandler(void* arg, esp_event_base_t eventBase,
                          int32_t eventId, void* eventData);

  esp_netif_t* mpEspNetIf{nullptr};
  bool mInitialConnectionReported{false};
  std::promise<void> mInitialConnectionPromise;
};
