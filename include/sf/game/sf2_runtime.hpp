#pragma once

#include "sf/game/hud.hpp"

#include <cstdint>
#include <optional>

namespace sf::game {

// SF2 executable item IDs are not a generic sequel ABI. SF3 must supply its
// own translation before native actors or pickups can use it.
[[nodiscard]] std::optional<WeaponId>
sf2WeaponForItem(std::uint8_t item) noexcept;

[[nodiscard]] constexpr bool
sf2ActorInitiallyDormant(std::uint32_t mission_index,
                         std::uint16_t source_index) noexcept {
  return mission_index == 1U &&
         (source_index == 84U || source_index == 85U ||
          source_index == 86U);
}

} // namespace sf::game
