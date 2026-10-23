#pragma once

#include "serialization/cereal_types.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <sstream>
#include <vector>

namespace room_protocol {
enum Type : uint8_t {
  kProfile = 1,
  kRoster,
  kTc,
  kStart,
  kInput,
  kFrame,
  kPause,
  kFinished,
  kLobby
};

template <typename... Args>
std::vector<uint8_t> Encode(Type type, const Args&... args) {
  std::ostringstream stream(std::ios::binary);
  {
    // NOLINTNEXTLINE(misc-const-correctness) — non-empty packs mutate the archive.
    cereal::PortableBinaryOutputArchive archive(stream);
    if constexpr (sizeof...(Args) > 0) {
      archive(args...);
    }
  }
  auto const kBytes = stream.str();
  std::vector<uint8_t> packet{static_cast<uint8_t>(type)};
  packet.insert(packet.end(), kBytes.begin(), kBytes.end());
  return packet;
}

template <typename... Args>
void Decode(const std::vector<uint8_t>& packet, Args&... args) {
  std::string const kBytes(packet.begin() + 1, packet.end());
  std::istringstream stream(kBytes, std::ios::binary);
  // NOLINTNEXTLINE(misc-const-correctness) — deserialization mutates the archive.
  cereal::PortableBinaryInputArchive archive(stream);
  archive(args...);
}
}  // namespace room_protocol
