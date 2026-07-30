#pragma once

#include "sf/assets/tim_image.hpp"
#include "sf/game/runtime_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sf::platform::detail {

inline constexpr std::size_t texture_page_bytes = 64U * 256U * 2U;
inline constexpr unsigned int psx_texture_page_count = 32U;
// TPAGE bits 10..15 are host-only in this port. Code zero selects native PSX
// VRAM; codes 1..63 select the complete host alias atlas. Keeping every
// possible (logical page, texture bank) identity resident avoids order-based
// eviction of actor textures during room transitions.
inline constexpr unsigned int extended_texture_page_count = 63U;
inline constexpr unsigned int resident_texture_page_count =
    psx_texture_page_count + extended_texture_page_count;
// Keep the four-page native HUD atlas in host-only texture storage. Raw TIM
// uploads into PS1 framebuffer pages leaked weapon-source rectangles into the
// displayed scene (most visibly the shotgun at rows 109..180).
inline constexpr unsigned int hud_resident_texture_page_count = 4U;
inline constexpr unsigned int hud_resident_first_texture_page =
    resident_texture_page_count - hud_resident_texture_page_count;
inline constexpr unsigned int maximum_scene_texture_identities =
    psx_texture_page_count * 2U + 1U;
static_assert(resident_texture_page_count - 6U -
                  hud_resident_texture_page_count >=
              maximum_scene_texture_identities);
inline constexpr unsigned int resident_texture_page_token_bits = 7U;
static_assert((1U << resident_texture_page_token_bits) >=
              resident_texture_page_count);
inline constexpr std::size_t clut_bytes = 256U * 32U * 2U;
inline constexpr std::uint16_t mission_clut_source_x = 768U;
inline constexpr std::uint16_t mission_clut_source_y = 480U;
inline constexpr std::uint16_t mission_clut_resident_x = 0U;
inline constexpr std::uint16_t mission_clut_resident_y = 192U;
inline constexpr std::uint16_t hud_source_vram_x = 768U;
inline constexpr std::uint16_t hud_resident_vram_x = 0U;

// Retail missions may contain only VRAM.HOG. Bank one is a valid renderer
// selector only when VRAM1.HOG exists; for a single-bank mission it aliases
// the sole authored bank instead of becoming an out-of-range archive access.
[[nodiscard]] constexpr int
canonicalMissionTextureBank(int requested_bank,
                            std::size_t available_banks) noexcept {
  return available_banks == 1U && requested_bank == 1 ? 0 : requested_bank;
}

[[nodiscard]] unsigned int physicalTexturePage(unsigned int page) noexcept;
[[nodiscard]] constexpr std::uint16_t
encodeResidentTexturePage(std::uint16_t tpage,
                          unsigned int physical_page) noexcept {
  if (physical_page < psx_texture_page_count) {
    return static_cast<std::uint16_t>((tpage & 0x03e0U) | physical_page);
  }
  const auto extension = physical_page - psx_texture_page_count;
  if (extension >= extended_texture_page_count) {
    return tpage;
  }
  return static_cast<std::uint16_t>(
      (tpage & 0x03e0U) |
      static_cast<std::uint16_t>((extension + 1U) << 10U));
}
[[nodiscard]] constexpr std::uint64_t
residentTexturePageToken(std::uint64_t generation,
                         unsigned int physical_page) noexcept {
  return (generation << resident_texture_page_token_bits) | physical_page;
}
[[nodiscard]] std::uint32_t validateVlf(std::span<const std::byte> bytes);
[[nodiscard]] std::span<const std::byte>
vlfPage(std::span<const std::byte> bytes, std::uint32_t page_mask,
        unsigned int page);
[[nodiscard]] std::span<const std::byte>
vlfClut(std::span<const std::byte> bytes, std::uint32_t page_mask);
[[nodiscard]] bool
texturePageMatchesVlf(std::span<const std::byte> vlf,
                      std::uint32_t page_mask, unsigned int page,
                      std::span<const std::byte> texture_bank_page);

void uploadTexturePage(unsigned int page, std::span<const std::byte> bytes);
void uploadTexturePageAt(unsigned int physical_page,
                         std::span<const std::byte> bytes);
void readTexturePageAt(unsigned int physical_page,
                       std::span<std::uint16_t> words);
void uploadTexturePageBlockAt(unsigned int physical_page,
                              const assets::TimBlock &block,
                              unsigned int local_x, unsigned int local_y);
void copyTexturePageRectangle(std::span<const std::byte> source,
                              unsigned int source_x, unsigned int source_y,
                              std::span<std::byte> destination,
                              unsigned int destination_x,
                              unsigned int destination_y, unsigned int width,
                              unsigned int height,
                              std::span<std::byte> scratch);
[[nodiscard]] bool texturePageNeedsAuthoredReload(
    std::span<const std::byte> resident, std::span<const std::byte> authored,
    bool runtime_mutated) noexcept;
void uploadClut(std::span<const std::byte> bytes);

[[nodiscard]] int texturePageMode(assets::TimPixelMode mode) noexcept;
[[nodiscard]] unsigned int
timTexturePage(const assets::TimImage &image) noexcept;
void uploadTimBlockAt(const assets::TimBlock &block,
                      std::uint16_t destination_x, std::uint16_t destination_y);
void uploadTimBlock(const assets::TimBlock &block);

[[nodiscard]] constexpr std::uint16_t
hudResidentX(std::uint16_t source_x) noexcept {
  return static_cast<std::uint16_t>(hud_resident_vram_x + source_x -
                                    hud_source_vram_x);
}

struct HudResidentPlacement {
  std::uint16_t x{};
  std::uint16_t y{};

  constexpr bool operator==(const HudResidentPlacement &) const = default;
};

struct MissionClutResidentRow {
  bool valid{};
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};

  constexpr bool operator==(const MissionClutResidentRow &) const = default;
};

// Convert one row of a bounded retail ZCLUT upload to the corresponding
// framebuffer-safe native residency row. The caller supplies the live
// bank-specific row mapping selected by TextureStreamer.
[[nodiscard]] constexpr MissionClutResidentRow
missionClutResidentRow(std::uint16_t source_x, std::uint16_t source_y,
                       std::uint16_t width, std::uint16_t height,
                       std::uint16_t local_row,
                       unsigned int mapped_row) noexcept {
  const auto right = static_cast<std::uint32_t>(source_x) + width;
  const auto bottom = static_cast<std::uint32_t>(source_y) + height;
  if (width == 0U || height == 0U || local_row >= height ||
      source_x < mission_clut_source_x ||
      right > mission_clut_source_x + 256U ||
      source_y < mission_clut_source_y ||
      bottom > mission_clut_source_y + 32U || mapped_row >= 32U) {
    return {};
  }
  return {
      .valid = true,
      .x = static_cast<std::uint16_t>(
          mission_clut_resident_x + source_x - mission_clut_source_x),
      .y = static_cast<std::uint16_t>(
          mission_clut_resident_y + mapped_row),
      .width = width,
  };
}

// The sequels place several weapon/item icon layers in streamed lower VRAM
// pages or across the resident CLUT rows. Pack those exact authored
// rectangles into unused native framebuffer HUD-atlas space; every other
// INTERFACE TIM retains the SF1/SF2 x-only relocation.
[[nodiscard]] constexpr HudResidentPlacement
hudResidentPlacement(const assets::TimBlock &block) noexcept {
  if (block.x == 808U && block.y == 240U && block.width_words == 16U &&
      block.height == 16U) {
    // SF2 KNIFEB ends at source V=256. An FT4 exclusive far edge wraps that
    // byte to zero and stretches the icon across unrelated atlas rows.
    return {192U, 32U};
  }
  if (block.x == 884U && block.y == 238U && block.width_words == 9U &&
      block.height == 17U) {
    return {170U, 0U}; // SF2 KEYCARDB
  }
  if (block.x == 792U && block.y == 240U && block.width_words == 16U &&
      block.height == 16U) {
    return {144U, 0U}; // SF2 SNIPER1C
  }
  if (block.x == 866U && block.y == 232U && block.width_words == 16U &&
      block.height == 24U) {
    return {195U, 0U}; // SF2 SUPER1A
  }
  if (block.x == 811U && block.y == 456U && block.width_words == 5U &&
      block.height == 20U) {
    return {0U, 227U}; // GASGRENA
  }
  if (block.x == 769U && block.y == 384U && block.width_words == 15U &&
      block.height == 20U) {
    return {5U, 227U}; // SHOT2A
  }
  if (block.x == 1009U && block.y == 300U && block.width_words == 15U &&
      block.height == 20U) {
    return {20U, 227U}; // SHOT2C
  }
  if (block.x == 1000U && block.y == 300U && block.width_words == 5U &&
      block.height == 20U) {
    return {35U, 227U}; // SNIFFER
  }
  if (block.x == 876U && block.y == 230U && block.width_words == 11U &&
      block.height == 20U) {
    return {40U, 227U}; // G3A
  }
  if (block.x == 768U && block.y == 336U && block.width_words == 16U &&
      block.height == 16U) {
    return {80U, 231U}; // SNIPER1C
  }
  if (block.x == 816U && block.y == 232U && block.width_words == 15U &&
      block.height == 20U) {
    return {96U, 227U}; // SHOT2B
  }
  return {hudResidentX(block.x), block.y};
}

[[nodiscard]] constexpr bool hudResidentRelocationSupported(
    game::HudAtlasKind atlas, const assets::TimBlock &block) noexcept {
  switch (atlas) {
  case game::HudAtlasKind::sf1:
    return false;
  case game::HudAtlasKind::sf2:
    return (block.x == 808U && block.y == 240U &&
            block.width_words == 16U && block.height == 16U) ||
           (block.x == 884U && block.y == 238U &&
            block.width_words == 9U && block.height == 17U) ||
           (block.x == 792U && block.y == 240U &&
            block.width_words == 16U && block.height == 16U) ||
           (block.x == 866U && block.y == 232U &&
            block.width_words == 16U && block.height == 24U);
  case game::HudAtlasKind::sf3:
    return (block.x == 811U && block.y == 456U &&
            block.width_words == 5U && block.height == 20U) ||
           (block.x == 769U && block.y == 384U &&
            block.width_words == 15U && block.height == 20U) ||
           (block.x == 1009U && block.y == 300U &&
            block.width_words == 15U && block.height == 20U) ||
           (block.x == 1000U && block.y == 300U &&
            block.width_words == 5U && block.height == 20U) ||
           (block.x == 876U && block.y == 230U &&
            block.width_words == 11U && block.height == 20U) ||
           (block.x == 768U && block.y == 336U &&
            block.width_words == 16U && block.height == 16U) ||
           (block.x == 816U && block.y == 232U &&
            block.width_words == 15U && block.height == 20U);
  }
  return false;
}

void uploadHudPixels(const assets::TimBlock &block);

} // namespace sf::platform::detail
