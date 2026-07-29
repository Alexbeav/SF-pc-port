#include "sf/core/error.hpp"
#include "sf/game/chase_camera.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/gameplay.hpp"
#include "sf/game/mission.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>

namespace sf::game {

class Sf2NativeMissionProbeAccess {
public:
  static bool placePlayerBehind(GameplaySession &gameplay,
                                std::uint16_t scene) noexcept {
    if (scene >= gameplay.npc_states_.size()) {
      return false;
    }
    const auto &target = gameplay.npc_states_[scene];
    const auto forward = headingDirection(target.yaw);
    auto player = PlayerState{
        target.x - forward.x * 180.0,
        target.y,
        target.z - forward.z * 180.0,
        0,
        true,
    };
    player.yaw =
        headingFromDirection(target.x - player.x, target.z - player.z);
    gameplay.player_controller_.reset(player);
    gameplay.camera_collision_initialized_ = false;
    return true;
  }

  static bool placePlayerAtSource(GameplaySession &gameplay,
                                  std::size_t source) noexcept {
    const auto &objects = gameplay.mission_.objects().objects();
    if (source >= objects.size()) {
      return false;
    }
    const auto &transform = objects[source].transform;
    gameplay.player_controller_.reset(PlayerState{
        static_cast<double>(transform.x),
        -static_cast<double>(transform.y),
        static_cast<double>(transform.z),
        0,
        true,
    });
    const auto rooms =
        gameplay.mission_.objects().roomsContainingObject(source);
    if (!rooms.empty()) {
      gameplay.current_room_ = rooms.front();
      gameplay.rebuildActiveModels();
    }
    gameplay.camera_collision_initialized_ = false;
    return true;
  }

  static bool sourceHidden(const GameplaySession &gameplay,
                           std::size_t source) noexcept {
    if (source >= gameplay.source_to_scene_object_.size()) {
      return false;
    }
    const auto scene = gameplay.source_to_scene_object_[source];
    return scene < gameplay.object_script_hidden_.size() &&
           gameplay.object_script_hidden_[scene];
  }

  static bool cameraSegmentClear(const GameplaySession &gameplay) noexcept {
    const auto camera = gameplay.camera();
    return gameplay.traceWorldSegment(
               camera.target_x, camera.target_y, camera.target_z, camera.x,
               camera.y, camera.z) >=
           0.999;
  }

  static bool damageActor(GameplaySession &gameplay, std::uint16_t scene,
                          WeaponDamageKind kind) noexcept {
    if (scene >= gameplay.npc_states_.size()) {
      return false;
    }
    // AIRBASE's opening guards are correctly dormant until their authored
    // script activation. This helper deliberately activates the selected
    // actor so the independent damage/parameter contract remains probeable.
    gameplay.npc_states_[scene].active = true;
    gameplay.damageNpc(scene, gameplay.npc_states_[scene].health, kind);
    return true;
  }
};

} // namespace sf::game

namespace {

std::pair<std::uint16_t, const sf::game::NpcState *>
actorBySource(const sf::game::GameplaySession &gameplay,
              std::uint16_t source) noexcept {
  for (std::size_t scene = 0U; scene < gameplay.objects().size(); ++scene) {
    if (gameplay.objects()[scene].source_index == source &&
        scene <= 0xffffU) {
      return {static_cast<std::uint16_t>(scene),
              gameplay.npcState(static_cast<std::uint16_t>(scene))};
    }
  }
  return {std::uint16_t{0U}, nullptr};
}

bool probeAirbase(sf::game::GameDisc &disc) {
  const auto mission = sf::game::MissionPackage::load(disc, 1U);
  auto gameplay = sf::game::GameplaySession{mission};
  const auto [first_guard_scene, first_guard] = actorBySource(gameplay, 84U);
  const auto [second_guard_scene, second_guard] =
      actorBySource(gameplay, 85U);
  const auto [straggler_scene, straggler] = actorBySource(gameplay, 86U);
  static_cast<void>(first_guard_scene);
  static_cast<void>(second_guard_scene);
  static_cast<void>(straggler_scene);
  const auto delayed_guards =
      first_guard == nullptr && second_guard == nullptr &&
      straggler == nullptr &&
      first_guard_scene < gameplay.objects().size() &&
      second_guard_scene < gameplay.objects().size() &&
      straggler_scene < gameplay.objects().size() &&
      gameplay.objects()[first_guard_scene].source_index == 84U &&
      gameplay.objects()[second_guard_scene].source_index == 85U &&
      gameplay.objects()[straggler_scene].source_index == 86U;
  const auto initial_state =
      gameplay.missionObjectiveCount() == 3U &&
      gameplay.missionParameterCount() == 3U &&
      gameplay.revealedObjectiveMask() == 0x7U &&
      gameplay.missionParameterMask() == 0x7U &&
      gameplay.hud().vitals().health == 15U &&
      gameplay.hud().vitals().maximum_health == 150U &&
      gameplay.nativeMissionTimerSeconds() ==
          std::optional<unsigned int>{120U} &&
      delayed_guards;

  auto positioned = sf::game::Sf2NativeMissionProbeAccess::
      placePlayerAtSource(gameplay, 103U);
  gameplay.update(sf::game::GameplayInput{});
  const auto adrenaline_complete =
      !gameplay.nativeMissionTimerSeconds() &&
      (gameplay.completedObjectiveMask() & 0x4U) != 0U;

  for (const auto source :
       {std::size_t{97U}, std::size_t{98U}, std::size_t{99U},
        std::size_t{100U}}) {
    positioned =
        sf::game::Sf2NativeMissionProbeAccess::placePlayerAtSource(gameplay,
                                                                   source) &&
        positioned;
    gameplay.update(sf::game::GameplayInput{});
  }
  const auto gear_complete =
      (gameplay.completedObjectiveMask() & 0x2U) != 0U;
  positioned =
      sf::game::Sf2NativeMissionProbeAccess::placePlayerAtSource(gameplay,
                                                                 105U) &&
      positioned;
  gameplay.update(sf::game::GameplayInput{.interact = true});
  const auto security_doors_open =
      sf::game::Sf2NativeMissionProbeAccess::sourceHidden(gameplay, 31U) &&
      sf::game::Sf2NativeMissionProbeAccess::sourceHidden(gameplay, 32U);
  positioned =
      sf::game::Sf2NativeMissionProbeAccess::placePlayerAtSource(gameplay,
                                                                 24U) &&
      positioned;
  gameplay.update(sf::game::GameplayInput{});
  const auto escape_complete =
      gameplay.missionComplete() &&
      (gameplay.completedObjectiveMask() & 0x1U) != 0U;

  auto electrical_gameplay = sf::game::GameplaySession{mission};
  const auto [electrical_scene, electrical_actor] =
      actorBySource(electrical_gameplay, 84U);
  static_cast<void>(electrical_actor);
  const auto electrical_applied =
      electrical_scene < electrical_gameplay.objects().size() &&
      electrical_gameplay.objects()[electrical_scene].source_index == 84U &&
      sf::game::Sf2NativeMissionProbeAccess::damageActor(
          electrical_gameplay, electrical_scene,
          sf::game::WeaponDamageKind::electrical);
  const auto electrical_nonlethal =
      electrical_applied &&
      (electrical_gameplay.failedParameterMask() & 0x2U) == 0U &&
      !electrical_gameplay.missionFailed();

  auto lethal_gameplay = sf::game::GameplaySession{mission};
  const auto [lethal_scene, lethal_actor] =
      actorBySource(lethal_gameplay, 84U);
  static_cast<void>(lethal_actor);
  const auto lethal_applied =
      lethal_scene < lethal_gameplay.objects().size() &&
      lethal_gameplay.objects()[lethal_scene].source_index == 84U &&
      sf::game::Sf2NativeMissionProbeAccess::damageActor(
          lethal_gameplay, lethal_scene,
          sf::game::WeaponDamageKind::ballistic);
  const auto lethal_parameter_failed =
      lethal_applied &&
      (lethal_gameplay.failedParameterMask() & 0x2U) != 0U &&
      lethal_gameplay.missionFailed();

  auto timeout_gameplay = sf::game::GameplaySession{mission};
  for (auto update = 0U; update < 120U * sf::game::npc_updates_per_second;
       ++update) {
    timeout_gameplay.update(sf::game::GameplayInput{});
  }
  const auto timer_parameter_failed =
      (timeout_gameplay.failedParameterMask() & 0x4U) != 0U &&
      timeout_gameplay.missionFailed();
  std::cout << "mission=2 source-index=1"
            << " initial-state=" << initial_state
            << " delayed-guards=" << delayed_guards
            << " adrenaline-complete=" << adrenaline_complete
            << " gear-complete=" << gear_complete
            << " security-doors-open=" << security_doors_open
            << " escape-complete=" << escape_complete
            << " electrical-nonlethal=" << electrical_nonlethal
            << " lethal-parameter-failed=" << lethal_parameter_failed
            << " timer-parameter-failed=" << timer_parameter_failed << '\n';
  return positioned && initial_state && adrenaline_complete &&
         gear_complete && security_doors_open && escape_complete &&
         electrical_nonlethal && lethal_parameter_failed &&
         timer_parameter_failed;
}

bool probeHighway(sf::game::GameDisc &disc) {
  const auto mission = sf::game::MissionPackage::load(disc, 2U);
  auto gameplay = sf::game::GameplaySession{mission};
  constexpr std::array diversion_sources{std::uint16_t{278U},
                                         std::uint16_t{279U},
                                         std::uint16_t{280U}};
  const auto [chance_scene, chance_before] = actorBySource(gameplay, 45U);
  const auto [guard_scene, guard_before] = actorBySource(gameplay, 197U);
  static_cast<void>(chance_scene);
  if (chance_before == nullptr || guard_before == nullptr) {
    std::cerr << "missing authored HWAY opening actor\n";
    return false;
  }

  const auto player = gameplay.player();
  const auto guard_forward = sf::game::headingDirection(guard_before->yaw);
  const auto guard_to_player_x = player.x - guard_before->x;
  const auto guard_to_player_z = player.z - guard_before->z;
  const auto player_behind_guard =
      guard_forward.x * guard_to_player_x +
          guard_forward.z * guard_to_player_z <
      0.0;
  const auto chance_initial_health = chance_before->health;
  auto diversion_configured = true;
  auto diversion_approached_chance = true;
  std::array<double, diversion_sources.size()> initial_chance_distances{};
  auto diversion_index = std::size_t{};
  for (const auto source : diversion_sources) {
    const auto [scene, actor] = actorBySource(gameplay, source);
    static_cast<void>(scene);
    diversion_configured =
        diversion_configured && actor != nullptr &&
        actor->scripted_target_source ==
            std::optional<std::uint16_t>{std::uint16_t{45U}};
    initial_chance_distances[diversion_index++] =
        actor == nullptr
            ? 0.0
            : std::hypot(actor->x - chance_before->x,
                         actor->z - chance_before->z);
  }

  for (auto update = 0U; update < 300U; ++update) {
    gameplay.update(sf::game::GameplayInput{});
    gameplay.advanceAnimationClock();
  }

  const auto *chance_after = actorBySource(gameplay, 45U).second;
  if (chance_after == nullptr) {
    std::cerr << "Chance disappeared during deterministic opening probe\n";
    return false;
  }
  const auto chance_damaged = chance_after->health < chance_initial_health;
  const auto chance_engaged =
      chance_after->behavior != sf::game::NpcBehavior::idle &&
      chance_after->behavior != sf::game::NpcBehavior::patrol;
  const auto camera_segment_clear =
      sf::game::Sf2NativeMissionProbeAccess::cameraSegmentClear(gameplay);
  std::cout << "mission=3 source-index=2"
            << " player-behind-guard=" << player_behind_guard
            << " guard-source=197"
            << " guard-weapon="
            << static_cast<unsigned int>(guard_before->weapon)
            << " diversion-configured=" << diversion_configured
            << " chance-health-before=" << chance_initial_health
            << " chance-health-after=" << chance_after->health
            << " chance-behavior="
            << static_cast<unsigned int>(chance_after->behavior)
            << " chance-damaged=" << chance_damaged
            << " camera-segment-clear=" << camera_segment_clear << '\n';
  diversion_index = 0U;
  auto diversion_took_fire = false;
  for (const auto source : diversion_sources) {
    const auto *actor = actorBySource(gameplay, source).second;
    if (actor != nullptr) {
      const auto final_distance =
          std::hypot(actor->x - chance_after->x,
                     actor->z - chance_after->z);
      diversion_approached_chance =
          diversion_approached_chance &&
          final_distance < initial_chance_distances[diversion_index];
      diversion_took_fire =
          diversion_took_fire || actor->health < actor->maximum_health;
      std::cout << "source=" << source << " active=" << actor->active
                << " health=" << actor->health
                << " behavior=" << static_cast<unsigned int>(actor->behavior)
                << " position=" << actor->x << ',' << actor->y << ','
                << actor->z
                << " chance-distance-before="
                << initial_chance_distances[diversion_index]
                << " chance-distance-after=" << final_distance << '\n';
    }
    ++diversion_index;
  }
  const auto placed_for_takedown =
      sf::game::Sf2NativeMissionProbeAccess::placePlayerBehind(gameplay,
                                                               guard_scene);
  const auto guard_magazine_before = guard_before->magazine;
  const auto guard_reserve_before = guard_before->reserve_ammo;
  gameplay.update(sf::game::GameplayInput{.fire_pressed = true});
  const auto *guard_after = actorBySource(gameplay, 197U).second;
  const auto takedown_lethal =
      guard_after != nullptr && guard_after->health == 0U;
  const auto drops = gameplay.nativeDroppedItems();
  const auto m16_dropped =
      drops.size() == 1U &&
      drops.front().item ==
          static_cast<std::uint16_t>(sf::game::WeaponId::m_16);
  const auto drop_quantity_preserved =
      m16_dropped && drops.front().quantity_valid &&
      drops.front().magazine == guard_magazine_before &&
      drops.front().reserve == guard_reserve_before;
  std::cout << "placed-for-takedown=" << placed_for_takedown
            << " takedown-lethal=" << takedown_lethal
            << " dropped-items=" << drops.size()
            << " m16-dropped=" << m16_dropped
            << " quantity-preserved=" << drop_quantity_preserved << '\n';
  gameplay.update(sf::game::GameplayInput{});
  const auto &collected_m16 =
      gameplay.hud().inventory().state(sf::game::WeaponId::m_16);
  const auto m16_collected =
      gameplay.nativeDroppedItems().empty() &&
      gameplay.hud().inventory().current() == sf::game::WeaponId::m_16 &&
      collected_m16.magazine == guard_magazine_before &&
      collected_m16.reserve == guard_reserve_before;
  std::cout << "m16-collected=" << m16_collected << '\n';
  return player_behind_guard &&
         guard_before->weapon == sf::game::WeaponId::m_16 &&
         diversion_configured && diversion_approached_chance &&
         chance_engaged && diversion_took_fire && camera_segment_clear &&
         placed_for_takedown && takedown_lethal && m16_dropped &&
         drop_quantity_preserved && m16_collected;
}

int run(const std::filesystem::path &cue) {
  auto disc = sf::game::GameDisc::open(cue);
  const auto airbase = probeAirbase(disc);
  const auto highway = probeHighway(disc);
  return airbase && highway ? 0 : 4;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: sf_sf2_native_mission_probe <sf2-disc1.cue>\n";
    return 1;
  }
  try {
    return run(std::filesystem::path{argv[1]});
  } catch (const std::exception &error) {
    std::cerr << "SF2 native mission probe failed: " << error.what() << '\n';
    return 10;
  }
}
