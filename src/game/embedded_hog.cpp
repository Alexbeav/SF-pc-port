#include "sf/game/embedded_hog.hpp"

#include "sf/core/error.hpp"
#include "sf/psx/executable.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sf::game {
namespace {

std::uint32_t readLe32(std::span<const std::byte> bytes,
                       std::size_t offset) noexcept {
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::optional<std::size_t>
findEmbeddedHog(std::span<const std::byte> executable,
                std::string_view first_entry_name) {
  constexpr std::size_t fixed_header_size = 20U;
  for (std::size_t base = 0; base + fixed_header_size <= executable.size();
       base += 4U) {
    const auto view = executable.subspan(base);
    const auto count = readLe32(view, 4U);
    const auto names_offset = static_cast<std::size_t>(readLe32(view, 12U));
    const auto data_offset = static_cast<std::size_t>(readLe32(view, 16U));
    if (count == 0U || count > 4096U ||
        fixed_header_size + static_cast<std::size_t>(count) * 4U >
            names_offset ||
        names_offset >= data_offset || data_offset > view.size() ||
        first_entry_name.size() + 1U > data_offset - names_offset) {
      continue;
    }
    const auto first_name = view.subspan(names_offset, first_entry_name.size());
    if (std::equal(first_name.begin(), first_name.end(),
                   reinterpret_cast<const std::byte *>(
                       first_entry_name.data())) &&
        view[names_offset + first_entry_name.size()] == std::byte{0}) {
      return base;
    }
  }
  return std::nullopt;
}

} // namespace

assets::HogArchive
parseEmbeddedHog(const psx::Executable &executable,
                 std::string_view first_entry_name,
                 std::string_view following_archive_first_entry) {
  const auto text = executable.text();
  const auto begin = findEmbeddedHog(text, first_entry_name);
  const auto end = following_archive_first_entry.empty()
                       ? std::optional<std::size_t>{text.size()}
                       : findEmbeddedHog(text, following_archive_first_entry);
  if (!begin || !end || *begin >= *end) {
    throw core::Error{
        core::ErrorCode::invalid_format,
        "Executable resident HOG was not found: " +
            std::string{first_entry_name},
    };
  }
  return assets::HogArchive::parse(std::vector<std::byte>{
      text.begin() + static_cast<std::ptrdiff_t>(*begin),
      text.begin() + static_cast<std::ptrdiff_t>(*end),
  });
}

} // namespace sf::game
