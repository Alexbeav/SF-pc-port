#pragma once

#include "sf/assets/fog_archive.hpp"
#include "sf/assets/hog_archive.hpp"
#include "sf/assets/level_layout.hpp"
#include "sf/assets/mission_briefing.hpp"
#include "sf/assets/mission_objects.hpp"
#include "sf/assets/mission_script.hpp"
#include "sf/game/disc_movie.hpp"
#include "sf/game/legacy_mission_image.hpp"
#include "sf/game/runtime_profile.hpp"
#include "sf/game/supported_games.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf::game {

class GameDisc;

struct MissionDefinition {
  std::uint32_t index{};
  std::string_view title;
  std::string_view resource_name;
  std::string_view overlay_name;
  std::string_view opening_movie_path;
  std::string_view ending_movie_path;
  std::int32_t selection_index{-1};
  std::uint8_t briefing_record{};
  // Some continuation missions execute a gameplay overlay retained for
  // compatibility while their authored briefing table lives in another
  // retail overlay. Empty means overlay_name.
  std::string_view briefing_overlay_name;
};

[[nodiscard]] std::span<const MissionDefinition> missionCatalog() noexcept;
[[nodiscard]] std::span<const MissionDefinition>
missionCatalog(GameId game) noexcept;

// Reads one raw Mode 2 STR range from SF2's MOVIE1.HOG/MOVIE2.HOG catalog.
// The returned bytes retain their 2352-byte sectors for the native decoder.
[[nodiscard]] DiscMovie loadSf2EmbeddedMovie(GameDisc &disc,
                                             std::string_view name);
struct Sf2EmbeddedMovieCatalogEntry {
  std::string name;
  std::uint32_t sector_offset{};
  std::uint32_t sector_count{};
  std::size_t raw_size{};
};
[[nodiscard]] std::vector<Sf2EmbeddedMovieCatalogEntry>
sf2EmbeddedMovieCatalog(GameDisc &disc);
[[nodiscard]] const MissionDefinition &missionDefinition(std::uint32_t index);
[[nodiscard]] const MissionDefinition &missionDefinition(
    GameId game, std::uint32_t index);
[[nodiscard]] std::span<const std::string_view>
missionScriptedMoviePaths(std::uint32_t index) noexcept;
[[nodiscard]] std::span<const std::string_view>
missionScriptedMoviePaths(GameId game, std::uint32_t index) noexcept;
[[nodiscard]] std::span<const std::uint8_t>
missionScriptedMovieCatalogIndices(GameId game,
                                   std::uint32_t index) noexcept;

class MissionPackage final {
public:
  [[nodiscard]] static MissionPackage load(GameDisc &disc, std::uint32_t index);
  [[nodiscard]] static MissionPackage loadFirst(GameDisc &disc);

  [[nodiscard]] const MissionDefinition &definition() const noexcept {
    return definition_;
  }
  [[nodiscard]] GameId gameId() const noexcept { return game_id_; }
  [[nodiscard]] const GameRuntimeProfile &runtimeProfile() const {
    return game::runtimeProfile(game_id_);
  }
  [[nodiscard]] const assets::MissionBriefing &briefing() const noexcept {
    return briefing_;
  }
  [[nodiscard]] bool hasRetailBriefing() const noexcept {
    return has_retail_briefing_;
  }
  [[nodiscard]] const assets::FogArchive &archive() const noexcept {
    return archive_;
  }
  [[nodiscard]] const LegacyMissionImage &legacyImage() const noexcept {
    return legacy_image_;
  }
  [[nodiscard]] const std::optional<assets::MissionScriptArchive> &
  missionScripts() const noexcept {
    return mission_scripts_;
  }
  [[nodiscard]] DiscMovie &openingMovie() noexcept { return opening_movie_; }
  [[nodiscard]] std::span<const DiscMovie> scriptedMovies() const noexcept {
    return scripted_movies_;
  }
  [[nodiscard]] const DiscMovie &endingMovie() const noexcept {
    return ending_movie_;
  }
  [[nodiscard]] const assets::HogArchive &worldModels() const noexcept {
    return world_models_;
  }
  [[nodiscard]] const assets::HogArchive &objectModels() const noexcept {
    return object_models_;
  }
  [[nodiscard]] const assets::HogArchive &specialEffects() const noexcept {
    return special_effects_;
  }
  [[nodiscard]] const assets::HogArchive &interfaceAssets() const noexcept {
    return interface_assets_;
  }
  [[nodiscard]] const assets::HogArchive &menuAssets() const noexcept {
    return menu_assets_;
  }
  [[nodiscard]] const assets::HogArchive &characterAnimations() const noexcept {
    return character_animations_;
  }
  [[nodiscard]] const assets::HogArchive &textureBank(std::size_t bank) const;
  [[nodiscard]] std::size_t textureBankCount() const noexcept {
    return texture_banks_.size();
  }
  [[nodiscard]] const assets::LevelLayout &layout() const noexcept {
    return layout_;
  }
  [[nodiscard]] const assets::MissionObjects &objects() const noexcept {
    return objects_;
  }
  [[nodiscard]] std::size_t textureFileCount() const noexcept {
    return texture_file_count_;
  }
  [[nodiscard]] std::size_t worldModelCount() const noexcept {
    return world_models_.entries().size();
  }

private:
  MissionPackage(
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
      assets::MissionObjects objects, std::size_t texture_file_count);

  GameId game_id_{GameId::syphon_filter};
  MissionDefinition definition_;
  assets::MissionBriefing briefing_;
  bool has_retail_briefing_{};
  assets::FogArchive archive_;
  std::optional<assets::MissionScriptArchive> mission_scripts_;
  LegacyMissionImage legacy_image_;
  DiscMovie opening_movie_;
  std::vector<DiscMovie> scripted_movies_;
  DiscMovie ending_movie_;
  assets::HogArchive world_models_;
  assets::HogArchive object_models_;
  assets::HogArchive special_effects_;
  assets::HogArchive interface_assets_;
  assets::HogArchive menu_assets_;
  assets::HogArchive character_animations_;
  std::vector<assets::HogArchive> texture_banks_;
  assets::LevelLayout layout_;
  assets::MissionObjects objects_;
  std::size_t texture_file_count_{};
};

} // namespace sf::game
