#include "sf/assets/mission_script.hpp"

#include "sf/core/error.hpp"

#include <limits>

namespace sf::assets {
namespace {

std::uint16_t readLe16(std::span<const std::byte> bytes,
                       std::size_t offset) {
  if (offset > bytes.size() ||
      bytes.size() - offset < sizeof(std::uint16_t)) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Truncated mission-script halfword"};
  }
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(bytes[offset]) |
      (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

std::uint32_t readLe32(std::span<const std::byte> bytes,
                       std::size_t offset) {
  if (offset > bytes.size() ||
      bytes.size() - offset < sizeof(std::uint32_t)) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Truncated mission-script integer"};
  }
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::string readString(std::span<const std::byte> bytes,
                       std::size_t offset) {
  if (offset >= bytes.size()) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Mission-script name pointer is invalid"};
  }
  std::string result;
  while (offset < bytes.size() && bytes[offset] != std::byte{0}) {
    const auto character = std::to_integer<unsigned char>(bytes[offset++]);
    if (character < 0x20U || character > 0x7eU) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Mission-script name is not ASCII"};
    }
    result.push_back(static_cast<char>(character));
  }
  if (result.empty() || offset == bytes.size()) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Mission-script name is truncated"};
  }
  return result;
}

} // namespace

MissionScriptArchive
MissionScriptArchive::parse(std::span<const std::byte> bytes) {
  constexpr std::size_t file_header_size = 4U;
  constexpr std::size_t program_header_size = 0x24U;
  constexpr std::size_t name_pointer_index = 3U;
  if (bytes.size() < file_header_size) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Mission-script archive is truncated"};
  }
  const auto count = static_cast<std::size_t>(readLe32(bytes, 0U));
  if (count == 0U ||
      count > (std::numeric_limits<std::size_t>::max() - file_header_size) /
                  sizeof(std::uint32_t) ||
      file_header_size + count * sizeof(std::uint32_t) > bytes.size()) {
    throw core::Error{core::ErrorCode::invalid_format,
                      "Mission-script program table is invalid"};
  }

  std::vector<MissionScriptProgram> programs;
  programs.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto offset = static_cast<std::size_t>(
        readLe32(bytes, file_header_size + index * sizeof(std::uint32_t)));
    if ((offset & 3U) != 0U || offset > bytes.size() ||
        bytes.size() - offset < program_header_size) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Mission-script program offset is invalid"};
    }

    MissionScriptProgram program;
    program.offset = offset;
    program.header_word_0 = readLe32(bytes, offset);
    program.header_word_1 = readLe32(bytes, offset + 4U);
    for (std::size_t pointer = 0;
         pointer < program.relative_pointers.size(); ++pointer) {
      const auto relative =
          readLe32(bytes, offset + 8U + pointer * sizeof(std::uint32_t));
      if (pointer == name_pointer_index &&
          relative > bytes.size() - offset) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Mission-script name pointer is invalid"};
      }
      program.relative_pointers[pointer] = relative;
    }
    program.format_version =
        static_cast<std::uint8_t>(program.header_word_0 & 0xffU);
    program.variable_count =
        static_cast<std::uint8_t>((program.header_word_0 >> 8U) & 0xffU);
    program.serialized_variable_begin =
        static_cast<std::uint8_t>((program.header_word_0 >> 16U) & 0xffU);
    program.timer_count =
        static_cast<std::uint8_t>((program.header_word_0 >> 24U) & 0xffU);
    program.serialized_size = readLe32(bytes, offset + 0x20U);
    if ((program.serialized_variable_begin != 0xffU &&
         program.serialized_variable_begin > program.variable_count) ||
        program.serialized_size < program_header_size ||
        program.serialized_size > bytes.size() - offset) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Mission-script program header is invalid"};
    }
    program.serialized_bytes.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() +
            static_cast<std::ptrdiff_t>(offset + program.serialized_size));
    const auto read_halfword_table =
        [&](std::size_t pointer_index, std::size_t element_count,
            auto &&append) {
          const auto relative = static_cast<std::size_t>(
              program.relative_pointers[pointer_index]);
          if (element_count >
                  std::numeric_limits<std::size_t>::max() /
                      sizeof(std::uint16_t) ||
              relative > program.serialized_size ||
              element_count * sizeof(std::uint16_t) >
                  program.serialized_size - relative) {
            throw core::Error{
                core::ErrorCode::invalid_format,
                "Mission-script mutable table is invalid"};
          }
          for (std::size_t element = 0; element < element_count; ++element) {
            append(readLe16(bytes,
                            offset + relative +
                                element * sizeof(std::uint16_t)));
          }
        };
    program.initial_variables.reserve(program.variable_count);
    read_halfword_table(
        1U, program.variable_count, [&](std::uint16_t value) {
          program.initial_variables.push_back(value);
        });
    program.initial_timers.reserve(program.timer_count);
    read_halfword_table(2U, program.timer_count, [&](std::uint16_t value) {
      program.initial_timers.push_back(static_cast<std::int16_t>(value));
    });
    program.name = readString(
        bytes, offset + program.relative_pointers[name_pointer_index]);

    auto event_offset =
        static_cast<std::size_t>(program.relative_pointers[0]);
    if ((event_offset & 1U) != 0U ||
        event_offset > program.serialized_size ||
        program.serialized_size - event_offset < 4U) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Mission-script event stream is invalid"};
    }
    while (true) {
      const auto encoded_header = readLe16(bytes, offset + event_offset);
      const auto encoded_event =
          static_cast<std::uint8_t>(encoded_header >> 8U);
      if (encoded_event == 0xffU) {
        program.event_terminator_offset =
            static_cast<std::uint32_t>(event_offset);
        break;
      }
      const auto length =
          static_cast<std::uint8_t>(encoded_header & 0xffU);
      const auto byte_length = static_cast<std::size_t>(length) * 2U;
      if (length < 2U || byte_length > program.serialized_size - event_offset) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Mission-script event record is invalid"};
      }
      MissionScriptEventRecord event{
          static_cast<std::uint32_t>(event_offset),
          encoded_header,
          readLe16(bytes, offset + event_offset + 2U),
          static_cast<std::uint8_t>(encoded_event & 0x3fU),
          static_cast<std::uint8_t>(encoded_event & 0xc0U),
          length};
      event.action_halfwords.reserve(static_cast<std::size_t>(length) - 2U);
      for (std::size_t word = 2U; word < length; ++word) {
        event.action_halfwords.push_back(
            readLe16(bytes, offset + event_offset + word * 2U));
      }
      program.events.push_back(std::move(event));
      event_offset += byte_length;
      if (event_offset > program.serialized_size ||
          program.serialized_size - event_offset < 2U) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Mission-script event stream is unterminated"};
      }
    }
    programs.push_back(std::move(program));
  }
  return MissionScriptArchive{std::move(programs)};
}

} // namespace sf::assets
