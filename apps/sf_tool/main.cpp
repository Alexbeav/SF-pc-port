#include "ui_export.hpp"

#include "sf/assets/emd_scene.hpp"
#include "sf/assets/fog_archive.hpp"
#include "sf/assets/hog_archive.hpp"
#include "sf/assets/mission_briefing.hpp"
#include "sf/assets/mission_script.hpp"
#include "sf/assets/tim_image.hpp"
#include "sf/core/error.hpp"
#include "sf/core/file_io.hpp"
#include "sf/core/sha256.hpp"
#include "sf/game/actor_animation.hpp"
#include "sf/game/disc_cdrom_media.hpp"
#include "sf/game/disc_info.hpp"
#include "sf/game/embedded_hog.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/gameplay.hpp"
#include "sf/game/legacy_first_mission_runtime.hpp"
#include "sf/game/legacy_gameplay_vm.hpp"
#include "sf/game/legacy_mission_image.hpp"
#include "sf/game/localization.hpp"
#include "sf/game/mission.hpp"
#include "sf/game/sf2_runtime.hpp"
#include "sf/game/title.hpp"
#include "sf/psx/function_map.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  sf_tool inspect <game.cue>\n"
      << "  sf_tool inspect-disc-info <game.cue>\n"
      << "  sf_tool inspect-title <game.cue>\n"
      << "  sf_tool inspect-mission <game.cue> [mission-index]\n"
      << "  sf_tool inspect-mission-archive <game.cue> <resource-name>\n"
      << "  sf_tool catalog <game.cue>\n"
      << "  sf_tool list-files <game.cue> [iso-path]\n"
      << "  sf_tool extract-exe <game.cue> <output-file>\n"
      << "  sf_tool extract-file <game.cue> <iso-path> <output-file>\n"
      << "  sf_tool list-hog <game.cue> <hog-path>\n"
      << "  sf_tool extract-hog-file <game.cue> <hog-path> <name> "
         "<output-file>\n"
      << "  sf_tool list-fog <game.cue> <fog-path>\n"
      << "  sf_tool extract-fog-file <game.cue> <fog-path> <name> "
         "<output-file>\n"
      << "  sf_tool list-fog-hog <game.cue> <fog-path> <hog-name>\n"
      << "  sf_tool extract-fog-hog-file <game.cue> <fog-path> <hog-name> "
         "<name> <output-file>\n"
      << "  sf_tool extract-mission-file <game.cue> <fog-name> <output-file>\n"
      << "  sf_tool export-ui-assets <game.cue> <output-directory>\n"
      << "  sf_tool export-vit-language-pack <vit.cue> <usa-v1.1.cue> "
         "<output-directory>\n"
      << "  sf_tool export-runtime-strings <game.cue> <output.tsv>\n"
      << "  sf_tool map-functions <game.cue> <output.csv>\n"
      << "  sf_tool map-function-union <left.cue> <right.cue> <output.csv>\n"
      << "  sf_tool map-function-calls <game.cue> <output.csv>\n"
      << "  sf_tool compare-functions <left.cue> <right.cue> <output.csv>\n"
      << "  sf_tool map-embedded-archives <game.cue> <output.csv>\n"
      << "  sf_tool map-resident-overlays <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-overlays <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-classes <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-objects <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-scripts <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-script-opcodes <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-script-handler-calls <game.cue> <output.csv>\n"
      << "  sf_tool compare-mission-script-opcodes <left.cue> <right.cue> "
         "<output.csv>\n"
      << "  sf_tool map-mission-script-events <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-script-actions <game.cue> <output.csv>\n"
      << "  sf_tool map-mission-script-strings <game.cue> <output.csv>\n"
      << "  sf_tool map-object-handlers <game.cue> <output.csv>\n"
      << "  sf_tool map-string-references <game.cue> <output.csv>\n"
      << "  sf_tool map-xa-streams <game.cue> <output.csv>\n"
      << "  sf_tool probe-legacy-vm <game.cue>\n"
      << "  sf_tool probe-executable-entry <game.cue> [instruction-budget]\n"
      << "  sf_tool probe-sf2-guest-bootstrap <game.cue> "
         "[instruction-budget]\n"
      << "  sf_tool probe-sf2-mission-transition <game.cue> "
         "[instruction-budget]\n"
      << "  sf_tool probe-sf2-product-runtime <game.cue> [frames] "
         "[neutral|forward|combat|crouch|quickstate|quickobjective|"
         "crouchback|objective|objective-dialogue|weapons|pause|complete|completeflow|movies|ui|uiobjective] "
         "[resource-index-0-based] [scripted-movie-ordinal-0-based]\n"
      << "  sf_tool probe-legacy-cd <game.cue>\n"
      << "  sf_tool probe-legacy-loop <game.cue>\n"
      << "  sf_tool probe-legacy-bootstrap <game.cue>\n"
      << "  sf_tool probe-legacy-level <game.cue> [frames]\n"
      << "  sf_tool probe-legacy-mission <game.cue> <raw-ram.bin>\n"
      << "  sf_tool probe-legacy-frame <game.cue> <raw-ram.bin> [frames]\n";
}

sf::game::GameDisc openDisc(const char *path) {
  return sf::game::GameDisc::open(std::filesystem::path{path});
}

std::uint32_t parseFrameCount(const char *text) {
  const std::string_view value{text};
  std::uint32_t count{};
  const auto *const value_end = value.data() + value.size();
  const auto [end, error] = std::from_chars(value.data(), value_end, count);
  if (error != std::errc{} || end != value_end || count == 0U ||
      count > 10'000U) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Legacy frame count must be in the range 1..10000"};
  }
  return count;
}

std::uint32_t parseSf2MissionIndex(const char *text) {
  const std::string_view value{text};
  std::uint32_t index{};
  const auto *const value_end = value.data() + value.size();
  const auto [end, error] =
      std::from_chars(value.data(), value_end, index);
  if (error != std::errc{} || end != value_end || index > 20U) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "SF2 mission index must be in the range 0..20"};
  }
  return index;
}

std::uint32_t parseSf2MovieOrdinal(const char *text) {
  const std::string_view value{text};
  std::uint32_t ordinal{};
  const auto *const value_end = value.data() + value.size();
  const auto [end, error] =
      std::from_chars(value.data(), value_end, ordinal);
  if (error != std::errc{} || end != value_end || ordinal > 7U) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "SF2 scripted movie ordinal must be in the range 0..7"};
  }
  return ordinal;
}

int inspect(const char *path) {
  auto disc = openDisc(path);
  const auto &header = disc.executable().header();

  std::cout << "Volume ID:       " << disc.image().volumeId() << '\n'
            << "Boot executable: " << disc.bootPath() << '\n'
            << "Executable SHA:  " << sf::core::toHex(disc.executableHash())
            << '\n'
            << "Entry point:     0x" << std::hex << std::uppercase
            << header.initial_pc << '\n'
            << "Text address:    0x" << header.text_address << '\n'
            << "Text size:       0x" << header.text_size << std::dec << " ("
            << header.text_size << ")\n";

  if (disc.game()) {
    const auto &game = *disc.game();
    std::cout << "Recognized:      " << game.title << " " << game.region << " v"
              << game.version << " [" << game.serial << "]\n";
    return 0;
  }

  std::cout << "Recognized:      no\n";
  return 2;
}

int extractExecutable(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto &bytes = disc.executableFile();
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, bytes);
  std::cout << "Extracted " << bytes.size() << " bytes to " << output_path
            << '\n';
  return 0;
}

int extractFile(const char *cue_path, const char *iso_path,
                const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto bytes = disc.image().readFile(iso_path);
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, bytes);
  std::cout << "Extracted " << bytes.size() << " bytes from " << iso_path
            << " to " << output_path << '\n';
  return 0;
}

int listHog(const char *cue_path, const char *hog_path) {
  auto disc = openDisc(cue_path);
  const auto archive =
      sf::assets::HogArchive::parse(disc.image().readFile(hog_path));
  std::cout << "name,size\n";
  for (const auto &entry : archive.entries()) {
    std::cout << entry.name << ',' << entry.size << '\n';
  }
  return 0;
}

int extractHogFile(const char *cue_path, const char *hog_path, const char *name,
                   const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto archive =
      sf::assets::HogArchive::parse(disc.image().readFile(hog_path));
  const auto file = archive.file(name);
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, file);
  std::cout << "Extracted " << file.size() << " bytes from " << hog_path << ':'
            << name << " to " << output_path << '\n';
  return 0;
}

int listFog(const char *cue_path, const char *fog_path) {
  auto disc = openDisc(cue_path);
  const auto archive =
      sf::assets::FogArchive::parse(disc.image().readFile(fog_path));
  std::cout << "name,size\n";
  for (const auto &entry : archive.entries()) {
    std::cout << entry.name << ',' << entry.size << '\n';
  }
  return 0;
}

int extractFogFile(const char *cue_path, const char *fog_path, const char *name,
                   const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto archive =
      sf::assets::FogArchive::parse(disc.image().readFile(fog_path));
  const auto bytes = archive.file(name);
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, bytes);
  std::cout << "Extracted " << bytes.size() << " bytes from " << fog_path << ':'
            << name << " to " << output_path << '\n';
  return 0;
}

int listFogHog(const char *cue_path, const char *fog_path,
               const char *hog_name) {
  auto disc = openDisc(cue_path);
  const auto fog =
      sf::assets::FogArchive::parse(disc.image().readFile(fog_path));
  const auto bytes = fog.file(hog_name);
  const auto archive = sf::assets::HogArchive::parse(
      std::vector<std::byte>{bytes.begin(), bytes.end()});
  std::cout << "name,size\n";
  for (const auto &entry : archive.entries()) {
    std::cout << entry.name << ',' << entry.size << '\n';
  }
  return 0;
}

int extractFogHogFile(const char *cue_path, const char *fog_path,
                      const char *hog_name, const char *name,
                      const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto fog =
      sf::assets::FogArchive::parse(disc.image().readFile(fog_path));
  const auto bytes = fog.file(hog_name);
  const auto archive = sf::assets::HogArchive::parse(
      std::vector<std::byte>{bytes.begin(), bytes.end()});
  const auto file = archive.file(name);
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, file);
  std::cout << "Extracted " << file.size() << " bytes from " << fog_path << ':'
            << hog_name << ':' << name << " to " << output_path << '\n';
  return 0;
}

int extractMissionFile(const char *cue_path, const char *name,
                       const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  const auto bytes = mission.archive().file(name);
  sf::core::writeBinaryFile(std::filesystem::path{output_path}, bytes);
  std::cout << "Extracted " << bytes.size() << " bytes from " << name << " to "
            << output_path << '\n';
  return 0;
}

std::vector<std::byte> copyBytes(std::span<const std::byte> source) {
  return {source.begin(), source.end()};
}

void appendLe32(std::vector<std::byte> &output, std::uint32_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void appendString(std::vector<std::byte> &output, std::string_view value) {
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Localized string is too large"};
  }
  appendLe32(output, static_cast<std::uint32_t>(value.size()));
  output.insert(
      output.end(), reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(value.data() + value.size()));
}

bool isVitTextByte(std::byte value) noexcept {
  const auto character = std::to_integer<unsigned char>(value);
  return character == '\n' || character == '\r' || character == '\t' ||
         (character >= 0x20U && character <= 0x7eU) ||
         (character >= 0xdfU && character <= 0xfcU);
}

bool isBriefingDate(std::string_view value) noexcept {
  return value.size() >= 11U && value[2] == '/' && value[5] == ' ' &&
         value[8] == ':' && value[0] >= '0' && value[0] <= '9' &&
         value[1] >= '0' && value[1] <= '9' && value[3] >= '0' &&
         value[3] <= '9' && value[4] >= '0' && value[4] <= '9' &&
         value[6] >= '0' && value[6] <= '9' && value[7] >= '0' &&
         value[7] <= '9' && value[9] >= '0' && value[9] <= '9' &&
         value[10] >= '0' && value[10] <= '9';
}

std::vector<std::string>
scanVitOverlayStrings(std::span<const std::byte> bytes) {
  std::vector<std::string> strings;
  auto cursor = std::size_t{};
  while (cursor < bytes.size()) {
    while (cursor < bytes.size() && !isVitTextByte(bytes[cursor])) {
      ++cursor;
    }
    const auto start = cursor;
    while (cursor < bytes.size() && isVitTextByte(bytes[cursor])) {
      ++cursor;
    }
    if (cursor - start >= 3U && cursor < bytes.size() &&
        bytes[cursor] == std::byte{}) {
      std::string value(cursor - start, '\0');
      std::ranges::transform(bytes.subspan(start, cursor - start),
                             value.begin(), [](std::byte character) {
                               return static_cast<char>(
                                   std::to_integer<unsigned char>(character));
                             });
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
      }
      strings.push_back(std::move(value));
    }
    ++cursor;
  }
  return strings;
}

std::string escapeTsv(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    switch (character) {
    case '\\':
      result += "\\\\";
      break;
    case '\t':
      result += "\\t";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\n':
      result += "\\n";
      break;
    default:
      result.push_back(character);
      break;
    }
  }
  return result;
}

int exportRuntimeStrings(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot create runtime-string catalogue"};
  }
  output << "SOURCE\tTEXT\n";
  const auto write_strings = [&](std::string_view source,
                                 std::span<const std::byte> bytes) {
    for (const auto &text : scanVitOverlayStrings(bytes)) {
      const auto alphabetic = std::ranges::count_if(text, [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
      });
      if (alphabetic < 2) {
        continue;
      }
      output << escapeTsv(source) << '\t' << escapeTsv(text) << '\n';
    }
  };
  write_strings(disc.bootPath(), disc.executableFile());
  for (const auto &overlay : disc.overlays()) {
    const auto bytes = disc.image().readFile(overlay.path);
    write_strings(overlay.path, bytes);
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Could not write runtime-string catalogue"};
  }
  return 0;
}

struct VitMissionText {
  sf::assets::MissionBriefing briefing;
};

VitMissionText extractVitMissionText(std::span<const std::byte> overlay,
                                     std::size_t record_index) {
  const auto strings = scanVitOverlayStrings(overlay);
  std::vector<std::size_t> dates;
  for (std::size_t index = 0U; index < strings.size(); ++index) {
    if (isBriefingDate(strings[index])) {
      dates.push_back(index);
    }
  }
  if (dates.empty()) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "ViT mission overlay briefing is missing"};
  }
  // Several localized one-mission overlays collapse the source DLF's shared
  // record table to one authored record.
  const auto date = dates[std::min(record_index, dates.size() - 1U)];
  const auto first_date = dates.front();
  if (date < 2U || date + 1U >= strings.size() ||
      first_date + 2U >= strings.size()) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "ViT mission overlay text table is truncated"};
  }

  auto directive = strings[date - 1U];
  if (const auto separator = directive.find("\r\n\r\n");
      separator != std::string::npos) {
    directive.erase(0U, separator + 4U);
  } else if (const auto line_separator = directive.find("\n\n");
             line_separator != std::string::npos) {
    directive.erase(0U, line_separator + 2U);
  }
  auto briefing = sf::assets::MissionBriefing::fromFields(
      strings[first_date + 2U], strings[date + 1U], strings[date],
      std::move(directive), strings[date - 2U]);
  return VitMissionText{std::move(briefing)};
}

std::vector<std::string>
missionMenuCandidates(std::span<const std::byte> overlay,
                      std::string_view marker) {
  auto strings = scanVitOverlayStrings(overlay);
  std::vector<std::size_t> dates;
  for (std::size_t index = 0U; index < strings.size(); ++index) {
    if (isBriefingDate(strings[index])) {
      dates.push_back(index);
    }
  }
  if (dates.empty()) {
    return {};
  }
  auto cursor = dates.back() + (dates.size() == 1U ? 3U : 2U);
  std::vector<std::string> result;
  while (cursor < strings.size()) {
    auto value = std::move(strings[cursor++]);
    const auto uppercase_resource =
        !value.empty() && value.size() <= 16U &&
        std::ranges::all_of(value, [](unsigned char character) {
          return (character >= 'A' && character <= 'Z') ||
                 (character >= '0' && character <= '9') || character == '_';
        });
    const auto letter_count = static_cast<std::size_t>(
        std::count_if(value.begin(), value.end(), [](unsigned char character) {
          return (character >= 'A' && character <= 'Z') ||
                 (character >= 'a' && character <= 'z') || character >= 0xdfU;
        }));
    const auto binary_punctuation =
        value.find_first_of("<$!@#^\\") != std::string::npos;
    const auto source_path = value.starts_with("bin/") || value.ends_with(".c");
    // The localized root overlays are not named after every mission resource
    // (LEVSPEC.OVL, for example, reaches CHURCH2 before its binary tables).
    // Stop at the first resource token instead of scanning machine data as
    // text and poisoning the English/Russian alignment table.
    if (value == marker || value == "MOVIE" || value.starts_with('\\') ||
        source_path || binary_punctuation || letter_count < 2U ||
        (!result.empty() && uppercase_resource)) {
      break;
    }
    result.push_back(std::move(value));
  }
  return result;
}

std::vector<std::pair<std::string, std::string>>
alignMissionMenuText(std::span<const std::byte> english_overlay,
                     std::span<const std::byte> russian_overlay,
                     std::string_view marker) {
  auto english = missionMenuCandidates(english_overlay, marker);
  auto russian = missionMenuCandidates(russian_overlay, marker);
  if (english.empty() || russian.empty()) {
    return {};
  }

  // ViT rebuilt three tables with additional Russian singular/plural forms
  // and moved a few status labels.  Their order is deterministic for the
  // supported SCES-01913 image; recording those authored indices is safer
  // than guessing from string lengths (which previously paired objectives
  // with Italian fallback text or binary data).
  constexpr std::array<std::size_t, 19U> baseext_indices{
      0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  11U,
      12U, 15U, 16U, 13U, 17U, 18U, 19U, 20U, 14U,
  };
  constexpr std::array<std::size_t, 9U> levspec_indices{
      0U, 1U, 2U, 4U, 6U, 7U, 8U, 9U, 10U,
  };
  constexpr std::array<std::size_t, 9U> whouse_indices{
      0U, 1U, 4U, 5U, 6U, 7U, 8U, 10U, 11U,
  };
  std::span<const std::size_t> indices;
  if (marker == "BASEEXT") {
    indices = baseext_indices;
  } else if (marker == "LEVSPEC") {
    indices = levspec_indices;
  } else if (marker == "WHOUSE") {
    indices = whouse_indices;
  }
  if (!indices.empty() && indices.size() != english.size()) {
    return {};
  }

  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(english.size());
  for (auto index = std::size_t{}; index < english.size(); ++index) {
    const auto russian_index = indices.empty() ? index : indices[index];
    if (russian_index >= russian.size()) {
      return {};
    }
    result.emplace_back(std::move(english[index]),
                        std::move(russian[russian_index]));
  }
  return result;
}

int exportVitLanguagePack(const char *cue_path, const char *english_cue_path,
                          const char *output_path) {
  auto disc = openDisc(cue_path);
  auto english_disc = openDisc(english_cue_path);
  if (disc.bootPath() != "SCES_019.13") {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Language source must be the ViT Co. SCES-01913 image"};
  }
  if (!english_disc.game() || english_disc.bootPath() != "SCUS_942.40") {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "English mapping source must be Syphon Filter USA v1.1 SCUS-94240"};
  }
  sf::game::setGameLanguage(sf::game::GameLanguage::russian_vit);

  const auto root = std::filesystem::path{output_path};
  const auto &executable = disc.executableFile();
  struct EmbeddedTim {
    std::string_view name;
    std::size_t offset;
    std::size_t size;
  };
  constexpr std::array fonts{
      EmbeddedTim{"FONTA.TIM", 0x15ba60U, 2592U},
      EmbeddedTim{"FONTB.TIM", 0x15cc80U, 4640U},
      EmbeddedTim{"FONTC.TIM", 0x15dea0U, 5710U},
  };
  for (const auto &font : fonts) {
    if (font.offset > executable.size() ||
        executable.size() - font.offset < font.size) {
      throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                            "ViT executable font table is truncated"};
    }
    const auto bytes = std::span{executable}.subspan(font.offset, font.size);
    static_cast<void>(sf::assets::TimImage::parse(bytes));
    sf::core::writeBinaryFile(root / "fonts" / font.name, bytes, true);
  }

  const auto title =
      sf::assets::HogArchive::parse(disc.image().readFile("COMMON/TITLEI.HOG"));
  constexpr std::array title_names{
      std::string_view{"NEW.TIM"},
      std::string_view{"LOAD.TIM"},
      std::string_view{"VIDEO.TIM"},
      std::string_view{"SEARCH.TIM"},
  };
  for (const auto name : title_names) {
    const auto bytes = title.file(name);
    static_cast<void>(sf::assets::TimImage::parse(bytes));
    sf::core::writeBinaryFile(root / "title" / name, bytes, true);
  }

  const auto first_fog =
      sf::assets::FogArchive::parse(disc.image().readFile("FOG/SUBWAY.FOG"));
  const auto menu =
      sf::assets::HogArchive::parse(copyBytes(first_fog.file("MENU.HOG")));
  sf::core::writeBinaryFile(root / "WEAPDESC.TXT", menu.file("WEAPDESC.TXT"),
                            true);

  constexpr std::array briefing_magic{
      std::byte{'S'}, std::byte{'F'}, std::byte{'L'}, std::byte{'B'},
      std::byte{'R'}, std::byte{'F'}, std::byte{'1'}, std::byte{0},
  };
  std::vector<std::byte> briefings{briefing_magic.begin(),
                                   briefing_magic.end()};
  constexpr std::array menu_magic{
      std::byte{'S'}, std::byte{'F'}, std::byte{'L'}, std::byte{'M'},
      std::byte{'N'}, std::byte{'U'}, std::byte{'2'}, std::byte{0},
  };
  std::vector<std::byte> mission_menu{menu_magic.begin(), menu_magic.end()};
  const auto missions = sf::game::missionCatalog();
  appendLe32(briefings, static_cast<std::uint32_t>(missions.size()));
  appendLe32(mission_menu, static_cast<std::uint32_t>(missions.size()));
  for (const auto &definition : missions) {
    std::cout << "  mission " << (definition.index + 1U) << ": "
              << definition.resource_name << std::endl;
    const auto fog_path =
        "FOG/" + std::string{definition.resource_name} + ".FOG";
    const auto fog =
        sf::assets::FogArchive::parse(disc.image().readFile(fog_path));
    const auto localized_menu =
        sf::assets::HogArchive::parse(copyBytes(fog.file("MENU.HOG")));
    for (const auto &entry : localized_menu.entries()) {
      if (!std::string_view{entry.name}.starts_with("MAP") ||
          !std::string_view{entry.name}.ends_with(".TIM")) {
        continue;
      }
      const auto bytes = localized_menu.file(entry.name);
      static_cast<void>(sf::assets::TimImage::parse(bytes));
      sf::core::writeBinaryFile(
          root / "maps" / std::to_string(definition.index) / entry.name, bytes,
          true);
    }
    const auto language_overlay_name =
        std::string{definition.briefing_overlay_name.empty()
                        ? definition.overlay_name
                        : definition.briefing_overlay_name};
    const auto briefing_record = definition.briefing_record;
    const auto overlay_path = "BIN/" + language_overlay_name;
    const auto russian_overlay = disc.image().readFile(overlay_path);
    const auto english_overlay = english_disc.image().readFile(overlay_path);
    const auto localized =
        extractVitMissionText(russian_overlay, briefing_record);
    const auto &briefing = localized.briefing;
    appendString(briefings, briefing.location());
    appendString(briefings, sf::game::localizeText(definition.title));
    appendString(briefings, briefing.dateTime());
    appendString(briefings, briefing.directive());
    appendString(briefings, briefing.additionalDirective());
    const auto mapping = alignMissionMenuText(
        english_overlay, russian_overlay,
        std::filesystem::path{language_overlay_name}.stem().string());
    appendLe32(mission_menu, static_cast<std::uint32_t>(mapping.size()));
    for (const auto &[source, translated] : mapping) {
      appendString(mission_menu, source);
      appendString(mission_menu, translated);
    }
  }
  sf::core::writeBinaryFile(root / "briefings.dat", briefings, true);
  sf::core::writeBinaryFile(root / "mission_menu.dat", mission_menu, true);

  constexpr std::string_view manifest{
      "SFLANG=1\r\nLocale=ru-RU\r\nSource=SCES-01913 ViT Co.\r\n"
      "TextOnly=1\r\nAudio=Original\r\nFMV=Original\r\n"};
  sf::core::writeBinaryFile(
      root / "manifest.txt",
      std::span{reinterpret_cast<const std::byte *>(manifest.data()),
                manifest.size()},
      true);
  std::cout << "Exported ViT text-only language pack for " << missions.size()
            << " missions to " << root.string() << '\n';
  return 0;
}

int catalog(const char *cue_path) {
  auto disc = openDisc(cue_path);
  const auto overlays = disc.overlays();
  std::cout << "path,size,sha256\n";
  for (const auto &overlay : overlays) {
    std::cout << overlay.path << ',' << overlay.size << ','
              << sf::core::toHex(overlay.sha256) << '\n';
  }
  return 0;
}

int mapXaStreams(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->layout.streaming_audio_path.empty()) {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Disc has no recognized streaming-audio path"};
  }
  if (!disc.image().hasRawSectors()) {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "XA mapping requires a MODE2/2352 source track"};
  }
  const auto path = std::string{disc.game()->layout.streaming_audio_path};
  const auto entry = disc.image().find(path);
  if (entry.is_directory) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Streaming-audio path is a directory"};
  }
  const auto sector_count = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(entry.size) +
       sf::disc::Iso9660Image::logical_sector_size - 1U) /
      sf::disc::Iso9660Image::logical_sector_size);

  struct StreamSummary {
    struct Clip {
      std::uint32_t first_sector{};
      std::uint32_t last_sector{};
      std::uint32_t sectors{};
      bool eof{};
    };
    std::uint32_t first_sector{};
    std::uint32_t last_sector{};
    std::uint32_t sectors{};
    std::uint32_t eof_sectors{};
    std::uint32_t largest_gap{};
    std::uint8_t submode_or{};
    std::uint32_t active_clip_first{};
    std::uint32_t active_clip_sectors{};
    std::vector<Clip> clips;
  };
  std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>, StreamSummary>
      streams;
  std::array<std::byte, 2352U> sector{};
  for (std::uint32_t index = 0U; index < sector_count; ++index) {
    if (!disc.image().copyRawSector(entry.extent_lba + index, sector)) {
      throw sf::core::Error{sf::core::ErrorCode::io,
                            "Could not read an XA source sector"};
    }
    constexpr std::size_t subheader = 16U;
    const auto file = std::to_integer<std::uint8_t>(sector[subheader]);
    const auto channel = std::to_integer<std::uint8_t>(sector[subheader + 1U]);
    const auto submode = std::to_integer<std::uint8_t>(sector[subheader + 2U]);
    const auto coding = std::to_integer<std::uint8_t>(sector[subheader + 3U]);
    const auto repeated = sector[subheader] == sector[subheader + 4U] &&
                          sector[subheader + 1U] == sector[subheader + 5U] &&
                          sector[subheader + 2U] == sector[subheader + 6U] &&
                          sector[subheader + 3U] == sector[subheader + 7U];
    constexpr std::uint8_t audio_bit = 0x04U;
    constexpr std::uint8_t form2_bit = 0x20U;
    if (!repeated ||
        (submode & (audio_bit | form2_bit)) != (audio_bit | form2_bit)) {
      continue;
    }
    auto [position, inserted] = streams.try_emplace(
        {file, channel, coding}, StreamSummary{index, index, 0U, 0U, 0U, 0U});
    auto &summary = position->second;
    if (!inserted && index > summary.last_sector + 1U) {
      summary.largest_gap =
          std::max(summary.largest_gap, index - summary.last_sector - 1U);
    }
    summary.last_sector = index;
    ++summary.sectors;
    if (summary.active_clip_sectors == 0U) {
      summary.active_clip_first = index;
    }
    ++summary.active_clip_sectors;
    summary.eof_sectors += (submode & 0x80U) != 0U ? 1U : 0U;
    summary.submode_or =
        static_cast<std::uint8_t>(summary.submode_or | submode);
    if ((submode & 0x80U) != 0U) {
      summary.clips.push_back(StreamSummary::Clip{
          summary.active_clip_first, index, summary.active_clip_sectors, true});
      summary.active_clip_sectors = 0U;
    }
  }
  for (auto &[key, summary] : streams) {
    static_cast<void>(key);
    if (summary.active_clip_sectors != 0U) {
      summary.clips.push_back(
          StreamSummary::Clip{summary.active_clip_first, summary.last_sector,
                              summary.active_clip_sectors, false});
      summary.active_clip_sectors = 0U;
    }
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Could not create XA stream map"};
  }
  output << "path,file,channel,coding,stereo,sample_rate_hz,clip,"
            "clip_sectors,clip_duration_seconds,first_relative_sector,"
            "last_relative_sector,first_lba,last_lba,eof,"
            "stream_sectors,stream_duration_seconds,stream_clip_count,"
            "largest_interleave_gap,submode_or\n";
  for (const auto &[key, summary] : streams) {
    const auto [file, channel, coding] = key;
    for (std::size_t clip_index = 0U; clip_index < summary.clips.size();
         ++clip_index) {
      const auto &clip = summary.clips[clip_index];
      output << path << ',' << static_cast<unsigned int>(file) << ','
             << static_cast<unsigned int>(channel) << ",0x" << std::hex
             << std::uppercase << static_cast<unsigned int>(coding) << std::dec
             << ',' << ((coding & 1U) != 0U ? 1 : 0) << ','
             << ((coding & 4U) != 0U ? 18900 : 37800) << ',' << clip_index
             << ',' << clip.sectors << ',' << std::fixed << std::setprecision(3)
             << static_cast<double>(clip.sectors) / 75.0 << std::defaultfloat
             << ',' << clip.first_sector << ',' << clip.last_sector << ','
             << entry.extent_lba + clip.first_sector << ','
             << entry.extent_lba + clip.last_sector << ',' << (clip.eof ? 1 : 0)
             << ',' << summary.sectors << ',' << std::fixed
             << std::setprecision(3)
             << static_cast<double>(summary.sectors) / 75.0 << std::defaultfloat
             << ',' << summary.clips.size() << ',' << summary.largest_gap
             << ",0x" << std::hex << std::uppercase
             << static_cast<unsigned int>(summary.submode_or) << std::dec
             << '\n';
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Could not write XA stream map"};
  }
  std::cout << "Mapped " << streams.size() << " XA streams from " << path
            << " across " << sector_count << " sectors\n";
  return 0;
}

void listFiles(sf::disc::Iso9660Image &image, const std::string &path) {
  for (const auto &entry : image.list(path)) {
    const auto child = path.empty() ? entry.name : path + '/' + entry.name;
    if (entry.is_directory) {
      listFiles(image, child);
    } else {
      std::cout << child << ',' << entry.size << '\n';
    }
  }
}

int listDiscFiles(const char *cue_path, const char *root) {
  auto disc = openDisc(cue_path);
  std::cout << "path,size\n";
  listFiles(disc.image(), root);
  return 0;
}

int inspectTitle(const char *cue_path) {
  auto disc = openDisc(cue_path);
  const auto title = sf::game::TitleAssets::load(disc);
  std::cout << "name,mode,width,height,vram_x,vram_y,screen_x,screen_y\n";
  for (const auto &sprite : title.sprites()) {
    const auto &pixels = sprite.image.pixels();
    std::cout << sprite.name << ','
              << static_cast<unsigned int>(sprite.image.mode()) << ','
              << sprite.image.displayWidth() << ','
              << sprite.image.displayHeight() << ',' << pixels.x << ','
              << pixels.y << ',' << sprite.x << ',' << sprite.y << '\n';
  }
  return 0;
}

int inspectDiscInfo(const char *cue_path) {
  auto disc = openDisc(cue_path);
  const auto titles = sf::game::loadDiscSelectionTitles(disc);
  const auto resources =
      disc.game() ? sf::game::missionResources(disc.game()->id,
                                               disc.game()->disc_number)
                  : std::span<const sf::game::GameMissionResource>{};
  std::cout << "index,title,available-on-disc,resource\n";
  for (const auto &entry : titles) {
    const auto resource =
        std::ranges::find(resources, entry.index,
                          &sf::game::GameMissionResource::selection_index);
    std::cout << entry.index << ',' << std::quoted(entry.title) << ','
              << (resource != resources.end() ? "yes" : "no") << ','
              << (resource != resources.end() ? resource->resource_name
                                              : std::string_view{})
              << '\n';
  }
  return 0;
}

int inspectMissionArchive(const char *cue_path,
                          std::string_view resource_name) {
  auto disc = openDisc(cue_path);
  auto resource = std::string{resource_name};
  std::ranges::transform(resource, resource.begin(), [](char value) {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A'))
                                        : value;
  });
  const auto archive_directory =
      disc.game() ? disc.game()->layout.mission_archive_directory
                  : std::string_view{"FOG"};
  const auto archive_path =
      std::string{archive_directory} + '/' + resource + ".FOG";
  const auto archive =
      sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
  const auto legacy_image =
      sf::game::LegacyMissionImage::load(disc, archive, archive_path);
  static_cast<void>(legacy_image.createVirtualCd());
  const auto world_model_bytes = archive.file("WLDEMD.HOG");
  const auto world_models =
      sf::assets::HogArchive::parse(std::vector<std::byte>{
          world_model_bytes.begin(), world_model_bytes.end()});
  std::optional<sf::assets::LevelLayout> layout;
  std::optional<sf::assets::MissionObjects> objects;
  std::string layout_error;
  std::string objects_error;
  try {
    layout = sf::assets::LevelLayout::parse(archive.file(resource + ".DAT"),
                                            world_models.entries().size());
  } catch (const sf::core::Error &error) {
    layout_error = error.what();
  }
  try {
    objects =
        sf::assets::MissionObjects::parse(archive.file(resource + ".BIN"));
  } catch (const sf::core::Error &error) {
    objects_error = error.what();
  }

  auto texture_count = std::size_t{};
  for (const auto &entry : archive.entries()) {
    if (entry.name != "VRAM.HOG" && entry.name != "VRAM1.HOG") {
      continue;
    }
    const auto bytes = archive.file(entry.name);
    texture_count += sf::assets::HogArchive::parse(
                         std::vector<std::byte>{bytes.begin(), bytes.end()})
                         .entries()
                         .size();
  }
  std::cout << "resource=" << resource
            << " archive-files=" << archive.entries().size()
            << " virtual-root-files=" << legacy_image.rootFileCount()
            << " virtual-archive-files=" << legacy_image.archiveFileCount()
            << " world-models=" << world_models.entries().size()
            << " texture-files=" << texture_count;
  if (layout) {
    std::cout << " layout-compatible=yes rooms=" << layout->modelCount()
              << " initial-room=" << layout->initialRoom()
              << " resident-models=" << layout->residentModels().size();
  } else {
    std::cout << " layout-compatible=no layout-error="
              << std::quoted(layout_error);
  }
  if (objects) {
    std::cout << " objects-compatible=yes objects=" << objects->objects().size()
              << " definitions=" << objects->definitions().size()
              << " object-rooms=" << objects->roomCount()
              << " player-index=" << objects->playerIndex();
  } else {
    std::cout << " objects-compatible=no objects-error="
              << std::quoted(objects_error);
  }
  std::cout << '\n';
  return 0;
}

int inspectMission(const char *cue_path, std::uint32_t mission_index) {
  auto disc = openDisc(cue_path);
  const auto mission = sf::game::MissionPackage::load(disc, mission_index);
  const auto &definition = mission.definition();
  std::size_t section_count = 0;
  std::size_t vertex_count = 0;
  std::size_t polygon_count = 0;
  std::vector<sf::assets::EmdScene> world_scenes;
  world_scenes.reserve(mission.worldModels().entries().size());
  const auto emd_vertex_index_stride = static_cast<std::uint8_t>(
      mission.gameId() == sf::game::GameId::syphon_filter_3 ? 2U : 3U);
  for (const auto &entry : mission.worldModels().entries()) {
    auto scene = sf::assets::EmdScene::parse(
        mission.worldModels().file(entry.name), emd_vertex_index_stride);
    section_count += scene.sections().size();
    vertex_count += scene.vertexCount();
    polygon_count += scene.polygonCount();
    world_scenes.push_back(std::move(scene));
  }
  const sf::game::GameplaySession gameplay{mission};
  const auto *player_hmd =
      std::get_if<sf::assets::HmdModel>(&gameplay.playerModel().geometry);
  if (player_hmd == nullptr) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Player model is not an HMD"};
  }
  const sf::game::ActorAnimationBank actor_animations{
      mission.characterAnimations(), player_hmd->parts().size()};
  static_cast<void>(actor_animations);
  std::size_t animation_clip_count{};
  std::size_t animation_frame_count{};
  std::size_t walking_root_frames{};
  std::size_t running_root_frames{};
  std::int64_t walking_root_distance{};
  std::int64_t running_root_distance{};
  for (const auto &entry : mission.characterAnimations().entries()) {
    if (!entry.name.ends_with(".HAN") && !entry.name.ends_with(".LWR") &&
        !entry.name.ends_with(".UPR")) {
      continue;
    }
    const auto clip = sf::assets::HmdAnimationClip::parse(
        mission.characterAnimations().file(entry.name),
        player_hmd->parts().size());
    ++animation_clip_count;
    animation_frame_count += clip.frames().size();
    if (entry.name == "WK0.LWR" || entry.name == "RN0.LWR") {
      auto distance = std::int64_t{};
      for (const auto &frame : clip.rootMotion()) {
        distance += frame.z;
      }
      if (entry.name == "WK0.LWR") {
        walking_root_frames = clip.rootMotion().size();
        walking_root_distance = distance;
      } else {
        running_root_frames = clip.rootMotion().size();
        running_root_distance = distance;
      }
    }
  }
  const auto hmd_model_count = static_cast<std::size_t>(std::ranges::count_if(
      gameplay.objectModels(), [](const sf::game::ObjectModel &model) {
        return std::holds_alternative<sf::assets::HmdModel>(model.geometry);
      }));

  const auto vlf = mission.archive().file("VLF.RFF");
  const auto byte = [&vlf](std::size_t index) {
    return std::to_integer<std::uint32_t>(vlf[index]);
  };
  const auto vlf_mask =
      byte(0) | (byte(1) << 8U) | (byte(2) << 16U) | (byte(3) << 24U);
  auto vram_conflict_rooms = std::size_t{};
  std::vector<std::string> vram_conflict_names;
  std::vector<std::string> vram_conflict_details;
  std::vector<std::string> vram_alias_remap_names;
  std::vector<std::string> no_effect_page_names;
  auto minimum_free_effect_pages = std::numeric_limits<std::size_t>::max();
  constexpr std::array effect_pages{
      31U, 30U, 29U, 28U, 27U, 26U, 25U, 24U, 23U, 22U,
      15U, 14U, 13U, 12U, 11U, 9U,  8U,  7U,  6U,
  };
  for (std::size_t room = 0; room < world_scenes.size(); ++room) {
    struct SlotOwner {
      int page{-1};
      int bank{-4};
    };
    std::array<SlotOwner, 32> slots{};
    std::array<unsigned int, 32> remap{};
    for (unsigned int page = 0; page < remap.size(); ++page) {
      remap[page] = (page & 15U) < 6U ? page + 6U : page;
    }
    auto conflict = false;
    auto alias_remapped = false;
    std::vector<std::string> room_conflicts;
    const auto canonical_bank = [&](int bank) {
      return bank >= 0 && mission.textureBankCount() == 1U ? 0 : bank;
    };
    const auto page_bytes = [&](unsigned int page,
                                int bank) -> std::span<const std::byte> {
      constexpr std::size_t texture_page_size = 64U * 256U * 2U;
      if (bank == -2) {
        const auto preceding = page == 0U ? 0U : vlf_mask & ((1U << page) - 1U);
        return vlf.subspan(static_cast<std::size_t>(std::popcount(preceding)) *
                               texture_page_size,
                           texture_page_size);
      }
      auto name = std::string{"TP"};
      if (page < 10U) {
        name.push_back('0');
      }
      name += std::to_string(page) + ".BIN";
      return mission.textureBank(static_cast<std::size_t>(canonical_bank(bank)))
          .file(name);
    };
    const auto require_page = [&](unsigned int page, int bank) {
      page &= 0x1fU;
      bank = canonical_bank(bank);
      auto physical = remap[page];
      const auto source_bank = (vlf_mask & (1U << page)) != 0U ? -2 : bank;
      auto *owner = &slots[physical];
      if (owner->page >= 0 && (owner->page != static_cast<int>(page) ||
                               owner->bank != source_bank)) {
        constexpr unsigned int escape_page = 21U;
        if (slots[escape_page].page < 0) {
          remap[page] = escape_page;
          physical = escape_page;
          owner = &slots[physical];
          alias_remapped = true;
        } else {
          conflict = true;
          room_conflicts.push_back(
              "slot" + std::to_string(physical) + "=" +
              std::to_string(owner->page) + "/" + std::to_string(owner->bank) +
              " vs " + std::to_string(page) + "/" +
              std::to_string(source_bank) +
              (std::ranges::equal(
                   page_bytes(static_cast<unsigned int>(owner->page),
                              owner->bank),
                   page_bytes(page, source_bank))
                   ? " equal"
                   : " different"));
          return;
        }
      }
      *owner = SlotOwner{static_cast<int>(page), source_bank};
    };
    const auto require_mask = [&](std::uint32_t mask, int bank) {
      for (unsigned int page = 0; page < 32U; ++page) {
        if ((mask & (1U << page)) != 0U) {
          require_page(page, bank);
        }
      }
    };
    std::vector<std::uint16_t> active_rooms{static_cast<std::uint16_t>(room)};
    for (const auto model : mission.layout().visibility(room).active_models) {
      if (std::ranges::find(active_rooms, model) == active_rooms.end()) {
        active_rooms.push_back(model);
      }
    }
    for (const auto model : active_rooms) {
      const auto &scene = world_scenes[model];
      require_mask(scene.texturePageMask(), scene.textureBank());
    }
    const auto object_bank = static_cast<int>(world_scenes[room].textureBank());
    const auto require_geometry =
        [&](const sf::game::ObjectGeometry &geometry) {
          if (const auto *gmd = std::get_if<sf::assets::GmdModel>(&geometry)) {
            require_mask(gmd->texturePageMask(), object_bank);
          } else if (const auto *hmd =
                         std::get_if<sf::assets::HmdModel>(&geometry)) {
            require_mask(hmd->texturePageMask(), object_bank);
          } else if (const auto *emd =
                         std::get_if<sf::assets::EmdScene>(&geometry)) {
            require_mask(emd->texturePageMask(), emd->textureBank());
          }
        };
    for (const auto active_room : active_rooms) {
      for (const auto source : mission.objects().objectsInRoom(active_room)) {
        const auto object = std::ranges::find_if(
            gameplay.objects(),
            [source](const sf::game::SceneObject &candidate) {
              return candidate.source_index == source;
            });
        if (object != gameplay.objects().end()) {
          require_geometry(gameplay.objectModels()[object->model].geometry);
        }
      }
    }
    require_geometry(gameplay.playerModel().geometry);
    if (const auto *weapon =
            gameplay.weaponModel(gameplay.hud().inventory().current())) {
      require_geometry(weapon->geometry);
    }
    const auto free_pages = static_cast<std::size_t>(
        std::ranges::count_if(effect_pages, [&slots](unsigned int page) {
          return slots[page].page < 0;
        }));
    minimum_free_effect_pages = std::min(minimum_free_effect_pages, free_pages);
    vram_conflict_rooms += conflict ? 1U : 0U;
    if (conflict) {
      vram_conflict_names.push_back(std::to_string(room) + ":" +
                                    mission.worldModels().entries()[room].name);
      vram_conflict_details.push_back(std::to_string(room) + ":" +
                                      room_conflicts.front());
    }
    if (alias_remapped) {
      vram_alias_remap_names.push_back(
          std::to_string(room) + ":" +
          mission.worldModels().entries()[room].name);
    }
    if (free_pages == 0U) {
      no_effect_page_names.push_back(
          std::to_string(room) + ":" +
          mission.worldModels().entries()[room].name);
    }
  }

  auto script_program_count = std::size_t{};
  auto script_event_count = std::size_t{};
  if (mission.missionScripts()) {
    script_program_count = mission.missionScripts()->programs().size();
    for (const auto &program : mission.missionScripts()->programs()) {
      script_event_count += program.events.size();
    }
  }

  std::cout << "Mission:      " << definition.index << " - " << definition.title
            << '\n'
            << "Resource:     " << definition.resource_name << '\n'
            << "Overlay:      " << definition.overlay_name << '\n'
            << "Opening:      " << definition.opening_movie_path << '\n'
            << "FOG files:    " << mission.archive().entries().size() << '\n'
            << "Textures:     " << mission.textureFileCount() << '\n'
            << "World models: " << mission.worldModelCount() << '\n'
            << "EMD sections: " << section_count << '\n'
            << "EMD vertices: " << vertex_count << '\n'
            << "EMD polygons: " << polygon_count << '\n'
            << "Script programs/events: " << script_program_count << '/'
            << script_event_count << '\n'
            << "Native objects: " << gameplay.objects().size() << '\n'
            << "Active objects: " << gameplay.activeObjects().size() << '\n'
            << "Player source:  " << mission.objects().playerIndex() << " @ "
            << mission.objects().player().transform.x << ','
            << -mission.objects().player().transform.y << ','
            << mission.objects().player().transform.z << " yaw "
            << gameplay.player().yaw << '\n'
            << "Initial weapon:  "
            << static_cast<unsigned int>(gameplay.hud().inventory().current())
            << " (" << gameplay.hud().inventory().currentDefinition().name
            << "), armor " << gameplay.hud().vitals().armor << ", timer "
            << (gameplay.nativeMissionTimerSeconds()
                    ? std::to_string(*gameplay.nativeMissionTimerSeconds())
                    : std::string{"none"})
            << ", model "
            << (gameplay.weaponModel(gameplay.hud().inventory().current()) !=
                        nullptr
                    ? "loaded"
                    : "none")
            << '\n'
            << "Initial room:   " << mission.layout().initialRoom() << '\n'
            << "HMD models:     " << hmd_model_count << '\n'
            << "VRAM conflicts: " << vram_conflict_rooms << '\n'
            << "VRAM alias remaps: " << vram_alias_remap_names.size() << '\n'
            << "Min free effect pages: " << minimum_free_effect_pages << "\n\n"
            << "Actor clips:    " << animation_clip_count << " ("
            << animation_frame_count << " frames validated)\n"
            << "Root motion:    WK0 " << walking_root_frames << "/"
            << walking_root_distance << ", RN0 " << running_root_frames << "/"
            << running_root_distance << " (frames/world units)\n\n"
            << "name,start_sector,sector_count,size\n";
  std::cout << "Initial visibility:";
  for (const auto model : mission.layout()
                              .visibility(mission.layout().initialRoom())
                              .active_models) {
    std::cout << ' ' << model;
  }
  std::cout << "\nResident models:";
  for (const auto model : mission.layout().residentModels()) {
    std::cout << ' ' << model;
  }
  std::cout << "\nResident visibility:";
  for (const auto resident : mission.layout().residentModels()) {
    std::cout << " [" << resident << ':';
    for (const auto model :
         mission.layout().visibility(resident).active_models) {
      std::cout << ' ' << model;
    }
    std::cout << ']';
  }
  std::cout << "\nInitial model bounds:\n";
  for (const auto model : gameplay.activeModels()) {
    if (model >= gameplay.models().size()) {
      continue;
    }
    const auto &world = gameplay.models()[model];
    std::cout << model << ',' << world.name << ',' << world.bounds.minimum_x
              << ',' << world.bounds.minimum_y << ',' << world.bounds.minimum_z
              << ',' << world.bounds.maximum_x << ',' << world.bounds.maximum_y
              << ',' << world.bounds.maximum_z << '\n';
  }
  std::cout << "Object definitions:\n";
  for (std::size_t index = 0; index < mission.objects().definitions().size();
       ++index) {
    const auto &object_definition = mission.objects().definitions()[index];
    const auto object_count = static_cast<std::size_t>(
        std::ranges::count_if(mission.objects().objects(),
                              [index](const sf::assets::MissionObject &object) {
                                return object.type == index;
                              }));
    std::cout << index << ",0x" << std::hex << object_definition.class_id
              << std::dec << ',' << object_count << ','
              << object_definition.primary_model << ','
              << object_definition.secondary_model << '\n';
  }
  std::cout << "Mission objects:\n";
  for (std::size_t index = 0; index < mission.objects().objects().size();
       ++index) {
    const auto &object = mission.objects().objects()[index];
    const auto &object_definition = mission.objects().definition(object.type);
    auto room = std::numeric_limits<std::size_t>::max();
    for (std::size_t candidate = 0; candidate < world_scenes.size();
         ++candidate) {
      if (std::ranges::find(mission.objects().objectsInRoom(candidate),
                            index) !=
          mission.objects().objectsInRoom(candidate).end()) {
        room = candidate;
        break;
      }
    }
    std::cout << index << ','
              << (room == std::numeric_limits<std::size_t>::max()
                      ? -1
                      : static_cast<int>(room))
              << ",0x" << std::hex << object_definition.class_id << std::dec
              << ',' << object_definition.primary_model << ','
              << object_definition.secondary_model << ','
              << object.maximum_health << ',' << object.health << ",0x"
              << std::hex << object.attributes << ",0x" << object.ai_parameter
              << ",0x" << object.path_data_offset << std::dec << ','
              << object.linked_object << ',' << object.transform.x << ','
              << object.transform.y << ',' << object.transform.z << ','
              << object.transform.rotation[2] << ','
              << object.transform.rotation[8] << ','
              << object.patrol_path.size() << ','
              << (object.patrol_path_loops ? 1 : 0) << ','
              << static_cast<unsigned int>(object.patrol_loop_start);
    for (const auto &point : object.patrol_path) {
      std::cout << ',' << point.x << ':' << point.y << ':' << point.z;
    }
    std::cout << '\n';
  }
  std::cout << "Special effects:\n";
  for (const auto &entry : mission.specialEffects().entries()) {
    std::cout << entry.name << ',' << entry.size << '\n';
  }
  if (!vram_conflict_names.empty() || !no_effect_page_names.empty()) {
    std::cout << "VRAM conflict rooms:";
    for (const auto &name : vram_conflict_names) {
      std::cout << ' ' << name;
    }
    std::cout << "\nVRAM conflict details:";
    for (const auto &detail : vram_conflict_details) {
      std::cout << ' ' << detail;
    }
    std::cout << "\nNo CFIRE scratch page rooms:";
    for (const auto &name : no_effect_page_names) {
      std::cout << ' ' << name;
    }
    std::cout << "\n\n";
  }
  if (!vram_alias_remap_names.empty()) {
    std::cout << "VRAM alias-remap rooms:";
    for (const auto &name : vram_alias_remap_names) {
      std::cout << ' ' << name;
    }
    std::cout << "\n\n";
  }
  for (const auto &entry : mission.archive().entries()) {
    std::cout << entry.name << ',' << entry.start_sector << ','
              << entry.sector_count << ',' << entry.size << '\n';
  }
  return 0;
}

std::vector<std::uint32_t>
sequelOverlayExecutableSeeds(sf::game::GameDisc &disc);
std::vector<std::pair<std::uint32_t, std::uint32_t>>
embeddedArchiveExactDataRanges(std::span<const std::byte> text,
                               std::uint32_t load_address);

int mapFunctions(const char *cue_path, const char *output_path) {
  const auto disc = openDisc(cue_path);
  const auto &executable = disc.executable();
  const auto &header = executable.header();
  auto mutable_disc = openDisc(cue_path);
  const auto overlay_seeds = sequelOverlayExecutableSeeds(mutable_disc);
  const auto candidates = sf::psx::fingerprintFunctionCandidates(
      executable.text(), header.text_address, header.initial_pc, true,
      overlay_seeds);
  const std::set overlay_seed_set(overlay_seeds.begin(), overlay_seeds.end());
  const auto asset_ranges =
      embeddedArchiveExactDataRanges(executable.text(), header.text_address);
  const auto in_asset_range = [&](std::uint32_t address) {
    return std::ranges::any_of(asset_ranges, [&](const auto &range) {
      return address >= range.first && address < range.second;
    });
  };

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open function-map output"};
  }
  output
      << "address,static_call_sites,instructions,direct_callees,has_return,"
         "overlay_referenced,embedded_asset_range,seed_evidence,exact_sha256,"
         "structural_sha256\n"
      << std::hex << std::uppercase;
  for (const auto &candidate : candidates) {
    output << "0x" << candidate.address << ',' << std::dec
           << candidate.static_call_count << ',' << candidate.instruction_count
           << ',' << candidate.direct_callee_count << ','
           << (candidate.has_return ? 1 : 0) << ','
           << (overlay_seed_set.contains(candidate.address) ? 1 : 0) << ','
           << (in_asset_range(candidate.address) ? 1 : 0) << ',';
    if (candidate.address == header.initial_pc) {
      output << "entry";
    } else if (candidate.static_call_count != 0U &&
               overlay_seed_set.contains(candidate.address)) {
      output << "direct_call+overlay";
    } else if (candidate.static_call_count != 0U) {
      output << "direct_call";
    } else if (overlay_seed_set.contains(candidate.address)) {
      output << "overlay";
    } else {
      output << "prologue_only";
    }
    output << ',' << candidate.exact_sha256 << ','
           << candidate.structural_sha256 << '\n'
           << std::hex;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write function map"};
  }
  std::cout << "Wrote " << candidates.size() << " function seeds to "
            << output_path << '\n';
  return 0;
}

int mapFunctionUnion(const char *left_cue_path, const char *right_cue_path,
                     const char *output_path) {
  auto left = openDisc(left_cue_path);
  auto right = openDisc(right_cue_path);
  if (left.executableHash() != right.executableHash()) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_argument,
        "Function-union mapping requires byte-identical executables"};
  }
  const auto left_seeds = sequelOverlayExecutableSeeds(left);
  const auto right_seeds = sequelOverlayExecutableSeeds(right);
  std::set<std::uint32_t> union_set(left_seeds.begin(), left_seeds.end());
  union_set.insert(right_seeds.begin(), right_seeds.end());
  const std::vector<std::uint32_t> union_seeds(union_set.begin(),
                                               union_set.end());
  const std::set<std::uint32_t> left_set(left_seeds.begin(), left_seeds.end());
  const std::set<std::uint32_t> right_set(right_seeds.begin(),
                                          right_seeds.end());
  const auto &executable = left.executable();
  const auto &header = executable.header();
  const auto candidates = sf::psx::fingerprintFunctionCandidates(
      executable.text(), header.text_address, header.initial_pc, true,
      union_seeds);
  const auto asset_ranges =
      embeddedArchiveExactDataRanges(executable.text(), header.text_address);
  const auto in_asset_range = [&](std::uint32_t address) {
    return std::ranges::any_of(asset_ranges, [&](const auto &range) {
      return address >= range.first && address < range.second;
    });
  };

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open function-union map output"};
  }
  output << "address,static_call_sites,instructions,direct_callees,has_return,"
            "left_overlay_referenced,right_overlay_referenced,"
            "embedded_asset_range,seed_evidence,exact_sha256,"
            "structural_sha256\n"
         << std::hex << std::uppercase;
  for (const auto &candidate : candidates) {
    const auto left_overlay = left_set.contains(candidate.address);
    const auto right_overlay = right_set.contains(candidate.address);
    output << "0x" << candidate.address << ',' << std::dec
           << candidate.static_call_count << ',' << candidate.instruction_count
           << ',' << candidate.direct_callee_count << ','
           << (candidate.has_return ? 1 : 0) << ',' << (left_overlay ? 1 : 0)
           << ',' << (right_overlay ? 1 : 0) << ','
           << (in_asset_range(candidate.address) ? 1 : 0) << ',';
    if (candidate.address == header.initial_pc) {
      output << "entry";
    } else if (candidate.static_call_count != 0U &&
               (left_overlay || right_overlay)) {
      output << "direct_call+overlay";
    } else if (candidate.static_call_count != 0U) {
      output << "direct_call";
    } else if (left_overlay || right_overlay) {
      output << "overlay";
    } else {
      output << "prologue_only";
    }
    output << ',' << candidate.exact_sha256 << ','
           << candidate.structural_sha256 << '\n'
           << std::hex;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write function-union map"};
  }
  std::cout << "Wrote " << candidates.size()
            << " union function seeds from byte-identical executables to "
            << output_path << '\n';
  return 0;
}

int mapFunctionCalls(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto &executable = disc.executable();
  const auto &header = executable.header();
  const auto overlay_seeds = sequelOverlayExecutableSeeds(disc);
  const auto functions = sf::psx::fingerprintFunctionCandidates(
      executable.text(), header.text_address, header.initial_pc, true,
      overlay_seeds);
  const auto calls =
      sf::psx::discoverDirectCalls(executable.text(), header.text_address);
  const auto asset_ranges =
      embeddedArchiveExactDataRanges(executable.text(), header.text_address);
  const auto in_asset_range = [&](std::uint32_t address) {
    return std::ranges::any_of(asset_ranges, [&](const auto &range) {
      return address >= range.first && address < range.second;
    });
  };

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open function-call map output"};
  }
  output << "caller_address,site,target,target_in_executable,"
            "site_in_embedded_asset_range\n"
         << std::hex << std::uppercase;
  std::size_t mapped_calls{};
  for (const auto &call : calls) {
    const auto next = std::ranges::upper_bound(
        functions, call.site, {}, &sf::psx::FunctionFingerprint::address);
    if (next == functions.begin()) {
      continue;
    }
    const auto &function = *std::prev(next);
    const auto function_end =
        function.address +
        static_cast<std::uint32_t>(function.instruction_count * 4U);
    if (call.site >= function_end) {
      continue;
    }
    output << "0x" << function.address << ",0x" << call.site << ",0x"
           << call.target << ',' << std::dec << (call.target_in_text ? 1 : 0)
           << ',' << (in_asset_range(call.site) ? 1 : 0) << '\n'
           << std::hex;
    ++mapped_calls;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write function-call map"};
  }
  std::cout << "Mapped " << mapped_calls << " direct calls to " << output_path
            << '\n';
  return 0;
}

int compareFunctions(const char *left_cue_path, const char *right_cue_path,
                     const char *output_path) {
  const auto left_disc = openDisc(left_cue_path);
  const auto right_disc = openDisc(right_cue_path);
  const auto &left_executable = left_disc.executable();
  const auto &right_executable = right_disc.executable();
  auto left_seed_disc = openDisc(left_cue_path);
  auto right_seed_disc = openDisc(right_cue_path);
  const auto left_overlay_seeds = sequelOverlayExecutableSeeds(left_seed_disc);
  const auto right_overlay_seeds =
      sequelOverlayExecutableSeeds(right_seed_disc);
  const auto left = sf::psx::fingerprintFunctionCandidates(
      left_executable.text(), left_executable.header().text_address,
      left_executable.header().initial_pc, true, left_overlay_seeds);
  const auto right = sf::psx::fingerprintFunctionCandidates(
      right_executable.text(), right_executable.header().text_address,
      right_executable.header().initial_pc, true, right_overlay_seeds);

  std::map<std::string, std::vector<const sf::psx::FunctionFingerprint *>>
      right_by_hash;
  for (const auto &function : right) {
    right_by_hash[function.structural_sha256].push_back(&function);
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open function comparison output"};
  }
  output << "left_address,right_address,instructions,match_kind,"
            "structural_sha256\n"
         << std::hex << std::uppercase;
  std::size_t exact_matches{};
  std::size_t structural_matches{};
  for (const auto &left_function : left) {
    const auto found = right_by_hash.find(left_function.structural_sha256);
    if (found == right_by_hash.end() || found->second.size() != 1U) {
      continue;
    }
    const auto &right_function = *found->second.front();
    if (right_function.instruction_count != left_function.instruction_count) {
      continue;
    }
    const auto exact =
        right_function.exact_sha256 == left_function.exact_sha256;
    exact_matches += exact ? 1U : 0U;
    structural_matches += exact ? 0U : 1U;
    output << "0x" << left_function.address << ",0x" << right_function.address
           << ',' << std::dec << left_function.instruction_count << ','
           << (exact ? "exact" : "structural") << ','
           << left_function.structural_sha256 << '\n'
           << std::hex;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write function comparison"};
  }
  std::cout << "Matched " << exact_matches << " exact and "
            << structural_matches << " structurally equivalent unique "
            << "function seeds to " << output_path << '\n';
  return 0;
}

std::size_t sequelOverlayCodeOffset(std::span<const std::byte> bytes) {
  const auto read_le32 = [&bytes](std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
  };
  for (std::size_t offset = 0x20U;
       offset + sizeof(std::uint32_t) <= bytes.size() && offset < 0x100U;
       offset += sizeof(std::uint32_t)) {
    const auto instruction = read_le32(offset);
    const auto opcode = instruction >> 26U;
    const auto source = (instruction >> 21U) & 0x1fU;
    const auto target = (instruction >> 16U) & 0x1fU;
    const auto immediate = instruction & 0xffffU;
    if ((opcode == 0x09U || opcode == 0x08U) && source == 29U &&
        target == 29U && (immediate & 0x8000U) != 0U) {
      return offset;
    }
  }
  for (std::size_t offset = 0x28U;
       offset + sizeof(std::uint32_t) <= bytes.size() && offset < 0x100U;
       offset += sizeof(std::uint32_t)) {
    if (read_le32(offset) == 0x03e00008U) {
      return offset;
    }
  }
  for (std::size_t offset = 0x28U;
       offset + sizeof(std::uint32_t) <= bytes.size() && offset < 0x100U;
       offset += sizeof(std::uint32_t)) {
    const auto instruction = read_le32(offset);
    if (instruction >= 0x00010000U && instruction != 0xfffffffeU &&
        instruction != 0xcdcdcdcdU) {
      return offset;
    }
  }
  throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                        "Could not locate sequel overlay code prologue"};
}

constexpr std::size_t sequelMissionOverlayHeaderSize = 0x28U;
constexpr std::uint32_t sequelMissionOverlayLoadAddress = 0x8014b978U;

std::uint32_t sequelMissionOverlayCodeAddress(std::size_t code_offset) {
  if (code_offset < sequelMissionOverlayHeaderSize) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "Sequel mission-overlay code precedes its fixed file header"};
  }
  // The loader removes the fixed ten-word OVL header, but preserves any
  // mission-local data between that header and the first function.  Mapping
  // every inferred code span directly at 0x8014b978 consequently shifted
  // COLO by 0x10, HWAY by 0x08, TRAIN by 0x30, and other data-prefixed
  // overlays by their respective pre-code spans.  Absolute callback pointers
  // embedded in those overlays prove the corrected address relationship.
  return sequelMissionOverlayLoadAddress +
         static_cast<std::uint32_t>(code_offset -
                                    sequelMissionOverlayHeaderSize);
}

std::size_t sequelOverlayContentSize(std::span<const std::byte> bytes,
                                     std::size_t code_offset) {
  auto end = bytes.size();
  while (end > code_offset && (bytes[end - 1U] == std::byte{0xcd} ||
                               bytes[end - 1U] == std::byte{0})) {
    --end;
  }
  end = std::min(bytes.size(), (end + 3U) & ~std::size_t{3U});
  return end - code_offset;
}

struct ResidentOverlayDefinition {
  std::string_view name;
  std::uint32_t load_address;
};

std::vector<ResidentOverlayDefinition>
sequelResidentOverlayDefinitions(sf::game::GameId game) {
  if (game == sf::game::GameId::syphon_filter_2) {
    // TITLE and INIT are adjacent in the retail memory layout:
    // 0x8014b950 + sizeof(TITLE.OVL) == 0x80158878.
    return {
        {"MENU.OVL", 0x80142150U},
        {"MOVIE.OVL", 0x80142150U},
        {"TITLE.OVL", 0x8014b950U},
        {"INIT.OVL", 0x80158878U},
    };
  }
  if (game == sf::game::GameId::syphon_filter_3) {
    // SF3's larger TITLE2 image ends exactly where INIT begins:
    // 0x80150950 + sizeof(TITLE2.OVL) == 0x8015e978.
    return {
        {"MENU.OVL", 0x80146950U},   {"MENU2.OVL", 0x80146950U},
        {"MOVIE.OVL", 0x80146950U},  {"TITLE.OVL", 0x80150950U},
        {"TITLE2.OVL", 0x80150950U}, {"INIT.OVL", 0x8015e978U},
    };
  }
  return {};
}

std::size_t sequelResidentOverlayCodeOffset(std::span<const std::byte> bytes,
                                            std::uint32_t load_address) {
  auto first_internal_target = bytes.size();
  for (const auto &call : sf::psx::discoverDirectCalls(bytes, load_address)) {
    if (call.target_in_text) {
      first_internal_target =
          std::min(first_internal_target,
                   static_cast<std::size_t>(call.target - load_address));
    }
  }
  if (first_internal_target == bytes.size()) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Resident overlay has no internal code target"};
  }
  return first_internal_target;
}

std::span<const std::byte>
readResidentOverlay(sf::game::GameDisc &disc,
                    const ResidentOverlayDefinition &definition,
                    std::vector<std::byte> &storage) {
  storage = disc.image().readFile("BIN/" + std::string{definition.name});
  return storage;
}

std::vector<std::uint32_t>
sequelOverlayExecutableSeeds(sf::game::GameDisc &disc) {
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    return {};
  }
  std::set<std::uint32_t> result;
  const auto text_begin = disc.executable().header().text_address;
  const auto text_end = text_begin + disc.executable().header().text_size;
  for (const auto &definition :
       sequelResidentOverlayDefinitions(disc.game()->id)) {
    std::vector<std::byte> storage;
    const auto overlay_bytes = readResidentOverlay(disc, definition, storage);
    const auto code_offset =
        sequelResidentOverlayCodeOffset(overlay_bytes, definition.load_address);
    const auto code_size = sequelOverlayContentSize(overlay_bytes, code_offset);
    for (const auto &call : sf::psx::discoverDirectCalls(
             overlay_bytes.subspan(code_offset, code_size),
             definition.load_address +
                 static_cast<std::uint32_t>(code_offset))) {
      if (!call.target_in_text && call.target >= text_begin &&
          call.target < text_end) {
        result.insert(call.target);
      }
    }
  }
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto specific_overlay = std::string{resource.resource_name} + ".OVL";
    const auto has_specific_overlay =
        std::ranges::any_of(archive.entries(), [&](const auto &entry) {
          return entry.name == specific_overlay;
        });
    const auto overlay_name =
        has_specific_overlay ? specific_overlay : std::string{"GENERIC.OVL"};
    const auto overlay_bytes = archive.file(overlay_name);
    const auto code_offset = sequelOverlayCodeOffset(overlay_bytes);
    const auto code_size = sequelOverlayContentSize(overlay_bytes, code_offset);
    const auto code_address = sequelMissionOverlayCodeAddress(code_offset);
    for (const auto &call : sf::psx::discoverDirectCalls(
             overlay_bytes.subspan(code_offset, code_size), code_address)) {
      if (!call.target_in_text && call.target >= text_begin &&
          call.target < text_end) {
        result.insert(call.target);
      }
    }
  }
  return {result.begin(), result.end()};
}

int mapResidentOverlays(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Resident-overlay mapping requires a recognized sequel disc"};
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open resident-overlay map output"};
  }
  output << "overlay,file_size,load_address,code_offset,code_size,"
            "function_address,instructions,static_call_sites,direct_callees,"
            "has_return,exact_sha256,structural_sha256,external_targets\n";

  std::size_t overlay_count{};
  std::size_t function_count{};
  for (const auto &definition :
       sequelResidentOverlayDefinitions(disc.game()->id)) {
    std::vector<std::byte> storage;
    const auto overlay_bytes = readResidentOverlay(disc, definition, storage);
    const auto code_offset =
        sequelResidentOverlayCodeOffset(overlay_bytes, definition.load_address);
    const auto code_size = sequelOverlayContentSize(overlay_bytes, code_offset);
    const auto code = overlay_bytes.subspan(code_offset, code_size);
    const auto code_address =
        definition.load_address + static_cast<std::uint32_t>(code_offset);
    const auto functions = sf::psx::fingerprintFunctionCandidates(
        code, code_address, code_address, true);
    const auto calls = sf::psx::discoverDirectCalls(code, code_address);

    for (const auto &function : functions) {
      std::set<std::uint32_t> external_targets;
      const auto function_end =
          function.address +
          static_cast<std::uint32_t>(function.instruction_count * 4U);
      for (const auto &call : calls) {
        if (call.site >= function.address && call.site < function_end &&
            !call.target_in_text) {
          external_targets.insert(call.target);
        }
      }
      output << definition.name << ',' << overlay_bytes.size() << ",0x"
             << std::hex << std::uppercase << definition.load_address
             << std::dec << ',' << code_offset << ',' << code_size << ",0x"
             << std::hex << std::uppercase << function.address << std::dec
             << ',' << function.instruction_count << ','
             << function.static_call_count << ','
             << function.direct_callee_count << ','
             << (function.has_return ? 1 : 0) << ',' << function.exact_sha256
             << ',' << function.structural_sha256 << ',';
      auto first = true;
      for (const auto target : external_targets) {
        output << (first ? "" : ";") << "0x" << std::hex << std::uppercase
               << target << std::dec;
        first = false;
      }
      output << '\n';
      ++function_count;
    }
    ++overlay_count;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write resident-overlay map"};
  }
  std::cout << "Mapped " << function_count << " function seeds across "
            << overlay_count << " resident overlays to " << output_path << '\n';
  return 0;
}

int mapMissionOverlays(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission overlay mapping requires a recognized sequel disc"};
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-overlay map output"};
  }
  output << "mission,resource,overlay,file_size,load_address,code_offset,"
            "code_address,code_size,function_address,instructions,"
            "static_call_sites,direct_callees,has_return,exact_sha256,"
            "structural_sha256,external_targets\n";

  std::size_t mission_count{};
  std::size_t function_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto specific_overlay = std::string{resource.resource_name} + ".OVL";
    const auto has_specific_overlay =
        std::ranges::any_of(archive.entries(), [&](const auto &entry) {
          return entry.name == specific_overlay;
        });
    const auto overlay_name =
        has_specific_overlay ? specific_overlay : std::string{"GENERIC.OVL"};
    const auto overlay_bytes = archive.file(overlay_name);
    const auto code_offset = sequelOverlayCodeOffset(overlay_bytes);
    const auto code_size = sequelOverlayContentSize(overlay_bytes, code_offset);
    const auto code = overlay_bytes.subspan(code_offset, code_size);
    const auto code_address = sequelMissionOverlayCodeAddress(code_offset);
    const auto functions = sf::psx::fingerprintFunctionCandidates(
        code, code_address, code_address, true);
    const auto calls = sf::psx::discoverDirectCalls(code, code_address);

    for (const auto &function : functions) {
      std::set<std::uint32_t> external_targets;
      const auto function_end =
          function.address +
          static_cast<std::uint32_t>(function.instruction_count * 4U);
      for (const auto &call : calls) {
        if (call.site >= function.address && call.site < function_end &&
            !call.target_in_text) {
          external_targets.insert(call.target);
        }
      }
      output << resource.selection_index << ',' << resource.resource_name << ','
             << overlay_name << ',' << overlay_bytes.size() << ',' << "0x"
             << std::hex << std::uppercase << sequelMissionOverlayLoadAddress
             << std::dec << ',' << code_offset << ",0x" << std::hex
             << std::uppercase << code_address << std::dec << ',' << code_size
             << ",0x" << std::hex << std::uppercase << function.address
             << std::dec << ',' << function.instruction_count << ','
             << function.static_call_count << ','
             << function.direct_callee_count << ','
             << (function.has_return ? 1 : 0) << ',' << function.exact_sha256
             << ',' << function.structural_sha256 << ',';
      auto first = true;
      for (const auto target : external_targets) {
        output << (first ? "" : ";") << "0x" << std::hex << std::uppercase
               << target << std::dec;
        first = false;
      }
      output << '\n';
      ++function_count;
    }
    ++mission_count;
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-overlay map"};
  }
  std::cout << "Mapped " << function_count << " function seeds across "
            << mission_count << " mission overlays to " << output_path << '\n';
  return 0;
}

void writeCsvString(std::ostream &output, std::string_view value);

int mapMissionClasses(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-class mapping requires a recognized sequel disc"};
  }
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-class map output"};
  }
  output << "mission,resource,definition,class_id,class_family,class_flags,"
            "instances,primary_model,secondary_model\n";
  std::size_t definition_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto objects = sf::assets::MissionObjects::parse(
        archive.file(std::string{resource.resource_name} + ".BIN"));
    for (std::size_t index = 0; index < objects.definitions().size(); ++index) {
      const auto &definition = objects.definitions()[index];
      const auto instances = static_cast<std::size_t>(std::ranges::count(
          objects.objects(), index, &sf::assets::MissionObject::type));
      const auto family = definition.class_id & 0xffffU;
      const auto flags = definition.class_id & 0xffff0000U;
      output << resource.selection_index << ',' << resource.resource_name << ','
             << index << ",0x" << std::hex << std::uppercase
             << definition.class_id << ",0x" << family << ",0x" << flags
             << std::dec << ',' << instances << ',';
      writeCsvString(output, definition.primary_model);
      output << ',';
      writeCsvString(output, definition.secondary_model);
      output << '\n';
      ++definition_count;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-class map"};
  }
  std::cout << "Mapped " << definition_count << " object definitions across "
            << sf::game::missionResources(disc.game()->id,
                                          disc.game()->disc_number)
                   .size()
            << " missions to " << output_path << '\n';
  return 0;
}

int mapMissionObjects(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-object mapping requires a recognized sequel disc"};
  }
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-object map output"};
  }
  output << "mission,resource,source,definition,class_id,class_family,"
            "class_flags,primary_model,secondary_model,is_player,rooms,x,y,z,"
            "rotation_2,rotation_8,attributes,ai_parameter,path_data_offset,"
            "linked_object,maximum_health,health,handler_parameter_0,"
            "handler_parameter_1,handler_parameter_2,handler_parameter_3,"
            "handler_state,path_points,path_loops,path_loop_start\n";

  std::size_t object_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto objects = sf::assets::MissionObjects::parse(
        archive.file(std::string{resource.resource_name} + ".BIN"));
    for (std::size_t source = 0; source < objects.objects().size(); ++source) {
      const auto &object = objects.objects()[source];
      const auto &definition = objects.definition(object.type);
      const auto family = definition.class_id & 0xffffU;
      const auto flags = definition.class_id & 0xffff0000U;
      output << resource.selection_index << ',' << resource.resource_name << ','
             << source << ',' << object.type << ",0x" << std::hex
             << std::uppercase << definition.class_id << ",0x" << family
             << ",0x" << flags << std::dec << ',';
      writeCsvString(output, definition.primary_model);
      output << ',';
      writeCsvString(output, definition.secondary_model);
      output << ',' << (source == objects.playerIndex() ? 1 : 0) << ',';
      auto first_room = true;
      for (const auto room : objects.roomsContainingObject(source)) {
        output << (first_room ? "" : ";") << room;
        first_room = false;
      }
      output << ',' << object.transform.x << ',' << object.transform.y << ','
             << object.transform.z << ',' << object.transform.rotation[2] << ','
             << object.transform.rotation[8] << ",0x" << std::hex
             << std::uppercase << object.attributes << ",0x"
             << object.ai_parameter << ",0x" << object.path_data_offset
             << std::dec << ',' << object.linked_object << ','
             << object.maximum_health << ',' << object.health;
      for (const auto parameter : object.handler_parameters) {
        output << ',' << parameter;
      }
      output << ',' << object.handler_state << ',' << object.patrol_path.size()
             << ',' << (object.patrol_path_loops ? 1 : 0) << ','
             << static_cast<unsigned int>(object.patrol_loop_start) << '\n';
      ++object_count;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-object map"};
  }
  std::cout << "Mapped " << object_count << " authored objects across "
            << sf::game::missionResources(disc.game()->id,
                                          disc.game()->disc_number)
                   .size()
            << " missions to " << output_path << '\n';
  return 0;
}

int mapMissionScriptStrings(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script string mapping requires a recognized sequel disc"};
  }
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script string map output"};
  }
  output << "mission,resource,file_size,sha256,string_offset,string\n";
  std::size_t string_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto file_name = std::string{resource.resource_name} + ".SS";
    const auto bytes = archive.file(file_name);
    const auto digest = sf::core::toHex(sf::core::sha256(bytes));
    for (std::size_t offset = 0; offset < bytes.size();) {
      const auto printable = [](std::byte value) {
        const auto character = std::to_integer<unsigned char>(value);
        return character >= 0x20U && character <= 0x7eU;
      };
      if (!printable(bytes[offset])) {
        ++offset;
        continue;
      }
      auto end = offset;
      while (end < bytes.size() && printable(bytes[end])) {
        ++end;
      }
      if (end - offset >= 4U && end < bytes.size() &&
          bytes[end] == std::byte{0}) {
        std::string value;
        value.reserve(end - offset);
        for (auto cursor = offset; cursor < end; ++cursor) {
          value.push_back(
              static_cast<char>(std::to_integer<unsigned char>(bytes[cursor])));
        }
        output << resource.selection_index << ',' << resource.resource_name
               << ',' << bytes.size() << ',' << digest << ',' << offset << ',';
        writeCsvString(output, value);
        output << '\n';
        ++string_count;
      }
      offset = std::max(end, offset + 1U);
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script string map"};
  }
  std::cout << "Mapped " << string_count << " mission-script strings across "
            << sf::game::missionResources(disc.game()->id,
                                          disc.game()->disc_number)
                   .size()
            << " missions to " << output_path << '\n';
  return 0;
}

int mapMissionScripts(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script mapping requires a recognized sequel disc"};
  }
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script map output"};
  }
  output << "mission,resource,file_size,program,record_offset,name,"
            "header_word_0,header_word_1,format_version,variable_count,"
            "serialized_variable_begin,timer_count,event_pointer,"
            "variable_pointer,timer_pointer,name_pointer,pointer_4,pointer_5,"
            "serialized_size,event_count,event_terminator_offset\n";
  std::size_t program_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto file_name = std::string{resource.resource_name} + ".SS";
    const auto bytes = archive.file(file_name);
    const auto scripts = sf::assets::MissionScriptArchive::parse(bytes);
    for (std::size_t index = 0; index < scripts.programs().size(); ++index) {
      const auto &program = scripts.programs()[index];
      output << resource.selection_index << ',' << resource.resource_name << ','
             << bytes.size() << ',' << index << ',' << program.offset << ',';
      writeCsvString(output, program.name);
      output << ",0x" << std::hex << std::uppercase << program.header_word_0
             << ",0x" << program.header_word_1 << std::dec << ','
             << static_cast<unsigned>(program.format_version) << ','
             << static_cast<unsigned>(program.variable_count) << ','
             << static_cast<unsigned>(program.serialized_variable_begin) << ','
             << static_cast<unsigned>(program.timer_count);
      for (const auto pointer : program.relative_pointers) {
        output << ",0x" << std::hex << std::uppercase << pointer;
      }
      output << std::dec << ',' << program.serialized_size << ','
             << program.events.size() << ',' << program.event_terminator_offset
             << '\n';
      ++program_count;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script map"};
  }
  std::cout << "Mapped " << program_count
            << " compiled mission programs across "
            << sf::game::missionResources(disc.game()->id,
                                          disc.game()->disc_number)
                   .size()
            << " missions to " << output_path << '\n';
  return 0;
}

int mapMissionScriptEvents(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script event mapping requires a recognized sequel disc"};
  }
  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script event map output"};
  }
  output << "mission,resource,program,program_name,event_record,"
            "relative_offset,encoded_header,event_id,event_flags,selector,"
            "length_halfwords,action_bytes\n";
  std::size_t event_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto file_name = std::string{resource.resource_name} + ".SS";
    const auto scripts =
        sf::assets::MissionScriptArchive::parse(archive.file(file_name));
    for (std::size_t program_index = 0;
         program_index < scripts.programs().size(); ++program_index) {
      const auto &program = scripts.programs()[program_index];
      for (std::size_t event_index = 0; event_index < program.events.size();
           ++event_index) {
        const auto &event = program.events[event_index];
        output << resource.selection_index << ',' << resource.resource_name
               << ',' << program_index << ',';
        writeCsvString(output, program.name);
        output << ',' << event_index << ',' << event.relative_offset << ",0x"
               << std::hex << std::uppercase << event.encoded_header << std::dec
               << ',' << static_cast<unsigned>(event.event_id) << ",0x"
               << std::hex << std::uppercase
               << static_cast<unsigned>(event.event_flags) << ",0x"
               << event.selector << std::dec << ','
               << static_cast<unsigned>(event.length_halfwords) << ','
               << (static_cast<unsigned>(event.length_halfwords) * 2U - 4U)
               << '\n';
        ++event_count;
      }
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script event map"};
  }
  std::cout << "Mapped " << event_count << " mission-script event records to "
            << output_path << '\n';
  return 0;
}

std::uint32_t readAnalysisWord(std::span<const std::byte> bytes,
                               std::size_t offset) {
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint16_t readAnalysisHalfword(std::span<const std::byte> bytes,
                                   std::size_t offset) {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(bytes[offset]) |
      (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

int mapMissionScriptOpcodes(const char *cue_path, const char *output_path) {
  const auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script opcode mapping requires a recognized sequel disc"};
  }

  // These table roots are proven by the two retail bytecode decoders. Each
  // descriptor is {handler pointer, two 16-bit operand/result metadata words}.
  const auto predicate_table =
      disc.game()->id == sf::game::GameId::syphon_filter_2
          ? std::uint32_t{0x801150fcU}
          : std::uint32_t{0x80117dd0U};
  constexpr std::size_t opcode_count = 64U;
  constexpr std::size_t descriptor_size = 8U;
  constexpr std::size_t table_size = opcode_count * descriptor_size;
  // The action decoder indexes a biased 128-entry descriptor window with
  // encoded high bytes 0x80..0xff. Its two logical 64-entry halves begin
  // 0x180 and 0x380 bytes after the predicate table.
  constexpr std::array<std::uint32_t, 3> table_deltas{0U, 0x180U, 0x380U};

  const auto &executable = disc.executable();
  const auto text = executable.text();
  const auto text_address = executable.header().text_address;
  if (predicate_table < text_address ||
      static_cast<std::uint64_t>(predicate_table - text_address) +
              table_deltas.back() + table_size >
          text.size()) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Mission-script opcode tables are outside executable "
                          "text"};
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script opcode map output"};
  }
  output << "table,table_address,opcode,populated,handler_address,"
            "descriptor_word_0,descriptor_word_1,handler_in_executable\n";
  std::size_t populated{};
  constexpr std::array<std::string_view, 3> table_names{
      "predicate", "action_80_bf", "action_c0_ff"};
  for (std::size_t table_index = 0; table_index < table_deltas.size();
       ++table_index) {
    const auto table_address = predicate_table + table_deltas[table_index];
    const auto table_offset =
        static_cast<std::size_t>(table_address - text_address);
    for (std::size_t opcode = 0; opcode < opcode_count; ++opcode) {
      const auto descriptor_offset = table_offset + opcode * descriptor_size;
      const auto handler = readAnalysisWord(text, descriptor_offset);
      const auto metadata_0 =
          readAnalysisHalfword(text, descriptor_offset + 4U);
      const auto metadata_1 =
          readAnalysisHalfword(text, descriptor_offset + 6U);
      const auto handler_in_executable =
          handler >= text_address &&
          static_cast<std::uint64_t>(handler - text_address) < text.size();
      output << table_names[table_index] << ",0x" << std::hex << std::uppercase
             << table_address << ",0x" << opcode << std::dec << ','
             << (handler != 0U ? 1 : 0) << ",0x" << std::hex << std::uppercase
             << handler << ",0x" << metadata_0 << ",0x" << metadata_1
             << std::dec << ',' << (handler_in_executable ? 1 : 0) << '\n';
      populated += handler != 0U ? 1U : 0U;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script opcode map"};
  }
  std::cout << "Mapped " << populated
            << " populated mission-script opcode descriptors from 0x"
            << std::hex << std::uppercase << predicate_table << std::dec
            << " to " << output_path << '\n';
  return 0;
}

int mapMissionScriptHandlerCalls(const char *cue_path,
                                 const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script handler call mapping requires a recognized sequel "
        "disc"};
  }

  const auto predicate_table =
      disc.game()->id == sf::game::GameId::syphon_filter_2
          ? std::uint32_t{0x801150fcU}
          : std::uint32_t{0x80117dd0U};
  constexpr std::size_t opcode_count = 64U;
  constexpr std::size_t descriptor_size = 8U;
  constexpr std::array<std::uint32_t, 3> table_deltas{0U, 0x180U, 0x380U};
  constexpr std::array<std::string_view, 3> table_names{
      "predicate", "action_80_bf", "action_c0_ff"};

  const auto &executable = disc.executable();
  const auto text = executable.text();
  const auto text_address = executable.header().text_address;
  constexpr auto descriptor_span =
      table_deltas.back() + opcode_count * descriptor_size;
  if (predicate_table < text_address ||
      static_cast<std::uint64_t>(predicate_table - text_address) +
              descriptor_span >
          text.size()) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "Mission-script handler tables are outside executable text"};
  }
  const auto functions = sf::psx::fingerprintFunctionCandidates(
      text, text_address, executable.header().initial_pc, true,
      sequelOverlayExecutableSeeds(disc));
  const auto calls = sf::psx::discoverDirectCalls(text, text_address);

  std::multimap<std::uint32_t, sf::psx::DirectCall> calls_by_function;
  for (const auto &call : calls) {
    const auto next = std::ranges::upper_bound(
        functions, call.site, {}, &sf::psx::FunctionFingerprint::address);
    if (next == functions.begin()) {
      continue;
    }
    const auto &function = *std::prev(next);
    const auto function_end =
        function.address +
        static_cast<std::uint32_t>(function.instruction_count * 4U);
    if (call.site < function_end) {
      calls_by_function.emplace(function.address, call);
    }
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script handler-call map output"};
  }
  output << "table,opcode,handler_address,call_site,target,"
            "target_in_executable\n";
  std::size_t edge_count{};
  std::set<std::pair<std::uint32_t, std::uint32_t>> physical_edges;
  std::set<std::uint32_t> physical_handlers;
  for (std::size_t table_index = 0; table_index < table_deltas.size();
       ++table_index) {
    const auto table_offset = static_cast<std::size_t>(
        predicate_table + table_deltas[table_index] - text_address);
    for (std::size_t opcode = 0; opcode < opcode_count; ++opcode) {
      const auto handler =
          readAnalysisWord(text, table_offset + opcode * descriptor_size);
      const auto [begin, end] = calls_by_function.equal_range(handler);
      for (auto edge = begin; edge != end; ++edge) {
        output << table_names[table_index] << ",0x" << std::hex
               << std::uppercase << opcode << ",0x" << handler << ",0x"
               << edge->second.site << ",0x" << edge->second.target << std::dec
               << ',' << (edge->second.target_in_text ? 1 : 0) << '\n';
        ++edge_count;
        physical_edges.emplace(edge->second.site, edge->second.target);
        physical_handlers.emplace(handler);
      }
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script handler-call map"};
  }
  std::cout << "Mapped " << edge_count << " descriptor-associated rows ("
            << physical_edges.size() << " unique direct calls from "
            << physical_handlers.size() << " physical handlers) to "
            << output_path << '\n';
  return 0;
}

int compareMissionScriptOpcodes(const char *left_cue_path,
                                const char *right_cue_path,
                                const char *output_path) {
  const auto left = openDisc(left_cue_path);
  const auto right = openDisc(right_cue_path);
  const auto table_root = [](const sf::game::GameDisc &disc) {
    if (!disc.game() ||
        (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
         disc.game()->id != sf::game::GameId::syphon_filter_3)) {
      throw sf::core::Error{
          sf::core::ErrorCode::unsupported,
          "Mission-script opcode comparison requires recognized sequel "
          "discs"};
    }
    return disc.game()->id == sf::game::GameId::syphon_filter_2
               ? std::uint32_t{0x801150fcU}
               : std::uint32_t{0x80117dd0U};
  };
  const auto left_root = table_root(left);
  const auto right_root = table_root(right);
  constexpr std::array<std::uint32_t, 3> table_deltas{0U, 0x180U, 0x380U};
  constexpr std::array<std::string_view, 3> table_names{
      "predicate", "action_80_bf", "action_c0_ff"};
  constexpr std::size_t opcode_count = 64U;
  constexpr std::size_t descriptor_size = 8U;

  const auto descriptor = [&](const sf::game::GameDisc &disc,
                              std::uint32_t root, std::uint32_t delta,
                              std::size_t opcode) {
    const auto &executable = disc.executable();
    const auto address =
        root + delta + static_cast<std::uint32_t>(opcode * descriptor_size);
    const auto offset =
        static_cast<std::size_t>(address - executable.header().text_address);
    if (offset > executable.text().size() ||
        executable.text().size() - offset < descriptor_size) {
      throw sf::core::Error{
          sf::core::ErrorCode::invalid_format,
          "Mission-script opcode descriptor is outside executable text"};
    }
    return std::array{readAnalysisWord(executable.text(), offset),
                      static_cast<std::uint32_t>(
                          readAnalysisHalfword(executable.text(), offset + 4U)),
                      static_cast<std::uint32_t>(readAnalysisHalfword(
                          executable.text(), offset + 6U))};
  };

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{
        sf::core::ErrorCode::io,
        "Cannot open mission-script opcode comparison output"};
  }
  output << "table,opcode,left_handler,right_handler,left_word_0,"
            "right_word_0,left_word_1,right_word_1,metadata_equal\n";
  for (std::size_t table = 0; table < table_deltas.size(); ++table) {
    for (std::size_t opcode = 0; opcode < opcode_count; ++opcode) {
      const auto left_descriptor =
          descriptor(left, left_root, table_deltas[table], opcode);
      const auto right_descriptor =
          descriptor(right, right_root, table_deltas[table], opcode);
      output << table_names[table] << ",0x" << std::hex << std::uppercase
             << opcode << ",0x" << left_descriptor[0] << ",0x"
             << right_descriptor[0] << ",0x" << left_descriptor[1] << ",0x"
             << right_descriptor[1] << ",0x" << left_descriptor[2] << ",0x"
             << right_descriptor[2] << std::dec << ','
             << (left_descriptor[1] == right_descriptor[1] &&
                         left_descriptor[2] == right_descriptor[2]
                     ? 1
                     : 0)
             << '\n';
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script opcode comparison"};
  }
  std::cout << "Compared " << table_deltas.size() * opcode_count
            << " aligned mission-script descriptor slots to " << output_path
            << '\n';
  return 0;
}

int mapMissionScriptActions(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Mission-script action mapping requires a recognized sequel disc"};
  }
  const auto predicate_table =
      disc.game()->id == sf::game::GameId::syphon_filter_2
          ? std::uint32_t{0x801150fcU}
          : std::uint32_t{0x80117dd0U};
  const auto descriptor_window = predicate_table + 0x180U;
  const auto &executable = disc.executable();
  const auto executable_text = executable.text();
  const auto text_address = executable.header().text_address;
  const auto descriptor_offset =
      static_cast<std::size_t>(descriptor_window - text_address);
  constexpr std::size_t descriptor_count = 128U;
  constexpr std::size_t descriptor_size = 8U;
  if (descriptor_offset > executable_text.size() ||
      executable_text.size() - descriptor_offset <
          descriptor_count * descriptor_size) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Mission-script action descriptors are invalid"};
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open mission-script action map output"};
  }
  output << "mission,resource,program,program_name,event_record,event_id,"
            "relative_offset,encoded_word,kind,opcode,handler_address,"
            "descriptor_word_0,descriptor_word_1,next_offset,"
            "alternate_offset,operand_0,operand_0_source,operand_1,"
            "operand_1_source,program_operation,target_program,"
            "target_program_name\n";
  std::size_t instruction_count{};
  for (const auto &resource :
       sf::game::missionResources(disc.game()->id, disc.game()->disc_number)) {
    const auto archive_path =
        std::string{disc.game()->layout.mission_archive_directory} + '/' +
        std::string{resource.resource_name} + ".FOG";
    const auto archive =
        sf::assets::FogArchive::parse(disc.image().readFile(archive_path));
    const auto file_name = std::string{resource.resource_name} + ".SS";
    const auto bytes = archive.file(file_name);
    const auto scripts = sf::assets::MissionScriptArchive::parse(bytes);
    for (std::size_t program_index = 0;
         program_index < scripts.programs().size(); ++program_index) {
      const auto &program = scripts.programs()[program_index];
      for (std::size_t event_index = 0; event_index < program.events.size();
           ++event_index) {
        const auto &event = program.events[event_index];
        const auto event_begin =
            static_cast<std::size_t>(event.relative_offset);
        const auto event_end =
            event_begin + static_cast<std::size_t>(event.length_halfwords) * 2U;
        std::vector<std::size_t> pending{event_begin + 4U};
        std::set<std::size_t> visited;
        while (!pending.empty()) {
          const auto instruction_offset = pending.back();
          pending.pop_back();
          if (instruction_offset == event_end ||
              !visited.insert(instruction_offset).second) {
            continue;
          }
          if (instruction_offset < event_begin + 4U ||
              instruction_offset > event_end ||
              event_end - instruction_offset < 2U) {
            throw sf::core::Error{
                sf::core::ErrorCode::invalid_format,
                "Mission-script action control flow leaves event record in " +
                    std::string{resource.resource_name} + '/' + program.name +
                    " event " + std::to_string(event_index) + " at " +
                    std::to_string(instruction_offset) + " (record " +
                    std::to_string(event_begin) + ".." +
                    std::to_string(event_end) + ')'};
          }
          const auto encoded =
              readAnalysisHalfword(bytes, program.offset + instruction_offset);
          const auto high = static_cast<std::uint8_t>(encoded >> 8U);
          const auto low = static_cast<std::uint8_t>(encoded & 0xffU);
          std::string_view kind{"action"};
          std::uint32_t handler{};
          std::uint16_t metadata_0{};
          std::uint16_t metadata_1{};
          auto next_offset = std::numeric_limits<std::size_t>::max();
          auto alternate_offset = std::numeric_limits<std::size_t>::max();
          auto opcode = static_cast<std::uint8_t>(high & 0x7fU);
          std::optional<std::uint16_t> operand_0;
          std::string_view operand_0_source;
          std::optional<std::uint16_t> operand_1;
          std::string_view operand_1_source;
          std::string_view program_operation;
          std::optional<std::size_t> target_program;

          if (high == 0xffU) {
            kind = "end";
          } else if (high == 0xfeU) {
            kind = "predicate_branch";
            if (event_end - instruction_offset < 4U) {
              throw sf::core::Error{
                  sf::core::ErrorCode::invalid_format,
                  "Truncated mission-script predicate branch"};
            }
            next_offset =
                instruction_offset + static_cast<std::size_t>(low) * 2U;
            alternate_offset =
                instruction_offset +
                static_cast<std::size_t>(readAnalysisHalfword(
                    bytes, program.offset + instruction_offset + 2U)) *
                    2U;
            pending.push_back(next_offset);
            pending.push_back(alternate_offset);
          } else if (high == 0xfdU || high == 0xfcU || high == 0xfaU ||
                     high == 0xf9U) {
            kind = high == 0xfdU   ? "text"
                   : high == 0xfcU ? "control"
                   : high == 0xfaU ? "skip"
                                   : "formatted_text";
            next_offset =
                instruction_offset + static_cast<std::size_t>(low) * 2U;
            pending.push_back(next_offset);
          } else {
            const auto descriptor =
                descriptor_offset +
                static_cast<std::size_t>(opcode) * descriptor_size;
            handler = readAnalysisWord(executable_text, descriptor);
            metadata_0 = readAnalysisHalfword(executable_text, descriptor + 4U);
            metadata_1 = readAnalysisHalfword(executable_text, descriptor + 6U);
            auto size = std::size_t{2U};
            if ((low & 0x80U) != 0U) {
              operand_0 = static_cast<std::uint16_t>(low & 0x7fU);
              operand_0_source = "inline7";
            } else {
              if (event_end - instruction_offset < 4U) {
                throw sf::core::Error{
                    sf::core::ErrorCode::invalid_format,
                    "Truncated mission-script extended operand"};
              }
              operand_0 = readAnalysisHalfword(
                  bytes, program.offset + instruction_offset + 2U);
              operand_0_source = "extended16";
              size += 2U;
            }
            if (metadata_1 != 0xffU) {
              if ((low & 0x80U) == 0U && (low & 0x40U) != 0U) {
                operand_1 = static_cast<std::uint16_t>(low & 0x3fU);
                operand_1_source = "inline6";
              } else {
                if (event_end - instruction_offset < size + 2U) {
                  throw sf::core::Error{
                      sf::core::ErrorCode::invalid_format,
                      "Truncated mission-script second operand"};
                }
                operand_1 = readAnalysisHalfword(
                    bytes, program.offset + instruction_offset + size);
                operand_1_source = "extended16";
                size += 2U;
              }
            }
            next_offset = instruction_offset + size;
            pending.push_back(next_offset);
            if (opcode == 0x0bU || opcode == 0x0cU) {
              program_operation = opcode == 0x0bU ? "activate" : "deactivate";
              if (*operand_0 >= scripts.programs().size()) {
                throw sf::core::Error{
                    sf::core::ErrorCode::invalid_format,
                    "Mission-script program operation references invalid "
                    "program index"};
              }
              target_program = *operand_0;
            }
          }

          output << resource.selection_index << ',' << resource.resource_name
                 << ',' << program_index << ',';
          writeCsvString(output, program.name);
          output << ',' << event_index << ','
                 << static_cast<unsigned>(event.event_id) << ','
                 << instruction_offset << ",0x" << std::hex << std::uppercase
                 << encoded << std::dec << ',' << kind << ",0x" << std::hex
                 << std::uppercase << static_cast<unsigned>(opcode) << ",0x"
                 << handler << ",0x" << metadata_0 << ",0x" << metadata_1
                 << std::dec << ',';
          if (next_offset != std::numeric_limits<std::size_t>::max()) {
            output << next_offset;
          }
          output << ',';
          if (alternate_offset != std::numeric_limits<std::size_t>::max()) {
            output << alternate_offset;
          }
          output << ',';
          if (operand_0) {
            output << *operand_0;
          }
          output << ',' << operand_0_source << ',';
          if (operand_1) {
            output << *operand_1;
          }
          output << ',' << operand_1_source << ',' << program_operation << ',';
          if (target_program) {
            output << *target_program;
          }
          output << ',';
          if (target_program) {
            writeCsvString(output, scripts.programs()[*target_program].name);
          }
          output << '\n';
          ++instruction_count;
        }
      }
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write mission-script action map"};
  }
  std::cout << "Mapped " << instruction_count
            << " reachable mission-script action instructions to "
            << output_path << '\n';
  return 0;
}

void writeCsvString(std::ostream &output, std::string_view value) {
  output << '"';
  for (const auto character : value) {
    if (character == '"') {
      output << "\"\"";
    } else {
      output << character;
    }
  }
  output << '"';
}

struct EmbeddedArchiveCandidate {
  std::size_t offset{};
  std::uint32_t identifier{};
  std::size_t names_offset{};
  std::size_t data_offset{};
  std::vector<std::uint32_t> file_offsets;
  std::vector<std::string> names;
};

std::vector<EmbeddedArchiveCandidate>
discoverEmbeddedArchives(std::span<const std::byte> text) {
  constexpr std::size_t header_size = 20U;
  std::vector<EmbeddedArchiveCandidate> result;
  for (std::size_t base = 0; base + header_size <= text.size(); base += 4U) {
    const auto view = text.subspan(base);
    const auto count = static_cast<std::size_t>(readAnalysisWord(view, 4U));
    const auto offsets_offset =
        static_cast<std::size_t>(readAnalysisWord(view, 8U));
    const auto names_offset =
        static_cast<std::size_t>(readAnalysisWord(view, 12U));
    const auto data_offset =
        static_cast<std::size_t>(readAnalysisWord(view, 16U));
    if (count == 0U || count > 4096U || offsets_offset != header_size ||
        header_size + count * 4U > names_offset ||
        names_offset >= data_offset || data_offset > view.size()) {
      continue;
    }

    EmbeddedArchiveCandidate candidate{
        base, readAnalysisWord(view, 0U), names_offset, data_offset, {}, {}};
    candidate.file_offsets.reserve(count);
    auto valid = true;
    for (std::size_t index = 0; index < count; ++index) {
      const auto offset = readAnalysisWord(view, header_size + index * 4U);
      if ((index == 0U && offset != 0U) ||
          (index > 0U && offset < candidate.file_offsets.back()) ||
          offset >= view.size() - data_offset) {
        valid = false;
        break;
      }
      candidate.file_offsets.push_back(offset);
    }
    if (!valid) {
      continue;
    }

    auto cursor = names_offset;
    while (candidate.names.size() < count && cursor < data_offset) {
      const auto start = cursor;
      while (cursor < data_offset && view[cursor] != std::byte{0}) {
        const auto character = std::to_integer<unsigned char>(view[cursor]);
        if (character < 0x20U || character > 0x7eU) {
          valid = false;
          break;
        }
        ++cursor;
      }
      if (!valid || cursor == start || cursor >= data_offset) {
        valid = false;
        break;
      }
      candidate.names.emplace_back(
          reinterpret_cast<const char *>(view.data() + start), cursor - start);
      ++cursor;
    }
    if (valid && candidate.names.size() == count) {
      result.push_back(std::move(candidate));
    }
  }
  return result;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>>
embeddedArchiveExactDataRanges(std::span<const std::byte> text,
                               std::uint32_t load_address) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  for (const auto &archive : discoverEmbeddedArchives(text)) {
    // The final entry has no size in this archive format, so only preceding
    // entries are safe exact exclusions. The mapper reports the final size as
    // an upper bound separately.
    for (std::size_t index = 0; index + 1U < archive.file_offsets.size();
         ++index) {
      const auto begin =
          archive.offset + archive.data_offset + archive.file_offsets[index];
      const auto end = archive.offset + archive.data_offset +
                       archive.file_offsets[index + 1U];
      result.emplace_back(load_address + static_cast<std::uint32_t>(begin),
                          load_address + static_cast<std::uint32_t>(end));
    }
  }
  std::ranges::sort(result);
  return result;
}

int mapEmbeddedArchives(const char *cue_path, const char *output_path) {
  const auto disc = openDisc(cue_path);
  const auto &executable = disc.executable();
  const auto text = executable.text();
  const auto text_address = executable.header().text_address;
  const auto archives = discoverEmbeddedArchives(text);

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open embedded-archive map output"};
  }
  output << "archive_address,identifier,entry_count,entry_index,name,"
            "data_address,size,size_is_text_upper_bound\n";
  std::size_t entry_count{};
  for (const auto &archive : archives) {
    for (std::size_t index = 0; index < archive.names.size(); ++index) {
      const auto data_begin =
          archive.offset + archive.data_offset + archive.file_offsets[index];
      const auto data_end = index + 1U < archive.file_offsets.size()
                                ? archive.offset + archive.data_offset +
                                      archive.file_offsets[index + 1U]
                                : text.size();
      output << "0x" << std::hex << std::uppercase
             << text_address + static_cast<std::uint32_t>(archive.offset)
             << ",0x" << archive.identifier << std::dec << ','
             << archive.names.size() << ',' << index << ',';
      writeCsvString(output, archive.names[index]);
      output << ",0x" << std::hex << std::uppercase
             << text_address + static_cast<std::uint32_t>(data_begin)
             << std::dec << ',' << data_end - data_begin << ','
             << (index + 1U == archive.names.size() ? 1 : 0) << '\n';
      ++entry_count;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write embedded-archive map"};
  }
  std::cout << "Mapped " << entry_count << " entries across " << archives.size()
            << " embedded archives to " << output_path << '\n';
  return 0;
}

int mapObjectHandlers(const char *cue_path, const char *output_path) {
  const auto disc = openDisc(cue_path);
  if (!disc.game() || (disc.game()->id != sf::game::GameId::syphon_filter_2 &&
                       disc.game()->id != sf::game::GameId::syphon_filter_3)) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Object-handler mapping requires a recognized sequel disc"};
  }
  const auto table_address =
      disc.game()->id == sf::game::GameId::syphon_filter_2
          ? std::uint32_t{0x8010c3d4U}
          : std::uint32_t{0x8010f0f0U};
  const auto class_count = disc.game()->id == sf::game::GameId::syphon_filter_3
                               ? std::size_t{0x85U}
                               : std::size_t{0x84U};
  const auto &executable = disc.executable();
  const auto text = executable.text();
  const auto text_address = executable.header().text_address;
  const auto table_offset =
      static_cast<std::size_t>(table_address - text_address);
  if (table_offset > text.size() ||
      text.size() - table_offset < class_count * sizeof(std::uint32_t)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Object-handler table is outside executable text"};
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open object-handler map output"};
  }
  output << "class_family,handler_address,owner\n";
  constexpr std::uint32_t overlay_begin = 0x8014b978U;
  constexpr std::uint32_t overlay_end = overlay_begin + 0x10000U;
  for (std::size_t family = 0; family < class_count; ++family) {
    const auto handler = readAnalysisWord(text, table_offset + family * 4U);
    std::string owner{"direct"};
    if (handler >= overlay_begin && handler < overlay_end) {
      owner = "MISSION_OVERLAY";
    } else if (handler >= text_address &&
               handler < text_address + text.size()) {
      const auto handler_offset =
          static_cast<std::size_t>(handler - text_address);
      if (handler_offset + 0x18U < text.size()) {
        auto cursor = handler_offset + 0x18U;
        std::string candidate;
        while (cursor < text.size() && candidate.size() < 15U) {
          const auto character = std::to_integer<unsigned char>(text[cursor++]);
          if (character == 0U) {
            break;
          }
          if (character < 0x20U || character > 0x7eU) {
            candidate.clear();
            break;
          }
          candidate.push_back(static_cast<char>(character));
        }
        if (candidate.starts_with("OBJ_")) {
          owner = std::move(candidate);
        }
      }
    }
    output << "0x" << std::hex << std::uppercase << family << ",0x" << handler
           << std::dec << ',';
    writeCsvString(output, owner);
    output << '\n';
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write object-handler map"};
  }
  std::cout << "Mapped " << class_count << " object-class handlers from 0x"
            << std::hex << std::uppercase << table_address << std::dec << " to "
            << output_path << '\n';
  return 0;
}

int mapStringReferences(const char *cue_path, const char *output_path) {
  auto disc = openDisc(cue_path);
  const auto &executable = disc.executable();
  const auto &header = executable.header();
  const auto text = executable.text();
  const auto overlay_seeds = sequelOverlayExecutableSeeds(disc);
  const auto functions = sf::psx::fingerprintFunctionCandidates(
      text, header.text_address, header.initial_pc, true, overlay_seeds);

  std::map<std::uint32_t, std::string> strings;
  for (std::size_t offset = 0; offset < text.size();) {
    const auto printable = [](std::byte value) {
      const auto character = std::to_integer<unsigned char>(value);
      return character >= 0x20U && character <= 0x7eU;
    };
    if (!printable(text[offset])) {
      ++offset;
      continue;
    }
    auto end = offset;
    while (end < text.size() && printable(text[end])) {
      ++end;
    }
    if (end - offset >= 4U && end < text.size() && text[end] == std::byte{0}) {
      std::string value;
      value.reserve(end - offset);
      for (auto cursor = offset; cursor < end; ++cursor) {
        value.push_back(
            static_cast<char>(std::to_integer<unsigned char>(text[cursor])));
      }
      strings.emplace(header.text_address + static_cast<std::uint32_t>(offset),
                      std::move(value));
    }
    offset = std::max(end, offset + 1U);
  }

  std::ofstream output{std::filesystem::path{output_path}, std::ios::trunc};
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Cannot open string-reference map output"};
  }
  output << "reference_site,function_address,string_address,string\n";
  std::set<std::pair<std::uint32_t, std::uint32_t>> emitted;
  std::size_t reference_count{};
  for (std::size_t offset = 0; offset + 4U <= text.size(); offset += 4U) {
    const auto instruction = readAnalysisWord(text, offset);
    if ((instruction >> 26U) != 0x0fU) {
      continue;
    }
    const auto base_register = (instruction >> 16U) & 0x1fU;
    const auto upper = (instruction & 0xffffU) << 16U;
    for (std::size_t lookahead = 1U;
         lookahead <= 8U && offset + lookahead * 4U + 4U <= text.size();
         ++lookahead) {
      const auto use_offset = offset + lookahead * 4U;
      const auto use = readAnalysisWord(text, use_offset);
      const auto opcode = use >> 26U;
      const auto source = (use >> 21U) & 0x1fU;
      if ((opcode != 0x08U && opcode != 0x09U && opcode != 0x0dU) ||
          source != base_register) {
        continue;
      }
      const auto immediate = use & 0xffffU;
      const auto lower = (opcode == 0x0dU || (immediate & 0x8000U) == 0U)
                             ? immediate
                             : immediate | 0xffff0000U;
      const auto address = upper + lower;
      const auto found_string = strings.find(address);
      if (found_string == strings.end()) {
        continue;
      }
      const auto site =
          header.text_address + static_cast<std::uint32_t>(use_offset);
      if (!emitted.emplace(site, address).second) {
        continue;
      }
      auto function_address = std::uint32_t{};
      const auto function = std::ranges::upper_bound(
          functions, site, {}, &sf::psx::FunctionFingerprint::address);
      if (function != functions.begin()) {
        const auto &candidate = *std::prev(function);
        const auto candidate_end =
            candidate.address +
            static_cast<std::uint32_t>(candidate.instruction_count * 4U);
        if (site < candidate_end) {
          function_address = candidate.address;
        }
      }
      output << "0x" << std::hex << std::uppercase << site << ",0x"
             << function_address << ",0x" << address << std::dec << ',';
      writeCsvString(output, found_string->second);
      output << '\n';
      ++reference_count;
      break;
    }
  }
  if (!output) {
    throw sf::core::Error{sf::core::ErrorCode::io,
                          "Failed to write string-reference map"};
  }
  std::cout << "Mapped " << reference_count << " references to "
            << strings.size() << " null-terminated ASCII strings in "
            << output_path << '\n';
  return 0;
}

int probeLegacyVm(const char *cue_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Legacy VM probe requires Syphon Filter USA v1.1"};
  }

  // Original SCUS fixed-point multiply routine, used as a side-effect-free
  // proof target.
  constexpr std::uint32_t fixed_multiply_address = 0x800c6d4cU;
  constexpr std::array cases{
      std::array<std::int32_t, 2>{4096, 8192},
      std::array<std::int32_t, 2>{-4096, 8192},
      std::array<std::int32_t, 2>{12345, -2345},
      std::array<std::int32_t, 2>{-32767, -8191},
  };

  sf::game::LegacyGameplayVm vm{disc.executable()};
  std::uint64_t total_instructions{};
  for (const auto &values : cases) {
    const std::array arguments{
        std::bit_cast<std::uint32_t>(values[0]),
        std::bit_cast<std::uint32_t>(values[1]),
    };
    const auto result = vm.invoke(fixed_multiply_address, arguments, 64U);
    if (!result.completed()) {
      std::cerr << "LegacyGameplayVM stopped at 0x" << std::hex
                << std::uppercase << result.execution.pc << ": "
                << sf::psx::toString(result.execution.reason) << '\n';
      return 3;
    }
    const auto expected = static_cast<std::int32_t>(
        (static_cast<std::int64_t>(values[0]) * values[1]) / 4096);
    if (std::bit_cast<std::int32_t>(result.return_value) != expected) {
      std::cerr << "LegacyGameplayVM result mismatch for " << values[0] << " * "
                << values[1] << '\n';
      return 4;
    }
    total_instructions += result.execution.instructions;
  }

  constexpr std::uint32_t mission_overlay_address = 0x80146630U;
  constexpr std::uint32_t mission_overlay_bootstrap = 0x80146c18U;
  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  if (!vm.loadOverlay(
          mission_overlay_address,
          mission.archive().file(mission.definition().overlay_name))) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Mission overlay does not fit LegacyGameplayVM RAM"};
  }
  const auto overlay_result =
      vm.invoke(mission_overlay_bootstrap, {}, 100'000U);
  if (!overlay_result.completed()) {
    std::cerr << "LegacyGameplayVM mission bootstrap stopped at 0x" << std::hex
              << std::uppercase << overlay_result.execution.pc << ": "
              << sf::psx::toString(overlay_result.execution.reason) << '\n';
    return 5;
  }
  total_instructions += overlay_result.execution.instructions;

  std::cout << "LegacyGameplayVM SCUS probe passed: " << cases.size()
            << " math cases + SUBWAY.OVL bootstrap, " << total_instructions
            << " instructions\n";
  return 0;
}

int probeExecutableEntry(const char *cue_path, std::uint64_t budget) {
  auto disc = openDisc(cue_path);
  sf::game::LegacyGameplayVm vm{disc.executable()};
  sf::game::DiscCdRomMedia cdrom_media{disc.image()};
  vm.machine().setCdRomMedia(&cdrom_media);
  vm.bindPsxBiosCoreVector();
  if (disc.game()) {
    const auto &layout = disc.game()->executable_layout;
    vm.bindPsxVideoTimingCall(layout.vsync_address,
                              layout.retrace_counter_address);
    if (layout.cd_pending_command_address != 0U) {
      vm.bindPsxCdPendingCommandCall(
          layout.cd_pending_command_address, layout.cd_pending_command_state,
          layout.cd_response_pointer, layout.cd_completion_state,
          disc.game()->id == sf::game::GameId::syphon_filter_2
              ? 0x80141a10U
              : 0U);
    }
    if (layout.cd_control_address != 0U) {
      const auto is_sf2 =
          disc.game()->id == sf::game::GameId::syphon_filter_2;
      const auto sf2_profile = sf::game::sf2UsaGuestRuntimeProfile();
      vm.bindPsxCdControlCall(
          layout.cd_control_address,
          is_sf2 ? sf2_profile.cd_setloc_state : 0U,
          is_sf2 ? sf2_profile.cd_mode_state : 0U);
    }
    vm.bindPsxCdReadyCallback(
        layout.cd_ready_callback_address, layout.cd_ready_result_address,
        layout.cd_ready_state_address, layout.cd_ready_callback_is_pointer);
  }
  constexpr std::uint64_t frame_slice_budget = 50'000U;
  auto remaining = budget;
  auto total_instructions = std::uint64_t{};
  auto total_host_calls = std::uint64_t{};
  auto result =
      vm.resumeCurrentPcClockNeutral(std::min(remaining, frame_slice_budget));
  for (;;) {
    total_instructions += result.execution.instructions;
    total_host_calls += result.host_calls;
    const auto consumed = std::min(remaining, frame_slice_budget);
    remaining -= consumed;
    if (result.execution.reason !=
            sf::psx::R3000StopReason::instruction_budget ||
        remaining == 0U || !disc.game()) {
      break;
    }
    vm.machine().advanceHardwareTicks(consumed);
    const auto counter_address =
        disc.game()->executable_layout.retrace_counter_address;
    std::uint32_t counter{};
    if (!vm.runtime().read32(counter_address, counter) ||
        !vm.runtime().write32(counter_address, counter + 1U)) {
      throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                            "Could not advance executable VBlank counter"};
    }
    if (!vm.servicePsxCdReadyCallback()) {
      throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                            "Could not dispatch executable CD-ready callback"};
    }
    result =
        vm.resumeCurrentPcClockNeutral(std::min(remaining, frame_slice_budget));
  }
  std::uint16_t timer1_counter{};
  std::uint16_t timer1_mode{};
  static_cast<void>(vm.runtime().read16(0x1f801110U, timer1_counter));
  static_cast<void>(vm.runtime().read16(0x1f801114U, timer1_mode));
  const auto cdrom = vm.machine().cdrom().captureState();
  const auto cd_dma_madr = vm.machine().dma().madr(sf::psx::DmaChannel::cdrom);
  const auto cd_dma_bcr = vm.machine().dma().bcr(sf::psx::DmaChannel::cdrom);
  const auto cd_dma_chcr = vm.machine().dma().chcr(sf::psx::DmaChannel::cdrom);
  const auto spu_dma_madr = vm.machine().dma().madr(sf::psx::DmaChannel::spu);
  const auto spu_dma_bcr = vm.machine().dma().bcr(sf::psx::DmaChannel::spu);
  const auto spu_dma_chcr = vm.machine().dma().chcr(sf::psx::DmaChannel::spu);
  std::uint32_t cd_response_pointer{};
  std::uint8_t cd_response{};
  std::uint8_t cd_completion{};
  if (disc.game() && disc.game()->executable_layout.cd_response_pointer != 0U) {
    static_cast<void>(
        vm.runtime().read32(disc.game()->executable_layout.cd_response_pointer,
                            cd_response_pointer));
    static_cast<void>(vm.runtime().read8(cd_response_pointer, cd_response));
    static_cast<void>(vm.runtime().read8(
        disc.game()->executable_layout.cd_completion_state, cd_completion));
  }
  std::cout << "game="
            << (disc.game() ? std::string{disc.game()->title} : "unrecognized")
            << " start=0x" << std::hex << std::uppercase
            << disc.executable().header().initial_pc << " stop=0x"
            << result.execution.pc << std::dec
            << " reason=" << sf::psx::toString(result.execution.reason)
            << " instructions=" << total_instructions
            << " host-calls=" << total_host_calls << " instruction=0x"
            << std::hex << result.execution.instruction << " bad-vaddr=0x"
            << vm.runtime().state().cop0_bad_vaddr << " v0=0x"
            << vm.runtime().state().gpr[2U] << " v1=0x"
            << vm.runtime().state().gpr[3U] << " ra=0x"
            << vm.runtime().state().gpr[31U] << " sp=0x"
            << vm.runtime().state().gpr[29U] << " a0=0x"
            << vm.runtime().state().gpr[4U] << " a1=0x"
            << vm.runtime().state().gpr[5U] << " a2=0x"
            << vm.runtime().state().gpr[6U] << " a3=0x"
            << vm.runtime().state().gpr[7U] << " t1=0x"
            << vm.runtime().state().gpr[9U] << " timer1=0x" << timer1_counter
            << " timer1-mode=0x" << timer1_mode << " cd-command=0x"
            << static_cast<unsigned int>(cdrom.pending_command)
            << " cd-phase=" << static_cast<unsigned int>(cdrom.command_phase)
            << " cd-if=0x" << static_cast<unsigned int>(cdrom.interrupt_flags)
            << " cd-ie=0x" << static_cast<unsigned int>(cdrom.interrupt_enable)
            << " cd-response-ptr=0x" << cd_response_pointer << " cd-response=0x"
            << static_cast<unsigned int>(cd_response) << " cd-completion=0x"
            << static_cast<unsigned int>(cd_completion) << " cd-fifo0=0x"
            << static_cast<unsigned int>(cdrom.response[0]) << " cd-fifo-pos="
            << static_cast<unsigned int>(cdrom.response_position) << "/"
            << static_cast<unsigned int>(cdrom.response_count) << " cd-dma=0x"
            << cd_dma_madr << ",0x" << cd_dma_bcr << ",0x" << cd_dma_chcr
            << " spu-dma=0x" << spu_dma_madr << ",0x" << spu_dma_bcr << ",0x"
            << spu_dma_chcr << std::dec << '\n';
  return 0;
}

int probeSf2GuestBootstrap(const char *cue_path, std::uint64_t budget,
                           bool probe_mission_transition) {
  constexpr auto guest_profile = sf::game::sf2UsaGuestRuntimeProfile();
  constexpr std::size_t frame_boundary_count = 8U;
  constexpr std::size_t mission_boundary_limit = 512U;
  constexpr std::size_t stable_mission_boundary_count = 8U;
  constexpr std::uint64_t scheduler_slice_budget = 50'000U;
  constexpr std::uint32_t mission_selection_index = 2U;

  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->id != sf::game::GameId::syphon_filter_2) {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "SF2 guest bootstrap probe requires a supported Syphon Filter 2 disc"};
  }
  struct MissionProbeAssets {
    std::vector<std::byte> fog_bytes;
    std::vector<std::byte> init_overlay;
    std::vector<std::byte> expected_overlay;
    std::map<std::string, std::vector<std::byte>> fog_files;
    std::vector<sf::assets::FogEntry> fog_entries;
    std::map<std::string, std::vector<std::byte>> resident_files;
  };
  MissionProbeAssets mission_assets;
  if (probe_mission_transition) {
    if (disc.game()->disc_number != 1U) {
      throw sf::core::Error{
          sf::core::ErrorCode::unsupported,
          "SF2 Mission 3 transition probe requires retail Disc 1"};
    }
    mission_assets.fog_bytes = disc.image().readFile("FOG/HWAY.FOG");
    mission_assets.init_overlay = disc.image().readFile("BIN/INIT.OVL");
    const auto resident_archive =
        sf::game::parseEmbeddedHog(disc.executable(), "BEEPSX.VB");
    for (const auto &entry : resident_archive.entries()) {
      const auto file = resident_archive.file(entry.name);
      mission_assets.resident_files.emplace(
          entry.name, std::vector<std::byte>{file.begin(), file.end()});
    }
    const auto fog = sf::assets::FogArchive::parse(mission_assets.fog_bytes);
    mission_assets.fog_entries = fog.entries();
    for (const auto &entry : fog.entries()) {
      const auto file = fog.file(entry.name);
      mission_assets.fog_files.emplace(
          entry.name, std::vector<std::byte>{file.begin(), file.end()});
    }
    const auto overlay_file = fog.file("HWAY.OVL");
    if (overlay_file.size() <= sequelMissionOverlayHeaderSize) {
      throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                            "HWAY mission overlay is truncated"};
    }
    mission_assets.expected_overlay.assign(
        overlay_file.begin() +
            static_cast<std::ptrdiff_t>(sequelMissionOverlayHeaderSize),
        overlay_file.end());
  }
  const auto &fog_bytes = mission_assets.fog_bytes;
  const auto &init_overlay = mission_assets.init_overlay;
  const auto &expected_overlay = mission_assets.expected_overlay;
  const auto &fog_files = mission_assets.fog_files;
  const auto &fog_entries = mission_assets.fog_entries;
  const auto &resident_files = mission_assets.resident_files;
  const auto init_code_offset =
      probe_mission_transition
          ? sequelResidentOverlayCodeOffset(init_overlay, 0x80158878U)
          : std::size_t{};
  constexpr std::size_t init_code_probe_size = 256U;
  const auto fog_header = std::span<const std::byte>{fog_bytes}.first(
      std::min(fog_bytes.size(), sf::assets::FogArchive::sector_size));

  sf::game::LegacyGameplayVm vm{disc.executable()};
  sf::game::DiscCdRomMedia cdrom_media{disc.image()};
  auto enable_sf2_mission_search = probe_mission_transition;
  auto enable_sf2_resident_overlay_load = false;
  auto enable_sf2_resident_file_access = probe_mission_transition;
  auto sf2_search_calls = std::size_t{};
  auto sf2_search_matches = std::size_t{};
  std::string sf2_last_search_path;
  std::array<std::byte, 0x240U> sf2_catalog_after_copy{};
  auto sf2_catalog_copy_observations = std::size_t{};
  auto sf2_catalog_repair_bridges = std::size_t{};
  auto sf2_slf_open_bridges = std::size_t{};
  auto sf2_slf_load_bridges = std::size_t{};
  auto sf2_resident_file_open_bridges = std::size_t{};
  auto sf2_resident_file_load_bridges = std::size_t{};
  auto sf2_movie_catalog_open_bridges = std::size_t{};
  auto sf2_movie_catalog_load_bridges = std::size_t{};
  std::vector<std::string> sf2_file_open_paths;
  struct ResidentOpenFile {
    const std::vector<std::byte> *bytes{};
    std::size_t offset{};
    bool fog_member{};
  };
  std::map<std::uint32_t, ResidentOpenFile> sf2_resident_open_files;
  auto sf2_pad_poll_bridges = std::size_t{};
  auto sf2_resident_callback_table_bridges = std::size_t{};
  auto sf2_resident_overlay_load_bridges = std::size_t{};
  auto sf2_movie_overlay_load_bridges = std::size_t{};
  auto sf2_title_overlay_load_bridges = std::size_t{};
  std::vector<std::uint32_t> sf2_common_init_arguments;
  std::vector<std::uint32_t> sf2_common_disc_open_results;
  std::vector<std::array<std::uint32_t, 2U>> sf2_file_seek_results;
  std::vector<std::array<std::uint32_t, 3U>> sf2_application_state_calls;
  struct Sf2ArchiveMemberRequest {
    std::string name;
    std::uint32_t destination_slot{};
    std::uint32_t mode{};
    std::uint32_t caller{};
  };
  std::vector<Sf2ArchiveMemberRequest> sf2_archive_member_requests;
  std::vector<std::array<std::uint32_t, 7U>> sf2_init_resource_inputs;
  std::vector<std::array<std::uint32_t, 6U>> sf2_init_descriptor_writes;
  std::vector<std::string> sf2_init_archive_paths;
  std::string sf2_last_resident_overlay_name;
  std::uint32_t sf2_last_resident_overlay_address{};
  std::uint32_t sf2_last_resident_overlay_mode{};
  std::vector<std::tuple<std::string, std::uint32_t, std::uint32_t,
                         std::uint32_t>>
      sf2_overlay_requests;
  struct Sf2RenderListInsert {
    std::uint32_t list{};
    std::uint32_t object{};
    std::uint32_t caller{};
    std::uint32_t upstream_caller{};
    bool scheduled_callback{};
    std::array<std::uint32_t, 8U> object_words{};
  };
  std::vector<Sf2RenderListInsert> sf2_render_list_inserts;
  auto sf2_servicing_scheduled_callback = false;
  struct Sf2RenderListRemove {
    std::uint32_t list{};
    std::uint32_t node{};
    std::uint32_t object{};
    std::uint32_t caller{};
  };
  std::vector<Sf2RenderListRemove> sf2_render_list_removes;
  std::vector<std::array<std::uint32_t, 3U>> sf2_heap_rewinds;
  struct Sf2HeapAllocation {
    std::uint32_t size{};
    std::uint32_t address{};
    std::uint32_t caller{};
  };
  std::vector<Sf2HeapAllocation> sf2_heap_allocations;
  std::vector<std::array<std::uint32_t, 7U>> sf2_render_arena_resets;
  vm.machine().setCdRomMedia(&cdrom_media);
  vm.bindPsxBiosCoreVector(probe_mission_transition);
  if (probe_mission_transition) {
    // This probe runs without a ROM BIOS. Acknowledge the low exception
    // vector through the host-owned scheduler and resume the interrupted
    // retail instruction with the architectural RFE status rotation.
    constexpr std::uint32_t exception_return_trampoline = 0x8000c100U;
    constexpr std::array exception_return_code{
        0x03600008U, // jr k1
        0x0340f821U, // addu ra,k0,zero
    };
    std::array<std::byte, exception_return_code.size() * sizeof(std::uint32_t)>
        exception_return_bytes{};
    for (std::size_t word = 0U; word < exception_return_code.size(); ++word) {
      for (std::size_t byte = 0U; byte < sizeof(std::uint32_t); ++byte) {
        exception_return_bytes[word * sizeof(std::uint32_t) + byte] =
            static_cast<std::byte>(exception_return_code[word] >> (byte * 8U));
      }
    }
    if (!vm.runtime().loadBytes(exception_return_trampoline,
                                exception_return_bytes)) {
      throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                            "Could not install SF2 probe exception return"};
    }
    vm.bindHostCall(exception_return_trampoline,
                    [](sf::game::LegacyHostCallContext &context) {
                      context.continueGuestInstruction();
                    });
    vm.bindHostCall(0x80000080U, [&vm, exception_return_trampoline](
                                     sf::game::LegacyHostCallContext &context) {
      constexpr std::uint32_t mode_stack_mask = 0x0fU;
      constexpr std::uint32_t interrupt_status_address = 0x1f801070U;
      auto state = vm.runtime().state();
      const auto resume_pc = state.cop0_epc;
      const auto interrupted_return = state.gpr[31U];
      state.cop0_status = (state.cop0_status & ~mode_stack_mask) |
                          ((state.cop0_status >> 2U) & mode_stack_mask);
      vm.runtime().restoreCpuState(state);
      if (!context.write16(interrupt_status_address, 0U)) {
        context.rejectHostCall();
        return;
      }
      vm.runtime().setExternalInterrupt(false);
      context.setRegister(26U, interrupted_return);
      context.setRegister(27U, resume_pc);
      context.setRegister(31U, exception_return_trampoline);
      context.setReturnValue(0U);
    });
  }
  const auto &layout = disc.game()->executable_layout;
  vm.bindPsxVideoTimingCall(layout.vsync_address,
                            layout.retrace_counter_address);
  vm.bindPsxCdPendingCommandCall(
      layout.cd_pending_command_address, layout.cd_pending_command_state,
      layout.cd_response_pointer, layout.cd_completion_state,
      guest_profile.cd_completion_result);
  vm.bindPsxCdControlCall(layout.cd_control_address,
                          guest_profile.cd_setloc_state,
                          guest_profile.cd_mode_state);
  vm.bindPsxCdReadyCallback(
      layout.cd_ready_callback_address, layout.cd_ready_result_address,
      layout.cd_ready_state_address, layout.cd_ready_callback_is_pointer);
  vm.bindPsxCdCompletionCallback(0x8011d1bcU,
                                 guest_profile.cd_completion_result, true);
  if (probe_mission_transition) {
    // PsyQ CdSearchFile is a platform boundary: resolve the retail ISO extent
    // natively, then let guest code own every sector transfer, archive parse,
    // overlay placement, and overlay execution.
    vm.bindHostCall(
        guest_profile.cd_search_file_entry,
        [&disc, &enable_sf2_mission_search, &enable_sf2_resident_file_access,
         &sf2_search_calls, &sf2_search_matches,
         &sf2_last_search_path](sf::game::LegacyHostCallContext &context) {
          if (!enable_sf2_mission_search) {
            context.continueGuestInstruction();
            return;
          }
          const auto destination = context.argument(0);
          std::string path;
          if (destination == 0U ||
              !context.readCString(context.argument(1), path, 256U)) {
            context.setReturnValue(0U);
            return;
          }
          std::ranges::replace(path, '\\', '/');
          while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
          }
          if (path.ends_with(";1")) {
            path.resize(path.size() - 2U);
          }
          ++sf2_search_calls;
          sf2_last_search_path = path;
          std::uint32_t extent_lba{};
          std::uint32_t file_size{};
          std::string file_name;
          auto found = false;
          try {
            const auto entry = disc.image().find(path);
            if (entry.is_directory) {
              context.setReturnValue(0U);
              return;
            }
            extent_lba = entry.extent_lba;
            file_size = entry.size;
            file_name = entry.name;
            found = true;
          } catch (const sf::core::Error &) {
            if (!enable_sf2_resident_file_access &&
                path.ends_with("GLOBAL.DAT")) {
              const auto entry = disc.image().find(
                  std::string{disc.game()->layout.mission_info_path});
              extent_lba = entry.extent_lba;
              file_size = entry.size;
              file_name = entry.name;
              found = true;
            }
          }
          if (!found) {
            context.setReturnValue(0U);
            return;
          }
          constexpr std::uint32_t pregap_sectors = 150U;
          constexpr std::uint32_t sectors_per_second = 75U;
          constexpr std::uint32_t seconds_per_minute = 60U;
          const auto absolute_sector = extent_lba + pregap_sectors;
          const auto minute =
              absolute_sector / (sectors_per_second * seconds_per_minute);
          const auto second =
              (absolute_sector / sectors_per_second) % seconds_per_minute;
          const auto frame = absolute_sector % sectors_per_second;
          const auto bcd = [](std::uint32_t value) {
            return static_cast<std::byte>(((value / 10U) << 4U) |
                                          (value % 10U));
          };
          std::array<std::byte, 24U> cdl_file{};
          cdl_file[0] = bcd(minute);
          cdl_file[1] = bcd(second);
          cdl_file[2] = bcd(frame);
          for (std::size_t index = 0U; index < sizeof(file_size); ++index) {
            cdl_file[4U + index] =
                static_cast<std::byte>(file_size >> (index * 8U));
          }
          const auto name_size =
              std::min(file_name.size(), cdl_file.size() - 8U);
          for (std::size_t index = 0U; index < name_size; ++index) {
            cdl_file[8U + index] = static_cast<std::byte>(file_name[index]);
          }
          if (!context.writeBytes(destination, cdl_file)) {
            context.setReturnValue(0U);
            return;
          }
          ++sf2_search_matches;
          context.setReturnValue(destination);
        });
    vm.bindHostCall(0x80010750U, [&sf2_catalog_after_copy,
                                  &sf2_catalog_copy_observations](
                                     sf::game::LegacyHostCallContext &context) {
      if (context.argument(0) != 0x80126058U || context.argument(2) != 0x90U) {
        context.continueGuestInstruction();
        return;
      }
      if (!context.readBytes(context.argument(1), sf2_catalog_after_copy) ||
          !context.writeBytes(context.argument(0), sf2_catalog_after_copy)) {
        context.setReturnValue(0xffffffffU);
        return;
      }
      ++sf2_catalog_copy_observations;
      context.setReturnValue(0U);
    });
    vm.bindHostCall(0x800260e4U, [&disc, &sf2_catalog_after_copy,
                                  &sf2_catalog_copy_observations,
                                  &sf2_catalog_repair_bridges](
                                     sf::game::LegacyHostCallContext &context) {
      if (sf2_catalog_copy_observations == 0U) {
        context.continueGuestInstruction();
        return;
      }
      try {
        const auto entry = disc.image().find("FOG/HWAY.FOG");
        const auto stack_pointer = context.registerValue(29U);
        std::uint32_t caller_return{};
        std::uint32_t saved_s0{};
        if (!context.writeBytes(0x80126058U, sf2_catalog_after_copy) ||
            !context.write32(0x8011ee68U, 0x80126058U) ||
            !context.write32(0x80126060U, entry.extent_lba) ||
            !context.read32(stack_pointer + 0x81cU, caller_return) ||
            !context.read32(stack_pointer + 0x818U, saved_s0)) {
          context.setReturnValue(0U);
          return;
        }
        context.setRegister(16U, saved_s0);
        context.setRegister(29U, stack_pointer + 0x820U);
        context.setRegister(31U, caller_return);
        ++sf2_catalog_repair_bridges;
        context.setReturnValue(1U);
      } catch (const sf::core::Error &) {
        context.setReturnValue(0U);
      }
    });
    vm.bindHostCall(0x8002b4c4U, [&enable_sf2_resident_overlay_load,
                                  &sf2_resident_overlay_load_bridges,
                                  &sf2_movie_overlay_load_bridges,
                                   &sf2_title_overlay_load_bridges,
                                   &sf2_overlay_requests,
                                   &sf2_last_resident_overlay_name,
                                  &sf2_last_resident_overlay_address,
                                  &sf2_last_resident_overlay_mode](
                                     sf::game::LegacyHostCallContext &context) {
      if (!enable_sf2_resident_overlay_load) {
        context.continueGuestInstruction();
        return;
      }
      std::string name;
      if (!context.readCString(context.argument(0), name, 64U)) {
        context.continueGuestInstruction();
        return;
      }
      sf2_last_resident_overlay_name = name;
      sf2_last_resident_overlay_address = context.argument(1);
      sf2_last_resident_overlay_mode = context.argument(2);
      if (sf2_overlay_requests.size() < 32U) {
        sf2_overlay_requests.emplace_back(
            name, context.argument(1), context.argument(2),
            context.registerValue(31U));
      }
      ++sf2_resident_overlay_load_bridges;
      if (name == "MOVIE.OVL") {
        ++sf2_movie_overlay_load_bridges;
      } else if (name == "TITLE.OVL") {
        ++sf2_title_overlay_load_bridges;
      }
      // Observe the high-level request but leave heap teardown, file sizing,
      // relocation, and lifetime bookkeeping in the retail loader. The lower
      // open/read platform bridges provide only immutable file bytes.
      context.continueGuestInstruction();
    });
    vm.bindHostCall(0x80026414U, [&disc, &cdrom_media, &resident_files,
                                   &enable_sf2_resident_file_access,
                                   &sf2_resident_open_files,
                                   &sf2_resident_file_open_bridges,
                                   &sf2_resident_file_load_bridges,
                                   &sf2_slf_load_bridges,
                                   &sf2_movie_catalog_load_bridges](
                                      sf::game::LegacyHostCallContext &context) {
      auto resident = sf2_resident_open_files.find(context.argument(0));
      if (resident == sf2_resident_open_files.end()) {
        std::uint32_t handle_size{};
        const auto movie_entry = disc.image().find("MOVIE1.HOG");
        if (context.argument(1) != 0U && context.argument(2) == 0x800U &&
            context.read32(context.argument(0) + 4U, handle_size) &&
            handle_size == movie_entry.size) {
          std::array<std::byte, 0x800U> sector{};
          if (!cdrom_media.readDataSector(movie_entry.extent_lba, sector) ||
              !context.writeBytes(context.argument(1), sector) ||
              (context.argument(3) != 0U &&
               !context.write32(context.argument(3), 0U))) {
            context.setReturnValue(3U);
            return;
          }
          ++sf2_movie_catalog_load_bridges;
          context.setReturnValue(0U);
          return;
        }
        const std::vector<std::byte> *resident_bytes{};
        auto resident_size_matches = std::size_t{};
        if (enable_sf2_resident_file_access &&
            context.read32(context.argument(0) + 4U, handle_size)) {
          for (const auto &[name, bytes] : resident_files) {
            static_cast<void>(name);
            if (bytes.size() == handle_size) {
              resident_bytes = &bytes;
              ++resident_size_matches;
            }
          }
        }
        if (resident_size_matches == 1U) {
          resident =
              sf2_resident_open_files
                  .insert_or_assign(
                      context.argument(0),
                      ResidentOpenFile{resident_bytes, 0U, false})
                  .first;
          ++sf2_resident_file_open_bridges;
        }
      }
      if (resident == sf2_resident_open_files.end()) {
        context.continueGuestInstruction();
        return;
      }
      const auto requested = static_cast<std::size_t>(context.argument(2));
      const auto &bytes = *resident->second.bytes;
      const auto available = resident->second.offset < bytes.size()
                                 ? bytes.size() - resident->second.offset
                                 : 0U;
      const auto copied = std::min(requested, available);
      std::vector<std::byte> payload(requested);
      std::ranges::copy_n(
          bytes.begin() + static_cast<std::ptrdiff_t>(resident->second.offset),
          copied, payload.begin());
      if (context.argument(1) == 0U ||
          !context.writeBytes(context.argument(1), payload) ||
          (context.argument(3) != 0U &&
           !context.write32(context.argument(3), 0U))) {
        context.setReturnValue(3U);
        return;
      }
      resident->second.offset += copied;
      if (resident->second.fog_member) {
        ++sf2_slf_load_bridges;
      } else {
        ++sf2_resident_file_load_bridges;
      }
      if (resident->second.offset >= bytes.size()) {
        sf2_resident_open_files.erase(resident);
      }
      context.setReturnValue(0U);
    });
    vm.bindHostCall(
        0x8002662cU,
        [&sf2_resident_open_files](
            sf::game::LegacyHostCallContext &context) {
          std::uint32_t handle{};
          if (context.argument(0) != 0U &&
              context.read32(context.argument(0), handle)) {
            sf2_resident_open_files.erase(handle);
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(0x80026234U, [&disc, &fog_entries, &fog_files,
                                   &sf2_resident_open_files,
                                   &sf2_slf_open_bridges,
                                  &sf2_file_open_paths](
                                     sf::game::LegacyHostCallContext &context) {
      std::string path;
      if (!context.readCString(context.argument(0), path, 256U) ||
          context.argument(1) == 0U) {
        context.continueGuestInstruction();
        return;
      }
      if (sf2_file_open_paths.size() < 32U) {
        sf2_file_open_paths.push_back(path);
      }
      auto member_name = path;
      if (member_name.ends_with(";1")) {
        member_name.resize(member_name.size() - 2U);
      }
      const auto separator = member_name.find_last_of("\\/");
      if (separator != std::string::npos) {
        member_name.erase(0U, separator + 1U);
      }
      std::ranges::transform(
          member_name, member_name.begin(),
          [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
          });
      const auto fog_entry =
          std::ranges::find_if(fog_entries, [&member_name](const auto &entry) {
            auto candidate = entry.name;
            std::ranges::transform(
                candidate, candidate.begin(),
                [](unsigned char character) {
                  return static_cast<char>(std::toupper(character));
                });
            return candidate == member_name;
          });
      const auto fog_file = fog_files.find(member_name);
      if (fog_entry == fog_entries.end() || fog_file == fog_files.end()) {
        context.continueGuestInstruction();
        return;
      }
      auto handle = std::uint32_t{};
      for (std::size_t index = 0U; index < 5U; ++index) {
        const auto candidate =
            0x80125ff4U + static_cast<std::uint32_t>(index * 0x14U);
        std::uint32_t state{};
        if (context.read32(candidate + 4U, state) && state == 0xcacacacaU) {
          handle = candidate;
          break;
        }
      }
      if (handle == 0U) {
        context.setReturnValue(3U);
        return;
      }
      const auto mission_fog = disc.image().find("FOG/HWAY.FOG");
      const auto sector = mission_fog.extent_lba + fog_entry->start_sector;
      const auto size = fog_entry->sector_count << 11U;
      const auto absolute = sector + 150U;
      const auto bcd = [](std::uint32_t value) {
        return static_cast<std::byte>(((value / 10U) << 4U) | (value % 10U));
      };
      std::array<std::byte, 20U> file{};
      file[0] = bcd(absolute / (60U * 75U));
      file[1] = bcd((absolute / 75U) % 60U);
      file[2] = bcd(absolute % 75U);
      file[12] = file[0];
      file[13] = file[1];
      file[14] = file[2];
      const auto write_le32 = [&file](std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0U; index < sizeof(value); ++index) {
          file[offset + index] = static_cast<std::byte>(value >> (index * 8U));
        }
      };
      write_le32(4U, size);
      write_le32(8U, sector + fog_entry->sector_count - 1U);
      write_le32(16U, size);
      if (!context.writeBytes(handle, file) ||
          !context.write32(context.argument(1), handle)) {
        context.setReturnValue(3U);
        return;
      }
      sf2_resident_open_files.insert_or_assign(
          handle, ResidentOpenFile{&fog_file->second, 0U, true});
      ++sf2_slf_open_bridges;
      context.setReturnValue(0U);
    });
    vm.bindHostCall(
        0x8002b0d0U,
        [&sf2_archive_member_requests](
            sf::game::LegacyHostCallContext &context) {
          std::string name;
          if (sf2_archive_member_requests.size() < 32U &&
              context.readCString(context.argument(0), name, 256U)) {
            sf2_archive_member_requests.push_back(
                {std::move(name), context.argument(1), context.argument(2),
                 context.registerValue(31U)});
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x80158e3cU,
        [&sf2_init_resource_inputs](
            sf::game::LegacyHostCallContext &context) {
          std::uint32_t descriptor{};
          std::uint32_t field_0c{};
          std::uint32_t field_30{};
          std::uint32_t resource{};
          static_cast<void>(context.read32(0x8011f598U, descriptor));
          static_cast<void>(context.read32(0x8011f5a4U, resource));
          if (descriptor != 0U) {
            static_cast<void>(context.read32(descriptor + 0x0cU, field_0c));
            static_cast<void>(context.read32(descriptor + 0x30U, field_30));
          }
          if (sf2_init_resource_inputs.size() < 16U) {
            sf2_init_resource_inputs.push_back(
                {context.argument(0), descriptor, field_0c, field_30, resource,
                 context.registerValue(22U), context.registerValue(31U)});
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x8015d594U,
        [&sf2_init_archive_paths](sf::game::LegacyHostCallContext &context) {
          std::string path;
          if (sf2_init_archive_paths.size() < 16U &&
              context.readCString(context.argument(0), path, 256U)) {
            sf2_init_archive_paths.push_back(std::move(path));
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x8015d5bcU,
        [&sf2_init_descriptor_writes](
            sf::game::LegacyHostCallContext &context) {
          std::uint32_t prior{};
          std::uint32_t archive_result{};
          std::uint32_t root{};
          static_cast<void>(context.read32(0x8011f598U, prior));
          static_cast<void>(
              context.read32(context.registerValue(29U) + 0xd0U,
                             archive_result));
          if (archive_result != 0U) {
            static_cast<void>(context.read32(archive_result, root));
          }
          if (sf2_init_descriptor_writes.size() < 16U) {
            sf2_init_descriptor_writes.push_back(
                {context.argument(0), context.registerValue(16U), prior,
                 archive_result, root, context.registerValue(22U)});
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x80025c3cU,
        [&sf2_render_list_inserts, &sf2_servicing_scheduled_callback](
            sf::game::LegacyHostCallContext &context) {
          if (sf2_render_list_inserts.size() < 2'048U) {
            std::uint32_t upstream_caller{};
            static_cast<void>(context.read32(
                context.registerValue(29U) + 0x14U, upstream_caller));
            Sf2RenderListInsert insert{
                context.argument(0), context.argument(1),
                context.registerValue(31U), upstream_caller,
                sf2_servicing_scheduled_callback, {}};
            for (std::size_t index = 0U;
                 index < insert.object_words.size(); ++index) {
              static_cast<void>(context.read32(
                  insert.object +
                      static_cast<std::uint32_t>(
                          index * sizeof(std::uint32_t)),
                  insert.object_words[index]));
            }
            sf2_render_list_inserts.push_back(insert);
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x80025d3cU,
        [&sf2_render_list_removes](
            sf::game::LegacyHostCallContext &context) {
          if (sf2_render_list_removes.size() < 2'048U) {
            std::uint32_t object{};
            if (context.argument(1) != 0U) {
              static_cast<void>(
                  context.read32(context.argument(1), object));
            }
            sf2_render_list_removes.push_back(
                {context.argument(0), context.argument(1), object,
                 context.registerValue(31U)});
          }
          context.continueGuestInstruction();
        });
    vm.bindHostCall(
        0x80015878U,
        [&sf2_render_arena_resets](
            sf::game::LegacyHostCallContext &context) {
          if (sf2_render_arena_resets.size() < 32U) {
            std::array<std::uint32_t, 7U> reset{
                context.registerValue(5U), context.registerValue(31U)};
            static_cast<void>(context.read32(0x8011f4a0U, reset[2U]));
            static_cast<void>(context.read32(0x8011f4a4U, reset[3U]));
            static_cast<void>(context.read32(0x8011ee2cU, reset[4U]));
            static_cast<void>(context.read32(0x80120f0cU, reset[5U]));
            static_cast<void>(context.read32(0x80120f10U, reset[6U]));
            sf2_render_arena_resets.push_back(reset);
          }
          context.continueGuestInstruction();
        });
    const auto observe_sf2_heap_rewind =
        [&sf2_heap_rewinds](std::uint32_t entry) {
          return [&sf2_heap_rewinds, entry](
                     sf::game::LegacyHostCallContext &context) {
            if (sf2_heap_rewinds.size() < 128U) {
              sf2_heap_rewinds.push_back(
                  {entry, context.argument(0),
                   context.registerValue(31U)});
            }
            context.continueGuestInstruction();
          };
        };
    vm.bindHostCall(0x80025b3cU, observe_sf2_heap_rewind(0x80025b3cU));
    vm.bindHostCall(0x80025b48U, observe_sf2_heap_rewind(0x80025b48U));
    vm.bindHostCall(
        0x80025b24U,
        [&sf2_heap_allocations](
            sf::game::LegacyHostCallContext &context) {
          if (sf2_heap_allocations.size() < 4'096U) {
            std::uint32_t caller{};
            static_cast<void>(
                context.read32(context.registerValue(29U) + 0x14U, caller));
            sf2_heap_allocations.push_back(
                {context.registerValue(6U), context.registerValue(16U),
                 caller});
          }
          context.continueGuestInstruction();
        });
  }
  vm.bindHostCall(guest_profile.gpu_submission_entry,
                  [](sf::game::LegacyHostCallContext &context) {
                    context.setReturnValue(0U);
                  });
  vm.bindHostCall(
      guest_profile.common_init_entry,
      [&sf2_common_init_arguments](
          sf::game::LegacyHostCallContext &context) {
        if (sf2_common_init_arguments.size() < 16U) {
          sf2_common_init_arguments.push_back(context.argument(0));
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x8002a684U,
      [&sf2_common_disc_open_results](
          sf::game::LegacyHostCallContext &context) {
        if (sf2_common_disc_open_results.size() < 16U) {
          sf2_common_disc_open_results.push_back(context.registerValue(2U));
        }
        context.continueGuestInstruction();
      });
  const auto observe_sf2_file_seek =
      [&sf2_file_seek_results](std::uint32_t command) {
        return [&sf2_file_seek_results, command](
                   sf::game::LegacyHostCallContext &context) {
          if (sf2_file_seek_results.size() < 32U) {
            sf2_file_seek_results.push_back(
                {command, context.registerValue(2U)});
          }
          context.continueGuestInstruction();
        };
      };
  vm.bindHostCall(0x800261e0U, observe_sf2_file_seek(0x02U));
  vm.bindHostCall(0x800261f8U, observe_sf2_file_seek(0x15U));
  const auto observe_sf2_application_state =
      [&sf2_application_state_calls](std::uint32_t operation) {
        return [&sf2_application_state_calls, operation](
                   sf::game::LegacyHostCallContext &context) {
          if (sf2_application_state_calls.size() < 64U) {
            sf2_application_state_calls.push_back(
                {operation, context.argument(0), context.registerValue(31U)});
          }
          context.continueGuestInstruction();
        };
      };
  vm.bindHostCall(guest_profile.application_state_push_entry,
                  observe_sf2_application_state(1U));
  vm.bindHostCall(0x8002bdc0U, observe_sf2_application_state(2U));
  vm.bindHostCall(guest_profile.application_state_pop_entry,
                  observe_sf2_application_state(3U));
  // Observe the original function without replacing it. This is the first
  // architecture gate for the sequel guest runtime: CRT/Game_Main must reach
  // the retail fourteen-state loop under the shared PSX machine.
  vm.bindHostCall(guest_profile.state_loop_entry,
                  [](sf::game::LegacyHostCallContext &context) {
                    context.continueGuestInstruction();
                  });
  const auto state_loop =
      vm.runCurrentPcUntilHostBoundary(guest_profile.state_loop_entry, budget);
  if (!state_loop.stoppedAtHostBoundary()) {
    std::cerr << "SF2 guest did not reach state loop: stop=0x" << std::hex
              << std::uppercase << state_loop.execution.pc << std::dec
              << " reason=" << sf::psx::toString(state_loop.execution.reason)
              << " instructions=" << state_loop.execution.instructions << '\n';
    return 3;
  }

  struct BoundaryTrace {
    std::array<std::uint64_t, frame_boundary_count> instructions{};
    std::array<std::uint32_t, frame_boundary_count> retrace_counters{};
    std::array<std::uint32_t, frame_boundary_count> return_addresses{};
    std::array<std::uint32_t, frame_boundary_count> application_states{};
  };
  auto suppress_guest_interrupts = false;
  const std::array<std::uint32_t, 2U> sf2_root_callback_slots{
      guest_profile.interrupt_callback_table + 4U * 4U,
      guest_profile.interrupt_callback_table + 7U * 4U};
  constexpr std::uint32_t sf2_callback_stack = 0x807f0000U;
  constexpr std::uint64_t sf2_task_callback_period =
      sf::psx::CdRomController::cpu_clock_hz /
      sf::game::LegacyGameplayVm::updates_per_second;
  constexpr std::uint64_t sf2_retrace_period =
      sf::psx::CdRomController::cpu_clock_hz / 60U;
  auto sf2_task_callback_ticks = std::uint64_t{};
  auto sf2_retrace_ticks = std::uint64_t{};
  auto sf2_cd_completion_interrupts = std::size_t{};
  std::uint32_t sf2_cd_completion_callback_at_interrupt{};
  std::vector<std::array<std::uint32_t, 3U>> sf2_cd_completion_trace;
  auto sf2_cd_dma_callbacks = std::size_t{};
  auto sf2_spu_dma_callbacks = std::size_t{};
  std::string sf2_scheduler_failure;
  std::optional<sf::game::LegacyGameplayVmResult> sf2_callback_failure;
  std::uint32_t sf2_callback_failure_slot{};
  std::uint32_t sf2_callback_failure_address{};
  const auto service_sf2_cd_callback = [&]() {
    if ((vm.machine().cdrom().captureState().interrupt_flags & 0x07U) == 2U) {
      ++sf2_cd_completion_interrupts;
      static_cast<void>(vm.runtime().read32(
          0x8011d1bcU, sf2_cd_completion_callback_at_interrupt));
      std::uint16_t pending_command{};
      const auto cdrom = vm.machine().cdrom().captureState();
      static_cast<void>(
          vm.runtime().read16(0x8011bdf2U, pending_command));
      if (sf2_cd_completion_trace.size() < 32U) {
        sf2_cd_completion_trace.push_back(
            {pending_command, sf2_cd_completion_callback_at_interrupt,
             cdrom.response_count != 0U
                 ? cdrom.response[cdrom.response_position]
                 : 0xffffffffU});
      }
    }
    return vm.servicePsxCdReadyCallback();
  };
  const auto advance_sf2_retrace_counter = [&](std::uint64_t ticks) {
    sf2_retrace_ticks += ticks;
    const auto retraces = sf2_retrace_ticks / sf2_retrace_period;
    sf2_retrace_ticks %= sf2_retrace_period;
    if (retraces == 0U) {
      return true;
    }
    std::uint32_t counter{};
    return vm.runtime().read32(layout.retrace_counter_address, counter) &&
           vm.runtime().write32(
               layout.retrace_counter_address,
               counter + static_cast<std::uint32_t>(retraces));
  };
  const auto advance_sf2_root_callbacks = [&](std::uint64_t ticks) {
    sf2_task_callback_ticks += ticks;
    while (sf2_task_callback_ticks >= sf2_task_callback_period) {
      sf2_task_callback_ticks -= sf2_task_callback_period;
      for (const auto slot : sf2_root_callback_slots) {
        sf::game::LegacyGameplayVmResult callback_result;
        sf2_servicing_scheduled_callback = true;
        const auto callback_succeeded = vm.servicePsxCallbackSlot(
            slot, sf2_callback_stack, &callback_result);
        sf2_servicing_scheduled_callback = false;
        if (!callback_succeeded) {
          sf2_callback_failure = callback_result;
          sf2_callback_failure_slot = slot;
          static_cast<void>(
              vm.runtime().read32(slot, sf2_callback_failure_address));
          return false;
        }
      }
    }
    return true;
  };
  const auto service_sf2_spu_dma_callback = [&]() {
    constexpr std::uint32_t dma_interrupt_control_address = 0x1f8010f4U;
    constexpr std::uint32_t spu_dma_flag = 1U << (24U + 4U);
    constexpr std::uint32_t spu_transfer_callback_slot = 0x8011e3acU;
    std::uint32_t interrupt_control{};
    if (!vm.runtime().read32(dma_interrupt_control_address,
                             interrupt_control)) {
      return false;
    }
    if ((interrupt_control & spu_dma_flag) == 0U) {
      return true;
    }
    sf::game::LegacyGameplayVmResult callback_result;
    if (!vm.runtime().write32(dma_interrupt_control_address,
                              (interrupt_control & 0x00ffffffU) |
                                  spu_dma_flag) ||
        !vm.servicePsxCallbackSlot(spu_transfer_callback_slot,
                                   sf2_callback_stack, &callback_result)) {
      sf2_callback_failure = callback_result;
      sf2_callback_failure_slot = spu_transfer_callback_slot;
      static_cast<void>(vm.runtime().read32(spu_transfer_callback_slot,
                                            sf2_callback_failure_address));
      return false;
    }
    ++sf2_spu_dma_callbacks;
    return true;
  };
  const auto service_sf2_cd_dma_callback = [&]() {
    constexpr std::uint32_t dma_interrupt_control_address = 0x1f8010f4U;
    constexpr std::uint32_t cd_dma_flag = 1U << (24U + 3U);
    // Retail DMACallback at 0x801011a4 indexes the SDK callback array as
    // 0x8011d108 + channel * 4. STR registers channel 3 through
    // 0x800f61cc -> 0x800f4c20.
    constexpr std::uint32_t cd_transfer_callback_slot = 0x8011d114U;
    std::uint32_t interrupt_control{};
    if (!vm.runtime().read32(dma_interrupt_control_address,
                             interrupt_control)) {
      return false;
    }
    if ((interrupt_control & cd_dma_flag) == 0U) {
      return true;
    }
    sf::game::LegacyGameplayVmResult callback_result;
    if (!vm.runtime().write32(dma_interrupt_control_address,
                              (interrupt_control & 0x00ffffffU) |
                                  cd_dma_flag) ||
        !vm.servicePsxCallbackSlot(cd_transfer_callback_slot,
                                   sf2_callback_stack, &callback_result)) {
      sf2_callback_failure = callback_result;
      sf2_callback_failure_slot = cd_transfer_callback_slot;
      static_cast<void>(vm.runtime().read32(cd_transfer_callback_slot,
                                            sf2_callback_failure_address));
      return false;
    }
    ++sf2_cd_dma_callbacks;
    return true;
  };
  const auto service_sf2_scheduler_slice = [&](std::uint64_t ticks) {
    sf2_scheduler_failure.clear();
    sf2_callback_failure.reset();
    sf2_callback_failure_slot = 0U;
    sf2_callback_failure_address = 0U;
    if (suppress_guest_interrupts) {
      vm.machine().advanceHardwareTicks(ticks);
    }
    if (!advance_sf2_retrace_counter(ticks)) {
      sf2_scheduler_failure = "retrace";
      return false;
    }
    if (!service_sf2_cd_callback()) {
      sf2_scheduler_failure = "cd-ready";
      return false;
    }
    if (!service_sf2_cd_dma_callback()) {
      sf2_scheduler_failure = "cd-dma";
      return false;
    }
    if (!service_sf2_spu_dma_callback()) {
      sf2_scheduler_failure = "spu-dma";
      return false;
    }
    if (!advance_sf2_root_callbacks(ticks)) {
      sf2_scheduler_failure = "root-counter";
      return false;
    }
    return true;
  };
  const auto run_scheduled_until_boundary = [&](std::uint32_t address) {
    constexpr std::uint32_t interrupt_status_address = 0x1f801070U;
    if (suppress_guest_interrupts) {
      static_cast<void>(vm.runtime().write16(interrupt_status_address, 0U));
      vm.runtime().setExternalInterrupt(false);
    }
    auto remaining = budget;
    auto total_instructions = std::uint64_t{};
    sf::game::LegacyGameplayVmResult result;
    for (;;) {
      const auto slice = std::min(remaining, scheduler_slice_budget);
      result = suppress_guest_interrupts
                   ? vm.runCurrentPcUntilHostBoundaryClockNeutral(address,
                                                                  slice)
                   : vm.runCurrentPcUntilHostBoundary(address, slice);
      total_instructions += result.execution.instructions;
      if (!service_sf2_scheduler_slice(result.execution.instructions)) {
        result.execution.reason = sf::psx::R3000StopReason::memory_fault;
        result.execution.instructions = total_instructions;
        return result;
      }
      if (suppress_guest_interrupts) {
        static_cast<void>(vm.runtime().write16(interrupt_status_address, 0U));
        vm.runtime().setExternalInterrupt(false);
      }
      if (result.stoppedAtHostBoundary() ||
          result.execution.reason !=
              sf::psx::R3000StopReason::instruction_budget ||
          remaining <= slice) {
        result.execution.instructions = total_instructions;
        return result;
      }
      remaining -= slice;
    }
  };
  const auto invoke_scheduled = [&](std::uint32_t address,
                                    std::span<const std::uint32_t> arguments) {
    auto remaining = budget;
    const auto first_slice = std::min(remaining, scheduler_slice_budget);
    auto result = suppress_guest_interrupts
                      ? vm.invokeClockNeutral(address, arguments, first_slice)
                      : vm.invoke(address, arguments, first_slice);
    auto total_instructions = result.execution.instructions;
    remaining -= first_slice;
    if (!service_sf2_scheduler_slice(result.execution.instructions)) {
      result.execution.reason = sf::psx::R3000StopReason::memory_fault;
      result.execution.instructions = total_instructions;
      return result;
    }
    while (result.execution.reason ==
               sf::psx::R3000StopReason::instruction_budget &&
           remaining != 0U) {
      const auto slice = std::min(remaining, scheduler_slice_budget);
      if (suppress_guest_interrupts) {
        vm.runtime().setExternalInterrupt(false);
      }
      result = suppress_guest_interrupts
                   ? vm.resumeCurrentPcClockNeutral(slice)
                   : vm.resumeCurrentPc(slice);
      total_instructions += result.execution.instructions;
      remaining -= slice;
      if (!service_sf2_scheduler_slice(result.execution.instructions)) {
        result.execution.reason = sf::psx::R3000StopReason::memory_fault;
        break;
      }
    }
    result.execution.instructions = total_instructions;
    return result;
  };
  const auto invoke_nested_scheduled =
      [&](std::uint32_t address, std::span<const std::uint32_t> arguments) {
        constexpr std::uint32_t return_trampoline = 0x8000c000U;
        const auto continuation_state = vm.runtime().state();
        vm.bindHostCall(return_trampoline,
                        [](sf::game::LegacyHostCallContext &context) {
                          context.continueGuestInstruction();
                        });
        if (!vm.runtime().beginCall(address, arguments)) {
          return sf::game::LegacyGameplayVmResult{
              {sf::psx::R3000StopReason::memory_fault, 0U, address, 0U},
              vm.runtime().state().gpr[2U],
              0U,
              std::nullopt,
          };
        }
        vm.runtime().setRegister(31U, return_trampoline);
        auto result = run_scheduled_until_boundary(return_trampoline);
        if (result.stoppedAtHostBoundary() || result.completed()) {
          vm.runtime().restoreCpuState(continuation_state);
        }
        return result;
      };
  const auto trace_boundaries = [&](BoundaryTrace &trace) {
    for (std::size_t frame = 0U; frame < frame_boundary_count; ++frame) {
      const auto boundary =
          run_scheduled_until_boundary(guest_profile.gpu_submission_entry);
      if (!boundary.stoppedAtHostBoundary()) {
        std::cerr << "SF2 guest stopped before GPU boundary " << frame
                  << ": stop=0x" << std::hex << std::uppercase
                  << boundary.execution.pc << std::dec
                  << " reason=" << sf::psx::toString(boundary.execution.reason)
                  << " instructions=" << boundary.execution.instructions
                  << '\n';
        return false;
      }
      trace.instructions[frame] = boundary.execution.instructions;
      trace.return_addresses[frame] = vm.runtime().state().gpr[31U];
      if (!vm.runtime().read32(layout.retrace_counter_address,
                               trace.retrace_counters[frame]) ||
          !vm.runtime().read32(guest_profile.application_state,
                               trace.application_states[frame])) {
        return false;
      }

      // Retire the observed GPU host call before looking for the following
      // boundary. Otherwise the boundary-aware pump correctly yields again at
      // the same PC without proving that the guest loop advanced.
      const auto retired = vm.resumeCurrentPcClockNeutral(1U);
      if (retired.execution.reason !=
          sf::psx::R3000StopReason::instruction_budget) {
        std::cerr << "SF2 guest could not retire GPU boundary " << frame
                  << ": stop=0x" << std::hex << std::uppercase
                  << retired.execution.pc << std::dec
                  << " reason=" << sf::psx::toString(retired.execution.reason)
                  << '\n';
        return false;
      }
    }
    return true;
  };

  // The executable can reach its state-loop entry with a bootstrap INT1
  // already pending. Deliver that retail callback before entering the first
  // application slice; otherwise Common_Init(1)'s immediate Setloc retries
  // all observe the stale controller IRQ before the periodic scheduler gets
  // its first chance to run.
  if (!service_sf2_scheduler_slice(0U)) {
    return 4;
  }
  const auto state_loop_snapshot = vm.captureSnapshot();
  BoundaryTrace trace;
  if (!trace_boundaries(trace)) {
    return 4;
  }
  if (!vm.restoreSnapshot(state_loop_snapshot)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Could not restore SF2 state-loop snapshot"};
  }
  BoundaryTrace replay;
  if (!trace_boundaries(replay) || replay.instructions != trace.instructions ||
      replay.retrace_counters != trace.retrace_counters ||
      replay.return_addresses != trace.return_addresses ||
      replay.application_states != trace.application_states) {
    std::cerr << "SF2 guest GPU boundary replay diverged\n";
    return 5;
  }
  const auto init_code_is_loaded = [&]() {
    std::array<std::byte, init_code_probe_size> guest_code{};
    return init_code_offset + guest_code.size() <= init_overlay.size() &&
           vm.runtime().copyBytes(
               0x80158878U + static_cast<std::uint32_t>(init_code_offset),
               guest_code) &&
           std::ranges::equal(guest_code,
                              std::span<const std::byte>{init_overlay}.subspan(
                                  init_code_offset, guest_code.size()));
  };
  // The first state-loop boundary can still own the executable's bootstrap
  // read. Starting Common_Init from that point races a second CdRead against
  // stale INT1 sectors. Keep executing the retail loop until libcd and the
  // controller have remained idle for one complete 20 Hz task period, then
  // use that application-owned continuation for every later transition.
  suppress_guest_interrupts = true;
  constexpr std::uint32_t interrupt_status_address = 0x1f801070U;
  static_cast<void>(vm.runtime().write16(interrupt_status_address, 0U));
  vm.runtime().setExternalInterrupt(false);
  const auto sf2_cd_is_quiescent = [&]() {
    const auto cdrom = vm.machine().cdrom().captureState();
    std::uint8_t guest_read_cleanup{};
    return vm.runtime().read8(0x8011cf6cU, guest_read_cleanup) &&
           guest_read_cleanup == 0U &&
           (cdrom.interrupt_flags & 0x07U) == 0U &&
           cdrom.pending_command == 0U &&
           cdrom.command_phase == sf::psx::CdRomCommandPhase::idle &&
           cdrom.reading == 0U && cdrom.seeking == 0U &&
           cdrom.command_event.pending == 0U &&
           cdrom.sector_event.pending == 0U;
  };
  const auto sf2_frontend_bootstrap_is_ready = [&]() {
    std::uint8_t disc_index{};
    std::uint32_t task_callback{};
    return sf2_cd_is_quiescent() &&
           vm.runtime().read8(0x8011f608U, disc_index) &&
           (!probe_mission_transition || disc_index < 2U) &&
           vm.runtime().read32(sf2_root_callback_slots.front(),
                               task_callback) &&
           task_callback == 0x80022584U &&
           (!probe_mission_transition || init_code_is_loaded());
  };
  auto resident_boundaries = frame_boundary_count;
  auto quiescent_boundaries = std::size_t{};
  constexpr std::size_t bootstrap_settle_boundary_limit = 2'000U;
  while (resident_boundaries < bootstrap_settle_boundary_limit &&
         quiescent_boundaries < 1U) {
    const auto was_quiescent = sf2_frontend_bootstrap_is_ready();
    const auto boundary =
        run_scheduled_until_boundary(guest_profile.gpu_submission_entry);
    if (!boundary.stoppedAtHostBoundary()) {
      std::uint32_t failed_application_state{};
      std::uint32_t failed_hog_offset{};
      std::uint32_t failed_task_callback{};
      std::uint8_t failed_disc_index{};
      std::uint8_t failed_read_cleanup{};
      static_cast<void>(vm.runtime().read32(guest_profile.application_state,
                                             failed_application_state));
      static_cast<void>(
          vm.runtime().read32(0x801b92b4U, failed_hog_offset));
      static_cast<void>(
          vm.runtime().read8(0x8011f608U, failed_disc_index));
      static_cast<void>(vm.runtime().read32(sf2_root_callback_slots.front(),
                                            failed_task_callback));
      static_cast<void>(
          vm.runtime().read8(0x8011cf6cU, failed_read_cleanup));
      const auto failed_cdrom = vm.machine().cdrom().captureState();
      std::cerr << "SF2 bootstrap did not settle at an application frame: "
                << "stop=0x" << std::hex << std::uppercase
                << boundary.execution.pc << std::dec << " reason="
                << sf::psx::toString(boundary.execution.reason)
                << " instructions=" << boundary.execution.instructions
                << " t1=0x" << std::hex << std::uppercase
                << vm.runtime().state().gpr[9U] << " ra=0x"
                << vm.runtime().state().gpr[31U] << " a0=0x"
                << vm.runtime().state().gpr[4U] << " a1=0x"
                << vm.runtime().state().gpr[5U] << std::dec
                << " app=" << failed_application_state
                << " disc-index="
                << static_cast<unsigned int>(failed_disc_index)
                << " hog=0x" << std::hex << std::uppercase
                << failed_hog_offset << " task=0x" << failed_task_callback
                << std::dec << " init-code="
                << (init_code_is_loaded() ? 1 : 0)
                << " read-cleanup="
                << static_cast<unsigned int>(failed_read_cleanup)
                << " cd=" << static_cast<unsigned int>(
                                   failed_cdrom.interrupt_flags & 0x07U)
                << '/' << static_cast<unsigned int>(failed_cdrom.pending_command)
                << '/' << static_cast<unsigned int>(failed_cdrom.reading)
                << '/' << static_cast<unsigned int>(failed_cdrom.seeking)
                << " opens=";
      for (const auto &path : sf2_file_open_paths) {
        std::cerr << path << '/';
      }
      std::cerr << '\n';
      return 6;
    }
    ++resident_boundaries;
    if (!sf2_frontend_bootstrap_is_ready()) {
      quiescent_boundaries = 0U;
    } else if (was_quiescent) {
      ++quiescent_boundaries;
    } else {
      quiescent_boundaries = 1U;
    }
    const auto retired = vm.resumeCurrentPcClockNeutral(1U);
    const auto frame_padding =
        boundary.execution.instructions < sf2_retrace_period
            ? sf2_retrace_period - boundary.execution.instructions
            : 0U;
    if (retired.execution.reason !=
            sf::psx::R3000StopReason::instruction_budget ||
        !service_sf2_scheduler_slice(retired.execution.instructions +
                                     frame_padding)) {
      return 6;
    }
  }
  if (quiescent_boundaries < 1U) {
    const auto cdrom = vm.machine().cdrom().captureState();
    std::cerr << "SF2 bootstrap CD path did not become quiescent: irq="
              << static_cast<unsigned int>(cdrom.interrupt_flags & 0x07U)
              << " command="
              << static_cast<unsigned int>(cdrom.pending_command)
              << " reading=" << static_cast<unsigned int>(cdrom.reading)
              << " seeking=" << static_cast<unsigned int>(cdrom.seeking)
              << " boundaries=" << resident_boundaries << '\n';
    return 6;
  }
  const auto resident_loop_snapshot = vm.captureSnapshot();
  std::vector<std::byte> guest_init_overlay(init_overlay.size());
  const auto init_overlay_loaded =
      !init_overlay.empty() &&
      vm.runtime().copyBytes(0x80158878U, guest_init_overlay) &&
      std::ranges::equal(guest_init_overlay, init_overlay);
  const auto init_code_loaded = init_code_is_loaded();

  std::cout << "SF2 guest bootstrap passed: disc="
            << static_cast<unsigned int>(disc.game()->disc_number)
            << " entry=0x" << std::hex << std::uppercase
            << disc.executable().header().initial_pc << " state-loop=0x"
            << guest_profile.state_loop_entry << " gpu-submit=0x"
            << guest_profile.gpu_submission_entry << std::dec
            << " state-loop-instructions=" << state_loop.execution.instructions
            << " frame-boundaries=" << frame_boundary_count
            << " frame-instructions=";
  for (std::size_t frame = 0U; frame < frame_boundary_count; ++frame) {
    if (frame != 0U) {
      std::cout << '/';
    }
    std::cout << trace.instructions[frame];
  }
  std::cout << " retrace=";
  for (std::size_t frame = 0U; frame < frame_boundary_count; ++frame) {
    if (frame != 0U) {
      std::cout << '/';
    }
    std::cout << trace.retrace_counters[frame];
  }
  std::cout << " callers=";
  for (std::size_t frame = 0U; frame < frame_boundary_count; ++frame) {
    if (frame != 0U) {
      std::cout << '/';
    }
    std::cout << "0x" << std::hex << std::uppercase
              << trace.return_addresses[frame] << std::dec;
  }
  std::cout << " states=";
  for (std::size_t frame = 0U; frame < frame_boundary_count; ++frame) {
    if (frame != 0U) {
      std::cout << '/';
    }
    std::cout << trace.application_states[frame];
  }
  std::cout << " replay=identical resident-init="
            << (!probe_mission_transition ? "not-checked"
                : init_overlay_loaded     ? "exact"
                : init_code_loaded        ? "code-exact"
                                          : "missing-or-different")
             << " resident-boundaries=" << resident_boundaries;
  if (!probe_mission_transition) {
    std::cout << '\n';
    return 0;
  }

  // Common_Init(4) is the mission-mode initializer. Invoking it here starts
  // the default COLO mission and is not a frontend bootstrap checkpoint.
  // Continue from the quiescent executable frame into the retail selector
  // instead of using that old synthetic detour.
  std::cout << " common-init4=not-invoked\n";

  // MENU.OVL and TITLE.OVL both enter MissionArchive_Open with the zero-based
  // selection index followed by two true flags. Execute that retail
  // transition directly, then return to the original application loop so its
  // state-12 loader owns every subsequent FOG and overlay operation.
  if (!vm.restoreSnapshot(resident_loop_snapshot)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Could not restore SF2 mission-selection snapshot"};
  }
  enable_sf2_mission_search = true;
  enable_sf2_resident_overlay_load = true;
  enable_sf2_resident_file_access = true;
  std::uint32_t continuous_state{};
  std::uint32_t continuous_depth{};
  if (!vm.runtime().read32(guest_profile.application_state, continuous_state) ||
      !vm.runtime().read32(guest_profile.application_state_depth,
                           continuous_depth)) {
    return 7;
  }
  // Executable bootstrap already ran Common_Init(1) before entering this
  // state loop. Re-entering it reloads INIT over live allocator state. Resume
  // only this initialized retail frame and verify its resident MOVIE code.
  const auto movie_file = resident_files.find("MOVIE.OVL");
  std::vector<std::byte> continuous_movie_bytes;
  if (movie_file != resident_files.end()) {
    continuous_movie_bytes.resize(movie_file->second.size());
  }
  const auto continuous_movie_exact =
      !continuous_movie_bytes.empty() &&
      vm.runtime().copyBytes(0x80142150U, continuous_movie_bytes) &&
      std::ranges::equal(continuous_movie_bytes, movie_file->second);
  auto continuous_movie_code_exact = false;
  if (movie_file != resident_files.end()) {
    const auto movie_code_offset =
        sequelResidentOverlayCodeOffset(movie_file->second, 0x80142150U);
    std::array<std::byte, init_code_probe_size> guest_movie_code{};
    continuous_movie_code_exact =
        movie_code_offset + guest_movie_code.size() <=
            movie_file->second.size() &&
        vm.runtime().copyBytes(
            0x80142150U + static_cast<std::uint32_t>(movie_code_offset),
            guest_movie_code) &&
        std::ranges::equal(
            guest_movie_code,
            std::span<const std::byte>{movie_file->second}.subspan(
                movie_code_offset, guest_movie_code.size()));
  }
  std::array<std::uint32_t, 4U> continuous_movie_state{};
  std::uint8_t continuous_disc_index{};
  static_cast<void>(vm.runtime().read8(0x8011f608U,
                                       continuous_disc_index));
  for (std::size_t index = 0U; index < continuous_movie_state.size(); ++index) {
    static_cast<void>(vm.runtime().read32(
        0x80146710U + static_cast<std::uint32_t>(index * 4U),
        continuous_movie_state[index]));
  }
  std::cerr << "SF2 continuous frontend diagnostic: common-init1=bootstrap"
            << " exact=" << (continuous_movie_exact ? 1 : 0)
            << " code-exact=" << (continuous_movie_code_exact ? 1 : 0)
            << " resident-file=" << sf2_resident_file_open_bridges << '/'
            << sf2_resident_file_load_bridges << " overlay="
            << sf2_last_resident_overlay_name << "@0x" << std::hex
            << std::uppercase
            << sf2_last_resident_overlay_address << std::dec << ':'
            << sf2_last_resident_overlay_mode << '/'
            << sf2_resident_overlay_load_bridges << " movie/title="
            << sf2_movie_overlay_load_bridges << '/'
            << sf2_title_overlay_load_bridges
            << " disc-index="
            << static_cast<unsigned int>(continuous_disc_index)
            << " cd-search=" << sf2_search_matches << '/'
            << sf2_search_calls
            << " movie-state=" << std::hex << std::uppercase
            << continuous_movie_state[0] << '/' << continuous_movie_state[1]
            << '/' << continuous_movie_state[2] << '/'
            << continuous_movie_state[3] << std::dec << " common-init=";
  for (std::size_t index = 0U; index < sf2_common_init_arguments.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << sf2_common_init_arguments[index];
  }
  std::cerr << " disc-open=";
  for (std::size_t index = 0U;
       index < sf2_common_disc_open_results.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << sf2_common_disc_open_results[index];
  }
  std::cerr << " seek=";
  for (std::size_t index = 0U; index < sf2_file_seek_results.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << sf2_file_seek_results[index][0] << ','
              << sf2_file_seek_results[index][1] << std::dec;
  }
  std::cerr << '\n';
  std::cerr << "SF2 file-open diagnostic:";
  for (const auto &path : sf2_file_open_paths) {
    std::cerr << ' ' << path;
  }
  std::cerr << '\n';
  if (!continuous_movie_code_exact) {
    return 7;
  }
  std::uint32_t sf2_task_callback{};
  if (!vm.runtime().read32(sf2_root_callback_slots.front(),
                           sf2_task_callback) ||
      sf2_task_callback != 0x80022584U) {
    return 7;
  }
  auto movie_stream_starts = std::size_t{};
  auto movie_ready_dispatches = std::size_t{};
  auto title_handoffs = std::size_t{};
  auto title_mission_opens = std::size_t{};
  auto title_frame_updates = std::size_t{};
  auto title_selector_calls = std::size_t{};
  auto state_loop_epilogues = std::size_t{};
  auto game_main_returns = std::size_t{};
  std::uint32_t title_frame_return{};
  std::uint32_t title_selector_return{};
  std::array<std::uint32_t, 3U> title_mission_arguments{};
  std::vector<std::array<std::uint32_t, 2U>> title_selector_arguments;
  auto title_input_dispatches = std::size_t{};
  auto title_confirm_callbacks = std::size_t{};
  auto title_accept_callbacks = std::size_t{};
  auto title_cancel_callbacks = std::size_t{};
  std::vector<std::array<std::uint32_t, 3U>> title_button_edges;
  std::vector<std::array<std::uint32_t, 4U>> title_pad_reads;
  auto movie_open_calls = std::size_t{};
  auto movie_open_returns = std::size_t{};
  std::array<std::uint32_t, 8U> movie_open_arguments{};
  std::uint32_t movie_open_result{};
  std::uint8_t movie_open_active{};
  std::array<std::size_t, 7U> movie_task_trace{};
  constexpr std::array<std::uint32_t, 7U> movie_task_entries{
      0x8002686cU, 0x800226d4U, 0x80022584U, 0x800266d4U,
      0x800265a8U, 0x8002619cU, 0x8002676cU};
  std::vector<std::array<std::uint32_t, 3U>> movie_sync_results;
  auto movie_ring_callbacks = std::size_t{};
  std::array<std::uint32_t, 3U> movie_dma_registration{};
  auto movie_cd_dma_starts = std::size_t{};
  std::vector<std::uint32_t> movie_cd_read_start_times;
  std::vector<std::array<std::uint32_t, 6U>> movie_ready_trace;
  std::vector<std::uint32_t> title_input_states;
  constexpr std::array movie_preflight_entries{
      0x800f7808U, 0x800f703cU, 0x80153d30U,
      0x8002a338U, 0x80153e24U, 0x8002b9a8U,
      0x8002a028U, 0x800296e8U};
  vm.bindHostCall(
      movie_preflight_entries[0],
      [&movie_stream_starts](sf::game::LegacyHostCallContext &context) {
        ++movie_stream_starts;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[1],
      [&movie_ready_dispatches,
       &movie_ready_trace](sf::game::LegacyHostCallContext &context) {
        ++movie_ready_dispatches;
        if (movie_ready_trace.size() < 16U) {
          std::array<std::uint32_t, 6U> trace{
              context.registerValue(4U), context.registerValue(5U)};
          static_cast<void>(context.read32(0x8011cf58U, trace[2U]));
          static_cast<void>(context.read32(0x8011cf5cU, trace[3U]));
          static_cast<void>(context.read32(0x8011cf60U, trace[4U]));
          static_cast<void>(context.read32(0x8011cf68U, trace[5U]));
          movie_ready_trace.push_back(trace);
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[2],
      [&title_handoffs](sf::game::LegacyHostCallContext &context) {
        ++title_handoffs;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[3],
      [&title_mission_opens,
       &title_mission_arguments](sf::game::LegacyHostCallContext &context) {
        ++title_mission_opens;
        title_mission_arguments = {context.argument(0), context.argument(1),
                                   context.argument(2)};
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[4],
      [&title_frame_updates,
       &title_frame_return](sf::game::LegacyHostCallContext &context) {
        ++title_frame_updates;
        title_frame_return = context.registerValue(31U);
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[5],
      [&title_selector_calls,
       &title_selector_return,
       &title_selector_arguments](sf::game::LegacyHostCallContext &context) {
        ++title_selector_calls;
        title_selector_return = context.registerValue(31U);
        if (title_selector_arguments.size() < 16U) {
          title_selector_arguments.push_back(
              {context.argument(0), context.argument(1)});
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[6],
      [&state_loop_epilogues](sf::game::LegacyHostCallContext &context) {
        ++state_loop_epilogues;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      movie_preflight_entries[7],
      [&game_main_returns](sf::game::LegacyHostCallContext &context) {
        ++game_main_returns;
        context.continueGuestInstruction();
      });
  auto movie_preflight_stop = sf::game::LegacyGameplayVmResult{};
  std::vector<std::uint32_t> movie_preflight_title_states;
  std::vector<std::uint32_t> movie_preflight_application_states;
  std::vector<std::uint8_t> movie_preflight_transitions;
  enum class TitlePadPulse {
    waiting,
    armed,
    pressed,
    released,
  };
  auto title_pad_pulse = TitlePadPulse::waiting;
  auto title_pad_samples = std::size_t{};
  std::uint16_t title_injected_buttons{};
  constexpr std::uint32_t sf2_primary_pad_state = 0x80122fecU;
  vm.bindHostCall(
      0x801538c4U,
      [&title_input_dispatches, &title_input_states](
          sf::game::LegacyHostCallContext &context) {
        ++title_input_dispatches;
        const auto title_state = context.argument(0);
        if (title_input_states.size() < 32U &&
            (title_input_states.empty() ||
             title_input_states.back() != title_state)) {
          title_input_states.push_back(title_state);
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80153a34U,
      [&title_button_edges,
       &title_pad_pulse](sf::game::LegacyHostCallContext &context) {
        std::uint32_t edge{};
        static_cast<void>(
            context.read32(context.registerValue(29U) + 0x14U, edge));
        if (title_button_edges.size() < 16U) {
          title_button_edges.push_back(
              {context.registerValue(17U), edge, context.registerValue(19U)});
        }
        if ((edge & 0x0008U) != 0U) {
          title_pad_pulse = TitlePadPulse::released;
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x8015394cU,
      [&title_pad_reads](sf::game::LegacyHostCallContext &context) {
        if (title_pad_reads.size() < 32U) {
          std::uint32_t previous{};
          static_cast<void>(context.read32(context.registerValue(18U),
                                           previous));
          title_pad_reads.push_back(
              {context.registerValue(17U), previous,
               context.registerValue(16U), context.registerValue(19U)});
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80153930U,
      [&title_pad_pulse, &title_pad_samples,
       &title_injected_buttons](sf::game::LegacyHostCallContext &context) {
        std::uint32_t pad_state{};
        if (!context.read32(context.registerValue(29U) + 0x10U,
                            pad_state) ||
            pad_state == 0U) {
          context.rejectHostCall();
          return;
        }
        if (context.registerValue(20U) == 0U) {
          ++title_pad_samples;
          title_injected_buttons = 0U;
          if (title_pad_pulse == TitlePadPulse::waiting) {
            title_pad_pulse = TitlePadPulse::armed;
          } else if (title_pad_pulse == TitlePadPulse::armed &&
                     title_pad_samples >= 16U) {
            title_injected_buttons = 0x0008U;
            title_pad_pulse = TitlePadPulse::pressed;
          } else if (title_pad_pulse == TitlePadPulse::pressed) {
            title_injected_buttons = 0x0008U;
          }
        }
        if (!context.write8(pad_state, 0U) ||
            !context.write16(pad_state + 4U, title_injected_buttons)) {
          context.rejectHostCall();
          return;
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x800222bcU,
      [&title_pad_pulse, &title_pad_samples, &title_injected_buttons,
       sf2_primary_pad_state](sf::game::LegacyHostCallContext &context) {
        std::uint32_t title_state{};
        const auto pad_index = context.argument(1);
        if ((pad_index != 0U && pad_index != 4U) ||
            !context.read32(0x80156bdcU, title_state) ||
            title_state != 3U) {
          context.continueGuestInstruction();
          return;
        }
        if (pad_index == 0U) {
          ++title_pad_samples;
          title_injected_buttons = 0U;
          if (title_pad_pulse == TitlePadPulse::waiting) {
            title_pad_pulse = TitlePadPulse::armed;
          } else if (title_pad_pulse == TitlePadPulse::armed &&
                     title_pad_samples >= 16U) {
            title_injected_buttons = 0x0008U;
            title_pad_pulse = TitlePadPulse::pressed;
          } else if (title_pad_pulse == TitlePadPulse::pressed) {
            title_injected_buttons = 0x0008U;
          }
        }
        const auto pad_state =
            sf2_primary_pad_state + pad_index * 60U;
        if (!context.write8(pad_state, 0U) ||
            !context.write16(pad_state + 4U, title_injected_buttons)) {
          context.rejectHostCall();
          return;
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x801501f0U,
      [&title_confirm_callbacks](sf::game::LegacyHostCallContext &context) {
        ++title_confirm_callbacks;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80151db0U,
      [&title_accept_callbacks](sf::game::LegacyHostCallContext &context) {
        ++title_accept_callbacks;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80152264U,
      [&title_cancel_callbacks](sf::game::LegacyHostCallContext &context) {
        ++title_cancel_callbacks;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80142e60U,
      [&movie_open_calls,
       &movie_open_arguments](sf::game::LegacyHostCallContext &context) {
        ++movie_open_calls;
        movie_open_arguments[0] = context.argument(0);
        movie_open_arguments[1] = context.argument(1);
        movie_open_arguments[2] = context.argument(2);
        movie_open_arguments[3] = context.argument(3);
        const auto stack = context.registerValue(29U);
        for (std::size_t index = 4U; index < movie_open_arguments.size();
             ++index) {
          static_cast<void>(context.read32(
              stack + static_cast<std::uint32_t>(index * 4U),
              movie_open_arguments[index]));
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x8002c27cU,
      [&movie_open_returns, &movie_open_result,
       &movie_open_active](sf::game::LegacyHostCallContext &context) {
        ++movie_open_returns;
        movie_open_result = context.registerValue(2U);
        static_cast<void>(context.read8(0x80146710U, movie_open_active));
        context.continueGuestInstruction();
      });
  for (std::size_t index = 0U; index < movie_task_entries.size(); ++index) {
    vm.bindHostCall(
        movie_task_entries[index],
        [&movie_task_trace, index](
            sf::game::LegacyHostCallContext &context) {
          ++movie_task_trace[index];
          context.continueGuestInstruction();
        });
  }
  vm.bindHostCall(
      0x800266fcU,
      [&movie_sync_results](sf::game::LegacyHostCallContext &context) {
        if (movie_sync_results.size() < 32U) {
          std::uint8_t callback_result{};
          std::uint8_t completion_result{};
          std::uint8_t completion_state{};
          static_cast<void>(context.read8(context.registerValue(29U) + 0x10U,
                                          callback_result));
          static_cast<void>(context.read8(0x80141a10U, completion_result));
          static_cast<void>(context.read8(0x8011d498U, completion_state));
          movie_sync_results.push_back(
              {context.registerValue(2U), callback_result,
               (static_cast<std::uint32_t>(completion_state) << 8U) |
                   completion_result});
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x800ff11cU,
      [&movie_ring_callbacks](sf::game::LegacyHostCallContext &context) {
        ++movie_ring_callbacks;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x80104308U,
      [&movie_cd_dma_starts](sf::game::LegacyHostCallContext &context) {
        ++movie_cd_dma_starts;
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x800f7548U,
      [&movie_cd_read_start_times](
          sf::game::LegacyHostCallContext &context) {
        if (movie_cd_read_start_times.size() < 8U) {
          movie_cd_read_start_times.push_back(context.registerValue(2U));
        }
        context.continueGuestInstruction();
      });
  vm.bindHostCall(
      0x800f4c20U,
      [&movie_dma_registration](sf::game::LegacyHostCallContext &context) {
        std::uint32_t dispatch{};
        static_cast<void>(context.read32(0x8011ce78U, dispatch));
        movie_dma_registration[0U] = context.registerValue(4U);
        movie_dma_registration[1U] = context.registerValue(5U);
        if (dispatch != 0U) {
          static_cast<void>(
              context.read32(dispatch + 4U, movie_dma_registration[2U]));
        }
        context.continueGuestInstruction();
      });
  auto movie_frontend_seen = false;
  auto movie_attract_seen = false;
  for (std::size_t boundary_index = 0U; boundary_index < 3'000U;
       ++boundary_index) {
    movie_preflight_stop =
        run_scheduled_until_boundary(guest_profile.gpu_submission_entry);
    std::uint32_t title_state{};
    if (vm.runtime().read32(0x80156bdcU, title_state) &&
        (movie_preflight_title_states.empty() ||
         movie_preflight_title_states.back() != title_state) &&
        movie_preflight_title_states.size() < 32U) {
      movie_preflight_title_states.push_back(title_state);
    }
    movie_attract_seen = movie_attract_seen || title_state == 3U;
    std::uint32_t application_state{};
    if (vm.runtime().read32(guest_profile.application_state,
                            application_state) &&
        (movie_preflight_application_states.empty() ||
         movie_preflight_application_states.back() != application_state)) {
      movie_preflight_application_states.push_back(application_state);
    }
    movie_frontend_seen = movie_frontend_seen || application_state == 4U;
    std::uint8_t transition{};
    if (vm.runtime().read8(0x8011ee94U, transition) &&
        (movie_preflight_transitions.empty() ||
         movie_preflight_transitions.back() != transition)) {
      movie_preflight_transitions.push_back(transition);
    }
    if (title_handoffs != 0U || title_mission_opens != 0U ||
        (movie_frontend_seen && application_state != 4U) ||
        (movie_attract_seen && title_state != 3U) ||
        title_input_dispatches >= 32U) {
      break;
    }
    if (!movie_preflight_stop.stoppedAtHostBoundary()) {
      break;
    }
    const auto retired = vm.resumeCurrentPcClockNeutral(1U);
    if (retired.execution.reason !=
        sf::psx::R3000StopReason::instruction_budget) {
      return 7;
    }
    const auto frame_padding =
        movie_preflight_stop.execution.instructions < sf2_retrace_period
            ? sf2_retrace_period -
                  movie_preflight_stop.execution.instructions
            : 0U;
    if (!service_sf2_scheduler_slice(retired.execution.instructions +
                                     frame_padding)) {
      return 7;
    }
  }
  for (const auto entry : movie_preflight_entries) {
    if (!vm.unbindHostCall(entry)) {
      return 7;
    }
  }
  if (!vm.unbindHostCall(0x801538c4U)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x80153a34U)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x8015394cU)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x80153930U)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x800222bcU)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x801501f0U)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x80151db0U) ||
      !vm.unbindHostCall(0x80152264U)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x80142e60U) ||
      !vm.unbindHostCall(0x8002c27cU)) {
    return 7;
  }
  for (const auto entry : movie_task_entries) {
    if (!vm.unbindHostCall(entry)) {
      return 7;
    }
  }
  if (!vm.unbindHostCall(0x800266fcU)) {
    return 7;
  }
  if (!vm.unbindHostCall(0x800ff11cU)) {
    return 7;
  }
  const auto movie_preflight_passed =
      title_pad_pulse == TitlePadPulse::released &&
      movie_preflight_stop.stoppedAtHostBoundary();
  std::uint32_t movie_preflight_application_state{};
  std::uint32_t movie_preflight_application_depth{};
  std::uint32_t movie_preflight_title_selection{};
  std::uint32_t movie_preflight_title_clock{};
  std::uint32_t movie_preflight_title_deadline{};
  std::uint32_t movie_ring_base{};
  std::uint32_t movie_ring_index{};
  std::array<std::uint16_t, 4U> movie_ring_states{};
  std::array<std::uint32_t, 7U> movie_dma_state{};
  const auto movie_cd_state = vm.machine().cdrom().captureState();
  static_cast<void>(vm.runtime().read32(guest_profile.application_state,
                                        movie_preflight_application_state));
  static_cast<void>(vm.runtime().read32(guest_profile.application_state_depth,
                                        movie_preflight_application_depth));
  static_cast<void>(
      vm.runtime().read32(0x80156be4U, movie_preflight_title_selection));
  static_cast<void>(
      vm.runtime().read32(0x8011f668U, movie_preflight_title_clock));
  static_cast<void>(
      vm.runtime().read32(0x801582d0U, movie_preflight_title_deadline));
  static_cast<void>(vm.runtime().read32(0x801419c8U, movie_ring_base));
  static_cast<void>(vm.runtime().read32(0x801419d4U, movie_ring_index));
  static_cast<void>(vm.runtime().read32(0x1f8010f0U, movie_dma_state[0U]));
  static_cast<void>(vm.runtime().read32(0x1f8010f4U, movie_dma_state[1U]));
  static_cast<void>(vm.runtime().read32(0x1f8010b0U, movie_dma_state[2U]));
  static_cast<void>(vm.runtime().read32(0x1f8010b4U, movie_dma_state[3U]));
  static_cast<void>(vm.runtime().read32(0x1f8010b8U, movie_dma_state[4U]));
  static_cast<void>(vm.runtime().read32(0x8011d114U, movie_dma_state[5U]));
  static_cast<void>(vm.runtime().read32(layout.retrace_counter_address,
                                        movie_dma_state[6U]));
  if (movie_ring_base != 0U) {
    for (std::size_t index = 0U; index < movie_ring_states.size(); ++index) {
      static_cast<void>(vm.runtime().read16(
          movie_ring_base + static_cast<std::uint32_t>(index * 0x20U),
          movie_ring_states[index]));
    }
  }
  std::cerr << "SF2 MOVIE scheduler diagnostic: task=0x" << std::hex
            << std::uppercase << sf2_task_callback << std::dec
            << " catalog=" << sf2_movie_catalog_open_bridges << '/'
            << sf2_movie_catalog_load_bridges
            << " stream-start=" << movie_stream_starts
            << " ready=" << movie_ready_dispatches
            << " handoff=" << title_handoffs << '/' << title_mission_opens
            << ':' << title_mission_arguments[0] << '/'
            << title_mission_arguments[1] << '/' << title_mission_arguments[2]
             << " title-frame=" << title_frame_updates << "@0x" << std::hex
            << std::uppercase << title_frame_return << std::dec
             << " selector=" << title_selector_calls << "@0x" << std::hex
             << std::uppercase << title_selector_return << std::dec << ':';
  for (std::size_t index = 0U; index < title_selector_arguments.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << title_selector_arguments[index][0] << ','
              << title_selector_arguments[index][1];
  }
  std::cerr
            << " input=" << title_input_dispatches << '/'
            << title_confirm_callbacks << '/' << title_accept_callbacks << '/'
            << title_cancel_callbacks << ":edges=";
  for (std::size_t index = 0U; index < title_button_edges.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << title_button_edges[index][0U] << ','
              << title_button_edges[index][1U] << ','
              << title_button_edges[index][2U] << std::dec;
  }
  std::cerr << ":reads=";
  for (std::size_t index = 0U; index < title_pad_reads.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << title_pad_reads[index][0U] << ','
              << title_pad_reads[index][1U] << ','
              << title_pad_reads[index][2U] << ','
              << title_pad_reads[index][3U] << std::dec;
  }
  std::cerr << ':';
  for (std::size_t index = 0U; index < title_input_states.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << title_input_states[index];
  }
  std::cerr
             << " movie-open=" << movie_open_calls << '/'
             << movie_open_returns << ':' << movie_open_result << '/'
             << static_cast<unsigned int>(movie_open_active) << ':';
  for (std::size_t index = 0U; index < movie_open_arguments.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << movie_open_arguments[index] << std::dec;
  }
  std::cerr << " task-trace=";
  for (std::size_t index = 0U; index < movie_task_trace.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << movie_task_trace[index];
  }
  std::cerr << " sync=";
  for (std::size_t index = 0U; index < movie_sync_results.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << movie_sync_results[index][0]
              << ',' << movie_sync_results[index][1] << ',' << std::hex
              << std::uppercase << movie_sync_results[index][2] << std::dec;
  }
  std::cerr << " ring=" << movie_ring_callbacks << "@0x" << std::hex
            << std::uppercase << movie_ring_base << std::dec << ':'
            << movie_ring_index << ':';
  for (std::size_t index = 0U; index < movie_ring_states.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << movie_ring_states[index];
  }
  std::cerr << " dma-register=" << movie_dma_registration[0U] << "/0x"
            << std::hex << std::uppercase << movie_dma_registration[1U]
            << "/0x" << movie_dma_registration[2U] << std::dec
            << " dma-state=" << movie_cd_dma_starts << ':';
  for (std::size_t index = 0U; index < movie_dma_state.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << movie_dma_state[index] << std::dec;
  }
  std::cerr << " ready-trace=";
  for (std::size_t trace_index = 0U; trace_index < movie_ready_trace.size();
       ++trace_index) {
    std::cerr << (trace_index == 0U ? "" : ";");
    for (std::size_t field = 0U; field < movie_ready_trace[trace_index].size();
         ++field) {
      std::cerr << (field == 0U ? "" : ",") << std::hex << std::uppercase
                << movie_ready_trace[trace_index][field] << std::dec;
    }
  }
  std::cerr << " read-start=";
  for (std::size_t index = 0U; index < movie_cd_read_start_times.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << movie_cd_read_start_times[index];
  }
  std::cerr << " cd=" << static_cast<unsigned int>(movie_cd_state.reading)
            << '/' << static_cast<unsigned int>(movie_cd_state.seeking) << '/'
            << static_cast<unsigned int>(movie_cd_state.interrupt_flags & 7U)
            << '/' << movie_cd_state.current_lba << '/'
            << movie_cd_state.target_lba << '/'
            << static_cast<unsigned int>(movie_cd_state.mode);
  std::cerr
             << " loop-exit=" << state_loop_epilogues << '/'
            << game_main_returns
             << " app=" << movie_preflight_application_state << '/'
            << movie_preflight_application_depth
            << " selection=" << movie_preflight_title_selection
            << " clock=" << movie_preflight_title_clock << '/'
            << movie_preflight_title_deadline
            << " title-states=";
  for (std::size_t index = 0U; index < movie_preflight_title_states.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << movie_preflight_title_states[index];
  }
  std::cerr << " app-states=";
  for (std::size_t index = 0U;
       index < movie_preflight_application_states.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << movie_preflight_application_states[index];
  }
  std::cerr << " transitions=";
  for (std::size_t index = 0U; index < movie_preflight_transitions.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/")
              << static_cast<unsigned int>(movie_preflight_transitions[index]);
  }
  std::cerr << " stop="
            << sf::psx::toString(movie_preflight_stop.execution.reason)
            << "@0x" << std::hex << std::uppercase
             << movie_preflight_stop.execution.pc << " ra=0x"
            << vm.runtime().state().gpr[31U] << " sp=0x"
            << vm.runtime().state().gpr[29U] << " v0=0x"
             << vm.runtime().state().gpr[2U] << std::dec
             << " dma=" << sf2_cd_dma_callbacks << '/'
             << sf2_spu_dma_callbacks
             << " cd-complete=" << sf2_cd_completion_interrupts << "@0x"
             << std::hex << std::uppercase
             << sf2_cd_completion_callback_at_interrupt << std::dec
             << ':';
  for (std::size_t index = 0U; index < sf2_cd_completion_trace.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << sf2_cd_completion_trace[index][0] << ','
              << sf2_cd_completion_trace[index][1] << ','
              << sf2_cd_completion_trace[index][2] << std::dec;
  }
  std::cerr
             << " pad-pulse="
             << (title_pad_pulse == TitlePadPulse::released ? "released"
                                                            : "missing")
             << " overlay=" << sf2_last_resident_overlay_name << "@0x"
             << std::hex << std::uppercase
             << sf2_last_resident_overlay_address << std::dec << ':'
             << sf2_last_resident_overlay_mode << '/'
             << sf2_resident_overlay_load_bridges << '\n';
  if (title_handoffs != 0U || title_mission_opens != 0U) {
    return 7;
  }
  if (!movie_preflight_passed) {
    return 7;
  }
  std::uint32_t continuous_pre_heap_marker{};
  std::uint32_t continuous_pre_heap_current{};
  if (!vm.runtime().read32(0x8011ef00U, continuous_pre_heap_marker) ||
      !vm.runtime().read32(0x8011ee2cU, continuous_pre_heap_current)) {
    return 7;
  }
  // The mission-selection state normally reaches this cleanup through the
  // state-11 completion callback at 0x80152434. It shuts down TITLE's
  // asynchronous memory-card manager and unregisters interrupt slot 7 before
  // MissionArchive_Open reclaims the MOVIE overlay. The deterministic probe
  // enters at the selected-state boundary, so execute that exact retail
  // teardown explicitly rather than leaving a callback into freed overlay
  // code.
  const auto continuous_title_cleanup =
      invoke_nested_scheduled(0x8014db50U, std::span<const std::uint32_t>{});
  std::uint32_t continuous_title_worker_after_cleanup{};
  if ((!continuous_title_cleanup.stoppedAtHostBoundary() &&
       !continuous_title_cleanup.completed()) ||
      !vm.runtime().read32(guest_profile.interrupt_callback_table + 7U * 4U,
                           continuous_title_worker_after_cleanup) ||
      continuous_title_worker_after_cleanup != 0U) {
    std::cerr << "SF2 TITLE transition cleanup failed: "
              << sf::psx::toString(continuous_title_cleanup.execution.reason)
              << "@0x" << std::hex << std::uppercase
              << continuous_title_cleanup.execution.pc << " slot7=0x"
              << continuous_title_worker_after_cleanup << std::dec << '\n';
    return 7;
  }
  // The normal frontend exit balances the temporary packet-arena reservation
  // through this retail restore call before INIT reclaims the heap. The
  // deterministic selected-state entry skips that outer state-0 pass, so
  // release it here; state 8 will build the mission arena after loading.
  constexpr std::array<std::uint32_t, 2U>
      continuous_frontend_arena_release_arguments{0U, 0U};
  const auto continuous_frontend_arena_release = invoke_nested_scheduled(
      0x80015510U, continuous_frontend_arena_release_arguments);
  if (!continuous_frontend_arena_release.completed() &&
      !continuous_frontend_arena_release.stoppedAtHostBoundary()) {
    std::cerr << "SF2 frontend graphics reservation release failed\n";
    return 7;
  }
  // Drive the retail TITLE state that owns the mission-selection handoff.
  // State 17 passes the overlay selection plus both retail transition flags
  // to MissionArchive_Open; selection 2 is Mission 3.
  if (!vm.runtime().write32(0x801582d4U, mission_selection_index) ||
      !vm.runtime().write32(0x80156bdcU, 17U) ||
      !vm.runtime().write32(0x8011f61cU, 1U)) {
    return 7;
  }
  const auto mission_fog_entry = disc.image().find("FOG/HWAY.FOG");
  cdrom_media.mapRelativeExtent(
      mission_fog_entry.extent_lba,
      (mission_fog_entry.size + sf::assets::FogArchive::sector_size - 1U) /
          sf::assets::FogArchive::sector_size);
  std::vector<std::array<std::uint32_t, 2U>> continuous_clear_calls;
  vm.bindHostCall(
      0x8015cf84U,
      [&continuous_clear_calls](sf::game::LegacyHostCallContext &context) {
        if (continuous_clear_calls.size() < 16U) {
          continuous_clear_calls.push_back(
              {context.argument(0), context.registerValue(31U)});
        }
        context.continueGuestInstruction();
      });
  const auto continuous_mission = invoke_nested_scheduled(
      0x80153d30U, std::span<const std::uint32_t>{});
  std::uint32_t continuous_hog_offset_after_mission{};
  static_cast<void>(
      vm.runtime().read32(0x801b92b4U, continuous_hog_offset_after_mission));
  std::uint32_t continuous_heap_marker{};
  std::uint32_t continuous_heap_current{};
  std::uint32_t continuous_heap_base{};
  static_cast<void>(vm.runtime().read32(0x8011ef00U, continuous_heap_marker));
  static_cast<void>(vm.runtime().read32(0x8011ee2cU, continuous_heap_current));
  static_cast<void>(vm.runtime().read32(0x8011ee28U, continuous_heap_base));
  if (!vm.runtime().read32(guest_profile.application_state, continuous_state) ||
      !vm.runtime().read32(guest_profile.application_state_depth,
                           continuous_depth)) {
    return 7;
  }
  std::cerr << "SF2 continuous mission diagnostic: result="
            << sf::psx::toString(continuous_mission.execution.reason) << "@0x"
            << std::hex << std::uppercase << continuous_mission.execution.pc
            << std::dec << " state=" << continuous_state
            << " depth=" << continuous_depth << " pre-heap=0x" << std::hex
            << std::uppercase << continuous_pre_heap_marker << "/0x"
            << continuous_pre_heap_current << " heap=0x" << std::hex
            << std::uppercase << continuous_heap_marker << "/0x"
            << continuous_heap_current << "/0x" << continuous_heap_base
            << std::dec << " cd-search=" << sf2_search_matches << '/'
            << sf2_search_calls << " last-path=" << sf2_last_search_path
            << " catalog=" << sf2_catalog_copy_observations << '/'
            << sf2_catalog_repair_bridges
            << " instructions=" << continuous_mission.execution.instructions
            << " instruction=0x" << std::hex << std::uppercase
            << continuous_mission.execution.instruction << std::dec
            << " resident-file=" << sf2_resident_file_open_bridges << '/'
            << sf2_resident_file_load_bridges << " slf=" << sf2_slf_open_bridges
            << '/' << sf2_slf_load_bridges << " hog-offset=0x" << std::hex
            << std::uppercase << continuous_hog_offset_after_mission << std::dec
            << '\n';
  if (!continuous_mission.stoppedAtHostBoundary() &&
      !continuous_mission.completed()) {
    return 7;
  }
  suppress_guest_interrupts = true;
  vm.runtime().setExternalInterrupt(false);
  constexpr std::uint32_t cd_sync_boundary = 0x800f5d48U;
  auto continuous_cd_sync_polls = std::size_t{};
  vm.bindHostCall(
      cd_sync_boundary,
      [&continuous_cd_sync_polls](sf::game::LegacyHostCallContext &context) {
        ++continuous_cd_sync_polls;
        context.continueGuestInstruction();
      });
  auto continuous_loading_pad_polls = std::size_t{};
  auto continuous_loading_confirm_sent = false;
  vm.bindHostCall(
      0x80029b28U,
      [&continuous_loading_pad_polls, &continuous_loading_confirm_sent](
          sf::game::LegacyHostCallContext &context) {
        ++continuous_loading_pad_polls;
        std::uint32_t record{};
        if (context.read32(context.registerValue(29U) + 0x14U, record) &&
            record != 0U) {
          const auto confirm =
              !continuous_loading_confirm_sent &&
              continuous_loading_pad_polls >= 8U;
          if (!context.write8(record, 0U) ||
              !context.write16(record + 4U, confirm ? 0x0040U : 0U)) {
            context.rejectHostCall();
            return;
          }
          if (confirm) {
            continuous_loading_confirm_sent = true;
          }
        }
        context.continueGuestInstruction();
      });
  auto continuous_loading_frames = std::size_t{};
  auto continuous_state4_boundaries = std::size_t{};
  auto continuous_stable_boundaries = std::size_t{};
  auto continuous_background_slices = std::size_t{};
  constexpr std::size_t continuous_state4_probe_limit = 1'400U;
  std::vector<std::uint32_t> continuous_states;
  std::optional<sf::game::Sf2PresentationFrame> continuous_presentation_frame;
  std::optional<sf::game::Sf2PresentationFrame>
      continuous_best_presentation_frame;
  std::vector<std::uint32_t> continuous_presentation_roots;
  auto continuous_embedded_hog_lifetime_blocked = false;
  while (continuous_loading_frames < 2'000U) {
    const auto frame_boundary = guest_profile.gpu_submission_entry;
    const auto frame = run_scheduled_until_boundary(frame_boundary);
    if (!vm.runtime().read32(guest_profile.application_state,
                             continuous_state)) {
      return 7;
    }
    if (!frame.stoppedAtHostBoundary() &&
        frame.execution.reason ==
            sf::psx::R3000StopReason::instruction_budget &&
        continuous_background_slices < 64U) {
      if (continuous_states.empty() ||
          continuous_states.back() != continuous_state) {
        continuous_states.push_back(continuous_state);
      }
      ++continuous_background_slices;
      continue;
    }
    if (!frame.stoppedAtHostBoundary()) {
      std::uint32_t failure_pc_word{};
      std::uint32_t failure_object_table{};
      std::uint16_t failure_object_count{};
      std::uint32_t failure_object_entry{};
      std::array<std::uint32_t, 12U> failure_render_object{};
      std::array<std::uint32_t, 4U> failure_render_node{};
      std::array<std::uint32_t, 4U> failure_render_owner{};
      std::array<std::uint32_t, 2U> failure_primitive_starts{};
      std::uint32_t failure_primitive_cursor{};
      std::uint32_t failure_heap_cursor{};
      static_cast<void>(
          vm.runtime().read32(frame.execution.pc, failure_pc_word));
      static_cast<void>(
          vm.runtime().read32(0x8011f4b8U, failure_object_table));
      static_cast<void>(
          vm.runtime().read16(0x8011ed0cU, failure_object_count));
      if (failure_object_table != 0U) {
        static_cast<void>(vm.runtime().read32(
            failure_object_table +
                static_cast<std::uint32_t>(vm.runtime().state().gpr[3U] * 4U),
            failure_object_entry));
      }
      static_cast<void>(
          vm.runtime().read32(0x8011f4a0U, failure_primitive_starts[0U]));
      static_cast<void>(
          vm.runtime().read32(0x8011f4a4U, failure_primitive_starts[1U]));
      static_cast<void>(
          vm.runtime().read32(0x8013e6dcU, failure_primitive_cursor));
      static_cast<void>(
          vm.runtime().read32(0x8011ee2cU, failure_heap_cursor));
      for (std::size_t index = 0U; index < failure_render_object.size();
           ++index) {
        static_cast<void>(vm.runtime().read32(
            vm.runtime().state().gpr[16U] +
                static_cast<std::uint32_t>(index * sizeof(std::uint32_t)),
            failure_render_object[index]));
      }
      for (std::size_t index = 0U; index < failure_render_node.size();
           ++index) {
        static_cast<void>(vm.runtime().read32(
            vm.runtime().state().gpr[30U] +
                static_cast<std::uint32_t>(
                    index * sizeof(std::uint32_t)),
            failure_render_node[index]));
        static_cast<void>(vm.runtime().read32(
            vm.runtime().state().gpr[20U] +
                static_cast<std::uint32_t>(
                    index * sizeof(std::uint32_t)),
            failure_render_owner[index]));
      }
      std::cerr << "SF2 continuous application loop failed: frame="
                << continuous_loading_frames
                << " reason=" << sf::psx::toString(frame.execution.reason)
                << " pc=0x" << std::hex << std::uppercase << frame.execution.pc
                 << " instruction=0x" << frame.execution.instruction << " v0=0x"
                << vm.runtime().state().gpr[2U] << " v1=0x"
                << vm.runtime().state().gpr[3U] << " t1=0x"
                << vm.runtime().state().gpr[9U] << " a0=0x"
                << vm.runtime().state().gpr[4U] << " a1=0x"
                << vm.runtime().state().gpr[5U] << " s0=0x"
                << vm.runtime().state().gpr[16U] << " s1=0x"
                << vm.runtime().state().gpr[17U] << " s2=0x"
                << vm.runtime().state().gpr[18U] << " s3=0x"
                << vm.runtime().state().gpr[19U] << " s4=0x"
                << vm.runtime().state().gpr[20U] << " fp=0x"
                << vm.runtime().state().gpr[30U] << " ra=0x"
                << vm.runtime().state().gpr[31U] << " sp=0x"
                << vm.runtime().state().gpr[29U] << " status=0x"
                << vm.runtime().state().cop0_status << " cause=0x"
                << vm.runtime().state().cop0_cause << " epc=0x"
                << vm.runtime().state().cop0_epc << " istat=0x"
                << vm.machine().interrupts().status() << " imask=0x"
                << vm.machine().interrupts().mask() << " ilines=0x"
                 << vm.machine().interrupts().inputLines() << std::dec
                 << " pc-word=0x" << std::hex << std::uppercase
                 << failure_pc_word << " object-table=0x"
                 << failure_object_table << " object-count=0x"
                 << failure_object_count << " object-entry=0x"
                 << failure_object_entry << std::dec
                 << " scheduler-failure=" << sf2_scheduler_failure;
      if (sf2_callback_failure) {
        std::cerr << ":slot=0x" << std::hex << std::uppercase
                  << sf2_callback_failure_slot << ":callback=0x"
                  << sf2_callback_failure_address << ":"
                  << sf::psx::toString(
                         sf2_callback_failure->execution.reason)
                  << "@0x" << sf2_callback_failure->execution.pc
                  << ":instruction=0x"
                  << sf2_callback_failure->execution.instruction << std::dec;
      }
      std::cerr
                 << " state=" << continuous_state << " states=";
      for (std::size_t index = 0U; index < continuous_states.size(); ++index) {
        std::cerr << (index == 0U ? "" : "/") << continuous_states[index];
      }
      std::cerr << '\n';
      std::cerr << "SF2 render object:";
      for (const auto word : failure_render_object) {
        std::cerr << " 0x" << std::hex << std::uppercase << word << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 render node:";
      for (const auto word : failure_render_node) {
        std::cerr << " 0x" << std::hex << std::uppercase << word << std::dec;
      }
      std::cerr << "\nSF2 render owner:";
      for (const auto word : failure_render_owner) {
        std::cerr << " 0x" << std::hex << std::uppercase << word << std::dec;
      }
      std::cerr << "\nSF2 primitive arena: starts=0x" << std::hex
                << std::uppercase << failure_primitive_starts[0U] << "/0x"
                << failure_primitive_starts[1U] << " cursor=0x"
                << failure_primitive_cursor << " heap=0x"
                << failure_heap_cursor << std::dec << '\n';
      const auto failed_object = vm.runtime().state().gpr[16U];
      const auto matching_insert = std::find_if(
          sf2_render_list_inserts.rbegin(), sf2_render_list_inserts.rend(),
          [failed_object](const Sf2RenderListInsert &insert) {
            return insert.object == failed_object;
          });
      if (matching_insert != sf2_render_list_inserts.rend()) {
        std::cerr << "SF2 render-list insertion: list=0x" << std::hex
                  << std::uppercase << matching_insert->list << " object=0x"
                  << matching_insert->object << " caller=0x"
                  << matching_insert->caller << " upstream=0x"
                  << matching_insert->upstream_caller << " scheduled="
                  << (matching_insert->scheduled_callback ? 1 : 0)
                  << " words=";
        for (const auto word : matching_insert->object_words) {
          std::cerr << " 0x" << word;
        }
        std::cerr << std::dec << '\n';
      } else {
        std::cerr << "SF2 render-list insertion: not observed; total="
                  << sf2_render_list_inserts.size() << '\n';
      }
      std::cerr << "SF2 matching render-list removals:";
      auto matching_removals = std::size_t{};
      for (const auto &remove : sf2_render_list_removes) {
        if (remove.object == failed_object) {
          std::cerr << " list=0x" << std::hex << std::uppercase
                    << remove.list << ":node=0x" << remove.node
                    << "@0x" << remove.caller << std::dec;
          ++matching_removals;
        }
      }
      if (matching_removals == 0U) {
        std::cerr << " none";
      }
      std::cerr << "\nSF2 heap rewinds:";
      for (const auto &rewind : sf2_heap_rewinds) {
        std::cerr << " 0x" << std::hex << std::uppercase << rewind[0U]
                  << "->0x" << rewind[1U] << "@0x" << rewind[2U]
                  << std::dec;
      }
      std::cerr << "\nSF2 nearby heap allocations:";
      for (const auto &allocation : sf2_heap_allocations) {
        const auto size =
            static_cast<std::int32_t>(allocation.size) < 0
                ? 0U - allocation.size
                : allocation.size;
        if (allocation.address <= failed_object + 0x100U &&
            allocation.address + size >= failed_object - 0x1000U) {
          std::cerr << " 0x" << std::hex << std::uppercase
                    << allocation.address << "+0x" << size << "@0x"
                    << allocation.caller << std::dec;
        }
      }
      std::cerr << '\n';
      std::cerr << "SF2 render-arena resets:";
      for (const auto &reset : sf2_render_arena_resets) {
        std::cerr << " mode=0x" << std::hex << std::uppercase << reset[0U]
                  << "@0x" << reset[1U] << ":0x" << reset[2U] << "/0x"
                  << reset[3U] << ":heap=0x" << reset[4U] << ":span=0x"
                  << reset[5U] << "/0x" << reset[6U] << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 overlay requests:";
      for (const auto &[name, address, mode, caller] : sf2_overlay_requests) {
        std::cerr << " " << name << "@0x" << std::hex << std::uppercase
                  << address << ":0x" << mode << "@0x" << caller << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 INIT clear calls:";
      for (const auto &call : continuous_clear_calls) {
        std::cerr << " 0x" << std::hex << std::uppercase << call[0U]
                  << "@0x" << call[1U] << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 archive member requests:";
      for (const auto &request : sf2_archive_member_requests) {
        std::cerr << " " << request.name << "->0x" << std::hex
                  << std::uppercase << request.destination_slot << ":0x"
                  << request.mode << "@0x" << request.caller << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 INIT resource inputs:";
      for (const auto &input : sf2_init_resource_inputs) {
        std::cerr << " mode=0x" << std::hex << std::uppercase << input[0U]
                  << " descriptor=0x" << input[1U] << " fields=0x"
                  << input[2U] << "+0x" << input[3U] << " resource=0x"
                  << input[4U] << " s6=0x" << input[5U] << "@0x"
                  << input[6U] << std::dec;
      }
      std::cerr << '\n';
      std::cerr << "SF2 INIT archive paths:";
      for (const auto &path : sf2_init_archive_paths) {
        std::cerr << " " << path;
      }
      std::cerr << '\n';
      std::cerr << "SF2 INIT descriptor writes:";
      for (const auto &write : sf2_init_descriptor_writes) {
        std::cerr << " value=0x" << std::hex << std::uppercase << write[0U]
                  << " root=0x" << write[1U] << " prior=0x" << write[2U]
                  << " result=0x" << write[3U] << " *result=0x"
                  << write[4U] << " s6=0x" << write[5U] << std::dec;
      }
      std::cerr << '\n';
      if (continuous_state == 1U &&
          frame.execution.reason == sf::psx::R3000StopReason::alignment_fault &&
          frame.execution.pc == 0x80026d80U) {
        continuous_embedded_hog_lifetime_blocked = true;
        break;
      }
      return 7;
    }
    if (continuous_states.empty() ||
        continuous_states.back() != continuous_state) {
      continuous_states.push_back(continuous_state);
    }
    constexpr std::uint32_t retail_render_submission_return = 0x800f181cU;
    if (vm.runtime().state().gpr[31U] ==
        retail_render_submission_return) {
      auto published = sf::game::captureSf2PresentationFrame(
          vm.runtime().ram(), vm.runtime().state().gpr[5U], continuous_state,
          continuous_stable_boundaries + 1U, continuous_loading_frames + 1U);
      if (published) {
        if (!continuous_best_presentation_frame ||
            std::tuple{published->draw_command_count,
                       published->gpu_command_count,
                       published->gp0_word_count} >
                std::tuple{
                    continuous_best_presentation_frame->draw_command_count,
                    continuous_best_presentation_frame->gpu_command_count,
                    continuous_best_presentation_frame->gp0_word_count}) {
          continuous_best_presentation_frame = *published;
        }
        if (continuous_presentation_roots.size() ==
            stable_mission_boundary_count) {
          continuous_presentation_roots.erase(
              continuous_presentation_roots.begin());
        }
        continuous_presentation_roots.push_back(published->ordering_table_root);
        continuous_presentation_frame = std::move(published);
        ++continuous_stable_boundaries;
      }
    }
    const auto retired = vm.resumeCurrentPcClockNeutral(1U);
    if (retired.execution.reason !=
        sf::psx::R3000StopReason::instruction_budget) {
      return 7;
    }
    ++continuous_loading_frames;
    if (continuous_state == 4U) {
      ++continuous_state4_boundaries;
    }
    if (continuous_stable_boundaries >= stable_mission_boundary_count ||
        continuous_state4_boundaries >= continuous_state4_probe_limit) {
      break;
    }
  }
  const auto continuous_ram = vm.runtime().ram();
  const auto continuous_overlay_offset =
      guest_profile.mission_overlay_load_address & 0x1fffffU;
  auto continuous_overlay_bytes = std::size_t{};
  while (continuous_overlay_bytes < expected_overlay.size() &&
         continuous_overlay_offset + continuous_overlay_bytes <
             continuous_ram.size() &&
         continuous_ram[continuous_overlay_offset + continuous_overlay_bytes] ==
             expected_overlay[continuous_overlay_bytes]) {
    ++continuous_overlay_bytes;
  }
  const auto continuous_overlay_prefix =
      expected_overlay.size() >= 16U
          ? std::search(continuous_ram.begin(), continuous_ram.end(),
                        expected_overlay.begin(), expected_overlay.begin() + 16)
          : continuous_ram.end();
  const auto continuous_overlay_address =
      continuous_overlay_prefix == continuous_ram.end()
          ? 0U
          : 0x80000000U + static_cast<std::uint32_t>(continuous_overlay_prefix -
                                                     continuous_ram.begin());
  std::array<std::uint32_t, 8U> continuous_state4_words{};
  std::uint32_t continuous_title_state{};
  std::uint32_t continuous_title_timer{};
  std::uint32_t continuous_title_selection{};
  std::uint32_t continuous_title_phase{};
  static_cast<void>(vm.runtime().read32(0x80156bdcU, continuous_title_state));
  static_cast<void>(vm.runtime().read32(0x8011f668U, continuous_title_timer));
  static_cast<void>(
      vm.runtime().read32(0x801582d4U, continuous_title_selection));
  static_cast<void>(vm.runtime().read32(0x80156be4U, continuous_title_phase));
  for (std::size_t index = 0U; index < continuous_state4_words.size();
       ++index) {
    static_cast<void>(vm.runtime().read32(
        0x80153e24U + static_cast<std::uint32_t>(index * 4U),
        continuous_state4_words[index]));
  }
  std::cerr << "SF2 continuous loading diagnostic: frames="
            << continuous_loading_frames << " state=" << continuous_state
            << " overlay=" << continuous_overlay_bytes << '/'
            << expected_overlay.size() << "@0x" << std::hex << std::uppercase
            << continuous_overlay_address << std::dec << " states=";
  for (std::size_t index = 0U; index < continuous_states.size(); ++index) {
    std::cerr << (index == 0U ? "" : "/") << continuous_states[index];
  }
  std::cerr << " background-slices=" << continuous_background_slices
            << " cd-sync-polls=" << continuous_cd_sync_polls
            << " state4-boundaries=" << continuous_state4_boundaries
            << " stable-boundaries=" << continuous_stable_boundaries
            << " title-state=" << continuous_title_state
            << " title-timer=" << continuous_title_timer
            << " title-selection=" << continuous_title_selection
            << " title-phase=" << continuous_title_phase
            << " loading-confirm=" << continuous_loading_pad_polls << '/'
            << (continuous_loading_confirm_sent ? 1 : 0)
            << " pad-polls=" << sf2_pad_poll_bridges
            << " resident-file=" << sf2_resident_file_open_bridges << '/'
            << sf2_resident_file_load_bridges << " state4-code=";
  for (std::size_t index = 0U; index < continuous_state4_words.size();
       ++index) {
    std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
              << continuous_state4_words[index];
  }
  std::cerr << std::dec << " presentation=";
  if (continuous_presentation_frame) {
    std::cerr << "valid@0x" << std::hex << std::uppercase
              << continuous_presentation_frame->ordering_table_root << std::dec
              << ",packets:" << continuous_presentation_frame->packets.size()
              << ",words:" << continuous_presentation_frame->gp0_word_count
              << ",commands:"
              << continuous_presentation_frame->gpu_command_count
              << ",draws:" << continuous_presentation_frame->draw_command_count
              << ",roots:";
    for (std::size_t index = 0U; index < continuous_presentation_roots.size();
         ++index) {
      std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
                << continuous_presentation_roots[index];
    }
  } else {
    std::cerr << "missing";
  }
  std::cerr << std::dec << ",best:";
  if (continuous_best_presentation_frame) {
    std::cerr << "packets:"
              << continuous_best_presentation_frame->packets.size()
              << ",words:" << continuous_best_presentation_frame->gp0_word_count
              << ",commands:"
              << continuous_best_presentation_frame->gpu_command_count
              << ",draws:"
              << continuous_best_presentation_frame->draw_command_count
              << ",opcodes:";
    const auto opcode_count = std::min<std::size_t>(
        32U, continuous_best_presentation_frame->packets.size());
    for (std::size_t index = 0U; index < opcode_count; ++index) {
      const auto &packet = continuous_best_presentation_frame->packets[index];
      std::cerr << (index == 0U ? "" : "/") << std::hex << std::uppercase
                << (packet.gp0_words.front() >> 24U);
    }
    if (opcode_count < continuous_best_presentation_frame->packets.size()) {
      std::cerr << "/...";
    }
  } else {
    std::cerr << "missing";
  }
  std::cerr << std::dec << '\n';
  std::cerr << "SF2 application-state calls:";
  for (const auto &call : sf2_application_state_calls) {
    std::cerr << " " << call[0U] << ":" << call[1U] << "@0x" << std::hex
              << std::uppercase << call[2U] << std::dec;
  }
  std::cerr << '\n';
  std::cerr << "SF2 overlay requests:";
  for (const auto &[name, address, mode, caller] : sf2_overlay_requests) {
    std::cerr << " " << name << "@0x" << std::hex << std::uppercase
              << address << ":0x" << mode << "@0x" << caller << std::dec;
  }
  std::cerr << '\n';
  constexpr std::array expected_continuous_state_prefix{0U, 1U, 8U, 0U};
  constexpr std::array expected_lifetime_blocked_state_prefix{9U, 1U};
  const auto lifetime_blocker_valid =
      continuous_embedded_hog_lifetime_blocked &&
      continuous_states.size() >=
          expected_lifetime_blocked_state_prefix.size() &&
      std::ranges::equal(
          expected_lifetime_blocked_state_prefix,
          std::span{continuous_states}.first(
              expected_lifetime_blocked_state_prefix.size()));
  const auto continuous_mission_checkpoint_valid =
      !continuous_embedded_hog_lifetime_blocked &&
      continuous_stable_boundaries >= stable_mission_boundary_count &&
      continuous_states.size() >= expected_continuous_state_prefix.size() &&
      std::ranges::equal(expected_continuous_state_prefix,
                         std::span{continuous_states}.first(
                             expected_continuous_state_prefix.size())) &&
      continuous_overlay_bytes >= 0x1000U &&
      sf2_resident_overlay_load_bridges != 0U &&
      sf2_resident_file_open_bridges != 0U &&
      sf2_resident_file_load_bridges >= 1U &&
      continuous_presentation_frame && continuous_presentation_frame->valid() &&
      continuous_best_presentation_frame &&
      continuous_best_presentation_frame->valid();
  if (!lifetime_blocker_valid && !continuous_mission_checkpoint_valid) {
    return 7;
  }
  if (continuous_mission_checkpoint_valid) {
    std::cout << "SF2 Mission 3 retail transition passed: states=";
    for (std::size_t index = 0U; index < continuous_states.size(); ++index) {
      std::cout << (index == 0U ? "" : "/") << continuous_states[index];
    }
    std::cout << " frames=" << continuous_loading_frames
              << " stable-boundaries=" << continuous_stable_boundaries
              << " presentation=valid,packets:"
              << continuous_best_presentation_frame->packets.size()
              << ",words:"
              << continuous_best_presentation_frame->gp0_word_count
              << ",commands:"
              << continuous_best_presentation_frame->gpu_command_count
              << ",draws:"
              << continuous_best_presentation_frame->draw_command_count
              << '\n';
    return 0;
  }

  // Preserve the already-published deterministic HWAY checkpoint while the
  // title-owned input/selection continuation remains a separate gate. Replay
  // the prior mission snapshot for the exact HWAY assertions below; the
  // continuous experiment may stop at the documented embedded-HOG lifetime
  // boundary after states 9 -> 1.
  if (!vm.restoreSnapshot(resident_loop_snapshot)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Could not restore SF2 presentation checkpoint"};
  }
  if (!vm.runtime().loadBytes(0x80158878U, init_overlay)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Could not restore retail SF2 INIT overlay"};
  }
  suppress_guest_interrupts = false;
  vm.bindPsxBiosCoreVector();
  enable_sf2_mission_search = false;
  enable_sf2_resident_overlay_load = false;
  enable_sf2_resident_file_access = false;
  sf2_resident_open_files.clear();
  sf2_search_calls = 0U;
  sf2_search_matches = 0U;
  sf2_last_search_path.clear();
  sf2_catalog_after_copy.fill(std::byte{});
  sf2_catalog_copy_observations = 0U;
  sf2_catalog_repair_bridges = 0U;
  sf2_slf_open_bridges = 0U;
  sf2_slf_load_bridges = 0U;
  sf2_resident_file_open_bridges = 0U;
  sf2_resident_file_load_bridges = 0U;
  sf2_pad_poll_bridges = 0U;
  sf2_resident_callback_table_bridges = 0U;
  sf2_resident_overlay_load_bridges = 0U;
  vm.bindHostCall(0x8015ba54U, [&sf2_resident_callback_table_bridges](
                                   sf::game::LegacyHostCallContext &context) {
    ++sf2_resident_callback_table_bridges;
    context.setReturnValue(0U);
  });
  constexpr std::uint32_t probe_call_stack = 0x8000f000U;
  vm.runtime().setRegister(29U, probe_call_stack);
  vm.runtime().setRegister(30U, probe_call_stack);
  constexpr std::array seeded_init_arguments{4U};
  const auto seeded_init =
      invoke_scheduled(guest_profile.common_init_entry, seeded_init_arguments);
  if (!seeded_init.completed()) {
    std::cerr << "Seeded SF2 Common_Init(4) failed: reason="
              << sf::psx::toString(seeded_init.execution.reason) << " pc=0x"
              << std::hex << std::uppercase << seeded_init.execution.pc
              << " instruction=0x" << seeded_init.execution.instruction
              << std::dec << '\n';
    return 7;
  }
  enable_sf2_mission_search = true;
  enable_sf2_resident_overlay_load = true;
  // MissionArchive_Open consumes the allocator marker normally established
  // by the resident INIT lifecycle. The probe publishes that heap boundary
  // explicitly while the GLOBAL.DAT resource path remains disconnected.
  constexpr std::uint32_t resident_init_state_entry = 0x8015d0ccU;
  constexpr std::array resident_init_state_arguments{1U};
  const auto resident_init_state = invoke_scheduled(
      resident_init_state_entry, resident_init_state_arguments);
  std::uint32_t resident_application_state{};
  std::uint32_t resident_application_depth{};
  if (!resident_init_state.completed() ||
      !vm.runtime().read32(guest_profile.application_state,
                           resident_application_state) ||
      !vm.runtime().read32(guest_profile.application_state_depth,
                           resident_application_depth)) {
    std::cerr << "Seeded SF2 resident INIT state failed: reason="
              << sf::psx::toString(resident_init_state.execution.reason)
              << " pc=0x" << std::hex << std::uppercase
              << resident_init_state.execution.pc << " instruction=0x"
              << resident_init_state.execution.instruction << " ra=0x"
              << vm.runtime().state().gpr[31U] << " sp=0x"
              << vm.runtime().state().gpr[29U] << " a0=0x"
              << vm.runtime().state().gpr[4U] << std::dec
              << " instructions=" << resident_init_state.execution.instructions
              << '\n';
    return 7;
  }
  if (resident_application_state == 1U && resident_application_depth != 0U) {
    const auto resident_state_pop =
        invoke_scheduled(guest_profile.application_state_pop_entry, {});
    if (!resident_state_pop.completed() ||
        !vm.runtime().read32(guest_profile.application_state,
                             resident_application_state) ||
        resident_application_state != 0U) {
      std::cerr << "SF2 resident INIT state-1 pop failed\n";
      return 7;
    }
  }
  std::uint32_t resident_heap_marker{};
  std::uint32_t resident_heap_current{};
  auto resident_heap_state_read =
      vm.runtime().read32(0x8011ef00U, resident_heap_marker) &&
      vm.runtime().read32(0x8011ee2cU, resident_heap_current);
  auto resident_heap_marker_bridged = false;
  if (resident_heap_state_read && resident_heap_marker == 0U &&
      resident_heap_current != 0U) {
    resident_heap_state_read =
        vm.runtime().write32(0x8011ef00U, resident_heap_current);
    resident_heap_marker = resident_heap_current;
    resident_heap_marker_bridged = resident_heap_state_read;
  }
  constexpr std::array mission_open_arguments{mission_selection_index, 1U, 1U};
  const auto mission_open = invoke_scheduled(
      guest_profile.mission_archive_open_entry, mission_open_arguments);
  std::uint32_t mission_open_state{};
  std::uint32_t mission_open_depth{};
  std::uint32_t mission_open_transition{};
  std::uint32_t mission_pending_state{};
  if (!mission_open.completed() ||
      !vm.runtime().read32(guest_profile.application_state,
                           mission_open_state) ||
      !vm.runtime().read32(guest_profile.application_state_depth,
                           mission_open_depth) ||
      !vm.runtime().read32(guest_profile.application_transition,
                           mission_open_transition) ||
      !vm.runtime().read32(guest_profile.application_state_stack +
                               mission_open_depth * sizeof(std::uint32_t),
                           mission_pending_state)) {
    std::uint32_t failed_heap_marker{};
    std::uint32_t failed_heap_current{};
    std::uint32_t failed_heap_base{};
    const auto failed_heap_state_read =
        vm.runtime().read32(0x8011ef00U, failed_heap_marker) &&
        vm.runtime().read32(0x8011ee2cU, failed_heap_current) &&
        vm.runtime().read32(0x8011ee28U, failed_heap_base);
    std::cerr << "SF2 MissionArchive_Open failed: reason="
              << sf::psx::toString(mission_open.execution.reason) << " pc=0x"
              << std::hex << std::uppercase << mission_open.execution.pc
              << " instruction=0x" << mission_open.execution.instruction
              << " t1=0x" << vm.runtime().state().gpr[9U] << std::dec
              << " instructions=" << mission_open.execution.instructions
              << " cd-search=" << sf2_search_matches << '/' << sf2_search_calls
              << " last-path=" << sf2_last_search_path
              << " resident-callback-table="
              << sf2_resident_callback_table_bridges
              << " resident-overlay-load=" << sf2_resident_overlay_load_bridges
              << " catalog-copy=" << sf2_catalog_copy_observations << '/'
              << std::ranges::mismatch(
                     sf2_catalog_after_copy,
                     std::span<const std::byte>{fog_bytes}.first(std::min(
                         fog_bytes.size(), sf2_catalog_after_copy.size())))
                         .in1 -
                     sf2_catalog_after_copy.begin()
              << " catalog-repair=" << sf2_catalog_repair_bridges
              << " slf-open=" << sf2_slf_open_bridges
              << " slf-load=" << sf2_slf_load_bridges
              << " pad-polls=" << sf2_pad_poll_bridges << " heap-marker=0x"
              << std::hex << std::uppercase << failed_heap_marker
              << " heap-current=0x" << failed_heap_current << " heap-base=0x"
              << failed_heap_base << " s0=0x" << vm.runtime().state().gpr[16U]
              << " a0=0x" << vm.runtime().state().gpr[4U] << " v1=0x"
              << vm.runtime().state().gpr[3U] << " gp=0x"
              << vm.runtime().state().gpr[28U]
              << (failed_heap_state_read ? "" : "(read-failed)")
              << " resident-heap-marker=0x" << resident_heap_marker
              << " resident-heap-current=0x" << resident_heap_current
              << " resident-heap-store="
              << (resident_heap_marker_bridged ? "probe-bridged" : "retail")
              << (resident_heap_state_read ? "" : "(read-failed)") << std::dec
              << '\n';
    return 7;
  }
  constexpr std::uint32_t fog_catalog_pointer_address = 0x8011ee68U;
  constexpr std::uint32_t heap_end_address = 0x8011ee2cU;
  constexpr std::uint32_t fog_catalog_address = 0x80126058U;
  constexpr std::size_t fog_catalog_size = 0x240U;
  std::uint32_t fog_catalog_pointer{};
  std::uint32_t mission_heap_end{};
  std::array<std::byte, fog_catalog_size> guest_fog_catalog{};
  const auto fog_catalog_read =
      vm.runtime().read32(fog_catalog_pointer_address, fog_catalog_pointer) &&
      vm.runtime().read32(heap_end_address, mission_heap_end) &&
      vm.runtime().copyBytes(fog_catalog_address, guest_fog_catalog);
  auto matching_fog_catalog_bytes = std::size_t{};
  while (matching_fog_catalog_bytes < guest_fog_catalog.size() &&
         matching_fog_catalog_bytes < fog_bytes.size() &&
         guest_fog_catalog[matching_fog_catalog_bytes] ==
             fog_bytes[matching_fog_catalog_bytes]) {
    ++matching_fog_catalog_bytes;
  }
  const auto locate_fog_prefix = [&]() {
    constexpr std::size_t prefix_size = 16U;
    if (fog_bytes.size() < prefix_size) {
      return std::optional<std::uint32_t>{};
    }
    const auto ram = vm.runtime().ram();
    const auto prefix =
        std::span<const std::byte>{fog_bytes}.first(prefix_size);
    const auto match =
        std::search(ram.begin(), ram.end(), prefix.begin(), prefix.end());
    return match == ram.end()
               ? std::optional<std::uint32_t>{}
               : std::optional<std::uint32_t>{
                     0x80000000U +
                     static_cast<std::uint32_t>(match - ram.begin())};
  };
  const auto fog_prefix_address = locate_fog_prefix();
  auto mission_transition_bridged = false;
  std::uint32_t loading_state_callback{};
  std::uint8_t loading_state_ready{};
  std::uint8_t loading_state_complete{};
  std::uint16_t loading_state_timer{};
  const auto loading_state_read =
      vm.runtime().read32(0x8011f534U, loading_state_callback) &&
      vm.runtime().read8(0x8011f544U, loading_state_ready) &&
      vm.runtime().read8(0x8011f54cU, loading_state_complete) &&
      vm.runtime().read16(0x8011f548U, loading_state_timer);
  std::array<std::uint32_t, 8U> application_stack{};
  const auto read_application_stack = [&]() {
    auto valid = true;
    for (std::size_t index = 0U; index < application_stack.size(); ++index) {
      valid = valid &&
              vm.runtime().read32(
                  guest_profile.application_state_stack +
                      static_cast<std::uint32_t>(index * sizeof(std::uint32_t)),
                  application_stack[index]);
    }
    return valid;
  };
  auto application_stack_valid = read_application_stack();
  auto mission_state_queued =
      application_stack_valid &&
      std::ranges::find(application_stack, 12U) != application_stack.end();
  if (!mission_state_queued) {
    // MissionArchive_Open's retail heap clear currently reaches the synthetic
    // invoke stack and erases its saved a2 flag before the final state push.
    // Re-issue that exact final retail call as a narrow probe-only bridge.
    constexpr std::array mission_state_arguments{12U};
    const auto state_push = invoke_scheduled(
        guest_profile.application_state_push_entry, mission_state_arguments);
    if (!state_push.completed() ||
        !vm.runtime().read32(guest_profile.application_state_depth,
                             mission_open_depth) ||
        !vm.runtime().read32(guest_profile.application_state_stack +
                                 mission_open_depth * sizeof(std::uint32_t),
                             mission_pending_state)) {
      std::cerr << "SF2 mission state-12 push failed\n";
      return 7;
    }
    mission_transition_bridged = true;
    application_stack_valid = read_application_stack();
    mission_state_queued =
        application_stack_valid &&
        std::ranges::find(application_stack, 12U) != application_stack.end();
  }
  const auto mission_overlay_placed =
      !expected_overlay.empty() &&
      vm.runtime().loadBytes(guest_profile.mission_overlay_load_address,
                             expected_overlay);
  if (!mission_overlay_placed) {
    std::cerr << "Could not place untouched SF2 HWAY overlay\n";
    return 7;
  }
  std::uint32_t fog_header_heap_top{};
  const auto fog_header_heap_address =
      vm.runtime().read32(0x8011ee2cU, fog_header_heap_top) &&
              fog_header_heap_top >= fog_header.size()
          ? (fog_header_heap_top -
             static_cast<std::uint32_t>(fog_header.size())) &
                ~std::uint32_t{3U}
          : 0U;
  if (fog_header_heap_address == 0U ||
      !vm.runtime().loadBytes(fog_header_heap_address, fog_header) ||
      !vm.runtime().write32(0x8011ee2cU, fog_header_heap_address) ||
      !vm.runtime().write32(0x8011ee68U, fog_header_heap_address)) {
    std::cerr << "Could not preserve exact SF2 HWAY FOG header\n";
    return 7;
  }
  auto loading_state_frames = std::size_t{};
  std::uint32_t post_loading_state = mission_open_state;
  constexpr std::array<std::uint32_t, 0U> no_loading_state_arguments{};
  while (post_loading_state == 9U && loading_state_frames < 80U) {
    const auto loading_frame =
        invoke_scheduled(0x8002b8d0U, no_loading_state_arguments);
    if (!loading_frame.completed() ||
        !vm.runtime().read32(guest_profile.application_state,
                             post_loading_state)) {
      std::cerr << "SF2 loading state frame failed: frame="
                << loading_state_frames << " reason="
                << sf::psx::toString(loading_frame.execution.reason) << " pc=0x"
                << std::hex << std::uppercase << loading_frame.execution.pc
                << " instruction=0x" << loading_frame.execution.instruction;
      std::uint32_t failed_loading_state{};
      const auto failed_loading_state_read = vm.runtime().read32(
          guest_profile.application_state, failed_loading_state);
      std::cerr << std::dec << " state=" << failed_loading_state
                << (failed_loading_state_read ? "" : "(read-failed)") << '\n';
      return 7;
    }
    ++loading_state_frames;
  }
  auto state12_popped = false;
  if (post_loading_state == 12U) {
    const auto state12_ready =
        invoke_scheduled(0x8002bdccU, no_loading_state_arguments);
    if (!state12_ready.completed()) {
      std::cerr << "SF2 state-12 readiness check failed\n";
      return 7;
    }
    if (state12_ready.return_value == 0U) {
      const auto state_pop =
          invoke_scheduled(0x8002bc80U, no_loading_state_arguments);
      if (!state_pop.completed() ||
          !vm.runtime().read32(guest_profile.application_state,
                               post_loading_state)) {
        std::cerr << "SF2 state-12 retail pop failed: reason="
                  << sf::psx::toString(state_pop.execution.reason) << " pc=0x"
                  << std::hex << std::uppercase << state_pop.execution.pc
                  << " instruction=0x" << state_pop.execution.instruction
                  << std::dec << '\n';
        return 7;
      }
      state12_popped = true;
    }
  }
  std::cout << "SF2 MissionArchive_Open returned: state=" << mission_open_state
            << " depth=" << mission_open_depth
            << " pending-state=" << mission_pending_state
            << " transition=" << mission_open_transition << " state-push="
            << (mission_transition_bridged ? "probe-bridged" : "retail")
            << " seeded-init-instructions="
            << seeded_init.execution.instructions
            << " resident-init-state-instructions="
            << resident_init_state.execution.instructions
            << " resident-callback-table="
            << sf2_resident_callback_table_bridges
            << " resident-overlay-load=" << sf2_resident_overlay_load_bridges
            << " resident-heap-store="
            << (resident_heap_marker_bridged ? "probe-bridged" : "retail")
            << " catalog-copy=" << sf2_catalog_copy_observations
            << " catalog-repair=" << sf2_catalog_repair_bridges
            << " slf-open=" << sf2_slf_open_bridges
            << " slf-load=" << sf2_slf_load_bridges
            << " pad-polls=" << sf2_pad_poll_bridges
            << " instructions=" << mission_open.execution.instructions
            << " gp=0x" << std::hex << std::uppercase
            << vm.runtime().state().gpr[28U] << " loading=0x"
            << loading_state_callback << std::dec << '/'
            << static_cast<unsigned int>(loading_state_ready) << '/'
            << static_cast<unsigned int>(loading_state_complete) << '/'
            << loading_state_timer
            << (loading_state_read ? "" : "(read-failed)")
            << " loading-frames=" << loading_state_frames
            << " post-loading-state=" << post_loading_state
            << " state12-pop=" << (state12_popped ? "retail" : "pending")
            << " mission-overlay=exact-placed"
            << " fog-header=exact@0x" << std::hex << std::uppercase
            << fog_header_heap_address << std::dec << " stack=";
  for (std::size_t index = 0U; index < application_stack.size(); ++index) {
    std::cout << (index == 0U ? "" : "/") << application_stack[index];
  }
  std::cout << " cd-search=" << sf2_search_matches << '/' << sf2_search_calls
            << " last-path=" << sf2_last_search_path
            << " fog-catalog-pointer=0x" << std::hex << std::uppercase
            << fog_catalog_pointer << " heap-end=0x" << mission_heap_end
            << std::dec << " fog-catalog=" << matching_fog_catalog_bytes << '/'
            << guest_fog_catalog.size()
            << (fog_catalog_read ? "" : "(read-failed)") << " fog-prefix=";
  if (fog_prefix_address) {
    std::cout << "0x" << std::hex << std::uppercase << *fog_prefix_address
              << std::dec;
  } else {
    std::cout << "missing";
  }
  const auto cd_state_after_open = vm.machine().cdrom().captureState();
  std::cout << " cd-lba=" << cd_state_after_open.current_lba
            << " catalog-copy=" << sf2_catalog_copy_observations << '/'
            << std::ranges::mismatch(
                   sf2_catalog_after_copy,
                   std::span<const std::byte>{fog_bytes}.first(std::min(
                       fog_bytes.size(), sf2_catalog_after_copy.size())))
                       .in1 -
                   sf2_catalog_after_copy.begin()
            << " catalog-prefix=";
  for (std::size_t index = 0U;
       index < std::min<std::size_t>(32U, guest_fog_catalog.size()); ++index) {
    if (index != 0U) {
      std::cout << ':';
    }
    std::cout << std::hex << std::uppercase
              << static_cast<unsigned int>(
                     std::to_integer<std::uint8_t>(guest_fog_catalog[index]));
  }
  std::cout << std::dec << '\n';
  // The interpreter has no retail exception-vector owner yet. Keep the
  // deterministic probe on its explicit VSync/CD scheduler instead of
  // repeatedly entering the uninitialized low-memory IRQ vector.
  suppress_guest_interrupts = true;
  vm.runtime().setExternalInterrupt(false);

  const auto locate_in_guest_ram = [&](std::span<const std::byte> bytes) {
    const auto ram = vm.runtime().ram();
    const auto match =
        std::search(ram.begin(), ram.end(), bytes.begin(), bytes.end());
    return match == ram.end()
               ? std::optional<std::uint32_t>{}
               : std::optional<std::uint32_t>{
                     0x80000000U +
                     static_cast<std::uint32_t>(match - ram.begin())};
  };
  auto fog_header_address = locate_in_guest_ram(fog_header);
  auto overlay_loaded = false;
  auto stable_boundaries = std::size_t{};
  auto mission_boundaries = std::size_t{};
  auto state_loop_restarts = std::size_t{};
  std::vector<std::uint32_t> mission_states;
  std::optional<sf::game::Sf2PresentationFrame> presentation_frame;
  std::vector<std::uint32_t> presentation_roots;
  // Continue after the state-loop prologue/Common_Init call. The probe has
  // already executed the retail initializer and mission transition directly.
  // Recreate only the fixed frame/register setup consumed by 0x800297DC.
  constexpr std::uint32_t state_loop_stack = probe_call_stack - 0x70U;
  vm.runtime().setRegister(29U, state_loop_stack);
  vm.runtime().setRegister(18U, 1U);
  vm.runtime().setRegister(19U, 0x8012a574U);
  vm.runtime().setRegister(20U, 0xfffffffcU);
  vm.runtime().setRegister(21U, 0x80114710U);
  if (!vm.runtime().write32(state_loop_stack + 0x68U,
                            sf::psx::R3000Runtime::return_sentinel) ||
      !vm.runtime().beginCall(guest_profile.state_loop_dispatch_entry)) {
    throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                          "Could not enter SF2 mission application loop"};
  }
  while (mission_boundaries < mission_boundary_limit) {
    const auto boundary =
        run_scheduled_until_boundary(guest_profile.gpu_submission_entry);
    if (!boundary.stoppedAtHostBoundary()) {
      if (boundary.execution.reason == sf::psx::R3000StopReason::returned &&
          state_loop_restarts < 64U &&
          vm.runtime().beginCall(guest_profile.state_loop_entry)) {
        ++state_loop_restarts;
        continue;
      }
      std::uint32_t failed_state{};
      if (!vm.runtime().read32(guest_profile.application_state, failed_state)) {
        failed_state = std::numeric_limits<std::uint32_t>::max();
      }
      const auto ram = vm.runtime().ram();
      const auto overlay_offset =
          guest_profile.mission_overlay_load_address & 0x1fffffU;
      auto matching_overlay_bytes = std::size_t{};
      while (matching_overlay_bytes < expected_overlay.size() &&
             overlay_offset + matching_overlay_bytes < ram.size() &&
             ram[overlay_offset + matching_overlay_bytes] ==
                 expected_overlay[matching_overlay_bytes]) {
        ++matching_overlay_bytes;
      }
      std::cerr << "SF2 mission transition stopped before GPU boundary "
                << mission_boundaries
                << ": reason=" << sf::psx::toString(boundary.execution.reason)
                << " pc=0x" << std::hex << std::uppercase
                << boundary.execution.pc << " instruction=0x"
                << boundary.execution.instruction << std::dec
                << " state=" << failed_state << " t1=0x" << std::hex
                << std::uppercase << vm.runtime().state().gpr[9U]
                << " status=0x" << vm.runtime().state().cop0_status
                << " cause=0x" << vm.runtime().state().cop0_cause << " epc=0x"
                << vm.runtime().state().cop0_epc << " s0=0x"
                << vm.runtime().state().gpr[16U] << " a0=0x"
                << vm.runtime().state().gpr[4U] << " ra=0x"
                << vm.runtime().state().gpr[31U] << " sp=0x"
                << vm.runtime().state().gpr[29U] << std::dec << " fog-header="
                << (fog_header_address ? "present" : "missing")
                << " matching-overlay-bytes=" << matching_overlay_bytes << '/'
                << expected_overlay.size() << " states=";
      for (std::size_t index = 0U; index < mission_states.size(); ++index) {
        std::cerr << (index == 0U ? "" : "/") << mission_states[index];
      }
      std::cerr << '\n';
      return 8;
    }
    std::uint32_t state{};
    if (!vm.runtime().read32(guest_profile.application_state, state)) {
      return 8;
    }
    if (mission_states.empty() || mission_states.back() != state) {
      mission_states.push_back(state);
    }
    if (!fog_header_address) {
      fog_header_address = locate_in_guest_ram(fog_header);
    }
    std::vector<std::byte> guest_overlay(expected_overlay.size());
    overlay_loaded =
        vm.runtime().copyBytes(guest_profile.mission_overlay_load_address,
                               guest_overlay) &&
        std::ranges::equal(guest_overlay, expected_overlay);
    constexpr std::uint32_t retail_render_submission_return = 0x800f181cU;
    if (overlay_loaded &&
        vm.runtime().state().gpr[31U] == retail_render_submission_return) {
      auto published = sf::game::captureSf2PresentationFrame(
          vm.runtime().ram(), vm.runtime().state().gpr[5U], state,
          stable_boundaries + 1U, mission_boundaries + 1U);
      if (published) {
        presentation_roots.push_back(published->ordering_table_root);
        presentation_frame = std::move(published);
        ++stable_boundaries;
      }
    }

    const auto retired = vm.resumeCurrentPcClockNeutral(1U);
    if (retired.execution.reason !=
        sf::psx::R3000StopReason::instruction_budget) {
      return 8;
    }
    ++mission_boundaries;
    if (stable_boundaries == stable_mission_boundary_count) {
      break;
    }
  }

  std::cout << "SF2 Mission 3 transition: mission-open-state="
            << mission_open_state
            << " mission-open-depth=" << mission_open_depth
            << " mission-open-transition=" << mission_open_transition
            << " resident-init=retail-seeded"
            << " state-push="
            << (mission_transition_bridged ? "probe-bridged" : "retail")
            << " states=";
  for (std::size_t index = 0U; index < mission_states.size(); ++index) {
    std::cout << (index == 0U ? "" : "/") << mission_states[index];
  }
  std::cout << " boundaries=" << mission_boundaries
            << " state-loop-restarts=" << state_loop_restarts
            << " fog-header=" << (fog_header_address ? "0x" : "missing");
  if (fog_header_address) {
    std::cout << std::hex << std::uppercase << *fog_header_address << std::dec;
  }
  std::cout << " overlay="
            << (overlay_loaded ? "exact" : "missing-or-different")
            << " stable-boundaries=" << stable_boundaries
            << " bridges=search:" << sf2_search_matches
            << ",catalog:" << sf2_catalog_repair_bridges
            << ",resident-overlay:" << sf2_resident_overlay_load_bridges
            << ",slf-open:" << sf2_slf_open_bridges
            << ",slf-load:" << sf2_slf_load_bridges
            << ",pad-polls:" << sf2_pad_poll_bridges << " presentation=";
  if (presentation_frame) {
    std::cout << "valid@" << std::hex << std::uppercase
              << presentation_frame->ordering_table_root << std::dec
              << ",packets:" << presentation_frame->packets.size()
              << ",words:" << presentation_frame->gp0_word_count
              << ",commands:" << presentation_frame->gpu_command_count
              << ",draws:" << presentation_frame->draw_command_count
              << ",state:" << presentation_frame->application_state
              << ",roots:";
    for (std::size_t index = 0U; index < presentation_roots.size(); ++index) {
      std::cout << (index == 0U ? "" : "/") << std::hex << std::uppercase
                << presentation_roots[index] << std::dec;
    }
  } else {
    std::cout << "missing";
  }
  std::cout << '\n';
  if (!mission_state_queued || !fog_header_address || !overlay_loaded ||
      stable_boundaries != stable_mission_boundary_count ||
      !presentation_frame || !presentation_frame->valid()) {
    return 9;
  }
  return 0;
}

int probeSf2ProductRuntime(const char *cue_path, std::uint32_t frames,
                           bool forward, bool combat, bool crouch,
                           bool crouch_back,
                           bool quick_state, bool objective_event,
                           bool weapon_cycle, bool skip_objective_scene,
                           bool scan_ui_objects, bool pause_flow,
                           bool mission_complete_probe,
                           bool retail_completion_flow,
                           bool compact_movie_trace,
                           std::uint32_t mission_index,
                           std::uint32_t scripted_movie_ordinal) {
  sf::game::Sf2GuestMissionRuntime runtime{
      std::filesystem::path{cue_path}, mission_index};
  if (!runtime.ready()) {
    std::cerr << "SF2 product runtime failed: " << runtime.faultDetail()
              << '\n';
    return 7;
  }
  sf::game::LegacyHostPadState pad;
  pad.buttons = forward ? 0x0010U
                        : (crouch || crouch_back) ? 0x4000U : 0U;
  pad.face_axis_buttons = 0U;
  pad.use_explicit_face_axis_buttons = crouch || crouch_back;
  pad.left_y = forward ? 0x00U : crouch_back ? 0xffU : 0x80U;
  runtime.setHostPadState(pad);
  std::vector<std::array<std::uint32_t, 4U>> display_environments;
  auto observed_sequence = std::uint64_t{};
  std::array<sf::psx::SpuPcmFrame, 4096U> pcm{};
  auto pcm_frames = std::uint64_t{};
  auto nonzero_pcm_frames = std::uint64_t{};
  auto peak_pcm_sample = std::uint16_t{};
  const auto inspect_pcm = [&](std::size_t count) {
    for (const auto &sample : std::span{pcm}.first(count)) {
      const auto left = static_cast<std::int32_t>(sample.left);
      const auto right = static_cast<std::int32_t>(sample.right);
      if (left != 0 || right != 0) {
        ++nonzero_pcm_frames;
      }
      const auto magnitude = [](std::int32_t value) {
        return static_cast<std::uint16_t>(
            std::min<std::int32_t>(std::abs(value), 32767));
      };
      peak_pcm_sample =
          std::max({peak_pcm_sample, magnitude(left), magnitude(right)});
    }
  };
  auto first_restore_frame = std::uint32_t{};
  auto mission_completion_observed = false;
  auto mission_shell_resumed = false;
  auto campaign_carry_applied = false;
  auto next_mission_carry_verified = false;
  auto completion_carry = std::optional<sf::game::CampaignCarryState>{};
  auto completion_baseline = sf::game::Sf2GuestRuntimeDiagnostics{};
  auto first_sprite_frame = std::uint32_t{};
  auto first_target_frame = std::uint32_t{};
  auto target_active_frames = std::uint32_t{};
  auto maximum_player_danger = std::uint8_t{};
  auto maximum_player_threat_count = std::uint16_t{};
  auto maximum_radar_actor_count = std::uint8_t{};
  auto valid_radar_heading_seen = false;
  auto last_objective_bits =
      runtime.diagnostics().objective_completion_bits;
  auto last_objective_event_count =
      runtime.diagnostics().objective_completion_events;
  std::vector<std::pair<std::uint32_t, std::uint32_t>>
      objective_bit_transitions;
  objective_bit_transitions.emplace_back(0U, last_objective_bits);
  std::vector<std::pair<std::uint32_t, std::uint64_t>>
      objective_event_transitions;
  objective_event_transitions.emplace_back(0U,
                                           last_objective_event_count);
  auto last_dialogue_word = runtime.diagnostics().dialogue_state_word;
  auto last_dialogue_words =
      runtime.diagnostics().dialogue_state_words;
  auto dialogue_queue_transitions = std::uint32_t{};
  auto dialogue_active_frames = std::uint32_t{};
  auto first_dialogue_active_frame = std::uint32_t{};
  std::vector<std::pair<std::uint32_t, std::uint32_t>>
      dialogue_state_transitions;
  dialogue_state_transitions.emplace_back(0U, last_dialogue_word);
  auto maximum_sprite_commands = std::size_t{};
  auto maximum_active_spu_voices = std::size_t{};
  auto minimum_player_health = std::numeric_limits<std::uint16_t>::max();
  auto first_xa_frame = std::uint32_t{};
  auto maximum_cd_lba = std::uint32_t{};
  auto maximum_relative_cd_lba = std::uint32_t{};
  auto last_guest_room = runtime.diagnostics().guest_current_room;
  std::vector<std::pair<std::uint32_t, std::uint16_t>>
      guest_room_transitions;
  guest_room_transitions.emplace_back(0U, last_guest_room);
  auto collision_residency_gap_frames = std::uint32_t{};
  auto first_collision_residency_gap_frame = std::uint32_t{};
  auto quick_state_saved = sf::game::Sf2GuestRuntimeDiagnostics{};
  auto quick_state_restored = false;
  auto quick_state_replay_matched = false;
  auto quick_state_original_digest = sf::core::Sha256Digest{};
  std::vector<std::byte> quick_state_original_ram(
      sf::psx::R3000Runtime::ram_size);
  std::vector<sf::game::LegacyHostPadState> quick_state_replay_pads;
  const auto quick_save_frame = frames / 3U;
  const auto quick_load_frame = frames * 2U / 3U;
  const auto quick_replay_updates =
      quick_load_frame > quick_save_frame + 1U
          ? quick_load_frame - quick_save_frame - 1U
          : 0U;
  const auto quick_replay_end_frame =
      quick_replay_updates == 0U
          ? quick_load_frame
          : quick_load_frame + quick_replay_updates - 1U;
  auto objective_previous_x = runtime.diagnostics().player_x;
  auto objective_previous_z = runtime.diagnostics().player_z;
  auto objective_heading = 0.0;
  auto objective_heading_known = false;
  auto objective_minimum_distance =
      std::numeric_limits<double>::infinity();
  auto objective_minimum_distance_frame = std::uint32_t{};
  auto objective_waypoint = std::size_t{};
  auto objective_looted = false;
  auto objective_loot_frame = std::uint32_t{};
  auto objective_baseline_captured = false;
  auto objective_initial_armor = std::uint16_t{};
  auto objective_initial_owned_items =
      std::array<std::uint32_t, 2U>{};
  auto weapon_select =
      sf::game::Sf2WeaponSelectPulseQueue{runtime.inputSampleCount()};
  auto weapon_pulses_queued = std::uint32_t{};
  auto last_weapon_pulse_frame = std::uint32_t{};
  auto shotgun_observed = false;
  auto shotgun_stress_frames = std::uint32_t{};
  auto last_equipped_item = runtime.diagnostics().player_equipped_item;
  std::vector<std::pair<std::uint32_t, std::uint32_t>>
      equipped_item_transitions;
  equipped_item_transitions.emplace_back(0U, last_equipped_item);
  auto observed_renderer_repairs =
      runtime.diagnostics().renderer_text_repairs;
  auto last_application_state = runtime.diagnostics().application_state;
  auto pause_menu_frame = std::optional<std::uint32_t>{};
  auto pause_gameplay_presentation_seen = false;
  auto pause_menu_presentation_seen = false;
  auto pause_restored_presentation_seen = false;
  auto pause_menu_maximum_draw_commands = std::size_t{};
  auto pause_gameplay_digest = sf::core::Sha256Digest{};
  auto pause_menu_digest = sf::core::Sha256Digest{};
  auto pause_restored_digest = sf::core::Sha256Digest{};
  std::vector<std::pair<std::uint32_t, std::uint32_t>>
      application_state_transitions;
  application_state_transitions.emplace_back(0U, last_application_state);
  auto first_rejected_sound_bank_frame = std::uint32_t{};
  auto first_rejected_sound_bank_caller = std::uint32_t{};
  auto first_rejected_sound_bank_magic = std::uint32_t{};
  const auto scripted_movie_catalog_indices =
      sf::game::missionScriptedMovieCatalogIndices(
          sf::game::GameId::syphon_filter_2, mission_index);
  if (compact_movie_trace &&
      scripted_movie_ordinal >= scripted_movie_catalog_indices.size()) {
    std::cerr << "SF2 movie probe ordinal is not mapped for mission "
              << mission_index + 1U << ": ordinal="
              << scripted_movie_ordinal << '\n';
    return 10;
  }
  const auto expected_scripted_movie_catalog_index =
      compact_movie_trace
          ? std::optional<std::uint8_t>{
                scripted_movie_catalog_indices[scripted_movie_ordinal]}
          : std::nullopt;
  auto scripted_movie_probe_requested = false;
  auto observed_scripted_movie_request =
      std::optional<std::uint8_t>{};
  auto scripted_movie_completion_observed = false;
  std::vector<std::uint8_t> completion_movie_catalog_indices;
  const auto scripted_movie_probe_frame =
      frames > 200U ? frames - 200U : frames / 2U;
  constexpr std::array<std::array<double, 2U>, 6U>
      objective_waypoints{{
          {3448.0, -25485.0},
          {2423.0, -21363.0},
          {1680.0, -18916.0},
          {2423.0, -21363.0},
          {3448.0, -25485.0},
          {4206.0, -25824.0},
      }};
  for (std::uint32_t frame = 0U; frame < frames; ++frame) {
    if (expected_scripted_movie_catalog_index &&
        frame == scripted_movie_probe_frame) {
      scripted_movie_probe_requested =
          runtime.requestScriptedMovieForProbe(
              *expected_scripted_movie_catalog_index);
      if (!scripted_movie_probe_requested) {
        std::cerr << "SF2 movie probe could not submit the retail request\n";
        return 10;
      }
    }
    if (expected_scripted_movie_catalog_index &&
        scripted_movie_probe_requested &&
        !observed_scripted_movie_request) {
      if (!runtime.captureQuickState()) {
        std::cerr << "SF2 movie probe could not snapshot the loader state\n";
        return 10;
      }
      const auto pending_movie = runtime.consumeScriptedMovieRequest();
      if (!pending_movie) {
        // MovieRequest_Start changes application state asynchronously; allow
        // the authentic state-9 loader callback to reach decoder init.
      } else if (!runtime.restoreQuickState()) {
        std::cerr << "SF2 movie probe could not restore the pending "
                     "handoff\n";
        return 10;
      } else {
        const auto replayed_scripted_movie_request =
            runtime.consumeScriptedMovieRequest();
        if (replayed_scripted_movie_request != pending_movie) {
          std::cerr << "SF2 movie handoff changed across F5/F9 restore\n";
          return 10;
        }
        observed_scripted_movie_request =
            replayed_scripted_movie_request;
        if (!runtime.completeScriptedMovie(
                *observed_scripted_movie_request)) {
          std::cerr << "SF2 movie probe could not run retail completion\n";
          return 10;
        }
        scripted_movie_completion_observed = true;
      }
    }
    if (retail_completion_flow) {
      if (const auto completion_movie =
              runtime.consumeScriptedMovieRequest()) {
        completion_movie_catalog_indices.push_back(*completion_movie);
        if (!runtime.completeScriptedMovie(*completion_movie)) {
          std::cerr << "SF2 completion-flow could not retire movie catalog "
                    << static_cast<unsigned int>(*completion_movie) << '\n';
          return 10;
        }
      }
    }
    if (mission_complete_probe && frame == 900U) {
      sf::game::CampaignCarryState carry;
      const auto m16 = static_cast<std::size_t>(sf::game::WeaponId::m_16);
      const auto shotgun =
          static_cast<std::size_t>(sf::game::WeaponId::shotgun);
      carry.current_weapon = static_cast<std::uint8_t>(m16);
      carry.owned_weapons = 1U | (1U << m16) | (1U << shotgun);
      carry.magazines[m16] = 17U;
      carry.reserves[m16] = 51U;
      carry.magazines[shotgun] = 6U;
      carry.reserves[shotgun] = 19U;
      carry.health = 123U;
      carry.armor = 321U;
      carry.sequel = sf::game::SequelCampaignCarryState{};
      carry.sequel->current_item = 4U;
      carry.sequel->owned_items = {(1U << 4U) | (1U << 8U), 1U << 1U};
      carry.sequel->magazines[4U] = 17U;
      carry.sequel->reserves[4U] = 51U;
      carry.sequel->magazines[8U] = 6U;
      carry.sequel->reserves[8U] = 19U;
      carry.sequel->magazines[33U] = 2U;
      carry.sequel->reserves[33U] = 9U;
      campaign_carry_applied = runtime.applyCampaignCarryState(carry);
      if (!campaign_carry_applied) {
        std::cerr << "SF2 completion probe could not apply campaign carry\n";
        return 10;
      }
      completion_carry = carry;
    }
    if (mission_complete_probe && frame == 1'000U &&
        ((completion_baseline = runtime.diagnostics()),
         !runtime.requestMissionSuccessForProbe())) {
      std::cerr << "SF2 completion probe could not enter retail success\n";
      return 10;
    }
    if (quick_state && frame == quick_load_frame) {
      if (!runtime.restoreQuickState()) {
        std::cerr << "SF2 quick-state probe could not restore\n";
        return 10;
      }
      const auto restored = runtime.diagnostics();
      quick_state_restored =
          restored.system_clock == quick_state_saved.system_clock &&
          restored.player_x == quick_state_saved.player_x &&
          restored.player_y == quick_state_saved.player_y &&
          restored.player_z == quick_state_saved.player_z &&
          restored.guest_current_room ==
              quick_state_saved.guest_current_room;
      if (!quick_state_restored) {
        std::cerr << "SF2 quick-state probe did not restore the saved "
                     "clock/player/room\n";
        return 10;
      }
    }
    const auto quick_original_segment =
        frame > quick_save_frame && frame < quick_load_frame;
    const auto quick_replay_segment =
        frame >= quick_load_frame && frame <= quick_replay_end_frame;
    if (quick_state &&
        (quick_original_segment || quick_replay_segment)) {
      pad.buttons = 0x0010U;
      pad.left_y = 0x00U;
    } else if (quick_state) {
      pad.buttons = 0U;
      pad.left_y = 0x80U;
    }
    if (combat && frame >= 1'000U) {
      sf::game::PlayerInput input;
      input.run = true;
      input.move = 1.0;
      input.turn = ((frame / 180U) & 1U) == 0U ? 0.45 : -0.45;
      input.strafe = ((frame / 240U) & 1U) == 0U ? 1.0 : -1.0;
      input.target_lock_held = (frame % 180U) < 120U;
      input.aim = (frame % 360U) >= 240U;
      input.aim_sight_yaw =
          input.aim ? (((frame / 30U) & 1U) == 0U ? 0.75 : -0.75) : 0.0;
      input.aim_sight_pitch =
          input.aim ? (((frame / 45U) & 1U) == 0U ? 0.5 : -0.5) : 0.0;
      input.fire_held = (frame % 24U) < 8U;
      input.reload = (frame % 600U) == 0U;
      pad = sf::game::legacyPadStateFromPlayerInput(input);
      if ((frame % 420U) < 4U) {
        pad.buttons = static_cast<std::uint16_t>(pad.buttons | 0x0001U);
      }
    }
    if (objective_event && frame >= 800U) {
      if (!runtime.setPlayerHealthForProbe(1000U)) {
        std::cerr << "SF2 objective-event probe could not retain diagnostic "
                     "player health\n";
        return 10;
      }
      const auto player = runtime.diagnostics();
      if (!objective_baseline_captured) {
        objective_initial_armor = player.player_armor;
        objective_initial_owned_items = player.player_owned_items;
        objective_baseline_captured = true;
      } else if (!objective_looted &&
                 (player.player_armor > objective_initial_armor ||
                  player.player_owned_items !=
                      objective_initial_owned_items)) {
        objective_looted = true;
        objective_loot_frame = frame;
        objective_waypoint = 3U;
      }
      const auto velocity_x =
          static_cast<double>(player.player_x - objective_previous_x);
      const auto velocity_z =
          static_cast<double>(player.player_z - objective_previous_z);
      if (std::hypot(velocity_x, velocity_z) >= 2.0) {
        objective_heading = std::atan2(velocity_z, velocity_x);
        objective_heading_known = true;
      }
      objective_previous_x = player.player_x;
      objective_previous_z = player.player_z;
      auto delta_x =
          objective_waypoints[objective_waypoint][0U] - player.player_x;
      auto delta_z =
          objective_waypoints[objective_waypoint][1U] - player.player_z;
      auto distance = std::hypot(delta_x, delta_z);
      if (distance <= 300.0 &&
          objective_waypoint + 1U < objective_waypoints.size() &&
          (objective_looted || objective_waypoint + 1U < 3U)) {
        ++objective_waypoint;
        delta_x =
            objective_waypoints[objective_waypoint][0U] - player.player_x;
        delta_z =
            objective_waypoints[objective_waypoint][1U] - player.player_z;
        distance = std::hypot(delta_x, delta_z);
      }
      if (distance < objective_minimum_distance) {
        objective_minimum_distance = distance;
        objective_minimum_distance_frame = frame;
      }
      auto navigation = sf::game::PlayerInput{};
      navigation.run = true;
      navigation.move = distance > 260.0 ? 1.0 : 0.0;
      if (objective_heading_known) {
        constexpr auto pi = 3.14159265358979323846;
        auto error =
            std::atan2(delta_z, delta_x) - objective_heading;
        while (error > pi) {
          error -= 2.0 * pi;
        }
        while (error < -pi) {
          error += 2.0 * pi;
        }
        navigation.turn = -std::clamp(error, -1.0, 1.0);
      }
      navigation.interact =
          !objective_looted && objective_waypoint == 2U &&
          distance <= 340.0 && (frame % 30U) < 6U;
      if (skip_objective_scene && objective_looted &&
          frame - objective_loot_frame < 12U) {
        // Cross skips the short interaction scene in retail. Hold it across
        // enough 60 Hz submissions for one 20 Hz PAD sample, then begin the
        // return traversal that previously exposed the manual crash.
        navigation.kneel = true;
        navigation.move = 0.0;
      }
      pad = sf::game::legacyPadStateFromPlayerInput(navigation);
    }
    if (weapon_cycle && objective_looted &&
        frame >= objective_loot_frame + 120U) {
      // Dwell for one second on each retail selection. The older probe queued
      // all twelve Select edges at once and changed weapons every sampled
      // input interval, which could not reproduce the reported shotgun plus
      // crouch/back instability.
      if (weapon_pulses_queued < 12U &&
          (weapon_pulses_queued == 0U ||
           frame - last_weapon_pulse_frame >= 60U)) {
        weapon_select.enqueue(1U);
        ++weapon_pulses_queued;
        last_weapon_pulse_frame = frame;
      }
      const auto weapon_select_down =
          weapon_select.update(runtime.inputSampleCount());
      if (runtime.diagnostics().player_equipped_item == 8U) {
        shotgun_observed = true;
        auto stress = sf::game::PlayerInput{};
        stress.move = -1.0;
        stress.kneel = true;
        stress.fire_held = (frame % 18U) < 9U;
        pad = sf::game::legacyPadStateFromPlayerInput(stress);
        ++shotgun_stress_frames;
      }
      if (weapon_select_down) {
        pad.buttons = static_cast<std::uint16_t>(pad.buttons | 0x0001U);
      }
    }
    if (pause_flow) {
      // Long authored openings do not all unlock Start at the same host
      // frame. Retry a sampled pulse while gameplay owns state 0, then wait
      // for the observed state-7 menu before sending the close pulse.
      pad.buttons = static_cast<std::uint16_t>(pad.buttons & ~0x0008U);
      const auto pause_state = runtime.diagnostics();
      if (!pause_menu_frame && pause_state.application_state == 7U) {
        pause_menu_frame = frame;
      }
      const auto pulse_window = (frame % 120U) < 6U;
      const auto opening = frame >= 1'000U &&
                           pause_state.menu_vram_snapshots == 0U &&
                           pause_state.application_state == 0U &&
                           pulse_window;
      const auto closing = pause_menu_frame &&
                           frame >= *pause_menu_frame + 120U &&
                           pause_state.menu_vram_restores == 0U &&
                           pause_state.application_state == 7U &&
                           pulse_window;
      if (opening || closing) {
        pad.buttons = static_cast<std::uint16_t>(pad.buttons | 0x0008U);
      }
    }
    if (retail_completion_flow && mission_shell_resumed) {
      // State 4 is the retail post-mission save/menu overlay. Periodic Cross
      // edges accept its default path while preserving the guest's own menu
      // and movie-selection logic for this diagnostic continuation.
      pad.buttons = (frame % 120U) < 6U ? 0x4000U : 0U;
      pad.left_x = 0x80U;
      pad.left_y = 0x80U;
    }
    if (quick_state && quick_original_segment) {
      quick_state_replay_pads.push_back(pad);
    } else if (quick_state && quick_replay_segment) {
      const auto replay_index =
          static_cast<std::size_t>(frame - quick_load_frame);
      if (replay_index >= quick_state_replay_pads.size()) {
        std::cerr << "SF2 quick-state replay input transcript is incomplete\n";
        return 10;
      }
      pad = quick_state_replay_pads[replay_index];
    }
    runtime.setHostPadState(pad);
    if (!runtime.advanceHostUpdate()) {
      if (runtime.missionCompleteRequested()) {
        mission_completion_observed = true;
        break;
      }
      if (retail_completion_flow) {
        const auto stopped = runtime.diagnostics();
        std::cerr << "SF2 completion-flow stopped: state="
                  << stopped.application_state << " mission="
                  << stopped.selected_mission_index << " calls="
                  << stopped.campaign_advance_calls << "/"
                  << stopped.movie_request_calls << "/"
                  << stopped.movie_playback_init_calls << "/catalog="
                  << stopped.selected_movie_catalog_index << ":request=0x"
                  << std::hex << std::uppercase;
        for (const auto argument : stopped.last_movie_request_arguments) {
          std::cerr << argument << "/";
        }
        std::cerr << ":playback=0x";
        for (const auto argument : stopped.last_movie_playback_arguments) {
          std::cerr << argument << "/";
        }
        std::cerr << std::dec << " fault=" << runtime.faultDetail() << '\n';
      }
      std::cerr << "SF2 product runtime stopped at frame " << frame << ": "
                << runtime.faultDetail() << '\n';
      return 7;
    }
    if (runtime.missionCompleteRequested()) {
      mission_completion_observed = true;
      if (retail_completion_flow && !mission_shell_resumed) {
        if (!runtime.resumeMissionShellForProbe()) {
          std::cerr << "SF2 completion-flow probe could not resume retail "
                       "mission shell\n";
          return 10;
        }
        mission_shell_resumed = true;
      } else {
        break;
      }
    }
    while (const auto count = runtime.takePcm(pcm)) {
      pcm_frames += count;
      inspect_pcm(count);
    }
    const auto frame_diagnostics = runtime.diagnostics();
    if (frame_diagnostics.application_state != last_application_state) {
      last_application_state = frame_diagnostics.application_state;
      application_state_transitions.emplace_back(frame + 1U,
                                                 last_application_state);
    }
    if (frame_diagnostics.player_target_active) {
      ++target_active_frames;
      if (first_target_frame == 0U) {
        first_target_frame = frame + 1U;
      }
    }
    maximum_player_danger =
        std::max(maximum_player_danger, frame_diagnostics.player_danger);
    maximum_player_threat_count =
        std::max(maximum_player_threat_count,
                 frame_diagnostics.player_threat_count);
    maximum_radar_actor_count =
        std::max(maximum_radar_actor_count,
                 frame_diagnostics.radar_actor_count);
    const auto frame_forward_length_squared =
        static_cast<std::int64_t>(frame_diagnostics.player_forward_x) *
            frame_diagnostics.player_forward_x +
        static_cast<std::int64_t>(frame_diagnostics.player_forward_z) *
            frame_diagnostics.player_forward_z;
    valid_radar_heading_seen =
        valid_radar_heading_seen ||
        frame_forward_length_squared >= 2048LL * 2048LL;
    if (frame_diagnostics.objective_completion_bits !=
        last_objective_bits) {
      last_objective_bits = frame_diagnostics.objective_completion_bits;
      objective_bit_transitions.emplace_back(frame + 1U,
                                             last_objective_bits);
    }
    if (frame_diagnostics.objective_completion_events !=
        last_objective_event_count) {
      last_objective_event_count =
          frame_diagnostics.objective_completion_events;
      objective_event_transitions.emplace_back(
          frame + 1U, last_objective_event_count);
    }
    if (frame_diagnostics.dialogue_state_word !=
        last_dialogue_word) {
      last_dialogue_word = frame_diagnostics.dialogue_state_word;
      dialogue_state_transitions.emplace_back(frame + 1U,
                                              last_dialogue_word);
    }
    if (frame_diagnostics.dialogue_state_words !=
        last_dialogue_words) {
      last_dialogue_words =
          frame_diagnostics.dialogue_state_words;
      ++dialogue_queue_transitions;
    }
    if (std::ranges::any_of(
            frame_diagnostics.dialogue_state_words,
            [](std::uint32_t state) {
              return state != 0xffffffffU;
            })) {
      if (first_dialogue_active_frame == 0U) {
        first_dialogue_active_frame = frame + 1U;
      }
      ++dialogue_active_frames;
    }
    if (first_rejected_sound_bank_frame == 0U &&
        frame_diagnostics.rejected_sound_bank_lookups != 0U) {
      first_rejected_sound_bank_frame = frame + 1U;
      first_rejected_sound_bank_caller =
          frame_diagnostics.last_rejected_sound_bank_caller;
      first_rejected_sound_bank_magic =
          frame_diagnostics.last_rejected_sound_bank_magic;
    }
    if (frame_diagnostics.player_equipped_item != last_equipped_item) {
      last_equipped_item = frame_diagnostics.player_equipped_item;
      equipped_item_transitions.emplace_back(frame + 1U, last_equipped_item);
    }
    if (frame_diagnostics.renderer_text_repairs !=
        observed_renderer_repairs) {
      std::cerr << "SF2 probe renderer repair at frame " << frame + 1U
                << ": count="
                << frame_diagnostics.renderer_text_repairs
                << " address=0x" << std::hex << std::uppercase
                << frame_diagnostics.last_renderer_text_repair_address
                << " expected=0x"
                << frame_diagnostics.last_renderer_text_expected
                << " actual=0x"
                << frame_diagnostics.last_renderer_text_actual
                << " writer=0x"
                << frame_diagnostics.last_renderer_text_writer_pc
                << "/0x"
                << frame_diagnostics.last_renderer_text_writer_instruction
                << std::dec << '\n';
      observed_renderer_repairs =
          frame_diagnostics.renderer_text_repairs;
    }
    if (quick_state && frame == quick_save_frame) {
      quick_state_saved = frame_diagnostics;
      if (!runtime.captureQuickState()) {
        std::cerr << "SF2 quick-state probe could not capture\n";
        return 10;
      }
    }
    if (quick_state && frame + 1U == quick_load_frame) {
      quick_state_original_digest = runtime.guestRamDigestForProbe();
      if (!runtime.copyGuestRamForProbe(quick_state_original_ram)) {
        std::cerr << "SF2 quick-state probe could not retain replay RAM\n";
        return 10;
      }
    }
    if (quick_state && quick_replay_updates != 0U &&
        frame == quick_replay_end_frame) {
      const auto replay_digest = runtime.guestRamDigestForProbe();
      quick_state_replay_matched =
          replay_digest == quick_state_original_digest;
      if (!quick_state_replay_matched) {
        std::cerr << "SF2 quick-state deterministic replay diverged: "
                  << sf::core::toHex(quick_state_original_digest) << "/"
                  << sf::core::toHex(replay_digest);
        std::vector<std::byte> replay_ram(
            sf::psx::R3000Runtime::ram_size);
        if (runtime.copyGuestRamForProbe(replay_ram)) {
          auto reported = std::size_t{};
          std::cerr << " differences=";
          for (auto offset = std::size_t{};
               offset < replay_ram.size() && reported < 32U; ++offset) {
            if (replay_ram[offset] == quick_state_original_ram[offset]) {
              continue;
            }
            std::cerr << (reported == 0U ? "" : ",") << "0x"
                      << std::hex << std::uppercase
                      << (0x80000000U +
                          static_cast<std::uint32_t>(offset))
                      << ":" << static_cast<unsigned int>(
                                     std::to_integer<std::uint8_t>(
                                         quick_state_original_ram[offset]))
                      << "/"
                      << static_cast<unsigned int>(
                             std::to_integer<std::uint8_t>(
                                 replay_ram[offset]))
                      << std::dec;
            ++reported;
          }
        }
        std::cerr << '\n';
        return 10;
      }
    }
    if (frame_diagnostics.guest_current_room != last_guest_room) {
      last_guest_room = frame_diagnostics.guest_current_room;
      guest_room_transitions.emplace_back(frame + 1U, last_guest_room);
    }
    const auto current_room_is_valid =
        frame_diagnostics.guest_current_room != 0xffffU &&
        frame_diagnostics.guest_current_room <
            frame_diagnostics.guest_collision_room_count;
    if (current_room_is_valid &&
        (frame_diagnostics.guest_collision_room_record == 0U ||
         frame_diagnostics.guest_collision_list == 0U)) {
      ++collision_residency_gap_frames;
      if (first_collision_residency_gap_frame == 0U) {
        first_collision_residency_gap_frame = frame + 1U;
      }
    }
    minimum_player_health =
        std::min(minimum_player_health, frame_diagnostics.player_health);
    maximum_active_spu_voices =
        std::max(maximum_active_spu_voices,
                 frame_diagnostics.active_spu_voices);
    maximum_cd_lba = std::max(maximum_cd_lba, frame_diagnostics.cd_lba);
    if (frame_diagnostics.cd_lba < 4096U) {
      maximum_relative_cd_lba =
          std::max(maximum_relative_cd_lba, frame_diagnostics.cd_lba);
    }
    if (first_xa_frame == 0U &&
        frame_diagnostics.xa_stream_set != 0U) {
      first_xa_frame = frame + 1U;
    }
    if (first_restore_frame == 0U &&
        frame_diagnostics.checkpoint_restores != 0U) {
      first_restore_frame = frame + 1U;
    }
    const auto &published = runtime.presentationFrame();
    if (!published || published->sequence == observed_sequence) {
      continue;
    }
    observed_sequence = published->sequence;
    if (pause_flow) {
      std::vector<std::uint32_t> words;
      words.reserve(published->gp0_word_count);
      for (const auto &packet : published->packets) {
        words.insert(words.end(), packet.gp0_words.begin(),
                     packet.gp0_words.end());
      }
      const auto digest = sf::core::sha256(std::as_bytes(
          std::span<const std::uint32_t>{words}));
      if (frame_diagnostics.menu_vram_snapshots == 0U &&
          published->application_state == 0U) {
        pause_gameplay_presentation_seen = true;
        pause_gameplay_digest = digest;
      } else if (published->application_state == 7U) {
        pause_menu_presentation_seen = true;
        pause_menu_digest = digest;
        pause_menu_maximum_draw_commands = std::max(
            pause_menu_maximum_draw_commands,
            published->draw_command_count);
      } else if (frame_diagnostics.menu_vram_restores != 0U &&
                 published->application_state == 0U) {
        pause_restored_presentation_seen = true;
        pause_restored_digest = digest;
      }
    }
    const auto sprite_commands = std::ranges::count_if(
        published->packets, [](const auto &packet) {
          if (packet.gp0_words.empty()) {
            return false;
          }
          const auto opcode = packet.gp0_words.front() >> 24U;
          return opcode >= 0x60U && opcode <= 0x7fU;
        });
    maximum_sprite_commands =
        std::max(maximum_sprite_commands,
                 static_cast<std::size_t>(sprite_commands));
    if (first_sprite_frame == 0U && sprite_commands != 0) {
      first_sprite_frame = frame + 1U;
    }
    std::array<std::uint32_t, 4U> environment{};
    for (const auto &packet : published->packets) {
      if (packet.gp0_words.empty() ||
          (packet.gp0_words.front() >> 24U) < 0xe1U) {
        continue;
      }
      for (const auto word : packet.gp0_words) {
        const auto opcode = word >> 24U;
        if (opcode == 0xe1U) {
          environment[0U] = word;
        } else if (opcode == 0xe3U) {
          environment[1U] = word;
        } else if (opcode == 0xe4U) {
          environment[2U] = word;
        } else if (opcode == 0xe5U) {
          environment[3U] = word;
        }
      }
    }
    if ((display_environments.empty() ||
         display_environments.back() != environment) &&
        display_environments.size() < 16U) {
      display_environments.push_back(environment);
    }
  }
  const auto &presentation = runtime.presentationFrame();
  if (!presentation || !presentation->valid()) {
    std::cerr << "SF2 product runtime did not publish a valid frame\n";
    return 7;
  }
  while (const auto count = runtime.takePcm(pcm)) {
    pcm_frames += count;
    inspect_pcm(count);
  }
  const auto diagnostics = runtime.diagnostics();
  // Mission 8 -> 9 is the sole SF2 disc boundary. The platform resolver and
  // campaign tests own that path; this guest probe can boot the next runtime
  // directly only while the supplied CUE remains authoritative.
  const auto next_mission_is_same_disc = mission_index != 7U;
  if (retail_completion_flow && mission_index < 20U &&
      next_mission_is_same_disc &&
      sf::game::campaignMissionsShareCarry(mission_index,
                                           mission_index + 1U)) {
    if (!completion_carry || !completion_carry->sequel) {
      std::cerr << "SF2 completion-flow lost its exact sequel carry\n";
      return 10;
    }
    sf::game::Sf2GuestMissionRuntime next_runtime{
        std::filesystem::path{cue_path}, mission_index + 1U};
    if (!next_runtime.ready() ||
        !next_runtime.applyCampaignCarryState(*completion_carry)) {
      std::cerr << "SF2 completion-flow could not apply carry to mission "
                << mission_index + 2U << '\n';
      return 10;
    }
    next_runtime.setHostPadState({});
    if (!next_runtime.advanceHostUpdate()) {
      std::cerr << "SF2 completion-flow next mission did not advance: "
                << next_runtime.faultDetail() << '\n';
      return 10;
    }
    const auto next = next_runtime.diagnostics();
    const auto &expected = *completion_carry->sequel;
    next_mission_carry_verified =
        next.player_health == completion_carry->health &&
        next.player_armor == completion_carry->armor &&
        next.player_equipped_item == expected.current_item &&
        next.player_owned_items == expected.owned_items &&
        next.player_magazines == expected.magazines &&
        next.player_reserves == expected.reserves;
    if (!next_mission_carry_verified) {
      std::cerr << "SF2 completion-flow mission " << mission_index + 2U
                << " did not retain exact health/armor/inventory carry\n";
      return 10;
    }
  }
  if (compact_movie_trace) {
    std::cout << "SF2 movie trace: mission=" << mission_index + 1U
              << " state=" << diagnostics.application_state
              << " calls=" << diagnostics.movie_playback_init_calls
              << " catalog=";
    const auto count = std::min<std::uint64_t>(
        diagnostics.movie_playback_init_calls,
        diagnostics.movie_playback_catalog_history.size());
    for (auto index = std::size_t{}; index < count; ++index) {
      std::cout << diagnostics.movie_playback_catalog_history[index] << "/";
    }
    std::cout << " host-handoff=";
    if (observed_scripted_movie_request) {
      std::cout << static_cast<unsigned int>(
          *observed_scripted_movie_request);
    } else {
      std::cout << "none";
    }
    std::cout << '\n';
    if (expected_scripted_movie_catalog_index &&
        (!scripted_movie_probe_requested ||
         observed_scripted_movie_request !=
             expected_scripted_movie_catalog_index ||
         diagnostics.scripted_movie_handoffs == 0U ||
         diagnostics.last_scripted_movie_catalog_index !=
             *expected_scripted_movie_catalog_index)) {
      std::cerr << "SF2 scripted movie request did not reach the expected "
                   "native handoff: expected="
                << static_cast<unsigned int>(
                       *expected_scripted_movie_catalog_index)
                << " observed=";
      if (observed_scripted_movie_request) {
        std::cerr << static_cast<unsigned int>(
            *observed_scripted_movie_request);
      } else {
        std::cerr << "none";
      }
      std::cerr << " handoffs=" << diagnostics.scripted_movie_handoffs
                << '\n';
      return 10;
    }
    if (!scripted_movie_completion_observed) {
      std::cerr << "SF2 movie probe did not complete the retail handoff\n";
      return 10;
    }
    return 0;
  }
  std::array<std::uint32_t, 6U> pause_overlay_words{};
  if (pause_flow) {
    std::vector<std::byte> ram(sf::psx::R3000Runtime::ram_size);
    if (!runtime.copyGuestRamForProbe(ram)) {
      std::cerr << "SF2 pause probe could not copy guest RAM\n";
      return 10;
    }
    const auto read32 = [&ram](std::uint32_t address) {
      const auto offset = static_cast<std::size_t>(address & 0x1fffffU);
      return static_cast<std::uint32_t>(
          std::to_integer<std::uint8_t>(ram[offset]) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 1U]))
           << 8U) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 2U]))
           << 16U) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 3U]))
           << 24U));
    };
    constexpr std::array pause_addresses{
        0x8014aef4U, 0x8014aef8U, 0x8014b110U,
        0x8014b114U, 0x8014b118U, 0x8014b11cU};
    for (auto index = std::size_t{}; index < pause_addresses.size(); ++index) {
      pause_overlay_words[index] = read32(pause_addresses[index]);
    }
  }
  if (scan_ui_objects) {
    std::vector<std::byte> ram(sf::psx::R3000Runtime::ram_size);
    if (!runtime.copyGuestRamForProbe(ram)) {
      std::cerr << "SF2 UI-object probe could not copy guest RAM\n";
      return 10;
    }
    const auto read16 = [&ram](std::size_t offset) {
      return static_cast<std::uint16_t>(
          std::to_integer<std::uint8_t>(ram[offset]) |
          (static_cast<std::uint16_t>(
               std::to_integer<std::uint8_t>(ram[offset + 1U]))
           << 8U));
    };
    const auto read32 = [&ram](std::size_t offset) {
      return static_cast<std::uint32_t>(
          std::to_integer<std::uint8_t>(ram[offset]) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 1U]))
           << 8U) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 2U]))
           << 16U) |
          (static_cast<std::uint32_t>(
               std::to_integer<std::uint8_t>(ram[offset + 3U]))
           << 24U));
    };
    std::cout << "SF2 UI sprite packets:";
    for (const auto &packet : presentation->packets) {
      if (packet.gp0_words.empty()) {
        continue;
      }
      const auto opcode = packet.gp0_words.front() >> 24U;
      if (opcode < 0x60U || opcode > 0x7fU) {
        continue;
      }
      std::cout << " 0x" << std::hex << std::uppercase
                << packet.guest_address << ":";
      for (const auto word : packet.gp0_words) {
        std::cout << word << "/";
      }
      std::cout << std::dec;
    }
    std::cout << '\n';
    constexpr auto timer_format_string = 0x801bb182U;
    std::cout << "SF2 timer-format references:";
    auto format_references = std::size_t{};
    for (auto offset = std::size_t{}; offset + 4U <= ram.size();
         offset += 4U) {
      if (read32(offset) != timer_format_string) {
        continue;
      }
      std::cout << " 0x" << std::hex << std::uppercase
                << (0x80000000U + static_cast<std::uint32_t>(offset));
      ++format_references;
    }
    std::cout << std::dec << " count=" << format_references << '\n';
    // TextHandle_Resolve at 0x800A5718 proves a 64-entry, 0x1c-byte object
    // pool at 0x80137B04. TextGlyph_Allocate at 0x800A6290 proves the sequel
    // glyph pool at 0x80137014 uses a 0x10-byte stride and 0xaf entries. Dump
    // only live, pool-backed records; this is deterministic structural
    // evidence rather than a whole-RAM pattern guess.
    constexpr auto text_object_pool = 0x00137b04U;
    constexpr auto text_object_stride = 0x1cU;
    constexpr auto text_object_capacity = 64U;
    constexpr auto glyph_pool_begin = 0x80137014U;
    constexpr auto glyph_pool_end = glyph_pool_begin + 0xafU * 0x10U;
    auto live_objects = 0U;
    std::cout << "SF2 live text objects:";
    for (auto index = 0U; index < text_object_capacity; ++index) {
      const auto object = text_object_pool + index * text_object_stride;
      const auto glyph_pointer = read32(object);
      const auto glyph_count = read16(object + 0x0cU);
      if (glyph_pointer < glyph_pool_begin || glyph_pointer >= glyph_pool_end ||
          glyph_count == 0U || glyph_count > 0xafU) {
        continue;
      }
      std::cout << " " << index << "=0x" << std::hex << std::uppercase
                << glyph_pointer << std::dec << "/n" << glyph_count << "/g"
                << static_cast<unsigned int>(
                       std::to_integer<std::uint8_t>(ram[object + 0x15U]))
                << "/f0x" << std::hex
                << static_cast<unsigned int>(
                       std::to_integer<std::uint8_t>(ram[object + 0x14U]))
                << "/xy" << std::dec
                << std::bit_cast<std::int16_t>(read16(object + 0x0eU)) << ","
                << std::bit_cast<std::int16_t>(read16(object + 0x10U));
      ++live_objects;
    }
    std::cout << " count=" << live_objects << '\n';
    std::cout << "SF2 UI text events:";
    const auto first_ui_event =
        diagnostics.ui_text_event_count > diagnostics.ui_text_events.size()
            ? diagnostics.ui_text_event_count -
                  diagnostics.ui_text_events.size()
            : 0U;
    for (auto serial = first_ui_event;
         serial < diagnostics.ui_text_event_count; ++serial) {
      const auto &event = diagnostics.ui_text_events[
          serial % diagnostics.ui_text_events.size()];
      std::cout << " [" << serial << "/"
                << static_cast<unsigned int>(event.kind) << "@"
                << event.guest_frame << "/" << event.system_clock << ":0x"
                << std::hex << std::uppercase << event.arguments[0U] << "/"
                << event.arguments[1U] << "/" << event.arguments[2U] << "/"
                << event.arguments[3U] << std::dec << ":";
      for (const auto character : event.text) {
        if (character == '\0') {
          break;
        }
        std::cout << (static_cast<unsigned char>(character) < 0x20U ? ' '
                                                                      : character);
      }
      std::cout << "]";
    }
    std::cout << '\n';
  }
  std::vector<std::uint32_t> presentation_words;
  presentation_words.reserve(presentation->gp0_word_count);
  for (const auto &packet : presentation->packets) {
    presentation_words.insert(presentation_words.end(),
                              packet.gp0_words.begin(),
                              packet.gp0_words.end());
  }
  const auto input_mode =
      weapon_cycle   ? "weapons"
      : pause_flow   ? "pause"
      : retail_completion_flow ? "completeflow"
      : mission_complete_probe ? "complete"
      : objective_event && quick_state
          ? "quickobjective"
      : objective_event
          ? "objective"
      : combat       ? "combat"
      : forward      ? "forward"
      : crouch       ? "crouch"
      : quick_state  ? "quickstate"
      : scan_ui_objects ? "ui"
                      : "neutral";
  std::cout << "SF2 product runtime completed: frames=" << frames
            << " input=" << input_mode
            << (objective_event
                    ? " objective-nearest=" +
                          std::to_string(
                              static_cast<unsigned int>(
                                  std::lround(objective_minimum_distance))) +
                          "@" +
                          std::to_string(objective_minimum_distance_frame) +
                          "/wp" + std::to_string(objective_waypoint) +
                          "/loot@" +
                          std::to_string(objective_loot_frame)
                    : std::string{})
            << " guest-frame=" << presentation->guest_frame
            << " packets=" << presentation->packets.size()
            << " words=" << presentation->gp0_word_count
            << " commands=" << presentation->gpu_command_count
            << " draws=" << presentation->draw_command_count
            << " pcm=" << pcm_frames
            << ":" << nonzero_pcm_frames << "/" << peak_pcm_sample
            << " pad-samples=" << runtime.inputSampleCount()
            << " pad-caller=0x" << std::hex << std::uppercase
            << diagnostics.last_pad_caller << std::dec
            << " pad-index=" << diagnostics.last_pad_index
            << " state=" << diagnostics.application_state
            << "/transitions=";
  for (const auto &[frame, state] : application_state_transitions) {
    std::cout << frame << ":" << state << "/";
  }
  if (pause_flow) {
    std::cout << "/menu=";
    for (const auto word : pause_overlay_words) {
      std::cout << "0x" << std::hex << std::uppercase << word << "/";
    }
    std::cout << std::dec << "presentation="
              << (pause_gameplay_presentation_seen ? 1 : 0) << "/"
              << (pause_menu_presentation_seen ? 1 : 0) << "/"
              << (pause_restored_presentation_seen ? 1 : 0) << "/draws="
              << pause_menu_maximum_draw_commands << "/digests="
              << sf::core::toHex(pause_gameplay_digest).substr(0U, 12U)
              << "/" << sf::core::toHex(pause_menu_digest).substr(0U, 12U)
              << "/" << sf::core::toHex(pause_restored_digest).substr(0U, 12U);
  }
  std::cout << " mission=" << diagnostics.selected_mission_index
            << " clock=" << diagnostics.system_clock
            << " player=0x" << std::hex << std::uppercase
            << diagnostics.player_instance << std::dec << ":"
            << diagnostics.player_x << "/" << diagnostics.player_y << "/"
            << diagnostics.player_z << ":forward="
            << diagnostics.player_forward_x << "/"
            << diagnostics.player_forward_z << ":radar="
            << static_cast<unsigned int>(diagnostics.radar_actor_count) << "/"
            << static_cast<unsigned int>(maximum_radar_actor_count)
            << ":room="
            << diagnostics.guest_current_room << "/"
            << diagnostics.guest_collision_room_count << ":collision=0x"
            << std::hex << diagnostics.guest_collision_room_record << "/"
            << diagnostics.guest_collision_list << std::dec << ":"
            << diagnostics.player_health << "/" << diagnostics.player_armor
            << ":target=" << diagnostics.player_target_active << "/"
            << diagnostics.player_target_slot << "/"
            << diagnostics.player_target_meter << "/health="
            << static_cast<unsigned int>(
                   diagnostics.player_target_health_percent)
            << "/first@"
            << first_target_frame << "/frames=" << target_active_frames
            << ":objectives="
            << diagnostics.objective_state_valid << "/0x" << std::hex
            << diagnostics.objective_completion_bits << std::dec << "/"
            << objective_bit_transitions.size() << "/events="
            << diagnostics.objective_completion_events << "/last="
            << diagnostics.last_objective_completion_index << "/"
            << diagnostics.last_objective_completion_text << "/trace="
            << objective_event_transitions.size() << ":"
            ;
  for (const auto &[frame, count] : objective_event_transitions) {
    std::cout << frame << "@" << count << "/";
  }
  std::cout << ":bits=";
  for (const auto &[frame, bits] : objective_bit_transitions) {
    std::cout << frame << "@0x" << std::hex << bits << std::dec << "/";
  }
  std::cout
            << ":pickups=" << diagnostics.pickup_presentation_events
            << "/" << diagnostics.last_pickup_actor << "/0x" << std::hex
            << diagnostics.last_pickup_text << std::dec << "/"
            << diagnostics.last_pickup_item << "/words=";
  for (const auto word : diagnostics.last_pickup_text_words) {
    std::cout << std::hex << word << "/";
  }
  std::cout << std::dec
            << ":threat=" << diagnostics.threat_state_valid << "/"
            << diagnostics.player_object_slot << "/"
            << diagnostics.player_threat_count << "/"
            << static_cast<unsigned int>(diagnostics.player_danger)
            << "/max=" << maximum_player_threat_count << "/"
            << static_cast<unsigned int>(maximum_player_danger)
            << ":dialogue=" << diagnostics.dialogue_state_valid << "/0x"
            << std::hex << diagnostics.dialogue_state_word << std::dec
            << "/queues=0x" << std::hex
            << diagnostics.dialogue_state_words[0] << "/"
            << diagnostics.dialogue_state_words[1] << "/"
            << diagnostics.dialogue_state_words[2] << std::dec
            << "/transitions=" << dialogue_queue_transitions
            << "/active=" << dialogue_active_frames << "/first@"
            << first_dialogue_active_frame << "/"
            << dialogue_state_transitions.size() << ":";
  for (const auto &[frame, word] : dialogue_state_transitions) {
    std::cout << frame << "@0x" << std::hex << word << std::dec << "/";
  }
  std::cout
            << ":item=" << diagnostics.player_equipped_item << ":owned=0x"
            << std::hex << diagnostics.player_owned_items[0] << "/"
            << diagnostics.player_owned_items[1] << std::dec
            << " weapon-cycle=";
  for (const auto &[frame, item] : equipped_item_transitions) {
    std::cout << frame << ":" << item << "/";
  }
  std::cout << " shotgun-stress=" << shotgun_observed << "/"
            << shotgun_stress_frames << " room-fallbacks="
            << diagnostics.collision_room_fallbacks << ":"
            << diagnostics.last_collision_room_fallback
            << "/request:"
            << diagnostics.collision_request_fallbacks << ":"
            << diagnostics.last_collision_request_fallback
            << "/player:"
            << diagnostics.player_collision_requests << "/invalid:"
            << diagnostics.invalid_player_collision_requests
            << " renderer-repairs=" << diagnostics.renderer_text_repairs
            << ":0x" << std::hex << std::uppercase
            << diagnostics.last_renderer_text_repair_address << "/"
            << diagnostics.last_renderer_text_expected << "/"
            << diagnostics.last_renderer_text_actual << "/"
            << diagnostics.last_renderer_text_writer_pc << "/"
            << diagnostics.last_renderer_text_writer_instruction
            << std::dec << " render-views=" << diagnostics.render_view_adds
            << "/" << diagnostics.render_view_removes << ":0x" << std::hex
            << diagnostics.last_render_view_added << "/"
            << diagnostics.last_render_view_removed
            << ":head/node/flags/next="
            << diagnostics.render_view_head << "/"
            << diagnostics.player_render_node << "/"
            << diagnostics.player_render_flags << "/"
            << diagnostics.player_render_next << ":chain=";
  for (auto index = std::size_t{};
       index < diagnostics.render_view_chain.size() &&
       diagnostics.render_view_chain[index] != 0U; ++index) {
    std::cout << diagnostics.render_view_chain[index] << "/"
              << diagnostics.render_view_chain_nodes[index] << "/"
              << diagnostics.render_view_chain_flags[index] << ",";
  }
  std::cout
            << std::dec
            << " rejected-renderer-ots="
            << diagnostics.rejected_renderer_ordering_tables
            << ":0x" << std::hex << std::uppercase
            << diagnostics.last_rejected_renderer_packet << "/"
            << diagnostics.last_rejected_renderer_root << std::dec << "/"
            << diagnostics.last_rejected_renderer_frame
            << " rejected-renderer-vertices="
            << diagnostics.rejected_renderer_vertex_entries << ":0x"
            << std::hex << std::uppercase
            << diagnostics.last_rejected_renderer_vertex_cursor << "/"
            << diagnostics.last_rejected_renderer_vertex_address << std::dec
            << " clamped-renderer-ots="
            << diagnostics.clamped_renderer_ordering_table_entries
            << ":0x" << std::hex << std::uppercase
            << diagnostics.last_renderer_ordering_table_requested << "/"
            << diagnostics.last_renderer_ordering_table_clamped << "/"
            << diagnostics.last_renderer_ordering_table_base << std::dec
            << "/"
            << diagnostics.last_renderer_ordering_table_buckets
            << " rejected-renderer-merges="
            << diagnostics.rejected_renderer_list_merges << ":0x"
            << std::hex << std::uppercase
            << diagnostics.last_rejected_renderer_list_descriptor << "/"
            << diagnostics.last_rejected_renderer_list_root << "/"
            << diagnostics.last_rejected_renderer_list_cursor << "/"
            << diagnostics.last_rejected_renderer_list_tag << std::dec
            << " rejected-sound-banks="
            << diagnostics.rejected_sound_bank_lookups << ":0x"
            << std::hex << std::uppercase
            << diagnostics.last_rejected_sound_bank << "/"
            << diagnostics.last_rejected_sound_bank_table << "/"
            << diagnostics.last_rejected_sound_bank_caller << "/"
            << diagnostics.last_rejected_sound_bank_magic << std::dec << "/"
            << diagnostics.last_rejected_sound_bank_index << "/"
            << diagnostics.last_rejected_sound_bank_entry_count
            << ":first@" << first_rejected_sound_bank_frame << "/"
            << std::hex << std::uppercase
            << first_rejected_sound_bank_caller << "/"
            << first_rejected_sound_bank_magic << std::dec
            << " rejected-sound-voices="
            << diagnostics.rejected_sound_voice_updates << ":"
            << diagnostics.last_rejected_sound_voice << "/0x"
            << std::hex << std::uppercase
            << diagnostics.last_rejected_sound_voice_caller << std::dec
            << " room-textures="
            << diagnostics.room_texture_activations << "/"
            << diagnostics.room_texture_page_requests << "/"
            << diagnostics.room_texture_upload_completions << "/"
            << diagnostics.retail_load_image_calls << "/"
            << diagnostics.retained_retail_load_images << ":"
            << diagnostics.last_room_texture_activation << "/"
            << diagnostics.last_room_texture_page << "/"
            << diagnostics.last_room_texture_bank
            << " loadimage-sites=";
  for (const auto count : diagnostics.retail_load_image_call_sites) {
    std::cout << count << "/";
  }
  std::cout << diagnostics.unknown_retail_load_image_call_sites
            << ":0x" << std::hex << std::uppercase
            << diagnostics.last_retail_load_image_caller << std::dec
            << "/" << diagnostics.last_retail_load_image_transfer.x
            << "," << diagnostics.last_retail_load_image_transfer.y
            << "," << diagnostics.last_retail_load_image_transfer.width
            << "," << diagnostics.last_retail_load_image_transfer.height
            << ":mask=0x" << std::hex << std::uppercase
            << diagnostics.retained_retail_texture_page_mask << std::dec
            << "/halfwords="
            << diagnostics.retained_retail_upload_halfwords
            << "/fb="
            << diagnostics.retained_retail_framebuffer_rectangles
            << "/full="
            << diagnostics.retained_retail_fullscreen_rectangles
            << "/clut="
            << diagnostics.retained_retail_clut_rectangles << ":";
  for (const auto packed :
       diagnostics.retained_retail_clut_transfers) {
    if (packed == 0U) {
      continue;
    }
    std::cout << static_cast<std::uint16_t>(packed) << ","
              << static_cast<std::uint16_t>(packed >> 16U) << ","
              << static_cast<std::uint16_t>(packed >> 32U) << ","
              << static_cast<std::uint16_t>(packed >> 48U) << "/";
  }
  std::cout << " setup-packets="
            << diagnostics.retained_vram_setup_packets
            << "/menu-vram=" << diagnostics.menu_vram_snapshots << "/"
            << diagnostics.menu_vram_restores;
  std::cout
            << " checkpoint-restores="
            << diagnostics.checkpoint_restores
            << " first-restore-frame=" << first_restore_frame
            << " min-health=" << minimum_player_health
            << " first-sprite-frame=" << first_sprite_frame
            << " max-sprites=" << maximum_sprite_commands
            << " last-restore=0x" << std::hex << std::uppercase
            << diagnostics.last_restore_caller << std::dec << ":"
            << diagnostics.pre_restore_player_x << "/"
            << diagnostics.pre_restore_player_y << "/"
            << diagnostics.pre_restore_player_z << ":"
            << diagnostics.pre_restore_player_health
            << " damage-events=" << diagnostics.damage_events
            << " last-damage=0x" << std::hex << std::uppercase
            << diagnostics.last_damage_caller << ":"
            << diagnostics.last_damage_arguments[0U] << "/"
            << diagnostics.last_damage_arguments[1U] << "/"
            << diagnostics.last_damage_arguments[2U] << "/"
            << diagnostics.last_damage_arguments[3U] << std::dec
            << " player-damage-events=" << diagnostics.player_damage_events
            << " last-player-damage=0x" << std::hex << std::uppercase
            << diagnostics.last_player_damage_caller << ":";
  for (const auto word : diagnostics.last_player_damage_request) {
    std::cout << word << "/";
  }
  std::cout << std::dec
            << " collision=" << diagnostics.world_collision_scans << ":0x"
            << std::hex << std::uppercase
            << diagnostics.last_world_collision_caller << std::dec << ":"
            << diagnostics.last_world_collision_object << "/"
            << diagnostics.last_world_collision_room
            << " floor=" << diagnostics.player_floor_probes << ":"
            << diagnostics.player_floor_probe_true << "/"
            << diagnostics.player_floor_probe_false << ":"
            << diagnostics.player_floor_false_streak << "/"
            << diagnostics.maximum_player_floor_false_streak
            << " collision-gaps="
            << collision_residency_gap_frames << "/"
            << first_collision_residency_gap_frame << " rooms=";
  for (const auto &[frame, room] : guest_room_transitions) {
    std::cout << frame << ":" << room << "/";
  }
  std::cout
            << " audio=" << diagnostics.spu_mixed_frames << ":"
            << diagnostics.active_spu_voices << "/max:"
            << maximum_active_spu_voices << "/"
            << "keys:" << diagnostics.spu_key_on_writes << "/"
            << diagnostics.spu_key_off_writes << ":0x" << std::hex
            << diagnostics.spu_last_key_on_mask << "/"
            << diagnostics.spu_last_key_off_mask << std::dec << "/"
            << diagnostics.spu_control << "/" << diagnostics.spu_status << "/"
            << diagnostics.spu_cd_frames << "/"
            << static_cast<unsigned int>(diagnostics.cd_muted) << "/"
            << static_cast<unsigned int>(diagnostics.cd_adpcm_muted) << ":"
            << diagnostics.cd_lba << "/"
            << static_cast<unsigned int>(diagnostics.cd_reading) << "/"
            << static_cast<unsigned int>(diagnostics.cd_interrupt_flags) << "/"
            << static_cast<unsigned int>(diagnostics.cd_pending_command) << "/"
            << static_cast<unsigned int>(diagnostics.cd_command_phase) << "/"
            << static_cast<unsigned int>(diagnostics.cd_data_valid) << "/"
            << static_cast<unsigned int>(
                   diagnostics.cd_sector_event_pending)
            << "/"
            << static_cast<unsigned int>(diagnostics.xa_stream_set) << "/"
            << static_cast<unsigned int>(diagnostics.xa_file) << "/"
            << static_cast<unsigned int>(diagnostics.xa_channel)
            << " first-xa-frame=" << first_xa_frame
            << " max-cd-lba=" << maximum_cd_lba
            << "/" << maximum_relative_cd_lba
            << " scripts=" << diagnostics.script_archive_loads << ":"
            << diagnostics.script_program_count << "/"
            << diagnostics.script_level_starts << "/"
            << diagnostics.script_dispatches << "/"
            << diagnostics.script_event5_dispatches << "/"
            << diagnostics.script_program_dispatches << "/"
            << diagnostics.script_activations << "/0x" << std::hex
            << diagnostics.last_script_activation_program << std::dec
            << "/timers="
            << static_cast<unsigned int>(
                   diagnostics.active_script_timer_count) << ":";
  for (auto index = std::size_t{};
       index < diagnostics.active_script_timer_count; ++index) {
    const auto &timer = diagnostics.active_script_timers[index];
    std::cout << "0x" << std::hex << timer.program << "/"
              << timer.program_name_words[0U] << "/"
              << timer.program_name_words[1U] << std::dec << "/"
              << timer.timer_index << "=" << timer.remaining_ticks << ",";
  }
  std::cout << "/hud=" << diagnostics.mission_timer_visible << "/"
            << diagnostics.mission_timer_ticks << "/"
            << diagnostics.mission_timer_handle << "/"
            << diagnostics.mission_timer_text.data() << "/updates="
            << diagnostics.mission_timer_text_updates
            << " level=0x" << std::hex << std::uppercase
            << diagnostics.script_level_program << "/"
            << diagnostics.script_level_name_pointer << ":"
            << diagnostics.script_level_name_words[0U] << "/"
            << diagnostics.script_level_name_words[1U] << ":"
            << diagnostics.script_lookup_name_words[0U] << "/"
            << diagnostics.script_lookup_name_words[1U] << std::dec
            << " xa-calls=" << diagnostics.scene_xa_archive_opens << "/"
            << diagnostics.scene_speech_starts << "/"
            << diagnostics.scene_speech_callbacks << "/"
            << diagnostics.scene_speech_stops << ":"
            << static_cast<unsigned int>(diagnostics.scene_speech_stage) << "/"
            << static_cast<unsigned int>(diagnostics.scene_speech_io_ready)
            << ":" << diagnostics.spatial_sound_starts << "/"
            << diagnostics.scene_sound_cue_plays << ":callbacks="
            << std::hex << std::uppercase;
  for (const auto callback : diagnostics.interrupt_callbacks) {
    std::cout << callback << "/";
  }
  std::cout << ":"
            << std::hex << std::uppercase;
  for (const auto value : diagnostics.last_scene_speech_arguments) {
    std::cout << value << "/";
  }
  std::cout << ":";
  for (const auto value :
       diagnostics.last_scene_speech_callback_arguments) {
    std::cout << value << "/";
  }
  std::cout << ":";
  for (const auto value : diagnostics.last_scene_speech_stop_arguments) {
    std::cout << value << "/";
  }
  std::cout << ":script=";
  for (const auto value : diagnostics.last_script_dispatch_arguments) {
    std::cout << value << "/";
  }
  std::cout << ":";
  for (const auto value : diagnostics.scene_speech_io_state) {
    std::cout << value << "/";
  }
  std::cout << ":";
  for (const auto value : diagnostics.xa_globals) {
    std::cout << value << "/";
  }
  std::cout << diagnostics.xa_status_source << "/"
            << diagnostics.xa_status_result << std::dec << "/"
            << diagnostics.xa_cue_plays << "/"
            << diagnostics.xa_stream_starts << "/"
            << diagnostics.xa_stream_stops
            << " completion-flow=" << diagnostics.campaign_advance_calls
            << "/" << diagnostics.movie_request_calls << "/"
            << diagnostics.movie_playback_init_calls << "/title="
            << diagnostics.title_transition_mode << "/"
            << diagnostics.title_substate << "/disc="
            << static_cast<unsigned int>(diagnostics.mounted_campaign_disc)
            << "/catalog="
            << diagnostics.selected_movie_catalog_index
            << "/next-carry=" << next_mission_carry_verified
            << "/movies=";
  for (const auto catalog_index : completion_movie_catalog_indices) {
    std::cout << static_cast<unsigned int>(catalog_index) << ",";
  }
  std::cout
            << ":request=0x"
            << std::hex << std::uppercase;
  for (const auto argument : diagnostics.last_movie_request_arguments) {
    std::cout << argument << "/";
  }
  std::cout << ":baseline=" << completion_baseline.campaign_advance_calls
            << "/" << completion_baseline.movie_request_calls << "/"
            << completion_baseline.movie_playback_init_calls << "/catalog="
            << completion_baseline.selected_movie_catalog_index;
  std::cout << ":playback=0x";
  for (const auto argument : diagnostics.last_movie_playback_arguments) {
    std::cout << argument << "/";
  }
  std::cout << ":playback-history=";
  for (const auto index : diagnostics.movie_playback_catalog_history) {
    std::cout << index << "/";
  }
  std::cout << ":selection-writes=" << std::dec
            << diagnostics.movie_selection_writes << ":0x" << std::hex
            << std::uppercase << diagnostics.last_movie_selection_writer_pc
            << "/" << diagnostics.last_movie_selection_writer_instruction
            << "/" << diagnostics.last_movie_selection_write_value
            << ":history=";
  for (auto index = std::size_t{};
       index < diagnostics.movie_selection_writer_pcs.size(); ++index) {
    std::cout << diagnostics.movie_selection_writer_pcs[index] << "@"
              << diagnostics.movie_selection_write_values[index] << "/";
  }
  std::cout << std::dec
            << " timeline=" << diagnostics.timeline_event_count << ":";
  const auto first_timeline_event =
      diagnostics.timeline_event_count >
              diagnostics.timeline_events.size()
          ? diagnostics.timeline_event_count -
                diagnostics.timeline_events.size()
          : 0U;
  for (auto serial = first_timeline_event;
       serial < diagnostics.timeline_event_count; ++serial) {
    const auto &event =
        diagnostics.timeline_events[serial %
                                    diagnostics.timeline_events.size()];
    std::cout << static_cast<unsigned int>(event.kind) << "@"
              << event.guest_frame << "." << event.system_clock << "("
              << std::hex << std::uppercase;
    for (const auto argument : event.arguments) {
      std::cout << argument << "/";
    }
    std::cout << std::dec << "),";
  }
  std::cout
            << " async-file=" << diagnostics.async_file_services << "/"
            << diagnostics.async_file_completions << ":0x"
            << std::hex
            << std::uppercase << diagnostics.last_async_completion_caller
            << std::dec
            << " sp=0x" << std::hex << std::uppercase
            << diagnostics.stack_pointer << "/"
            << diagnostics.minimum_stack_pointer << "/"
            << diagnostics.maximum_stack_pointer
            << " gp=0x" << diagnostics.global_pointer << std::dec
            << " gp0-sha256="
            << sf::core::toHex(sf::core::sha256(
                   std::as_bytes(std::span{presentation_words})));
  const auto first_draw = std::ranges::find_if(
      presentation->packets, [](const auto &packet) {
        return sf::game::sf2GpuCommandKind(packet) ==
               sf::game::Sf2GpuCommandKind::draw;
      });
  if (first_draw != presentation->packets.end()) {
    std::cout << " first-draw=";
    for (const auto word : first_draw->gp0_words) {
      std::cout << std::hex << std::uppercase << word << '/';
    }
    std::cout << std::dec;
  }
  std::array<std::size_t, 256U> opcode_counts{};
  for (const auto &packet : presentation->packets) {
    if (!packet.gp0_words.empty()) {
      ++opcode_counts[packet.gp0_words.front() >> 24U];
    }
  }
  std::cout << " opcodes=";
  for (std::size_t opcode = 0U; opcode < opcode_counts.size(); ++opcode) {
    if (opcode_counts[opcode] != 0U) {
      std::cout << std::hex << std::uppercase << opcode << std::dec << ':'
                << opcode_counts[opcode] << '/';
    }
  }
  std::cout << " copies=";
  for (const auto &packet : presentation->packets) {
    if (sf::game::sf2GpuCommandKind(packet) !=
        sf::game::Sf2GpuCommandKind::copy_vram) {
      continue;
    }
    for (const auto word : packet.gp0_words) {
      std::cout << std::hex << std::uppercase << word << '/';
    }
    std::cout << std::dec << ',';
  }
  std::cout << " display-envs=";
  for (std::size_t index = 0U; index < display_environments.size(); ++index) {
    std::cout << (index == 0U ? "" : ",");
    for (const auto word : display_environments[index]) {
      std::cout << std::hex << std::uppercase << word << '/';
    }
    std::cout << std::dec;
  }
  std::cout << '\n';
  const auto restore_stability_missing =
      first_restore_frame != 0U &&
      frames - first_restore_frame < 300U;
  if (frames >= 800U && !retail_completion_flow &&
      (nonzero_pcm_frames == 0U || peak_pcm_sample == 0U ||
       maximum_active_spu_voices == 0U ||
       diagnostics.spu_key_on_writes <= 2U ||
       diagnostics.player_instance == 0U ||
       runtime.inputSampleCount() == 0U ||
       restore_stability_missing)) {
    std::cerr << "SF2 playable-alpha gate failed: PCM, SPU voices, player, "
                 "PAD cadence, or post-checkpoint stability is missing\n";
    return 8;
  }
  if (quick_state &&
      (!quick_state_restored || !quick_state_replay_matched)) {
    std::cerr << "SF2 quick-state probe did not cross an exact deterministic "
                 "restore/replay\n";
    return 10;
  }
  if (objective_event && !objective_looted) {
    std::cerr << "SF2 objective-event probe did not loot the truck\n";
    return 10;
  }
  const auto known_actor_radar_route =
      objective_event || (combat && mission_index == 2U);
  if ((combat || objective_event) && frames >= 1'200U &&
      (!valid_radar_heading_seen ||
       (known_actor_radar_route && maximum_radar_actor_count == 0U))) {
    std::cerr << "SF2 radar probe did not observe actor poses and a valid "
                 "player heading\n";
    return 10;
  }
  if (weapon_cycle &&
      (weapon_pulses_queued < 12U || equipped_item_transitions.size() < 4U ||
       !shotgun_observed || shotgun_stress_frames < 40U)) {
    std::cerr << "SF2 weapon-cycle probe did not complete the retail "
                 "selection and shotgun crouch/back/fire stress interval\n";
    return 10;
  }
  if (pause_flow &&
      (diagnostics.menu_vram_snapshots != 1U ||
       diagnostics.menu_vram_restores != 1U ||
       !pause_gameplay_presentation_seen ||
       !pause_menu_presentation_seen ||
       !pause_restored_presentation_seen ||
       pause_menu_maximum_draw_commands == 0U ||
       pause_menu_digest == pause_gameplay_digest)) {
    std::cerr << "SF2 pause probe did not publish a distinct authored menu "
                 "and restore gameplay presentation/VRAM residency\n";
    return 10;
  }
  if (mission_complete_probe && !retail_completion_flow &&
      (!mission_completion_observed ||
       !campaign_carry_applied ||
       diagnostics.mission_success_events != 1U ||
       diagnostics.mission_failure_events != 0U ||
       !diagnostics.mission_complete_requested ||
       diagnostics.player_health != 123U ||
       diagnostics.player_armor != 321U ||
       diagnostics.player_equipped_item != 4U ||
       diagnostics.player_magazines[4U] != 17U ||
       diagnostics.player_reserves[4U] != 51U ||
       diagnostics.player_magazines[8U] != 6U ||
       diagnostics.player_reserves[8U] != 19U ||
       diagnostics.player_owned_items[1U] != (1U << 1U) ||
       diagnostics.player_magazines[33U] != 2U ||
       diagnostics.player_reserves[33U] != 9U)) {
    std::cerr << "SF2 completion probe did not preserve the retail "
                 "success-only terminal handoff\n";
    return 10;
  }
  if (retail_completion_flow &&
      (!mission_completion_observed || !mission_shell_resumed ||
       diagnostics.mission_success_events != 1U ||
       diagnostics.mission_failure_events != 0U ||
       (mission_index < 20U &&
        (diagnostics.campaign_advance_calls != 1U ||
         diagnostics.selected_mission_index != mission_index + 1U ||
         diagnostics.application_state != 4U ||
         diagnostics.title_transition_mode != 1U ||
         diagnostics.movie_playback_init_calls !=
             completion_baseline.movie_playback_init_calls +
                 completion_movie_catalog_indices.size() ||
         (next_mission_is_same_disc &&
          sf::game::campaignMissionsShareCarry(mission_index,
                                               mission_index + 1U) &&
          !next_mission_carry_verified))))) {
    std::cerr << "SF2 completion-flow probe did not cross the exact retail "
                 "campaign/TITLE success handoff\n";
    return 10;
  }
  return 0;
}

int probeLegacyCd(const char *cue_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Legacy CD probe requires Syphon Filter USA v1.1"};
  }

  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  const auto &legacy_image = mission.legacyImage();
  sf::game::LegacyGameplayVm vm{legacy_image.executable()};
  vm.bindSyphonFilterUsaV11BootstrapPlatformCalls();
  vm.bindSyphonFilterUsaV11VirtualCdCalls(legacy_image.createVirtualCd());
  const auto bootstrap = vm.bootstrapFirstMission();
  if (!bootstrap.completed()) {
    std::cerr << "Legacy CD bootstrap failed at phase "
              << static_cast<unsigned int>(bootstrap.phase) << '\n';
    return 3;
  }

  constexpr std::uint32_t overlay_address = 0x80146630U;
  constexpr std::uint32_t bootstrap_offset = 0x5e8U;
  constexpr std::size_t code_probe_size = 64U;
  const auto overlay =
      mission.archive().file(mission.definition().overlay_name);
  if (overlay.size() < bootstrap_offset + code_probe_size) {
    return 4;
  }
  std::array<std::byte, code_probe_size> guest_code{};
  if (!vm.runtime().copyBytes(overlay_address + bootstrap_offset, guest_code) ||
      !std::ranges::equal(guest_code,
                          overlay.subspan(bootstrap_offset, code_probe_size))) {
    std::cerr << "SUBWAY.OVL code differs after CD/DMA3 loading\n";
    return 5;
  }

  const auto &machine = vm.machine();
  const auto cdrom = machine.cdrom().captureState();
  const auto dma3_control = machine.dma().chcr(sf::psx::DmaChannel::cdrom);
  constexpr std::uint16_t cdrom_irq = 1U << 2U;
  if (machine.currentTick() == 0U || cdrom.mode != 0xa0U ||
      cdrom.current_lba == 0U || cdrom.interrupt_flags != 0U ||
      cdrom.command_event.pending != 0U || cdrom.sector_event.pending != 0U ||
      (machine.interrupts().status() & cdrom_irq) != 0U ||
      machine.dma().madr(sf::psx::DmaChannel::cdrom) == 0U ||
      machine.dma().bcr(sf::psx::DmaChannel::cdrom) != 0x00010200U ||
      (dma3_control & (1U << 24U)) != 0U ||
      !machine.validateState(machine.captureState())) {
    std::cerr << "Legacy CD/DMA3 did not reach a clean hardware boundary\n";
    return 6;
  }

  std::cout << "Legacy CD probe passed: SUBWAY.OVL via CD-ROM/DMA3, lba="
            << cdrom.current_lba << ", ticks=" << machine.currentTick() << '\n';
  return 0;
}

int probeLegacyLoop(const char *cue_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Legacy loop probe requires Syphon Filter USA v1.1"};
  }

  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  const auto &legacy_image = mission.legacyImage();
  sf::game::LegacyGameplayVm vm{legacy_image.executable()};
  vm.bindSyphonFilterUsaV11BootstrapPlatformCalls();
  vm.bindSyphonFilterUsaV11VirtualCdCalls(legacy_image.createVirtualCd());

  constexpr std::uint32_t native_gpu_boundary = 0x800e6e74U;
  constexpr std::uint64_t boot_budget = 20'000'000U;
  const auto first =
      vm.runCurrentPcUntilHostBoundary(native_gpu_boundary, boot_budget);
  if (!first.stoppedAtHostBoundary()) {
    std::cerr << "Continuous guest boot stopped: "
              << sf::psx::toString(first.execution.reason) << ", pc=0x"
              << std::hex << first.execution.pc << std::dec
              << ", instructions=" << first.execution.instructions
              << ", vector-call=0x" << std::hex << vm.runtime().state().gpr[9]
              << ", a0=0x" << vm.runtime().state().gpr[4] << ", a1=0x"
              << vm.runtime().state().gpr[5] << std::dec << '\n';
    return 3;
  }
  struct FrameCounters {
    std::uint32_t vblank{};
    std::uint32_t system_clock{};
    std::uint32_t gameplay_frame{};
  };
  const auto read_counters = [&vm](FrameCounters &counters) {
    return vm.runtime().read32(0x8010f378U, counters.vblank) &&
           vm.runtime().read32(0x801169a4U, counters.system_clock) &&
           vm.runtime().read32(0x80116a88U, counters.gameplay_frame);
  };

  const auto first_frame = vm.runtime().state();
  FrameCounters initial_counters{};
  if (!read_counters(initial_counters)) {
    return 4;
  }
  auto boundary_frame = first_frame;
  auto previous_tick = vm.machine().currentTick();
  auto total_instructions = first.execution.instructions;
  std::uint32_t boundary_count = 1U;
  constexpr std::uint32_t maximum_boundaries = 8U;
  for (; boundary_count < maximum_boundaries; ++boundary_count) {
    const auto submit = vm.resumeCurrentPc(1U);
    const auto after_submit = vm.runtime().state();
    if (submit.host_calls != 1U ||
        submit.execution.reason !=
            sf::psx::R3000StopReason::instruction_budget ||
        after_submit.pc != boundary_frame.gpr[31] ||
        after_submit.gpr[29] != boundary_frame.gpr[29]) {
      return 4;
    }

    vm.machine().pulseVBlank();
    if ((vm.machine().interrupts().status() & 1U) == 0U) {
      return 4;
    }
    const auto next =
        vm.runCurrentPcUntilHostBoundary(native_gpu_boundary, boot_budget);
    if (!next.stoppedAtHostBoundary()) {
      std::cerr << "Continuous guest loop stopped: "
                << sf::psx::toString(next.execution.reason) << ", pc=0x"
                << std::hex << next.execution.pc << std::dec
                << ", instructions=" << next.execution.instructions << '\n';
      return 5;
    }

    const auto next_tick = vm.machine().currentTick();
    const auto next_frame = vm.runtime().state();
    FrameCounters counters{};
    if (!read_counters(counters) || next_tick <= previous_tick ||
        next.execution.instructions == 0U ||
        next_frame.gpr[31] != first_frame.gpr[31]) {
      return 6;
    }
    total_instructions += next.execution.instructions;
    if (counters.vblank != initial_counters.vblank ||
        counters.system_clock != initial_counters.system_clock ||
        counters.gameplay_frame != initial_counters.gameplay_frame) {
      std::cout << "Legacy continuous loop passed: " << boundary_count + 1U
                << " native GPU boundaries, " << total_instructions
                << " guest instructions, vblank=" << initial_counters.vblank
                << "/" << counters.vblank
                << ", clock=" << initial_counters.system_clock << "/"
                << counters.system_clock
                << ", frame=" << initial_counters.gameplay_frame << "/"
                << counters.gameplay_frame << '\n';
      return 0;
    }
    boundary_frame = next_frame;
    previous_tick = next_tick;
  }

  std::cerr << "Continuous loop reached " << boundary_count
            << " GPU boundaries without advancing guest frame counters\n";
  return 6;
}

int probeLegacyBootstrap(const char *cue_path) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Legacy bootstrap probe requires Syphon Filter USA v1.1"};
  }
  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  const auto &legacy_image = mission.legacyImage();
  auto virtual_cd = legacy_image.createVirtualCd();
  sf::game::LegacyGameplayVm vm{legacy_image.executable()};
  vm.bindSyphonFilterUsaV11BootstrapPlatformCalls();
  vm.bindSyphonFilterUsaV11VirtualCdCalls(std::move(virtual_cd));

  const auto report_phase =
      [&vm](std::string_view phase,
            const sf::game::LegacyGameplayVmResult &result) {
        std::cout << "legacy-bootstrap-" << phase << ": "
                  << sf::psx::toString(result.execution.reason)
                  << ", instructions=" << result.execution.instructions
                  << ", host-calls=" << result.host_calls << ", pc=0x"
                  << std::hex << std::uppercase << result.execution.pc
                  << ", instruction=0x" << result.execution.instruction
                  << ", ra=0x" << vm.runtime().state().gpr[31] << ", sp=0x"
                  << vm.runtime().state().gpr[29] << ", a0=0x"
                  << vm.runtime().state().gpr[4] << ", a1=0x"
                  << vm.runtime().state().gpr[5] << ", a2=0x"
                  << vm.runtime().state().gpr[6] << ", a3=0x"
                  << vm.runtime().state().gpr[7] << ", t1=0x"
                  << vm.runtime().state().gpr[9] << ", t2=0x"
                  << vm.runtime().state().gpr[10] << std::dec << '\n';
      };

  const auto bootstrap = vm.bootstrapFirstMission();
  report_phase("first-mission", bootstrap.execution);
  if (!bootstrap.completed()) {
    std::cout << "legacy-bootstrap-failed-phase="
              << static_cast<unsigned int>(bootstrap.phase)
              << ", bridge-fault=" << bootstrap.bridge_fault << '\n';
    return 11;
  }

  const auto activate_opening_cbdc = vm.invoke(0x8005fd04U, std::array{6U});
  if (!activate_opening_cbdc.completed()) {
    report_phase("activate-opening-cbdc", activate_opening_cbdc);
    return 12;
  }
  constexpr std::array mission_state_addresses{
      0x80115c78U, 0x80115c7cU, 0x80115c74U, 0x80102aa8U, 0x80116a88U,
      0x801169a4U, 0x801163b4U, 0x80116a20U, 0x80115cd4U, 0x80116958U,
      0x80115cccU, 0x80115d84U, 0x8015469cU, 0x80116af0U, 0x80116b9cU,
      0x80116ab0U, 0x801169d4U, 0x80130c8cU, 0x80116c68U, 0x8011775cU,
  };
  std::cout << "legacy-bootstrap-mission-state:";
  for (const auto address : mission_state_addresses) {
    std::uint32_t value{};
    if (!vm.runtime().read32(address, value)) {
      return 17;
    }
    std::cout << " [0x" << std::hex << std::uppercase << address << "]=0x"
              << value;
  }
  std::cout << std::dec << '\n';
  std::uint32_t pending_events{};
  if (!vm.runtime().read32(0x80116c68U, pending_events)) {
    return 17;
  }
  std::cout << "legacy-bootstrap-pending-events:";
  for (std::uint32_t index = 0U; index < pending_events; ++index) {
    const auto entry = 0x80116c6cU + index * 0x1cU;
    std::uint16_t event{};
    std::uint16_t priority{};
    std::uint32_t source{};
    std::uint32_t target{};
    if (!vm.runtime().read16(entry, event) ||
        !vm.runtime().read16(entry + 2U, priority) ||
        !vm.runtime().read32(entry + 4U, source) ||
        !vm.runtime().read32(entry + 8U, target)) {
      return 17;
    }
    std::cout << " [" << index << ":e" << event << ",p" << priority << ",s"
              << static_cast<std::int32_t>(source) << ",t"
              << static_cast<std::int32_t>(target) << ']';
  }
  std::cout << '\n';

  const auto report_bridge = [&vm](std::uint32_t frame) {
    const auto bridge = vm.readBridgeState();
    if (!bridge) {
      return false;
    }
    std::uint32_t state{};
    std::uint32_t state_depth{};
    static_cast<void>(vm.runtime().read32(0x80115c78U, state));
    static_cast<void>(vm.runtime().read32(0x80115c74U, state_depth));
    std::cout << "legacy-bridge-frame-" << frame << ": state=" << state << '/'
              << state_depth << ": eye=(" << bridge->camera.eye.x << ','
              << bridge->camera.eye.y << ',' << bridge->camera.eye.z
              << "), target=(" << bridge->camera.target.x << ','
              << bridge->camera.target.y << ',' << bridge->camera.target.z
              << "), projection=" << bridge->camera.projection
              << ", native-projection="
              << bridge->camera.projectionForDisplayWidth(384)
              << ", fov=" << bridge->camera.fov_raw
              << ", fade=" << bridge->fade.current << '/'
              << static_cast<unsigned int>(bridge->fade.floor)
              << " step=" << bridge->fade.step << " cb=0x" << std::hex
              << bridge->fade.callback << std::dec << ", opacity=" << std::fixed
              << std::setprecision(3) << bridge->fade.blackOpacity()
              << std::defaultfloat << ", objects=" << bridge->objects.size();
    constexpr std::array opening_slots{
        35U,  36U,  57U,  61U,  64U,  83U,  172U,
        173U, 184U, 350U, 351U, 352U, 353U, 354U,
    };
    for (const auto slot : opening_slots) {
      if (slot >= bridge->objects.size()) {
        continue;
      }
      const auto &object = bridge->objects[slot];
      std::uint32_t records{};
      std::uint32_t source{};
      std::uint32_t source_path{};
      std::uint32_t instance{};
      std::uint32_t actor_state{};
      static_cast<void>(vm.runtime().read32(0x80115cccU, records));
      static_cast<void>(
          vm.runtime().read32(records + slot * 0x4cU + 0x2cU, source));
      if (source != 0U) {
        static_cast<void>(vm.runtime().read32(source + 0x2cU, source_path));
      }
      static_cast<void>(
          vm.runtime().read32(records + slot * 0x4cU + 0x34U, instance));
      if (instance != 0U) {
        static_cast<void>(vm.runtime().read32(instance + 0x1cU, actor_state));
      }
      std::cout << " [" << slot << ":c" << object.class_id << ",r"
                << object.resident << ",hp" << object.health << ",a0x"
                << std::hex << object.attributes << std::dec << ",arg"
                << object.parameter << ",link" << object.linked_slot << ",src0x"
                << std::hex << source << ",path0x" << source_path << ",inst0x"
                << instance << ",root0x" << object.root_node << ",mot0x"
                << object.motion_controller << ",pres0x"
                << object.presentation_controller << ",ai0x" << actor_state
                << ",aif0x" << object.ai_flags << ",fire" << std::dec
                << static_cast<unsigned int>(object.ai_fire_latch) << std::hex
                << ",route0x" << object.path_pointer << ",node" << std::dec
                << static_cast<unsigned int>(object.ai_route_node) << ",rf0x"
                << std::hex << object.ai_route_flags << ",pose0x"
                << object.pose_flags << std::dec << ",pe"
                << static_cast<unsigned int>(object.presentation_enabled)
                << ",pm" << static_cast<unsigned int>(object.presentation_mode)
                << ",sim" << object.simulated << ",t" << object.target_slot
                << ",am" << static_cast<unsigned int>(object.ai_mode) << ",ac"
                << static_cast<unsigned int>(object.ai_combat_mode) << ",p("
                << object.position.x << ',' << object.position.y << ','
                << object.position.z << ")]";
    }
    if (frame == 0U || frame == 120U || frame == 193U || frame == 208U) {
      for (const auto &object : bridge->objects) {
        if (object.class_id != 0x35 || !object.resident) {
          continue;
        }
        std::cout << " [CHEMO" << object.slot << ":hp" << object.health
                  << ",a0x" << std::hex << object.attributes << std::dec
                  << ",arg" << object.parameter << ",link" << object.linked_slot
                  << ",p(" << object.position.x << ',' << object.position.y
                  << ',' << object.position.z << ")]";
      }
    }
    std::cout << '\n';
    return true;
  };
  constexpr std::array report_frames{
      0U,   1U,   10U,  30U,  60U,  120U, 193U, 199U, 200U,
      201U, 202U, 203U, 204U, 205U, 206U, 207U, 208U, 209U,
      210U, 211U, 212U, 220U, 250U, 300U, 400U, 499U,
  };
  constexpr std::uint32_t retail_opening_updates = 209U;
  constexpr std::uint32_t retail_followup_updates = 180U;
  // GameplaySession owns the already-rendered direct frame 0 snapshot, then
  // advances the guest once per retail 20 Hz simulation update.
  constexpr std::uint32_t matching_direct_frame = retail_opening_updates;
  constexpr std::uint32_t matching_followup_direct_frame =
      matching_direct_frame + retail_followup_updates;
  std::optional<sf::game::LegacyGameplayBridgeState> matching_guest_bridge;
  std::optional<sf::game::LegacyGameplayBridgeState>
      matching_followup_guest_bridge;
  std::optional<sf::game::LegacyObjectBridgeState> matching_guest_vehicle;
  bool native_driven{};
  auto previous_current_state = std::numeric_limits<std::uint32_t>::max();
  auto previous_next_state = std::numeric_limits<std::uint32_t>::max();
  for (std::uint32_t frame = 0U; frame < 500U; ++frame) {
    if (!vm.writeHostPadState(sf::game::LegacyHostPadState{})) {
      return 24;
    }
    std::uint32_t current_state{};
    if (!vm.runtime().read32(0x80115c78U, current_state)) {
      return 24;
    }
    const auto gameplay_state = current_state == 0U || current_state == 5U;
    const auto retail_frame = native_driven && gameplay_state
                                  ? vm.tickNativeDrivenGameplayFrame()
                                  : vm.tickRetailOuterFrame();
    if (!retail_frame.completed()) {
      std::uint32_t next_state{};
      static_cast<void>(vm.runtime().read32(0x80115c7cU, next_state));
      std::cout << "legacy-bootstrap-outer-state: before="
                << retail_frame.state_before
                << ", after=" << retail_frame.state_after
                << ", next=" << next_state
                << ", bridge-fault=" << retail_frame.bridge_fault
                << ", unsupported=" << retail_frame.unsupported_state << '\n';
      if (!retail_frame.guest_calls.empty()) {
        report_phase("outer-frame", retail_frame.guest_calls.back());
      }
      if (retail_frame.renderer_tail) {
        report_phase("retail-render-tail", *retail_frame.renderer_tail);
      }
      report_phase("retail-tail-callbacks",
                   retail_frame.platform_tail.delayed_callbacks);
      if (retail_frame.platform_tail.fade_callback) {
        report_phase("retail-tail-fade",
                     *retail_frame.platform_tail.fade_callback);
      }
      return 24;
    }
    std::uint32_t current_after{};
    std::uint32_t next_after{};
    std::uint32_t stack_depth{};
    std::uint32_t loader_callback{};
    std::uint32_t movie_callback{};
    std::uint32_t loader_ticks{};
    std::uint8_t overlay_ready{};
    std::uint8_t fade_ready{};
    if (!vm.runtime().read32(0x80115c78U, current_after) ||
        !vm.runtime().read32(0x80115c7cU, next_after) ||
        !vm.runtime().read32(0x80115c74U, stack_depth) ||
        !vm.runtime().read32(0x80116b04U, loader_callback) ||
        !vm.runtime().read32(0x80115c80U, movie_callback) ||
        !vm.runtime().read32(0x80116978U, loader_ticks) ||
        !vm.runtime().read8(0x801169f0U, overlay_ready) ||
        !vm.runtime().read8(0x80116940U, fade_ready)) {
      return 25;
    }
    if (current_after != previous_current_state ||
        next_after != previous_next_state) {
      std::array<std::uint32_t, 4U> state_stack{};
      std::array<std::uint32_t, 4U> state7_words{};
      std::array<char, 17U> loader_name{};
      for (std::size_t index = 0U; index < state_stack.size(); ++index) {
        if (!vm.runtime().read32(0x80102aa4U +
                                     static_cast<std::uint32_t>(index * 4U),
                                 state_stack[index]) ||
            !vm.runtime().read32(0x80145accU +
                                     static_cast<std::uint32_t>(index * 4U),
                                 state7_words[index])) {
          return 25;
        }
      }
      for (std::size_t index = 0U; index + 1U < loader_name.size(); ++index) {
        std::uint8_t character{};
        if (!vm.runtime().read8(0x80115ca8U + static_cast<std::uint32_t>(index),
                                character)) {
          return 25;
        }
        loader_name[index] = static_cast<char>(character);
        if (character == 0U) {
          break;
        }
      }
      std::cout << "legacy-state-frame-" << frame
                << ": call=" << retail_frame.state_before << '/'
                << retail_frame.state_after << ", live=" << current_after << '/'
                << next_after << ", depth=" << stack_depth
                << ", loader=" << loader_ticks << '/'
                << static_cast<unsigned int>(overlay_ready) << '/'
                << static_cast<unsigned int>(fade_ready) << ", callbacks=0x"
                << std::hex << loader_callback << "/0x" << movie_callback
                << ", stack=" << state_stack[0] << '/' << state_stack[1] << '/'
                << state_stack[2] << '/' << state_stack[3]
                << ", state7=" << state7_words[0] << '/' << state7_words[1]
                << '/' << state7_words[2] << '/' << state7_words[3] << std::dec
                << ", name=" << loader_name.data() << '\n';
      previous_current_state = current_after;
      previous_next_state = next_after;
    }
    if (std::ranges::find(report_frames, frame) != report_frames.end() &&
        !report_bridge(frame)) {
      return 26;
    }
    const auto bridge = vm.readBridgeState();
    if (!bridge || bridge->objects.size() <= 35U) {
      return 26;
    }
    if (frame == matching_direct_frame) {
      if (bridge->objects.size() <= 352U) {
        return 26;
      }
      matching_guest_bridge = *bridge;
      matching_guest_vehicle = bridge->objects[57U];
    }
    if (frame == matching_followup_direct_frame) {
      if (bridge->objects.size() <= 354U) {
        return 26;
      }
      matching_followup_guest_bridge = *bridge;
    }
    native_driven = native_driven || bridge->objects[35U].health <= 0;
  }
  if (!matching_guest_bridge || !matching_followup_guest_bridge ||
      !matching_guest_vehicle) {
    return 26;
  }

  sf::game::GameplaySession gameplay{mission};
  const auto initial_camera = gameplay.camera();
  if (initial_camera.x != 2372.0 || initial_camera.y != -3206.0 ||
      initial_camera.z != 5977.0 || gameplay.mapFade() != 0U) {
    std::cout << "native-opening-initial-bridge-mismatch: eye=("
              << initial_camera.x << ',' << initial_camera.y << ','
              << initial_camera.z
              << "), fade=" << static_cast<unsigned int>(gameplay.mapFade())
              << ", cinematic=" << gameplay.cinematic()
              << ", complete=" << gameplay.missionComplete() << '\n';
    return 26;
  }
  bool saw_both_cbdc_during_rail{};
  bool saw_both_hostiles_during_rail{};
  bool saw_crouched_cbdc_motion{};
  bool saw_guest_expl_particles{};
  bool retained_authored_glit_rotation{true};
  const auto &authored_glit_rotation =
      mission.objects().objects()[74U].transform.rotation;
  for (std::uint32_t update = 0U; update < retail_opening_updates; ++update) {
    gameplay.update(sf::game::GameplayInput{});
    gameplay.advanceAnimationClock();
    saw_guest_expl_particles =
        saw_guest_expl_particles ||
        std::ranges::any_of(gameplay.legacyExplParticles(),
                            [](const sf::game::LegacyExplParticle &particle) {
                              return particle.scale_byte != 0U &&
                                     particle.frame <= 7U &&
                                     (particle.red != 0U ||
                                      particle.green != 0U ||
                                      particle.blue != 0U);
                            });
    const auto glit = std::ranges::find_if(
        gameplay.objects(), [](const sf::game::SceneObject &object) {
          return object.source_index == 74U;
        });
    retained_authored_glit_rotation =
        retained_authored_glit_rotation && glit != gameplay.objects().end() &&
        glit->transform.rotation == authored_glit_rotation;
    if (!gameplay.cinematic()) {
      continue;
    }
    std::size_t cbdc_count{};
    std::size_t hostile_count{};
    for (std::uint16_t object = 0U; object < gameplay.objects().size();
         ++object) {
      const auto *state = gameplay.npcState(object);
      if (state == nullptr || state->scripted_opening_lane >= 2U) {
        continue;
      }
      if (state->scripted_intro_agent) {
        ++cbdc_count;
        saw_crouched_cbdc_motion =
            saw_crouched_cbdc_motion || state->scripted_low_locomotion;
      } else if (state->disposition == sf::game::NpcDisposition::hostile) {
        ++hostile_count;
      }
    }
    saw_both_cbdc_during_rail = saw_both_cbdc_during_rail || cbdc_count == 2U;
    saw_both_hostiles_during_rail =
        saw_both_hostiles_during_rail || hostile_count == 2U;
  }

  std::array<const sf::game::NpcState *, 2U> opening_hostiles{};
  std::array<const sf::game::NpcState *, 2U> opening_cbdc{};
  for (std::uint16_t object = 0U; object < gameplay.objects().size();
       ++object) {
    const auto *state = gameplay.npcState(object);
    if (state == nullptr ||
        state->scripted_opening_lane >= opening_hostiles.size()) {
      continue;
    }
    if (state->disposition == sf::game::NpcDisposition::hostile) {
      opening_hostiles[state->scripted_opening_lane] = state;
    } else if (state->scripted_intro_agent) {
      opening_cbdc[state->scripted_opening_lane] = state;
    }
  }
  const auto settled_vehicle = std::ranges::find_if(
      gameplay.objects(), [](const sf::game::SceneObject &object) {
        return object.source_index == 57U;
      });
  const auto vehicle_basis_matches_guest =
      matching_guest_vehicle->position.y !=
          std::numeric_limits<std::int32_t>::min() &&
      settled_vehicle != gameplay.objects().end() &&
      settled_vehicle->transform.x == matching_guest_vehicle->position.x &&
      settled_vehicle->transform.y == -matching_guest_vehicle->position.y &&
      settled_vehicle->transform.z == matching_guest_vehicle->position.z &&
      settled_vehicle->transform.rotation ==
          matching_guest_vehicle->guest_rotation;
  const auto actor_matches_guest =
      [&gameplay](const sf::game::NpcState *actor,
                  const sf::game::LegacyObjectBridgeState &guest) {
        const auto presented =
            guest.resident && (guest.presentation_controller == 0U ||
                               guest.presentation_enabled != 0U);
        if (!presented) {
          return actor == nullptr;
        }
        const auto exact_guest_pose =
            actor != nullptr && actor->object < gameplay.objects().size() &&
            guest.bone_matrix_count == sf::game::legacy_actor_bone_count &&
            gameplay.objects()[actor->object].legacy_hmd_bone_count ==
                sf::game::legacy_actor_bone_count;
        const auto legacy_pose_available =
            exact_guest_pose ||
            (actor != nullptr && actor->object < gameplay.objects().size() &&
             gameplay.objects()[actor->object].legacy_hmd_root_space);
        return actor != nullptr && actor->object < gameplay.objects().size() &&
               legacy_pose_available &&
               actor->health ==
                   static_cast<std::uint16_t>(std::max<int>(guest.health, 0)) &&
               actor->x == guest.position.x && actor->y == guest.position.y &&
               actor->z == guest.position.z;
      };
  // Static source 184 is lane 0. Retail allocates lane 1 in dynamic slot
  // 350 and the two CBDC actors in slots 352/351 respectively.
  const auto opening_actors_match_guest =
      actor_matches_guest(opening_hostiles[0],
                          matching_guest_bridge->objects[184U]) &&
      actor_matches_guest(opening_hostiles[1],
                          matching_guest_bridge->objects[350U]) &&
      actor_matches_guest(opening_cbdc[0],
                          matching_guest_bridge->objects[352U]) &&
      actor_matches_guest(opening_cbdc[1],
                          matching_guest_bridge->objects[351U]);
  if (gameplay.cinematic() || !saw_both_cbdc_during_rail ||
      !saw_both_hostiles_during_rail || !saw_crouched_cbdc_motion ||
      !saw_guest_expl_particles || !retained_authored_glit_rotation ||
      !vehicle_basis_matches_guest || !opening_actors_match_guest) {
    std::cout << "native-opening-handoff-mismatch: cinematic="
              << gameplay.cinematic()
              << ",rail-cbdc=" << saw_both_cbdc_during_rail
              << ",rail-hostiles=" << saw_both_hostiles_during_rail
              << ",crouch-motion=" << saw_crouched_cbdc_motion
              << ",guest-expl=" << saw_guest_expl_particles
              << ",glit-authored=" << retained_authored_glit_rotation
              << ",vehicle-basis=" << vehicle_basis_matches_guest
              << ",actors-match=" << opening_actors_match_guest;
    const auto report_actor = [](std::string_view name, const auto *actor) {
      std::cout << ", " << name << '=';
      if (actor == nullptr) {
        std::cout << "missing";
        return;
      }
      std::cout << std::setprecision(17) << '(' << actor->x << ',' << actor->y
                << ',' << actor->z << ')' << std::setprecision(6);
    };
    report_actor("hostile0", opening_hostiles[0]);
    report_actor("hostile1", opening_hostiles[1]);
    report_actor("cbdc0", opening_cbdc[0]);
    report_actor("cbdc1", opening_cbdc[1]);
    std::cout << '\n';
    return 27;
  }
  const auto static_184_active = std::ranges::any_of(
      gameplay.activeObjects(), [&gameplay](std::uint16_t object) {
        const auto *state = gameplay.npcState(object);
        return state != nullptr && state->source_index == 184U &&
               state->scripted_opening_lane == 0U;
      });
  std::cout << "native-opening-bridge: updates=" << retail_opening_updates
            << ", fade=" << static_cast<unsigned int>(gameplay.mapFade())
            << ", guest-expl=" << saw_guest_expl_particles
            << ", glit-authored=" << retained_authored_glit_rotation
            << ", vehicle-basis=" << vehicle_basis_matches_guest
            << ", actors-match=" << opening_actors_match_guest
            << ", static184=" << static_184_active;
  const auto report_actor = [](std::string_view name, const auto *actor) {
    std::cout << ", " << name << '=';
    if (actor == nullptr) {
      std::cout << "dead";
      return;
    }
    std::cout << '(' << actor->x << ',' << actor->y << ',' << actor->z << ')';
  };
  report_actor("hostile0", opening_hostiles[0]);
  report_actor("hostile1", opening_hostiles[1]);
  report_actor("cbdc0", opening_cbdc[0]);
  report_actor("cbdc1", opening_cbdc[1]);
  std::cout << '\n';
  const std::array followup_actor_objects{
      opening_hostiles[1]->object,
      opening_cbdc[0]->object,
      opening_cbdc[1]->object,
  };
  const std::array initial_shot_serials{
      opening_hostiles[1]->shot_serial,
      opening_cbdc[0]->shot_serial,
      opening_cbdc[1]->shot_serial,
  };
  const std::array initial_animation_ticks{
      opening_hostiles[1]->animation_tick,
      opening_cbdc[0]->animation_tick,
      opening_cbdc[1]->animation_tick,
  };
  std::array<bool, 3U> saw_followup_motion{};
  std::array<bool, 3U> saw_followup_animation{};
  std::array<bool, 3U> saw_followup_target{};
  std::array<bool, 3U> saw_followup_fire{};
  for (std::uint32_t update = 0U; update < retail_followup_updates; ++update) {
    gameplay.update(sf::game::GameplayInput{});
    gameplay.advanceAnimationClock();
    for (std::size_t actor = 0U; actor < followup_actor_objects.size();
         ++actor) {
      const auto *state = gameplay.npcState(followup_actor_objects[actor]);
      if (state == nullptr) {
        continue;
      }
      saw_followup_motion[actor] =
          saw_followup_motion[actor] || state->movement_distance > 1.0;
      saw_followup_animation[actor] =
          saw_followup_animation[actor] ||
          state->animation_tick != initial_animation_ticks[actor];
      saw_followup_target[actor] =
          saw_followup_target[actor] ||
          state->behavior == sf::game::NpcBehavior::attack;
      saw_followup_fire[actor] =
          saw_followup_fire[actor] ||
          state->shot_serial > initial_shot_serials[actor];
    }
  }
  const auto *followup_opening_hostile =
      gameplay.npcState(followup_actor_objects[0]);
  const auto *followup_cbdc0 = gameplay.npcState(followup_actor_objects[1]);
  const auto *followup_cbdc1 = gameplay.npcState(followup_actor_objects[2]);
  const auto &followup_guest = *matching_followup_guest_bridge;
  const auto presentation_matches_guest =
      [&actor_matches_guest](const sf::game::NpcState *actor,
                             const sf::game::LegacyObjectBridgeState &guest) {
        const auto expected_phase =
            guest.ai_fire_latch != 0U ? sf::game::NpcCombatPhase::burst
            : guest.has_target        ? sf::game::NpcCombatPhase::aim
                                      : sf::game::NpcCombatPhase::acquire;
        return actor_matches_guest(actor, guest) && actor != nullptr &&
               actor->behavior == sf::game::NpcBehavior::attack &&
               guest.has_target && actor->combat_phase == expected_phase &&
               actor->fire_animation_updates == guest.ai_fire_latch;
      };
  const sf::game::NpcState *followup_hostile{};
  for (std::uint16_t object = 0U; object < gameplay.objects().size();
       ++object) {
    const auto *actor = gameplay.npcState(object);
    if (actor == followup_opening_hostile ||
        !presentation_matches_guest(actor, followup_guest.objects[354U])) {
      continue;
    }
    followup_hostile = actor;
    break;
  }
  // At direct frame 389 the dead slot-350 opening lifetime and live slot 354
  // coexist. They must retain separate scene objects; slot 354 may not
  // overwrite the dedicated opening presentation.
  const auto opening_identity_preserved =
      actor_matches_guest(followup_opening_hostile,
                          followup_guest.objects[350U]) &&
      !actor_matches_guest(followup_opening_hostile,
                           followup_guest.objects[354U]);
  const auto followup_actors_match_guest =
      !followup_guest.objects[184U].alive() && opening_identity_preserved &&
      presentation_matches_guest(followup_hostile,
                                 followup_guest.objects[354U]) &&
      presentation_matches_guest(followup_cbdc0,
                                 followup_guest.objects[352U]) &&
      presentation_matches_guest(followup_cbdc1, followup_guest.objects[351U]);
  const auto all_saw = [](const std::array<bool, 3U> &values) {
    return std::ranges::all_of(values, std::identity{});
  };
  const auto followup_static_184_active = std::ranges::any_of(
      gameplay.activeObjects(), [&gameplay](std::uint16_t object) {
        const auto *state = gameplay.npcState(object);
        return state != nullptr && state->source_index == 184U &&
               state->scripted_opening_lane == 0U;
      });
  const auto followup_ok =
      followup_actors_match_guest && followup_static_184_active &&
      all_saw(saw_followup_motion) && all_saw(saw_followup_animation) &&
      all_saw(saw_followup_target) && all_saw(saw_followup_fire);
  std::cout << "native-opening-followup: updates=" << retail_followup_updates;
  report_actor("hostile0", static_cast<const sf::game::NpcState *>(nullptr));
  report_actor("hostile1", followup_hostile);
  report_actor("cbdc0", followup_cbdc0);
  report_actor("cbdc1", followup_cbdc1);
  std::cout << ", actors-match=" << followup_actors_match_guest
            << ", motion=" << all_saw(saw_followup_motion)
            << ", animation=" << all_saw(saw_followup_animation)
            << ", target=" << all_saw(saw_followup_target)
            << ", fire=" << all_saw(saw_followup_fire)
            << ", opening-identity=" << opening_identity_preserved
            << ", static184=" << followup_static_184_active << '\n';
  if (!followup_ok) {
    return 28;
  }
  return 0;
}

struct LegacyLevelActorIdentity {
  std::int16_t class_id{};
  std::uint32_t definition{};
  std::int32_t parameter{};
  sf::game::LegacyNativePoint authored_position;
  std::uint32_t path_pointer{};
};

struct LegacyLevelActorLifetime {
  LegacyLevelActorIdentity identity;
  std::uint32_t slot{};
  std::uint32_t generation{};
  std::uint32_t first_frame{};
  std::uint32_t last_frame{};
  std::int16_t start_health{};
  std::int16_t minimum_health{};
  std::int16_t end_health{};
  sf::game::LegacyNativePoint start_position;
  sf::game::LegacyNativePoint end_position;
  std::int16_t last_target{-1};
  std::uint64_t last_pose_fingerprint{};
  std::uint32_t target_changes{};
  std::uint32_t animation_changes{};
  std::uint32_t fire_frames{};
  std::uint32_t simulated_frames{};
  std::uint32_t exact_pose_frames{};
  std::uint32_t ground_frames{};
  std::uint32_t packed_ground_sentinel_frames{};
  std::uint32_t current_stagnant_combat_frames{};
  std::uint32_t longest_stagnant_combat_frames{};
  std::int64_t maximum_ground_delta{};
  bool saw_positive_health{};
  bool died{};
  bool retired{};
  bool saw_target{};
  bool moved{};
};

struct LegacyLevelSlotTrace {
  std::optional<std::size_t> lifetime;
  std::uint32_t generations{};
};

enum class LegacyLevelDriverStage : std::uint8_t {
  waiting_opening,
  failure_branch,
  clear_opening,
  trigger_256,
  passage_64,
  passage_65,
  trigger_257,
  intro_157,
  lock_140,
  bank_175,
  kravitch_174,
  radio_260,
  bomb_29,
  trigger_190,
  trigger_194,
  power_317,
  trigger_192,
  trigger_193,
  elevator_315,
  elevator_316,
  bomb_28,
  trigger_191,
  trigger_258,
  station_318,
  station_319,
  trigger_259,
  finale_30,
  complete,
};

std::string_view
legacyLevelDriverStageName(LegacyLevelDriverStage stage) noexcept {
  using enum LegacyLevelDriverStage;
  switch (stage) {
  case waiting_opening:
    return "waiting-opening";
  case failure_branch:
    return "failure-branch";
  case clear_opening:
    return "clear-opening";
  case trigger_256:
    return "trigger-256";
  case passage_64:
    return "passage-64";
  case passage_65:
    return "passage-65";
  case trigger_257:
    return "trigger-257";
  case intro_157:
    return "intro-157";
  case lock_140:
    return "lock-140";
  case bank_175:
    return "bank-175";
  case kravitch_174:
    return "kravitch-174";
  case radio_260:
    return "radio-260";
  case bomb_29:
    return "bomb-29";
  case trigger_190:
    return "trigger-190";
  case trigger_194:
    return "trigger-194";
  case power_317:
    return "power-317";
  case trigger_192:
    return "trigger-192";
  case trigger_193:
    return "trigger-193";
  case elevator_315:
    return "elevator-315";
  case elevator_316:
    return "elevator-316";
  case bomb_28:
    return "bomb-28";
  case trigger_191:
    return "trigger-191";
  case trigger_258:
    return "trigger-258";
  case station_318:
    return "station-318";
  case station_319:
    return "station-319";
  case trigger_259:
    return "trigger-259";
  case finale_30:
    return "finale-30";
  case complete:
    return "complete";
  }
  return "unknown";
}

bool sameLegacyPoint(const sf::game::LegacyNativePoint &lhs,
                     const sf::game::LegacyNativePoint &rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool sameLegacyActorIdentity(const LegacyLevelActorIdentity &lhs,
                             const LegacyLevelActorIdentity &rhs) noexcept {
  return lhs.class_id == rhs.class_id && lhs.definition == rhs.definition &&
         lhs.parameter == rhs.parameter &&
         sameLegacyPoint(lhs.authored_position, rhs.authored_position) &&
         lhs.path_pointer == rhs.path_pointer;
}

bool sameLegacyMissionState(
    const sf::game::LegacyMissionBridgeState &lhs,
    const sf::game::LegacyMissionBridgeState &rhs) noexcept {
  return lhs.player_slot == rhs.player_slot &&
         lhs.player_health == rhs.player_health &&
         lhs.player_armor == rhs.player_armor &&
         lhs.objective_count == rhs.objective_count &&
         lhs.parameter_count == rhs.parameter_count &&
         lhs.objective_texts == rhs.objective_texts &&
         lhs.parameter_texts == rhs.parameter_texts &&
         lhs.completed_objectives == rhs.completed_objectives &&
         lhs.failed_objectives == rhs.failed_objectives &&
         lhs.revealed_objectives == rhs.revealed_objectives &&
         lhs.notified_objectives == rhs.notified_objectives &&
         lhs.failed_parameters == rhs.failed_parameters &&
         lhs.parameter_mask == rhs.parameter_mask &&
         lhs.success == rhs.success && lhs.terminal == rhs.terminal &&
         lhs.failure == rhs.failure &&
         lhs.failure_transition == rhs.failure_transition;
}

std::uint64_t legacyPoseFingerprint(
    const sf::game::LegacyObjectBridgeState &object) noexcept {
  auto fingerprint = std::uint64_t{1469598103934665603ULL};
  const auto mix = [&fingerprint](std::uint64_t value) {
    fingerprint ^= value;
    fingerprint *= 1099511628211ULL;
  };
  mix(object.pose_flags);
  mix(object.presentation_enabled);
  mix(object.presentation_mode);
  mix(object.bone_matrix_count);
  for (std::size_t bone = 0U; bone < object.bone_matrix_count; ++bone) {
    const auto &matrix = object.bone_matrices[bone];
    for (const auto value : matrix.rotation) {
      mix(static_cast<std::uint16_t>(value));
    }
    mix(static_cast<std::uint32_t>(matrix.translation.x));
    mix(static_cast<std::uint32_t>(matrix.translation.y));
    mix(static_cast<std::uint32_t>(matrix.translation.z));
  }
  return fingerprint;
}

bool legacyActorAllocated(const sf::game::LegacyObjectBridgeState &object,
                          std::uint16_t dynamic_first_slot) noexcept {
  const auto actor_class =
      object.class_id == 0 || object.class_id == 1 || object.class_id == 0x35;
  if (!actor_class || !object.resident) {
    return false;
  }
  if (object.slot < dynamic_first_slot) {
    return true;
  }
  return object.maximum_health != 0 || object.health != 0 ||
         object.attributes != 0U || object.parameter != 0 ||
         object.path_pointer != 0U || object.authored_position.x != 0 ||
         object.authored_position.y != 0 || object.authored_position.z != 0;
}

int probeLegacyLevel(const char *cue_path, std::uint32_t frame_count) {
  auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Legacy level probe requires Syphon Filter USA v1.1"};
  }

  const auto mission = sf::game::MissionPackage::loadFirst(disc);
  const auto &legacy_image = mission.legacyImage();
  auto virtual_cd = legacy_image.createVirtualCd();
  sf::game::LegacyGameplayVm vm{legacy_image.executable()};
  vm.bindSyphonFilterUsaV11BootstrapPlatformCalls();
  vm.bindSyphonFilterUsaV11VirtualCdCalls(std::move(virtual_cd));
  vm.bindHostCall(0x800ddc34U, [](sf::game::LegacyHostCallContext &context) {
    std::cout << "legacy-assert: ra=0x" << std::hex << std::uppercase
              << context.returnAddress();
    for (std::size_t index = 0U; index < 4U; ++index) {
      const auto argument = context.argument(index);
      std::cout << ", a" << index << "=0x" << argument;
      std::string text;
      if (context.readCString(argument, text, 128U)) {
        std::cout << "(\"" << text << "\")";
      }
    }
    std::cout << std::dec << '\n';
    context.rejectHostCall();
  });

  const auto bootstrap = vm.bootstrapFirstMission();
  if (!bootstrap.completed()) {
    std::cout << "legacy-level-bootstrap-fault: phase="
              << static_cast<unsigned int>(bootstrap.phase) << ", reason="
              << sf::psx::toString(bootstrap.execution.execution.reason)
              << ", pc=0x" << std::hex << std::uppercase
              << bootstrap.execution.execution.pc << std::dec
              << ", bridge=" << bootstrap.bridge_fault << '\n';
    return 30;
  }
  const auto activate_opening_cbdc = vm.invoke(0x8005fd04U, std::array{6U});
  if (!activate_opening_cbdc.completed()) {
    std::cout << "legacy-level-opening-activation-fault: reason="
              << sf::psx::toString(activate_opening_cbdc.execution.reason)
              << ", pc=0x" << std::hex << std::uppercase
              << activate_opening_cbdc.execution.pc << std::dec << '\n';
    return 30;
  }

  std::vector<LegacyLevelActorLifetime> lifetimes;
  std::vector<LegacyLevelSlotTrace> slots;
  std::optional<sf::game::LegacyMissionBridgeState> first_mission;
  std::optional<sf::game::LegacyMissionBridgeState> previous_mission;
  std::optional<sf::game::LegacyMissionBridgeState> last_mission;
  std::optional<sf::game::LegacyNativePoint> post_opening_player_origin;
  std::optional<std::uint32_t> opening_complete_frame;
  std::optional<std::uint32_t> last_checkpoint_frame;
  std::uint16_t dynamic_first_slot{};
  std::uint32_t completed_frames{};
  std::uint32_t outer_updates{};
  std::uint32_t native_updates{};
  std::uint32_t mission_transitions{};
  std::uint32_t checkpoints{};
  std::uint32_t maximum_pending_events{};
  std::uint32_t maximum_ready_events{};
  std::uint32_t invalid_targets{};
  bool native_driven{};
  bool post_opening_player_moved{};
  std::optional<sf::game::LegacyGameplayBridgeState> driver_bridge;
  std::optional<sf::game::LegacyMissionBridgeState> driver_mission;
  std::optional<sf::game::LegacyNativePoint> previous_applied_host_position;
  std::optional<std::string> driver_first_blocker;
  std::optional<std::uint32_t> previous_application_state;
  std::optional<std::uint32_t> previous_camera_controller;
  std::optional<std::uint32_t> previous_camera_mode;
  std::optional<std::uint32_t> previous_camera_lock;
  std::optional<sf::game::LegacyNativePoint> previous_camera_eye;
  std::vector<std::uint64_t> damaged_actor_generations;
  LegacyLevelDriverStage driver_stage{LegacyLevelDriverStage::waiting_opening};
  std::uint32_t driver_stage_frames{};
  std::uint32_t driver_stage_guest_updates{};
  std::uint32_t driver_stage_entry_trace_frame{};
  std::uint32_t bank_reinforcement_kills{};
  std::uint32_t bank_roots_materialized{};
  std::uint32_t bank_reinforcement_goal{};
  std::uint32_t bank_quiescent_updates{};
  std::uint32_t bank_last_root_idle_updates{};
  std::uint32_t power_post_transition_updates{};
  std::array<std::uint32_t, 5U> bank_descriptor_paths{};
  std::optional<std::uint16_t> bank_materialized_slot;
  std::optional<std::uint64_t> bank_materialized_generation;
  std::optional<sf::game::LegacyNativePoint> bank_route_hold_position;
  std::uint32_t bank_route_next_sync_update{1U};
  bool bank_activation_volume_entered{};
  bool bank_source_fallback_attempted{};
  bool bank_descriptor_fallback_attempted{};
  bool bomb_29_callback_attempted{};
  bool bomb_29_callback_completed{};
  bool bomb_28_callback_attempted{};
  bool bomb_28_callback_completed{};
  bool non_gameplay_transition_traced{};
  bool power_scripted_transition_completed{};
  std::uint32_t camera_controller_changes{};
  std::uint32_t camera_discontinuities{};
  std::uint32_t scripted_camera_rail_frames{};
  std::uint32_t player_position_overrides{};
  std::uint32_t packed_ground_sentinel_samples{};
  std::uint32_t raw_ground_dumps{};
  bool raw_ground_sentinel_dumped{};
  std::array<bool, 9U> trigger_visited{};
  std::array<bool, 9U> trigger_observed{};
  std::array<bool, 10U> trigger_fallback_attempted{};
  std::array<bool, 10U> trigger_fallback_completed{};
  bool gate_event14_fallback_attempted{};
  bool gate_event14_fallback_completed{};
  bool gate_handler14_fallback_completed{};
  bool bank_descriptor_completed{};
  bool failure_branch_checked{};
  bool failure_branch_passed{};
  bool intro_state9_seen{};
  bool intro_state9_returned{};
  bool scripted_camera_rail_seen{};
  bool elevator_315_event14_attempted{};
  bool elevator_315_event14_completed{};
  bool elevator_315_motion_started{};
  bool elevator_315_motion_completed{};
  std::uint32_t elevator_passenger_boarding_updates{};
  std::optional<std::int32_t> elevator_passenger_board_y;
  bool elevator_passenger_positioned{};
  bool elevator_return_started{};
  bool elevator_return_completed{};
  bool elevator_passenger_carried{};
  bool elevator_316_event14_attempted{};
  bool elevator_316_event14_completed{};
  bool elevator_316_motion_started{};
  bool elevator_316_motion_completed{};
  bool station_318_event14_attempted{};
  bool station_318_event14_completed{};
  bool station_318_handler_fallback_attempted{};
  bool station_318_scripted_transition_completed{};
  bool station_318_motion_started{};
  bool station_318_motion_completed{};
  bool station_319_event14_attempted{};
  bool station_319_event14_completed{};
  bool station_319_handler_fallback_attempted{};
  bool station_319_motion_started{};
  bool station_319_motion_completed{};
  bool finale_callback_attempted{};
  bool finale_callback_completed{};
  bool finale_state9_seen{};
  bool finale_state9_returned{};
  bool scripted_route_complete{};
  bool driver_failed{};

  const auto source_host_position = [&mission](std::uint16_t source) {
    const auto &transform = mission.objects().objects()[source].transform;
    return sf::game::LegacyNativePoint{
        transform.x,
        -transform.y,
        transform.z,
    };
  };
  const auto source_room = [&mission](std::uint16_t source) {
    // Static event volumes are authored in EMD room records rather than
    // the mission object's room list.
    switch (source) {
    case 157U:
      return std::uint16_t{80U};
    case 190U:
      return std::uint16_t{21U};
    case 191U:
      return std::uint16_t{42U};
    case 192U:
      return std::uint16_t{40U};
    case 193U:
      return std::uint16_t{0U};
    case 194U:
      return std::uint16_t{24U};
    case 257U:
      return std::uint16_t{68U};
    case 258U:
      return std::uint16_t{9U};
    case 259U:
      return std::uint16_t{44U};
    default:
      break;
    }
    for (std::size_t room = 0U; room < mission.objects().roomCount(); ++room) {
      if (std::ranges::find(mission.objects().objectsInRoom(room), source) !=
          mission.objects().objectsInRoom(room).end()) {
        return static_cast<std::uint16_t>(room);
      }
    }
    const auto &source_transform =
        mission.objects().objects()[source].transform;
    auto nearest_room = std::uint16_t{};
    auto nearest_distance = std::numeric_limits<std::int64_t>::max();
    for (std::size_t room = 0U; room < mission.objects().roomCount(); ++room) {
      for (const auto candidate : mission.objects().objectsInRoom(room)) {
        const auto &transform =
            mission.objects().objects()[candidate].transform;
        const auto dx =
            static_cast<std::int64_t>(transform.x) - source_transform.x;
        const auto dy =
            static_cast<std::int64_t>(transform.y) - source_transform.y;
        const auto dz =
            static_cast<std::int64_t>(transform.z) - source_transform.z;
        const auto distance = dx * dx + dy * dy + dz * dz;
        if (distance < nearest_distance) {
          nearest_distance = distance;
          nearest_room = static_cast<std::uint16_t>(room);
        }
      }
    }
    return nearest_room;
  };
  const auto object_matches_source =
      [&](const sf::game::LegacyObjectBridgeState &object,
          std::uint16_t source) {
        if (object.slot == source) {
          return true;
        }
        return sameLegacyPoint(object.authored_position,
                               source_host_position(source));
      };
  const auto record_driver_blocker = [&](std::string blocker) {
    driver_failed = true;
    if (!driver_first_blocker) {
      std::cout << "driver-blocker: stage="
                << legacyLevelDriverStageName(driver_stage)
                << ", reason=" << blocker << '\n';
      driver_first_blocker = std::move(blocker);
    }
  };
  const auto enter_driver_stage = [&](LegacyLevelDriverStage stage,
                                      std::uint32_t frame) {
    driver_stage = stage;
    driver_stage_frames = 0U;
    driver_stage_guest_updates = 0U;
    driver_stage_entry_trace_frame = frame;
    previous_applied_host_position.reset();
    std::cout << "driver-stage: frame=" << frame
              << ", stage=" << legacyLevelDriverStageName(stage) << '\n';
  };
  const auto dispatch_trigger_fallback = [&](std::size_t index,
                                             std::uint16_t source) {
    if (trigger_fallback_attempted[index]) {
      return trigger_fallback_completed[index];
    }
    trigger_fallback_attempted[index] = true;
    const auto event =
        vm.queueHostInteraction(static_cast<std::int16_t>(source));
    trigger_fallback_completed[index] = event.completed();
    std::cout << "trigger-event12-fallback: stage="
              << legacyLevelDriverStageName(driver_stage)
              << ", source=" << source << ", completed=" << event.completed()
              << ", reason=" << sf::psx::toString(event.execution.reason)
              << ", pc=0x" << std::hex << std::uppercase << event.execution.pc
              << std::dec << '\n';
    if (!event.completed()) {
      record_driver_blocker("source-" + std::to_string(source) +
                            "-event12-dispatch-fault");
    }
    return event.completed();
  };
  const auto dispatch_direct_object_event_from = [&](std::uint16_t destination,
                                                     std::uint16_t event_id,
                                                     std::uint16_t source) {
    constexpr std::uint32_t event_address = 0x80117e60U;
    constexpr std::uint32_t object_handler_table = 0x801028a4U;
    const auto bridge = vm.readBridgeState();
    if (!bridge || destination >= bridge->objects.size() ||
        bridge->objects[destination].class_id < 0) {
      return false;
    }
    auto event_written = true;
    for (std::uint32_t offset = 0U; offset < 0x1cU; offset += 4U) {
      event_written =
          event_written && vm.runtime().write32(event_address + offset, 0U);
    }
    event_written = event_written &&
                    vm.runtime().write16(event_address, event_id) &&
                    vm.runtime().write32(event_address + 4U, source) &&
                    vm.runtime().write32(event_address + 8U, destination);
    std::uint32_t handler_entry{};
    event_written =
        event_written &&
        vm.runtime().read32(object_handler_table +
                                static_cast<std::uint32_t>(
                                    bridge->objects[destination].class_id) *
                                    4U,
                            handler_entry);
    std::optional<sf::game::LegacyGameplayVmResult> handler;
    if (event_written && handler_entry != 0U) {
      handler = vm.invoke(handler_entry, std::array{event_address});
    }
    const auto completed = handler && handler->completed();
    std::cout << "probe-direct-object-event: stage="
              << legacyLevelDriverStageName(driver_stage)
              << ", source=" << source << ", destination=" << destination
              << ", event=0x" << std::hex << std::uppercase << event_id
              << ", handler=0x" << handler_entry << std::dec
              << ", written=" << event_written << ", completed=" << completed;
    if (handler) {
      std::cout << ", reason=" << sf::psx::toString(handler->execution.reason)
                << ", pc=0x" << std::hex << std::uppercase
                << handler->execution.pc << std::dec;
    }
    std::cout << '\n';
    return completed;
  };
  const auto dispatch_direct_object_event =
      [&dispatch_direct_object_event_from](std::uint16_t destination,
                                           std::uint16_t event_id) {
        return dispatch_direct_object_event_from(destination, event_id,
                                                 destination);
      };
  const auto dispatch_load_fallback = [&](std::uint16_t source) {
    constexpr std::uint32_t event_entry = 0x80015364U;
    const std::array arguments{
        0x0aU,
        2U,
        static_cast<std::uint32_t>(source),
        static_cast<std::uint32_t>(source),
        0U,
        0U,
        0U,
        0U,
    };
    const auto event = vm.invoke(event_entry, arguments, 5'000'000U);
    std::cout << "probe-event0a-continuation: stage="
              << legacyLevelDriverStageName(driver_stage)
              << ", source=" << source << ", completed=" << event.completed()
              << ", reason=" << sf::psx::toString(event.execution.reason)
              << ", pc=0x" << std::hex << std::uppercase << event.execution.pc
              << std::dec << '\n';
    if (!event.completed()) {
      record_driver_blocker("source-" + std::to_string(source) +
                            "-event0a-continuation-fault");
    }
    return event.completed();
  };
  const auto materialize_descriptor_actor = [&](std::uint8_t descriptor,
                                                std::uint16_t slot,
                                                std::uint8_t root,
                                                std::uint8_t type) {
    std::uint32_t descriptor_table{};
    const auto descriptor_read =
        vm.runtime().read32(0x80116adcU, descriptor_table);
    const auto descriptor_address =
        descriptor_table + static_cast<std::uint32_t>(descriptor) * 16U;
    const auto packed = (static_cast<std::uint32_t>(type) << 24U) |
                        (static_cast<std::uint32_t>(root) << 16U) |
                        (static_cast<std::uint32_t>(descriptor) << 8U) | 0xe0U;
    std::optional<sf::game::LegacyGameplayVmResult> materialization;
    if (descriptor_read) {
      materialization = vm.invoke(0x800663a8U,
                                  std::array{
                                      static_cast<std::uint32_t>(slot),
                                      0U,
                                      packed,
                                      0U,
                                  },
                                  5'000'000U);
    }
    const auto materialized = materialization && materialization->completed();
    // The retail visibility path follows FUN800663a8 with class-1 event 6.
    // FUN80061874 then calls FUN800659b8, which enables NPC simulation.
    const auto simulated =
        materialized && dispatch_direct_object_event_from(slot, 0x06U, slot);
    std::cout << "probe-descriptor-materialization: descriptor="
              << static_cast<unsigned int>(descriptor) << ", slot=" << slot
              << ", root=" << static_cast<unsigned int>(root)
              << ", type=" << static_cast<unsigned int>(type) << ", address=0x"
              << std::hex << std::uppercase << descriptor_address
              << ", packed=0x" << packed << std::dec
              << ", table-read=" << descriptor_read
              << ", materialized=" << materialized << ", event6=" << simulated;
    if (materialization) {
      std::cout << ", return=" << materialization->return_value << ", reason="
                << sf::psx::toString(materialization->execution.reason)
                << ", pc=0x" << std::hex << std::uppercase
                << materialization->execution.pc << std::dec;
    }
    std::cout << '\n';
    return simulated;
  };
  const auto trace_driver_object = [&](std::string_view label,
                                       std::uint16_t source) {
    const auto bridge = vm.readBridgeState();
    if (!bridge || source >= bridge->objects.size()) {
      std::cout << "driver-object-state: label=" << label
                << ", source=" << source << ", bridge=0\n";
      return;
    }
    const auto &object = bridge->objects[source];
    std::uint8_t instance_flags{};
    std::uint8_t instance_secondary_flags{};
    std::uint32_t pending_events{};
    std::uint32_t ready_events{};
    static_cast<void>(vm.runtime().read8(object.instance, instance_flags));
    static_cast<void>(
        vm.runtime().read8(object.instance + 1U, instance_secondary_flags));
    static_cast<void>(vm.runtime().read32(0x80116c68U, pending_events));
    static_cast<void>(vm.runtime().read32(0x8011775cU, ready_events));
    std::cout << "driver-object-state: label=" << label << ", source=" << source
              << ", class=0x" << std::hex << std::uppercase
              << static_cast<std::uint16_t>(object.class_id) << std::dec
              << ", hp=" << object.health << '/' << object.maximum_health
              << ", linked=" << object.linked_slot
              << ", resident=" << object.resident
              << ", simulated=" << object.simulated << ", instance=0x"
              << std::hex << std::uppercase << object.instance << ':'
              << static_cast<unsigned int>(instance_flags) << '/'
              << static_cast<unsigned int>(instance_secondary_flags)
              << ", state="
              << static_cast<unsigned int>(object.instance_state[0]) << '/'
              << static_cast<unsigned int>(object.instance_state[1]) << '/'
              << static_cast<unsigned int>(object.instance_state[2]) << '/'
              << static_cast<unsigned int>(object.instance_state[3])
              << ", presentation=0x" << object.presentation_controller << ':'
              << static_cast<unsigned int>(object.presentation_enabled) << '/'
              << static_cast<unsigned int>(object.presentation_mode) << std::dec
              << ", position=(" << object.position.x << ',' << object.position.y
              << ',' << object.position.z << ')'
              << ", ground=" << object.ground_contact_valid << ':'
              << object.ground_contact_y << ", events=" << pending_events << '/'
              << ready_events << '\n';
  };
  const auto trace_driver_events = [&](std::string_view label) {
    const auto trace_queue = [&](std::string_view queue,
                                 std::uint32_t count_address) {
      std::uint32_t count{};
      if (!vm.runtime().read32(count_address, count)) {
        return;
      }
      count = std::min(count, 16U);
      for (std::uint32_t index = 0U; index < count; ++index) {
        const auto event = count_address + 4U + index * 0x1cU;
        std::uint16_t id{};
        std::uint32_t source{};
        std::uint32_t destination{};
        if (!vm.runtime().read16(event, id) ||
            !vm.runtime().read32(event + 4U, source) ||
            !vm.runtime().read32(event + 8U, destination)) {
          break;
        }
        std::cout << "driver-event-state: label=" << label
                  << ", queue=" << queue << ", index=" << index << ", id=0x"
                  << std::hex << std::uppercase << id << ", source=0x" << source
                  << ", destination=0x" << destination << std::dec << '\n';
      }
    };
    trace_queue("pending", 0x80116c68U);
    trace_queue("ready", 0x8011775cU);
  };
  const auto queue_driver_damage =
      [&](const sf::game::LegacyObjectBridgeState &object,
          std::string_view reason) {
        const auto generation =
            object.slot < slots.size() ? slots[object.slot].generations : 0U;
        const auto key =
            (static_cast<std::uint64_t>(object.slot) << 32U) | generation;
        if (std::ranges::find(damaged_actor_generations, key) !=
            damaged_actor_generations.end()) {
          return 0;
        }
        if (!driver_mission || driver_mission->player_slot < 0) {
          return -1;
        }
        if (object.slot == 140U) {
          trace_driver_object("lock-before-damage", 140U);
          trace_driver_object("gate-before-damage", 67U);
        }
        const auto impact_only = object.class_id == 0x3a;
        if (impact_only) {
          const auto impact =
              vm.queueHostImpact(driver_mission->player_slot,
                                 static_cast<std::int16_t>(object.slot));
          if (!impact.completed()) {
            std::cout << "driver-impact-fault: slot=" << object.slot
                      << ", reason=" << reason << ", pc=0x" << std::hex
                      << std::uppercase << impact.execution.pc << std::dec
                      << '\n';
            return -1;
          }
        } else {
          const auto immediate_static_actor =
              object.slot < dynamic_first_slot &&
              (object.class_id == 0 || object.class_id == 1 ||
               object.class_id == 0x35);
          if (immediate_static_actor) {
            std::uint16_t hit_part{1U};
            if (object.health_controller != 0U) {
              static_cast<void>(
                  vm.runtime().read16(object.health_controller + 2U, hit_part));
            }
            // Headless actors have no HMD display root from which
            // FUN80069224 can derive an impact vector. Continue at the
            // exact retail health core with its decoded arguments.
            const auto damage = vm.invoke(
                0x80068770U,
                std::array{
                    static_cast<std::uint32_t>(object.slot),
                    0xffffffffU,
                    static_cast<std::uint32_t>(driver_mission->player_slot),
                    0x7fffU,
                    static_cast<std::uint32_t>(hit_part),
                    0U,
                    0x8011e670U,
                    0x8011e670U,
                },
                5'000'000U);
            if (!damage.completed()) {
              std::cout << "driver-damage-core-fault: slot=" << object.slot
                        << ", reason=" << reason << '\n';
              return -1;
            }
          } else {
            const auto damage =
                vm.queueHostDamage(sf::game::LegacyHostDamageEvent{
                    driver_mission->player_slot,
                    driver_mission->player_slot,
                    static_cast<std::int16_t>(object.slot),
                    std::numeric_limits<std::int16_t>::max(),
                    0x0f,
                });
            if (!damage.completed()) {
              std::cout << "driver-damage-fault: slot=" << object.slot
                        << ", reason=" << reason << ", pc=0x" << std::hex
                        << std::uppercase << damage.execution.pc << std::dec
                        << '\n';
              return -1;
            }
          }
        }
        if (object.slot == 174U || object.slot == 260U) {
          // The headless route bypasses the normal death dispatcher. Replay
          // the exact SUBWAY.BIN callback registered by FUN800686f4.
          const auto overlay_death = vm.invoke(
              0x80148168U,
              std::array{
                  static_cast<std::uint32_t>(object.slot),
                  static_cast<std::uint32_t>(driver_mission->player_slot),
              },
              5'000'000U);
          if (!overlay_death.completed()) {
            std::cout << "driver-overlay-death-fault: slot=" << object.slot
                      << ", reason=" << reason << '\n';
            return -1;
          }
        }
        damaged_actor_generations.push_back(key);
        std::cout << "driver-damage: stage="
                  << legacyLevelDriverStageName(driver_stage)
                  << ", slot=" << object.slot << ", generation=" << generation
                  << ", reason=" << reason << '\n';
        if (object.slot == 140U) {
          trace_driver_object("lock-after-damage", 140U);
          trace_driver_object("gate-after-damage", 67U);
        }
        return 1;
      };
  const auto verify_failure_branch = [&](std::uint16_t protected_slot) {
    constexpr std::uint32_t retail_failure_delay = 0xc8U;
    const auto snapshot = vm.captureSnapshot();
    std::optional<std::uint32_t> failure_update;
    std::optional<std::uint32_t> terminal_update;
    std::optional<std::uint32_t> transition_update;
    auto completed_seen = false;
    auto failure_bridge_seen = false;
    auto early_transition = false;
    const auto player_slot =
        driver_mission ? driver_mission->player_slot : std::int16_t{-1};
    const auto trace_failure_latches = [&](std::string_view phase,
                                           std::uint32_t update) {
      std::uint8_t terminal{};
      std::uint8_t transition_started{};
      std::uint8_t transition{};
      std::uint8_t failure{};
      std::uint8_t completed{};
      const auto raw = vm.runtime().read8(0x80115cc8U, terminal) &&
                       vm.runtime().read8(0x80115cc9U, transition_started) &&
                       vm.runtime().read8(0x80115ccaU, transition) &&
                       vm.runtime().read8(0x80116b24U, failure) &&
                       vm.runtime().read8(0x80116b25U, completed);
      const auto mission_state = vm.readMissionBridgeState();
      const auto bridge_state = vm.readBridgeState();
      std::cout << "failure-latch-trace: phase=" << phase
                << ", update=" << update << ", raw=" << raw << ':'
                << static_cast<unsigned int>(terminal) << '/'
                << static_cast<unsigned int>(transition_started) << '/'
                << static_cast<unsigned int>(transition) << '/'
                << static_cast<unsigned int>(failure) << '/'
                << static_cast<unsigned int>(completed)
                << ", bridge=" << mission_state.has_value();
      if (mission_state) {
        std::cout << ':' << mission_state->terminal << '/'
                  << mission_state->success << '/' << mission_state->failure
                  << '/' << mission_state->failure_transition;
      }
      if (bridge_state && bridge_state->objects.size() > protected_slot) {
        std::cout << ", health="
                  << bridge_state->objects[protected_slot].health;
      }
      std::cout << '\n';
    };
    trace_failure_latches("before-damage", 0U);
    const auto damage = vm.queueHostDamage(sf::game::LegacyHostDamageEvent{
        player_slot,
        player_slot,
        std::bit_cast<std::int16_t>(protected_slot),
        std::numeric_limits<std::int16_t>::max(),
        0x0f,
    });
    trace_failure_latches("after-damage", 0U);
    if (damage.completed()) {
      for (std::uint32_t update = 0U; update < retail_failure_delay + 160U;
           ++update) {
        std::uint8_t terminal{};
        std::uint8_t transition{};
        std::uint8_t failure{};
        std::uint8_t completed{};
        if (!vm.runtime().read8(0x80115cc8U, terminal) ||
            !vm.runtime().read8(0x80115ccaU, transition) ||
            !vm.runtime().read8(0x80116b24U, failure) ||
            !vm.runtime().read8(0x80116b25U, completed)) {
          break;
        }
        completed_seen = completed_seen || completed != 0U;
        if (failure != 0U && !failure_update) {
          failure_update = update;
        }
        if (terminal != 0U && !terminal_update) {
          terminal_update = update;
        }
        const auto mission_state = vm.readMissionBridgeState();
        failure_bridge_seen =
            failure_bridge_seen || (mission_state && mission_state->failure);
        if (transition != 0U && failure_update) {
          transition_update = update;
          early_transition = update - *failure_update < retail_failure_delay;
          break;
        }
        std::uint32_t application_state{};
        if (!vm.runtime().read32(0x80115c78U, application_state) ||
            !vm.writeHostPadState(sf::game::LegacyHostPadState{})) {
          break;
        }
        const auto gameplay =
            application_state == 0U || application_state == 5U;
        const auto result = gameplay ? vm.tickNativeDrivenGameplayFrame()
                                     : vm.tickRetailOuterFrame();
        if (!result.completed()) {
          break;
        }
        const auto traced_update = update + 1U;
        if (traced_update <= 3U ||
            (failure_update &&
             traced_update + 2U >= *failure_update + retail_failure_delay)) {
          trace_failure_latches("after-tick", traced_update);
        }
      }
    }
    constexpr std::uint32_t exact_failure_update = 2U;
    constexpr std::uint32_t exact_transition_update =
        exact_failure_update + retail_failure_delay;
    const auto passed = damage.completed() && failure_update.has_value() &&
                        terminal_update.has_value() &&
                        transition_update.has_value() && failure_bridge_seen &&
                        !completed_seen && !early_transition &&
                        *failure_update == exact_failure_update &&
                        *terminal_update == exact_failure_update &&
                        *transition_update == exact_transition_update;
    const auto restored = vm.restoreSnapshot(snapshot);
    std::cout << "failure-branch: protected-slot=" << protected_slot
              << ", failure=" << passed << ", outcome-update=";
    if (failure_update) {
      std::cout << *failure_update;
    } else {
      std::cout << "none";
    }
    std::cout << ", terminal-update=";
    if (terminal_update) {
      std::cout << *terminal_update;
    } else {
      std::cout << "none";
    }
    std::cout << ", transition-update=";
    if (transition_update) {
      std::cout << *transition_update;
    } else {
      std::cout << "none";
    }
    std::cout << ", completed-seen=" << completed_seen
              << ", bridge-failure=" << failure_bridge_seen
              << ", early-transition=" << early_transition
              << ", restored=" << restored << '\n';
    return passed && restored;
  };
  const auto force_driver_room = [&](std::uint16_t source) {
    const auto room = source_room(source);
    // The probe teleports instead of crossing authored portals, but uses
    // the same exact FUN_800820d4 room-change boundary as production.
    constexpr std::uint32_t current_room_address = 0x80116946U;
    std::uint16_t previous_room{};
    const auto previous_room_read =
        vm.runtime().read16(current_room_address, previous_room);
    const auto room_changed = previous_room_read && previous_room != room;
    auto room_synchronized = previous_room_read;
    std::vector<std::uint16_t> room_route;
    std::optional<std::uint16_t> failed_route_room;
    auto bank_portal_route_selected = false;
    if (room_synchronized && room_changed) {
      // The neighbour table also contains visibility-only links. This
      // retail route is the portal/AABB-connected path extracted from
      // SUBWAY.DAT for the radio/Kravitch area back to bank room 18.
      constexpr std::array<std::uint16_t, 19U> bank_portal_route{
          71U, 70U, 68U, 69U, 67U, 82U, 81U, 73U, 75U, 76U,
          84U, 85U, 86U, 89U, 90U, 23U, 21U, 22U, 18U,
      };
      const auto bank_route_position =
          std::ranges::find(bank_portal_route, previous_room);
      if (room == 18U && bank_route_position != bank_portal_route.end() &&
          std::next(bank_route_position) != bank_portal_route.end()) {
        bank_portal_route_selected = true;
        room_route.push_back(*std::next(bank_route_position));
      } else {
        std::uint32_t room_data{};
        std::uint32_t room_count{};
        room_synchronized =
            vm.runtime().read32(0x80116a60U, room_data) && room_data != 0U &&
            vm.runtime().read32(room_data + 0x88U, room_count) &&
            room_count != 0U && room_count <= 256U &&
            previous_room < room_count && room < room_count;
        std::vector<std::int16_t> predecessor;
        std::vector<std::uint16_t> pending;
        if (room_synchronized) {
          predecessor.assign(room_count, std::int16_t{-2});
          predecessor[previous_room] = -1;
          pending.push_back(previous_room);
        }
        for (std::size_t cursor = 0U;
             room_synchronized && cursor < pending.size() &&
             predecessor[room] == -2;
             ++cursor) {
          const auto current = pending[cursor];
          const auto neighbours =
              room_data + 0x90U + static_cast<std::uint32_t>(current) * 0x0fU;
          for (std::uint32_t index = 0U; index < 0x0fU; ++index) {
            std::uint8_t neighbour{};
            if (!vm.runtime().read8(neighbours + index, neighbour)) {
              room_synchronized = false;
              break;
            }
            if (neighbour >= room_count) {
              break;
            }
            if (predecessor[neighbour] == -2) {
              predecessor[neighbour] = static_cast<std::int16_t>(current);
              pending.push_back(neighbour);
            }
          }
        }
        room_synchronized = room_synchronized && predecessor[room] != -2;
        if (room_synchronized) {
          for (auto current = room; current != previous_room;
               current = static_cast<std::uint16_t>(predecessor[current])) {
            room_route.push_back(current);
          }
          std::ranges::reverse(room_route);
        }
      }
      if (room_synchronized) {
        constexpr std::array bank_route_portal_seeds{
            std::pair{70U, sf::game::LegacyNativePoint{-302, -2'140, 5'606}},
            std::pair{68U, sf::game::LegacyNativePoint{1'133, -2'140, 6'810}},
            std::pair{69U, sf::game::LegacyNativePoint{1'400, -2'140, 5'154}},
            std::pair{67U, sf::game::LegacyNativePoint{1'766, -2'140, 3'410}},
            std::pair{82U, sf::game::LegacyNativePoint{2'425, -2'140, 5'137}},
            std::pair{81U, sf::game::LegacyNativePoint{2'977, -2'140, 4'495}},
            std::pair{73U, sf::game::LegacyNativePoint{4'221, -2'140, 3'177}},
            std::pair{75U, sf::game::LegacyNativePoint{5'439, -2'140, 3'639}},
            std::pair{76U, sf::game::LegacyNativePoint{5'766, -2'140, 4'532}},
            std::pair{84U, sf::game::LegacyNativePoint{6'431, -2'140, 4'979}},
            std::pair{85U, sf::game::LegacyNativePoint{7'340, -2'140, 6'669}},
            std::pair{86U, sf::game::LegacyNativePoint{8'669, -2'140, 5'656}},
            std::pair{89U, sf::game::LegacyNativePoint{10'358, -2'140, 7'237}},
            std::pair{90U, sf::game::LegacyNativePoint{10'472, -2'140, 9'040}},
            std::pair{23U, sf::game::LegacyNativePoint{10'840, -2'140, 9'813}},
            std::pair{21U, sf::game::LegacyNativePoint{11'682, -2'140, 10'890}},
            std::pair{22U, sf::game::LegacyNativePoint{11'812, -2'140, 11'447}},
            std::pair{18U, sf::game::LegacyNativePoint{12'546, -2'140, 12'554}},
        };
        constexpr std::array bank_route_destination_points{
            std::pair{70U, sf::game::LegacyNativePoint{418, -2'141, 6'148}},
            std::pair{68U, sf::game::LegacyNativePoint{1'449, -2'140, 6'062}},
            std::pair{69U, sf::game::LegacyNativePoint{1'473, -2'141, 3'877}},
            std::pair{67U, sf::game::LegacyNativePoint{2'315, -2'140, 4'186}},
            std::pair{82U, sf::game::LegacyNativePoint{3'318, -2'133, 5'272}},
            std::pair{81U, sf::game::LegacyNativePoint{3'633, -2'133, 3'836}},
            std::pair{73U, sf::game::LegacyNativePoint{5'112, -2'133, 2'632}},
            std::pair{75U, sf::game::LegacyNativePoint{5'434, -2'133, 4'198}},
            std::pair{76U, sf::game::LegacyNativePoint{6'212, -2'133, 4'757}},
            std::pair{84U, sf::game::LegacyNativePoint{6'557, -2'133, 6'101}},
            std::pair{85U, sf::game::LegacyNativePoint{8'008, -2'133, 6'108}},
            std::pair{86U, sf::game::LegacyNativePoint{9'678, -2'133, 6'342}},
            std::pair{89U, sf::game::LegacyNativePoint{9'682, -2'133, 8'150}},
            std::pair{90U, sf::game::LegacyNativePoint{9'676, -2'133, 10'206}},
            std::pair{23U, sf::game::LegacyNativePoint{11'196, -2'140, 10'367}},
            std::pair{21U, sf::game::LegacyNativePoint{12'065, -2'140, 11'004}},
            std::pair{22U, sf::game::LegacyNativePoint{12'113, -2'140, 12'114}},
            std::pair{18U, sf::game::LegacyNativePoint{13'666, -2'140, 11'801}},
        };
        for (const auto route_room : room_route) {
          if (bank_portal_route_selected) {
            const auto portal_seed = std::ranges::find_if(
                bank_route_portal_seeds, [route_room](const auto &entry) {
                  return entry.first == route_room;
                });
            if (portal_seed == bank_route_portal_seeds.end()) {
              room_synchronized = false;
              failed_route_room = route_room;
              break;
            }
            if (!vm.writeHostPlayerState(sf::game::LegacyHostPlayerState{
                    portal_seed->second,
                    0,
                    150,
                    600,
                })) {
              room_synchronized = false;
              failed_route_room = route_room;
              break;
            }
          }
          std::uint16_t synchronized_room{};
          if (!vm.synchronizeHostRoom(static_cast<std::int16_t>(route_room)) ||
              !vm.runtime().read16(current_room_address, synchronized_room) ||
              synchronized_room != route_room) {
            room_synchronized = false;
            failed_route_room = route_room;
            break;
          }
          if (bank_portal_route_selected) {
            const auto destination = std::ranges::find_if(
                bank_route_destination_points, [route_room](const auto &entry) {
                  return entry.first == route_room;
                });
            if (destination == bank_route_destination_points.end() ||
                !vm.writeHostPlayerState(sf::game::LegacyHostPlayerState{
                    destination->second,
                    0,
                    150,
                    600,
                })) {
              room_synchronized = false;
              failed_route_room = route_room;
              break;
            }
            bank_route_hold_position = destination->second;
            bank_route_next_sync_update = driver_stage_guest_updates + 2U;
          }
        }
      }
    }
    if (room_changed) {
      previous_applied_host_position.reset();
    }
    const auto bridge_after_bootstrap = vm.readBridgeState();
    const auto source_resident =
        bridge_after_bootstrap &&
        source < bridge_after_bootstrap->objects.size() &&
        bridge_after_bootstrap->objects[source].resident;
    const auto source_simulated =
        bridge_after_bootstrap &&
        source < bridge_after_bootstrap->objects.size() &&
        bridge_after_bootstrap->objects[source].simulated;
    std::uint16_t current_room{};
    const auto bridge_ok =
        vm.runtime().read16(current_room_address, current_room);
    std::cout << "driver-room: stage="
              << legacyLevelDriverStageName(driver_stage)
              << ", source=" << source << ", requested=" << room
              << ", previous=" << std::bit_cast<std::int16_t>(previous_room)
              << ", current=" << std::bit_cast<std::int16_t>(current_room)
              << ", production-room-sync=" << room_synchronized
              << ", changed=" << room_changed << ", route=";
    if (room_route.empty()) {
      std::cout << "same";
    } else {
      for (const auto route_room : room_route) {
        std::cout << route_room << '/';
      }
    }
    if (failed_route_room) {
      std::cout << ", failed-route-room=" << *failed_route_room;
    }
    std::cout << ", resident=" << source_resident
              << ", simulated=" << source_simulated << '\n';
    if (!room_synchronized) {
      const auto trace_word = [&](std::string_view name,
                                  std::uint32_t address) {
        std::uint32_t value{};
        const auto read = vm.runtime().read32(address, value);
        std::cout << "room-stream-state: " << name << "=";
        if (read) {
          std::cout << "0x" << std::hex << value << std::dec;
        } else {
          std::cout << "unreadable";
        }
        std::cout << '\n';
      };
      trace_word("queue-a", 0x80116a08U);
      trace_word("queue-b", 0x80116b3cU);
      trace_word("stream-transfer", 0x8011609cU);
      trace_word("stream-destination", 0x80116098U);
      trace_word("stream-count", 0x801160a0U);
      trace_word("stream-callback", 0x801160acU);
      trace_word("stream-state", 0x80116944U);
      trace_word("stream-request", 0x80115e80U);
      std::uint32_t queue_link{};
      if (vm.runtime().read32(0x80116a08U, queue_link) && queue_link != 0U) {
        for (std::uint32_t offset = 0U; offset < 0x24U; offset += 4U) {
          trace_word("queue-link+" + std::to_string(offset),
                     queue_link + offset);
        }
        std::uint32_t request{};
        if (vm.runtime().read32(queue_link, request) && request != 0U) {
          for (std::uint32_t offset = 0U; offset < 0x24U; offset += 4U) {
            trace_word("request+" + std::to_string(offset), request + offset);
          }
        }
      }
    }
    return previous_room_read && room_synchronized && bridge_ok;
  };

  const auto begin_lifetime =
      [&](const sf::game::LegacyObjectBridgeState &object,
          std::uint32_t frame) {
        auto &trace = slots[object.slot];
        LegacyLevelActorLifetime lifetime;
        lifetime.identity = LegacyLevelActorIdentity{
            object.class_id,          object.definition,   object.parameter,
            object.authored_position, object.path_pointer,
        };
        lifetime.slot = object.slot;
        lifetime.generation = trace.generations++;
        lifetime.first_frame = frame;
        lifetime.last_frame = frame;
        lifetime.start_health = object.health;
        lifetime.minimum_health = object.health;
        lifetime.end_health = object.health;
        lifetime.start_position = object.position;
        lifetime.end_position = object.position;
        lifetime.last_target = object.has_target ? object.target_slot : -1;
        lifetime.last_pose_fingerprint = legacyPoseFingerprint(object);
        lifetime.saw_positive_health = object.health > 0;
        lifetime.saw_target = object.has_target;
        lifetimes.push_back(lifetime);
        trace.lifetime = lifetimes.size() - 1U;
      };

  for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
    std::uint32_t current_state{};
    if (!vm.runtime().read32(0x80115c78U, current_state)) {
      return 31;
    }
    const auto gameplay_state = current_state == 0U || current_state == 5U;
    if (!previous_application_state ||
        *previous_application_state != current_state) {
      std::cout << "application-state: frame=" << frame
                << ", state=" << current_state
                << ", driver=" << legacyLevelDriverStageName(driver_stage)
                << '\n';
      previous_application_state = current_state;
    }
    sf::game::LegacyHostPadState driver_pad;
    std::optional<sf::game::LegacyNativePoint> applied_host_position;
    std::optional<std::uint16_t> room_bootstrap_source;
    if (native_driven && driver_stage != LegacyLevelDriverStage::complete) {
      ++driver_stage_frames;
    }
    if (native_driven && gameplay_state && driver_bridge && driver_mission) {
      ++driver_stage_guest_updates;
      const auto hold_source = [&](std::uint16_t source) {
        if (driver_stage_guest_updates == 1U) {
          room_bootstrap_source = source;
        }
        applied_host_position = source_host_position(source);
      };
      const auto cross_trigger = [&](std::uint16_t source,
                                     sf::game::LegacyNativePoint outside,
                                     sf::game::LegacyNativePoint inside) {
        if (driver_stage_guest_updates == 1U) {
          room_bootstrap_source = source;
        }
        applied_host_position =
            driver_stage_guest_updates <= 2U ? outside : inside;
      };
      const auto interact_after_settle = [&](std::uint16_t source) {
        if (driver_stage_guest_updates <= 2U) {
          hold_source(source);
        }
        if (driver_stage_guest_updates == 8U) {
          const auto interaction =
              vm.queueHostInteraction(std::bit_cast<std::int16_t>(source));
          std::cout << "driver-interaction: frame=" << frame
                    << ", stage=" << legacyLevelDriverStageName(driver_stage)
                    << ", source=" << source
                    << ", completed=" << interaction.completed() << ", reason="
                    << sf::psx::toString(interaction.execution.reason)
                    << ", pc=0x" << std::hex << std::uppercase
                    << interaction.execution.pc << std::dec << '\n';
          if (!interaction.completed()) {
            record_driver_blocker("source-" + std::to_string(source) +
                                  "-interaction-fault");
          }
        }
      };
      using enum LegacyLevelDriverStage;
      switch (driver_stage) {
      case waiting_opening:
      case failure_branch:
      case complete:
        break;
      case clear_opening: {
        hold_source(
            static_cast<std::uint16_t>(mission.objects().playerIndex()));
        if (!failure_branch_checked) {
          const auto protected_cbdc = std::ranges::find_if(
              driver_bridge->objects,
              [&](const sf::game::LegacyObjectBridgeState &object) {
                return object.slot == 351U && object.class_id == 0x35 &&
                       object.definition == 0x0dU && object.simulated &&
                       object.health > 0;
              });
          if (protected_cbdc != driver_bridge->objects.end()) {
            failure_branch_checked = true;
            failure_branch_passed = verify_failure_branch(
                static_cast<std::uint16_t>(protected_cbdc->slot));
            if (!failure_branch_passed) {
              record_driver_blocker("protected-cbdc-failure-branch-missing");
            }
          }
        }
        const auto hostile = std::ranges::find_if(
            driver_bridge->objects,
            [&](const sf::game::LegacyObjectBridgeState &object) {
              return object.class_id == 1 && object.simulated &&
                     object.health > 0 &&
                     !object_matches_source(object, 174U) &&
                     !object_matches_source(object, 175U);
            });
        if (hostile != driver_bridge->objects.end() &&
            queue_driver_damage(*hostile, "opening-hostile") < 0) {
          record_driver_blocker("opening-hostile-damage-fault");
        }
        break;
      }
      case trigger_256:
        hold_source(256U);
        if (driver_stage_guest_updates == 8U) {
          static_cast<void>(dispatch_trigger_fallback(0U, 256U));
        }
        break;
      case passage_64:
        if (driver_stage_guest_updates == 1U) {
          room_bootstrap_source = std::uint16_t{257U};
        }
        applied_host_position = source_host_position(64U);
        applied_host_position->y = source_host_position(257U).y;
        break;
      case passage_65:
        applied_host_position = source_host_position(65U);
        applied_host_position->y = source_host_position(257U).y;
        break;
      case trigger_257:
        cross_trigger(257U, {1'040, -2'150, 4'796}, {1'328, -2'150, 4'796});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 257U &&
            (driver_bridge->objects[257U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(1U, 257U));
        }
        break;
      case intro_157:
        cross_trigger(157U, {-5'784, -2'204, 3'636}, {-5'636, -2'204, 3'636});
        if (driver_stage_guest_updates == 8U && !intro_state9_seen) {
          static_cast<void>(dispatch_trigger_fallback(2U, 157U));
        }
        break;
      case lock_140:
        hold_source(140U);
        if (driver_stage_guest_updates >= 3U &&
            driver_bridge->objects.size() > 140U &&
            driver_bridge->objects[140U].health > 0 &&
            queue_driver_damage(driver_bridge->objects[140U], "gate-lock") <
                0) {
          record_driver_blocker("gate-lock-damage-fault");
        }
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 140U &&
            (driver_bridge->objects[140U].instance_state[3] & 0x02U) != 0U &&
            (driver_bridge->objects[67U].instance_state[0] & 0x08U) == 0U) {
          gate_event14_fallback_attempted = true;
          const std::array arguments{
              0x14U, 3U, 140U, 67U, 0U, 0U, 0U, 0U,
          };
          const auto event = vm.invoke(0x80015364U, arguments);
          gate_event14_fallback_completed = event.completed();
          std::cout << "gate-event14-fallback: completed=" << event.completed()
                    << ", reason=" << sf::psx::toString(event.execution.reason)
                    << ", pc=0x" << std::hex << std::uppercase
                    << event.execution.pc << std::dec << '\n';
        }
        if (driver_stage_guest_updates == 12U &&
            gate_event14_fallback_attempted &&
            gate_event14_fallback_completed &&
            driver_bridge->objects.size() > 140U &&
            (driver_bridge->objects[140U].instance_state[3] & 0x02U) != 0U &&
            (driver_bridge->objects[67U].instance_state[0] & 0x08U) == 0U) {
          // Probe-only continuation: portal visibility is not reproduced
          // by the teleported route, so dispatch the exact class handler.
          gate_handler14_fallback_completed =
              dispatch_direct_object_event_from(67U, 0x14U, 140U);
          trace_driver_object("gate-after-direct-handler14", 67U);
        }
        break;
      case bank_175: {
        constexpr sf::game::LegacyNativePoint bank_safe_position{
            14'000,
            -2'140,
            10'700,
        };
        // Source 173 is not activated by entering room 18 directly.
        // Retail room 22 keeps room 18 resident; cross source 173's
        // exact AAA4B1 event OBB before taking the final portal.
        constexpr sf::game::LegacyNativePoint bank_activation_outside{
            12'120,
            -2'140,
            12'081,
        };
        constexpr sf::game::LegacyNativePoint bank_activation_inside{
            11'802,
            -2'140,
            12'081,
        };
        std::uint16_t bank_current_room{};
        const auto bank_room_read =
            vm.runtime().read16(0x80116946U, bank_current_room);
        const auto bank_route_in_progress =
            bank_room_read && bank_current_room != 18U;
        const auto bank_activation_pending =
            bank_route_in_progress && bank_current_room == 22U &&
            driver_stage_guest_updates < bank_route_next_sync_update + 3U;
        const auto bank_route_settling =
            bank_route_hold_position &&
            driver_stage_guest_updates < bank_route_next_sync_update;
        if (bank_route_in_progress && !bank_activation_pending &&
            driver_stage_guest_updates >= bank_route_next_sync_update) {
          room_bootstrap_source = std::uint16_t{173U};
        }
        // Room 18 bounds are x=12546..14787, z=10601..13001. Keep the
        // probe inside visibility range but off source 173's exact EA0
        // route (which starts at 12606,10770 and ends at 14559,12464).
        if (bank_activation_pending) {
          const auto crossing_volume =
              driver_stage_guest_updates >= bank_route_next_sync_update;
          applied_host_position = crossing_volume ? bank_activation_inside
                                                  : bank_activation_outside;
          if (crossing_volume && !bank_activation_volume_entered) {
            bank_activation_volume_entered = true;
            std::cout << "bank-source-173-volume-enter: update="
                      << driver_stage_guest_updates << '\n';
          }
        } else {
          applied_host_position =
              (bank_route_in_progress || bank_route_settling) &&
                      bank_route_hold_position
                  ? *bank_route_hold_position
                  : bank_safe_position;
        }
        if (!bank_route_in_progress && !bank_route_settling) {
          bank_route_hold_position.reset();
        }
        if (bank_activation_volume_entered && bank_current_room == 18U &&
            !bank_source_fallback_attempted) {
          bank_source_fallback_attempted = true;
          const auto source_173_loaded = dispatch_load_fallback(173U);
          const auto source_175_loaded = dispatch_load_fallback(175U);
          const auto sources_activated =
              source_173_loaded && source_175_loaded &&
              dispatch_direct_object_event(173U, 0x0aU) &&
              dispatch_direct_object_event(175U, 0x0aU) &&
              dispatch_direct_object_event(173U, 0x06U) &&
              dispatch_direct_object_event(175U, 0x06U);
          std::cout << "bank-sources-teleport-continuation: loaded="
                    << source_173_loaded << '/' << source_175_loaded
                    << ", activated=" << sources_activated << '\n';
          if (!sources_activated) {
            record_driver_blocker("bank-source-visibility-continuation-fault");
          }
        }
        std::uint32_t descriptor_table{};
        std::uint16_t descriptor_flags{};
        std::uint32_t descriptor_roots{};
        std::uint32_t root_table{};
        auto descriptor_state_read =
            vm.runtime().read32(0x80116adcU, descriptor_table) &&
            vm.runtime().read16(descriptor_table + 10U * 16U,
                                descriptor_flags) &&
            vm.runtime().read32(descriptor_table + 10U * 16U + 4U,
                                descriptor_roots) &&
            vm.runtime().read32(descriptor_table + 10U * 16U + 8U,
                                root_table) &&
            (descriptor_roots & 0xffU) == bank_descriptor_paths.size();
        for (std::size_t root = 0U;
             descriptor_state_read && root < bank_descriptor_paths.size();
             ++root) {
          descriptor_state_read = vm.runtime().read32(
              root_table + static_cast<std::uint32_t>(root * 4U),
              bank_descriptor_paths[root]);
        }
        if (descriptor_state_read) {
          auto active = (descriptor_flags & 0x4000U) != 0U;
          if (!active && bank_activation_volume_entered &&
              bank_current_room == 18U && !bank_descriptor_fallback_attempted) {
            bank_descriptor_fallback_attempted = true;
            const auto activation =
                vm.invoke(0x8005fd04U, std::array{10U}, 5'000'000U);
            const auto flags_read = vm.runtime().read16(
                descriptor_table + 10U * 16U, descriptor_flags);
            active = flags_read && (descriptor_flags & 0x4000U) != 0U;
            std::cout << "bank-descriptor-10-teleport-continuation: completed="
                      << activation.completed() << ", flags=0x" << std::hex
                      << std::uppercase << descriptor_flags << std::dec
                      << ", active=" << active << ", reason="
                      << sf::psx::toString(activation.execution.reason)
                      << ", pc=0x" << std::hex << std::uppercase
                      << activation.execution.pc << std::dec << '\n';
            if (!activation.completed() || !active) {
              record_driver_blocker("bank-descriptor-10-activation-fault");
            }
          }
          const auto remaining = descriptor_flags & 0x3fffU;
          if (bank_reinforcement_goal == 0U && remaining != 0U) {
            // The five roots are candidate paths. The low flag
            // field is the finite actor count; activation consumes
            // the first count in the same guest update.
            bank_reinforcement_goal = remaining + (active ? 1U : 0U);
          }
          if (active && !bank_descriptor_completed) {
            bank_descriptor_completed = true;
            std::cout << "bank-descriptor-10-natural: flags=0x" << std::hex
                      << std::uppercase << descriptor_flags << ", paths=";
            for (const auto path : bank_descriptor_paths) {
              std::cout << " 0x" << path;
            }
            std::cout << std::dec << '\n';
          }
        }

        if (driver_stage_guest_updates == 4U ||
            driver_stage_guest_updates % 120U == 0U) {
          std::cout << "bank-natural-state: update="
                    << driver_stage_guest_updates << ", flags=0x" << std::hex
                    << std::uppercase << descriptor_flags << std::dec
                    << ", descriptor-read=" << descriptor_state_read
                    << ", seen=" << bank_roots_materialized
                    << ", kills=" << bank_reinforcement_kills << '/'
                    << bank_reinforcement_goal << '\n';
          trace_driver_object("bank-protected-state", 173U);
          trace_driver_object("bank-static-state", 175U);
        }

        if (driver_bridge->objects.size() > 175U) {
          const auto &static_hostile = driver_bridge->objects[175U];
          if (static_hostile.simulated && static_hostile.health > 0 &&
              queue_driver_damage(static_hostile, "bank-static-hostile") < 0) {
            record_driver_blocker("bank-static-hostile-damage-fault");
          }
        }
        if (bank_materialized_slot) {
          const auto slot = *bank_materialized_slot;
          if (slot < driver_bridge->objects.size()) {
            const auto &object = driver_bridge->objects[slot];
            const auto generation =
                slot < slots.size() ? slots[slot].generations : 0U;
            const auto key =
                (static_cast<std::uint64_t>(slot) << 32U) | generation;
            const auto bank_path =
                std::ranges::find(bank_descriptor_paths, object.path_pointer) !=
                bank_descriptor_paths.end();
            if (bank_materialized_generation &&
                *bank_materialized_generation != key) {
              const auto damaged =
                  std::ranges::find(damaged_actor_generations,
                                    *bank_materialized_generation) !=
                  damaged_actor_generations.end();
              if (damaged) {
                ++bank_reinforcement_kills;
                std::cout << "bank-materialized-recycled: slot=" << slot
                          << ", generation=" << generation
                          << ", kills=" << bank_reinforcement_kills << '\n';
              }
              bank_materialized_generation.reset();
              if (!bank_path || object.health <= 0) {
                bank_materialized_slot.reset();
              }
            }
            if (!bank_materialized_generation && bank_path &&
                object.health > 0) {
              bank_materialized_generation = key;
              ++bank_roots_materialized;
              std::cout << "bank-materialized-actor: slot=" << slot
                        << ", generation=" << generation << ", path=0x"
                        << std::hex << std::uppercase << object.path_pointer
                        << std::dec << '\n';
            }
            if (bank_materialized_generation &&
                *bank_materialized_generation == key) {
              if (object.health <= 0) {
                ++bank_reinforcement_kills;
                std::cout << "bank-materialized-death: slot=" << slot
                          << ", generation=" << generation
                          << ", kills=" << bank_reinforcement_kills << '\n';
                bank_materialized_generation.reset();
                bank_materialized_slot.reset();
              } else if (queue_driver_damage(object,
                                             "bank-finite-descriptor-10") < 0) {
                record_driver_blocker("bank-finite-hostile-damage-fault");
              }
            }
          }
        }
        if (bank_descriptor_completed && !bank_materialized_slot) {
          const auto materialized = std::ranges::find_if(
              driver_bridge->objects,
              [&](const sf::game::LegacyObjectBridgeState &object) {
                return object.slot >= dynamic_first_slot &&
                       object.class_id == 1 && object.health > 0 &&
                       object.simulated &&
                       std::ranges::find(bank_descriptor_paths,
                                         object.path_pointer) !=
                           bank_descriptor_paths.end();
              });
          if (materialized != driver_bridge->objects.end()) {
            const auto generation = materialized->slot < slots.size()
                                        ? slots[materialized->slot].generations
                                        : 0U;
            bank_materialized_slot =
                static_cast<std::uint16_t>(materialized->slot);
            bank_materialized_generation =
                (static_cast<std::uint64_t>(materialized->slot) << 32U) |
                generation;
            ++bank_roots_materialized;
            std::cout << "bank-materialized-actor: slot=" << materialized->slot
                      << ", generation=" << generation
                      << ", root=" << bank_roots_materialized << ", path=0x"
                      << std::hex << std::uppercase
                      << materialized->path_pointer << std::dec << '\n';
          }
        }
        const auto live_bank_root = std::ranges::any_of(
            driver_bridge->objects,
            [&](const sf::game::LegacyObjectBridgeState &object) {
              return object.slot >= dynamic_first_slot &&
                     object.class_id == 1 && object.health > 0 &&
                     std::ranges::find(bank_descriptor_paths,
                                       object.path_pointer) !=
                         bank_descriptor_paths.end();
            });
        const auto descriptor_remaining = descriptor_flags & 0x3fffU;
        const auto last_root_stranded =
            bank_descriptor_completed && descriptor_state_read &&
            descriptor_remaining == 1U && bank_reinforcement_goal != 0U &&
            bank_reinforcement_kills + 1U == bank_reinforcement_goal &&
            !live_bank_root && !bank_materialized_slot;
        if (last_root_stranded) {
          ++bank_last_root_idle_updates;
        } else {
          bank_last_root_idle_updates = 0U;
        }
        if (bank_last_root_idle_updates == 20U) {
          std::optional<std::uint8_t> unseen_root;
          const auto candidate_count = std::min<std::size_t>(
              bank_reinforcement_goal, bank_descriptor_paths.size());
          for (std::size_t root = 0U; root < candidate_count; ++root) {
            const auto seen = std::ranges::any_of(
                lifetimes, [&](const LegacyLevelActorLifetime &lifetime) {
                  return lifetime.slot >= dynamic_first_slot &&
                         lifetime.first_frame >=
                             driver_stage_entry_trace_frame &&
                         lifetime.identity.path_pointer ==
                             bank_descriptor_paths[root];
                });
            if (!seen) {
              if (unseen_root) {
                unseen_root.reset();
                break;
              }
              unseen_root = static_cast<std::uint8_t>(root);
            }
          }
          if (!unseen_root || !materialize_descriptor_actor(
                                  10U, dynamic_first_slot, *unseen_root, 0U)) {
            record_driver_blocker(
                "bank-last-root-materialization-continuation-fault");
          } else {
            std::cout << "bank-last-root-teleport-continuation: root="
                      << static_cast<unsigned int>(*unseen_root) << '\n';
          }
          bank_last_root_idle_updates = 0U;
        }
        if (bank_reinforcement_goal != 0U &&
            bank_reinforcement_kills == bank_reinforcement_goal &&
            !live_bank_root) {
          ++bank_quiescent_updates;
        } else {
          bank_quiescent_updates = 0U;
        }
        break;
      }
      case kravitch_174:
        hold_source(174U);
        if (driver_stage_guest_updates >= 3U &&
            driver_bridge->objects.size() > 174U &&
            driver_bridge->objects[174U].health > 0 &&
            queue_driver_damage(driver_bridge->objects[174U], "kravitch") < 0) {
          record_driver_blocker("kravitch-damage-fault");
        }
        break;
      case radio_260:
        hold_source(260U);
        if (driver_stage_guest_updates >= 3U &&
            driver_bridge->objects.size() > 260U &&
            driver_bridge->objects[260U].health > 0 &&
            queue_driver_damage(driver_bridge->objects[260U],
                                "communications-array") < 0) {
          record_driver_blocker("communications-damage-fault");
        }
        break;
      case bomb_29:
        hold_source(29U);
        if (driver_stage_guest_updates == 4U) {
          driver_pad.buttons = 0x1000U;
          std::cout << "driver-triangle: frame=" << frame
                    << ", stage=" << legacyLevelDriverStageName(driver_stage)
                    << '\n';
        }
        if (driver_stage_guest_updates == 8U &&
            (driver_mission->completed_objectives & 0x02U) == 0U &&
            !bomb_29_callback_attempted) {
          bomb_29_callback_attempted = true;
          const auto callback =
              vm.invoke(0x801485b8U, std::array{29U}, 5'000'000U);
          bomb_29_callback_completed = callback.completed();
          std::cout << "probe-bomb-callback: source=29"
                    << ", production-interaction=0"
                    << ", completed=" << callback.completed() << ", reason="
                    << sf::psx::toString(callback.execution.reason) << ", pc=0x"
                    << std::hex << std::uppercase << callback.execution.pc
                    << std::dec << '\n';
          if (!callback.completed()) {
            record_driver_blocker("source-29-bomb-callback-fault");
          }
        }
        break;
      case trigger_190:
        cross_trigger(190U, {12'120, -2'144, 11'349}, {12'323, -2'144, 11'345});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 190U &&
            (driver_bridge->objects[190U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(3U, 190U));
        }
        break;
      case trigger_194:
        cross_trigger(194U, {753, -2'224, 3'661}, {754, -2'224, 3'157});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 194U &&
            (driver_bridge->objects[194U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(4U, 194U));
        }
        break;
      case power_317:
        if (!power_scripted_transition_completed) {
          break;
        }
        ++power_post_transition_updates;
        if (power_post_transition_updates <= 2U) {
          hold_source(317U);
        }
        if (power_post_transition_updates == 2U ||
            power_post_transition_updates == 3U) {
          trace_driver_object("power-317-before-interaction", 317U);
          std::uint32_t records{};
          std::uint32_t word_28{};
          std::uint32_t word_30{};
          std::uint32_t word_34{};
          const auto raw =
              vm.runtime().read32(0x80115cccU, records) &&
              vm.runtime().read32(records + 317U * 0x4cU + 0x28U, word_28) &&
              vm.runtime().read32(records + 317U * 0x4cU + 0x30U, word_30) &&
              vm.runtime().read32(records + 317U * 0x4cU + 0x34U, word_34);
          const auto record =
              records + static_cast<std::uint32_t>(317U * 0x4cU);
          std::cout << "power-317-raw: update=" << power_post_transition_updates
                    << ", read=" << raw << ", base=0x" << std::hex
                    << std::uppercase << records << ", record=0x" << record
                    << ", +28=0x" << word_28 << ", +30=0x" << word_30
                    << ", +34=0x" << word_34 << std::dec << '\n';
        }
        if (power_post_transition_updates == 4U) {
          driver_pad.buttons = 0x1000U;
          std::cout << "driver-triangle: frame=" << frame
                    << ", stage=power-317\n";
        }
        if (power_post_transition_updates == 8U &&
            (driver_mission->completed_objectives & 0x04U) == 0U) {
          const std::array arguments{
              0x12U, 5U, 317U, 317U, 0U, 0U, 0U, 0U,
          };
          const auto interaction =
              vm.invoke(0x80015364U, arguments, 5'000'000U);
          std::cout << "power-317-event12-continuation: priority=5"
                    << ", completed=" << interaction.completed() << ", reason="
                    << sf::psx::toString(interaction.execution.reason)
                    << ", pc=0x" << std::hex << std::uppercase
                    << interaction.execution.pc << std::dec << '\n';
          if (!interaction.completed()) {
            record_driver_blocker("power-317-event12-continuation-fault");
          }
        }
        if (power_post_transition_updates == 12U &&
            (driver_mission->completed_objectives & 0x04U) == 0U) {
          std::uint32_t records{};
          std::uint32_t linked_source{};
          const auto link_guard =
              vm.runtime().read32(0x80115cccU, records) &&
              vm.runtime().read32(records + 317U * 0x4cU + 0x30U,
                                  linked_source) &&
              linked_source == 68U;
          std::optional<sf::game::LegacyGameplayVmResult> callback;
          if (link_guard) {
            callback.emplace(
                vm.invoke(0x801488c8U, std::array{317U}, 5'000'000U));
          }
          const auto mission_after_callback = vm.readMissionBridgeState();
          std::cout << "probe-power-callback: source=317"
                    << ", link-guard=" << link_guard
                    << ", linked=" << linked_source
                    << ", completed=" << (callback && callback->completed())
                    << ", objective2="
                    << (mission_after_callback &&
                        (mission_after_callback->completed_objectives &
                         0x04U) != 0U);
          if (callback) {
            std::cout << ", reason="
                      << sf::psx::toString(callback->execution.reason)
                      << ", pc=0x" << std::hex << std::uppercase
                      << callback->execution.pc << std::dec;
          }
          std::cout << '\n';
          if (!link_guard) {
            record_driver_blocker("power-317-overlay-link-guard-fault");
          } else if (!callback || !callback->completed()) {
            record_driver_blocker("power-317-overlay-callback-fault");
          }
        }
        if (power_post_transition_updates == 16U &&
            (driver_mission->completed_objectives & 0x04U) == 0U &&
            (driver_mission->revealed_objectives & 0x04U) != 0U &&
            (driver_mission->notified_objectives & 0x04U) != 0U) {
          driver_pad.buttons = 0x1000U;
          std::cout << "driver-triangle: frame=" << frame
                    << ", stage=power-317, phase=revealed\n";
        }
        if (power_post_transition_updates == 20U &&
            (driver_mission->completed_objectives & 0x04U) == 0U &&
            (driver_mission->revealed_objectives & 0x04U) != 0U &&
            (driver_mission->notified_objectives & 0x04U) != 0U) {
          const std::array arguments{
              0x12U, 5U, 317U, 317U, 0U, 0U, 0U, 0U,
          };
          const auto interaction =
              vm.invoke(0x80015364U, arguments, 5'000'000U);
          std::cout << "power-317-event12-continuation: priority=5"
                    << ", phase=revealed"
                    << ", completed=" << interaction.completed() << ", reason="
                    << sf::psx::toString(interaction.execution.reason)
                    << ", pc=0x" << std::hex << std::uppercase
                    << interaction.execution.pc << std::dec << '\n';
          if (!interaction.completed()) {
            record_driver_blocker(
                "power-317-revealed-event12-continuation-fault");
          }
        }
        if (power_post_transition_updates == 24U &&
            (driver_mission->completed_objectives & 0x04U) == 0U &&
            (driver_mission->revealed_objectives & 0x04U) != 0U &&
            (driver_mission->notified_objectives & 0x04U) != 0U) {
          std::uint32_t records{};
          std::uint32_t linked_source{};
          const auto bridge = vm.readBridgeState();
          const auto link_guard =
              vm.runtime().read32(0x80115cccU, records) &&
              vm.runtime().read32(records + 317U * 0x4cU + 0x30U,
                                  linked_source) &&
              linked_source == 68U && bridge && bridge->objects.size() > 68U &&
              bridge->objects[68U].class_id == 0x54;
          const auto player_source =
              driver_mission->player_slot >= 0
                  ? static_cast<std::uint16_t>(driver_mission->player_slot)
                  : std::numeric_limits<std::uint16_t>::max();
          const auto event_completed =
              link_guard &&
              player_source != std::numeric_limits<std::uint16_t>::max() &&
              dispatch_direct_object_event_from(317U, 0x14U, player_source);
          const auto bridge_after_event = vm.readBridgeState();
          const auto mission_after_event = vm.readMissionBridgeState();
          std::uint32_t overlay_flags{};
          static_cast<void>(vm.runtime().read32(0x80149910U, overlay_flags));
          const auto instance_latched =
              bridge_after_event && bridge_after_event->objects.size() > 317U &&
              (bridge_after_event->objects[317U].instance_flags & 0x20U) != 0U;
          std::cout << "probe-power-switch-event14: source=317"
                    << ", link-guard=" << link_guard
                    << ", linked=" << linked_source << ", linked-class=0x"
                    << std::hex << std::uppercase
                    << (bridge && bridge->objects.size() > 68U
                            ? static_cast<std::uint16_t>(
                                  bridge->objects[68U].class_id)
                            : 0xffffU)
                    << ", overlay-flags=0x" << overlay_flags << std::dec
                    << ", completed=" << event_completed
                    << ", instance-latched=" << instance_latched
                    << ", objective2="
                    << (mission_after_event &&
                        (mission_after_event->completed_objectives & 0x04U) !=
                            0U);
          std::cout << '\n';
          if (!link_guard) {
            record_driver_blocker("power-317-switch-link-guard-fault");
          } else if (!event_completed || !instance_latched) {
            record_driver_blocker("power-317-switch-event14-fault");
          }
        }
        break;
      case trigger_192:
        cross_trigger(192U, {-666, -1'198, 4'925}, {-204, -1'198, 4'925});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 192U &&
            (driver_bridge->objects[192U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(5U, 192U));
        }
        break;
      case trigger_193:
        cross_trigger(193U, {143, -131, 4'808}, {336, -131, 4'807});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 193U &&
            (driver_bridge->objects[193U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(6U, 193U));
        }
        break;
      case elevator_315:
        if (!elevator_315_motion_completed) {
          interact_after_settle(315U);
        } else if (!elevator_return_started) {
          ++elevator_passenger_boarding_updates;
          if (elevator_passenger_boarding_updates <= 4U &&
              driver_bridge->objects.size() > 62U) {
            constexpr std::int32_t passenger_root_offset_y = -127;
            const auto &platform = driver_bridge->objects[62U].position;
            applied_host_position = sf::game::LegacyNativePoint{
                platform.x,
                platform.y + passenger_root_offset_y,
                platform.z,
            };
            elevator_passenger_positioned = true;
            if (elevator_passenger_boarding_updates == 1U) {
              std::cout << "probe-elevator-passenger-board: linked=62"
                        << ", platform=(" << platform.x << ',' << platform.y
                        << ',' << platform.z << "), player-root=("
                        << applied_host_position->x << ','
                        << applied_host_position->y << ','
                        << applied_host_position->z << ")\n";
            }
          }
        }
        if (driver_stage_guest_updates == 1U ||
            driver_stage_guest_updates == 12U ||
            driver_stage_guest_updates == 20U ||
            driver_stage_guest_updates == 40U ||
            driver_stage_guest_updates == 80U ||
            driver_stage_guest_updates == 100U ||
            driver_stage_guest_updates == 120U ||
            driver_stage_guest_updates == 140U) {
          trace_driver_object("elevator-315-switch", 315U);
          trace_driver_object("elevator-315-linked", 62U);
        }
        if (driver_stage_guest_updates == 12U &&
            !elevator_315_event14_attempted) {
          elevator_315_event14_attempted = true;
          const auto player_source =
              driver_mission->player_slot >= 0
                  ? static_cast<std::uint16_t>(driver_mission->player_slot)
                  : std::numeric_limits<std::uint16_t>::max();
          const auto guard =
              driver_bridge->objects.size() > 315U &&
              driver_bridge->objects[315U].class_id == 0x24 &&
              driver_bridge->objects[315U].parameter == 0 &&
              driver_bridge->objects[315U].linked_slot == 62 &&
              driver_bridge->objects.size() > 62U &&
              driver_bridge->objects[62U].class_id == 0x0b &&
              (driver_mission->completed_objectives & 0x04U) != 0U &&
              player_source != std::numeric_limits<std::uint16_t>::max();
          elevator_315_event14_completed =
              guard &&
              dispatch_direct_object_event_from(315U, 0x14U, player_source);
          std::cout << "probe-elevator-switch-event14: source=315"
                    << ", guard=" << guard << ", player=" << player_source
                    << ", parameter="
                    << (driver_bridge->objects.size() > 315U
                            ? driver_bridge->objects[315U].parameter
                            : -1)
                    << ", link="
                    << (driver_bridge->objects.size() > 315U
                            ? driver_bridge->objects[315U].linked_slot
                            : -1)
                    << ", linked-class=0x" << std::hex << std::uppercase
                    << (driver_bridge->objects.size() > 62U
                            ? static_cast<std::uint16_t>(
                                  driver_bridge->objects[62U].class_id)
                            : 0U)
                    << std::dec
                    << ", completed=" << elevator_315_event14_completed << '\n';
          if (!guard) {
            record_driver_blocker("elevator-315-switch-event14-guard-fault");
          } else if (!elevator_315_event14_completed) {
            record_driver_blocker("elevator-315-switch-event14-fault");
          }
        }
        break;
      case elevator_316:
        interact_after_settle(316U);
        if (driver_stage_guest_updates == 1U ||
            driver_stage_guest_updates == 12U ||
            driver_stage_guest_updates == 40U ||
            driver_stage_guest_updates == 80U ||
            driver_stage_guest_updates == 120U) {
          trace_driver_object("elevator-316-switch", 316U);
          trace_driver_object("elevator-316-linked", 62U);
        }
        if (driver_stage_guest_updates == 12U &&
            !elevator_316_event14_attempted) {
          elevator_316_event14_attempted = true;
          const auto player_source =
              driver_mission->player_slot >= 0
                  ? static_cast<std::uint16_t>(driver_mission->player_slot)
                  : std::numeric_limits<std::uint16_t>::max();
          const auto guard =
              driver_bridge->objects.size() > 316U &&
              driver_bridge->objects[316U].class_id == 0x24 &&
              driver_bridge->objects[316U].parameter == 0 &&
              driver_bridge->objects[316U].linked_slot == 62 &&
              driver_bridge->objects.size() > 62U &&
              driver_bridge->objects[62U].class_id == 0x0b &&
              player_source != std::numeric_limits<std::uint16_t>::max();
          elevator_316_event14_completed =
              guard &&
              dispatch_direct_object_event_from(316U, 0x14U, player_source);
          std::cout << "probe-elevator-switch-event14: source=316"
                    << ", guard=" << guard << ", player=" << player_source
                    << ", link="
                    << (driver_bridge->objects.size() > 316U
                            ? driver_bridge->objects[316U].linked_slot
                            : -1)
                    << ", linked-class=0x" << std::hex << std::uppercase
                    << (driver_bridge->objects.size() > 62U
                            ? static_cast<std::uint16_t>(
                                  driver_bridge->objects[62U].class_id)
                            : 0U)
                    << std::dec
                    << ", completed=" << elevator_316_event14_completed << '\n';
          if (!guard) {
            record_driver_blocker("elevator-316-switch-event14-guard-fault");
          } else if (!elevator_316_event14_completed) {
            record_driver_blocker("elevator-316-switch-event14-fault");
          }
        }
        break;
      case bomb_28:
        interact_after_settle(28U);
        if (driver_stage_guest_updates == 4U) {
          driver_pad.buttons = 0x1000U;
          std::cout << "driver-triangle: frame=" << frame
                    << ", stage=bomb-28\n";
        }
        if (driver_stage_guest_updates == 8U && !bomb_28_callback_attempted &&
            (driver_mission->completed_objectives & 0x08U) == 0U) {
          bomb_28_callback_attempted = true;
          const auto guard = driver_bridge->objects.size() > 28U &&
                             driver_bridge->objects[28U].class_id == 0x2e;
          std::optional<sf::game::LegacyGameplayVmResult> callback;
          if (guard) {
            callback = vm.invoke(0x801485b8U, std::array{28U}, 5'000'000U);
            bomb_28_callback_completed = callback->completed();
          }
          const auto mission_after_callback = vm.readMissionBridgeState();
          std::cout << "probe-bomb-callback: source=28, guard=" << guard
                    << ", completed=" << bomb_28_callback_completed
                    << ", objective3="
                    << (mission_after_callback &&
                        (mission_after_callback->completed_objectives &
                         0x08U) != 0U);
          if (callback) {
            std::cout << ", reason="
                      << sf::psx::toString(callback->execution.reason)
                      << ", pc=0x" << std::hex << std::uppercase
                      << callback->execution.pc << std::dec;
          }
          std::cout << '\n';
          if (!guard) {
            record_driver_blocker("bomb-28-objective-callback-guard-fault");
          } else if (!bomb_28_callback_completed) {
            record_driver_blocker("bomb-28-objective-callback-fault");
          }
        }
        break;
      case trigger_191:
        cross_trigger(191U, {-647, -1'183, 3'382}, {12, -1'183, 3'382});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 191U &&
            (driver_bridge->objects[191U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(7U, 191U));
        }
        break;
      case trigger_258:
        cross_trigger(258U, {685, -24, -10'492}, {1'102, -24, -10'497});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 258U &&
            (driver_bridge->objects[258U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(8U, 258U));
        }
        break;
      case station_318:
        if (!station_318_scripted_transition_completed) {
          hold_source(318U);
          break;
        }
        interact_after_settle(318U);
        if (driver_stage_guest_updates == 1U ||
            driver_stage_guest_updates == 12U ||
            driver_stage_guest_updates == 40U ||
            driver_stage_guest_updates == 80U ||
            driver_stage_guest_updates == 120U) {
          trace_driver_object("station-318-switch", 318U);
          trace_driver_object("station-318-linked", 342U);
        }
        if (driver_stage_guest_updates == 12U &&
            !station_318_event14_attempted) {
          station_318_event14_attempted = true;
          const auto player_source =
              driver_mission->player_slot >= 0
                  ? static_cast<std::uint16_t>(driver_mission->player_slot)
                  : std::numeric_limits<std::uint16_t>::max();
          const auto guard =
              driver_bridge->objects.size() > 342U &&
              driver_bridge->objects[318U].class_id == 0x24 &&
              driver_bridge->objects[318U].linked_slot == 342 &&
              driver_bridge->objects[342U].class_id == 0x0b &&
              player_source != std::numeric_limits<std::uint16_t>::max();
          std::optional<sf::game::LegacyGameplayVmResult> event;
          if (guard) {
            const std::array arguments{
                0x14U, 3U, static_cast<std::uint32_t>(player_source),
                318U,  0U, 0U,
                0U,    0U,
            };
            event = vm.invoke(0x80015364U, arguments, 5'000'000U);
            station_318_event14_completed = event->completed();
          }
          std::cout << "probe-station-switch-event14: source=318"
                    << ", mode=event-entry, guard=" << guard
                    << ", completed=" << station_318_event14_completed << '\n';
          if (!guard || !station_318_event14_completed) {
            record_driver_blocker("station-318-event14-queue-fault");
          }
        }
        if (driver_stage_guest_updates == 20U && !station_318_motion_started &&
            !station_318_handler_fallback_attempted) {
          station_318_handler_fallback_attempted = true;
          const auto player_source = static_cast<std::uint16_t>(
              std::max<std::int16_t>(driver_mission->player_slot, 0));
          const auto completed =
              dispatch_direct_object_event_from(318U, 0x14U, player_source);
          std::cout << "probe-station-switch-event14: source=318"
                    << ", mode=direct-handler, completed=" << completed << '\n';
          if (!completed) {
            record_driver_blocker("station-318-event14-handler-fault");
          }
        }
        break;
      case station_319:
        interact_after_settle(319U);
        if (driver_stage_guest_updates == 1U ||
            driver_stage_guest_updates == 12U ||
            driver_stage_guest_updates == 40U ||
            driver_stage_guest_updates == 80U ||
            driver_stage_guest_updates == 120U) {
          trace_driver_object("station-319-switch", 319U);
          trace_driver_object("station-319-linked", 342U);
        }
        if (driver_stage_guest_updates == 12U &&
            !station_319_event14_attempted) {
          station_319_event14_attempted = true;
          const auto player_source =
              driver_mission->player_slot >= 0
                  ? static_cast<std::uint16_t>(driver_mission->player_slot)
                  : std::numeric_limits<std::uint16_t>::max();
          const auto guard =
              driver_bridge->objects.size() > 342U &&
              driver_bridge->objects[319U].class_id == 0x7b &&
              driver_bridge->objects[319U].linked_slot == 342 &&
              driver_bridge->objects[342U].class_id == 0x0b &&
              player_source != std::numeric_limits<std::uint16_t>::max();
          std::optional<sf::game::LegacyGameplayVmResult> event;
          if (guard) {
            const std::array arguments{
                0x14U, 3U, static_cast<std::uint32_t>(player_source),
                319U,  0U, 0U,
                0U,    0U,
            };
            event = vm.invoke(0x80015364U, arguments, 5'000'000U);
            station_319_event14_completed = event->completed();
          }
          std::cout << "probe-station-switch-event14: source=319"
                    << ", mode=event-entry, guard=" << guard
                    << ", completed=" << station_319_event14_completed << '\n';
          if (!guard || !station_319_event14_completed) {
            record_driver_blocker("station-319-event14-queue-fault");
          }
        }
        if (driver_stage_guest_updates == 20U && !station_319_motion_started &&
            !station_319_handler_fallback_attempted) {
          station_319_handler_fallback_attempted = true;
          const auto player_source = static_cast<std::uint16_t>(
              std::max<std::int16_t>(driver_mission->player_slot, 0));
          const auto completed =
              dispatch_direct_object_event_from(319U, 0x14U, player_source);
          std::cout << "probe-station-switch-event14: source=319"
                    << ", mode=direct-handler, completed=" << completed << '\n';
          if (!completed) {
            record_driver_blocker("station-319-event14-handler-fault");
          }
        }
        break;
      case trigger_259:
        cross_trigger(259U, {-676, 548, -11'943}, {-7, 548, -11'940});
        if (driver_stage_guest_updates == 8U &&
            driver_bridge->objects.size() > 259U &&
            (driver_bridge->objects[259U].attributes & 0x20U) == 0U) {
          static_cast<void>(dispatch_trigger_fallback(9U, 259U));
        }
        break;
      case finale_30:
        // Source 30 is the protected lower subway bomb.  Its death callback
        // is mission failure; the retail success callback is proximity-only
        // after the upper bomb has been tagged.  Cross the 900-unit radius
        // from a real outside sample and let the overlay own the ending.
        {
          const auto inside = source_host_position(30U);
          auto outside = inside;
          outside.x -= 1'200;
          cross_trigger(30U, outside, inside);
          if (driver_stage_guest_updates == 1U ||
              driver_stage_guest_updates == 3U) {
            const auto &position =
                driver_stage_guest_updates == 1U ? outside : inside;
            std::cout << "probe-finale-proximity: source=30, phase="
                      << (driver_stage_guest_updates == 1U ? "outside"
                                                           : "inside")
                      << ", position=(" << position.x << ',' << position.y
                      << ',' << position.z << ")\n";
          }
          if (driver_stage_guest_updates == 8U && !finale_callback_attempted) {
            finale_callback_attempted = true;
            const auto guard =
                driver_bridge->objects.size() > 30U &&
                driver_bridge->objects[30U].class_id == 0x58 &&
                driver_bridge->objects[30U].linked_slot == -1 &&
                (driver_mission->completed_objectives & 0x08U) != 0U;
            std::optional<sf::game::LegacyGameplayVmResult> callback;
            if (guard) {
              callback.emplace(
                  vm.invoke(0x801488c8U, std::array{30U}, 5'000'000U));
              finale_callback_completed = callback->completed();
            }
            std::cout << "probe-finale-callback: source=30, guard=" << guard
                      << ", completed=" << finale_callback_completed;
            if (callback) {
              std::cout << ", reason="
                        << sf::psx::toString(callback->execution.reason)
                        << ", pc=0x" << std::hex << std::uppercase
                        << callback->execution.pc << std::dec;
            }
            std::cout << '\n';
            if (!guard) {
              record_driver_blocker("finale-overlay-callback-guard-fault");
            } else if (!finale_callback_completed) {
              record_driver_blocker("finale-overlay-callback-fault");
            }
          }
        }
        break;
      }
    }
    if (applied_host_position) {
      const auto previous_position = previous_applied_host_position
                                         ? *previous_applied_host_position
                                         : *applied_host_position;
      const sf::game::LegacyHostPlayerState player{
          *applied_host_position,
          0,
          150,
          600,
          previous_position,
          previous_applied_host_position.has_value(),
      };
      if (!vm.writeHostPlayerState(player)) {
        std::cout << "legacy-level-player-bridge-fault: frame=" << frame
                  << '\n';
        return 31;
      }
      previous_applied_host_position = *applied_host_position;
    }
    if (room_bootstrap_source && !force_driver_room(*room_bootstrap_source)) {
      record_driver_blocker("source-" + std::to_string(*room_bootstrap_source) +
                            "-room-activation-fault");
    }
    if (!vm.writeHostPadState(driver_pad)) {
      std::cout << "legacy-level-pad-bridge-fault: frame=" << frame << '\n';
      return 31;
    }
    std::uint32_t state_after_driver{};
    if (!vm.runtime().read32(0x80115c78U, state_after_driver)) {
      return 31;
    }
    if (state_after_driver == 2U && !non_gameplay_transition_traced) {
      non_gameplay_transition_traced = true;
      std::uint32_t next_state{};
      std::uint32_t state_depth{};
      std::uint32_t movie_callback{};
      std::uint32_t loader_callback{};
      std::uint16_t fade_step{};
      std::uint16_t fade_current{};
      std::uint32_t fade_callback{};
      std::uint8_t terminal{};
      std::uint8_t success_latch{};
      std::uint8_t transition_latch{};
      std::uint8_t failure{};
      std::uint8_t completed{};
      const auto raw = vm.runtime().read32(0x80115c7cU, next_state) &&
                       vm.runtime().read32(0x80115c74U, state_depth) &&
                       vm.runtime().read32(0x80115c80U, movie_callback) &&
                       vm.runtime().read32(0x80116b04U, loader_callback) &&
                       vm.runtime().read16(0x801164d8U, fade_step) &&
                       vm.runtime().read16(0x801164daU, fade_current) &&
                       vm.runtime().read32(0x801164e0U, fade_callback) &&
                       vm.runtime().read8(0x80115cc8U, terminal) &&
                       vm.runtime().read8(0x80115cc9U, success_latch) &&
                       vm.runtime().read8(0x80115ccaU, transition_latch) &&
                       vm.runtime().read8(0x80116b24U, failure) &&
                       vm.runtime().read8(0x80116b25U, completed);
      const auto transition_mission = vm.readMissionBridgeState();
      std::cout << "non-gameplay-transition: frame=" << frame
                << ", stage=" << legacyLevelDriverStageName(driver_stage)
                << ", state=" << state_after_driver << '/' << next_state
                << ", depth=" << state_depth << ", raw=" << raw << ':'
                << static_cast<unsigned int>(terminal) << '/'
                << static_cast<unsigned int>(success_latch) << '/'
                << static_cast<unsigned int>(transition_latch) << '/'
                << static_cast<unsigned int>(failure) << '/'
                << static_cast<unsigned int>(completed) << ", progress=";
      if (transition_mission) {
        std::cout << "0x" << std::hex
                  << transition_mission->completed_objectives << "/0x"
                  << transition_mission->revealed_objectives << "/0x"
                  << transition_mission->notified_objectives << "/0x"
                  << transition_mission->parameter_mask << std::dec
                  << ", outcome=" << transition_mission->terminal << '/'
                  << transition_mission->success << '/'
                  << transition_mission->failure;
      } else {
        std::cout << "unavailable";
      }
      std::cout << ", fade=0x" << std::hex << fade_current << "/0x" << fade_step
                << "/0x" << fade_callback << ", callbacks=0x" << loader_callback
                << "/0x" << movie_callback << std::dec << '\n';
    }
    if (state_after_driver == 2U) {
      std::uint32_t radio_state{};
      std::uint16_t radio_event{};
      std::uint16_t radio_object{};
      std::uint16_t radio_id{};
      std::uint16_t prompt{};
      std::uint8_t transition_byte{};
      std::uint8_t terminal_latch{};
      std::uint8_t success_latch{};
      std::uint8_t failure_latch{};
      std::uint8_t completed_latch{};
      std::array<std::uint32_t, 9U> bindings{};
      auto state2_trace_read =
          vm.runtime().read32(0x80128dacU, radio_state) &&
          vm.runtime().read16(0x80128db0U, radio_event) &&
          vm.runtime().read16(0x80128db2U, radio_object) &&
          vm.runtime().read16(0x80128db4U, radio_id) &&
          vm.runtime().read16(0x80116374U, prompt) &&
          vm.runtime().read8(0x80115ccaU, transition_byte) &&
          vm.runtime().read8(0x80115cc8U, terminal_latch) &&
          vm.runtime().read8(0x80115cc9U, success_latch) &&
          vm.runtime().read8(0x80116b24U, failure_latch) &&
          vm.runtime().read8(0x80116b25U, completed_latch);
      for (std::size_t index = 0U; index < bindings.size(); ++index) {
        state2_trace_read =
            state2_trace_read &&
            vm.runtime().read32(0x8010baf4U +
                                    static_cast<std::uint32_t>(index) * 4U,
                                bindings[index]);
      }
      std::cout << "probe-state2-retail-input: stage="
                << legacyLevelDriverStageName(driver_stage)
                << ", raw=" << state2_trace_read << ", radio=0x" << std::hex
                << std::uppercase << radio_state << "/0x" << radio_event
                << "/0x" << radio_object << "/0x" << radio_id << ", prompt=0x"
                << prompt << ", transition=0x"
                << static_cast<unsigned int>(transition_byte)
                << ", outcome-latches="
                << static_cast<unsigned int>(terminal_latch) << '/'
                << static_cast<unsigned int>(success_latch) << '/'
                << static_cast<unsigned int>(failure_latch) << '/'
                << static_cast<unsigned int>(completed_latch)
                << ", binding-8010BAF4=[";
      for (std::size_t index = 0U; index < bindings.size(); ++index) {
        std::cout << (index == 0U ? "" : ",") << "0x" << bindings[index];
      }
      std::cout << ']';
      if (driver_bridge && driver_bridge->objects.size() > 30U) {
        const auto &source_30 = driver_bridge->objects[30U];
        std::cout << ", source30=0x" << std::hex
                  << static_cast<std::uint16_t>(source_30.class_id) << "/0x"
                  << source_30.attributes << std::dec << '/' << source_30.health
                  << '/' << source_30.parameter << '/' << source_30.linked_slot;
      } else {
        std::cout << ", source30=unavailable";
      }
      std::cout << std::dec << '\n';
      const auto transition_mission = vm.readMissionBridgeState();
      const auto dispatch_allowed =
          transition_mission && sf::game::legacyRetailState2DispatchAllowed(
                                    state_after_driver, *transition_mission);
      const auto transition =
          dispatch_allowed ? vm.dispatchRetailState2Transition()
                           : sf::game::LegacyRetailState2TransitionResult{};
      if (dispatch_allowed) {
        state_after_driver = transition.final_state;
      }
      std::cout << "probe-state2-dispatch: stage="
                << legacyLevelDriverStageName(driver_stage)
                << ", allowed=" << dispatch_allowed << ", completed="
                << (dispatch_allowed && transition.completed())
                << ", dispatches=" << transition.dispatches
                << ", final-state=" << transition.final_state << '\n';
      if (dispatch_allowed && transition.completed() &&
          driver_stage == LegacyLevelDriverStage::power_317) {
        power_scripted_transition_completed = true;
        power_post_transition_updates = 0U;
        driver_stage_frames = 0U;
        driver_stage_guest_updates = 0U;
        driver_stage_entry_trace_frame = frame;
        previous_applied_host_position.reset();
      } else if (dispatch_allowed && transition.completed() &&
                 driver_stage == LegacyLevelDriverStage::station_318 &&
                 !station_318_scripted_transition_completed) {
        station_318_scripted_transition_completed = true;
        station_318_event14_attempted = false;
        station_318_event14_completed = false;
        station_318_handler_fallback_attempted = false;
        station_318_motion_started = false;
        station_318_motion_completed = false;
        driver_stage_frames = 0U;
        driver_stage_guest_updates = 0U;
        driver_stage_entry_trace_frame = frame;
        previous_applied_host_position.reset();
      } else if (dispatch_allowed && !transition.completed()) {
        record_driver_blocker("state2-transition-dispatch-fault");
      }
    }
    const auto driver_gameplay_state =
        state_after_driver == 0U || state_after_driver == 5U;
    // The production loop always enters through the retail outer frame.  Keep
    // the whole trigger/finale tail on that path so the proximity scan and
    // overlay callback scheduling remain coherent.
    const auto finale_retail_proximity_stage =
        driver_stage == LegacyLevelDriverStage::trigger_259 ||
        driver_stage == LegacyLevelDriverStage::finale_30;
    const auto use_native_tick = native_driven && driver_gameplay_state &&
                                 !finale_retail_proximity_stage;
    const auto result = use_native_tick ? vm.tickNativeDrivenGameplayFrame()
                                        : vm.tickRetailOuterFrame();
    use_native_tick ? ++native_updates : ++outer_updates;
    if (!result.completed()) {
      std::cout << "legacy-level-frame-fault: frame=" << frame
                << ", state=" << result.state_before << '/'
                << result.state_after << ", bridge=" << result.bridge_fault
                << ", unsupported=" << result.unsupported_state;
      if (!result.guest_calls.empty()) {
        const auto &execution = result.guest_calls.back().execution;
        std::cout << ", reason=" << sf::psx::toString(execution.reason)
                  << ", pc=0x" << std::hex << std::uppercase << execution.pc
                  << std::dec;
      }
      std::cout << '\n';
      return 30;
    }
    if (!vm.advanceAudioFrameClock()) {
      std::cout << "legacy-level-audio-fault: frame=" << frame << '\n';
      return 30;
    }
    ++completed_frames;

    const auto bridge = vm.readBridgeState();
    const auto mission_state = vm.readMissionBridgeState();
    if (!bridge || !mission_state || bridge->objects.empty()) {
      std::cout << "legacy-level-state-bridge-fault: frame=" << frame << '\n';
      return 31;
    }
    if (slots.empty()) {
      slots.resize(bridge->objects.size());
      dynamic_first_slot = bridge->dynamic_first_slot;
    } else if (slots.size() != bridge->objects.size() ||
               dynamic_first_slot != bridge->dynamic_first_slot) {
      std::cout << "legacy-level-object-table-changed: frame=" << frame
                << ", objects=" << slots.size() << "->"
                << bridge->objects.size() << ", dynamic=" << dynamic_first_slot
                << "->" << bridge->dynamic_first_slot << '\n';
      return 31;
    }

    if (!first_mission) {
      first_mission = *mission_state;
    }
    if (!previous_mission ||
        !sameLegacyMissionState(*previous_mission, *mission_state)) {
      ++mission_transitions;
      std::cout << "mission-transition: frame=" << frame
                << ", player=" << mission_state->player_slot
                << ", hp=" << mission_state->player_health << ", objectives=0x"
                << std::hex << mission_state->completed_objectives << "/0x"
                << mission_state->revealed_objectives << "/0x"
                << mission_state->notified_objectives << ", parameters=0x"
                << mission_state->parameter_mask << std::dec
                << ", success=" << mission_state->success
                << ", terminal=" << mission_state->terminal
                << ", failure=" << mission_state->failure << '\n';
      previous_mission = *mission_state;
    }
    last_mission = *mission_state;

    std::uint8_t checkpoint_latch{};
    std::uint32_t checkpoint_frame{};
    std::uint32_t pending_events{};
    std::uint32_t ready_events{};
    if (!vm.runtime().read8(0x801163b1U, checkpoint_latch) ||
        !vm.runtime().read32(0x80121950U, checkpoint_frame) ||
        !vm.runtime().read32(0x80116c68U, pending_events) ||
        !vm.runtime().read32(0x8011775cU, ready_events)) {
      return 31;
    }
    maximum_pending_events = std::max(maximum_pending_events, pending_events);
    maximum_ready_events = std::max(maximum_ready_events, ready_events);
    if (checkpoint_latch != 0U &&
        (!last_checkpoint_frame ||
         *last_checkpoint_frame != checkpoint_frame)) {
      ++checkpoints;
      last_checkpoint_frame = checkpoint_frame;
      std::cout << "checkpoint: trace-frame=" << frame
                << ", retail-frame=" << checkpoint_frame << '\n';
    }

    if (!native_driven && bridge->objects.size() > 35U &&
        bridge->objects[35U].health <= 0) {
      native_driven = true;
      opening_complete_frame = frame;
      std::cout << "opening-complete: frame=" << frame << '\n';
      enter_driver_stage(LegacyLevelDriverStage::clear_opening, frame);
    }
    if (native_driven && mission_state->player_slot >= 0 &&
        static_cast<std::size_t>(mission_state->player_slot) <
            bridge->objects.size()) {
      const auto &player =
          bridge->objects[static_cast<std::size_t>(mission_state->player_slot)];
      if (!post_opening_player_origin) {
        post_opening_player_origin = player.position;
      } else if (!sameLegacyPoint(*post_opening_player_origin,
                                  player.position)) {
        post_opening_player_moved = true;
      }
    }

    for (const auto &object : bridge->objects) {
      auto &trace = slots[object.slot];
      const auto allocated = legacyActorAllocated(object, dynamic_first_slot);
      if (!allocated) {
        if (trace.lifetime) {
          lifetimes[*trace.lifetime].retired = true;
          trace.lifetime.reset();
        }
        continue;
      }
      const LegacyLevelActorIdentity identity{
          object.class_id,          object.definition,   object.parameter,
          object.authored_position, object.path_pointer,
      };
      const auto restarted = trace.lifetime &&
                             lifetimes[*trace.lifetime].died &&
                             object.health > 0;
      if (!trace.lifetime || restarted ||
          !sameLegacyActorIdentity(lifetimes[*trace.lifetime].identity,
                                   identity)) {
        if (trace.lifetime) {
          lifetimes[*trace.lifetime].retired = true;
          trace.lifetime.reset();
        }
        begin_lifetime(object, frame);
      }

      auto &lifetime = lifetimes[*trace.lifetime];
      const auto pose_fingerprint = legacyPoseFingerprint(object);
      const auto target = object.has_target ? object.target_slot : -1;
      lifetime.last_frame = frame;
      lifetime.minimum_health =
          std::min(lifetime.minimum_health, object.health);
      lifetime.end_health = object.health;
      lifetime.saw_positive_health =
          lifetime.saw_positive_health || object.health > 0;
      lifetime.died =
          lifetime.died || (lifetime.saw_positive_health && object.health <= 0);
      lifetime.moved = lifetime.moved ||
                       !sameLegacyPoint(lifetime.end_position, object.position);
      lifetime.end_position = object.position;
      lifetime.saw_target = lifetime.saw_target || object.has_target;
      lifetime.target_changes += target != lifetime.last_target ? 1U : 0U;
      lifetime.last_target = static_cast<std::int16_t>(target);
      lifetime.fire_frames += object.ai_fire_latch != 0U ? 1U : 0U;
      lifetime.simulated_frames += object.simulated ? 1U : 0U;
      lifetime.exact_pose_frames +=
          object.bone_matrix_count == sf::game::legacy_actor_bone_count ? 1U
                                                                        : 0U;
      if (pose_fingerprint != lifetime.last_pose_fingerprint) {
        ++lifetime.animation_changes;
        lifetime.current_stagnant_combat_frames = 0U;
      } else if (object.simulated && object.has_target && object.health > 0) {
        ++lifetime.current_stagnant_combat_frames;
        lifetime.longest_stagnant_combat_frames =
            std::max(lifetime.longest_stagnant_combat_frames,
                     lifetime.current_stagnant_combat_frames);
      } else {
        lifetime.current_stagnant_combat_frames = 0U;
      }
      lifetime.last_pose_fingerprint = pose_fingerprint;
      std::uint32_t raw_ground_contact{};
      const auto has_raw_ground_contact =
          object.motion_controller != 0U &&
          vm.runtime().read32(object.motion_controller + 0x12cU,
                              raw_ground_contact);
      const auto packed_ground_sentinel =
          has_raw_ground_contact &&
          (raw_ground_contact & 0xfffffffcu) == 0x80000000U;
      if (packed_ground_sentinel) {
        ++lifetime.packed_ground_sentinel_frames;
        ++packed_ground_sentinel_samples;
      }
      const auto dump_ground_sample =
          (packed_ground_sentinel && !raw_ground_sentinel_dumped) ||
          (!packed_ground_sentinel && raw_ground_dumps < 2U);
      if (native_driven && object.motion_controller != 0U &&
          raw_ground_dumps < 3U && dump_ground_sample) {
        std::array<std::uint32_t, 7U> words{};
        auto complete = true;
        for (std::size_t word = 0U; word < words.size(); ++word) {
          complete = complete && vm.runtime().read32(
                                     object.motion_controller + 0x120U +
                                         static_cast<std::uint32_t>(word * 4U),
                                     words[word]);
        }
        if (complete) {
          std::cout << "motion-ground-raw: frame=" << frame
                    << ", slot=" << object.slot << ", motion=0x" << std::hex
                    << std::uppercase << object.motion_controller << ", words=";
          for (const auto word : words) {
            std::cout << " 0x" << word;
          }
          std::cout << std::dec
                    << ", decoded-valid=" << object.ground_contact_valid
                    << ", decoded-y=" << object.ground_contact_y
                    << ", packed-sentinel=" << packed_ground_sentinel << '\n';
          ++raw_ground_dumps;
          raw_ground_sentinel_dumped =
              raw_ground_sentinel_dumped || packed_ground_sentinel;
        }
      }
      if (object.ground_contact_valid && !packed_ground_sentinel) {
        ++lifetime.ground_frames;
        auto delta = static_cast<std::int64_t>(object.position.y) -
                     static_cast<std::int64_t>(object.ground_contact_y);
        if (delta < 0) {
          delta = -delta;
        }
        lifetime.maximum_ground_delta =
            std::max(lifetime.maximum_ground_delta, delta);
      }
      if (object.has_target && (object.target_slot < 0 ||
                                static_cast<std::size_t>(object.target_slot) >=
                                    bridge->objects.size())) {
        ++invalid_targets;
        std::cout << "invalid-target: frame=" << frame
                  << ", slot=" << object.slot
                  << ", target=" << object.target_slot << '\n';
      }
    }

    std::uint32_t camera_controller{};
    std::uint32_t camera_mode{};
    std::uint32_t camera_lock{};
    if (!vm.runtime().read32(0x80115d84U, camera_controller) ||
        !vm.runtime().read32(0x801191ecU, camera_mode) ||
        !vm.runtime().read32(0x801169e0U, camera_lock)) {
      return 31;
    }
    if (!previous_camera_controller ||
        *previous_camera_controller != camera_controller) {
      camera_controller_changes += previous_camera_controller ? 1U : 0U;
      std::cout << "camera-owner: frame=" << frame << ", controller=0x"
                << std::hex << std::uppercase << camera_controller << std::dec
                << ", driver=" << legacyLevelDriverStageName(driver_stage)
                << '\n';
      previous_camera_controller = camera_controller;
    }
    if (!previous_camera_mode || *previous_camera_mode != camera_mode ||
        !previous_camera_lock || *previous_camera_lock != camera_lock) {
      std::cout << "camera-mode: frame=" << frame << ", mode=0x" << std::hex
                << std::uppercase << camera_mode << ", lock=0x" << camera_lock
                << std::dec
                << ", driver=" << legacyLevelDriverStageName(driver_stage)
                << '\n';
      previous_camera_mode = camera_mode;
      previous_camera_lock = camera_lock;
    }
    if (native_driven && camera_mode == 0x0bU) {
      scripted_camera_rail_seen = true;
      ++scripted_camera_rail_frames;
    }
    if (previous_camera_eye &&
        !sameLegacyPoint(*previous_camera_eye, bridge->camera.eye)) {
      const auto absolute = [](std::int32_t value) {
        return value < 0 ? -static_cast<std::int64_t>(value)
                         : static_cast<std::int64_t>(value);
      };
      const auto delta =
          absolute(bridge->camera.eye.x - previous_camera_eye->x) +
          absolute(bridge->camera.eye.y - previous_camera_eye->y) +
          absolute(bridge->camera.eye.z - previous_camera_eye->z);
      if (delta > 2'000) {
        if (camera_discontinuities < 16U) {
          std::cout << "camera-discontinuity: frame=" << frame
                    << ", delta=" << delta << ", eye=("
                    << previous_camera_eye->x << ',' << previous_camera_eye->y
                    << ',' << previous_camera_eye->z << ")->("
                    << bridge->camera.eye.x << ',' << bridge->camera.eye.y
                    << ',' << bridge->camera.eye.z
                    << "), driver=" << legacyLevelDriverStageName(driver_stage)
                    << '\n';
        }
        ++camera_discontinuities;
      }
    }
    previous_camera_eye = bridge->camera.eye;

    const sf::game::LegacyObjectBridgeState *player{};
    if (mission_state->player_slot >= 0 &&
        static_cast<std::size_t>(mission_state->player_slot) <
            bridge->objects.size()) {
      player =
          &bridge
               ->objects[static_cast<std::size_t>(mission_state->player_slot)];
    }
    if (applied_host_position && player != nullptr &&
        !sameLegacyPoint(*applied_host_position, player->position)) {
      if (player_position_overrides < 12U) {
        std::cout << "player-script-override: frame=" << frame << ", expected=("
                  << applied_host_position->x << ',' << applied_host_position->y
                  << ',' << applied_host_position->z << "), actual=("
                  << player->position.x << ',' << player->position.y << ','
                  << player->position.z
                  << "), driver=" << legacyLevelDriverStageName(driver_stage)
                  << '\n';
      }
      ++player_position_overrides;
    }
    if (driver_stage == LegacyLevelDriverStage::elevator_315 &&
        bridge->objects.size() > 62U) {
      const auto moving = (bridge->objects[62U].instance_flags & 0x08U) != 0U;
      if (!elevator_315_motion_completed) {
        elevator_315_motion_started = elevator_315_motion_started || moving;
        if (elevator_315_motion_started && !moving &&
            driver_stage_guest_updates >= 12U) {
          elevator_315_motion_completed = true;
          std::cout << "scripted-elevator-motion: switch=315, phase=lower"
                    << ", linked=62, position=("
                    << bridge->objects[62U].position.x << ','
                    << bridge->objects[62U].position.y << ','
                    << bridge->objects[62U].position.z << ")\n";
        }
      } else if (elevator_passenger_positioned) {
        if (!elevator_passenger_board_y && player != nullptr) {
          elevator_passenger_board_y = player->position.y;
        }
        if (!elevator_return_started && moving) {
          elevator_return_started = true;
          std::cout
              << "scripted-elevator-motion: switch=315, phase=return-start"
              << ", linked=62, platform-y=" << bridge->objects[62U].position.y
              << ", player-y=" << (player != nullptr ? player->position.y : 0)
              << '\n';
        }
        if (elevator_return_started && elevator_passenger_board_y &&
            player != nullptr &&
            std::abs(static_cast<std::int64_t>(player->position.y) -
                     *elevator_passenger_board_y) > 512) {
          elevator_passenger_carried = true;
        }
        if (elevator_return_started && !moving && !elevator_return_completed) {
          elevator_return_completed = true;
          std::cout << "scripted-elevator-motion: switch=315, phase=return-end"
                    << ", linked=62, position=("
                    << bridge->objects[62U].position.x << ','
                    << bridge->objects[62U].position.y << ','
                    << bridge->objects[62U].position.z << "), player-y="
                    << (player != nullptr ? player->position.y : 0)
                    << ", passenger-carried=" << elevator_passenger_carried
                    << '\n';
        }
      }
    }
    if (driver_stage == LegacyLevelDriverStage::elevator_316 &&
        bridge->objects.size() > 62U) {
      const auto moving = (bridge->objects[62U].instance_flags & 0x08U) != 0U;
      elevator_316_motion_started = elevator_316_motion_started || moving;
      if (elevator_316_motion_started && !moving &&
          driver_stage_guest_updates >= 12U && !elevator_316_motion_completed) {
        elevator_316_motion_completed = true;
        std::cout << "scripted-elevator-motion: switch=316, phase=upper"
                  << ", linked=62, position=("
                  << bridge->objects[62U].position.x << ','
                  << bridge->objects[62U].position.y << ','
                  << bridge->objects[62U].position.z << ")\n";
      }
    }
    if ((driver_stage == LegacyLevelDriverStage::station_318 ||
         driver_stage == LegacyLevelDriverStage::station_319) &&
        bridge->objects.size() > 342U) {
      const auto moving = (bridge->objects[342U].instance_flags & 0x08U) != 0U;
      auto &motion_started = driver_stage == LegacyLevelDriverStage::station_318
                                 ? station_318_motion_started
                                 : station_319_motion_started;
      auto &motion_completed =
          driver_stage == LegacyLevelDriverStage::station_318
              ? station_318_motion_completed
              : station_319_motion_completed;
      motion_started = motion_started || moving;
      if (driver_stage == LegacyLevelDriverStage::station_318 &&
          station_318_event14_completed && !motion_started && !moving &&
          driver_stage_guest_updates >= 24U && !motion_completed &&
          std::abs(static_cast<std::int64_t>(bridge->objects[342U].position.y) -
                   bridge->objects[318U].position.y) <= 256) {
        motion_completed = true;
        std::cout << "scripted-station-motion: switch=318"
                  << ", linked=342, already-at-endpoint=1, position=("
                  << bridge->objects[342U].position.x << ','
                  << bridge->objects[342U].position.y << ','
                  << bridge->objects[342U].position.z << ")\n";
      }
      if (motion_started && !moving && driver_stage_guest_updates >= 12U &&
          !motion_completed) {
        motion_completed = true;
        std::cout << "scripted-station-motion: switch="
                  << (driver_stage == LegacyLevelDriverStage::station_318
                          ? 318U
                          : 319U)
                  << ", linked=342, position=("
                  << bridge->objects[342U].position.x << ','
                  << bridge->objects[342U].position.y << ','
                  << bridge->objects[342U].position.z << ")\n";
      }
    }

    if (driver_stage == LegacyLevelDriverStage::intro_157 &&
        (current_state == 9U || result.state_before == 9U ||
         result.state_after == 9U)) {
      intro_state9_seen = true;
    }
    if (driver_stage == LegacyLevelDriverStage::intro_157 &&
        intro_state9_seen &&
        (result.state_after == 0U || result.state_after == 5U)) {
      intro_state9_returned = true;
    }
    if (driver_stage == LegacyLevelDriverStage::finale_30 &&
        finale_callback_completed &&
        (current_state == 9U || result.state_before == 9U ||
         result.state_after == 9U)) {
      finale_state9_seen = true;
    }
    if (driver_stage == LegacyLevelDriverStage::finale_30 &&
        finale_state9_seen &&
        (result.state_after == 0U || result.state_after == 5U)) {
      finale_state9_returned = true;
    }

    const auto mark_trigger = [&](std::uint16_t source, std::size_t index) {
      trigger_visited[index] = true;
      if (source < bridge->objects.size()) {
        const auto &trigger = bridge->objects[source];
        trigger_observed[index] =
            trigger_observed[index] || (trigger.attributes & 0x20U) != 0U;
        std::cout << "trigger-state: source=" << source
                  << ", resident=" << trigger.resident
                  << ", simulated=" << trigger.simulated
                  << ", hp=" << trigger.health << ", attributes=0x" << std::hex
                  << trigger.attributes << std::dec << ", instance="
                  << static_cast<unsigned int>(trigger.instance_state[0]) << '/'
                  << static_cast<unsigned int>(trigger.instance_state[1]) << '/'
                  << static_cast<unsigned int>(trigger.instance_state[2]) << '/'
                  << static_cast<unsigned int>(trigger.instance_state[3])
                  << '\n';
        if (!trigger_observed[index]) {
          record_driver_blocker("source-" + std::to_string(source) +
                                "-trigger-not-activated");
        }
      }
    };
    const auto source_dead = [&](std::uint16_t source) {
      return source < bridge->objects.size() &&
             bridge->objects[source].health <= 0;
    };
    const auto enter_after_timeout = [&](std::string blocker,
                                         LegacyLevelDriverStage) {
      record_driver_blocker(std::move(blocker));
    };
    using enum LegacyLevelDriverStage;
    switch (driver_stage) {
    case waiting_opening:
    case failure_branch:
    case complete:
      break;
    case clear_opening: {
      const auto live_opening_hostile = std::ranges::any_of(
          bridge->objects,
          [&](const sf::game::LegacyObjectBridgeState &object) {
            return object.class_id == 1 && object.simulated &&
                   object.health > 0 && !object_matches_source(object, 174U) &&
                   !object_matches_source(object, 175U);
          });
      if (!live_opening_hostile && driver_stage_guest_updates >= 8U) {
        enter_driver_stage(trigger_256, frame);
      } else if (driver_stage_frames >= 120U) {
        enter_after_timeout("opening-hostile-clear-timeout", trigger_256);
      }
      break;
    }
    case trigger_256:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(256U, 0U);
        enter_driver_stage(passage_64, frame);
      }
      break;
    case passage_64:
      if (driver_stage_guest_updates >= 20U) {
        enter_driver_stage(passage_65, frame);
      }
      break;
    case passage_65:
      if (driver_stage_guest_updates >= 20U) {
        enter_driver_stage(trigger_257, frame);
      }
      break;
    case trigger_257:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(257U, 1U);
        enter_driver_stage(intro_157, frame);
      }
      break;
    case intro_157:
      if (intro_state9_seen && intro_state9_returned) {
        enter_driver_stage(lock_140, frame);
      } else if (!intro_state9_seen && driver_stage_guest_updates >= 60U) {
        enter_after_timeout("source-157-state9-not-triggered", lock_140);
      } else if (intro_state9_seen && !intro_state9_returned &&
                 driver_stage_frames >= 240U) {
        record_driver_blocker("source-157-state9-did-not-return");
      }
      break;
    case lock_140: {
      std::uint8_t gate_instance_flags{};
      const auto gate_state_available =
          bridge->objects.size() > 140U &&
          vm.runtime().read8(bridge->objects[67U].instance,
                             gate_instance_flags);
      if (gate_state_available &&
          (bridge->objects[140U].instance_state[3] & 0x02U) != 0U &&
          (gate_instance_flags & 0x08U) != 0U &&
          driver_stage_guest_updates >= 4U) {
        trace_driver_object("lock-script-complete", 140U);
        trace_driver_object("gate-script-complete", 67U);
        enter_driver_stage(kravitch_174, frame);
      } else if (driver_stage_frames >= 40U) {
        trace_driver_object("lock-script-timeout", 140U);
        trace_driver_object("gate-script-timeout", 67U);
        trace_driver_events("gate-script-timeout");
        enter_after_timeout("gate-lock-script-state-missing", kravitch_174);
      }
      break;
    }
    case bank_175: {
      if (bank_descriptor_completed && bank_reinforcement_goal != 0U &&
          bank_roots_materialized == bank_reinforcement_goal &&
          bank_reinforcement_kills == bank_reinforcement_goal &&
          source_dead(175U) && bridge->objects.size() > 173U &&
          bridge->objects[173U].health > 0 && bank_quiescent_updates >= 20U) {
        enter_driver_stage(bomb_29, frame);
      } else if (driver_stage_frames >= 1'000U) {
        enter_after_timeout("finite-bank-wave-not-complete", bomb_29);
      }
      break;
    }
    case kravitch_174:
      if (source_dead(174U) && driver_stage_guest_updates >= 4U) {
        enter_driver_stage(radio_260, frame);
      } else if (driver_stage_frames >= 80U) {
        enter_after_timeout("kravitch-not-killed", radio_260);
      }
      break;
    case radio_260:
      if ((mission_state->completed_objectives & 0x01U) != 0U) {
        enter_driver_stage(bank_175, frame);
      } else if (driver_stage_frames >= 80U) {
        enter_after_timeout("objective-0-kravitch-radio-not-complete",
                            bank_175);
      }
      break;
    case bomb_29:
      if ((mission_state->completed_objectives & 0x02U) != 0U) {
        enter_driver_stage(trigger_190, frame);
      } else if (driver_stage_frames >= 240U) {
        enter_after_timeout("objective-1-bomb-29-not-tagged", trigger_190);
      }
      break;
    case trigger_190:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(190U, 4U);
        enter_driver_stage(trigger_194, frame);
      }
      break;
    case trigger_194:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(194U, 5U);
        enter_driver_stage(power_317, frame);
      }
      break;
    case power_317:
      if ((mission_state->completed_objectives & 0x04U) != 0U) {
        enter_driver_stage(trigger_192, frame);
      } else if (driver_stage_frames >= 80U) {
        enter_after_timeout("objective-2-power-switch-not-complete",
                            trigger_192);
      }
      break;
    case trigger_192:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(192U, 6U);
        enter_driver_stage(trigger_193, frame);
      }
      break;
    case trigger_193:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(193U, 7U);
        enter_driver_stage(elevator_315, frame);
      }
      break;
    case elevator_315:
      if (elevator_315_motion_completed) {
        enter_driver_stage(elevator_316, frame);
      } else if (driver_stage_frames >= 180U) {
        enter_after_timeout("elevator-315-motion-did-not-complete",
                            elevator_316);
      }
      break;
    case elevator_316:
      if (elevator_316_motion_completed) {
        enter_driver_stage(bomb_28, frame);
      } else if (driver_stage_frames >= 180U) {
        enter_after_timeout("elevator-316-motion-did-not-complete", bomb_28);
      }
      break;
    case bomb_28:
      if ((mission_state->completed_objectives & 0x08U) != 0U) {
        enter_driver_stage(trigger_191, frame);
      } else if (driver_stage_frames >= 80U) {
        enter_after_timeout("objective-3-bomb-28-not-tagged", trigger_191);
      }
      break;
    case trigger_191:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(191U, 8U);
        enter_driver_stage(trigger_258, frame);
      }
      break;
    case trigger_258:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(258U, 2U);
        enter_driver_stage(station_318, frame);
      }
      break;
    case station_318:
      if (station_318_motion_completed) {
        enter_driver_stage(station_319, frame);
      } else if (driver_stage_frames >= 180U) {
        enter_after_timeout("station-318-motion-did-not-complete", station_319);
      }
      break;
    case station_319:
      if (station_319_motion_completed) {
        enter_driver_stage(trigger_259, frame);
      } else if (driver_stage_frames >= 180U) {
        enter_after_timeout("station-319-motion-did-not-complete", trigger_259);
      }
      break;
    case trigger_259:
      if (driver_stage_guest_updates >= 12U) {
        mark_trigger(259U, 3U);
        enter_driver_stage(finale_30, frame);
      }
      break;
    case finale_30:
      // FMV presentation is native.  The retail proof boundary is the exact
      // SUBWAY callback entering and returning from its second state-9 movie
      // loader, not synthetic mission completion latches.
      if (finale_callback_completed && finale_state9_seen &&
          finale_state9_returned) {
        scripted_route_complete = true;
        enter_driver_stage(complete, frame);
      } else if (driver_stage_frames >= 400U) {
        record_driver_blocker("finale-success-not-reached");
      }
      break;
    }

    driver_bridge = *bridge;
    driver_mission = *mission_state;

    if (driver_failed || mission_state->terminal || mission_state->failure) {
      break;
    }
  }

  std::size_t static_lifetimes{};
  std::size_t dynamic_lifetimes{};
  std::size_t deaths{};
  std::size_t retired{};
  for (const auto &lifetime : lifetimes) {
    static_cast<void>(lifetime.slot < dynamic_first_slot ? ++static_lifetimes
                                                         : ++dynamic_lifetimes);
    deaths += lifetime.died ? 1U : 0U;
    retired += lifetime.retired ? 1U : 0U;
    if (lifetime.slot < dynamic_first_slot && lifetime.identity.class_id != 0 &&
        !lifetime.saw_target && lifetime.fire_frames == 0U && !lifetime.moved &&
        !lifetime.died) {
      continue;
    }
    std::cout << "actor-lifetime: slot=" << lifetime.slot
              << ", generation=" << lifetime.generation << ", class=0x"
              << std::hex
              << static_cast<std::uint16_t>(lifetime.identity.class_id)
              << ", definition=0x" << lifetime.identity.definition
              << ", path=0x" << lifetime.identity.path_pointer << std::dec
              << ", parameter=" << lifetime.identity.parameter << ", authored=("
              << lifetime.identity.authored_position.x << ','
              << lifetime.identity.authored_position.y << ','
              << lifetime.identity.authored_position.z << ')'
              << ", frames=" << lifetime.first_frame << '-'
              << lifetime.last_frame << ", hp=" << lifetime.start_health << '/'
              << lifetime.minimum_health << '/' << lifetime.end_health
              << ", position=(" << lifetime.start_position.x << ','
              << lifetime.start_position.y << ',' << lifetime.start_position.z
              << ")->(" << lifetime.end_position.x << ','
              << lifetime.end_position.y << ',' << lifetime.end_position.z
              << ')' << ", target=" << lifetime.saw_target << '/'
              << lifetime.target_changes << ", fire=" << lifetime.fire_frames
              << ", animation=" << lifetime.animation_changes
              << ", exact-pose=" << lifetime.exact_pose_frames
              << ", ground=" << lifetime.ground_frames << '/'
              << lifetime.maximum_ground_delta
              << ", ground-sentinel=" << lifetime.packed_ground_sentinel_frames
              << ", stagnant-combat=" << lifetime.longest_stagnant_combat_frames
              << ", died=" << lifetime.died << ", retired=" << lifetime.retired
              << '\n';
  }

  const auto mission_progressed =
      first_mission && last_mission &&
      (first_mission->completed_objectives !=
           last_mission->completed_objectives ||
       first_mission->revealed_objectives !=
           last_mission->revealed_objectives ||
       first_mission->notified_objectives !=
           last_mission->notified_objectives ||
       first_mission->parameter_mask != last_mission->parameter_mask ||
       checkpoints != 0U);
  const auto objectives_asserted = first_mission && last_mission &&
                                   (first_mission->completed_objectives !=
                                        last_mission->completed_objectives ||
                                    last_mission->success);
  const auto triggers_asserted =
      std::ranges::all_of(trigger_visited, std::identity{}) &&
      std::ranges::all_of(trigger_observed, std::identity{});
  const auto scripted_transports_asserted =
      elevator_315_motion_completed && elevator_316_motion_completed &&
      station_318_motion_completed && station_319_motion_completed;
  const auto success_asserted =
      finale_callback_attempted && finale_callback_completed &&
      finale_state9_seen && finale_state9_returned && scripted_route_complete;
  std::cout << "legacy-level-summary: requested=" << frame_count
            << ", completed=" << completed_frames << ", outer=" << outer_updates
            << ", native=" << native_updates << ", opening=";
  if (opening_complete_frame) {
    std::cout << *opening_complete_frame;
  } else {
    std::cout << "none";
  }
  std::cout << ", dynamic-first=" << dynamic_first_slot
            << ", actor-lifetimes=" << lifetimes.size()
            << " (static=" << static_lifetimes
            << ", dynamic=" << dynamic_lifetimes << ")"
            << ", deaths=" << deaths << ", retired=" << retired
            << ", mission-transitions=" << mission_transitions
            << ", checkpoints=" << checkpoints
            << ", event-high-water=" << maximum_pending_events << '/'
            << maximum_ready_events << ", invalid-targets=" << invalid_targets
            << ", post-opening-player-moved=" << post_opening_player_moved
            << ", mission-progressed=" << mission_progressed
            << ", driver-stage=" << legacyLevelDriverStageName(driver_stage)
            << ", driver-stage-age="
            << (completed_frames > driver_stage_entry_trace_frame
                    ? completed_frames - driver_stage_entry_trace_frame
                    : 0U)
            << ", bank-kills=" << bank_reinforcement_kills
            << ", camera-owner-changes=" << camera_controller_changes
            << ", camera-discontinuities=" << camera_discontinuities
            << ", rail-mode-frames=" << scripted_camera_rail_frames
            << ", player-overrides=" << player_position_overrides
            << ", packed-ground-sentinels=" << packed_ground_sentinel_samples;
  if (last_mission) {
    std::cout << ", success=" << last_mission->success
              << ", terminal=" << last_mission->terminal
              << ", failure=" << last_mission->failure;
  }
  std::cout << '\n';

  std::cout << "legacy-level-assertions: failure-snapshot="
            << (failure_branch_checked && failure_branch_passed)
            << ", triggers=" << triggers_asserted << " [" << trigger_visited[0]
            << '/' << trigger_observed[0] << ',' << trigger_visited[1] << '/'
            << trigger_observed[1] << ',' << trigger_visited[2] << '/'
            << trigger_observed[2] << ',' << trigger_visited[3] << '/'
            << trigger_observed[3] << ']'
            << ", intro-state9=" << intro_state9_seen << '/'
            << intro_state9_returned
            << ", scripted-rail-mode=" << scripted_camera_rail_seen
            << ", scripted-transports=" << scripted_transports_asserted << " ["
            << elevator_315_motion_completed << ','
            << elevator_316_motion_completed << ','
            << station_318_motion_completed << ','
            << station_319_motion_completed << ']'
            << ", bomb29-callback=" << bomb_29_callback_attempted << '/'
            << bomb_29_callback_completed
            << ", objectives=" << objectives_asserted
            << ", checkpoint=" << (checkpoints != 0U)
            << ", finale-callback=" << finale_callback_attempted << '/'
            << finale_callback_completed
            << ", finale-state9=" << finale_state9_seen << '/'
            << finale_state9_returned << ", success=" << success_asserted
            << '\n';

  std::string_view first_blocker{"none"};
  std::string synthesized_blocker;
  auto exit_code = 0;
  if (invalid_targets != 0U) {
    first_blocker = "invalid-guest-target-slot";
    exit_code = 32;
  } else if (!opening_complete_frame) {
    first_blocker = "opening-did-not-complete";
    exit_code = 33;
  } else if (!last_mission) {
    first_blocker = "mission-bridge-unavailable";
    exit_code = 31;
  } else if (driver_first_blocker) {
    first_blocker = *driver_first_blocker;
    exit_code = 36;
  } else if (!failure_branch_checked || !failure_branch_passed) {
    first_blocker = "protected-object-failure-assertion";
    exit_code = 36;
  } else if (last_mission->failure) {
    first_blocker = "mission-failure-before-success";
    exit_code = 34;
  } else if (!triggers_asserted) {
    first_blocker = "authored-trigger-activation-assertion";
    exit_code = 36;
  } else if (!intro_state9_seen || !intro_state9_returned) {
    first_blocker = "source-157-loader-boundary-assertion";
    exit_code = 36;
  } else if (!scripted_transports_asserted) {
    first_blocker = "guest-scripted-transport-assertion";
    exit_code = 36;
  } else if (!objectives_asserted) {
    first_blocker = "mission-objective-assertion";
    exit_code = 36;
  } else if (checkpoints == 0U) {
    first_blocker = "mission-checkpoint-assertion";
    exit_code = 36;
  } else if (!success_asserted) {
    synthesized_blocker = "scripted-route-stalled-at-";
    synthesized_blocker += legacyLevelDriverStageName(driver_stage);
    first_blocker = synthesized_blocker;
    exit_code = 36;
  }
  std::cout << "first-blocker=" << first_blocker << '\n';
  return exit_code;
}

int probeLegacyMission(const char *cue_path, const char *ram_path) {
  const auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{
        sf::core::ErrorCode::unsupported,
        "Legacy mission probe requires Syphon Filter USA v1.1"};
  }

  const auto ram = sf::core::readBinaryFile(std::filesystem::path{ram_path});
  sf::game::LegacyGameplayVm vm{disc.executable()};
  vm.bindPsxBiosRandomCalls();
  vm.bindPsxVideoTimingCall();
  if (!vm.runtime().restoreRam(ram)) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "Legacy mission probe requires an exact 2 MiB raw RAM image"};
  }
  vm.runtime().reset(disc.executable().header().initial_pc, 0x80115c68U,
                     0x807fff70U);

  const auto tick =
      vm.tickMission(sf::game::syphonFilterUsaV11MissionProfile());
  std::cout << "frame-event: "
            << sf::psx::toString(tick.frame_event.execution.reason)
            << ", instructions=" << tick.frame_event.execution.instructions
            << ", pc=0x" << std::hex << std::uppercase
            << tick.frame_event.execution.pc << std::dec << '\n';
  if (!tick.frame_event.completed()) {
    return 6;
  }
  std::cout << "delayed-callbacks: "
            << sf::psx::toString(tick.delayed_callbacks.execution.reason)
            << ", instructions="
            << tick.delayed_callbacks.execution.instructions << ", pc=0x"
            << std::hex << std::uppercase << tick.delayed_callbacks.execution.pc
            << std::dec << '\n';
  if (!tick.delayed_callbacks.completed()) {
    return 7;
  }
  std::cout << "queue-drain: "
            << sf::psx::toString(tick.queue_drain.execution.reason)
            << ", instructions=" << tick.queue_drain.execution.instructions
            << ", pc=0x" << std::hex << std::uppercase
            << tick.queue_drain.execution.pc << std::dec << '\n';
  if (!tick.queue_drain.completed()) {
    return 8;
  }
  std::cout << "ready-events: " << tick.ready_events
            << ", dispatched=" << tick.dispatched_events.size()
            << ", instructions=" << tick.instructions() << '\n';
  for (std::size_t index = 0U; index < tick.dispatched_events.size(); ++index) {
    const auto &event = tick.dispatched_events[index];
    std::cout << "event[" << index
              << "]: " << sf::psx::toString(event.execution.reason)
              << ", instructions=" << event.execution.instructions << ", pc=0x"
              << std::hex << std::uppercase << event.execution.pc << std::dec
              << '\n';
  }
  if (tick.bridge_fault) {
    std::cout << "mission bridge fault\n";
  }
  return tick.completed() ? 0 : 9;
}

int probeLegacyFrame(const char *cue_path, const char *ram_path,
                     std::uint32_t frame_count) {
  const auto disc = openDisc(cue_path);
  if (!disc.game() || disc.game()->serial != "SCUS-94240" ||
      disc.game()->version != "1.1") {
    throw sf::core::Error{sf::core::ErrorCode::unsupported,
                          "Legacy frame probe requires Syphon Filter USA v1.1"};
  }

  const auto ram = sf::core::readBinaryFile(std::filesystem::path{ram_path});
  sf::game::LegacyGameplayVm vm{disc.executable()};
  vm.bindSyphonFilterUsaV11PlatformCalls();
  if (!vm.runtime().restoreRam(ram)) {
    throw sf::core::Error{
        sf::core::ErrorCode::invalid_format,
        "Legacy frame probe requires an exact 2 MiB raw RAM image"};
  }
  vm.runtime().reset(disc.executable().header().initial_pc, 0x80115c68U,
                     0x807fff70U);

  sf::game::LegacyGameplayVmResult frame;
  std::uint64_t total_instructions{};
  std::uint64_t total_host_calls{};
  for (std::uint32_t index = 0U; index < frame_count; ++index) {
    frame = vm.tickRetailFrame();
    total_instructions += frame.execution.instructions;
    total_host_calls += frame.host_calls;
    if (!frame.completed()) {
      std::cerr << "retail-frame[" << index << "] failed\n";
      break;
    }
  }
  std::cout << "retail-frame: " << sf::psx::toString(frame.execution.reason)
            << ", frames=" << frame_count
            << ", instructions=" << total_instructions
            << ", host-calls=" << total_host_calls << ", pc=0x" << std::hex
            << std::uppercase << frame.execution.pc << ", instruction=0x"
            << frame.execution.instruction << ", ra=0x"
            << vm.runtime().state().gpr[31] << ", sp=0x"
            << vm.runtime().state().gpr[29] << std::dec << ", ram-sha256="
            << sf::core::toHex(sf::core::sha256(vm.runtime().ram())) << '\n';
  return frame.completed() ? 0 : 10;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string_view{argv[1]} == "inspect") {
      return inspect(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "inspect-disc-info") {
      return inspectDiscInfo(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "inspect-title") {
      return inspectTitle(argv[2]);
    }
    if ((argc == 3 || argc == 4) &&
        std::string_view{argv[1]} == "inspect-mission") {
      auto mission_index = std::uint32_t{};
      if (argc == 4) {
        const auto value = std::string_view{argv[3]};
        const auto parsed = std::from_chars(
            value.data(), value.data() + value.size(), mission_index);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size()) {
          throw sf::core::Error{sf::core::ErrorCode::invalid_format,
                                "Mission index is not an unsigned integer"};
        }
      }
      return inspectMission(argv[2], mission_index);
    }
    if (argc == 4 && std::string_view{argv[1]} == "inspect-mission-archive") {
      return inspectMissionArchive(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "extract-exe") {
      return extractExecutable(argv[2], argv[3]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "catalog") {
      return catalog(argv[2]);
    }
    if ((argc == 3 || argc == 4) && std::string_view{argv[1]} == "list-files") {
      return listDiscFiles(argv[2], argc == 4 ? argv[3] : "");
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-functions") {
      return mapFunctions(argv[2], argv[3]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "map-function-union") {
      return mapFunctionUnion(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-function-calls") {
      return mapFunctionCalls(argv[2], argv[3]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "compare-functions") {
      return compareFunctions(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-embedded-archives") {
      return mapEmbeddedArchives(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-resident-overlays") {
      return mapResidentOverlays(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-mission-overlays") {
      return mapMissionOverlays(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-mission-classes") {
      return mapMissionClasses(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-mission-objects") {
      return mapMissionObjects(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-mission-scripts") {
      return mapMissionScripts(argv[2], argv[3]);
    }
    if (argc == 4 &&
        std::string_view{argv[1]} == "map-mission-script-opcodes") {
      return mapMissionScriptOpcodes(argv[2], argv[3]);
    }
    if (argc == 4 &&
        std::string_view{argv[1]} == "map-mission-script-handler-calls") {
      return mapMissionScriptHandlerCalls(argv[2], argv[3]);
    }
    if (argc == 5 &&
        std::string_view{argv[1]} == "compare-mission-script-opcodes") {
      return compareMissionScriptOpcodes(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-mission-script-events") {
      return mapMissionScriptEvents(argv[2], argv[3]);
    }
    if (argc == 4 &&
        std::string_view{argv[1]} == "map-mission-script-actions") {
      return mapMissionScriptActions(argv[2], argv[3]);
    }
    if (argc == 4 &&
        (std::string_view{argv[1]} == "map-mission-script-strings" ||
         std::string_view{argv[1]} == "map-mission-sound-scenes")) {
      return mapMissionScriptStrings(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-object-handlers") {
      return mapObjectHandlers(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-string-references") {
      return mapStringReferences(argv[2], argv[3]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "map-xa-streams") {
      return mapXaStreams(argv[2], argv[3]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe-legacy-vm") {
      return probeLegacyVm(argv[2]);
    }
    if ((argc == 3 || argc == 4) &&
        std::string_view{argv[1]} == "probe-executable-entry") {
      auto budget = std::uint64_t{1'000'000U};
      if (argc == 4) {
        const auto value = std::string_view{argv[3]};
        const auto parsed =
            std::from_chars(value.data(), value.data() + value.size(), budget);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size() || budget == 0U) {
          throw sf::core::Error{sf::core::ErrorCode::invalid_argument,
                                "Instruction budget must be positive"};
        }
      }
      return probeExecutableEntry(argv[2], budget);
    }
    if ((argc == 3 || argc == 4) &&
        (std::string_view{argv[1]} == "probe-sf2-guest-bootstrap" ||
         std::string_view{argv[1]} == "probe-sf2-mission-transition")) {
      auto budget = std::uint64_t{5'000'000U};
      if (argc == 4) {
        const auto text = std::string_view{argv[3]};
        const auto [pointer, error] =
            std::from_chars(text.data(), text.data() + text.size(), budget);
        if (error != std::errc{} || pointer != text.data() + text.size() ||
            budget == 0U) {
          throw sf::core::Error{
              sf::core::ErrorCode::invalid_argument,
              "Instruction budget must be a positive integer"};
        }
      }
      return probeSf2GuestBootstrap(argv[2], budget,
                                    std::string_view{argv[1]} ==
                                        "probe-sf2-mission-transition");
    }
    if ((argc >= 3 && argc <= 7) &&
        std::string_view{argv[1]} == "probe-sf2-product-runtime") {
      const auto mode = argc >= 5 ? std::string_view{argv[4]}
                                  : std::string_view{};
      if (argc == 7 && mode != "movies") {
        printUsage();
        return 1;
      }
      if (!mode.empty() && mode != "neutral" && mode != "forward" &&
          mode != "combat" && mode != "crouch" &&
          mode != "crouchback" &&
          mode != "objective" && mode != "objective-dialogue" &&
          mode != "weapons" && mode != "pause" && mode != "complete" &&
          mode != "completeflow" &&
          mode != "movies" &&
          mode != "ui" &&
          mode != "uiobjective" &&
          mode != "quickstate" && mode != "quickobjective") {
        printUsage();
        return 1;
      }
      return probeSf2ProductRuntime(
          argv[2], argc >= 4 ? parseFrameCount(argv[3]) : 64U,
          mode == "forward", mode == "combat", mode == "crouch",
          mode == "crouchback",
          mode == "quickstate" || mode == "quickobjective",
          mode == "objective" || mode == "weapons" ||
              mode == "uiobjective" ||
              mode == "quickobjective" || mode == "objective-dialogue",
          mode == "weapons", mode != "objective-dialogue",
          mode == "ui" || mode == "uiobjective", mode == "pause",
          mode == "complete" || mode == "completeflow",
          mode == "completeflow",
          mode == "movies",
          argc >= 6 ? parseSf2MissionIndex(argv[5]) : 2U,
          argc == 7 ? parseSf2MovieOrdinal(argv[6]) : 0U);
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe-legacy-cd") {
      return probeLegacyCd(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe-legacy-loop") {
      return probeLegacyLoop(argv[2]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "probe-legacy-bootstrap") {
      return probeLegacyBootstrap(argv[2]);
    }
    if ((argc == 3 || argc == 4) &&
        std::string_view{argv[1]} == "probe-legacy-level") {
      return probeLegacyLevel(argv[2],
                              argc == 4 ? parseFrameCount(argv[3]) : 1'200U);
    }
    if (argc == 4 && std::string_view{argv[1]} == "probe-legacy-mission") {
      return probeLegacyMission(argv[2], argv[3]);
    }
    if ((argc == 4 || argc == 5) &&
        std::string_view{argv[1]} == "probe-legacy-frame") {
      return probeLegacyFrame(argv[2], argv[3],
                              argc == 5 ? parseFrameCount(argv[4]) : 1U);
    }
    if (argc == 5 && std::string_view{argv[1]} == "extract-file") {
      return extractFile(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "list-hog") {
      return listHog(argv[2], argv[3]);
    }
    if (argc == 6 && std::string_view{argv[1]} == "extract-hog-file") {
      return extractHogFile(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "list-fog") {
      return listFog(argv[2], argv[3]);
    }
    if (argc == 6 && std::string_view{argv[1]} == "extract-fog-file") {
      return extractFogFile(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "list-fog-hog") {
      return listFogHog(argv[2], argv[3], argv[4]);
    }
    if (argc == 7 && std::string_view{argv[1]} == "extract-fog-hog-file") {
      return extractFogHogFile(argv[2], argv[3], argv[4], argv[5], argv[6]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "extract-mission-file") {
      return extractMissionFile(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "export-ui-assets") {
      return sf::tool::exportUiAssets(argv[2], argv[3]);
    }
    if (argc == 5 && std::string_view{argv[1]} == "export-vit-language-pack") {
      return exportVitLanguagePack(argv[2], argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "export-runtime-strings") {
      return exportRuntimeStrings(argv[2], argv[3]);
    }
    printUsage();
    return 64;
  } catch (const sf::core::Error &error) {
    std::cerr << "sf_tool: " << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "sf_tool: unexpected error: " << error.what() << '\n';
    return 1;
  }
}
