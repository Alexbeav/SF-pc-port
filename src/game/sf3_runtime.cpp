#include "sf/game/runtime_profile.hpp"

namespace sf::game {

const GameRuntimeProfile &sf3RuntimeProfile() noexcept {
  static constexpr GameRuntimeProfile profile{
      .game = GameId::syphon_filter_3,
      .kind = GameRuntimeKind::sf3,
      .hud_atlas = HudAtlasKind::sf3,
      .emd_vertex_index_stride = 2U,
      .hmd_vertex_index_stride = 8U,
      .first_person_eye_height = 220.0,
      .player_collision_radius = 32.0,
      .player_collision_height = 260.0,
      .uses_legacy_guest_runtime = false,
      // SF3 must opt into its own translated mission runtime. It no longer
      // inherits SF2 pickup/interaction scaffolding merely by being a sequel.
      .supports_native_mission_items = false,
      .supports_native_mission_interactions = false,
      .uses_sf1_environment_atlas = false,
  };
  return profile;
}

} // namespace sf::game
