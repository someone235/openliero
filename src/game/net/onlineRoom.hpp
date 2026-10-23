#pragma once

#include "iceAgent.hpp"
#include "iceBridge.hpp"
#include "signaling.hpp"
#include "transport.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Star topology: every guest has an independent ICE/ENet link to the host.
// The signaling socket stays alive until the room is closed.
class OnlineRoom {
 public:
  explicit OnlineRoom(bool host);
  ~OnlineRoom();
  bool Open(const std::string& server, uint16_t port, const std::string& code = "");
  void Poll();
  void Lock(bool locked);
  void Send(uint32_t peer, const std::vector<uint8_t>& packet);
  std::vector<uint32_t> Peers() const;
  bool Settled() const;
  bool IsHost() const { return host_; }
  const std::string& Code() const { return signaling_.RoomCode(); }
  std::function<void(uint32_t)> on_connected;
  std::function<void(uint32_t)> on_disconnected;
  std::function<void(uint32_t, const std::vector<uint8_t>&)> on_packet;
  std::function<void(const std::string&)> on_error;

 private:
  struct Link {
    std::unique_ptr<IceAgent> ice;
    std::unique_ptr<IceBridge> bridge;
    NetTransport transport;
    std::vector<std::string> candidates;
    std::string remote_ufrag;
    bool connected = false;
    bool failed = false;
    uint64_t created = 0;
    uint64_t last_ice_send = 0;
    std::vector<uint8_t> incoming;
    uint32_t incoming_size = 0;
  };
  void AddPeer(uint32_t peer);
  void ReceiveIce(uint32_t peer, const std::vector<uint8_t>& payload);
  void SendIce(uint32_t peer, Link& link);
  void Receive(uint32_t peer, const std::vector<uint8_t>& packet);
  bool host_;
  bool locked_ = false;
  SignalingClient signaling_;
  std::map<uint32_t, std::unique_ptr<Link>> links_;
  std::string server_;
  uint64_t last_keepalive_ = 0;
};
