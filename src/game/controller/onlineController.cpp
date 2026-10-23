#include "onlineController.hpp"

#include "gfx.hpp"
#include "keys.hpp"
#include "net/roomProtocol.hpp"
#include "spectatorviewport.hpp"
#include "viewport.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

using enum room_protocol::Type;
using room_protocol::Decode;
using room_protocol::Encode;

OnlineController::OnlineController(const std::shared_ptr<Common>& common,
                                   const std::shared_ptr<Settings>& settings,
                                   const std::shared_ptr<SoundPlayer>& sound, bool host,
                                   uint32_t round, Send send)
    : game(common, std::make_shared<Settings>(*settings), sound),
      host_(host),
      round_(round),
      send_(std::move(send)) {
  game.stats_recorder = std::make_shared<StatsRecorder>();
}

OnlineController::~OnlineController() {
  for (auto* viewport : game.viewports) {
    delete viewport;
  }
  for (auto* viewport : game.spectator_viewports) {
    delete viewport;
  }
  game.ClearViewports();
}

void OnlineController::Prepare(const std::vector<std::shared_ptr<WormSettings>>& players,
                               const std::vector<uint32_t>& peers, uint32_t seed) {
  std::vector<uint32_t> enabled_weapons;
  for (uint32_t weapon = 1; weapon <= game.common->weapons.size(); ++weapon) {
    if (game.settings->weap_table[game.common->weap_order[weapon - 1]] == 0) {
      enabled_weapons.push_back(weapon);
    }
  }
  if (enabled_weapons.empty()) {
    throw std::runtime_error("Enable at least one weapon before hosting");
  }
  peers_ = peers;
  game.rand.Seed(seed);
  for (size_t i = 0; i < players.size(); ++i) {
    auto worm = std::make_shared<Worm>();
    worm->index = static_cast<int>(i);
    worm->settings = std::make_shared<WormSettings>(*players[i]);
    worm->settings->controller = 0;
    game.AddWorm(worm);
  }
  game.level.GenerateFromSettings(*game.common, *game.settings, game.rand);
  for (auto& worm : game.worms) {
    // Draw independently for each player. Refill only after every enabled
    // weapon has been used, allowing small weapon pools without an endless retry.
    auto choices = enabled_weapons;
    auto remaining = static_cast<uint32_t>(choices.size());
    for (auto& weapon : worm->settings->weapons) {
      if (remaining == 0) {
        remaining = static_cast<uint32_t>(choices.size());
      }
      auto const kChosen = game.rand(remaining);
      weapon = choices[kChosen];
      std::swap(choices[kChosen], choices[--remaining]);
    }
    worm->InitWeapons(game);
  }
  game.ResetWorms();
  game.StartGame();
  game.paused = false;
  delay_ = static_cast<uint32_t>(std::clamp(game.settings->input_delay, 1, 14));
  InitializeInputs();
}

std::vector<uint8_t> OnlineController::Snapshot() const {
  std::vector<uint8_t> snapshot;
  game.SaveSnapshot(snapshot);
  return snapshot;
}

void OnlineController::Load(const std::vector<uint8_t>& snapshot, size_t local_index,
                            uint32_t delay, int blood_particles) {
  game.LoadSnapshot(snapshot);
  for (auto* viewport : game.viewports) {
    delete viewport;
  }
  game.ClearViewports();
  local_index_ = local_index;
  if (local_index_ >= game.worms.size() || delay == 0 || delay > 14) {
    throw std::runtime_error("Invalid online player assignment");
  }
  delay_ = delay;
  if (blood_particles >= 0) {
    game.settings->blood_particle_max = blood_particles;
  }
  game.bobjects.Resize(game.settings->blood_particle_max);
  game.paused = false;
  InitializeInputs();
}

void OnlineController::InitializeInputs() {
  inputs_.resize(game.worms.size());
  player_paused_.resize(game.worms.size(), false);
  for (auto& inputs : inputs_) {
    for (uint32_t frame = 0; frame < delay_; ++frame) {
      inputs[frame] = 0;
    }
  }
  next_input_ = delay_;
  game.AddViewport(new Viewport(Rect(0, 0, 320, 158), static_cast<int>(local_index_)));
  game.AddSpectatorViewport(new SpectatorViewport(Rect(0, 0, 640, 400)));
}

void OnlineController::Broadcast(const std::vector<uint8_t>& packet) {
  for (auto peer : peers_) {
    send_(peer, packet);
  }
}

void OnlineController::Step(const std::vector<uint8_t>& inputs) {
  for (size_t i = 0; i < game.worms.size(); ++i) {
    game.worms[i]->control_states.Unpack(inputs[i]);
  }
  game.ProcessFrame();
  ++frame_;
  finished_ = game.IsGameOver();
}

void OnlineController::Receive(uint32_t peer, const std::vector<uint8_t>& packet) {
  if (packet.empty()) {
    return;
  }
  uint32_t round = 0;
  if (packet[0] == kInput && host_) {
    uint32_t frame = 0;
    uint8_t input = 0;
    Decode(packet, round, frame, input);
    auto const kIt = std::ranges::find(peers_, peer);
    if (round != round_ || kIt == peers_.end() || frame < frame_ || frame > frame_ + 64) {
      return;
    }
    auto const kIndex = static_cast<size_t>(kIt - peers_.begin()) + 1;
    inputs_[kIndex].try_emplace(frame, input);
  } else if (packet[0] == kFrame && !host_) {
    uint32_t frame = 0;
    Tick tick;
    Decode(packet, round, frame, tick.inputs, tick.checksum);
    if (round == round_ && tick.inputs.size() == game.worms.size() && frame >= frame_ &&
        frame <= frame_ + 64) {
      ticks_.try_emplace(frame, std::move(tick));
    }
  } else if (packet[0] == kPause) {
    bool paused = false;
    Decode(packet, round, paused);
    if (round != round_) {
      return;
    }
    if (host_) {
      auto const kIt = std::ranges::find(peers_, peer);
      if (kIt == peers_.end()) {
        return;
      }
      player_paused_[static_cast<size_t>(kIt - peers_.begin()) + 1] = paused;
      UpdatePaused();
    } else {
      paused_ = paused;
    }
  } else if (packet[0] == kFinished && !host_) {
    Decode(packet, round);
    if (round == round_) {
      finished_ = true;
    }
  }
}

void OnlineController::UpdatePaused() {
  paused_ = std::ranges::any_of(player_paused_, [](bool paused) { return paused; });
  Broadcast(Encode(kPause, round_, paused_));
}

void OnlineController::OnKey(int key, bool state) {
  if (key == SDLToDOSKey(SDL_SCANCODE_TAB)) {
    show_scores_ = state;
  }
  if (state && key == SDLToDOSKey(SDL_SCANCODE_PAGEDOWN)) {
    ++score_page_;
  }
  if (state && key == SDLToDOSKey(SDL_SCANCODE_PAGEUP) && score_page_ > 0) {
    --score_page_;
  }
  if (key == kDkEscape && state) {
    local_paused_ = !local_paused_;
    controls_.Unpack(0);
    clean_controls_.Unpack(0);
    if (host_) {
      player_paused_[0] = local_paused_;
      UpdatePaused();
    } else {
      send_(0, Encode(kPause, round_, local_paused_));
    }
    return;
  }
  auto const& settings = *game.worms[local_index_]->settings;
  for (int i = 0; i < WormSettings::kMaxControlEx; ++i) {
    bool const kKeyboard = settings.input_device == WormSettings::kInputKeyboard;
    if ((kKeyboard && std::cmp_equal(settings.controls_ex[i], key)) ||
        (!kKeyboard && IsGamepadControlKey(key) &&
         (key - kGamepadControlKeysStart) % 8 == static_cast<uint32_t>(i))) {
      clean_controls_.Set(static_cast<Worm::Control>(i), state);
      controls_.Unpack(clean_controls_.Pack());
      if (clean_controls_[WormSettings::kDig]) {
        controls_.Set(Worm::kLeft, /*v=*/true);
        controls_.Set(Worm::kRight, /*v=*/true);
      }
    }
  }
}

void OnlineController::Focus() {
  game.Focus(gfx.play_renderer);
  game.Focus(gfx.single_screen_renderer);
}
void OnlineController::Unfocus() {
  controls_.Unpack(0);
  clean_controls_.Unpack(0);
}
void OnlineController::SwapLevel(Level& level) { game.level.Swap(level); }

bool OnlineController::Process() {
  if (finished_) {
    return false;
  }
  // Sample each future input exactly once, including while stalled. Keeping
  // a short pipeline lets the host receive inputs before their frame is due.
  if (next_input_ <= frame_ + delay_) {
    auto const kPackedInput = static_cast<uint8_t>(controls_.Pack());
    if (host_) {
      inputs_[0].try_emplace(next_input_, kPackedInput);
    } else {
      send_(0, Encode(kInput, round_, next_input_, kPackedInput));
    }
    ++next_input_;
  }
  if (paused_) {
    return true;
  }
  if (host_) {
    std::vector<uint8_t> inputs;
    for (auto& player : inputs_) {
      auto const kIt = player.find(frame_);
      if (kIt == player.end()) {
        return true;
      }
      inputs.push_back(kIt->second);
    }
    auto const kTickFrame = frame_;
    Step(inputs);
    auto const kChecksum = WideRollbackChecksum(game);
    Broadcast(Encode(kFrame, round_, kTickFrame, inputs, kChecksum));
    for (auto& player : inputs_) {
      player.erase(kTickFrame);
    }
    if (frame_ % 700 == 0) {
      std::fprintf(stderr, "[online] frame=%u players=%zu checksum=%08x\n", frame_,
                   game.worms.size(), kChecksum);
    }
  } else {
    auto const kIt = ticks_.find(frame_);
    if (kIt == ticks_.end()) {
      return true;
    }
    Step(kIt->second.inputs);
    auto const kChecksum = WideRollbackChecksum(game);
    if (kChecksum != kIt->second.checksum) {
      desynced_ = true;
      finished_ = true;
    }
    ticks_.erase(kIt);
    if (frame_ % 700 == 0) {
      std::fprintf(stderr, "[online] frame=%u players=%zu checksum=%08x\n", frame_,
                   game.worms.size(), kChecksum);
    }
  }
  return !finished_;
}

void OnlineController::Finish() {
  if (host_) {
    Broadcast(Encode(kFinished, round_));
  }
  finished_ = true;
}

void OnlineController::Draw(Renderer& renderer, bool spectator) {
  game.Draw(renderer, kStateGame, spectator);
  auto& font = game.common->font;
  auto const& local = *game.worms[local_index_];
  font.DrawString(renderer.bmp, local.settings->name + "  LIVES " + std::to_string(local.lives),
                  110, renderer.render_res_y - 27, 7);
  if (show_scores_) {
    score_page_ %= std::max(size_t{1}, (game.worms.size() + 10) / 11);
    FillRect(renderer.bmp, 4, 4, 312, 154, 0);
    font.DrawString(renderer.bmp, "PLAYERS - PGUP/PGDN: MORE", 10, 10, 50);
    for (size_t row = 0; row < 11; ++row) {
      auto const kIndex = score_page_ * 11 + row;
      if (kIndex >= game.worms.size()) {
        break;
      }
      auto const& worm = *game.worms[kIndex];
      font.DrawString(renderer.bmp,
                      std::to_string(kIndex + 1) + ". " + worm.settings->name + " KILLS " +
                          std::to_string(worm.kills) + " LIVES " + std::to_string(worm.lives),
                      10, 25 + static_cast<int>(row) * 11, kIndex == local_index_ ? 50 : 7);
    }
  }
  if (paused_) {
    font.DrawString(renderer.bmp, "PAUSED - ESC: RESUME / Q: LEAVE", 12, 80, 50);
    if (host_) {
      font.DrawString(renderer.bmp, "E: END MATCH", 12, 95, 7);
    }
  }
}
