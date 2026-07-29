#pragma once

#include "sf/disc/iso9660.hpp"
#include "sf/psx/cdrom.hpp"

namespace sf::game {

// Non-owning bridge from the mounted CUE data track to the emulated CD-ROM.
// GameDisc owns the image and must outlive this adapter.
class DiscCdRomMedia final : public psx::CdRomMedia {
public:
  explicit DiscCdRomMedia(disc::Iso9660Image &image) noexcept : image_(image) {}

  [[nodiscard]] std::uint32_t sectorCount() const noexcept override {
    return image_.sectorCount();
  }
  [[nodiscard]] bool readDataSector(
      std::uint32_t lba,
      std::span<std::byte, sector_size> destination) noexcept override;
  [[nodiscard]] bool readRawSector(
      std::uint32_t lba,
      std::span<std::byte, raw_sector_size> destination) noexcept override;

private:
  disc::Iso9660Image &image_;
};

} // namespace sf::game
