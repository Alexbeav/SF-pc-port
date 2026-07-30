#pragma once

#include "sf/disc/iso9660.hpp"
#include "sf/psx/cdrom.hpp"

namespace sf::game {

// Non-owning bridge from the mounted CUE data track to the emulated CD-ROM.
// GameDisc owns the image and must outlive this adapter.
class DiscCdRomMedia final : public psx::CdRomMedia {
public:
  explicit DiscCdRomMedia(disc::Iso9660Image &image) noexcept : image_(image) {}

  // Retail mission stream descriptors address sectors relative to the
  // currently mounted FOG image. Map that low-LBA window back onto its ISO
  // extent while leaving ordinary absolute disc reads unchanged.
  void mapRelativeExtent(std::uint32_t base_lba,
                         std::uint32_t sector_count) noexcept {
    relative_extent_base_ = base_lba;
    relative_extent_sector_count_ = sector_count;
  }

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
  [[nodiscard]] std::uint32_t mappedLba(std::uint32_t lba) const noexcept;

  disc::Iso9660Image &image_;
  std::uint32_t relative_extent_base_{};
  std::uint32_t relative_extent_sector_count_{};
};

} // namespace sf::game
