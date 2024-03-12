#include "mqtt_client.h"

#include <string_view>

class MqttClient {
public:
  MqttClient();
  ~MqttClient();

  void publish(const char *topic, std::string_view data);

private:
  esp_mqtt_client_handle_t mClient;
};
