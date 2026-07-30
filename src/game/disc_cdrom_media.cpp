#include "sf/game/disc_cdrom_media.hpp"

namespace sf::game {

std::uint32_t DiscCdRomMedia::mappedLba(std::uint32_t lba) const noexcept {
  return relative_extent_sector_count_ != 0U &&
          lba < relative_extent_sector_count_
      ? relative_extent_base_ + lba
      : lba;
}

bool DiscCdRomMedia::readDataSector(
    std::uint32_t lba, std::span<std::byte, sector_size> destination) noexcept {
  return image_.copyDataSector(mappedLba(lba), destination);
}

bool DiscCdRomMedia::readRawSector(
    std::uint32_t lba,
    std::span<std::byte, raw_sector_size> destination) noexcept {
  const auto mapped = mappedLba(lba);
  const auto read = image_.hasRawSectors()
                        ? image_.copyRawSector(mapped, destination)
                        : psx::CdRomMedia::readRawSector(mapped, destination);
  if (!read) {
    return false;
  }
  if (mapped != lba) {
    // The mission file manager addresses the mounted FOG extent from LBA 0.
    // Its raw-sector validation compares the returned MSF header against that
    // relative address, while the CUE naturally contains the parent disc's
    // absolute MSF. Preserve the exact sector payload and expose the header
    // that a separately mounted extent would carry.
    constexpr std::uint32_t pregap = 150U;
    constexpr std::uint32_t sectors_per_second = 75U;
    const auto absolute = lba + pregap;
    const auto bcd = [](std::uint32_t value) {
      return static_cast<std::byte>(((value / 10U) << 4U) |
                                    (value % 10U));
    };
    destination[12U] =
        bcd(absolute / (60U * sectors_per_second));
    destination[13U] =
        bcd((absolute / sectors_per_second) % 60U);
    destination[14U] = bcd(absolute % sectors_per_second);
  }
  return true;
}

} // namespace sf::game
