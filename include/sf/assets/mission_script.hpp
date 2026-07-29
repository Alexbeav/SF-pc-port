#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sf::assets {

struct MissionScriptEventRecord {
  std::uint32_t relative_offset{};
  std::uint16_t encoded_header{};
  std::uint16_t selector{};
  std::uint8_t event_id{};
  std::uint8_t event_flags{};
  std::uint8_t length_halfwords{};
  // Raw descriptor/control stream following the two-halfword event header.
  // The words remain encoded because operand widths are selected by the
  // retail descriptor tables and differ in several SF3 slots.
  std::vector<std::uint16_t> action_halfwords;
};

// SF2/SF3 *.SS files contain named, compiled mission programs. Despite their
// historical "sound scene" extension, retail program names and call sites
// show that they also own objectives, actor choreography, dialogue, doors,
// mission completion, and other shared scripting.
struct MissionScriptProgram {
  std::size_t offset{};
  std::uint32_t header_word_0{};
  std::uint32_t header_word_1{};
  std::array<std::uint32_t, 6> relative_pointers{};
  // Packed bytes in header_word_0. Retail dispatch and save-state code prove
  // that bytes 1 and 3 are the halfword counts of the two mutable tables.
  // Save-state serialization starts the first table at byte 2; 0xff means
  // that none of that table is serialized.
  std::uint8_t format_version{};
  std::uint8_t variable_count{};
  std::uint8_t serialized_variable_begin{};
  std::uint8_t timer_count{};
  // Initial mutable state copied directly from the program's +0x0c and
  // +0x10 relative tables. Variables are manipulated as unsigned halfwords;
  // timers are decremented as signed halfwords and use -1 as inactive.
  std::vector<std::uint16_t> initial_variables;
  std::vector<std::int16_t> initial_timers;
  // Exact relocatable program image. Keeping this with the parsed indexes
  // gives a future interpreter access to descriptor operands and payload
  // pointers without retaining the entire disc archive.
  std::vector<std::byte> serialized_bytes;
  // Size of the serialized program blob, excluding inter-program alignment
  // and trailing CD-sector padding.
  std::uint32_t serialized_size{};
  std::uint32_t event_terminator_offset{};
  std::vector<MissionScriptEventRecord> events;
  std::string name;
};

class MissionScriptArchive final {
public:
  [[nodiscard]] static MissionScriptArchive
  parse(std::span<const std::byte> bytes);

  [[nodiscard]] std::span<const MissionScriptProgram>
  programs() const noexcept {
    return programs_;
  }

private:
  explicit MissionScriptArchive(std::vector<MissionScriptProgram> programs)
      : programs_(std::move(programs)) {}

  std::vector<MissionScriptProgram> programs_;
};

} // namespace sf::assets
