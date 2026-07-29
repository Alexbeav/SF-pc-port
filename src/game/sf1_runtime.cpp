#include "sf/game/runtime_profile.hpp"

namespace sf::game {

const GameRuntimeProfile &sf1RuntimeProfile() noexcept {
  static constexpr GameRuntimeProfile profile{
      .game = GameId::syphon_filter,
      .kind = GameRuntimeKind::sf1,
      .hud_atlas = HudAtlasKind::sf1,
      .emd_vertex_index_stride = 3U,
      .hmd_vertex_index_stride = 0U,
      .first_person_eye_height = 50.0,
      .player_collision_radius = 58.0,
      .player_collision_height = 390.0,
      .uses_legacy_guest_runtime = true,
      .supports_native_mission_items = false,
      .supports_native_mission_interactions = false,
      .uses_sf1_environment_atlas = true,
  };
  return profile;
}

} // namespace sf::game
