#include "onlineRoom.hpp"

#include "netutil.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
void Append32(std::vector<uint8_t>& bytes, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
  }
}
uint32_t Read32(const uint8_t* bytes) {
  return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) | (uint32_t{bytes[2]} << 16) |
         (uint32_t{bytes[3]} << 24);
}
}  // namespace

OnlineRoom::OnlineRoom(bool host) : host_(host) {
  signaling_.on_room_created = [](const std::string& code) {
    std::fprintf(stderr, "[online] room code=%s\n", code.c_str());
  };
  signaling_.on_room_peer = [this](uint32_t peer) { AddPeer(peer); };
  signaling_.on_room_ice = [this](uint32_t peer, const std::vector<uint8_t>& data) {
    ReceiveIce(peer, data);
  };
  signaling_.on_error = [this](const std::string& error) {
    if (on_error) {
      on_error(error);
    }
  };
  signaling_.on_room_expired = [this]() {
    if (on_error) {
      on_error("ROOM EXPIRED");
    }
  };
}

OnlineRoom::~OnlineRoom() { signaling_.LeaveRoom(); }

bool OnlineRoom::Open(const std::string& server, uint16_t port, const std::string& code) {
  server_ = server;
  return host_ ? signaling_.CreateMultiplayerRoom(server, port)
               : signaling_.JoinMultiplayerRoom(server, port, code);
}

void OnlineRoom::AddPeer(uint32_t peer) {
  if (links_.contains(peer)) {
    return;
  }
  if (host_ && locked_) {
    signaling_.RouteIce(peer, {3, 0});
    return;
  }
  auto link = std::make_unique<Link>();
  auto* ptr = link.get();
  ptr->created = netutil::NowMs();
  ptr->ice = std::make_unique<IceAgent>();
  ptr->bridge = std::make_unique<IceBridge>();
  ptr->ice->on_local_candidate = [ptr](const std::string& candidate) {
    ptr->candidates.push_back(candidate);
  };
  ptr->transport.on_connected = [this, peer, ptr]() {
    ptr->connected = true;
    if (on_connected) {
      on_connected(peer);
    }
  };
  ptr->transport.on_disconnected = [ptr]() { ptr->failed = true; };
  ptr->transport.on_room_packet = [this, peer](const std::vector<uint8_t>& packet) {
    Receive(peer, packet);
  };
  IceAgent::Config config;
  config.turn_server = server_;
  config.turn_user = signaling_.TurnUser();
  config.turn_password = signaling_.TurnPassword();
  links_[peer] = std::move(link);
  ptr->ice->Start(config);
  SendIce(peer, *ptr);
}

void OnlineRoom::SendIce(uint32_t peer, Link& link) {
  if (!link.ice) {
    return;
  }
  auto ufrag = link.ice->LocalUfrag();
  auto pwd = link.ice->LocalPwd();
  std::vector<uint8_t> credentials{1, static_cast<uint8_t>(ufrag.size())};
  credentials.insert(credentials.end(), ufrag.begin(), ufrag.end());
  credentials.insert(credentials.end(), pwd.begin(), pwd.end());
  signaling_.RouteIce(peer, credentials);
  for (const auto& candidate : link.candidates) {
    std::vector<uint8_t> data{2};
    data.insert(data.end(), candidate.begin(), candidate.end());
    signaling_.RouteIce(peer, data);
  }
  link.last_ice_send = netutil::NowMs();
}

void OnlineRoom::ReceiveIce(uint32_t peer, const std::vector<uint8_t>& payload) {
  if (payload.size() < 2) {
    return;
  }
  if (payload[0] == 3 && !host_) {
    if (on_error) {
      on_error("MATCH ALREADY STARTED");
    }
    return;
  }
  if (!links_.contains(peer)) {
    AddPeer(peer);
  }
  auto it = links_.find(peer);
  if (it == links_.end() || !it->second->ice) {
    return;
  }
  auto& link = *it->second;
  if (payload[0] == 1 && payload.size() > size_t{payload[1]} + 2) {
    std::string const kUfrag(payload.begin() + 2, payload.begin() + 2 + payload[1]);
    if (link.remote_ufrag != kUfrag) {
      link.remote_ufrag = kUfrag;
      link.ice->SetRemoteCredentials(kUfrag,
                                     std::string(payload.begin() + 2 + payload[1], payload.end()));
    }
  } else if (payload[0] == 2) {
    link.ice->AddRemoteCandidate(std::string(payload.begin() + 1, payload.end()));
  }
}

void OnlineRoom::Poll() {
  signaling_.Poll();
  auto const kNow = netutil::NowMs();
  if (kNow - last_keepalive_ >= 2000) {
    signaling_.SendKeepalive();
    if (host_) {
      signaling_.LockRoom(locked_);
    }
    last_keepalive_ = kNow;
  }
  std::vector<uint32_t> failed;
  for (auto& [peer, pointer] : links_) {
    auto& link = *pointer;
    if (link.ice) {
      link.ice->Poll();
      auto const kState = link.ice->CurrentState();
      if (kState == IceAgent::State::kConnected) {
        auto const kFd = link.bridge->Create(*link.ice);
        if (kFd == kBridgeInvalid || !link.transport.CreateHostOnBridgeSocket(kFd)) {
          link.failed = true;
        } else {
          auto const kPort = link.bridge->BridgePort();
          link.ice->on_local_candidate = nullptr;
          link.transport.AttachIce(std::move(link.bridge), std::move(link.ice));
          if (!host_ && !link.transport.ConnectExisting("::1", kPort)) {
            link.failed = true;
          }
        }
      } else if (kState == IceAgent::State::kFailed || kNow - link.created > 30000) {
        link.failed = true;
      } else if (kNow - link.last_ice_send >= 500) {
        SendIce(peer, link);
      }
    }
    link.transport.Poll();
    if (link.failed) {
      failed.push_back(peer);
    }
  }
  for (auto peer : failed) {
    links_.erase(peer);
    if (on_disconnected) {
      on_disconnected(peer);
    }
  }
}

void OnlineRoom::Lock(bool locked) {
  locked_ = locked;
  signaling_.LockRoom(locked);
}

std::vector<uint32_t> OnlineRoom::Peers() const {
  std::vector<uint32_t> result;
  for (auto const& [id, link] : links_) {
    if (link->connected && !link->failed) {
      result.push_back(id);
    }
  }
  return result;
}

void OnlineRoom::Send(uint32_t peer, const std::vector<uint8_t>& packet) {
  auto it = links_.find(peer);
  if (it == links_.end() || !it->second->connected) {
    return;
  }
  // ENet fragments each chunk reliably. Chunking bounds ENet's packet size even
  // for a large custom map or total conversion.
  constexpr size_t kChunkSize = 64 * 1024;
  for (size_t offset = 0; offset < packet.size(); offset += kChunkSize) {
    std::vector<uint8_t> chunk;
    Append32(chunk, static_cast<uint32_t>(packet.size()));
    Append32(chunk, static_cast<uint32_t>(offset));
    auto const kEnd = std::min(offset + kChunkSize, packet.size());
    chunk.insert(chunk.end(), packet.begin() + static_cast<std::ptrdiff_t>(offset),
                 packet.begin() + static_cast<std::ptrdiff_t>(kEnd));
    it->second->transport.SendRoomPacket(chunk);
  }
}

void OnlineRoom::Receive(uint32_t peer, const std::vector<uint8_t>& packet) {
  if (packet.size() < 8) {
    return;
  }
  auto& link = *links_.at(peer);
  auto const kSize = Read32(packet.data());
  auto const kOffset = Read32(packet.data() + 4);
  if (kSize == 0 || kSize > 256 * 1024 * 1024 || kOffset > kSize ||
      packet.size() - 8 > kSize - kOffset) {
    return;
  }
  if (kOffset == 0) {
    link.incoming.clear();
    link.incoming_size = kSize;
  }
  if (link.incoming_size != kSize || kOffset != link.incoming.size()) {
    return;
  }
  link.incoming.insert(link.incoming.end(), packet.begin() + 8, packet.end());
  if (link.incoming.size() == kSize && on_packet) {
    auto message = std::move(link.incoming);
    link.incoming_size = 0;
    on_packet(peer, message);
  }
}

bool OnlineRoom::Settled() const {
  return std::ranges::all_of(links_, [](const auto& link) { return link.second->connected; });
}
