#pragma once

#include <optional>
#include <span>

#include <cstdint>

class UdpBroadcastServer {
public:
  UdpBroadcastServer() = default;
  ~UdpBroadcastServer();

  bool bind(size_t port);

  std::optional<std::span<uint8_t>> receive(std::span<uint8_t> buffer);

private:
  int mSocket{-1};
};
