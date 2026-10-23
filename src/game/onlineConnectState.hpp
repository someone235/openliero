#pragma once

#include "net/onlineRoom.hpp"
#include "net/session.hpp"
#include "state.hpp"

#include <map>
#include <memory>
#include <string>

struct OnlineController;

// Online lobby persists across matches so any number of guests can join before
// the host starts. Each machine controls one worm using its network profile.
struct OnlineConnectState : AppState {
  OnlineConnectState(NetSession::Role role, std::string room_code = "");
  void Enter() override;
  void Leave() override;
  void HandleEvent(SDL_Event& event) override;
  bool Update() override;
  void Draw() override;
  bool WantsMenuFlip() const override { return !playing_; }

 private:
  struct Player {
    std::shared_ptr<WormSettings> settings;
    bool ready = false;
  };
  void Receive(uint32_t peer, const std::vector<uint8_t>& packet);
  void SendProfile();
  void SendRoster();
  void StartMatch();
  void ReturnToLobby();
  void DrawLobby();
  void DrawScores();
  OnlineController* Controller() const;
  std::unique_ptr<struct Controller> previous_controller_;
  bool host_;
  std::string code_;
  std::unique_ptr<OnlineRoom> room_;
  std::map<uint32_t, Player> players_;
  std::vector<std::string> names_;
  std::vector<uint8_t> ready_;
  std::shared_ptr<Common> original_common_;
  std::shared_ptr<MemoryFs> tc_fs_;
  std::vector<uint8_t> tc_archive_;
  std::shared_ptr<WormSettings> profile_;
  std::string error_;
  bool local_ready_ = false;
  bool tc_ready_ = false;
  bool playing_ = false;
  bool scores_ = false;
  size_t roster_page_ = 0;
  uint32_t round_ = 0;
  uint64_t entered_ = 0;
};
