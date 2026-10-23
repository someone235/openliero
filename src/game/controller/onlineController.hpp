#pragma once

#include "controller.hpp"
#include "game.hpp"

#include <functional>
#include <map>
#include <memory>
#include <vector>

// Host-coordinated lockstep. Frames advance only with every player's input;
// player slots and input queues are dynamically sized. The existing two-player
// rollback controller remains responsible for direct-connect matches.
class OnlineController : public Controller {
 public:
  using Send = std::function<void(uint32_t, const std::vector<uint8_t>&)>;
  OnlineController(const std::shared_ptr<Common>& common, const std::shared_ptr<Settings>& settings,
                   const std::shared_ptr<SoundPlayer>& sound, bool host, uint32_t round, Send send);
  ~OnlineController() override;
  void Prepare(const std::vector<std::shared_ptr<WormSettings>>& players,
               const std::vector<uint32_t>& peers, uint32_t seed);
  void Load(const std::vector<uint8_t>& snapshot, size_t local_index, uint32_t delay,
            int blood_particles = -1);
  std::vector<uint8_t> Snapshot() const;
  void Receive(uint32_t peer, const std::vector<uint8_t>& packet);
  void Finish();
  bool Finished() const { return finished_; }
  bool Paused() const { return paused_; }
  bool Desynced() const { return desynced_; }
  uint32_t Frame() const { return frame_; }
  uint32_t InputDelay() const { return delay_; }
  size_t LocalIndex() const { return local_index_; }
  void SetLocalInput(uint8_t input) { controls_.Unpack(input); }
  void OnKey(int key, bool state) override;
  void Focus() override;
  void Unfocus() override;
  bool Process() override;
  void Draw(Renderer& renderer, bool spectator) override;
  bool Running() override { return !finished_; }
  void MarkUnresumable() override { finished_ = true; }
  Level* CurrentLevel() override { return &game.level; }
  Game* CurrentGame() override { return &game; }
  void SwapLevel(Level& level) override;
  Game game;

 private:
  void InitializeInputs();
  void Broadcast(const std::vector<uint8_t>& packet);
  void Step(const std::vector<uint8_t>& inputs);
  void UpdatePaused();
  bool show_scores_ = false;
  size_t score_page_ = 0;
  bool host_;
  uint32_t round_;
  Send send_;
  std::vector<uint32_t> peers_;
  size_t local_index_ = 0;
  uint32_t frame_ = 0;
  uint32_t delay_ = 3;
  uint32_t next_input_ = 0;
  std::vector<std::map<uint32_t, uint8_t>> inputs_;
  struct Tick {
    std::vector<uint8_t> inputs;
    uint32_t checksum;
  };
  std::map<uint32_t, Tick> ticks_;
  Worm::ControlState controls_;
  Worm::ControlState clean_controls_;
  std::vector<bool> player_paused_;
  bool local_paused_ = false;
  bool paused_ = false;
  bool finished_ = false;
  bool desynced_ = false;
};
