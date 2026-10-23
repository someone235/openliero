#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "controller/onlineController.hpp"
#include "net/roomProtocol.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <set>

namespace {
struct Message {
  size_t destination;
  uint32_t sender;
  std::vector<uint8_t> data;
};

struct OnlineMatch {
  std::shared_ptr<Common> common = std::make_shared<Common>();
  std::shared_ptr<Settings> settings = std::make_shared<Settings>();
  std::shared_ptr<NullSoundPlayer> sound = std::make_shared<NullSoundPlayer>();
  std::vector<std::unique_ptr<OnlineController>> controllers;
  std::deque<Message> messages;

  explicit OnlineMatch(size_t count, uint32_t seed = 42,
                       const std::function<void(Settings&, Common&)>& configure = {}) {
    PrecomputeTables();
    common->load(FsNode("data") / "TC" / "openliero");
    settings->lives = 100;
    settings->loading_time = 0;
    settings->input_delay = 3;
    if (configure) {
      configure(*settings, *common);
    }
    std::vector<std::shared_ptr<WormSettings>> players;
    std::vector<uint32_t> peers;
    for (size_t i = 0; i < count; ++i) {
      auto profile =
          std::make_shared<WormSettings>(*settings->worm_settings[Settings::kNetworkPlayerIdx]);
      profile->name = "PLAYER " + std::to_string(i + 1);
      players.push_back(std::move(profile));
      if (i != 0) {
        peers.push_back(static_cast<uint32_t>(i));
      }
    }
    controllers.push_back(std::make_unique<OnlineController>(
        common, settings, sound, true, 1, [this](uint32_t peer, const std::vector<uint8_t>& data) {
          messages.push_back({.destination = peer, .sender = 0, .data = data});
        }));
    controllers[0]->Prepare(players, peers, seed);
    auto const kSnapshot = controllers[0]->Snapshot();
    for (size_t i = 1; i < count; ++i) {
      controllers.push_back(std::make_unique<OnlineController>(
          common, settings, sound, false, 1, [this, i](uint32_t, const std::vector<uint8_t>& data) {
            messages.push_back(
                {.destination = 0, .sender = static_cast<uint32_t>(i), .data = data});
          }));
      controllers.back()->Load(kSnapshot, i, controllers[0]->InputDelay());
    }
  }

  void Deliver() {
    while (!messages.empty()) {
      auto message = std::move(messages.front());
      messages.pop_front();
      controllers[message.destination]->Receive(message.sender, message.data);
    }
  }

  void Tick(int tick) {
    Deliver();
    for (size_t i = 0; i < controllers.size(); ++i) {
      // Distinct movement/aim/fire input for each worm, including fire edges
      // needed to spawn. Some guests deliberately pause delivery for jitter.
      auto const kInput =
          static_cast<uint8_t>(((tick / 11 + static_cast<int>(i) * 13) * 37) & 0x7f);
      controllers[i]->SetLocalInput(kInput);
      if ((tick + static_cast<int>(i)) % 5 != 0) {
        controllers[i]->Process();
      }
    }
  }
};
}  // namespace

TEST_CASE("Online matches support dynamic rosters and independent inputs", "[online]") {
  auto const kCount = GENERATE(size_t{3}, size_t{8}, size_t{17});
  OnlineMatch match(kCount);
  for (int tick = 0; tick < 1600; ++tick) {
    match.Tick(tick);
  }
  for (int tick = 0; tick < 100; ++tick) {
    match.Deliver();
    for (size_t i = 1; i < kCount; ++i) {
      match.controllers[i]->Process();
    }
  }
  auto const kFrame = match.controllers[0]->Frame();
  REQUIRE(kFrame > 500);
  auto const kChecksum = WideRollbackChecksum(match.controllers[0]->game);
  for (auto const& controller : match.controllers) {
    REQUIRE(controller->game.worms.size() == kCount);
    REQUIRE(controller->Frame() == kFrame);
    REQUIRE_FALSE(controller->Desynced());
    REQUIRE(WideRollbackChecksum(controller->game) == kChecksum);
  }
}

TEST_CASE("Online host waits for every guest and rejects stale round inputs", "[online]") {
  OnlineMatch match(4);
  for (int i = 0; i < 20; ++i) {
    match.controllers[0]->Process();
  }
  REQUIRE(match.controllers[0]->Frame() == match.controllers[0]->InputDelay());
  auto const kFrame = match.controllers[0]->Frame();
  for (uint32_t peer = 1; peer < 4; ++peer) {
    match.controllers[0]->Receive(
        peer, room_protocol::Encode(room_protocol::kInput, uint32_t{0}, kFrame, uint8_t{1}));
  }
  match.controllers[0]->Process();
  REQUIRE(match.controllers[0]->Frame() == kFrame);
  for (uint32_t peer = 1; peer < 3; ++peer) {
    match.controllers[0]->Receive(
        peer, room_protocol::Encode(room_protocol::kInput, uint32_t{1}, kFrame, uint8_t{1}));
  }
  match.controllers[0]->Process();
  REQUIRE(match.controllers[0]->Frame() == kFrame);
  match.controllers[0]->Receive(
      3, room_protocol::Encode(room_protocol::kInput, uint32_t{1}, kFrame, uint8_t{2}));
  match.controllers[0]->Process();
  REQUIRE(match.controllers[0]->Frame() == kFrame + 1);
  REQUIRE(match.controllers[0]->game.worms[1]->control_states.Pack() == 1);
  REQUIRE(match.controllers[0]->game.worms[3]->control_states.Pack() == 2);
}

TEST_CASE("An eliminated player does not end a free-for-all with multiple survivors", "[online]") {
  OnlineMatch match(5);
  auto& game = match.controllers[0]->game;
  game.worms[0]->lives = 0;
  REQUIRE_FALSE(game.IsGameOver());
  game.worms[1]->lives = 0;
  game.worms[2]->lives = 0;
  REQUIRE_FALSE(game.IsGameOver());
  game.worms[3]->lives = 0;
  REQUIRE(game.IsGameOver());
}

TEST_CASE("Online pause requires every paused player to resume", "[online]") {
  OnlineMatch match(3);
  auto& host = *match.controllers[0];
  host.Receive(1, room_protocol::Encode(room_protocol::kPause, uint32_t{1}, true));
  host.Receive(2, room_protocol::Encode(room_protocol::kPause, uint32_t{1}, true));
  host.Receive(1, room_protocol::Encode(room_protocol::kPause, uint32_t{1}, false));
  match.Deliver();
  REQUIRE(host.Paused());
  auto const kFrame = host.Frame();
  host.Process();
  REQUIRE(host.Frame() == kFrame);
  for (auto const& controller : match.controllers) {
    REQUIRE(controller->Paused());
  }
  host.Receive(2, room_protocol::Encode(room_protocol::kPause, uint32_t{1}, false));
  match.Deliver();
  for (auto const& controller : match.controllers) {
    REQUIRE_FALSE(controller->Paused());
  }
}

TEST_CASE("Online digging preserves independent movement keys", "[online]") {
  OnlineMatch match(3);
  auto& host = *match.controllers[0];
  auto& profile = *host.game.worms[0]->settings;
  profile.controls_ex[WormSettings::kDig] = 45;
  host.OnKey(static_cast<int>(profile.controls_ex[Worm::kLeft]), /*state=*/true);
  host.OnKey(45, /*state=*/true);
  for (int tick = 0; tick < 4; ++tick) {
    match.Deliver();
    for (auto& controller : match.controllers) {
      controller->Process();
    }
  }
  REQUIRE(host.game.worms[0]->Pressed(Worm::kLeft));
  REQUIRE(host.game.worms[0]->Pressed(Worm::kRight));
  host.OnKey(45, /*state=*/false);
  for (int tick = 0; tick < 4; ++tick) {
    match.Deliver();
    for (auto& controller : match.controllers) {
      controller->Process();
    }
  }
  REQUIRE(host.game.worms[0]->Pressed(Worm::kLeft));
  REQUIRE_FALSE(host.game.worms[0]->Pressed(Worm::kRight));
}

TEST_CASE("Online players get independent random loadouts synchronized to every client",
          "[online][weapons]") {
  OnlineMatch match(8, 42, [](Settings& settings, Common&) {
    // Reproduce identical saved profiles: all five slots contain weapon one.
    for (auto& weapon : settings.worm_settings[Settings::kNetworkPlayerIdx]->weapons) {
      weapon = 1;
    }
  });
  auto const& host = *match.controllers[0];
  std::set<std::array<uint32_t, NUM_WEAPONS>> loadouts;
  for (size_t player = 0; player < host.game.worms.size(); ++player) {
    auto const& worm = *host.game.worms[player];
    std::array<uint32_t, NUM_WEAPONS> loadout{};
    std::ranges::copy(worm.settings->weapons, loadout.begin());
    std::ranges::sort(loadout);
    REQUIRE(std::set<uint32_t>(loadout.begin(), loadout.end()).size() == NUM_WEAPONS);
    REQUIRE(loadout.front() >= 1);
    REQUIRE(loadout.back() <= 40);
    loadouts.insert(loadout);
    for (auto const& controller : match.controllers) {
      auto const& replica = *controller->game.worms[player];
      for (int slot = 0; slot < NUM_WEAPONS; ++slot) {
        REQUIRE(replica.settings->weapons[slot] == worm.settings->weapons[slot]);
        REQUIRE(replica.weapons[slot].type ==
                &match.common->weapons[match.common->weap_order[worm.settings->weapons[slot] - 1]]);
      }
    }
  }
  REQUIRE(loadouts.size() == host.game.worms.size());
}

TEST_CASE("Online weapon draws are deterministic and change with the next round seed",
          "[online][weapons]") {
  OnlineMatch first(3, 42);
  OnlineMatch repeat(3, 42);
  OnlineMatch next_round(3, 43);
  bool changed = false;
  for (size_t player = 0; player < first.controllers[0]->game.worms.size(); ++player) {
    auto const& original = *first.controllers[0]->game.worms[player]->settings;
    auto const& repeated = *repeat.controllers[0]->game.worms[player]->settings;
    auto const& rerolled = *next_round.controllers[0]->game.worms[player]->settings;
    for (int slot = 0; slot < NUM_WEAPONS; ++slot) {
      REQUIRE(original.weapons[slot] == repeated.weapons[slot]);
      changed = changed || original.weapons[slot] != rerolled.weapons[slot];
    }
  }
  REQUIRE(changed);
}

TEST_CASE("Online random loadouts respect restricted weapon pools", "[online][weapons]") {
  auto const kEnabledCount = GENERATE(size_t{1}, size_t{3}, size_t{5});
  std::array<uint32_t, 5> const kAllowed = {4, 9, 17, 23, 38};
  OnlineMatch match(3, 42, [&](Settings& settings, Common& common) {
    std::ranges::fill(settings.weap_table, uint32_t{2});
    for (size_t i = 0; i < kEnabledCount; ++i) {
      settings.weap_table[common.weap_order[kAllowed[i] - 1]] = 0;
    }
  });
  for (auto const& worm : match.controllers[0]->game.worms) {
    std::set<uint32_t> unique;
    for (auto weapon : worm->settings->weapons) {
      REQUIRE(match.settings->weap_table[match.common->weap_order[weapon - 1]] == 0);
      unique.insert(weapon);
    }
    REQUIRE(unique.size() == kEnabledCount);
  }
}

TEST_CASE("Online games reject an empty weapon pool without hanging", "[online][weapons]") {
  REQUIRE_THROWS_WITH(OnlineMatch(3, 42,
                                  [](Settings& settings, Common&) {
                                    std::ranges::fill(settings.weap_table, uint32_t{2});
                                  }),
                      "Enable at least one weapon before hosting");
}

namespace {
void AdvanceHeldInputs(OnlineMatch& match, const std::vector<uint8_t>& inputs, int ticks) {
  for (int tick = 0; tick < ticks; ++tick) {
    match.Deliver();
    for (size_t player = 0; player < match.controllers.size(); ++player) {
      match.controllers[player]->SetLocalInput(inputs[player]);
      match.controllers[player]->Process();
    }
    match.Deliver();
    for (size_t player = 1; player < match.controllers.size(); ++player) {
      match.controllers[player]->Process();
    }
  }
}

void MakeOnlineWormsVisible(OnlineMatch& match) {
  for (auto& controller : match.controllers) {
    for (auto& worm : controller->game.worms) {
      worm->visible = true;
      worm->ready = false;
      worm->killed_timer = 0;
      worm->pos = fixedvec(Itof(80 + worm->index * 80), Itof(80));
    }
  }
}
}  // namespace

TEST_CASE("Online weapon selection switches once per direction press", "[online][weapons][input]") {
  auto const kPlayer = GENERATE(size_t{0}, size_t{1}, size_t{2});
  auto const kDirection = GENERATE(Worm::kLeft, Worm::kRight);
  OnlineMatch match(3);
  MakeOnlineWormsVisible(match);
  std::vector<uint8_t> held(3, 0);
  held[kPlayer] = 1 << Worm::kChange;
  AdvanceHeldInputs(match, held, 8);
  int expected = 0;
  for (int press = 0; press < 2; ++press) {
    expected = (expected + (kDirection == Worm::kRight ? 1 : 4)) % NUM_WEAPONS;
    held[kPlayer] |= 1 << kDirection;
    // A short press spans several 70 Hz ticks. Keep holding well beyond the
    // old repeat interval and check every tick, including across a full cycle.
    AdvanceHeldInputs(match, held, 8);
    for (int tick = 0; tick < 35; ++tick) {
      AdvanceHeldInputs(match, held, 1);
      for (auto const& controller : match.controllers) {
        REQUIRE_FALSE(controller->Desynced());
        REQUIRE(controller->game.worms[kPlayer]->current_weapon == expected);
      }
    }
    held[kPlayer] &= ~(1 << kDirection);
    AdvanceHeldInputs(match, held, 8);
    for (auto const& controller : match.controllers) {
      REQUIRE(controller->game.worms[kPlayer]->current_weapon == expected);
    }
  }
}

TEST_CASE("Online weapon selection ignores a direction already held when change is pressed",
          "[online][weapons][input]") {
  OnlineMatch match(3);
  MakeOnlineWormsVisible(match);
  std::vector<uint8_t> held(3, 0);
  held[1] = 1 << Worm::kRight;
  AdvanceHeldInputs(match, held, 8);
  held[1] |= 1 << Worm::kChange;
  AdvanceHeldInputs(match, held, 20);
  for (auto const& controller : match.controllers) {
    REQUIRE(controller->game.worms[1]->current_weapon == 0);
  }
  held[1] = 1 << Worm::kChange;
  AdvanceHeldInputs(match, held, 8);
  held[1] |= 1 << Worm::kRight;
  AdvanceHeldInputs(match, held, 8);
  for (auto const& controller : match.controllers) {
    REQUIRE(controller->game.worms[1]->current_weapon == 1);
  }
}

TEST_CASE("Online movement remains held until the direction is released", "[online][input]") {
  OnlineMatch match(3);
  MakeOnlineWormsVisible(match);
  std::vector<uint8_t> held(3, 0);
  held[2] = 1 << Worm::kRight;
  AdvanceHeldInputs(match, held, 8);
  for (int tick = 0; tick < 15; ++tick) {
    AdvanceHeldInputs(match, held, 1);
    for (auto const& controller : match.controllers) {
      REQUIRE(controller->game.worms[2]->Pressed(Worm::kRight));
    }
  }
  held[2] = 0;
  AdvanceHeldInputs(match, held, 8);
  for (auto const& controller : match.controllers) {
    REQUIRE_FALSE(controller->game.worms[2]->Pressed(Worm::kRight));
  }
}
