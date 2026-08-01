#include "sf/game/mission.hpp"

#include "sf/assets/hog_archive.hpp"
#include "sf/core/error.hpp"
#include "sf/game/disc_info.hpp"
#include "sf/game/embedded_hog.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/localization.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace sf::game {
namespace {

constexpr std::array missions{
    MissionDefinition{0U, "Georgia Street", "SUBWAY", "SUBWAY.OVL",
                      "SOL/SUBWAY.STR", "EOL/SUBWAY.STR", 0},
    MissionDefinition{1U, "Destroyed subway", "SUBWAY2", "SUBWAY2.OVL", "",
                      "EOL/SUBWAY2.STR", 1},
    MissionDefinition{2U, "Main subway line", "SUBWAY3", "SUBWAY3.OVL", "",
                      "EOL/SUBWAY3.STR", 2},
    MissionDefinition{3U, "Washington Park", "PARK", "PARK.OVL", "SOL/PARK.STR",
                      "", 3},
    MissionDefinition{4U, "Freedom Memorial", "PARK2", "PARK2.OVL",
                      "SOL/PARK2.STR", "EOL/PARK2.STR", 4},
    MissionDefinition{5U, "Expo Center Reception", "MUSEUM", "MUSEUM.OVL",
                      "SOL/MUSEUM.STR", "EOL/MUSEUM.STR", 5},
    MissionDefinition{6U, "Expo Center Dinorama", "MUSEUM2", "MUSEUM2.OVL", "",
                      "", 6},
    MissionDefinition{7U, "Rhoemer's Base", "BASEEXT", "BASEEXT.OVL",
                      "SOL/BASEEXT.STR", "", 7, 0U},
    MissionDefinition{8U, "Base Bunker", "BUNKER", "BASEEXT.OVL", "", "", 8,
                      1U},
    MissionDefinition{9U, "Base Tower", "CHOPPER", "CHOPPER.OVL",
                      "SOL/CHOPPER.STR", "EOL/CHOPPER.STR", 9, 0U},
    MissionDefinition{10U, "Base Escape", "BASEEXT2", "BASEEXT.OVL", "",
                      "EOL/BASEEXT2.STR", 10, 2U},
    MissionDefinition{11U, "Rhoemer's Stronghold", "CHURCH", "LEVSPEC.OVL",
                      "SOL/CHURCH.STR", "EOL/CHURCH.STR", 11},
    MissionDefinition{12U, "Stronghold lower level", "CHURCH2", "LEVSPEC.OVL",
                      "", "", 12, 1U},
    MissionDefinition{13U, "Stronghold catacombs", "CATACOMB", "CATACOMB.OVL",
                      "SOL/CATACOMB.STR", "EOL/CATACOMB.STR", 13, 0U},
    MissionDefinition{14U, "PHARCOM warehouses", "WHOUSE", "WHOUSE.OVL",
                      "SOL/WHOUSE.STR", "", 14},
    MissionDefinition{15U, "PHARCOM elite guards", "WHOUSE2", "WHOUSE.OVL",
                      "SOL/WHOUSE2.STR", "EOL/WHOUSE2.STR", 15, 1U},
    MissionDefinition{16U, "Warehouse 76", "INWHOUSE", "WHOUSE.OVL", "",
                      "EOL/INWHOUSE.STR", 16, 2U},
    MissionDefinition{17U, "Silo access tunnels", "CAVE", "CAVE.OVL", "",
                      "EOL/CAVE.STR", 17, 0U},
    MissionDefinition{18U, "Tunnel blackout", "CAVE2", "WHOUSE.OVL", "",
                      "EOL/CAVE2.STR", 18, 1U, "CAVE.OVL"},
    MissionDefinition{19U, "Missile Silo", "SILO", "WHOUSE.OVL", "",
                      "EOL/SILO.STR", 19, 2U, "CAVE.OVL"},
};

// SF2 groups story movies by chapter number. `_1` is the chapter opening,
// the highest later suffix is its ending, and intermediate suffixes are
// scripted mid-mission clips. Empty entries are authored omissions.
constexpr std::array sf2_missions{
    MissionDefinition{0U, "Colorado Mountains", "COLO", "COLO.OVL",
                      "2_1.STR", "2_3.STR", 0},
    MissionDefinition{1U, "McKenzie Airbase Interior", "AIRBASE",
                      "AIRBASE.OVL", "3_1.STR", "3_4.STR", 1},
    MissionDefinition{2U, "Colorado Interstate 70", "HWAY", "HWAY.OVL",
                      "4_1.STR", "", 2},
    MissionDefinition{3U, "I-70 Mountain Bridge", "BRIDGE", "BRIDGE.OVL",
                      "5_1.STR", "5_2.STR", 3},
    MissionDefinition{4U, "McKenzie Airbase Exterior", "AIRBASEX",
                      "AIRBASEX.OVL", "6_1.STR", "", 4},
    MissionDefinition{5U, "Colorado Train Ride", "TRAIN", "TRAIN.OVL",
                      "7_1.STR", "7_2.STR", 5},
    MissionDefinition{6U, "Colorado Train Race", "TRAIN2", "TRAIN2.OVL",
                      "8_1.STR", "8_2.STR", 6},
    MissionDefinition{7U, "C-130 Wreck Site", "WRECK", "WRECK.OVL",
                      "9_1.STR", "9_2.STR", 7},
    MissionDefinition{8U, "Pharcom Expo Center", "DISCO", "DISCO.OVL",
                      "10_1.STR", "", 8},
    MissionDefinition{9U, "Morgan", "DARKMUSE", "DARKMUSE.OVL",
                      "11_1.STR", "", 9},
    MissionDefinition{10U, "Moscow Club 32", "MOSCOW", "MOSCOW.OVL",
                      "12_1.STR", "", 10},
    MissionDefinition{11U, "Moscow Streets", "MOSCOW2", "MOSCOW2.OVL",
                      "13_1.STR", "13_2.STR", 11},
    MissionDefinition{12U, "Volkov Park", "MOSCOW3", "MOSCOW3.OVL",
                      "14_1.STR", "14_2.STR", 12},
    MissionDefinition{13U, "Gregorov", "GARAGE", "GARAGE.OVL",
                      "15_1.STR", "15_3.STR", 13},
    MissionDefinition{14U, "Aljir Prison Break-in", "GULAG", "GULAG.OVL",
                      "16_1.STR", "", 14},
    MissionDefinition{15U, "Aljir Prison Escape", "GULAG2", "GULAG2.OVL",
                      "", "17_3.STR", 15},
    MissionDefinition{16U, "Agency Bio-Lab", "LABS1", "LABS1.OVL",
                      "18_1.STR", "18_2.STR", 16},
    MissionDefinition{17U, "Agency Bio-Lab Escape", "LABS2", "LABS2.OVL",
                      "", "19_2.STR", 17},
    MissionDefinition{18U, "New York Slums", "SLUMS", "SLUMS.OVL", "",
                      "20_3.STR", 18},
    MissionDefinition{19U, "New York Sewer", "SLUMS2", "SLUMS2.OVL", "",
                      "21_2.STR", 19},
    MissionDefinition{20U, "Finale", "CHINBOSS", "CHINBOSS.OVL", "", "",
                      20},
};

constexpr std::array sf3_missions{
    MissionDefinition{0U, "Hotel Fukushima", "TOKYO", "GENERIC.OVL", "", "",
                      0},
    MissionDefinition{1U, "Costa Rican Plantation", "JUNGLE", "GENERIC.OVL",
                      "", "", 1},
    MissionDefinition{2U, "C-5 Galaxy Transport", "JUNGLE3", "JUNGLE3.OVL",
                      "", "", 2},
    MissionDefinition{3U, "Pugari Gold Mine", "AFRICA1", "AFRICA1.OVL", "",
                      "", 3},
    MissionDefinition{4U, "Pugari Complex", "AFRICA2", "AFRICA2.OVL", "", "",
                      4},
    MissionDefinition{5U, "Kabul, Afghanistan", "AFGHAN2", "GENERIC.OVL", "",
                      "", 5},
    MissionDefinition{6U, "S.S. Lorelei", "LONDON1", "GENERIC.OVL", "", "",
                      6},
    MissionDefinition{7U, "Aztec Ruins", "JUNGLE2", "GENERIC.OVL", "", "",
                      7},
    MissionDefinition{8U, "Waterfront", "LONDON2", "GENERIC.OVL", "", "", 8},
    MissionDefinition{9U, "Docks Final Assault", "LONDON3", "GENERIC.OVL", "",
                      "", 9},
    MissionDefinition{10U, "Convoy", "AFGHAN1", "GENERIC.OVL", "", "", 10},
    MissionDefinition{11U, "The Beast", "AFGHAN3", "GENERIC.OVL", "", "",
                      11},
    MissionDefinition{12U, "Australian Outback", "TRIAGE1", "GENERIC.OVL", "",
                      "", 12},
    MissionDefinition{13U, "St. George Australia", "TRIAGE2", "GENERIC.OVL",
                      "", "", 13},
    MissionDefinition{14U, "Paradise Ridge", "RIDGE", "GENERIC.OVL", "", "",
                      14},
    MissionDefinition{15U, "Militia Compound", "SNOWCAMP", "GENERIC.OVL", "",
                      "", 15},
    MissionDefinition{16U, "Underground Bunker", "MCAVES", "GENERIC.OVL", "",
                      "", 16},
    MissionDefinition{17U, "Senate Building", "SENATE", "GENERIC.OVL", "", "",
                      17},
    MissionDefinition{18U, "DC Subway", "SENATE2", "GENERIC.OVL", "", "", 18},
};
constexpr std::array<std::string_view, 1U> subway_scripted_movies{
    "SOL/INTRO.STR"};
constexpr std::array<std::string_view, 1U> museum_scripted_movies{
    "CUT/MUSEUM.STR"};
constexpr std::array<std::string_view, 1U> museum2_scripted_movies{
    "CUT/MUSEUM2.STR"};
constexpr std::array<std::string_view, 1U> church2_scripted_movies{
    "CUT/CHURCH2.STR"};
constexpr std::array<std::string_view, 2U> catacomb_scripted_movies{
    "CUT/CATACOMB.STR", "CUT/CAT2.STR"};
constexpr std::array<std::string_view, 1U> warehouse_scripted_movies{
    "CUT/WHOUSE.STR"};
constexpr std::array<std::string_view, 2U> silo_scripted_movies{
    "CUT/SILO.STR", "CUT/SILO2.STR"};
// SF2 action 0x70 calls MovieRequest_StartDefault with the embedded-HOG
// catalog index.  The exact mission-script operands recover these three
// mid-mission clips: AIRBASE requests indices 3/4 and AIRBASEX requests 10.
constexpr std::array<std::string_view, 2U> sf2_airbase_scripted_movies{
    "3_2.STR", "3_3.STR"};
constexpr std::array<std::string_view, 1U> sf2_airbasex_scripted_movies{
    "6_3.STR"};
constexpr std::array<std::uint8_t, 2U> sf2_airbase_scripted_movie_indices{
    3U, 4U};
constexpr std::array<std::uint8_t, 1U> sf2_airbasex_scripted_movie_indices{
    10U};

std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
    throw core::Error{core::ErrorCode::invalid_format, "Truncated DLF header"};
  }
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

DiscMovie loadSf2Movie(GameDisc &disc, std::string_view name) {
  const auto archive_path =
      disc.game()->disc_number == 2U ? "MOVIE2.HOG" : "MOVIE1.HOG";
  const auto archive = disc.image().find(archive_path);
  std::array<std::byte, disc::Iso9660Image::logical_sector_size> header{};
  if (!disc.image().copyDataSector(archive.extent_lba, header)) {
    throw core::Error{core::ErrorCode::io,
                      "Could not read the SF2 movie catalog"};
  }
  const auto count = readLe32(header, 4U);
  const auto names_offset = readLe32(header, 12U);
  const auto data_offset = readLe32(header, 16U);
  constexpr auto fixed_header_size = 20U;
  if (count == 0U || count > 256U ||
      fixed_header_size + (static_cast<std::size_t>(count) + 1U) * 4U >
          names_offset ||
      names_offset >= header.size() || data_offset < names_offset) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "SF2 movie catalog header is invalid"};
  }
  auto name_cursor = static_cast<std::size_t>(names_offset);
  for (auto index = 0U; index < count; ++index) {
    const auto begin = name_cursor;
    while (name_cursor < header.size() &&
           header[name_cursor] != std::byte{}) {
      ++name_cursor;
    }
    if (begin == name_cursor || name_cursor >= header.size()) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "SF2 movie catalog names are truncated"};
    }
    std::string entry_name;
    entry_name.reserve(name_cursor - begin);
    for (auto cursor = begin; cursor < name_cursor; ++cursor) {
      entry_name.push_back(static_cast<char>(
          std::to_integer<unsigned char>(header[cursor])));
    }
    ++name_cursor;
    if (!std::ranges::equal(entry_name, name,
                            [](char left, char right) {
                              return std::toupper(
                                         static_cast<unsigned char>(left)) ==
                                     std::toupper(
                                         static_cast<unsigned char>(right));
                            })) {
      continue;
    }
    const auto offset = readLe32(
        header, fixed_header_size + static_cast<std::size_t>(index) * 4U);
    const auto next_offset = readLe32(
        header,
        fixed_header_size + (static_cast<std::size_t>(index) + 1U) * 4U);
    if (next_offset <= offset) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "SF2 movie catalog range is invalid"};
    }
    const auto archive_sector_count = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(archive.size) +
         disc::Iso9660Image::logical_sector_size - 1U) /
        disc::Iso9660Image::logical_sector_size);
    // Catalog offsets are physical-sector indices following the one Form-1
    // header sector. The 0x920 catalog field describes each STR sector's
    // 2336-byte Mode-2 payload; it is not a conversion factor for the offset
    // table. The final sentinel plus the header exactly equals the ISO extent
    // sector count on both retail discs. Scaling the delta made every movie
    // consume the beginning of its successor and pushed the final entry past
    // the physical disc.
    if (next_offset >= archive_sector_count) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "SF2 movie catalog exceeds its ISO extent"};
    }
    const auto sector_count = next_offset - offset;
    const auto first_sector = archive.extent_lba + 1U + offset;
    return DiscMovie{
        std::string{archive_path} + ":" + entry_name,
        disc.image().readRawSectorRange(first_sector, sector_count),
    };
  }
  throw core::Error{core::ErrorCode::not_found,
                    "SF2 movie was not found in the retail catalog: " +
                        std::string{name}};
}

std::vector<std::byte> copyBytes(std::span<const std::byte> bytes) {
  return {bytes.begin(), bytes.end()};
}

assets::HogArchive parseObjectModels(std::span<const std::byte> dlf) {
  const auto archive_offset = static_cast<std::size_t>(readLe32(dlf, 0));
  const auto archive_end = static_cast<std::size_t>(readLe32(dlf, 4));
  if (archive_offset >= archive_end || archive_end > dlf.size()) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "DLF object-model archive is invalid"};
  }
  return assets::HogArchive::parse(
      copyBytes(dlf.subspan(archive_offset, archive_end - archive_offset)));
}

const MissionDefinition &definitionForDisc(const GameDisc &disc,
                                           std::uint32_t index) {
  if (!disc.game() || disc.game()->id == GameId::syphon_filter) {
    return missionDefinition(index);
  }
  if (disc.game()->id == GameId::syphon_filter_2) {
    const auto &definition =
        missionDefinition(GameId::syphon_filter_2, index);
    const auto resources =
        missionResources(disc.game()->id, disc.game()->disc_number);
    if (std::ranges::none_of(resources, [index](const auto &resource) {
          return resource.selection_index == index;
        })) {
      throw core::Error{
          core::ErrorCode::not_found,
          "Mission is not present on this Syphon Filter 2 disc",
      };
    }
    return definition;
  }
  if (disc.game()->id == GameId::syphon_filter_3) {
    const auto &definition =
        missionDefinition(GameId::syphon_filter_3, index);
    const auto resources =
        missionResources(disc.game()->id, disc.game()->disc_number);
    if (std::ranges::none_of(resources, [index](const auto &resource) {
          return resource.selection_index == index;
        })) {
      throw core::Error{
          core::ErrorCode::not_found,
          "Mission is not present on this Syphon Filter 3 disc",
      };
    }
    return definition;
  }
  throw core::Error{core::ErrorCode::invalid_argument,
                    "Native mission packaging is not mapped for this game"};
}

} // namespace

DiscMovie loadSf2EmbeddedMovie(GameDisc &disc, std::string_view name) {
  if (!disc.game() || disc.game()->id != GameId::syphon_filter_2) {
    throw core::Error{core::ErrorCode::unsupported,
                      "Embedded SF2 movies require a supported SF2 disc"};
  }
  return loadSf2Movie(disc, name);
}

std::vector<Sf2EmbeddedMovieCatalogEntry>
sf2EmbeddedMovieCatalog(GameDisc &disc) {
  if (!disc.game() || disc.game()->id != GameId::syphon_filter_2) {
    throw core::Error{core::ErrorCode::unsupported,
                      "Embedded SF2 movies require a supported SF2 disc"};
  }
  const auto archive_path =
      disc.game()->disc_number == 2U ? "MOVIE2.HOG" : "MOVIE1.HOG";
  const auto archive = disc.image().find(archive_path);
  std::array<std::byte, disc::Iso9660Image::logical_sector_size> header{};
  if (!disc.image().copyDataSector(archive.extent_lba, header)) {
    throw core::Error{core::ErrorCode::io,
                      "Could not read the SF2 movie catalog"};
  }
  const auto count = readLe32(header, 4U);
  const auto names_offset = readLe32(header, 12U);
  const auto data_offset = readLe32(header, 16U);
  constexpr auto fixed_header_size = 20U;
  if (count == 0U || count > 256U ||
      fixed_header_size + (static_cast<std::size_t>(count) + 1U) * 4U >
          names_offset ||
      names_offset >= header.size() || data_offset < names_offset) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "SF2 movie catalog header is invalid"};
  }
  const auto archive_sector_count = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(archive.size) +
       disc::Iso9660Image::logical_sector_size - 1U) /
      disc::Iso9660Image::logical_sector_size);
  auto name_cursor = static_cast<std::size_t>(names_offset);
  std::vector<Sf2EmbeddedMovieCatalogEntry> result;
  result.reserve(count);
  for (auto index = 0U; index < count; ++index) {
    const auto begin = name_cursor;
    while (name_cursor < header.size() &&
           header[name_cursor] != std::byte{}) {
      ++name_cursor;
    }
    if (begin == name_cursor || name_cursor >= header.size()) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "SF2 movie catalog names are truncated"};
    }
    std::string entry_name;
    entry_name.reserve(name_cursor - begin);
    for (auto cursor = begin; cursor < name_cursor; ++cursor) {
      entry_name.push_back(static_cast<char>(
          std::to_integer<unsigned char>(header[cursor])));
    }
    ++name_cursor;
    const auto offset = readLe32(
        header, fixed_header_size + static_cast<std::size_t>(index) * 4U);
    const auto next_offset = readLe32(
        header,
        fixed_header_size + (static_cast<std::size_t>(index) + 1U) * 4U);
    if (next_offset <= offset || next_offset >= archive_sector_count) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "SF2 movie catalog range is invalid"};
    }
    const auto sector_count = next_offset - offset;
    result.push_back(Sf2EmbeddedMovieCatalogEntry{
        std::move(entry_name), offset, sector_count,
        static_cast<std::size_t>(sector_count) * 2352U});
  }
  return result;
}

std::span<const MissionDefinition> missionCatalog() noexcept {
  return missions;
}

std::span<const MissionDefinition> missionCatalog(GameId game) noexcept {
  switch (game) {
  case GameId::syphon_filter:
    return missions;
  case GameId::syphon_filter_2:
    return sf2_missions;
  case GameId::syphon_filter_3:
    return sf3_missions;
  }
  return {};
}

const MissionDefinition &missionDefinition(std::uint32_t index) {
  if (index >= missions.size()) {
    throw core::Error{core::ErrorCode::invalid_argument,
                      "Mission index is outside the retail campaign"};
  }
  return missions[index];
}

const MissionDefinition &missionDefinition(GameId game, std::uint32_t index) {
  const auto select = [index](const auto &catalog,
                              std::string_view game_name)
      -> const MissionDefinition & {
    if (index >= catalog.size()) {
      throw core::Error{core::ErrorCode::invalid_argument,
                        "Mission index is outside the " +
                            std::string{game_name} + " campaign"};
    }
    return catalog[index];
  };
  switch (game) {
  case GameId::syphon_filter:
    return select(missions, "Syphon Filter");
  case GameId::syphon_filter_2:
    return select(sf2_missions, "Syphon Filter 2");
  case GameId::syphon_filter_3:
    return select(sf3_missions, "Syphon Filter 3");
  }
  throw core::Error{core::ErrorCode::invalid_argument,
                    "Unknown game in mission catalog"};
}

std::span<const std::string_view>
missionScriptedMoviePaths(std::uint32_t index) noexcept {
  switch (index) {
  case 0U:
    return subway_scripted_movies;
  case 5U:
    return museum_scripted_movies;
  case 6U:
    return museum2_scripted_movies;
  case 12U:
    return church2_scripted_movies;
  case 13U:
    return catacomb_scripted_movies;
  case 14U:
    return warehouse_scripted_movies;
  case 19U:
    return silo_scripted_movies;
  default:
    return {};
  }
}

std::span<const std::string_view>
missionScriptedMoviePaths(GameId game, std::uint32_t index) noexcept {
  if (game == GameId::syphon_filter) {
    return missionScriptedMoviePaths(index);
  }
  if (game != GameId::syphon_filter_2) {
    return {};
  }
  switch (index) {
  case 1U:
    return sf2_airbase_scripted_movies;
  case 4U:
    return sf2_airbasex_scripted_movies;
  default:
    return {};
  }
}

std::span<const std::uint8_t>
missionScriptedMovieCatalogIndices(GameId game,
                                   std::uint32_t index) noexcept {
  if (game != GameId::syphon_filter_2) {
    return {};
  }
  switch (index) {
  case 1U:
    return sf2_airbase_scripted_movie_indices;
  case 4U:
    return sf2_airbasex_scripted_movie_indices;
  default:
    return {};
  }
}

MissionPackage::MissionPackage(
    GameId game_id, MissionDefinition definition,
    assets::MissionBriefing briefing,
    bool has_retail_briefing, assets::FogArchive archive,
    std::optional<assets::MissionScriptArchive> mission_scripts,
    LegacyMissionImage legacy_image, DiscMovie opening_movie,
    std::vector<DiscMovie> scripted_movies, DiscMovie ending_movie,
    assets::HogArchive world_models, assets::HogArchive object_models,
    assets::HogArchive special_effects, assets::HogArchive interface_assets,
    assets::HogArchive menu_assets, assets::HogArchive character_animations,
    std::vector<assets::HogArchive> texture_banks, assets::LevelLayout layout,
    assets::MissionObjects objects, std::size_t texture_file_count)
    : game_id_(game_id), definition_(definition),
      briefing_(std::move(briefing)),
      has_retail_briefing_(has_retail_briefing), archive_(std::move(archive)),
      mission_scripts_(std::move(mission_scripts)),
      legacy_image_(std::move(legacy_image)),
      opening_movie_(std::move(opening_movie)),
      scripted_movies_(std::move(scripted_movies)),
      ending_movie_(std::move(ending_movie)),
      world_models_(std::move(world_models)),
      object_models_(std::move(object_models)),
      special_effects_(std::move(special_effects)),
      interface_assets_(std::move(interface_assets)),
      menu_assets_(std::move(menu_assets)),
      character_animations_(std::move(character_animations)),
      texture_banks_(std::move(texture_banks)), layout_(std::move(layout)),
      objects_(std::move(objects)), texture_file_count_(texture_file_count) {}

const assets::HogArchive &MissionPackage::textureBank(std::size_t bank) const {
  if (bank >= texture_banks_.size()) {
    throw core::Error{core::ErrorCode::invalid_argument,
                      "Invalid mission texture bank " + std::to_string(bank) +
                          " (mission contains " +
                          std::to_string(texture_banks_.size()) + ")"};
  }
  return texture_banks_[bank];
}

MissionPackage MissionPackage::load(GameDisc &disc, std::uint32_t index) {
  const auto &definition = definitionForDisc(disc, index);
  const auto is_sf1 =
      !disc.game() || disc.game()->id == GameId::syphon_filter;
  const MissionDefinition *runtime_definition = &definition;
  if (disc.game() && disc.game()->id == GameId::syphon_filter_2) {
    const auto runtime_selection = missionRuntimeSelection(
        disc.game()->id, disc.game()->disc_number,
        static_cast<std::uint16_t>(index));
    if (!runtime_selection) {
      throw core::Error{core::ErrorCode::not_found,
                        "SF2 mission has no runtime resource selection"};
    }
    runtime_definition =
        &missionDefinition(GameId::syphon_filter_2, *runtime_selection);
  }
  const auto resource = std::string{runtime_definition->resource_name};
  const auto archive_directory =
      disc.game() ? disc.game()->layout.mission_archive_directory
                  : std::string_view{"FOG"};
  const auto archive_path =
      std::string{archive_directory} + "/" + resource + ".FOG";
  auto archive = assets::FogArchive::parse(disc.image().readFile(archive_path));
  auto legacy_image = LegacyMissionImage::load(disc, archive, archive_path);

  constexpr std::array required_files{
      "SLF.RFF",  "VLF.RFF",  "DLF.RFF",  "WLDEMD.HOG",
      "VRAM.HOG", "INIT.OVL", "MENU.HOG",
  };
  for (const auto *name : required_files) {
    static_cast<void>(archive.file(name));
  }
  static_cast<void>(archive.file(resource + ".BIN"));
  static_cast<void>(archive.file(resource + ".DAT"));

  auto briefing =
      assets::MissionBriefing::fallback(std::string{definition.title});
  auto has_retail_briefing = false;
  try {
    const auto briefing_overlay =
        runtime_definition->briefing_overlay_name.empty()
            ? runtime_definition->overlay_name
            : runtime_definition->briefing_overlay_name;
    const auto overlay_bytes =
        is_sf1
            ? disc.image().readFile("BIN/" + std::string{briefing_overlay})
            : copyBytes(archive.file(briefing_overlay));
    briefing = assets::MissionBriefing::parseOverlayRecord(
        overlay_bytes, runtime_definition->briefing_record, definition.title);
    has_retail_briefing = true;
  } catch (const core::Error &) {
    // The overlay is authoritative. Keep DLF parsing only as a fallback
    // for compatible images whose overlay has no recoverable text table.
    try {
      briefing = assets::MissionBriefing::parseRecord(
          archive.file("DLF.RFF"), runtime_definition->briefing_record,
          definition.title);
      has_retail_briefing = true;
    } catch (const core::Error &) {
      // A malformed optional briefing must not prevent mission loading.
    }
  }
  if (is_sf1) {
    if (const auto localized = localizedMissionBriefing(index)) {
    briefing = assets::MissionBriefing::fromFields(
        localized->location, localized->mission_title, localized->date_time,
        localized->directive, localized->additional_directive);
    has_retail_briefing = true;
    }
  }
  std::vector<assets::HogArchive> texture_banks;
  texture_banks.push_back(
      assets::HogArchive::parse(copyBytes(archive.file("VRAM.HOG"))));
  if (const auto bank = std::ranges::find_if(
          archive.entries(),
          [](const auto &entry) { return entry.name == "VRAM1.HOG"; });
      bank != archive.entries().end()) {
    texture_banks.push_back(
        assets::HogArchive::parse(copyBytes(archive.file(bank->name))));
  }
  auto world_models =
      assets::HogArchive::parse(copyBytes(archive.file("WLDEMD.HOG")));
  auto object_models = parseObjectModels(archive.file("DLF.RFF"));
  auto special_effects =
      is_sf1 ? assets::HogArchive::parse(
                   disc.image().readFile("COMMON/SPFX.HOG"))
             : parseEmbeddedHog(disc.executable(), "90SIDE.TIM",
                                "BEEPSX.VB");
  auto interface_assets =
      is_sf1 ? assets::HogArchive::parse(
                   disc.image().readFile("COMMON/INTRFACE.HOG"))
             : parseEmbeddedHog(disc.executable(), "AMGA.TIM",
                                "90SIDE.TIM");
  auto menu_assets =
      assets::HogArchive::parse(copyBytes(archive.file("MENU.HOG")));
  auto character_animations =
      is_sf1
          ? assets::HogArchive::parse(
                disc.image().readFile("COMMON/PCHAN.HOG"))
          : parseEmbeddedHog(disc.executable(), "CLIMBA.HAN", "AMGA.TIM");
  if (is_sf1) {
    for (unsigned int frame = 0; frame < 8U; ++frame) {
      static_cast<void>(
          special_effects.file("EXPL00" + std::to_string(frame) + ".TIM"));
    }
  constexpr std::array required_interface_assets{
      "DANGER.TIM",   "TARGET.TIM",   "ARMOR.TIM",
      "PISTOL1A.TIM", "PISTOL1B.TIM", "TASERA.TIM",
      "TASERB.TIM",   "FLASHLTA.TIM", "FLASHLTB.TIM",
  };
  for (const auto *name : required_interface_assets) {
    static_cast<void>(interface_assets.file(name));
  }
  }
  constexpr std::array required_menu_assets{
      "GLOKSIL.TIM", "TASER.TIM", "FLASHLT.TIM",  "MAP1.TIM",
      "MAP2.TIM",    "MAP3.TIM",  "WEAPDESC.TXT",
  };
  if (is_sf1 && index == 0U) {
    for (const auto *name : required_menu_assets) {
      static_cast<void>(menu_assets.file(name));
    }
  }
  constexpr std::array required_animations{
      "ST0.LWR", "ST02.UPR", "WK0.LWR",    "WK0.UPR",
      "RN0.LWR", "RN0.UPR",  "IDLE13.HAN",
  };
  if (is_sf1) {
    for (const auto *name : required_animations) {
      static_cast<void>(character_animations.file(name));
    }
  }
  auto layout = assets::LevelLayout::parse(
      archive.file(resource + ".DAT"), world_models.entries().size(),
      is_sf1 ? 15U : 16U);
  auto objects = assets::MissionObjects::parse(archive.file(resource + ".BIN"));
  std::optional<assets::MissionScriptArchive> mission_scripts;
  if (!is_sf1) {
    mission_scripts =
        assets::MissionScriptArchive::parse(archive.file(resource + ".SS"));
  }
  const auto texture_file_count =
      std::accumulate(texture_banks.begin(), texture_banks.end(), std::size_t{},
                      [](std::size_t count, const assets::HogArchive &bank) {
                        return count + bank.entries().size();
                      });
  const auto load_movie = [&disc, is_sf1](std::string_view path) {
    if (path.empty()) {
      return DiscMovie{};
    }
    if (!is_sf1) {
      return loadSf2EmbeddedMovie(disc, path);
    }
    return DiscMovie{
        std::string{path},
        disc.image().readRawSectorFile(std::string{path}),
    };
  };
  const auto game_id =
      disc.game() ? disc.game()->id : GameId::syphon_filter;
  auto opening_movie = load_movie(definition.opening_movie_path);
  std::vector<DiscMovie> scripted_movies;
  for (const auto path : missionScriptedMoviePaths(game_id, index)) {
    scripted_movies.push_back(load_movie(path));
  }
  auto ending_movie = load_movie(definition.ending_movie_path);
  return MissionPackage{
      game_id,
      definition,
      std::move(briefing),
      has_retail_briefing,
      std::move(archive),
      std::move(mission_scripts),
      std::move(legacy_image),
      std::move(opening_movie),
      std::move(scripted_movies),
      std::move(ending_movie),
      std::move(world_models),
      std::move(object_models),
      std::move(special_effects),
      std::move(interface_assets),
      std::move(menu_assets),
      std::move(character_animations),
      std::move(texture_banks),
      std::move(layout),
      std::move(objects),
      texture_file_count,
  };
}

MissionPackage MissionPackage::loadFirst(GameDisc &disc) {
  return load(disc, 0U);
}

} // namespace sf::game
