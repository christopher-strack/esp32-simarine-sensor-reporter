#include "UdpBroadcastServer.hpp"

#include "esp_log.h"
#include "esp_netif.h"
#include <sys/select.h>

#include "lwip/sockets.h"

namespace {
const auto kTag = "UdpBroadcastServer";

int createBroadcastSocket(const size_t port) {
  struct sockaddr_in saddr;
  int sock = -1;
  int err = 0;

  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(kTag, "Failed to create socket. Error %d", errno);
    return -1;
  }

  saddr.sin_len = 0;
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
  if (err < 0) {
    ESP_LOGE(kTag, "Failed to bind socket. Error %d", errno);
    close(sock);
    return -1;
  }

  return sock;
}
} // namespace

UdpBroadcastServer::~UdpBroadcastServer() {
  ESP_LOGE(kTag, "Shutting down socket...");
  shutdown(mSocket, 0);
  close(mSocket);
}

bool UdpBroadcastServer::bind(const size_t port) {
  mSocket = createBroadcastSocket(port);
  return mSocket != -1;
}

std::optional<std::span<uint8_t>>
UdpBroadcastServer::receive(std::span<uint8_t> buffer) {
  if (mSocket == -1) {
    return std::nullopt;
  }

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(mSocket, &rfds);

  const auto s = select(mSocket + 1, &rfds, nullptr, nullptr, nullptr);
  if (s < 0) {
    ESP_LOGE(kTag, "Select failed: errno %d", errno);
    return std::nullopt;
  } else if (s > 0) {
    if (FD_ISSET(mSocket, &rfds)) {
      char raddrName[32] = {0};

      struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
      socklen_t socklen = sizeof(raddr);
      int len = recvfrom(mSocket, buffer.data(), buffer.size(), 0,
                         (struct sockaddr *)&raddr, &socklen);
      if (len < 0) {
        ESP_LOGE(kTag, "broadcast recvfrom failed: errno %d", errno);
        return std::nullopt;
      }

      // Get the sender's address as a string
      if (raddr.ss_family == PF_INET) {
        inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr, raddrName,
                    sizeof(raddrName) - 1);
      }

      // ESP_LOGI(kTag, "received %d bytes from %s:", len, raddrName);
      return std::span{buffer.begin(), buffer.begin() + len};
    }
  }

  return std::nullopt;
}
