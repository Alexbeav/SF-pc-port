#include "sf/game/supported_games.hpp"

#include <array>

namespace sf::game {
namespace {

constexpr core::Sha256Digest syphon_filter_us_v11_exe{
    std::byte{0xba}, std::byte{0xc2}, std::byte{0x92}, std::byte{0x06},
    std::byte{0x1a}, std::byte{0xd5}, std::byte{0xbc}, std::byte{0x71},
    std::byte{0x8c}, std::byte{0xe1}, std::byte{0x37}, std::byte{0xef},
    std::byte{0x5b}, std::byte{0x43}, std::byte{0xd3}, std::byte{0xd7},
    std::byte{0xe9}, std::byte{0xb1}, std::byte{0xb6}, std::byte{0x52},
    std::byte{0x48}, std::byte{0xfb}, std::byte{0x0d}, std::byte{0x52},
    std::byte{0x22}, std::byte{0x9f}, std::byte{0x32}, std::byte{0x8c},
    std::byte{0xcf}, std::byte{0xe4}, std::byte{0xab}, std::byte{0x4e},
};

constexpr core::Sha256Digest syphon_filter_2_us_exe{
    std::byte{0x75}, std::byte{0xa3}, std::byte{0x60}, std::byte{0xbf},
    std::byte{0x74}, std::byte{0x65}, std::byte{0xdf}, std::byte{0xde},
    std::byte{0xc8}, std::byte{0x5c}, std::byte{0x14}, std::byte{0xf9},
    std::byte{0xba}, std::byte{0x93}, std::byte{0x86}, std::byte{0x2a},
    std::byte{0xae}, std::byte{0x25}, std::byte{0x31}, std::byte{0xb4},
    std::byte{0x8d}, std::byte{0x83}, std::byte{0xfd}, std::byte{0x8d},
    std::byte{0x82}, std::byte{0xba}, std::byte{0x8c}, std::byte{0x9f},
    std::byte{0xff}, std::byte{0xa1}, std::byte{0x3d}, std::byte{0x33},
};

constexpr std::array games{
    SupportedGame{
        "Syphon Filter",
        "USA / NTSC-U",
        "1.1",
        "SCUS-94240",
        "SCUS94240",
        "SCUS_942.40",
        syphon_filter_us_v11_exe,
    },
    SupportedGame{
        "Syphon Filter 2 (Disc 1)",
        "USA / NTSC-U",
        "1.0",
        "SCUS-94451",
        "SCUS94451",
        "SCUS_944.51",
        syphon_filter_2_us_exe,
    },
    SupportedGame{
        "Syphon Filter 2 (Disc 2)",
        "USA / NTSC-U",
        "1.0",
        "SCUS-94492",
        "SCUS94492",
        "SCUS_944.92",
        syphon_filter_2_us_exe,
    },
};

} // namespace

std::span<const SupportedGame> supportedGames() noexcept {
    return games;
}

std::optional<SupportedGame> identify(
    std::string_view volume_id,
    const core::Sha256Digest& executable_sha256) noexcept {
    for (const auto& game : games) {
        if (game.volume_id == volume_id && game.executable_sha256 == executable_sha256) {
            return game;
        }
    }
    return std::nullopt;
}

} // namespace sf::game
