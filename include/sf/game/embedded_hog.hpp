#pragma once

#include "sf/assets/hog_archive.hpp"

#include <string_view>

namespace sf::psx {
class Executable;
}

namespace sf::game {

// Retail sequel executables keep their resident archives in the loaded text
// image. The first entry names are stable boundaries for supported hashes.
[[nodiscard]] assets::HogArchive
parseEmbeddedHog(const psx::Executable &executable,
                 std::string_view first_entry_name,
                 std::string_view following_archive_first_entry = {});

} // namespace sf::game
