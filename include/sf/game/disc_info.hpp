#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf::game {

class GameDisc;

struct DiscSelectionTitle {
  std::uint16_t index{};
  std::string title;
};

// SF2 stores its single-player and multiplayer selection labels at the front
// of DISK1/2.INF. The rest of the file is a separate common-data payload and
// intentionally remains opaque at this boundary.
[[nodiscard]] std::vector<DiscSelectionTitle>
parseDiscSelectionTitles(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<DiscSelectionTitle>
loadDiscSelectionTitles(GameDisc &disc);

} // namespace sf::game
