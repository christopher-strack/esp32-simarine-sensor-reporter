#include "UdpBroadcastServer.hpp"

#include "esp_log.h"
#include "esp_netif.h"
#include <optional>
#include <sys/select.h>

#include "lwip/sockets.h"

namespace {
const auto kTag = "udp_server";

std::optional<int> createBroadcastSocket(const size_t port) {
  int sock = -1;

  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(kTag, "Failed to create socket. Error %d", errno);
    return std::nullopt;
  }

  sockaddr_in saddr;
  saddr.sin_len = 0;
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  const auto err =
      bind(sock, reinterpret_cast<sockaddr*>(&saddr), sizeof(sockaddr_in));

  if (err == -1) {
    ESP_LOGE(kTag, "Failed to bind socket. Error %d", errno);
    close(sock);
    return std::nullopt;
  }

  return sock;
}
} // namespace

UdpBroadcastServer::~UdpBroadcastServer() {
  ESP_LOGE(kTag, "Shutting down socket...");
  if (mSocket) {
    shutdown(*mSocket, 0);
    close(*mSocket);
  }
}

bool UdpBroadcastServer::bind(const size_t port) {
  mSocket = createBroadcastSocket(port);
  return mSocket.has_value();
}

std::optional<std::span<uint8_t>>
UdpBroadcastServer::receive(std::span<uint8_t> buffer) {
  if (!mSocket) {
    return std::nullopt;
  }

  const auto socket = *mSocket;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(socket, &rfds);

  const auto s = select(socket + 1, &rfds, nullptr, nullptr, nullptr);
  if (s < 0) {
    ESP_LOGE(kTag, "Select failed: errno %d", errno);
    return std::nullopt;
  } else if (s > 0) {
    if (FD_ISSET(socket, &rfds)) {
      const int bytesReceived =
          recvfrom(socket, buffer.data(), buffer.size(), 0, nullptr, nullptr);

      if (bytesReceived < 0) {
        ESP_LOGE(kTag, "broadcast recvfrom failed: errno %d", errno);
        return std::nullopt;
      }

      return std::span{buffer.begin(), buffer.begin() + bytesReceived};
    }
  }

  return std::nullopt;
}
