#include "sf/game/disc_cdrom_media.hpp"

namespace sf::game {

bool DiscCdRomMedia::readDataSector(
    std::uint32_t lba, std::span<std::byte, sector_size> destination) noexcept {
  return image_.copyDataSector(lba, destination);
}

bool DiscCdRomMedia::readRawSector(
    std::uint32_t lba,
    std::span<std::byte, raw_sector_size> destination) noexcept {
  if (image_.hasRawSectors()) {
    return image_.copyRawSector(lba, destination);
  }
  return psx::CdRomMedia::readRawSector(lba, destination);
}

} // namespace sf::game
