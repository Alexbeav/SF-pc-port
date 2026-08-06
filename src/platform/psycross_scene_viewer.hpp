#pragma once

#include "sf/game/campaign.hpp"
#include "sf/game/pause_menu.hpp"
#include "sf/platform/host.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

struct PADRAW;

namespace sf::game {
class GameplaySession;
class MissionPackage;
class Sf2GuestMissionRuntime;
struct Sf2PresentationFrame;
} // namespace sf::game

namespace sf::platform::detail {

class PsyCrossAudioOutput;

// Retail SF2 ordering-table presenter shared by state-8 mission briefing and
// gameplay. The preparation call restores direct retail page identities;
// gameplay's TextureStreamer installs its own residency map afterward.
void prepareSf2GuestRetailPresentation() noexcept;
bool beginSf2GuestFrame(const game::Sf2PresentationFrame &frame,
                        bool clear_published_page);
void drawSf2GuestFrame(const game::Sf2PresentationFrame &frame,
                       unsigned int texture_bank,
                       bool retail_briefing_loaded = true);

enum class SceneExitReason {
  exit_application,
  return_to_title,
  save_and_return_to_title,
  restart_mission,
  mission_complete,
  mission_selected,
};

enum class CampaignSavePurpose {
  mission_complete,
  save_and_quit,
};

struct SceneViewerResult {
  std::uint16_t previous_buttons{0xffffU};
  SceneExitReason reason{SceneExitReason::exit_application};
  std::optional<std::uint32_t> selected_mission;
  std::optional<game::CampaignCarryState> carry;
};

// Uses the same retail INTRFACE font page and ACD primitive path as the
// in-game pause screen. Keeping mission-complete UI on this renderer avoids
// PsyCross's debug-font VRAM page, which gameplay legitimately overwrites.
class PsyCrossCampaignSaveRenderer final {
public:
  PsyCrossCampaignSaveRenderer(const game::MissionPackage &mission,
                               KeyboardMouseBindings input,
                               CampaignSavePurpose purpose);
  ~PsyCrossCampaignSaveRenderer();

  PsyCrossCampaignSaveRenderer(const PsyCrossCampaignSaveRenderer &) = delete;
  PsyCrossCampaignSaveRenderer &
  operator=(const PsyCrossCampaignSaveRenderer &) = delete;

  void draw(const game::CampaignSaveMenu &menu,
            const game::TitleSaveSlots &slots);
  void drawLoadSlots(const game::TitleSaveSlots &slots, std::size_t selection);

private:
  struct State;
  std::unique_ptr<State> state_;
};

class PsyCrossSceneViewer final {
public:
  PsyCrossSceneViewer(KeyboardMouseBindings input,
                      game::RetailCheatState &cheats) noexcept
      : input_(input), cheats_(cheats) {}

  [[nodiscard]] SceneViewerResult
  run(const game::MissionPackage &mission, PADRAW &pad,
      std::uint16_t previous_buttons, const std::filesystem::path &cue_path,
      std::uint32_t maximum_unlocked_mission,
      std::unique_ptr<game::GameplaySession> preloaded_gameplay = {},
      std::unique_ptr<PsyCrossAudioOutput> preloaded_audio = {},
      std::unique_ptr<game::Sf2GuestMissionRuntime> preloaded_sf2_runtime = {},
      std::optional<game::CampaignCarryState> campaign_carry = std::nullopt);

private:
  KeyboardMouseBindings input_;
  game::RetailCheatState &cheats_;
  game::PauseSettings pause_settings_;
  bool pause_settings_initialized_{};
};

} // namespace sf::platform::detail
