#include "sf/game/runtime_profile.hpp"
#include "sf/game/sf2_runtime.hpp"

namespace sf::game {

const GameRuntimeProfile &sf2RuntimeProfile() noexcept {
  static constexpr GameRuntimeProfile profile{
      .game = GameId::syphon_filter_2,
      .kind = GameRuntimeKind::sf2,
      .hud_atlas = HudAtlasKind::sf2,
      .emd_vertex_index_stride = 3U,
      .hmd_vertex_index_stride = 0U,
      .first_person_eye_height = 220.0,
      .player_collision_radius = 32.0,
      .player_collision_height = 260.0,
      .uses_legacy_guest_runtime = false,
      .supports_native_mission_items = true,
      .supports_native_mission_interactions = true,
      .uses_sf1_environment_atlas = false,
  };
  return profile;
}

std::optional<WeaponId> sf2WeaponForItem(std::uint8_t item) noexcept {
  switch (item) {
  case 1U:
    return WeaponId::silenced_9mm;
  case 2U:
    return WeaponId::pistol_9mm;
  case 3U:
    return WeaponId::pistol_45;
  case 4U:
    return WeaponId::m_16;
  case 5U:
  case 6U:
    return WeaponId::hk_5;
  case 7U:
    return WeaponId::pk_102;
  case 8U:
    return WeaponId::shotgun;
  case 9U:
    return WeaponId::combat_shotgun;
  case 10U:
    return WeaponId::g_18;
  case 11U:
    return WeaponId::biz_2;
  case 12U:
    return WeaponId::k3g4;
  case 14U:
  case 15U:
    return WeaponId::sniper_rifle;
  case 16U:
    return WeaponId::nightvision_rifle;
  case 18U:
  case 19U:
    return WeaponId::taser;
  case 20U:
    return WeaponId::knife;
  case 21U:
    return WeaponId::m_79;
  case 22U:
    return WeaponId::fragmentation_grenade;
  case 23U:
    return WeaponId::gas_grenade;
  case 24U:
    return WeaponId::c4_explosives;
  default:
    // H11, crossbow and non-weapon mission tools do not yet have a native
    // representation. Never alias them to similarly numbered SF1 slots.
    return std::nullopt;
  }
}

} // namespace sf::game
