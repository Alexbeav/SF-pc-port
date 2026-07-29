#include "sf/game/disc_info.hpp"

#include "sf/core/error.hpp"
#include "sf/game/game_disc.hpp"

#include <limits>
#include <utility>

namespace sf::game {
namespace {

std::uint16_t readLe16(std::span<const std::byte> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint16_t)) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Truncated disc-info selection index"};
  }
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(bytes[offset]) |
      (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

} // namespace

std::vector<DiscSelectionTitle>
parseDiscSelectionTitles(std::span<const std::byte> bytes) {
  constexpr std::size_t selection_table_offset = 8U;
  constexpr std::size_t maximum_title_size = 96U;
  if (bytes.size() <= selection_table_offset + sizeof(std::uint16_t)) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Disc-info selection table is truncated"};
  }

  std::vector<DiscSelectionTitle> result;
  auto offset = selection_table_offset;
  for (std::uint32_t expected = 0U;
       expected <= std::numeric_limits<std::uint16_t>::max(); ++expected) {
    const auto index = readLe16(bytes, offset);
    if (index != expected) {
      break;
    }
    offset += sizeof(std::uint16_t);

    std::string title;
    title.reserve(32U);
    auto terminated = false;
    while (offset < bytes.size() && title.size() < maximum_title_size) {
      const auto value = std::to_integer<unsigned char>(bytes[offset++]);
      if (value == 0U) {
        terminated = true;
        break;
      }
      if (value < 0x20U || value > 0x7eU) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Disc-info selection title is not ASCII"};
      }
      title.push_back(static_cast<char>(value));
    }
    if (title.empty() || !terminated) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Disc-info selection title is truncated"};
    }
    result.push_back(DiscSelectionTitle{static_cast<std::uint16_t>(expected),
                                        std::move(title)});
  }
  if (result.empty()) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Disc-info contains no selection titles"};
  }
  return result;
}

std::vector<DiscSelectionTitle> loadDiscSelectionTitles(GameDisc &disc) {
  if (!disc.game() || disc.game()->layout.mission_info_path.empty()) {
    throw core::Error{core::ErrorCode::not_found,
                      "This game disc has no external selection catalog"};
  }
  return parseDiscSelectionTitles(disc.image().readFile(
      std::string{disc.game()->layout.mission_info_path}));
}

} // namespace sf::game
