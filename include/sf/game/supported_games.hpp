#pragma once

#include "sf/core/sha256.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace sf::game {

enum class GameId : std::uint8_t {
  syphon_filter,
  syphon_filter_2,
  syphon_filter_3,
};

struct GameDiscLayout {
  std::string_view title_archive_path;
  std::string_view mission_info_path;
  std::string_view mission_archive_directory;
  std::string_view streaming_audio_path;
};

struct GameExecutableLayout {
  std::uint32_t vsync_address;
  std::uint32_t retrace_counter_address;
  std::uint32_t cd_pending_command_address;
  std::uint32_t cd_pending_command_state;
  std::uint32_t cd_response_pointer;
  std::uint32_t cd_completion_state;
  std::uint32_t cd_control_address;
  std::uint32_t cd_ready_callback_address;
  std::uint32_t cd_ready_result_address;
  std::uint32_t cd_ready_state_address;
  bool cd_ready_callback_is_pointer;
};

struct GameMissionResource {
  std::uint16_t selection_index;
  std::string_view resource_name;

  [[nodiscard]] friend constexpr bool
  operator==(const GameMissionResource &,
             const GameMissionResource &) noexcept = default;
};

struct SupportedGame {
  GameId id;
  std::uint8_t disc_number;
  std::string_view title;
  std::string_view region;
  std::string_view version;
  std::string_view serial;
  std::string_view volume_id;
  std::string_view executable_path;
  sf::core::Sha256Digest executable_sha256;
  GameDiscLayout layout;
  GameExecutableLayout executable_layout;
};

[[nodiscard]] std::span<const SupportedGame> supportedGames() noexcept;
[[nodiscard]] std::span<const GameMissionResource>
missionResources(GameId game, std::uint8_t disc_number) noexcept;

[[nodiscard]] std::optional<std::uint16_t>
missionArchiveSelection(GameId game, std::uint8_t disc_number,
                        std::uint16_t selection_index) noexcept;
[[nodiscard]] std::optional<SupportedGame> identify(
    std::string_view volume_id,
    const sf::core::Sha256Digest& executable_sha256) noexcept;

} // namespace sf::game
