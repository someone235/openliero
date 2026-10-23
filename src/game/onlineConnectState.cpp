#include "onlineConnectState.hpp"

#include "controller/onlineController.hpp"
#include "gfx.hpp"
#include "keys.hpp"
#include "net/netutil.hpp"
#include "net/roomProtocol.hpp"
#include "net/tcArchive.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using enum room_protocol::Type;
using room_protocol::Decode;
using room_protocol::Encode;

OnlineConnectState::OnlineConnectState(NetSession::Role role, std::string room_code)
    : host_(role == NetSession::kHost), code_(std::move(room_code)) {}

OnlineController* OnlineConnectState::Controller() const {
  return dynamic_cast<OnlineController*>(gfx->controller.get());
}

void OnlineConnectState::Enter() {
  entered_ = netutil::NowMs();
  original_common_ = gfx->common;
  profile_ =
      std::make_shared<WormSettings>(*gfx->settings->worm_settings[Settings::kNetworkPlayerIdx]);
  profile_->controller = 0;
  if (profile_->name.empty()) {
    profile_->name = host_ ? "HOST" : "PLAYER";
  }
  room_ = std::make_unique<OnlineRoom>(host_);
  room_->on_error = [this](const std::string& message) { error_ = message; };
  room_->on_connected = [this](uint32_t peer) {
    std::fprintf(stderr, "[online] peer %u connected\n", peer);
    if (host_) {
      if (playing_ || scores_) {
        return;
      }
      room_->Send(peer, Encode(kTc, tc_archive_));
    }
  };
  room_->on_disconnected = [this](uint32_t peer) {
    std::fprintf(stderr, "[online] peer %u disconnected\n", peer);
    if (host_) {
      players_.erase(peer);
      if (playing_ || scores_) {
        if (auto* controller = Controller()) {
          controller->Finish();
        }
        ReturnToLobby();
      }
      SendRoster();
    } else {
      error_ = "HOST DISCONNECTED";
    }
  };
  room_->on_packet = [this](uint32_t peer, const std::vector<uint8_t>& packet) {
    try {
      Receive(peer, packet);
    } catch (const std::exception& exception) {
      std::fprintf(stderr, "[online] invalid room packet: %s\n", exception.what());
      error_ = "INVALID ONLINE GAME DATA";
    }
  };
  std::string server = "liero-server.orbmit.org";
  if (auto const* value = std::getenv("OPENLIERO_SIGNALING_SERVER")) {
    server = value;
  }
  uint16_t port = 19533;
  if (auto const* value = std::getenv("OPENLIERO_SIGNALING_PORT")) {
    port = static_cast<uint16_t>(std::strtoul(value, nullptr, 10));
  }
  if (host_) {
    tc_archive_ = tc_archive::Pack(gfx->GetConfigNode() / "TC" / gfx->settings->tc);
    players_[0] = {.settings = profile_, .ready = true};
    tc_ready_ = true;
    SendRoster();
  }
  if (!room_->Open(server, port, code_)) {
    error_ = "FAILED TO REACH SIGNALING SERVER";
  }
}

void OnlineConnectState::Leave() {
  if (auto* controller = Controller()) {
    controller->MarkUnresumable();
  }
  if (Controller()) {
    gfx->controller = std::move(previous_controller_);
  }
  room_.reset();
  gfx->common = original_common_;
  gfx->play_renderer.LoadPalette(*gfx->common);
  gfx->single_screen_renderer.LoadPalette(*gfx->common);
  if (auto* player = dynamic_cast<DefaultSoundPlayer*>(gfx->sound_player.get())) {
    player->SetCommon(*gfx->common);
  }
  gfx->ClearKeys();
}

void OnlineConnectState::SendProfile() {
  if (host_) {
    players_[0] = {.settings = profile_, .ready = true};
    SendRoster();
  } else {
    for (auto peer : room_->Peers()) {
      room_->Send(peer, Encode(kProfile, *profile_, local_ready_));
    }
  }
}

void OnlineConnectState::SendRoster() {
  names_.clear();
  ready_.clear();
  for (const auto& [peer, player] : players_) {
    names_.push_back(player.settings->name);
    ready_.push_back(static_cast<uint8_t>(player.ready));
  }
  for (auto peer : room_->Peers()) {
    room_->Send(peer, Encode(kRoster, names_, ready_));
  }
}

void OnlineConnectState::Receive(uint32_t peer, const std::vector<uint8_t>& packet) {
  if (packet.empty()) {
    return;
  }
  switch (packet[0]) {
    case kTc: {
      if (host_ || playing_) {
        return;
      }
      std::vector<uint8_t> archive;
      Decode(packet, archive);
      tc_fs_ = std::make_shared<MemoryFs>();
      for (auto& file : tc_archive::Unpack(archive.data(), archive.size())) {
        tc_fs_->files[file.name] = std::move(file.data);
      }
      auto common = std::make_shared<Common>();
      common->load(tc_fs_->Root());
      gfx->common = common;
      gfx->play_renderer.LoadPalette(*common);
      gfx->single_screen_renderer.LoadPalette(*common);
      if (auto* player = dynamic_cast<DefaultSoundPlayer*>(gfx->sound_player.get())) {
        player->SetCommon(*common);
      }
      tc_ready_ = true;
      SendProfile();
      break;
    }
    case kProfile: {
      if (!host_ || playing_ || scores_ || packet.size() > 8192) {
        return;
      }
      auto profile = std::make_shared<WormSettings>();
      bool ready = false;
      Decode(packet, *profile, ready);
      profile->name.resize(std::min(profile->name.size(), size_t{20}));
      profile->controller = 0;
      profile->health = profile_->health;
      for (auto& weapon : profile->weapons) {
        if (weapon > 40) {
          weapon = 0;
        }
      }
      players_[peer] = {.settings = std::move(profile), .ready = ready};
      SendRoster();
      break;
    }
    case kRoster:
      if (!host_) {
        Decode(packet, names_, ready_);
      }
      break;
    case kStart: {
      if (host_ || !tc_ready_) {
        return;
      }
      uint32_t index = 0;
      uint32_t delay = 0;
      int blood_particles = 0;
      std::vector<uint8_t> snapshot;
      Decode(packet, round_, index, delay, blood_particles, snapshot);
      auto controller = std::make_unique<OnlineController>(
          gfx->common, gfx->settings, gfx->sound_player, false, round_,
          [this](uint32_t, const std::vector<uint8_t>& data) {
            for (auto host : room_->Peers()) {
              room_->Send(host, data);
            }
          });
      controller->Load(snapshot, index, delay, blood_particles);
      if (!previous_controller_) {
        previous_controller_ = std::move(gfx->controller);
      }
      gfx->controller = std::move(controller);
      playing_ = true;
      scores_ = false;
      Controller()->Focus();
      gfx->ClearKeys();
      std::fprintf(stderr, "[online] started round=%u players=%zu local=%u\n", round_,
                   Controller()->game.worms.size(), index);
      break;
    }
    case kLobby:
      if (!host_) {
        playing_ = false;
        scores_ = false;
        local_ready_ = false;
        gfx->controller = std::move(previous_controller_);
        SendProfile();
      }
      break;
    default:
      if (auto* controller = Controller()) {
        controller->Receive(peer, packet);
      }
      break;
  }
}

void OnlineConnectState::StartMatch() {
  if (!room_->Settled() || players_.size() < 2 || players_.size() != room_->Peers().size() + 1 ||
      std::ranges::any_of(players_, [](const auto& player) { return !player.second.ready; })) {
    return;
  }
  if (std::ranges::none_of(gfx->settings->weap_table, [](uint32_t state) { return state == 0; })) {
    error_ = "ENABLE AT LEAST ONE WEAPON";
    return;
  }
  ++round_;
  room_->Lock(/*locked=*/true);
  auto controller = std::make_unique<OnlineController>(
      gfx->common, gfx->settings, gfx->sound_player, true, round_,
      [this](uint32_t peer, const std::vector<uint8_t>& data) { room_->Send(peer, data); });
  std::vector<std::shared_ptr<WormSettings>> profiles;
  std::vector<uint32_t> peers;
  for (const auto& [peer, player] : players_) {
    profiles.push_back(player.settings);
    if (peer != 0) {
      peers.push_back(peer);
    }
  }
  controller->Prepare(profiles, peers, static_cast<uint32_t>(netutil::NowMs()));
  auto snapshot = controller->Snapshot();
  uint32_t index = 1;
  for (auto peer : peers) {
    room_->Send(peer, Encode(kStart, round_, index++, controller->InputDelay(),
                             controller->game.settings->blood_particle_max, snapshot));
  }
  if (!previous_controller_) {
    previous_controller_ = std::move(gfx->controller);
  }
  gfx->controller = std::move(controller);
  playing_ = true;
  scores_ = false;
  Controller()->Focus();
  gfx->ClearKeys();
  std::fprintf(stderr, "[online] started round=%u players=%zu local=0\n", round_, profiles.size());
}

void OnlineConnectState::ReturnToLobby() {
  for (auto peer : room_->Peers()) {
    room_->Send(peer, Encode(kLobby));
  }
  gfx->controller = std::move(previous_controller_);
  playing_ = false;
  scores_ = false;
  for (auto& [peer, player] : players_) {
    player.ready = (peer == 0);
  }
  room_->Lock(/*locked=*/false);
  SendRoster();
  gfx->ClearKeys();
}

void OnlineConnectState::HandleEvent(SDL_Event& event) {
  gfx->ProcessEvent(event, playing_ ? gfx->controller.get() : nullptr);
}

bool OnlineConnectState::Update() {
  room_->Poll();
  if (room_->Code().empty() && netutil::NowMs() - entered_ > 15000) {
    error_ = "SIGNALING TIMED OUT - CHECK SERVER VERSION";
  }
  if (!error_.empty()) {
    return !gfx->TestSdlKeyOnce(SDL_SCANCODE_ESCAPE) && !gfx->TestSdlKeyOnce(SDL_SCANCODE_RETURN);
  }
  if (playing_) {
    auto* controller = Controller();
    if (controller->Paused() && gfx->TestSdlKeyOnce(SDL_SCANCODE_Q)) {
      return false;
    }
    if (host_ && controller->Paused() && gfx->TestSdlKeyOnce(SDL_SCANCODE_E)) {
      controller->Finish();
    }
    if (!controller->Process()) {
      playing_ = false;
      scores_ = true;
      if (controller->Desynced()) {
        error_ = "ONLINE GAME DESYNC";
      }
      gfx->ClearKeys();
    }
    return true;
  }
  if (gfx->TestSdlKeyOnce(SDL_SCANCODE_ESCAPE)) {
    return false;
  }
  if (scores_) {
    if (gfx->TestSdlKeyOnce(SDL_SCANCODE_PAGEDOWN)) {
      ++roster_page_;
    }
    if (gfx->TestSdlKeyOnce(SDL_SCANCODE_PAGEUP) && roster_page_ > 0) {
      --roster_page_;
    }
    if (host_ && gfx->TestSdlKeyOnce(SDL_SCANCODE_RETURN)) {
      ReturnToLobby();
    }
    return true;
  }
  if (gfx->TestSdlKeyOnce(SDL_SCANCODE_RETURN) && tc_ready_) {
    if (host_) {
      StartMatch();
    } else {
      local_ready_ = !local_ready_;
      SendProfile();
    }
  }
  if (gfx->TestSdlKeyOnce(SDL_SCANCODE_PAGEDOWN)) {
    ++roster_page_;
  }
  if (gfx->TestSdlKeyOnce(SDL_SCANCODE_PAGEUP) && roster_page_ > 0) {
    --roster_page_;
  }
  return true;
}

void OnlineConnectState::DrawLobby() {
  auto& font = gfx->common->font;
  auto& bmp = gfx->play_renderer.bmp;
  auto const& code = room_->Code();
  font.DrawString(bmp, code.empty() ? "CONNECTING TO ONLINE ROOM..." : "ROOM CODE: " + code, 12, 12,
                  50);
  font.DrawString(bmp,
                  std::to_string(names_.size()) +
                      (host_ ? " PLAYERS - ONLINE LOBBY" : " PLAYERS - WAITING FOR HOST"),
                  12, 27, 7);
  size_t const kPageCount = std::max(size_t{1}, (names_.size() + 7) / 8);
  roster_page_ %= kPageCount;
  for (size_t row = 0; row < 8; ++row) {
    auto const kI = roster_page_ * 8 + row;
    if (kI >= names_.size()) {
      break;
    }
    auto const kReady = kI < ready_.size() && ready_[kI] != 0;
    font.DrawString(
        bmp, std::to_string(kI + 1) + ". " + names_[kI] + (kReady ? "  READY" : "  NOT READY"), 18,
        45 + static_cast<int>(row) * 12, kReady ? 10 : 7);
  }
  if (kPageCount > 1) {
    font.DrawString(bmp, "PGUP/PGDN: MORE PLAYERS", 12, 143, 7);
  }
  font.DrawString(bmp, host_ ? "ENTER: START WHEN EVERYONE IS READY" : "ENTER: TOGGLE READY", 12,
                  160, 50);
  font.DrawString(bmp, "RANDOM WEAPONS EACH ROUND - ESC: LEAVE", 12, 176, 7);
}

void OnlineConnectState::DrawScores() {
  auto& font = gfx->common->font;
  auto& bmp = gfx->play_renderer.bmp;
  auto* controller = Controller();
  font.DrawString(bmp, "MATCH FINISHED - PGUP/PGDN: MORE", 12, 12, 50);
  auto const& worms = controller->game.worms;
  auto const kPages = std::max(size_t{1}, (worms.size() + 9) / 10);
  roster_page_ %= kPages;
  for (size_t row = 0; row < 10; ++row) {
    auto const kI = roster_page_ * 10 + row;
    if (kI >= worms.size()) {
      break;
    }
    auto const& worm = *worms[kI];
    font.DrawString(bmp,
                    std::to_string(kI + 1) + ". " + worm.settings->name + "  KILLS " +
                        std::to_string(worm.kills) + "  LIVES " + std::to_string(worm.lives),
                    12, 36 + static_cast<int>(row) * 12, 7);
  }
  font.DrawString(
      bmp, host_ ? "ENTER: BACK TO LOBBY FOR REMATCH" : "WAITING FOR HOST TO RETURN TO LOBBY", 12,
      166, 50);
  font.DrawString(bmp, "ESC: LEAVE ROOM", 12, 182, 7);
}

void OnlineConnectState::Draw() {
  gfx->SetSpectatorLayout(/*fixed=*/false);
  if (playing_ && error_.empty()) {
    Controller()->Draw(gfx->play_renderer, /*spectator=*/false);
    Controller()->Draw(gfx->single_screen_renderer, /*spectator=*/true);
  } else {
    gfx->play_renderer.pal = gfx->common->exepal;
    gfx->play_renderer.UpdatePal32();
    Fill(gfx->play_renderer.bmp, 0);
    if (!error_.empty()) {
      gfx->common->font.DrawString(gfx->play_renderer.bmp, error_, 8, 75, 50);
      gfx->common->font.DrawString(gfx->play_renderer.bmp, "ENTER / ESC: LEAVE ROOM", 8, 95, 7);
    } else if (scores_) {
      DrawScores();
    } else {
      DrawLobby();
    }
  }
}
