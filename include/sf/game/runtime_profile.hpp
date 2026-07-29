#pragma once

#include "sf/game/supported_games.hpp"

#include <cstdint>

namespace sf::game {

enum class GameRuntimeKind : std::uint8_t {
  sf1,
  sf2,
  sf3,
};

enum class HudAtlasKind : std::uint8_t {
  sf1,
  sf2,
  sf3,
};

// Explicit ownership boundary for behavior that differs between retail
// games. A game must opt into a shared facility here; "not SF1" is never a
// sufficient compatibility claim.
struct GameRuntimeProfile {
  GameId game;
  GameRuntimeKind kind;
  HudAtlasKind hud_atlas;
  std::uint8_t emd_vertex_index_stride;
  std::uint16_t hmd_vertex_index_stride;
  double first_person_eye_height;
  double player_collision_radius;
  double player_collision_height;
  bool uses_legacy_guest_runtime;
  bool supports_native_mission_items;
  bool supports_native_mission_interactions;
  bool uses_sf1_environment_atlas;
};

[[nodiscard]] const GameRuntimeProfile &sf1RuntimeProfile() noexcept;
[[nodiscard]] const GameRuntimeProfile &sf2RuntimeProfile() noexcept;
[[nodiscard]] const GameRuntimeProfile &sf3RuntimeProfile() noexcept;
[[nodiscard]] const GameRuntimeProfile &runtimeProfile(GameId game);

} // namespace sf::game
