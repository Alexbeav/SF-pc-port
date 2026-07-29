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

constexpr core::Sha256Digest syphon_filter_3_us_exe{
    std::byte{0xb4}, std::byte{0xb3}, std::byte{0x2c}, std::byte{0xc9},
    std::byte{0x2e}, std::byte{0x6b}, std::byte{0x86}, std::byte{0x34},
    std::byte{0x76}, std::byte{0x28}, std::byte{0x93}, std::byte{0xb6},
    std::byte{0x37}, std::byte{0xbc}, std::byte{0x9a}, std::byte{0x47},
    std::byte{0x14}, std::byte{0x42}, std::byte{0xed}, std::byte{0xbe},
    std::byte{0xb7}, std::byte{0x56}, std::byte{0x9a}, std::byte{0xfc},
    std::byte{0xfb}, std::byte{0x18}, std::byte{0xea}, std::byte{0xfb},
    std::byte{0xe8}, std::byte{0x2b}, std::byte{0x94}, std::byte{0x60},
};

constexpr std::array sf2_disc1_missions{
    GameMissionResource{0U, "COLO"},
    GameMissionResource{1U, "AIRBASE"},
    GameMissionResource{2U, "HWAY"},
    GameMissionResource{3U, "BRIDGE"},
    GameMissionResource{4U, "AIRBASEX"},
    GameMissionResource{5U, "TRAIN"},
    GameMissionResource{6U, "TRAIN2"},
    GameMissionResource{7U, "WRECK"},
};

constexpr std::array sf2_disc2_missions{
    GameMissionResource{8U, "DISCO"},
    GameMissionResource{9U, "DARKMUSE"},
    GameMissionResource{10U, "MOSCOW"},
    GameMissionResource{11U, "MOSCOW2"},
    GameMissionResource{12U, "MOSCOW3"},
    GameMissionResource{13U, "GARAGE"},
    GameMissionResource{14U, "GULAG"},
    GameMissionResource{15U, "GULAG2"},
    GameMissionResource{16U, "LABS1"},
    GameMissionResource{17U, "LABS2"},
    GameMissionResource{18U, "SLUMS"},
    GameMissionResource{19U, "SLUMS2"},
    GameMissionResource{20U, "CHINBOSS"},
};

// The retail SF3 executable stores these names in reverse campaign order
// (SNOWCAMP is referenced separately). Keep selection indices in the order
// presented by the single-player campaign.
constexpr std::array sf3_missions{
    GameMissionResource{0U, "TOKYO"},
    GameMissionResource{1U, "JUNGLE"},
    GameMissionResource{2U, "JUNGLE3"},
    GameMissionResource{3U, "AFRICA1"},
    GameMissionResource{4U, "AFRICA2"},
    GameMissionResource{5U, "AFGHAN2"},
    GameMissionResource{6U, "LONDON1"},
    GameMissionResource{7U, "JUNGLE2"},
    GameMissionResource{8U, "LONDON2"},
    GameMissionResource{9U, "LONDON3"},
    GameMissionResource{10U, "AFGHAN1"},
    GameMissionResource{11U, "AFGHAN3"},
    GameMissionResource{12U, "TRIAGE1"},
    GameMissionResource{13U, "TRIAGE2"},
    GameMissionResource{14U, "RIDGE"},
    GameMissionResource{15U, "SNOWCAMP"},
    GameMissionResource{16U, "MCAVES"},
    GameMissionResource{17U, "SENATE"},
    GameMissionResource{18U, "SENATE2"},
};

constexpr std::array games{
    SupportedGame{
        GameId::syphon_filter,
        1U,
        "Syphon Filter",
        "USA / NTSC-U",
        "1.1",
        "SCUS-94240",
        "SCUS94240",
        "SCUS_942.40",
        syphon_filter_us_v11_exe,
        GameDiscLayout{"COMMON/TITLE.HOG", "", "FOG", "XA/INGAME.XA"},
        GameExecutableLayout{0x800e3f54U, 0x8010f378U, 0U, 0U, 0U, 0U, 0U,
                             0x80114cc4U, 0x80125450U, 0x80114f9dU, true},
    },
    SupportedGame{
        GameId::syphon_filter_2,
        1U,
        "Syphon Filter 2 (Disc 1)",
        "USA / NTSC-U",
        "1.0",
        "SCUS-94451",
        "SCUS94451",
        "SCUS_944.51",
        syphon_filter_2_us_exe,
        GameDiscLayout{"TITLE.HOG", "DISK1.INF", "FOG", "SCENES1.XA"},
        GameExecutableLayout{0x800f48f0U, 0x8012d0f4U, 0x800f4cb4U, 0x8011bdf2U,
                             0x8012d480U, 0x8012d498U, 0x80103968U, 0x800f703cU,
                             0x8012d49cU, 0U, false},
    },
    SupportedGame{
        GameId::syphon_filter_2,
        2U,
        "Syphon Filter 2 (Disc 2)",
        "USA / NTSC-U",
        "1.0",
        "SCUS-94492",
        "SCUS94492",
        "SCUS_944.92",
        syphon_filter_2_us_exe,
        GameDiscLayout{"TITLE.HOG", "DISK2.INF", "FOG", "SCENES2.XA"},
        GameExecutableLayout{0x800f48f0U, 0x8012d0f4U, 0x800f4cb4U, 0x8011bdf2U,
                             0x8012d480U, 0x8012d498U, 0x80103968U, 0x800f703cU,
                             0x8012d49cU, 0U, false},
    },
    SupportedGame{
        GameId::syphon_filter_3,
        1U,
        "Syphon Filter 3",
        "USA / NTSC-U",
        "1.0",
        "SCUS-94640",
        "SCUS94640",
        "SCUS_946.40",
        syphon_filter_3_us_exe,
        GameDiscLayout{"TITLE.HOG", "", "FOG", "SCENES1.XA"},
        GameExecutableLayout{0x800f7660U, 0x8012fdc8U, 0x800f7a84U, 0x8011eac6U,
                             0x80120154U, 0x8012016cU, 0x8010669cU, 0x800f9e0cU,
                             0x80130170U, 0U, false},
    },
};

} // namespace

std::span<const SupportedGame> supportedGames() noexcept {
  return games;
}

std::span<const GameMissionResource>
missionResources(GameId game, std::uint8_t disc_number) noexcept {
  if (game == GameId::syphon_filter_3 && disc_number == 1U) {
    return sf3_missions;
  }
  if (game != GameId::syphon_filter_2) {
    return {};
  }
  if (disc_number == 1U) {
    return sf2_disc1_missions;
  }
  if (disc_number == 2U) {
    return sf2_disc2_missions;
  }
  return {};
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
