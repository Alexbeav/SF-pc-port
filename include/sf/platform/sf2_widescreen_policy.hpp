#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace sf::platform {

struct Sf2PrimitiveBounds {
  std::int16_t minimum_x{};
  std::int16_t minimum_y{};
  std::int16_t maximum_x{};
  std::int16_t maximum_y{};
};

template <std::size_t Size>
[[nodiscard]] constexpr Sf2PrimitiveBounds sf2PrimitiveBounds(
    const std::array<std::int16_t, Size> &x,
    const std::array<std::int16_t, Size> &y) noexcept {
  static_assert(Size != 0U);
  return {
      .minimum_x = *std::ranges::min_element(x),
      .minimum_y = *std::ranges::min_element(y),
      .maximum_x = *std::ranges::max_element(x),
      .maximum_y = *std::ranges::max_element(y),
  };
}

// SF2 authors fixed interface coordinates around a 384x240 centre-origin
// display. A primitive which owns both horizontal edges is a framebuffer
// effect/backdrop rather than an ordinary HUD element. Expand that authored
// ownership to the adaptive host width without recognizing mission state,
// packet addresses, colors, texture names, or individual effects.
[[nodiscard]] constexpr bool sf2AuxiliaryPrimitiveOwnsFullWidth(
    Sf2PrimitiveBounds bounds) noexcept {
  constexpr auto half_width = std::int16_t{192};
  return bounds.minimum_x <= -half_width &&
         bounds.maximum_x >= half_width;
}

// Cinematic mattes can begin narrower than the complete authored width while
// their vertical edge animates. Only flat black quads use this supplementary
// rule; general colored/textured effects must prove full-width ownership.
[[nodiscard]] constexpr bool sf2AnimatedCinematicMatteOwnsFullWidth(
    Sf2PrimitiveBounds bounds, std::uint32_t rgb) noexcept {
  constexpr auto half_height = std::int16_t{120};
  return (rgb & 0x00ffffffU) == 0U &&
         (bounds.minimum_y <= -half_height ||
          bounds.maximum_y >= half_height);
}

} // namespace sf::platform
