#include "sf/assets/fog_archive.hpp"
#include "sf/assets/hog_archive.hpp"
#include "sf/core/error.hpp"
#include "sf/game/disc_cdrom_media.hpp"
#include "sf/game/embedded_hog.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/legacy_gameplay_vm.hpp"
#include "sf/game/mission.hpp"
#include "sf/game/runtime_profile.hpp"
#include "sf/game/sf2_runtime.hpp"
#include "sf/psx/gte_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>

namespace sf::game {

std::optional<std::uint16_t>
sf2FacingAngle(std::int64_t delta_x, std::int64_t delta_z) noexcept {
  if (delta_x == 0 && delta_z == 0) {
    return std::nullopt;
  }
  constexpr auto full_turn = 4096.0;
  constexpr auto two_pi = 6.28318530717958647692;
  auto angle = static_cast<std::int64_t>(std::llround(
      std::atan2(static_cast<double>(delta_x),
                 static_cast<double>(delta_z)) *
      full_turn / two_pi));
  angle %= 4096;
  if (angle < 0) {
    angle += 4096;
  }
  return static_cast<std::uint16_t>(angle);
}

Sf2GpuCommandKind
sf2GpuCommandKind(const Sf2GpuPacket &packet) noexcept {
  if (packet.gp0_words.empty()) {
    return Sf2GpuCommandKind::unsupported;
  }
  const auto opcode =
      static_cast<std::uint8_t>(packet.gp0_words.front() >> 24U);
  if ((opcode >= 0x20U && opcode <= 0x7fU)) {
    return Sf2GpuCommandKind::draw;
  }
  if (opcode >= 0xe1U && opcode <= 0xe6U) {
    return Sf2GpuCommandKind::draw_environment;
  }
  switch (opcode) {
  case 0x02U:
    return Sf2GpuCommandKind::fill_vram;
  case 0x80U:
    return Sf2GpuCommandKind::copy_vram;
  case 0xa0U:
    return Sf2GpuCommandKind::upload_vram;
  default:
    return Sf2GpuCommandKind::unsupported;
  }
}

std::optional<std::size_t>
sf2Gp0CommandWordCount(std::span<const std::uint32_t> words) noexcept {
  if (words.empty()) {
    return std::nullopt;
  }
  const auto opcode = static_cast<std::uint8_t>(words.front() >> 24U);
  auto count = std::size_t{1U};
  if (opcode == 0x02U || opcode == 0xc0U) {
    count = 3U;
  } else if (opcode == 0x80U) {
    count = 4U;
  } else if (opcode == 0xa0U) {
    if (words.size() < 3U) {
      return std::nullopt;
    }
    auto width = static_cast<std::uint16_t>(words[2U]);
    auto height = static_cast<std::uint16_t>(words[2U] >> 16U);
    width = width == 0U ? 1024U : width;
    height = height == 0U ? 512U : height;
    count = 3U +
            (static_cast<std::size_t>(width) *
                 static_cast<std::size_t>(height) +
             1U) /
                2U;
  } else if (opcode >= 0x20U && opcode <= 0x23U) {
    count = 4U;
  } else if (opcode >= 0x24U && opcode <= 0x27U) {
    count = 7U;
  } else if (opcode >= 0x28U && opcode <= 0x2bU) {
    count = 5U;
  } else if (opcode >= 0x2cU && opcode <= 0x2fU) {
    count = 9U;
  } else if (opcode >= 0x30U && opcode <= 0x33U) {
    count = 6U;
  } else if (opcode >= 0x34U && opcode <= 0x37U) {
    count = 9U;
  } else if (opcode >= 0x38U && opcode <= 0x3bU) {
    count = 8U;
  } else if (opcode >= 0x3cU && opcode <= 0x3fU) {
    count = 12U;
  } else if (opcode >= 0x40U && opcode <= 0x47U) {
    count = 3U;
  } else if (opcode >= 0x48U && opcode <= 0x4fU) {
    const auto terminator = std::ranges::find_if(
        words.subspan(std::min<std::size_t>(3U, words.size())),
        [](std::uint32_t word) {
          return (word & 0xf000f000U) == 0x50005000U;
        });
    if (terminator == words.end()) {
      return std::nullopt;
    }
    count = static_cast<std::size_t>(
                std::distance(words.begin(), terminator)) +
            1U;
  } else if (opcode >= 0x50U && opcode <= 0x57U) {
    count = 4U;
  } else if (opcode >= 0x58U && opcode <= 0x5fU) {
    const auto terminator = std::ranges::find_if(
        words.subspan(std::min<std::size_t>(4U, words.size())),
        [](std::uint32_t word) {
          return (word & 0xf000f000U) == 0x50005000U;
        });
    if (terminator == words.end()) {
      return std::nullopt;
    }
    count = static_cast<std::size_t>(
                std::distance(words.begin(), terminator)) +
            1U;
  } else if (opcode >= 0x60U && opcode <= 0x63U) {
    count = 3U;
  } else if (opcode >= 0x64U && opcode <= 0x67U) {
    count = 4U;
  } else if ((opcode >= 0x68U && opcode <= 0x73U) ||
             (opcode >= 0x78U && opcode <= 0x7bU)) {
    count = 2U;
  } else if ((opcode >= 0x74U && opcode <= 0x77U) ||
             (opcode >= 0x7cU && opcode <= 0x7fU)) {
    count = 3U;
  }
  return words.size() >= count ? std::optional{count} : std::nullopt;
}

std::optional<Sf2GpuTransfer>
sf2GpuTransfer(const Sf2GpuPacket &packet) noexcept {
  const auto kind = sf2GpuCommandKind(packet);
  const auto minimum_words =
      kind == Sf2GpuCommandKind::copy_vram ? 4U : 3U;
  if ((kind != Sf2GpuCommandKind::fill_vram &&
       kind != Sf2GpuCommandKind::copy_vram &&
       kind != Sf2GpuCommandKind::upload_vram) ||
      packet.gp0_words.size() < minimum_words) {
    return std::nullopt;
  }
  const auto coordinate_word =
      packet.gp0_words[kind == Sf2GpuCommandKind::copy_vram ? 2U : 1U];
  const auto size_word =
      packet.gp0_words[kind == Sf2GpuCommandKind::copy_vram ? 3U : 2U];
  constexpr auto transfer_field_mask = 0x01ff03ffU;
  // Retail-generated transfers keep the reserved coordinate/size bits clear.
  // Rejecting them on captured MoveImage commands prevents a damaged DMA tag
  // from reinterpreting polygon payload as a full-VRAM copy on the host.
  if (kind == Sf2GpuCommandKind::copy_vram &&
      ((packet.gp0_words[1U] & ~transfer_field_mask) != 0U ||
       (coordinate_word & ~transfer_field_mask) != 0U ||
       (size_word & ~transfer_field_mask) != 0U)) {
    return std::nullopt;
  }
  // The GPU ignores the unused upper bits in each packed field. Transfer
  // dimensions use the PS1's (value - 1) masks, so zero and other exact
  // multiples encode the maximum dimension rather than an empty transfer.
  const auto width = static_cast<std::uint16_t>(
      ((size_word & 0xffffU) - 1U & 0x03ffU) + 1U);
  const auto height = static_cast<std::uint16_t>(
      (((size_word >> 16U) - 1U) & 0x01ffU) + 1U);
  const auto payload_offset =
      kind == Sf2GpuCommandKind::upload_vram ? 3U : packet.gp0_words.size();
  if (kind == Sf2GpuCommandKind::upload_vram) {
    const auto pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto required_words = (pixel_count + 1U) / 2U;
    if (packet.gp0_words.size() - payload_offset < required_words) {
      return std::nullopt;
    }
  }
  return Sf2GpuTransfer{
      .x = static_cast<std::uint16_t>(coordinate_word & 0x03ffU),
      .y = static_cast<std::uint16_t>((coordinate_word >> 16U) & 0x01ffU),
      .width = width,
      .height = height,
      .payload = std::span<const std::uint32_t>{packet.gp0_words}.subspan(
          payload_offset),
  };
}

std::optional<Sf2PresentationFrame>
captureSf2PresentationFrame(std::span<const std::byte> guest_ram,
                            std::uint32_t ordering_table_root,
                            std::uint32_t application_state,
                            std::uint64_t sequence,
                            std::uint64_t guest_frame) noexcept {
  constexpr std::size_t psx_ram_size = 2U * 1024U * 1024U;
  constexpr std::uint32_t dma_end = 0x00ffffffU;
  constexpr std::size_t maximum_packets = 65'536U;
  constexpr std::size_t maximum_gp0_words = 1U << 20U;
  // Retail SF2's linked primitives top out at the 12-word gouraud textured
  // quad. Larger captured nodes are CPU-side descriptor/list records that can
  // become linked while the hybrid runtime relocates the auxiliary workspace;
  // replaying their arbitrary words as GP0 commands creates phantom UI and
  // full-VRAM copies. Preserve the link, but do not submit that payload.
  constexpr std::size_t maximum_retail_dma_payload_words = 32U;
  auto previous_address = std::uint32_t{};
  auto previous_tag = std::uint32_t{};
  const auto trace_failure = [] {
#if defined(_WIN32)
    char *value{};
    std::size_t size{};
    const auto found =
        _dupenv_s(&value, &size, "SF2_TRACE_OT_CAPTURE_FAILURE") == 0 &&
        value != nullptr;
    std::free(value);
    return found;
#else
    return std::getenv("SF2_TRACE_OT_CAPTURE_FAILURE") != nullptr;
#endif
  }();
  const auto reject = [&](const char *reason, std::uint32_t address,
                          std::uint32_t tag = 0U) {
    if (trace_failure) {
      std::fprintf(stderr,
                   "SF2 OT capture rejected: root=0x%08X address=0x%08X "
                   "tag=0x%08X previous=0x%08X/0x%08X reason=%s\n",
                   ordering_table_root, address, tag, previous_address,
                   previous_tag, reason);
    }
    return std::optional<Sf2PresentationFrame>{};
  };
  if (guest_ram.size() != psx_ram_size || sequence == 0U ||
      (ordering_table_root & 3U) != 0U) {
    return reject("invalid arguments", ordering_table_root);
  }

  const auto read_word = [&](std::uint32_t address,
                             std::uint32_t &value) noexcept {
    const auto offset = address & 0x001fffffU;
    if ((offset & 3U) != 0U || offset > guest_ram.size() - 4U) {
      return false;
    }
    value = static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(guest_ram[offset])) |
            (static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(guest_ram[offset + 1U]))
             << 8U) |
            (static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(guest_ram[offset + 2U]))
             << 16U) |
            (static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(guest_ram[offset + 3U]))
             << 24U);
    return true;
  };

  Sf2PresentationFrame frame{
      .sequence = sequence,
      .guest_frame = guest_frame,
      .application_state = application_state,
      .ordering_table_root = ordering_table_root,
  };
  std::vector<std::uint32_t> visited;
  visited.reserve(4096U);
  auto address = ordering_table_root;
  for (auto packet_index = std::size_t{}; packet_index < maximum_packets;
       ++packet_index) {
    const auto physical_address = address & 0x001fffffU;
    if (std::ranges::find(visited, physical_address) != visited.end()) {
      return reject("DMA chain cycle", address);
    }
    visited.push_back(physical_address);

    std::uint32_t tag{};
    if (!read_word(address, tag)) {
      return reject("unreadable DMA tag", address);
    }
    const auto authored_word_count = static_cast<std::size_t>(tag >> 24U);
    const auto word_count = authored_word_count <= maximum_retail_dma_payload_words
                                ? authored_word_count
                                : std::size_t{};
    if (frame.gp0_word_count > maximum_gp0_words - word_count) {
      return reject("GP0 word limit", address, tag);
    }
    if (word_count != 0U) {
      std::vector<std::uint32_t> payload;
      payload.reserve(word_count);
      for (auto word_index = std::size_t{}; word_index < word_count;
           ++word_index) {
        std::uint32_t word{};
        if (!read_word(address + static_cast<std::uint32_t>(
                                     (word_index + 1U) * 4U),
                       word)) {
          return reject("unreadable DMA payload", address, tag);
        }
        payload.push_back(word);
      }
      frame.gp0_word_count += word_count;
      for (auto payload_offset = std::size_t{};
           payload_offset < payload.size();) {
        const auto remaining =
            std::span<const std::uint32_t>{payload}.subspan(payload_offset);
        const auto command_words = sf2Gp0CommandWordCount(remaining);
        if (!command_words) {
          return reject("malformed GP0 payload", address, tag);
        }
        Sf2GpuPacket packet;
        packet.guest_address =
            0x80000000U |
            ((physical_address + 4U +
              static_cast<std::uint32_t>(payload_offset * 4U)) &
             0x001fffffU);
        packet.gp0_words.assign(
            remaining.begin(),
            remaining.begin() +
                static_cast<std::ptrdiff_t>(*command_words));
        const auto opcode = packet.gp0_words.front() >> 24U;
        if (trace_failure && opcode == 0x80U &&
            packet.gp0_words.size() == 4U) {
          std::fprintf(stderr,
                       "SF2 captured GP0(80): root=0x%08X dma=0x%08X "
                       "tag=0x%08X offset=%zu words=%08X/%08X/%08X/%08X\n",
                       ordering_table_root, address, tag, payload_offset,
                       packet.gp0_words[0U], packet.gp0_words[1U],
                       packet.gp0_words[2U], packet.gp0_words[3U]);
        }
        frame.draw_command_count +=
            opcode >= 0x20U && opcode <= 0x7fU ? 1U : 0U;
        ++frame.gpu_command_count;
        frame.packets.push_back(std::move(packet));
        payload_offset += *command_words;
      }
    }

    const auto next = tag & dma_end;
    if (next == dma_end) {
      return frame.valid()
                 ? std::optional<Sf2PresentationFrame>{std::move(frame)}
                 : std::nullopt;
    }
    previous_address = address;
    previous_tag = tag;
    address = 0x80000000U | next;
  }
  return reject("DMA packet limit", address);
}

Sf2ProjectionMatchStats attachSf2ProjectionProvenance(
    Sf2PresentationFrame &frame,
    std::span<const psx::GteProjectedVertex> observed_vertices,
    std::span<const psx::GteVertexStoreTrace> observed_stores) {
  struct Candidate {
    Sf2GpuVertexProvenance vertex{};
    bool ambiguous{};
  };
  const auto convert = [](const psx::GteProjectedVertex &source) {
    return Sf2GpuVertexProvenance{
        .camera_x_q12 = source.camera_x_q12,
        .camera_y_q12 = source.camera_y_q12,
        .camera_z_q12 = source.camera_z_q12,
        .screen_x_q16 = source.screen_x_q16,
        .screen_y_q16 = source.screen_y_q16,
        .offset_x_q16 = source.offset_x_q16,
        .offset_y_q16 = source.offset_y_q16,
        .packed_sxy = source.packed_sxy,
        .projection = source.projection,
    };
  };
  const auto same_projection = [](const Sf2GpuVertexProvenance &first,
                                  const Sf2GpuVertexProvenance &second) {
    return first.camera_x_q12 == second.camera_x_q12 &&
           first.camera_y_q12 == second.camera_y_q12 &&
           first.camera_z_q12 == second.camera_z_q12 &&
           first.screen_x_q16 == second.screen_x_q16 &&
           first.screen_y_q16 == second.screen_y_q16 &&
           first.offset_x_q16 == second.offset_x_q16 &&
           first.offset_y_q16 == second.offset_y_q16 &&
           first.projection == second.projection;
  };

  auto stats = Sf2ProjectionMatchStats{
      .observed_vertices = observed_vertices.size(),
      .observed_vertex_stores = observed_stores.size(),
  };
  for (auto &packet : frame.packets) {
    packet.projected_vertices = {};
    packet.projected_vertex_count = 0U;
    packet.projected_vertices_address_matched = false;
  }
  std::unordered_map<std::uint32_t, Candidate> candidates;
  candidates.reserve(observed_vertices.size());
  for (const auto &observed : observed_vertices) {
    const auto converted = convert(observed);
    const auto [entry, inserted] =
        candidates.try_emplace(observed.packed_sxy,
                               Candidate{converted, false});
    if (!inserted && !same_projection(entry->second.vertex, converted)) {
      entry->second.ambiguous = true;
    }
  }
  stats.unique_positions = candidates.size();
  stats.ambiguous_positions = static_cast<std::size_t>(std::ranges::count_if(
      candidates, [](const auto &entry) { return entry.second.ambiguous; }));
  std::unordered_map<std::uint32_t, Sf2GpuVertexProvenance> stored_vertices;
  stored_vertices.reserve(observed_stores.size());
  for (const auto &store : observed_stores) {
    stored_vertices[store.address & 0x001fffffU] = convert(store.vertex);
  }

  const auto position_words = [](const Sf2GpuPacket &packet) {
    auto result = std::array<std::size_t, 4U>{};
    auto count = std::size_t{};
    if (packet.gp0_words.empty()) {
      return std::pair{result, count};
    }
    const auto opcode = static_cast<std::uint8_t>(
        packet.gp0_words.front() >> 24U);
    const auto base = static_cast<std::uint8_t>(opcode & 0xfcU);
    switch (base) {
    case 0x20U: result = {1U, 2U, 3U, 0U}; count = 3U; break;
    case 0x24U: result = {1U, 3U, 5U, 0U}; count = 3U; break;
    case 0x28U: result = {1U, 2U, 3U, 4U}; count = 4U; break;
    case 0x2cU: result = {1U, 3U, 5U, 7U}; count = 4U; break;
    case 0x30U: result = {1U, 3U, 5U, 0U}; count = 3U; break;
    case 0x34U: result = {1U, 4U, 7U, 0U}; count = 3U; break;
    case 0x38U: result = {1U, 3U, 5U, 7U}; count = 4U; break;
    case 0x3cU: result = {1U, 4U, 7U, 10U}; count = 4U; break;
    default: break;
    }
    return std::pair{result, count};
  };

  const auto world_packet_end =
      frame.application_state == 0U
          ? (frame.submission_packet_ends.empty()
                 ? frame.packets.size()
                 : std::min(frame.submission_packet_ends.front(),
                            frame.packets.size()))
          : std::size_t{};
  for (auto packet_index = std::size_t{}; packet_index < world_packet_end;
       ++packet_index) {
    auto &packet = frame.packets[packet_index];
    const auto [words, count] = position_words(packet);
    if (count == 0U) {
      continue;
    }
    ++stats.world_polygon_packets;
    auto matched = std::array<Sf2GpuVertexProvenance, 4U>{};
    auto complete = true;
    auto address_matched = true;
    for (auto corner = std::size_t{}; corner < count; ++corner) {
      if (words[corner] >= packet.gp0_words.size()) {
        complete = false;
        break;
      }
      const auto word_address =
          (packet.guest_address + static_cast<std::uint32_t>(
                                      words[corner] * sizeof(std::uint32_t))) &
          0x001fffffU;
      if (const auto stored = stored_vertices.find(word_address);
          stored != stored_vertices.end() &&
          stored->second.packed_sxy == packet.gp0_words[words[corner]]) {
        matched[corner] = stored->second;
      } else {
        address_matched = false;
        if (!observed_stores.empty()) {
          complete = false;
          break;
        }
        const auto candidate =
            candidates.find(packet.gp0_words[words[corner]]);
        if (candidate == candidates.end() || candidate->second.ambiguous) {
          complete = false;
          break;
        }
        matched[corner] = candidate->second.vertex;
      }
      if (matched[corner].camera_z_q12 <= 0 ||
          matched[corner].projection == 0U) {
        complete = false;
        break;
      }
      if (corner != 0U &&
          (matched[corner].projection != matched[0].projection ||
           matched[corner].offset_x_q16 != matched[0].offset_x_q16 ||
           matched[corner].offset_y_q16 != matched[0].offset_y_q16)) {
        complete = false;
        break;
      }
    }
    if (!complete) {
      continue;
    }
    packet.projected_vertices = matched;
    packet.projected_vertex_count = static_cast<std::uint8_t>(count);
    packet.projected_vertices_address_matched = address_matched;
    ++stats.matched_packets;
    stats.matched_vertices += count;
    if (address_matched) {
      ++stats.address_matched_packets;
    }
  }
  frame.projection_matches = stats;
  return stats;
}

void projectSf2GuestHud(
    GameplayHud &hud,
    const Sf2GuestRuntimeDiagnostics &guest,
    bool first_person_aim) noexcept {
  const auto previous_weapon = hud.inventory().current();
  auto &inventory = hud.inventory();
  inventory.resetUnarmed();
  for (auto item = std::size_t{}; item < sf2_inventory_item_count; ++item) {
    const auto owned =
        (guest.player_owned_items[item / 32U] &
         (std::uint32_t{1U} << (item % 32U))) != 0U;
    if (!owned) {
      continue;
    }
    if (const auto weapon =
            sf2WeaponForItem(static_cast<std::uint8_t>(item))) {
      inventory.grant(*weapon, guest.player_magazines[item],
                      guest.player_reserves[item]);
    }
  }
  const auto equipped = sf2WeaponForItem(
      static_cast<std::uint8_t>(guest.player_equipped_item & 0x3fU));
  if (equipped) {
    static_cast<void>(inventory.select(*equipped));
  }
  if (inventory.current() != previous_weapon) {
    hud.notifyWeaponChanged();
  }
  const auto vitals = PlayerVitals{
      .health = guest.player_health,
      .maximum_health = 150U,
      .armor = guest.player_armor,
      .maximum_armor = 600U,
  };
  if (guest.player_health == 0U) {
    hud.synchronizeVitals(vitals);
  } else {
    hud.setVitals(vitals);
  }
  const auto equipped_item = static_cast<std::uint8_t>(
      guest.player_equipped_item & 0x3fU);
  // SF2's retail item ordering keeps its ranged lock-on weapons in slots
  // 1..18 (through the Air Taser), followed by Hand Taser 19 and Knife 20;
  // slot 21 is the M-79. Emulator comparison confirms melee lock-on retains
  // a selected actor but does not raise TARGET.
  const auto ranged_target_weapon =
      (equipped_item >= 1U && equipped_item <= 18U) ||
      equipped_item == 21U;
  hud.setTargetHealth(
      guest.player_target_active && ranged_target_weapon &&
              !first_person_aim
          ? std::optional<std::uint8_t>{
                guest.player_target_health_percent}
          : std::nullopt);
  hud.setDanger(guest.threat_state_valid ? guest.player_danger : 0U,
                guest.threat_state_valid && guest.player_danger == 100U);
}

std::optional<CampaignCarryState>
sf2CampaignCarryState(const Sf2GuestRuntimeDiagnostics &guest) noexcept {
  CampaignCarryState state;
  state.sequel = SequelCampaignCarryState{
      .current_item = static_cast<std::uint8_t>(
          guest.player_equipped_item & 0x3fU),
      .owned_items = guest.player_owned_items,
      .magazines = guest.player_magazines,
      .reserves = guest.player_reserves,
  };
  state.owned_weapons = 1U; // unarmed is always available
  for (auto item = std::size_t{}; item < sf2_inventory_item_count; ++item) {
    const auto owned =
        (guest.player_owned_items[item / 32U] &
         (std::uint32_t{1U} << (item % 32U))) != 0U;
    const auto weapon = sf2WeaponForItem(static_cast<std::uint8_t>(item));
    if (!owned || !weapon) {
      continue;
    }
    const auto slot = static_cast<std::size_t>(*weapon);
    if (slot >= weapon_slot_count ||
        (campaign_persistent_weapon_mask &
         (std::uint32_t{1U} << slot)) == 0U) {
      continue;
    }
    state.owned_weapons |= std::uint32_t{1U} << slot;
    // A few SF2 item IDs are variants of one native weapon slot. They are
    // mutually exclusive in ordinary play; max is deterministic and avoids
    // manufacturing ammunition if a diagnostic state contains both.
    state.magazines[slot] =
        std::max(state.magazines[slot], guest.player_magazines[item]);
    state.reserves[slot] =
        std::max(state.reserves[slot], guest.player_reserves[item]);
  }
  if (const auto equipped = sf2WeaponForItem(static_cast<std::uint8_t>(
          guest.player_equipped_item & 0x3fU))) {
    const auto slot = static_cast<unsigned>(*equipped);
    if (slot < weapon_slot_count &&
        (state.owned_weapons & (std::uint32_t{1U} << slot)) != 0U) {
      state.current_weapon = static_cast<std::uint8_t>(slot);
    }
  }
  if ((state.owned_weapons &
       (std::uint32_t{1U} << state.current_weapon)) == 0U) {
    state.current_weapon = static_cast<std::uint8_t>(
        std::countr_zero(state.owned_weapons));
  }
  state.health = guest.player_health;
  state.armor = guest.player_armor;
  return validCampaignCarry(state)
             ? std::optional<CampaignCarryState>{state}
             : std::nullopt;
}

Sf2SampledMouseAccumulator::Sf2SampledMouseAccumulator(
    std::uint64_t initial_sample) noexcept
    : sample_(initial_sample) {}

void Sf2SampledMouseAccumulator::add(std::uint64_t sample, int delta_x,
                                     int delta_y) noexcept {
  if (sample != sample_) {
    sample_ = sample;
    x_ = 0;
    y_ = 0;
  }
  const auto accumulate = [](int current, int delta) {
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(current) + delta, -384, 384));
  };
  x_ = accumulate(x_, delta_x);
  y_ = accumulate(y_, delta_y);
}

Sf2WeaponSelectPulseQueue::Sf2WeaponSelectPulseQueue(
    std::uint64_t initial_sample) noexcept
    : sample_(initial_sample) {}

void Sf2WeaponSelectPulseQueue::enqueue(unsigned int count) noexcept {
  pending_ = static_cast<unsigned int>(
      std::min<std::uint64_t>(
          static_cast<std::uint64_t>(pending_) + count, maximum_pending));
}

bool Sf2WeaponSelectPulseQueue::update(std::uint64_t sample) noexcept {
  if (sample != sample_) {
    sample_ = sample;
    if (down_) {
      down_ = false;
      may_press_ = false;
    } else {
      may_press_ = true;
    }
  }
  if (!down_ && may_press_ && pending_ != 0U) {
    down_ = true;
    may_press_ = false;
    --pending_;
  }
  return down_;
}

namespace {

struct Sf2SelectableItems {
  std::array<std::uint8_t, sf2_inventory_item_count> items{};
  std::size_t count{};
  std::optional<std::size_t> current;
};

Sf2SelectableItems
sf2SelectableItems(const Sf2GuestRuntimeDiagnostics &guest) noexcept {
  auto result = Sf2SelectableItems{};
  const auto equipped =
      static_cast<std::uint8_t>(guest.player_equipped_item & 0x3fU);
  for (auto item = std::size_t{1U}; item < sf2_inventory_item_count; ++item) {
    const auto owned =
        (guest.player_owned_items[item / 32U] &
         (std::uint32_t{1U} << (item % 32U))) != 0U;
    // Selection is a retail inventory operation and must not depend on the
    // optional projection into SF1's native WeaponId table. H11 (13) and the
    // crossbow (17) have unique sequel presentation/controllers but are still
    // ordinary selectable weapons in SF2's ascending owned-item ring.
    const auto selectable_weapon =
        sf2WeaponForItem(static_cast<std::uint8_t>(item)).has_value() ||
        item == 13U || item == 17U;
    if (!owned || !selectable_weapon) {
      continue;
    }
    result.items[result.count] = static_cast<std::uint8_t>(item);
    if (item == equipped) {
      result.current = result.count;
    }
    ++result.count;
  }
  return result;
}

} // namespace

unsigned int
sf2WeaponCyclePulseCount(const Sf2GuestRuntimeDiagnostics &guest,
                         std::int32_t steps) noexcept {
  const auto selectable = sf2SelectableItems(guest);
  if (steps == 0 || selectable.count < 2U || !selectable.current) {
    return 0U;
  }
  const auto count = static_cast<std::int64_t>(selectable.count);
  const auto current = static_cast<std::int64_t>(*selectable.current);
  auto target = (current + static_cast<std::int64_t>(steps)) % count;
  if (target < 0) {
    target += count;
  }
  return static_cast<unsigned int>((target - current + count) % count);
}

std::optional<unsigned int>
sf2WeaponSlotPulseCount(const Sf2GuestRuntimeDiagnostics &guest,
                        std::size_t slot) noexcept {
  const auto selectable = sf2SelectableItems(guest);
  if (slot >= selectable.count || !selectable.current) {
    return std::nullopt;
  }
  return static_cast<unsigned int>(
      (slot + selectable.count - *selectable.current) % selectable.count);
}

class Sf2GuestMissionRuntime::Impl final {
public:
  Impl(const std::filesystem::path &cue_path, std::uint32_t mission_index,
       Sf2GuestRuntimeStartMode start_mode)
      : disc_(GameDisc::open(cue_path)), vm_(disc_.executable()),
        cdrom_media_(disc_.image()), mission_index_(mission_index),
        start_mode_(start_mode) {
    try {
      if (!disc_.game() ||
          disc_.game()->id != GameId::syphon_filter_2) {
        markFault("SF2 guest runtime requires a supported SF2 disc");
        return;
      }
      const auto resources =
          missionResources(disc_.game()->id, disc_.game()->disc_number);
      const auto runtime_selection = missionRuntimeSelection(
          disc_.game()->id, disc_.game()->disc_number,
          static_cast<std::uint16_t>(mission_index));
      if (!runtime_selection) {
        markFault("SF2 guest runtime has no mapped retail resource");
        return;
      }
      runtime_selection_ = *runtime_selection;
      const auto mission = std::ranges::find(
          resources, runtime_selection_,
          &GameMissionResource::selection_index);
      if (mission == resources.end()) {
        markFault("SF2 guest runtime does not contain the selected mission");
        return;
      }
      if (!vm_.runtime().copyBytes(protected_renderer_begin_,
                                   protected_renderer_baseline_)) {
        markFault("could not capture SF2 resident renderer baseline");
        return;
      }
      fog_path_ = "FOG/" + std::string{mission->resource_name} + ".FOG";
      setStage("asset load");
      loadAssets();
      setStage("platform binding");
      bindPlatform();
      projected_vertices_.reserve(maximum_projected_vertices_);
      vm_.runtime().setGteProjectionObserver(
          [this](const psx::GteProjectionTrace &trace, std::uint32_t,
                 std::uint32_t) {
            if (projected_vertices_overflow_ || trace.count == 0U) {
              return;
            }
            if (trace.count >
                maximum_projected_vertices_ - projected_vertices_.size()) {
              projected_vertices_overflow_ = true;
              projected_vertices_.clear();
              return;
            }
            projected_vertices_.insert(
                projected_vertices_.end(), trace.vertices.begin(),
                trace.vertices.begin() +
                    static_cast<std::ptrdiff_t>(trace.count));
          });
      projected_vertex_stores_.reserve(maximum_projected_vertices_);
      vm_.runtime().setGteVertexStoreObserver(
          [this](const psx::GteVertexStoreTrace &store, std::uint32_t,
                 std::uint32_t) {
            if (projected_vertices_overflow_) {
              return;
            }
            if (projected_vertex_stores_.size() >=
                maximum_projected_vertices_) {
              projected_vertices_overflow_ = true;
              projected_vertices_.clear();
              projected_vertex_stores_.clear();
              return;
            }
            projected_vertex_stores_.push_back(store);
          });
      configureUiInstructionTrace();
      // Arm before TITLE-to-mission transition work: bootstrap itself builds
      // and
      // submits ordering tables, and a bad tag written here can remain
      // dormant until gameplay later enters the damaged renderer branch.
      // The retail callback trampoline at 0x8001DA9C is intentionally
      // self-cleared on first use. Protect the resident renderer around that
      // one proven mutable word.
      vm_.runtime().setWriteWatch(0x8001d000U, 0x8001da9cU);
      vm_.runtime().addWriteWatch(0x8001daa0U, 0x8001f000U);
      vm_.runtime().setWriteTrace(0x8014f000U, 0x80169000U);
      vm_.runtime().setWriteTracePc(0x800133b4U, 0x800133dcU);
      if (!bootstrap()) {
        if (!faulted_) {
          markFault("retail TITLE-to-mission bootstrap failed at " + stage_);
        }
        return;
      }
      if (start_mode_ == Sf2GuestRuntimeStartMode::gameplay) {
        normalizeInitialAuxiliaryRendererState();
      }
      std::array<std::byte, protected_renderer_size_> protected_renderer{};
      if (!vm_.runtime().copyBytes(protected_renderer_begin_,
                                   protected_renderer)) {
        markFault("could not verify SF2 resident renderer after bootstrap");
        return;
      }
      const auto read_word = [](const auto &bytes,
                                std::size_t index) {
          return static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[index])) |
                 (static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index + 1U]))
                  << 8U) |
                 (static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index + 2U]))
                  << 16U) |
                 (static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index + 3U]))
                  << 24U);
      };
      auto mismatch_offset = protected_renderer_size_;
      for (auto offset = std::size_t{};
           offset < protected_renderer_size_;) {
        if (protected_renderer_baseline_[offset] ==
            protected_renderer[offset]) {
          ++offset;
          continue;
        }
        const auto word_offset = offset & ~std::size_t{3U};
        // Retail's callback trampoline patches this one JALR after its first
        // continuation. It is the proven bounded mutation previously seen by
        // the write watch, not ordering-table ownership.
        if (word_offset == 0x0a9cU &&
            read_word(protected_renderer_baseline_, word_offset) ==
                0x01204009U &&
            read_word(protected_renderer, word_offset) == 0U) {
          offset = word_offset + sizeof(std::uint32_t);
          continue;
        }
        mismatch_offset = offset;
        break;
      }
      if (mismatch_offset != protected_renderer_size_) {
        const auto word_offset =
            mismatch_offset & ~std::size_t{3U};
        const auto hex = [](std::uint32_t value) {
          constexpr char digits[] = "0123456789ABCDEF";
          std::string text(8U, '0');
          for (auto index = std::size_t{}; index < text.size(); ++index) {
            text[text.size() - index - 1U] =
                digits[(value >> (index * 4U)) & 0x0fU];
          }
          return text;
        };
        markFault(
            "bootstrap changed resident renderer text at 0x" +
            hex(protected_renderer_begin_ +
                static_cast<std::uint32_t>(word_offset)) +
            " expected=0x" +
            hex(read_word(protected_renderer_baseline_, word_offset)) +
            " actual=0x" +
            hex(read_word(protected_renderer, word_offset)));
        return;
      }
      // The one proven callback patch above is part of the canonical
      // post-bootstrap renderer image and must not be undone by gameplay
      // integrity repair.
      protected_renderer_baseline_ = protected_renderer;
      if (const auto &write_watch = vm_.runtime().writeWatchHit();
          write_watch.width != 0U) {
        const auto hex = [] (std::uint32_t value) {
          constexpr char digits[] = "0123456789ABCDEF";
          std::string text(8U, '0');
          for (auto index = std::size_t{}; index < text.size(); ++index) {
            text[text.size() - index - 1U] =
                digits[(value >> (index * 4U)) & 0x0fU];
          }
          return text;
        };
        markFault(
            "bootstrap resident renderer code write: address=0x" +
            hex(write_watch.address) + " value=0x" +
            hex(write_watch.value) + " width=" +
            std::to_string(write_watch.width) + " writer-pc=0x" +
            hex(write_watch.pc) + " writer-instruction=0x" +
            hex(write_watch.instruction));
        return;
      }
      // Bootstrap completed without modifying resident renderer text. From
      // this point a write is always corruption. Suppress it at the RAM
      // boundary so an OTC completion and a following renderer call in the
      // same guest slice cannot execute damaged text.
      vm_.runtime().clearWriteWatchHit();
      vm_.runtime().setBreakOnWriteWatch(false);
      vm_.runtime().setSuppressWriteWatch(true);
      realtime_display_clock_ = true;
      vm_.clearPcm();
      ready_ = true;
    } catch (const std::exception &error) {
      markFault(error.what());
    }
  }

  ~Impl() {
    vm_.setHostCallObserver({});
    vm_.runtime().setExecutionObserver({});
    vm_.runtime().setGteProjectionObserver({});
    vm_.runtime().setGteVertexStoreObserver({});
    flushUiInstructionTrace();
  }

  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] bool faulted() const noexcept { return faulted_; }
  [[nodiscard]] std::string_view faultDetail() const noexcept {
    return fault_detail_.empty() ? std::string_view{"none"}
                                 : std::string_view{fault_detail_};
  }
  void setHostPadState(const LegacyHostPadState &state) noexcept {
    host_pad_ = state;
  }
  void requestRetailBriefingConfirm() noexcept {
    briefing_confirm_requested_ = true;
    briefing_confirm_pulse_pending_ = true;
  }
  void setRetailAuxiliaryUiEnabled(bool enabled) noexcept {
    disable_retail_auxiliary_ui_ = !enabled;
  }
  [[nodiscard]] bool
  dispatchScriptEventForProbe(std::uint32_t event,
                              std::uint32_t selector) noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    const std::array arguments{event, selector, 0U, 9U};
    const auto dispatched = invokeNested(0x800b38b0U, arguments);
    return dispatched.completed() || dispatched.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool
  activateScriptProgramForProbe(std::string_view name) noexcept {
    constexpr std::uint32_t probe_name_address = 0x1f800300U;
    if (!ready_ || faulted_ || name.empty() || name.size() >= 64U) {
      return false;
    }
    std::array<std::byte, 64U> text{};
    for (auto index = std::size_t{}; index < name.size(); ++index) {
      text[index] =
          static_cast<std::byte>(static_cast<unsigned char>(name[index]));
    }
    if (!vm_.runtime().loadBytes(
            probe_name_address,
            std::span<const std::byte>{text}.first(name.size() + 1U))) {
      return false;
    }
    const std::array lookup_arguments{probe_name_address};
    const auto lookup = invokeNested(0x800b3920U, lookup_arguments);
    if ((!lookup.completed() && !lookup.stoppedAtHostBoundary()) ||
        lookup.return_value == 0U) {
      return false;
    }
    const std::array activation_arguments{lookup.return_value};
    const auto activation =
        invokeNested(0x800b39e4U, activation_arguments);
    return activation.completed() || activation.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool
  startSceneSpeechForProbe(std::uint16_t cue) noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    const std::array arguments{static_cast<std::uint32_t>(cue)};
    const auto started = invokeNested(0x800b246cU, arguments);
    return started.completed() || started.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool stopSceneSpeechForProbe() noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    const std::array arguments{0U};
    const auto stopped = invokeNested(0x800b26a0U, arguments);
    return stopped.completed() || stopped.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool setPlayerPositionForProbe(
      std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
    std::uint32_t instance{};
    std::int32_t current_x{};
    std::int32_t current_y{};
    std::int32_t current_z{};
    std::uint16_t health{};
    std::uint16_t armor{};
    if (!ready_ || faulted_) {
      return false;
    }
    readPlayerState(instance, current_x, current_y, current_z, health, armor);
    if (instance == 0U) {
      return false;
    }
    // Retail keeps three synchronized player transforms: the instance's
    // authored matrix and two physics/render working matrices. Writing only
    // the public node matrix is undone on the next logic tick. Restrict the
    // diagnostic search to the mission heap and replace every exact copy of
    // the current translation so the authoritative owner moves with its
    // consumers.
    constexpr std::uint32_t mission_heap_begin = 0x80180000U;
    constexpr std::size_t mission_heap_size = 0x40000U;
    std::vector<std::byte> heap(mission_heap_size);
    if (!vm_.runtime().copyBytes(mission_heap_begin, heap)) {
      return false;
    }
    const std::array current{
        std::bit_cast<std::uint32_t>(current_x),
        std::bit_cast<std::uint32_t>(current_y),
        std::bit_cast<std::uint32_t>(current_z),
    };
    const std::array target{
        std::bit_cast<std::uint32_t>(x),
        std::bit_cast<std::uint32_t>(y),
        std::bit_cast<std::uint32_t>(z),
    };
    auto replacements = std::size_t{};
    for (auto offset = std::size_t{}; offset + sizeof(current) <= heap.size();
         offset += sizeof(std::uint32_t)) {
      std::array<std::uint32_t, 3U> candidate{};
      std::memcpy(candidate.data(), heap.data() + offset, sizeof(candidate));
      if (candidate != current) {
        continue;
      }
      const auto address =
          mission_heap_begin + static_cast<std::uint32_t>(offset);
      if (!vm_.runtime().write32(address, target[0U]) ||
          !vm_.runtime().write32(address + 4U, target[1U]) ||
          !vm_.runtime().write32(address + 8U, target[2U])) {
        return false;
      }
      ++replacements;
    }
    return replacements >= 3U;
  }
  [[nodiscard]] bool
  setPlayerRoomForProbe(std::uint16_t room) noexcept {
    constexpr std::uint32_t collision_owner_handle_pointer = 0x8012a654U;
    constexpr std::uint32_t collision_owner_room_offset = 0x1a0U;
    std::uint32_t owner_handle{};
    std::uint32_t owner{};
    return ready_ && !faulted_ &&
           vm_.runtime().read32(collision_owner_handle_pointer,
                                owner_handle) &&
           owner_handle != 0U &&
           vm_.runtime().read32(owner_handle, owner) && owner != 0U &&
           vm_.runtime().write16(owner + collision_owner_room_offset, room);
  }
  [[nodiscard]] bool exerciseRawCdSyncWaitForProbe() noexcept {
    constexpr std::uint32_t raw_cd_result = 0x8011cf5cU;
    constexpr std::uint32_t raw_cd_started_at = 0x8011cf60U;
    constexpr std::uint32_t raw_cd_outer_started_at = 0x8011cf64U;
    constexpr std::uint32_t raw_cd_callback = 0x8011cf6cU;
    std::uint32_t retrace{};
    if (!ready_ || faulted_ ||
        !vm_.runtime().read32(
            disc_.game()->executable_layout.retrace_counter_address,
            retrace) ||
        !vm_.runtime().write32(raw_cd_result, 1U) ||
        !vm_.runtime().write32(raw_cd_started_at, retrace) ||
        !vm_.runtime().write32(raw_cd_outer_started_at, retrace) ||
        !vm_.runtime().write32(raw_cd_callback, 0U)) {
      return false;
    }
    const auto slices_before = device_wait_scheduler_slices_;
    const std::array arguments{0U, 0U};
    const auto synchronized = invokeNested(0x800f7704U, arguments);
    if ((!synchronized.completed() &&
         !synchronized.stoppedAtHostBoundary()) ||
        device_wait_scheduler_slices_ == slices_before) {
      const auto hex = [](std::uint32_t value) {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string text(8U, '0');
        for (auto index = std::size_t{}; index < text.size(); ++index) {
          text[text.size() - index - 1U] =
              digits[(value >> (index * 4U)) & 0x0fU];
        }
        return text;
      };
      markFault("RawCdSync probe reason=" +
                std::string{psx::toString(synchronized.execution.reason)} +
                " pc=0x" + hex(synchronized.execution.pc) + " ra=0x" +
                hex(vm_.runtime().state().gpr[31U]) + " slices=" +
                 std::to_string(device_wait_scheduler_slices_ -
                                slices_before));
      return false;
    }
    return true;
  }
  [[nodiscard]] bool exerciseCleanMissionRestartForProbe() noexcept {
    if (!ready_ || faulted_ || checkpoint_captured_) {
      return false;
    }
    const auto restart =
        invokeNested(0x800ad9f4U, std::span<const std::uint32_t>{});
    return restart.yieldedAfterHostCall() && mission_restart_requested_;
  }
  [[nodiscard]] bool exerciseOpeningMissionRestartForProbe() noexcept {
    if (!ready_ || faulted_ || checkpoint_captured_ ||
        !initial_checkpoint_deferred_by_opening_event_ ||
        script_level_starts_ == 0U) {
      return false;
    }
    const auto restart =
        invokeNested(0x800b3d34U, std::span<const std::uint32_t>{});
    return restart.yieldedAfterHostCall() && mission_restart_requested_;
  }
  [[nodiscard]] bool
  exercisePauseMenuLifecycleForProbe(bool save_and_quit) noexcept {
    constexpr std::uint32_t menu_save_and_quit_callback = 0x80143568U;
    constexpr std::uint32_t menu_restart_mission_callback = 0x80143624U;
    std::uint32_t application_state{};
    if (!ready_ || faulted_ ||
        !vm_.runtime().read32(profile_.application_state,
                              application_state) ||
        application_state != 7U) {
      return false;
    }
    const std::array arguments{1U};
    const auto result = invokeNested(
        save_and_quit ? menu_save_and_quit_callback
                      : menu_restart_mission_callback,
        arguments);
    return result.yieldedAfterHostCall() &&
           (save_and_quit ? quit_to_title_requested_
                          : mission_restart_requested_);
  }
  [[nodiscard]] bool
  setPlayerHealthForProbe(std::uint16_t health) noexcept {
    constexpr std::uint32_t player_pointer = 0x8012a574U;
    constexpr std::uint32_t instance_health_offset = 0x18U;
    constexpr std::uint32_t health_value_offset = 0x08U;
    std::uint32_t instance{};
    std::uint32_t controller{};
    return ready_ && !faulted_ &&
           vm_.runtime().read32(player_pointer, instance) && instance != 0U &&
           vm_.runtime().read32(instance + instance_health_offset,
                                controller) &&
           controller != 0U &&
           vm_.runtime().write16(controller + health_value_offset, health);
  }
  [[nodiscard]] bool alignPlayerAimToLockedTarget() noexcept {
    // Retail's transition seeds manual aim from lower-body yaw. Preserve the
    // selected actor bearing until 0x800539D0, where the completed retail
    // camera angles are consumed; earlier transform/controller writes are
    // overwritten during the transition itself.
    constexpr std::uint32_t player_pointer = 0x8012a574U;
    constexpr std::uint32_t application_state = 0x8011ee90U;
    constexpr std::uint32_t object_records_pointer = 0x8011eef8U;
    constexpr std::uint32_t object_count_address = 0x8011f564U;
    constexpr std::uint32_t object_record_stride = 0x4cU;
    constexpr std::uint32_t object_instance_offset = 0x34U;
    constexpr std::uint32_t object_health_offset = 0x40U;
    constexpr std::uint32_t instance_node_offset = 0x08U;
    constexpr std::uint32_t instance_target_offset = 0x14U;
    constexpr std::uint32_t target_slot_offset = 0U;
    constexpr std::uint32_t node_matrix_offset = 0x0cU;
    constexpr std::uint32_t matrix_translation_offset = 0x14U;
    constexpr std::uint32_t maximum_objects = 1024U;

    std::uint32_t state{};
    std::uint32_t player{};
    std::uint32_t player_node{};
    std::uint32_t player_matrix{};
    std::uint32_t target_controller{};
    std::uint16_t target_slot_bits{};
    std::uint32_t records{};
    std::uint32_t count_bits{};
    if (!ready_ || faulted_ ||
        !vm_.runtime().read32(application_state, state) || state != 0U ||
        !vm_.runtime().read32(player_pointer, player) || player == 0U ||
        !vm_.runtime().read32(player + instance_node_offset, player_node) ||
        player_node == 0U ||
        !vm_.runtime().read32(player_node + node_matrix_offset,
                              player_matrix) ||
        player_matrix == 0U ||
        !vm_.runtime().read32(player + instance_target_offset,
                              target_controller) ||
        target_controller == 0U ||
        !vm_.runtime().read16(target_controller + target_slot_offset,
                              target_slot_bits) ||
        !vm_.runtime().read32(object_records_pointer, records) ||
        records == 0U ||
        !vm_.runtime().read32(object_count_address, count_bits)) {
      return false;
    }
    const auto count = std::bit_cast<std::int32_t>(count_bits);
    const auto target_slot = std::bit_cast<std::int16_t>(target_slot_bits);
    if (count <= 0 || static_cast<std::uint32_t>(count) > maximum_objects ||
        target_slot < 0 || target_slot >= count) {
      return false;
    }

    const auto target_record =
        records + static_cast<std::uint32_t>(target_slot) *
                      object_record_stride;
    std::uint16_t target_health_bits{};
    std::uint32_t target_instance{};
    std::uint32_t target_node{};
    std::uint32_t target_matrix{};
    std::uint32_t player_x_bits{};
    std::uint32_t player_z_bits{};
    std::uint32_t target_x_bits{};
    std::uint32_t target_z_bits{};
    if (!vm_.runtime().read16(target_record + object_health_offset,
                              target_health_bits) ||
        std::bit_cast<std::int16_t>(target_health_bits) <= 0 ||
        !vm_.runtime().read32(target_record + object_instance_offset,
                              target_instance) ||
        target_instance == 0U || target_instance == player ||
        !vm_.runtime().read32(target_instance + instance_node_offset,
                              target_node) ||
        target_node == 0U ||
        !vm_.runtime().read32(target_node + node_matrix_offset,
                              target_matrix) ||
        target_matrix == 0U ||
        !vm_.runtime().read32(player_matrix + matrix_translation_offset,
                              player_x_bits) ||
        !vm_.runtime().read32(player_matrix + matrix_translation_offset + 8U,
                              player_z_bits) ||
        !vm_.runtime().read32(target_matrix + matrix_translation_offset,
                              target_x_bits) ||
        !vm_.runtime().read32(target_matrix + matrix_translation_offset + 8U,
                              target_z_bits)) {
      return false;
    }
    const auto bearing = sf2FacingAngle(
        static_cast<std::int64_t>(std::bit_cast<std::int32_t>(target_x_bits)) -
            std::bit_cast<std::int32_t>(player_x_bits),
        static_cast<std::int64_t>(std::bit_cast<std::int32_t>(target_z_bits)) -
            std::bit_cast<std::int32_t>(player_z_bits));
    if (!bearing) {
      return false;
    }
    pc_manual_aim_snap_yaw_ = static_cast<std::int32_t>(*bearing);
    return true;
  }
  void setPcManualAimInput(std::int32_t yaw_delta,
                           std::int32_t pitch_delta,
                           bool enabled) noexcept {
    pc_manual_aim_enabled_ = enabled;
    if (!enabled) {
      pc_manual_aim_yaw_pending_ = 0;
      pc_manual_aim_pitch_pending_ = 0;
      pc_manual_aim_snap_yaw_.reset();
      return;
    }
    pc_manual_aim_yaw_pending_ = std::clamp(
        pc_manual_aim_yaw_pending_ + yaw_delta, -1024, 1024);
    pc_manual_aim_pitch_pending_ = std::clamp(
        pc_manual_aim_pitch_pending_ + pitch_delta, -1024, 1024);
  }
  void setPcChaseCameraYawInput(std::int32_t delta,
                                bool enabled) noexcept {
    pc_chase_yaw_enabled_ = enabled;
    if (!enabled) {
      pc_chase_yaw_pending_ = 0;
      return;
    }
    pc_chase_yaw_pending_ = std::clamp<std::int32_t>(
        pc_chase_yaw_pending_ + delta, -1024, 1024);
  }
  void setPcChaseCameraPitchInput(std::int32_t delta,
                                  bool enabled) noexcept {
    pc_chase_pitch_enabled_ = enabled;
    if (!enabled) {
      pc_chase_pitch_pending_ = 0;
      pc_chase_pitch_valid_ = false;
      pc_chase_scripted_camera_seen_ = false;
      pc_chase_camera_base_ = 0U;
      return;
    }
    pc_chase_pitch_pending_ = std::clamp<std::int32_t>(
        pc_chase_pitch_pending_ + delta, -96, 96);
  }

  void setPcHorizontalProjectionScale(std::uint32_t scale_q16) noexcept {
    // Do not permit a zero or magnifying projection through this surface.
    // 4:3 is 1.0; wider outputs supply a smaller positive Q16 factor.
    vm_.runtime().setGteHorizontalProjectionScale(
        std::clamp(scale_q16, 1U, 0x10000U));
  }
  [[nodiscard]] bool setObjectRecordHealthForProbe(
      std::uint16_t source_index, std::int16_t health) noexcept {
    constexpr std::uint32_t object_records_pointer = 0x8011eef8U;
    constexpr std::uint32_t object_count_address = 0x8011f564U;
    constexpr std::uint32_t object_record_stride = 0x4cU;
    constexpr std::uint32_t object_health_offset = 0x40U;
    std::uint32_t records{};
    std::uint32_t count_bits{};
    if (!ready_ || faulted_ ||
        !vm_.runtime().read32(object_records_pointer, records) ||
        records == 0U ||
        !vm_.runtime().read32(object_count_address, count_bits)) {
      return false;
    }
    const auto count = std::bit_cast<std::int32_t>(count_bits);
    return count > 0 && source_index < count &&
           vm_.runtime().write16(
               records + static_cast<std::uint32_t>(source_index) *
                             object_record_stride +
                   object_health_offset,
               std::bit_cast<std::uint16_t>(health));
  }

  [[nodiscard]] std::optional<Sf2GuestObjectProbeState>
  objectStateForProbe(std::uint16_t source_index) const noexcept {
    constexpr std::uint32_t records_pointer = 0x8011eef8U;
    constexpr std::uint32_t count_address = 0x8011f564U;
    constexpr std::uint32_t record_stride = 0x4cU;
    constexpr std::uint32_t instance_offset = 0x34U;
    std::uint32_t records{};
    std::uint32_t count_bits{};
    if (!vm_.runtime().read32(records_pointer, records) || records == 0U ||
        !vm_.runtime().read32(count_address, count_bits) ||
        source_index >= count_bits) {
      return std::nullopt;
    }
    Sf2GuestObjectProbeState state{};
    state.source_index = source_index;
    const auto record = records +
        static_cast<std::uint32_t>(source_index) * record_stride;
    std::uint16_t health_bits{};
    std::uint32_t record_x_bits{};
    std::uint32_t record_y_bits{};
    std::uint32_t record_z_bits{};
    if (!vm_.runtime().read8(record + 0x2aU, state.object_class) ||
        !vm_.runtime().read8(record + 0x27U, state.object_flags) ||
        !vm_.runtime().read32(record + 0x18U, record_x_bits) ||
        !vm_.runtime().read32(record + 0x1cU, record_y_bits) ||
        !vm_.runtime().read32(record + 0x20U, record_z_bits) ||
        !vm_.runtime().read16(record + 0x40U, health_bits) ||
        !vm_.runtime().read32(record + instance_offset, state.instance)) {
      return std::nullopt;
    }
    state.health = std::bit_cast<std::int16_t>(health_bits);
    state.record_x = std::bit_cast<std::int32_t>(record_x_bits);
    state.record_y = std::bit_cast<std::int32_t>(record_y_bits);
    state.record_z = std::bit_cast<std::int32_t>(record_z_bits);
    if (state.instance == 0U) {
      return state;
    }
    std::uint16_t actor_index_bits{};
    if (!vm_.runtime().read16(state.instance + 0x02U,
                              actor_index_bits) ||
        !vm_.runtime().read32(state.instance + 0x08U,
                              state.physics_node) ||
        !vm_.runtime().read32(state.instance + 0x0cU,
                              state.render_node) ||
        !vm_.runtime().read32(state.instance + 0x14U, state.target) ||
        !vm_.runtime().read32(state.instance + 0x1cU,
                              state.actor_controller)) {
      return std::nullopt;
    }
    state.actor_index = std::bit_cast<std::int16_t>(actor_index_bits);
    if (state.actor_controller != 0U) {
      for (auto index = std::size_t{};
           index < state.actor_controller_words.size(); ++index) {
        if (!vm_.runtime().read32(
                state.actor_controller +
                    static_cast<std::uint32_t>(index * 4U),
                state.actor_controller_words[index])) {
          return std::nullopt;
        }
      }
    }
    if (state.target != 0U) {
      for (auto index = std::size_t{}; index < state.target_words.size();
           ++index) {
        if (!vm_.runtime().read32(
                state.target + static_cast<std::uint32_t>(index * 4U),
                state.target_words[index])) {
          return std::nullopt;
        }
      }
    }
    if (state.physics_node == 0U) {
      return state;
    }
    if (!vm_.runtime().read32(state.physics_node, state.physics_model) ||
        !vm_.runtime().read32(state.physics_node + 0x0cU, state.matrix)) {
      return std::nullopt;
    }
    if (state.render_node != 0U) {
      std::uint16_t minimum_y_bits{};
      if (!vm_.runtime().read32(state.render_node + 0x104U,
                                state.render_flags) ||
          !vm_.runtime().read32(state.render_node + 0x18cU,
                                state.render_next) ||
          !vm_.runtime().read16(state.render_node + 0x10aU,
                                minimum_y_bits)) {
        return std::nullopt;
      }
      state.motion_minimum_y =
          std::bit_cast<std::int16_t>(minimum_y_bits);
      for (auto component = std::size_t{}; component < 3U; ++component) {
        std::uint32_t position_bits{};
        std::uint32_t velocity_bits{};
        if (!vm_.runtime().read32(
                state.render_node + 0x40U +
                    static_cast<std::uint32_t>(component * 4U),
                position_bits) ||
            !vm_.runtime().read32(
                state.render_node + 0x50U +
                    static_cast<std::uint32_t>(component * 4U),
                velocity_bits)) {
          return std::nullopt;
        }
        state.motion_position[component] =
            std::bit_cast<std::int32_t>(position_bits);
        state.motion_velocity[component] =
            std::bit_cast<std::int32_t>(velocity_bits);
      }
      for (auto index = std::size_t{};
           index < state.motion_ground_words.size(); ++index) {
        if (!vm_.runtime().read32(
                state.render_node + 0x120U +
                    static_cast<std::uint32_t>(index * 4U),
                state.motion_ground_words[index])) {
          return std::nullopt;
        }
      }
    }
    if (state.matrix != 0U) {
      std::uint32_t x{};
      std::uint32_t y{};
      std::uint32_t z{};
      if (!vm_.runtime().read32(state.matrix + 0x14U, x) ||
          !vm_.runtime().read32(state.matrix + 0x18U, y) ||
          !vm_.runtime().read32(state.matrix + 0x1cU, z)) {
        return std::nullopt;
      }
      state.x = std::bit_cast<std::int32_t>(x);
      state.y = std::bit_cast<std::int32_t>(y);
      state.z = std::bit_cast<std::int32_t>(z);
    }
    return state;
  }
  [[nodiscard]] bool
  traceObjectMatrixYForProbe(std::uint16_t source_index) noexcept {
    const auto state = objectStateForProbe(source_index);
    if (!state || state->physics_node == 0U) {
      return false;
    }
    // Diagnostic: the bounds builder consumes the model matrix table pointer
    // at physics-node +0x18. Trace its lifecycle to distinguish an absent
    // model attachment from a later erroneous clear.
    vm_.runtime().setWriteTrace(state->physics_node + 0x18U,
                                state->physics_node + 0x1cU);
    vm_.runtime().setWriteTracePc(0U, 0U);
    return true;
  }
  [[nodiscard]] std::optional<std::uint16_t>
  scriptProgramVariableForProbe(std::string_view name,
                                std::uint16_t index) noexcept {
    constexpr std::uint32_t probe_name_address = 0x1f800300U;
    if (!ready_ || faulted_ || name.empty() || name.size() >= 64U) {
      return std::nullopt;
    }
    std::array<std::byte, 64U> text{};
    for (auto cursor = std::size_t{}; cursor < name.size(); ++cursor) {
      text[cursor] =
          static_cast<std::byte>(static_cast<unsigned char>(name[cursor]));
    }
    if (!vm_.runtime().loadBytes(
            probe_name_address,
            std::span<const std::byte>{text}.first(name.size() + 1U))) {
      return std::nullopt;
    }
    const std::array lookup_arguments{probe_name_address};
    const auto lookup = invokeNested(0x800b3920U, lookup_arguments);
    std::uint8_t count{};
    std::uint32_t variables{};
    std::uint16_t value{};
    if ((!lookup.completed() && !lookup.stoppedAtHostBoundary()) ||
        lookup.return_value == 0U ||
        !vm_.runtime().read8(lookup.return_value + 1U, count) ||
        index >= count ||
        !vm_.runtime().read32(lookup.return_value + 0x0cU, variables) ||
        variables == 0U ||
        !vm_.runtime().read16(
            variables + static_cast<std::uint32_t>(index) * 2U, value)) {
      return std::nullopt;
    }
    return value;
  }
  [[nodiscard]] bool setMissionProgressBitForProbe(
      std::uint16_t bit, bool enabled) noexcept {
    constexpr std::uint32_t progress_pointer = 0x8011f570U;
    constexpr std::uint32_t progress_flags_offset = 0x1cU;
    std::uint32_t progress{};
    std::uint32_t flags{};
    if (!ready_ || faulted_ || bit >= 32U ||
        !vm_.runtime().read32(progress_pointer, progress) || progress == 0U ||
        !vm_.runtime().read32(progress + progress_flags_offset, flags)) {
      return false;
    }
    const auto mask = std::uint32_t{1U} << bit;
    flags = enabled ? flags | mask : flags & ~mask;
    return vm_.runtime().write32(progress + progress_flags_offset, flags);
  }
  [[nodiscard]] bool startPlayerObjectInteractionForProbe(
      std::uint32_t selector) noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    const std::array arguments{selector, 0U, 0U};
    const auto interaction = invokeNested(0x80041054U, arguments);
    return interaction.completed() || interaction.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool
  requestScriptedMovieForProbe(std::uint8_t catalog_index) noexcept {
    constexpr auto sf2_movie_catalog_capacity = 27U;
    if (!ready_ || faulted_ ||
        catalog_index >= sf2_movie_catalog_capacity) {
      return false;
    }
    const std::array arguments{static_cast<std::uint32_t>(catalog_index)};
    const auto request = invokeNested(0x8002c558U, arguments);
    return request.completed() || request.stoppedAtHostBoundary() ||
           request.yieldedAfterHostCall();
  }
  [[nodiscard]] std::optional<std::uint8_t>
  consumeScriptedMovieRequest() noexcept {
    if (!pending_scripted_movie_catalog_index_ ||
        active_scripted_movie_catalog_index_) {
      return std::nullopt;
    }
    active_scripted_movie_catalog_index_ =
        pending_scripted_movie_catalog_index_;
    pending_scripted_movie_catalog_index_.reset();
    return active_scripted_movie_catalog_index_;
  }
  [[nodiscard]] bool
  completeScriptedMovie(std::uint8_t catalog_index) noexcept {
    if (!ready_ || faulted_ ||
        active_scripted_movie_catalog_index_ != catalog_index) {
      return false;
    }
    // Decoder init is a synchronous call inside MovieLoader (0x8002C188).
    // Native playback replaced that call and yielded at its return PC. Let
    // MovieLoader execute its authentic zero-result setup (0x8002C0BC) and
    // return to its caller before invoking the asynchronous completion path.
    // Calling MovieCompletion while still inside MovieLoader leaves stale
    // mission callbacks installed; Missions 19/20 expose that immediately.
    if (!scripted_movie_host_yielded_ ||
        vm_.runtime().state().pc != 0x8002c27cU) {
      return false;
    }
    std::uint32_t movie_loader_return{};
    if (!vm_.runtime().read32(vm_.runtime().state().gpr[29U] + 0x24U,
                              movie_loader_return)) {
      return false;
    }
    scripted_movie_host_yielded_ = false;
    const auto loader = runUntilBoundary(movie_loader_return);
    if (!loader.stoppedAtHostBoundary()) {
      return false;
    }
    // All movie globals and the application-state stack are now authentic.
    // Retail MovieCompletion owns decoder teardown, restores the prior state,
    // and emits the exact 0x001E0000 | catalog_index script event.
    const auto completion = invokeNested(0x8002be68U, {});
    if (!completion.completed() && !completion.stoppedAtHostBoundary()) {
      return false;
    }
    active_scripted_movie_catalog_index_.reset();
    scripted_movie_host_yielded_ = false;
    return true;
  }
  [[nodiscard]] bool requestMissionSuccessForProbe() noexcept {
    if (!ready_ || faulted_ || mission_complete_requested_) {
      return false;
    }
    // invokeNested stops at a bound entry before executing the original
    // function. Temporarily remove only the success-entry observer, seed the
    // same observation it would have made, and retain the shared 0x8002D8D0
    // observer. Reaching that callback therefore still proves retail's real
    // success routine selected the terminal outcome path.
    if (!vm_.unbindHostCall(0x8002d6d8U)) {
      return false;
    }
    // Direct bootstrap retains the archive-order selection during gameplay.
    // Disc 2's overlays consult that value, so translate to the player-facing
    // campaign cursor only at the exact terminal handoff.
    if (!vm_.runtime().write16(
            0x8012b02cU,
            static_cast<std::uint16_t>(mission_index_))) {
      return false;
    }
    completion_flow_trace_active_ = true;
    vm_.runtime().setWriteTrace(0x801279a8U, 0x801279a9U);
    vm_.runtime().setWriteTracePc(0U, 0U);
    ++mission_success_events_;
    mission_success_pending_ = true;
    // a0=1 is the routine's verified force-through argument. The authored
    // action normally passes zero after satisfying every objective; a probe
    // cannot cheaply reproduce those mission-specific predicates, so it
    // exercises the identical accepted branch explicitly.
    const std::array arguments{1U};
    const auto result = invokeNested(0x8002d6d8U, arguments);
    if (const auto &trace = vm_.runtime().writeTraceHit();
        trace.width != 0U) {
      ++movie_selection_writes_;
      last_movie_selection_writer_pc_ = trace.pc;
      last_movie_selection_writer_instruction_ = trace.instruction;
      last_movie_selection_write_value_ = trace.value;
      const auto slot = static_cast<std::size_t>(
          (movie_selection_writes_ - 1U) % movie_selection_writer_pcs_.size());
      movie_selection_writer_pcs_[slot] = trace.pc;
      movie_selection_write_values_[slot] = trace.value;
      vm_.runtime().clearWriteTraceHit();
    }
    vm_.bindHostCall(
        0x8002d6d8U, [this](LegacyHostCallContext &context) {
          if (!context.write16(
                  0x8012b02cU,
                  static_cast<std::uint16_t>(mission_index_))) {
            context.rejectHostCall();
            return;
          }
          ++mission_success_events_;
          mission_success_pending_ = true;
          context.continueGuestInstruction();
        });
    return result.completed() || result.stoppedAtHostBoundary();
  }
  [[nodiscard]] bool resumeMissionShellForProbe() noexcept {
    if (!ready_ || faulted_ || !mission_complete_requested_) {
      return false;
    }
    mission_complete_requested_ = false;
    mission_success_pending_ = false;
    completion_flow_trace_active_ = true;
    vm_.runtime().setWriteTrace(0x801279a8U, 0x801279a9U);
    vm_.runtime().setWriteTracePc(0U, 0U);
    return true;
  }
  [[nodiscard]] bool
  applyCampaignCarryState(const CampaignCarryState &state) noexcept {
    if (!ready_ || faulted_ || !validCampaignCarry(state)) {
      return false;
    }
    const auto item_for_weapon = [](WeaponId weapon)
        -> std::optional<std::uint8_t> {
      switch (weapon) {
      case WeaponId::unarmed: return std::uint8_t{0U};
      case WeaponId::silenced_9mm: return std::uint8_t{1U};
      case WeaponId::pistol_9mm: return std::uint8_t{2U};
      case WeaponId::knife: return std::uint8_t{20U};
      case WeaponId::pistol_45: return std::uint8_t{3U};
      case WeaponId::g_18: return std::uint8_t{10U};
      case WeaponId::combat_shotgun: return std::uint8_t{9U};
      case WeaponId::shotgun: return std::uint8_t{8U};
      case WeaponId::pk_102: return std::uint8_t{7U};
      case WeaponId::m_16: return std::uint8_t{4U};
      case WeaponId::biz_2: return std::uint8_t{11U};
      case WeaponId::hk_5: return std::uint8_t{5U};
      case WeaponId::nightvision_rifle: return std::uint8_t{16U};
      case WeaponId::sniper_rifle: return std::uint8_t{14U};
      case WeaponId::taser: return std::uint8_t{18U};
      case WeaponId::m_79: return std::uint8_t{21U};
      case WeaponId::k3g4: return std::uint8_t{12U};
      case WeaponId::fragmentation_grenade: return std::uint8_t{22U};
      case WeaponId::gas_grenade: return std::uint8_t{23U};
      default: return std::nullopt;
      }
    };
    constexpr std::uint32_t player_pointer = 0x8012a574U;
    constexpr std::uint32_t health_pointer_offset = 0x18U;
    constexpr std::uint32_t inventory_pointer_offset = 0x20U;
    constexpr std::uint32_t health_armor_offset = 0x06U;
    constexpr std::uint32_t health_value_offset = 0x08U;
    constexpr std::uint32_t inventory_owned_offset = 0x3cU;
    constexpr std::uint32_t inventory_ammo_offset = 0x44U;
    constexpr std::uint32_t inventory_equipped_offset = 0xccU;
    std::uint32_t player{};
    std::uint32_t health{};
    std::uint32_t inventory{};
    std::array<std::uint32_t, 2U> owned{};
    if (!vm_.runtime().read32(player_pointer, player) || player == 0U ||
        !vm_.runtime().read32(player + health_pointer_offset, health) ||
        health == 0U ||
        !vm_.runtime().read32(player + inventory_pointer_offset, inventory) ||
        inventory == 0U ||
        !vm_.runtime().read32(inventory + inventory_owned_offset, owned[0U]) ||
        !vm_.runtime().read32(inventory + inventory_owned_offset + 4U,
                              owned[1U])) {
      return false;
    }
    const auto snapshot = vm_.captureSnapshot();
    auto committed = false;
    if (state.sequel) {
      auto exact_inventory_written = true;
      for (auto item = std::size_t{}; item < sf2_inventory_item_count; ++item) {
        const auto ammo =
            static_cast<std::uint32_t>(state.sequel->reserves[item]) |
            (static_cast<std::uint32_t>(state.sequel->magazines[item]) <<
             16U);
        exact_inventory_written =
            exact_inventory_written &&
            vm_.runtime().write32(
                inventory + inventory_ammo_offset +
                    static_cast<std::uint32_t>(item * 4U),
                ammo);
      }
      committed =
          exact_inventory_written &&
          vm_.runtime().write32(inventory + inventory_owned_offset,
                                state.sequel->owned_items[0U]) &&
          vm_.runtime().write32(inventory + inventory_owned_offset + 4U,
                                state.sequel->owned_items[1U]) &&
          vm_.runtime().write32(inventory + inventory_equipped_offset,
                                state.sequel->current_item) &&
          vm_.runtime().write16(health + health_value_offset, state.health) &&
          vm_.runtime().write16(health + health_armor_offset, state.armor);
    } else {
      // V4 and older saves contain only the native weapon projection. Keep
      // their compatible import path while new SF2 saves use the exact block.
      for (auto item = std::size_t{}; item < sf2_inventory_item_count; ++item) {
        const auto mapped = sf2WeaponForItem(static_cast<std::uint8_t>(item));
        if (!mapped ||
            (campaign_persistent_weapon_mask &
             (std::uint32_t{1U} << static_cast<unsigned>(*mapped))) == 0U) {
          continue;
        }
        owned[item / 32U] &= ~(std::uint32_t{1U} << (item % 32U));
        if (!vm_.runtime().write32(
                inventory + inventory_ammo_offset +
                    static_cast<std::uint32_t>(item * 4U),
                0U)) {
          static_cast<void>(vm_.restoreSnapshot(snapshot));
          return false;
        }
      }
      for (auto slot = std::size_t{}; slot < weapon_slot_count; ++slot) {
        if ((state.owned_weapons & (std::uint32_t{1U} << slot)) == 0U) {
          continue;
        }
        const auto item = item_for_weapon(static_cast<WeaponId>(slot));
        if (!item || *item == 0U) {
          continue;
        }
        owned[*item / 32U] |= std::uint32_t{1U} << (*item % 32U);
        const auto ammo = static_cast<std::uint32_t>(state.reserves[slot]) |
                          (static_cast<std::uint32_t>(state.magazines[slot])
                           << 16U);
        if (!vm_.runtime().write32(
                inventory + inventory_ammo_offset +
                    static_cast<std::uint32_t>(*item * 4U),
                ammo)) {
          static_cast<void>(vm_.restoreSnapshot(snapshot));
          return false;
        }
      }
      const auto equipped =
          item_for_weapon(static_cast<WeaponId>(state.current_weapon));
      committed = equipped &&
          vm_.runtime().write32(inventory + inventory_owned_offset,
                                owned[0U]) &&
          vm_.runtime().write32(inventory + inventory_owned_offset + 4U,
                                owned[1U]) &&
          vm_.runtime().write32(inventory + inventory_equipped_offset,
                                *equipped) &&
          vm_.runtime().write16(health + health_value_offset, state.health) &&
          vm_.runtime().write16(health + health_armor_offset, state.armor);
    }
    if (!committed) {
      static_cast<void>(vm_.restoreSnapshot(snapshot));
      return false;
    }
    const auto checkpoint =
        invokeNested(0x800ad48cU, std::span<const std::uint32_t>{});
    if (!checkpoint.completed() && !checkpoint.stoppedAtHostBoundary()) {
      static_cast<void>(vm_.restoreSnapshot(snapshot));
      return false;
    }
    if (!checkpointStateReady()) {
      static_cast<void>(vm_.restoreSnapshot(snapshot));
      return false;
    }
    // A failure restart must return to the carried chapter state rather than
    // the package's standalone defaults captured during bootstrap.
    checkpoint_captured_ = true;
    return true;
  }
  [[nodiscard]] const std::shared_ptr<const Sf2PresentationFrame> &
  presentationFrame() const noexcept {
    return presentation_frame_;
  }
  [[nodiscard]] std::size_t
  takePcm(std::span<psx::SpuPcmFrame> destination) noexcept {
    // Retail checkpoint restore suspends its sound-service callback while the
    // synchronous mission loader owns the guest. The SPU hardware clock still
    // advances during that interval, so exporting its loader-era mixture would
    // refill the freshly reset host sink with seconds of stale PCM. Treat the
    // restore as a stream-generation boundary and expose only PCM produced
    // after retail has returned to stable app-state-0 submissions.
    if (retail_restore_active_) {
      checkpoint_audio_discarded_frames_ +=
          vm_.audioDiagnostics().spu_pcm_frames;
      vm_.clearPcm();
      return 0U;
    }
    return vm_.takePcm(destination);
  }
  void clearPcm() noexcept { vm_.clearPcm(); }
  [[nodiscard]] std::uint64_t inputSampleCount() const noexcept {
    return host_pad_samples_;
  }
  [[nodiscard]] bool captureQuickState() noexcept {
    try {
      QuickState state;
      state.vm = vm_.captureSnapshot();
      state.open_files = open_files_;
      state.gpu_gp0_stream = gpu_gp0_stream_;
      state.gpu_gp0_scan = gpu_gp0_scan_;
      state.gpu_draw_environment_words = gpu_draw_environment_words_;
      state.gpu_draw_environment_valid = gpu_draw_environment_valid_;
      state.vram_setup_packets = vram_setup_packets_;
      state.menu_gameplay_vram_setup_packets =
          menu_gameplay_vram_setup_packets_;
      state.previous_application_state = previous_application_state_;
      state.menu_transition_active = menu_transition_active_;
      state.pause_menu_lifecycle_active = pause_menu_lifecycle_active_;
      state.pause_menu_player_was_present = pause_menu_player_was_present_;
      state.pending_immediate_gpu_packets = pending_immediate_gpu_packets_;
      state.presentation_frame = presentation_frame_;
      state.pending_presentation = pending_presentation_;
      state.pending_presentation_clock = pending_presentation_clock_;
      state.published_sequence = published_sequence_;
      state.active_script_programs = active_script_programs_;
      state.callback_ticks = callback_ticks_;
      state.audio_callback_ticks = audio_callback_ticks_;
      state.retrace_ticks = retrace_ticks_;
      state.presentation_sequence = presentation_sequence_;
      state.guest_frame = guest_frame_;
      state.mission_pad_polls = mission_pad_polls_;
      state.host_pad_samples = host_pad_samples_;
      state.suppress_interrupts = suppress_interrupts_;
      state.realtime_display_clock = realtime_display_clock_;
      state.scripts_started = scripts_started_;
      state.loading_confirm_sent = loading_confirm_sent_;
      state.checkpoint_captured = checkpoint_captured_;
      state.checkpoint_capture_frame = checkpoint_capture_frame_;
      state.retail_checkpoint_capture_calls =
          retail_checkpoint_capture_calls_;
      state.retail_checkpoint_capture_pending =
          retail_checkpoint_capture_pending_;
      state.mission_restart_requested = mission_restart_requested_;
      state.quit_to_title_requested = quit_to_title_requested_;
      state.scripted_camera_observed = scripted_camera_observed_;
      state.initial_checkpoint_deferred_by_opening_event =
          initial_checkpoint_deferred_by_opening_event_;
      state.retail_restore_active = retail_restore_active_;
      state.retail_restore_start_frame = retail_restore_start_frame_;
      state.xa_absolute_disc_active = xa_absolute_disc_active_;
      state.mission_success_pending = mission_success_pending_;
      state.mission_complete_requested = mission_complete_requested_;
      state.mission_success_events = mission_success_events_;
      state.mission_failure_events = mission_failure_events_;
      state.scripted_movie_handoffs = scripted_movie_handoffs_;
      state.last_scripted_movie_catalog_index =
          last_scripted_movie_catalog_index_;
      state.pending_scripted_movie_catalog_index =
          pending_scripted_movie_catalog_index_;
      state.active_scripted_movie_catalog_index =
          active_scripted_movie_catalog_index_;
      state.scripted_movie_host_yielded = scripted_movie_host_yielded_;
      state.ui_text_event_count = ui_text_event_count_;
      state.ui_text_events = ui_text_events_;
      state.mission_timer_handle = mission_timer_handle_;
      state.mission_timer_text = mission_timer_text_;
      state.mission_timer_text_updates = mission_timer_text_updates_;
      quick_state_ = std::move(state);
      return true;
    } catch (...) {
      return false;
    }
  }
  [[nodiscard]] bool restoreQuickState() noexcept {
    if (!quick_state_) {
      return false;
    }
    try {
      // Complete every potentially allocating host copy before mutating the
      // live guest so an allocation failure cannot leave a half-restored
      // process.
      auto open_files = quick_state_->open_files;
      auto gpu_gp0_stream = quick_state_->gpu_gp0_stream;
      auto vram_setup_packets = quick_state_->vram_setup_packets;
      auto pending_immediate_gpu_packets =
          quick_state_->pending_immediate_gpu_packets;
      auto active_script_programs = quick_state_->active_script_programs;
      if (!vm_.restoreSnapshot(quick_state_->vm)) {
        return false;
      }
      open_files_.swap(open_files);
      // The published frame is the visible half of the save state. Restoring
      // it immediately avoids a black/stale native frame and, critically,
      // prevents advanceHostUpdate() from consuming extra guest GPU
      // boundaries while it searches for a replacement authored OT.
      presentation_frame_ = quick_state_->presentation_frame;
      pending_presentation_ = quick_state_->pending_presentation;
      pending_presentation_clock_ =
          quick_state_->pending_presentation_clock;
      // Projection provenance is a host presentation observation, not guest
      // checkpoint state. Never match pre-restore transforms to packets built
      // after the restored boundary; an incomplete restored composition falls
      // back to retail affine replay until a fresh generation is published.
      projected_vertices_.clear();
      projected_vertex_stores_.clear();
      projected_vertices_overflow_ = false;
      published_sequence_ = quick_state_->published_sequence;
      gpu_gp0_stream_.swap(gpu_gp0_stream);
      vram_setup_packets_.swap(vram_setup_packets);
      menu_gameplay_vram_setup_packets_ =
          quick_state_->menu_gameplay_vram_setup_packets;
      previous_application_state_ = quick_state_->previous_application_state;
      menu_transition_active_ = quick_state_->menu_transition_active;
      pause_menu_lifecycle_active_ =
          quick_state_->pause_menu_lifecycle_active;
      pause_menu_player_was_present_ =
          quick_state_->pause_menu_player_was_present;
      pending_immediate_gpu_packets_.swap(pending_immediate_gpu_packets);
      active_script_programs_.swap(active_script_programs);
      gpu_gp0_scan_ = quick_state_->gpu_gp0_scan;
      gpu_draw_environment_words_ =
          quick_state_->gpu_draw_environment_words;
      gpu_draw_environment_valid_ =
          quick_state_->gpu_draw_environment_valid;
      callback_ticks_ = quick_state_->callback_ticks;
      audio_callback_ticks_ = quick_state_->audio_callback_ticks;
      retrace_ticks_ = quick_state_->retrace_ticks;
      presentation_sequence_ = quick_state_->presentation_sequence;
      guest_frame_ = quick_state_->guest_frame;
      mission_pad_polls_ = quick_state_->mission_pad_polls;
      mission_pad_record_ = 0U;
      host_pad_samples_ = quick_state_->host_pad_samples;
      suppress_interrupts_ = quick_state_->suppress_interrupts;
      realtime_display_clock_ = quick_state_->realtime_display_clock;
      scripts_started_ = quick_state_->scripts_started;
      loading_confirm_sent_ = quick_state_->loading_confirm_sent;
      checkpoint_captured_ = quick_state_->checkpoint_captured;
      checkpoint_capture_frame_ = quick_state_->checkpoint_capture_frame;
      retail_checkpoint_capture_calls_ =
          quick_state_->retail_checkpoint_capture_calls;
      retail_checkpoint_capture_pending_ =
          quick_state_->retail_checkpoint_capture_pending;
      mission_restart_requested_ =
          quick_state_->mission_restart_requested;
      quit_to_title_requested_ = quick_state_->quit_to_title_requested;
      scripted_camera_observed_ = quick_state_->scripted_camera_observed;
      initial_checkpoint_deferred_by_opening_event_ =
          quick_state_->initial_checkpoint_deferred_by_opening_event;
      retail_restore_active_ = quick_state_->retail_restore_active;
      retail_restore_start_frame_ =
          quick_state_->retail_restore_start_frame;
      setXaAbsoluteDiscActive(quick_state_->xa_absolute_disc_active);
      mission_success_pending_ = quick_state_->mission_success_pending;
      mission_complete_requested_ =
          quick_state_->mission_complete_requested;
      mission_success_events_ = quick_state_->mission_success_events;
      mission_failure_events_ = quick_state_->mission_failure_events;
      scripted_movie_handoffs_ = quick_state_->scripted_movie_handoffs;
      last_scripted_movie_catalog_index_ =
          quick_state_->last_scripted_movie_catalog_index;
      pending_scripted_movie_catalog_index_ =
          quick_state_->pending_scripted_movie_catalog_index;
      active_scripted_movie_catalog_index_ =
          quick_state_->active_scripted_movie_catalog_index;
      scripted_movie_host_yielded_ =
          quick_state_->scripted_movie_host_yielded;
      ui_text_event_count_ = quick_state_->ui_text_event_count;
      ui_text_events_ = quick_state_->ui_text_events;
      mission_timer_handle_ = quick_state_->mission_timer_handle;
      mission_timer_text_ = quick_state_->mission_timer_text;
      mission_timer_text_updates_ = quick_state_->mission_timer_text_updates;
      // Timeline counters intentionally describe the whole host run rather
      // than guest state, but the bounded callback window is expressed in
      // guest-frame coordinates. Do not compare a restored earlier frame to
      // the pre-restore speech frame; the next authentic speech start arms a
      // fresh window.
      last_scene_speech_timeline_frame_.reset();
      scheduler_fault_detail_.clear();
      vm_.runtime().clearWriteWatchHit();
      last_valid_collision_room_ = 0xffffU;
      stabilizeCollisionRoom();
      vm_.clearPcm();
      return true;
    } catch (...) {
      return false;
    }
  }
  [[nodiscard]] bool hasQuickState() const noexcept {
    return quick_state_.has_value();
  }
  [[nodiscard]] core::Sha256Digest guestRamDigestForProbe() const noexcept {
    return core::sha256(vm_.runtime().ram());
  }
  [[nodiscard]] bool
  copyGuestRamForProbe(std::span<std::byte> destination) const noexcept {
    const auto ram = vm_.runtime().ram();
    if (destination.size() != ram.size()) {
      return false;
    }
    std::ranges::copy(ram, destination.begin());
    return true;
  }
  [[nodiscard]] Sf2GuestRuntimeDiagnostics diagnostics() const noexcept {
    auto result = diagnostics_;
    const auto &state = vm_.runtime().state();
    result.pc = state.pc;
    result.stack_pointer = state.gpr[29U];
    result.return_address = state.gpr[31U];
    result.global_pointer = state.gpr[28U];
    result.last_pad_caller = last_pad_caller_;
    result.last_pad_index = last_pad_index_;
    result.pc_chase_pitch_hook_calls = pc_chase_pitch_hook_calls_;
    result.pc_chase_camera_base = pc_chase_camera_base_;
    result.pc_chase_camera_flags = pc_chase_camera_flags_;
    result.pc_chase_interaction_suspensions =
        pc_chase_interaction_suspensions_;
    result.pc_chase_desired_pitch = pc_chase_pitch_target_;
    result.pc_chase_rendered_pitch = pc_chase_rendered_pitch_;
    result.pc_chase_yaw_hook_calls = pc_chase_yaw_hook_calls_;
    result.pc_chase_yaw_command = pc_chase_yaw_command_;
    result.pc_manual_aim_hook_calls = pc_manual_aim_hook_calls_;
    result.pc_manual_aim_yaw_command = pc_manual_aim_yaw_command_;
    result.pc_manual_aim_pitch_command = pc_manual_aim_pitch_command_;
    readPlayerState(result.player_instance, result.player_x, result.player_y,
                    result.player_z, result.player_health,
                    result.player_armor);
    if (readPlayerCameraOwnership(result.player_instance,
                                  result.player_camera_wrapper,
                                  result.player_camera_owner)) {
      result.player_owns_camera =
          result.player_camera_owner == result.player_instance;
    }
    result.checkpoint_captured = checkpoint_captured_;
    result.checkpoint_capture_frame = checkpoint_capture_frame_;
    result.retail_checkpoint_capture_calls =
        retail_checkpoint_capture_calls_;
    result.mission_restart_requested = mission_restart_requested_;
    result.quit_to_title_requested = quit_to_title_requested_;
    result.scripted_camera_observed = scripted_camera_observed_;
    result.initial_checkpoint_deferred_by_opening_event =
        initial_checkpoint_deferred_by_opening_event_;
    std::uint32_t collision_owner_handle{};
    std::uint32_t collision_owner{};
    if (vm_.runtime().read32(0x8012a654U, collision_owner_handle) &&
        collision_owner_handle != 0U &&
        vm_.runtime().read32(collision_owner_handle, collision_owner) &&
        collision_owner != 0U) {
      static_cast<void>(vm_.runtime().read16(
          collision_owner + 0x1a0U, result.guest_current_room));
    }
    static_cast<void>(vm_.runtime().read32(
        0x8011f660U, result.guest_collision_room_count));
    std::uint32_t collision_room_table{};
    if (result.guest_current_room < result.guest_collision_room_count &&
        vm_.runtime().read32(0x8011f680U, collision_room_table) &&
        collision_room_table != 0U) {
      static_cast<void>(vm_.runtime().read32(
          collision_room_table +
              static_cast<std::uint32_t>(result.guest_current_room) * 0x40U,
          result.guest_collision_room_record));
      if (result.guest_collision_room_record != 0U) {
        static_cast<void>(vm_.runtime().read32(
            result.guest_collision_room_record,
            result.guest_collision_list));
      }
    }
    readPlayerHudState(result.player_instance, result);
    result.threat_state_valid =
        readPlayerThreatState(result.player_instance, result);
    result.dialogue_state_valid = true;
    for (auto index = std::size_t{};
         index < result.dialogue_state_words.size(); ++index) {
      result.dialogue_state_valid =
          vm_.runtime().read32(
              0x80134d1cU + static_cast<std::uint32_t>(index * 0x10U),
              result.dialogue_state_words[index]) &&
          result.dialogue_state_valid;
    }
    result.dialogue_state_word = result.dialogue_state_words.front();
    std::uint32_t mission_progress{};
    if (vm_.runtime().read32(0x8011f570U, mission_progress) &&
        mission_progress != 0U) {
      static_cast<void>(vm_.runtime().read32(
          mission_progress + 0x04U,
          result.mission_progress_visible_bits));
    }
    std::uint32_t player_state_root{};
    std::uint32_t player_packed_state_record{};
    if (vm_.runtime().read32(0x8012a574U, player_state_root) &&
        player_state_root != 0U &&
        vm_.runtime().read32(player_state_root + 0x20U,
                             player_packed_state_record) &&
        player_packed_state_record != 0U) {
      static_cast<void>(vm_.runtime().read32(
          player_packed_state_record + 0x540U,
          result.player_packed_state_pointer));
      constexpr auto predicate_operand = std::uint32_t{51U};
      const auto tagged_slot = result.player_packed_state_pointer & 3U;
      const auto bit = predicate_operand + tagged_slot * 8U;
      const auto bitset = result.player_packed_state_pointer & ~3U;
      if (bitset != 0U && vm_.runtime().read32(
                              bitset + (bit >> 5U) * 4U,
                              result.player_packed_state_bit51_word)) {
        result.player_packed_state_bit51 =
            (result.player_packed_state_bit51_word &
             (1U << (bit & 31U))) != 0U;
      }
    }
    // MissionObjective_Complete (0x8002E6E8) sets bits in word +0x0C of
    // the state record rooted at GP+0x90C (0x8011F570). Read that exact
    // retail state so the native overlay can reproduce completion notices
    // without guessing from mission-specific triggers.
    std::uint32_t objective_state{};
    result.objective_state_valid =
        vm_.runtime().read32(0x8011f570U, objective_state) &&
        objective_state != 0U &&
        vm_.runtime().read32(objective_state + 0x0cU,
                             result.objective_completion_bits);
    result.objective_completion_events = objective_completion_events_;
    result.last_objective_completion_index =
        last_objective_completion_index_;
    result.last_objective_completion_text =
        last_objective_completion_text_;
    result.pickup_presentation_events = pickup_presentation_events_;
    result.last_pickup_actor = last_pickup_actor_;
    result.last_pickup_text = last_pickup_text_;
    result.last_pickup_item = last_pickup_item_;
    result.last_pickup_text_words = last_pickup_text_words_;
    result.last_pickup_text_bytes = last_pickup_text_bytes_;
    result.last_restore_caller = last_restore_caller_;
    result.pre_restore_player_x = pre_restore_player_x_;
    result.pre_restore_player_y = pre_restore_player_y_;
    result.pre_restore_player_z = pre_restore_player_z_;
    result.pre_restore_player_health = pre_restore_player_health_;
    result.last_damage_caller = last_damage_caller_;
    result.last_damage_arguments = last_damage_arguments_;
    result.damage_events = damage_events_;
    result.last_player_damage_caller = last_player_damage_caller_;
    result.last_player_damage_request = last_player_damage_request_;
    result.player_damage_events = player_damage_events_;
    result.world_collision_scans = world_collision_scans_;
    result.last_world_collision_caller = last_world_collision_caller_;
    result.last_world_collision_object = last_world_collision_object_;
    result.last_world_collision_room = last_world_collision_room_;
    result.player_floor_probes = player_floor_probes_;
    result.player_floor_probe_true = player_floor_probe_true_;
    result.player_floor_probe_false = player_floor_probe_false_;
    result.player_floor_false_streak = player_floor_false_streak_;
    result.maximum_player_floor_false_streak =
        maximum_player_floor_false_streak_;
    result.collision_room_fallbacks = collision_room_fallbacks_;
    result.last_collision_room_fallback =
        last_collision_room_fallback_;
    result.collision_request_fallbacks =
        collision_request_fallbacks_;
    result.last_collision_request_fallback =
        last_collision_request_fallback_;
    result.player_collision_requests = player_collision_requests_;
    result.invalid_player_collision_requests =
        invalid_player_collision_requests_;
    result.renderer_text_repairs = renderer_text_repairs_;
    result.last_renderer_text_repair_address =
        last_renderer_text_repair_address_;
    result.last_renderer_text_expected = last_renderer_text_expected_;
    result.last_renderer_text_actual = last_renderer_text_actual_;
    result.last_renderer_text_writer_pc =
        last_renderer_text_writer_pc_;
    result.last_renderer_text_writer_instruction =
        last_renderer_text_writer_instruction_;
    result.render_view_adds = render_view_adds_;
    result.render_view_removes = render_view_removes_;
    result.last_render_view_added = last_render_view_added_;
    result.last_render_view_removed = last_render_view_removed_;
    static_cast<void>(vm_.runtime().read32(
        vm_.runtime().state().gpr[28U] + 0x4fcU,
        result.render_view_head));
    if (result.player_instance != 0U &&
        vm_.runtime().read32(result.player_instance + 0x0cU,
                             result.player_render_node) &&
        result.player_render_node != 0U) {
      static_cast<void>(vm_.runtime().read32(
          result.player_render_node + 0x104U,
          result.player_render_flags));
      static_cast<void>(vm_.runtime().read32(
          result.player_render_node + 0x18cU,
          result.player_render_next));
    }
    auto render_view = result.render_view_head;
    for (auto index = std::size_t{};
         index < result.render_view_chain.size() && render_view != 0U;
         ++index) {
      result.render_view_chain[index] = render_view;
      std::uint32_t node{};
      if (!vm_.runtime().read32(render_view + 0x0cU, node) || node == 0U) {
        break;
      }
      result.render_view_chain_nodes[index] = node;
      static_cast<void>(vm_.runtime().read32(
          node + 0x104U, result.render_view_chain_flags[index]));
      if (!vm_.runtime().read32(node + 0x18cU, render_view)) {
        break;
      }
    }
    result.rejected_renderer_ordering_tables =
        rejected_renderer_ordering_tables_;
    result.observed_gpu_submissions = observed_gpu_submissions_;
    result.last_gpu_submission_root = last_gpu_submission_root_;
    result.last_gpu_submission_draw_count =
        last_gpu_submission_draw_count_;
    result.last_gpu_submission_packet_count =
        last_gpu_submission_packet_count_;
    result.last_gpu_submission_copy_count =
        last_gpu_submission_copy_count_;
    result.last_gpu_submission_upload_count =
        last_gpu_submission_upload_count_;
    result.last_gpu_submission_copy = last_gpu_submission_copy_;
    result.last_gpu_submission_copy_source_x =
        last_gpu_submission_copy_source_x_;
    result.last_gpu_submission_copy_source_y =
        last_gpu_submission_copy_source_y_;
    result.last_gpu_submission_clock = last_gpu_submission_clock_;
    result.last_gpu_submission_draw_buffer =
        last_gpu_submission_draw_buffer_;
    result.last_gpu_submission_build_buffer =
        last_gpu_submission_build_buffer_;
    result.last_gpu_submission_first_draw_packet =
        last_gpu_submission_first_draw_packet_;
    result.last_gpu_submission_last_draw_packet =
        last_gpu_submission_last_draw_packet_;
    result.text_renderer_calls = text_renderer_calls_;
    result.text_renderer = text_renderer_;
    result.text_renderer_flags = text_renderer_flags_;
    result.text_renderer_list_heads = text_renderer_list_heads_;
    result.hud_primitive_registrations = hud_primitive_registrations_;
    result.hud_primitive_registration_callers =
        hud_primitive_registration_callers_;
    result.hud_primitive_registration_packets =
        hud_primitive_registration_packets_;
    result.hud_primitive_registration_roots =
        hud_primitive_registration_roots_;
    result.hud_primitive_registration_counts =
        hud_primitive_registration_counts_;
    result.hud_primitive_registration_buffer_masks =
        hud_primitive_registration_buffer_masks_;
    result.hud_primitive_writes = hud_primitive_writes_;
    result.hud_primitive_writer_pcs = hud_primitive_writer_pcs_;
    result.hud_primitive_writer_addresses = hud_primitive_writer_addresses_;
    result.hud_primitive_writer_instructions =
        hud_primitive_writer_instructions_;
    result.hud_primitive_writer_counts = hud_primitive_writer_counts_;
    result.hud_primitive_writer_buffer_masks =
        hud_primitive_writer_buffer_masks_;
    result.last_rejected_renderer_packet =
        last_rejected_renderer_packet_;
    result.last_rejected_renderer_root = last_rejected_renderer_root_;
    result.last_rejected_renderer_frame = last_rejected_renderer_frame_;
    result.rejected_renderer_vertex_entries =
        rejected_renderer_vertex_entries_;
    result.last_rejected_renderer_vertex_cursor =
        last_rejected_renderer_vertex_cursor_;
    result.last_rejected_renderer_vertex_address =
        last_rejected_renderer_vertex_address_;
    result.clamped_renderer_ordering_table_entries =
        clamped_renderer_ordering_table_entries_;
    result.last_renderer_ordering_table_requested =
        last_renderer_ordering_table_requested_;
    result.last_renderer_ordering_table_clamped =
        last_renderer_ordering_table_clamped_;
    result.last_renderer_ordering_table_base =
        last_renderer_ordering_table_base_;
    result.last_renderer_ordering_table_buckets =
        last_renderer_ordering_table_buckets_;
    result.rejected_renderer_list_merges =
        rejected_renderer_list_merges_;
    result.last_rejected_renderer_list_descriptor =
        last_rejected_renderer_list_descriptor_;
    result.last_rejected_renderer_list_root =
        last_rejected_renderer_list_root_;
    result.last_rejected_renderer_list_cursor =
        last_rejected_renderer_list_cursor_;
    result.last_rejected_renderer_list_tag =
        last_rejected_renderer_list_tag_;
    result.room_texture_activations = room_texture_activations_;
    result.room_texture_page_requests = room_texture_page_requests_;
    result.room_texture_upload_completions =
        room_texture_upload_completions_;
    result.retail_load_image_calls = retail_load_image_calls_;
    result.retained_retail_load_images =
        retained_retail_load_images_;
    result.retail_load_image_call_sites =
        retail_load_image_call_sites_;
    result.unknown_retail_load_image_call_sites =
        unknown_retail_load_image_call_sites_;
    result.last_room_texture_activation =
        last_room_texture_activation_;
    result.last_room_texture_page = last_room_texture_page_;
    result.last_room_texture_bank = last_room_texture_bank_;
    result.last_retail_load_image_caller =
        last_retail_load_image_caller_;
    result.last_retail_load_image_transfer =
        last_retail_load_image_transfer_;
    result.rejected_sound_bank_lookups =
        rejected_sound_bank_lookups_;
    result.last_rejected_sound_bank = last_rejected_sound_bank_;
    result.last_rejected_sound_bank_table =
        last_rejected_sound_bank_table_;
    result.last_rejected_sound_bank_index =
        last_rejected_sound_bank_index_;
    result.last_rejected_sound_bank_caller =
        last_rejected_sound_bank_caller_;
    result.last_rejected_sound_bank_magic =
        last_rejected_sound_bank_magic_;
    result.last_rejected_sound_bank_entry_count =
        last_rejected_sound_bank_entry_count_;
    result.rejected_sound_voice_updates =
        rejected_sound_voice_updates_;
    result.last_rejected_sound_voice =
        last_rejected_sound_voice_;
    result.last_rejected_sound_voice_caller =
        last_rejected_sound_voice_caller_;
    result.retained_vram_setup_packets = vram_setup_packets_.size();
    result.menu_vram_snapshots = menu_vram_snapshots_;
    result.menu_vram_restores = menu_vram_restores_;
    for (const auto &packet : vram_setup_packets_) {
      if (packet.guest_address == 0U ||
          sf2GpuCommandKind(packet) !=
              Sf2GpuCommandKind::upload_vram) {
        continue;
      }
      const auto transfer = sf2GpuTransfer(packet);
      if (!transfer) {
        continue;
      }
      result.retained_retail_upload_halfwords +=
          static_cast<std::uint64_t>(transfer->width) *
          transfer->height;
      const auto first_page_x = transfer->x / 64U;
      const auto last_page_x =
          (static_cast<std::uint32_t>(transfer->x) +
           transfer->width - 1U) /
          64U;
      const auto first_page_y = transfer->y / 256U;
      const auto last_page_y =
          (static_cast<std::uint32_t>(transfer->y) +
           transfer->height - 1U) /
          256U;
      for (auto page_y = first_page_y; page_y <= last_page_y;
           ++page_y) {
        for (auto page_x = first_page_x; page_x <= last_page_x;
             ++page_x) {
          const auto page = page_x + page_y * 16U;
          if (page < 32U) {
            result.retained_retail_texture_page_mask |=
                1U << page;
          }
        }
      }
      if (transfer->x < 384U && transfer->y < 480U) {
        ++result.retained_retail_framebuffer_rectangles;
      }
      if (transfer->width >= 320U && transfer->height >= 200U) {
        ++result.retained_retail_fullscreen_rectangles;
      }
      if (transfer->y >= 480U) {
        if (result.retained_retail_clut_rectangles <
            result.retained_retail_clut_transfers.size()) {
          result.retained_retail_clut_transfers[
              result.retained_retail_clut_rectangles] =
              static_cast<std::uint64_t>(transfer->x) |
              (static_cast<std::uint64_t>(transfer->y) << 16U) |
              (static_cast<std::uint64_t>(transfer->width) << 32U) |
              (static_cast<std::uint64_t>(transfer->height) << 48U);
        }
        ++result.retained_retail_clut_rectangles;
      }
    }
    const auto audio = vm_.audioDiagnostics();
    result.spu_mixed_frames = audio.spu_mixed_frames;
    result.spu_pcm_frames = audio.spu_pcm_frames;
    result.spu_dropped_pcm_frames = audio.spu_dropped_pcm_frames;
    const auto &spu_state = vm_.machine().spu().state();
    result.spu_key_on_writes = spu_state.key_on_writes;
    result.spu_key_off_writes = spu_state.key_off_writes;
    result.spu_last_key_on_mask = spu_state.last_key_on_mask;
    result.spu_last_key_off_mask = spu_state.last_key_off_mask;
    result.spu_endx = spu_state.endx;
    for (auto voice = std::size_t{}; voice < spu_state.voices.size();
         ++voice) {
      const auto &voice_state = spu_state.voices[voice];
      if (voice_state.active != 0U) {
        result.spu_active_voice_mask |=
            static_cast<std::uint32_t>(1U << voice);
      }
      result.spu_voice_block_flags[voice] = voice_state.block_flags;
      result.spu_voice_block_addresses[voice] = voice_state.block_address;
      result.spu_voice_repeat_addresses[voice] = voice_state.repeat_address;
    }
    result.active_spu_voices = audio.active_spu_voices;
    result.spu_control = audio.spu_control;
    result.spu_status = audio.spu_status;
    result.spu_cd_frames = audio.spu_cd_frames;
    result.cd_muted = audio.cd_muted;
    result.cd_adpcm_muted = audio.cd_adpcm_muted;
    result.cd_lba = audio.cd_lba;
    result.cd_reading = audio.cd_reading;
    const auto cd = vm_.machine().cdrom().captureState();
    result.cd_interrupt_flags = cd.interrupt_flags;
    result.cd_pending_command = cd.pending_command;
    result.cd_command_phase =
        static_cast<std::uint8_t>(cd.command_phase);
    result.cd_data_valid = cd.data_valid;
    result.cd_sector_event_pending = cd.sector_event.pending;
    result.xa_stream_set = audio.xa_stream_set;
    result.xa_file = audio.xa_file;
    result.xa_channel = audio.xa_channel;
    const auto &xa_admission =
        vm_.machine().xaSectorAdmissionDiagnostics();
    result.xa_sectors_received = xa_admission.received;
    result.xa_sectors_admitted = xa_admission.admitted;
    result.xa_sectors_rejected_busy = xa_admission.rejected_busy;
    result.xa_sectors_rejected_decode = xa_admission.rejected_decode;
    result.xa_sectors_muted = xa_admission.muted;
    result.xa_frames_admitted = xa_admission.admitted_frames;
    result.xa_maximum_queued_frames =
        xa_admission.maximum_queued_frames_before_admission;
    result.xa_has_received_sector = xa_admission.has_received_sector ? 1U : 0U;
    result.xa_first_received_lba = xa_admission.first_received_lba;
    result.xa_last_received_lba = xa_admission.last_received_lba;
    result.xa_first_received_file = xa_admission.first_received_file;
    result.xa_first_received_channel = xa_admission.first_received_channel;
    result.xa_last_received_file = xa_admission.last_received_file;
    result.xa_last_received_channel = xa_admission.last_received_channel;
    result.script_archive_loads = script_archive_loads_;
    result.script_program_count_at_start =
        script_program_count_at_start_;
    result.script_start_guest_frame = script_start_guest_frame_;
    result.script_active_programs_at_start_check =
        script_active_programs_at_start_check_;
    result.script_level_active_at_start_check =
        script_level_active_at_start_check_;
    static_cast<void>(vm_.runtime().read16(
        state.gpr[28U] + 0x0db0U, result.script_program_count));
    constexpr std::uint32_t program_table = 0x8013c030U;
    for (auto index = std::uint16_t{};
         index < result.script_program_count; ++index) {
      std::uint32_t candidate{};
      std::uint32_t name{};
      std::array<std::uint32_t, 2U> words{};
      if (!vm_.runtime().read32(
              program_table + static_cast<std::uint32_t>(index) * 4U,
              candidate) ||
          candidate == 0U ||
          !vm_.runtime().read32(candidate + 0x14U, name) ||
          name == 0U ||
          !vm_.runtime().read32(name, words[0U]) ||
          !vm_.runtime().read32(name + 4U, words[1U])) {
        continue;
      }
      if (words[0U] == 0x4556454cU &&
          (words[1U] & 0xffffU) == 0x004cU) {
        result.script_level_program = candidate;
        result.script_level_name_pointer = name;
        result.script_level_name_words = words;
        break;
      }
    }
    if (result.script_level_program != 0U) {
      for (auto index = std::size_t{};
           index < result.script_lookup_name_words.size(); ++index) {
        static_cast<void>(vm_.runtime().read32(
            state.gpr[28U] + 0x06e8U +
                static_cast<std::uint32_t>(index * 4U),
            result.script_lookup_name_words[index]));
      }
    }
    result.script_level_starts = script_level_starts_;
    result.airbasex_hard_difficulty_reads =
        airbasex_hard_difficulty_reads_;
    result.airbasex_hard_difficulty = airbasex_hard_difficulty_;
    result.script_dispatches = script_dispatches_;
    result.script_event5_dispatches = script_event5_dispatches_;
    result.last_script_dispatch_arguments =
        last_script_dispatch_arguments_;
    result.script_dispatch_event_count = script_dispatch_event_count_;
    result.script_dispatch_events = script_dispatch_events_;
    result.script_handler_event_count = script_handler_event_count_;
    result.script_handler_events = script_handler_events_;
    result.object_event_dispatch_count = object_event_dispatch_count_;
    result.object_event_dispatches = object_event_dispatches_;
    result.actor_activation_count = actor_activation_count_;
    result.actor_activations = actor_activations_;
    result.airbasex_motion_update_count = airbasex_motion_update_count_;
    result.airbasex_motion_updates = airbasex_motion_updates_;
    result.actor_collision_response_count = actor_collision_response_count_;
    result.actor_collision_responses = actor_collision_responses_;
    result.airbasex_actor_removal_count = airbasex_actor_removal_count_;
    result.airbasex_actor_removal_source = airbasex_actor_removal_source_;
    result.airbasex_actor_target_before = airbasex_actor_target_before_;
    result.airbasex_actor_target_word_before =
        airbasex_actor_target_word_before_;
    result.airbasex_actor_target_after = airbasex_actor_target_after_;
    result.airbasex_bounds_update_count = airbasex_bounds_update_count_;
    result.airbasex_bounds_update_caller = airbasex_bounds_update_caller_;
    result.airbasex_bounds_update_instance = airbasex_bounds_update_instance_;
    result.airbasex_bounds_minimum_y = airbasex_bounds_minimum_y_;
    result.airbasex_bounds_instance_words = airbasex_bounds_instance_words_;
    result.airbasex_bounds_physics_words = airbasex_bounds_physics_words_;
    result.auxiliary_packet_cursor_calls = auxiliary_packet_cursor_calls_;
    result.auxiliary_packet_cursor_minimum = auxiliary_packet_cursor_minimum_;
    result.auxiliary_packet_cursor_maximum = auxiliary_packet_cursor_maximum_;
    result.auxiliary_packet_output_maximum =
        auxiliary_packet_output_maximum_;
    result.airbasex_attachment_writer_pc = airbasex_attachment_writer_pc_;
    result.airbasex_attachment_writer_instruction =
        airbasex_attachment_writer_instruction_;
    result.airbasex_attachment_writer_value = airbasex_attachment_writer_value_;
    result.airbasex_attachment_write_count = airbasex_attachment_write_count_;
    result.airbasex_attachment_writer_code = airbasex_attachment_writer_code_;
    result.airbasex_attachment_init_calls = airbasex_attachment_init_calls_;
    result.airbasex_attachment_init_arguments =
        airbasex_attachment_init_arguments_;
    result.airbasex_attachment_link_calls = airbasex_attachment_link_calls_;
    result.airbasex_attachment_link_arguments =
        airbasex_attachment_link_arguments_;
    result.airbasex_attachment_existing_link =
        airbasex_attachment_existing_link_;
    result.airbasex_attachment_node = airbasex_attachment_node_;
    result.airbasex_attachment_node_flags = airbasex_attachment_node_flags_;
    result.airbasex_actor_collision_request_count =
        airbasex_actor_collision_request_count_;
    result.airbasex_actor_collision_request_caller =
        airbasex_actor_collision_request_caller_;
    result.airbasex_actor_collision_request_object =
        airbasex_actor_collision_request_object_;
    result.airbasex_actor_collision_request_room =
        airbasex_actor_collision_request_room_;
    static_cast<void>(vm_.runtime().read32(
        0x80166f58U, result.airbasex_activation_instruction));
    result.airbasex_source123_matrix_copies =
        airbasex_source123_matrix_copies_;
    result.airbasex_source123_matrix_copy_source =
        airbasex_source123_matrix_copy_source_;
    result.airbasex_source123_matrix_copy_caller =
        airbasex_source123_matrix_copy_caller_;
    result.airbasex_source123_matrix_copy_y =
        airbasex_source123_matrix_copy_y_;
    result.airbasex_source123_local_writer_caller =
        airbasex_source123_local_writer_caller_;
    result.script_program_dispatches = script_program_dispatches_;
    result.script_activations = script_activations_;
    result.last_script_activation_program =
        last_script_activation_program_;
    for (const auto program : active_script_programs_) {
      std::uint32_t header{};
      std::uint32_t timers{};
      std::uint32_t name{};
      std::array<std::uint32_t, 2U> name_words{};
      if (!vm_.runtime().read32(program, header) ||
          !vm_.runtime().read32(program + 0x10U, timers) ||
          !vm_.runtime().read32(program + 0x14U, name) ||
          timers == 0U || name == 0U ||
          !vm_.runtime().read32(name, name_words[0U]) ||
          !vm_.runtime().read32(name + 4U, name_words[1U])) {
        continue;
      }
      const auto timer_count =
          static_cast<std::uint8_t>(header >> 24U);
      for (auto index = std::uint16_t{}; index < timer_count; ++index) {
        std::uint16_t remaining_bits{};
        if (!vm_.runtime().read16(
                timers + static_cast<std::uint32_t>(index) * 2U,
                remaining_bits)) {
          break;
        }
        const auto remaining =
            std::bit_cast<std::int16_t>(remaining_bits);
        if (remaining <= 0 ||
            result.active_script_timer_count >=
                result.active_script_timers.size()) {
          continue;
        }
        result.active_script_timers[
            result.active_script_timer_count++] = Sf2GuestScriptTimer{
            .program = program,
            .timer_index = index,
            .remaining_ticks = remaining,
            .program_name_words = name_words,
        };
      }
    }
    result.mission_timer_handle = mission_timer_handle_;
    result.mission_timer_text = mission_timer_text_;
    result.mission_timer_text_updates = mission_timer_text_updates_;
    result.mission_timer_visible = mission_timer_handle_ != 0xffffU &&
                                   mission_timer_text_[0U] != '\0';
    result.scene_xa_archive_opens = scene_xa_archive_opens_;
    result.scene_speech_starts = scene_speech_starts_;
    result.scene_speech_callbacks = scene_speech_callbacks_;
    result.scene_speech_stops = scene_speech_stops_;
    result.scene_speech_stage = scene_speech_stage_;
    result.scene_speech_io_ready = scene_speech_io_ready_;
    result.last_scene_speech_arguments = last_scene_speech_arguments_;
    result.last_scene_speech_callback_arguments =
        last_scene_speech_callback_arguments_;
    result.last_scene_speech_stop_arguments =
        last_scene_speech_stop_arguments_;
    constexpr std::array scene_speech_io_addresses{
        0x8011f20cU, 0x8011f998U, 0x8011f9acU, 0x8011ee90U,
    };
    for (auto index = std::size_t{}; index < scene_speech_io_addresses.size();
         ++index) {
      static_cast<void>(vm_.runtime().read32(
          scene_speech_io_addresses[index],
          result.scene_speech_io_state[index]));
    }
    std::uint32_t async_record{};
    if (vm_.runtime().read32(0x8011f998U, async_record) &&
        async_record != 0U) {
      for (auto index = std::size_t{}; index < 8U; ++index) {
        static_cast<void>(vm_.runtime().read32(
            async_record + static_cast<std::uint32_t>(index * 4U),
            result.scene_speech_io_state[index + 4U]));
      }
      std::uint32_t request{};
      if (vm_.runtime().read32(async_record, request) && request != 0U) {
        for (auto index = std::size_t{}; index < 8U; ++index) {
          static_cast<void>(vm_.runtime().read32(
              request + static_cast<std::uint32_t>(index * 4U),
              result.scene_speech_io_state[index + 12U]));
        }
      }
    }
    result.spatial_sound_starts = spatial_sound_starts_;
    result.scene_sound_cue_plays = scene_sound_cue_plays_;
    constexpr std::array sound_service_global_offsets{
        0x07b8U, 0x07c0U, 0x07c4U, 0x0800U,
        0x0804U, 0x0808U, 0x080cU, 0x0810U,
    };
    for (auto index = std::size_t{};
         index < sound_service_global_offsets.size(); ++index) {
      static_cast<void>(vm_.runtime().read32(
          state.gpr[28U] + sound_service_global_offsets[index],
          result.sound_service_globals[index]));
    }
    static_cast<void>(vm_.runtime().read32(
        state.gpr[28U] + 0x07dcU, result.sound_sequence_pointer));
    if (result.sound_sequence_pointer != 0U) {
      static_cast<void>(vm_.runtime().read8(
          result.sound_sequence_pointer + 0x06U,
          result.sound_sequence_flags));
      static_cast<void>(vm_.runtime().read32(
          result.sound_sequence_pointer + 0x1cU,
          result.sound_sequence_cursor));
      static_cast<void>(vm_.runtime().read32(
          result.sound_sequence_pointer + 0x24U,
          result.sound_sequence_countdown));
      static_cast<void>(vm_.runtime().read32(
          result.sound_sequence_pointer + 0x30U,
          result.sound_sequence_step));
      static_cast<void>(vm_.runtime().read16(
          result.sound_sequence_pointer + 0x38U,
          result.sound_sequence_tempo));
      static_cast<void>(vm_.runtime().read16(
          result.sound_sequence_pointer + 0x3aU,
          result.sound_sequence_loop_count));
    }
    for (auto index = std::size_t{};
         index < result.interrupt_callbacks.size(); ++index) {
      static_cast<void>(vm_.runtime().read32(
          profile_.interrupt_callback_table +
              static_cast<std::uint32_t>(index * sizeof(std::uint32_t)),
          result.interrupt_callbacks[index]));
    }
    constexpr std::array xa_global_offsets{
        0x073cU, 0x0764U, 0x0c94U, 0x0c9cU, 0x0cb4U,
    };
    for (auto index = std::size_t{}; index < xa_global_offsets.size();
         ++index) {
      static_cast<void>(vm_.runtime().read32(
          state.gpr[28U] + xa_global_offsets[index],
          result.xa_globals[index]));
    }
    result.xa_status_source = xa_status_source_;
    result.xa_status_result = xa_status_result_;
    result.xa_cue_plays = xa_cue_plays_;
    result.xa_stream_starts = xa_stream_starts_;
    result.xa_stream_stops = xa_stream_stops_;
    result.xa_absolute_disc_active = xa_absolute_disc_active_;
    result.timeline_event_count = timeline_event_count_;
    result.timeline_events = timeline_events_;
    result.ui_text_event_count = ui_text_event_count_;
    result.ui_text_events = ui_text_events_;
    result.async_file_services = async_file_services_;
    result.async_file_completions = async_file_completions_;
    result.last_async_completion_caller = last_async_completion_caller_;
    result.device_wait_scheduler_slices = device_wait_scheduler_slices_;
    result.input_samples = host_pad_samples_;
    result.checkpoint_restores = alpha_checkpoint_restores_;
    result.checkpoint_audio_discarded_frames =
        checkpoint_audio_discarded_frames_;
    result.mission_success_events = mission_success_events_;
    result.mission_failure_events = mission_failure_events_;
    result.mission_complete_requested = mission_complete_requested_;
    result.campaign_advance_calls = campaign_advance_calls_;
    result.movie_request_calls = movie_request_calls_;
    result.movie_playback_init_calls = movie_playback_init_calls_;
    result.scripted_movie_handoffs = scripted_movie_handoffs_;
    result.last_scripted_movie_catalog_index =
        last_scripted_movie_catalog_index_;
    result.selected_movie_catalog_index = selected_movie_catalog_index_;
    result.last_movie_request_arguments = last_movie_request_arguments_;
    result.last_movie_playback_arguments =
        last_movie_playback_arguments_;
    result.movie_selection_writes = movie_selection_writes_;
    result.last_movie_selection_writer_pc =
        last_movie_selection_writer_pc_;
    result.last_movie_selection_writer_instruction =
        last_movie_selection_writer_instruction_;
    result.last_movie_selection_write_value =
        last_movie_selection_write_value_;
    result.movie_selection_writer_pcs = movie_selection_writer_pcs_;
    result.movie_selection_write_values = movie_selection_write_values_;
    result.movie_playback_catalog_history =
        movie_playback_catalog_history_;
    static_cast<void>(
        vm_.runtime().read32(0x80156bd8U, result.title_transition_mode));
    static_cast<void>(
        vm_.runtime().read32(0x80156bdcU, result.title_substate));
    static_cast<void>(
        vm_.runtime().read8(0x8011f608U, result.mounted_campaign_disc));
    static_cast<void>(
        vm_.runtime().read32(profile_.application_state,
                             result.application_state));
    std::uint16_t selected_mission_index{};
    if (vm_.runtime().read16(0x8012b02cU, selected_mission_index)) {
      result.selected_mission_index = selected_mission_index;
    }
    static_cast<void>(
        vm_.runtime().read32(profile_.system_clock, result.system_clock));
    return result;
  }

  [[nodiscard]] bool advanceHostUpdate() noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    // Decoder init yielded with its retail caller still live. Do not allow a
    // caller that missed the handoff to run MOVIE.OVL ahead of native STR
    // playback; completeScriptedMovie() is the only operation that releases
    // this boundary.
    if (scripted_movie_host_yielded_ || mission_restart_requested_ ||
        quit_to_title_requested_) {
      return true;
    }
    observeMissionOutcomeState();
    if (mission_complete_requested_) {
      return true;
    }
    // Preserve the 60 Hz guest/input contract: one public update retires one
    // retail display-list submission. Presentation publication is separate;
    // gameplay can queue world and UI lists during one 20 Hz logic tick, and
    // advanceGuestBoundary() publishes their completed composition while
    // intermediate updates continue to display the previous completed frame.
    constexpr auto maximum_boundaries_per_frame = 128U;
    for (auto boundary = 0U; boundary < maximum_boundaries_per_frame;
         ++boundary) {
      auto display_submitted = false;
      if (!advanceGuestBoundary(display_submitted)) {
        return false;
      }
      if (scripted_movie_host_yielded_) {
        return true;
      }
      if (mission_restart_requested_ || quit_to_title_requested_) {
        return true;
      }
      observeMissionOutcomeState();
      if (mission_complete_requested_) {
        return true;
      }
      if (display_submitted && presentation_frame_) {
        return true;
      }
    }
    markFault("SF2 did not submit a display frame within the boundary limit");
    return false;
  }

  [[nodiscard]] bool advanceRetailBriefingAudioSlice(
      bool dispatch_sound_callback) noexcept {
    if (!ready_ || faulted_ ||
        start_mode_ != Sf2GuestRuntimeStartMode::retail_briefing) {
      return false;
    }
    auto profile = syphonFilterUsaV11RetailAudioProfile();
    // SF2 installs its own timer-6 callback during bootstrap. Accept that
    // registered target while retaining the shared 120 Hz device clock.
    profile.expected_tick_callback = 0U;
    const auto sound_callback_slot =
        profile_.interrupt_callback_table + 6U * 4U;
    if (!vm_.advanceAudioSliceClock(profile) ||
        (dispatch_sound_callback &&
         !vm_.servicePsxCallbackSlot(sound_callback_slot,
                                     callback_stack_))) {
      markFault("SF2 retail briefing audio slice failed");
      return false;
    }
    return true;
  }

  [[nodiscard]] bool missionCompleteRequested() const noexcept {
    return mission_complete_requested_;
  }

  [[nodiscard]] bool missionRestartRequested() const noexcept {
    return mission_restart_requested_;
  }
  [[nodiscard]] bool quitToTitleRequested() const noexcept {
    return quit_to_title_requested_;
  }

private:
  void observeMissionOutcomeState() noexcept {
    if (!mission_success_pending_ || mission_complete_requested_) {
      return;
    }
    std::uint32_t application_state{};
    if (!vm_.runtime().read32(profile_.application_state,
                              application_state)) {
      return;
    }
    // Accepted MissionSuccess pushes state 11. Its overlay lifecycle can
    // advance through loading state 9 into movie state 4 without publishing
    // an intervening display list, so state 4 is the bounded fallback. The
    // success-entry latch keeps unrelated scripted movies out of this path.
    if (application_state == 11U || application_state == 4U) {
      mission_complete_requested_ = true;
      mission_success_pending_ = false;
    }
  }

  void normalizeInitialAuxiliaryRendererState() {
    // The direct TITLE-to-mission handoff leaves TITLE's full-screen map-grid
    // list attached to the resident auxiliary renderer. Retail's checkpoint
    // restart clears this list head while retaining the renderer and its HUD,
    // text and radar lists. Mirror that initialization state once, before the
    // first gameplay frame, but only after verifying the complete observed
    // 113-node stale list. Mission allocation moves the dense node window,
    // while its primitive pool and structure are invariant across all 21
    // packages. A real map opened later installs a fresh list and is
    // unaffected.
    constexpr std::uint32_t auxiliary_general_list = 0x80120b78U;
    constexpr std::uint32_t primitive_begin = 0x80168ae8U;
    constexpr std::uint32_t primitive_end = 0x80169790U;
    constexpr std::size_t expected_nodes = 113U;
    constexpr std::uint32_t node_size = 12U;
    constexpr auto node_window_bytes =
        static_cast<std::uint32_t>((expected_nodes - 1U) * node_size);

    std::uint32_t cursor{};
    if (!vm_.runtime().read32(auxiliary_general_list, cursor) ||
        cursor < node_window_bytes) {
      return;
    }
    const auto node_begin = cursor - node_window_bytes;
    const auto node_end = cursor + node_size;
    std::array<bool, expected_nodes> visited_nodes{};
    for (auto node = std::size_t{}; node < expected_nodes; ++node) {
      std::uint32_t primitive{};
      std::uint32_t next{};
      if (cursor < node_begin || cursor >= node_end ||
          ((cursor - node_begin) % node_size) != 0U ||
          !vm_.runtime().read32(cursor, primitive) ||
          !vm_.runtime().read32(cursor + 8U, next) ||
          primitive < primitive_begin || primitive >= primitive_end ||
          (primitive & 3U) != 0U ||
          (node + 1U == expected_nodes ? next != 0U : next == 0U)) {
        return;
      }
      const auto node_index =
          static_cast<std::size_t>((cursor - node_begin) / node_size);
      if (visited_nodes[node_index]) {
        return;
      }
      visited_nodes[node_index] = true;
      cursor = next;
    }
    static_cast<void>(vm_.runtime().write32(auxiliary_general_list, 0U));
  }

  struct UiInstructionTraceRecord {
    std::uint64_t ordinal{};
    std::uint64_t guest_frame{};
    std::uint32_t system_clock{};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    std::uint16_t draw_buffer{};
    std::uint16_t display_buffer{};
    std::uint16_t build_buffer{};
    std::uint16_t reserved{};
    std::uint32_t packet_cursor{};
    std::array<std::uint32_t, 32U> gpr{};
    std::array<std::uint32_t, 8U> a0_words{};
    std::array<std::uint32_t, 8U> a1_words{};
    std::array<std::uint32_t, 8U> s3_words{};
  };

  void configureUiInstructionTrace() {
    std::string path;
#if defined(_WIN32)
    char *environment_value{};
    std::size_t environment_size{};
    if (_dupenv_s(&environment_value, &environment_size,
                  "SF2_UI_INSTRUCTION_TRACE") == 0 &&
        environment_value != nullptr) {
      path.assign(environment_value);
      std::free(environment_value);
    }
#else
    if (const auto *environment_value =
            std::getenv("SF2_UI_INSTRUCTION_TRACE")) {
      path.assign(environment_value);
    }
#endif
    if (path.empty()) {
      return;
    }
    const auto environment_unsigned = [](const char *name,
                                         std::uint64_t fallback) {
      std::string value;
#if defined(_WIN32)
      char *environment_value{};
      std::size_t environment_size{};
      if (_dupenv_s(&environment_value, &environment_size, name) == 0 &&
          environment_value != nullptr) {
        value.assign(environment_value);
        std::free(environment_value);
      }
#else
      if (const auto *environment_value = std::getenv(name)) {
        value.assign(environment_value);
      }
#endif
      if (value.empty()) {
        return fallback;
      }
      char *end{};
      const auto parsed = std::strtoull(value.c_str(), &end, 10);
      return end != value.c_str() && *end == '\0' ? parsed : fallback;
    };
    ui_instruction_trace_control_flow_only_ =
        environment_unsigned("SF2_UI_TRACE_CONTROL_FLOW_ONLY", 0U) != 0U;
    ui_instruction_trace_begin_clock_ = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(environment_unsigned(
                                    "SF2_UI_TRACE_BEGIN_CLOCK", 0U),
                                std::numeric_limits<std::uint32_t>::max()));
    ui_instruction_trace_end_clock_ = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(environment_unsigned(
                                    "SF2_UI_TRACE_END_CLOCK",
                                    std::numeric_limits<std::uint32_t>::max()),
                                std::numeric_limits<std::uint32_t>::max()));
    constexpr auto default_maximum_trace_bytes =
        std::uint64_t{8U} * 1024U * 1024U * 1024U;
    constexpr auto absolute_maximum_trace_bytes =
        std::uint64_t{32U} * 1024U * 1024U * 1024U;
    ui_instruction_trace_max_bytes_ = std::clamp<std::uint64_t>(
        environment_unsigned("SF2_UI_TRACE_MAX_BYTES",
                             default_maximum_trace_bytes),
        sizeof(UiInstructionTraceRecord) + 12U,
        absolute_maximum_trace_bytes);
    ui_instruction_trace_.open(path,
                               std::ios::binary | std::ios::trunc);
    if (!ui_instruction_trace_) {
      markFault("could not open SF2 UI instruction trace");
      return;
    }
    constexpr std::array<char, 8U> magic{'S', 'F', '2', 'U', 'I', 'T', 'R', '1'};
    const auto record_size =
        static_cast<std::uint32_t>(sizeof(UiInstructionTraceRecord));
    ui_instruction_trace_.write(magic.data(),
                                static_cast<std::streamsize>(magic.size()));
    ui_instruction_trace_.write(
        reinterpret_cast<const char *>(&record_size), sizeof(record_size));
    ui_instruction_trace_buffer_.reserve(4096U);
    vm_.runtime().setExecutionObserver(
        [this](const psx::R3000State &state, std::uint32_t pc,
               std::uint32_t instruction) {
          recordUiInstructionTrace(state, pc, instruction);
        });
    vm_.setHostCallObserver(
        [this](std::uint32_t address, const psx::R3000State &state) {
          if (address == 0x800e54ecU || address == 0x800e6e74U ||
              address == profile_.gpu_submission_entry) {
            recordUiInstructionTrace(state, address, 0xffffffffU);
          }
        });
  }

  void recordUiInstructionTrace(const psx::R3000State &state,
                                std::uint32_t pc,
                                std::uint32_t instruction) {
    const auto opcode = instruction >> 26U;
    const auto function = instruction & 0x3fU;
    const auto is_control_flow =
        opcode == 0x02U || opcode == 0x03U ||
        (opcode >= 0x04U && opcode <= 0x07U) || opcode == 0x01U ||
        (opcode == 0U &&
         (function == 0x08U || function == 0x09U || function == 0x0cU ||
          function == 0x0dU));
    const auto writes_traced_ordering_table = [&] {
      if (opcode != 0x28U && opcode != 0x29U && opcode != 0x2aU &&
          opcode != 0x2bU && opcode != 0x2eU) {
        return false;
      }
      const auto base_register = (instruction >> 21U) & 0x1fU;
      const auto displacement = static_cast<std::int32_t>(
          static_cast<std::int16_t>(instruction & 0xffffU));
      const auto address = state.gpr[base_register] + displacement;
      return address >= 0x801f8c60U && address < 0x801f8cb0U;
    }();
    const auto in_menu_overlay = pc >= 0x80142150U && pc < 0x8014b6b0U;
    const auto in_menu_outcome_region =
        (pc >= 0x801435c0U && pc < 0x80143680U) ||
        (pc >= 0x80145000U && pc < 0x80145a00U);
    const auto menu_overlay_relevant =
        in_menu_overlay &&
        (!ui_instruction_trace_control_flow_only_ ||
         (guest_frame_ != 0U && (is_control_flow || in_menu_outcome_region)));
    const auto raw_relevant = instruction == 0xffffffffU ||
        writes_traced_ordering_table ||
        (pc >= 0x80012000U && pc < 0x8001f000U) ||
        (pc >= 0x800a5000U && pc < 0x800a9000U) ||
        // MENU.OVL owns pause confirmation dispatch, including the indirect
        // Save-and-Quit callback path.  Keep this whole small overlay range so
        // a trace can distinguish a missed host boundary from a callback that
        // enters and then blocks in its retail teardown.
        menu_overlay_relevant ||
        (pc >= 0x80150000U && pc < 0x80170000U);
    const auto relevant = ui_instruction_trace_control_flow_only_
        ? guest_frame_ != 0U && in_menu_overlay &&
              (is_control_flow || in_menu_outcome_region)
        : raw_relevant;
    if (!relevant || !ui_instruction_trace_) {
      return;
    }
    std::uint32_t system_clock{};
    static_cast<void>(
        vm_.runtime().read32(profile_.system_clock, system_clock));
    if (system_clock < ui_instruction_trace_begin_clock_ ||
        system_clock > ui_instruction_trace_end_clock_ ||
        ui_instruction_trace_capped_) {
      return;
    }
    const auto next_size = 12U +
        (ui_instruction_trace_events_ + 1U) *
            sizeof(UiInstructionTraceRecord);
    if (next_size > ui_instruction_trace_max_bytes_) {
      ui_instruction_trace_capped_ = true;
      flushUiInstructionTrace();
      return;
    }
    UiInstructionTraceRecord record{};
    record.ordinal = ++ui_instruction_trace_events_;
    record.guest_frame = guest_frame_;
    record.pc = pc;
    record.instruction = instruction;
    record.gpr = state.gpr;
    record.system_clock = system_clock;
    static_cast<void>(
        vm_.runtime().read16(state.gpr[28U] + 0x26U, record.draw_buffer));
    static_cast<void>(vm_.runtime().read16(state.gpr[28U] + 0x28U,
                                           record.display_buffer));
    static_cast<void>(
        vm_.runtime().read16(state.gpr[28U] + 0x2aU, record.build_buffer));
    static_cast<void>(vm_.runtime().read32(0x8013e6dcU,
                                           record.packet_cursor));
    const auto snapshot = [this](std::uint32_t address, auto &words) {
      for (auto index = std::size_t{}; index < words.size(); ++index) {
        static_cast<void>(vm_.runtime().read32(
            address + static_cast<std::uint32_t>(index * 4U),
            words[index]));
      }
    };
    snapshot(state.gpr[4U], record.a0_words);
    snapshot(state.gpr[5U], record.a1_words);
    snapshot(state.gpr[19U], record.s3_words);
    ui_instruction_trace_buffer_.push_back(record);
    if (ui_instruction_trace_buffer_.size() >= 4096U) {
      flushUiInstructionTrace();
    }
  }

  void flushUiInstructionTrace() {
    if (!ui_instruction_trace_ || ui_instruction_trace_buffer_.empty()) {
      return;
    }
    ui_instruction_trace_.write(
        reinterpret_cast<const char *>(ui_instruction_trace_buffer_.data()),
        static_cast<std::streamsize>(ui_instruction_trace_buffer_.size() *
                                     sizeof(UiInstructionTraceRecord)));
    ui_instruction_trace_buffer_.clear();
    ui_instruction_trace_.flush();
  }

  void readPlayerState(std::uint32_t &instance, std::int32_t &x,
                       std::int32_t &y, std::int32_t &z,
                       std::uint16_t &health,
                       std::uint16_t &armor) const noexcept {
    constexpr std::uint32_t player_pointer = 0x8012a574U;
    constexpr std::uint32_t instance_node_offset = 0x08U;
    constexpr std::uint32_t instance_health_offset = 0x18U;
    constexpr std::uint32_t node_matrix_offset = 0x0cU;
    constexpr std::uint32_t matrix_translation_offset = 0x14U;
    constexpr std::uint32_t health_armor_offset = 0x06U;
    constexpr std::uint32_t health_value_offset = 0x08U;
    std::uint32_t node{};
    std::uint32_t matrix{};
    std::uint32_t health_controller{};
    std::uint32_t raw_x{};
    std::uint32_t raw_y{};
    std::uint32_t raw_z{};
    std::uint16_t health_bits{};
    std::uint16_t armor_bits{};
    if (!vm_.runtime().read32(player_pointer, instance) || instance == 0U ||
        !vm_.runtime().read32(instance + instance_node_offset, node) ||
        node == 0U ||
        !vm_.runtime().read32(node + node_matrix_offset, matrix) ||
        matrix == 0U ||
        !vm_.runtime().read32(instance + instance_health_offset,
                              health_controller) ||
        health_controller == 0U ||
        !vm_.runtime().read32(matrix + matrix_translation_offset, raw_x) ||
        !vm_.runtime().read32(matrix + matrix_translation_offset + 4U,
                              raw_y) ||
        !vm_.runtime().read32(matrix + matrix_translation_offset + 8U,
                              raw_z) ||
        !vm_.runtime().read16(health_controller + health_value_offset,
                              health_bits) ||
        !vm_.runtime().read16(health_controller + health_armor_offset,
                              armor_bits)) {
      instance = 0U;
      x = 0;
      y = 0;
      z = 0;
      health = 0U;
      armor = 0U;
      return;
    }
    x = std::bit_cast<std::int32_t>(raw_x);
    y = std::bit_cast<std::int32_t>(raw_y);
    z = std::bit_cast<std::int32_t>(raw_z);
    const auto signed_health = std::bit_cast<std::int16_t>(health_bits);
    const auto signed_armor = std::bit_cast<std::int16_t>(armor_bits);
    health = signed_health > 0 ? static_cast<std::uint16_t>(signed_health) : 0U;
    armor = signed_armor > 0 ? static_cast<std::uint16_t>(signed_armor) : 0U;
  }

  [[nodiscard]] bool readPlayerCameraOwnership(
      std::uint32_t player, std::uint32_t &camera_wrapper,
      std::uint32_t &camera_owner) const noexcept {
    constexpr std::uint32_t instance_player_state_offset = 0x20U;
    constexpr std::uint32_t player_state_camera_offset = 0xe0U;
    constexpr std::uint32_t camera_wrapper_owner_offset = 0xdcU;
    std::uint32_t player_state{};
    camera_wrapper = 0U;
    camera_owner = 0U;
    return player != 0U &&
           vm_.runtime().read32(player + instance_player_state_offset,
                                player_state) &&
           player_state != 0U &&
           vm_.runtime().read32(player_state + player_state_camera_offset,
                                camera_wrapper) &&
           camera_wrapper != 0U &&
           vm_.runtime().read32(camera_wrapper + camera_wrapper_owner_offset,
                                camera_owner);
  }

  void readPlayerHudState(
      std::uint32_t instance,
      Sf2GuestRuntimeDiagnostics &result) const noexcept {
    constexpr std::uint32_t instance_target_offset = 0x14U;
    constexpr std::uint32_t instance_inventory_offset = 0x20U;
    constexpr std::uint32_t target_slot_offset = 0U;
    constexpr std::uint32_t target_flags_offset = 4U;
    constexpr std::uint32_t target_meter_offset = 0x58U;
    constexpr std::uint32_t inventory_owned_offset = 0x3cU;
    constexpr std::uint32_t inventory_ammo_offset = 0x44U;
    constexpr std::uint32_t inventory_equipped_offset = 0xccU;
    if (instance == 0U) {
      return;
    }
    std::uint32_t target{};
    if (vm_.runtime().read32(instance + instance_target_offset, target) &&
        target != 0U) {
      std::uint16_t target_slot_bits{};
      std::uint16_t target_meter_bits{};
      if (vm_.runtime().read16(target + target_slot_offset,
                               target_slot_bits) &&
          vm_.runtime().read32(target + target_flags_offset,
                               result.player_target_flags) &&
          vm_.runtime().read16(target + target_meter_offset,
                               target_meter_bits)) {
        result.player_target_slot =
            std::bit_cast<std::int16_t>(target_slot_bits);
        result.player_target_meter =
            std::bit_cast<std::int16_t>(target_meter_bits);
      }
    }
    std::uint32_t inventory{};
    if (!vm_.runtime().read32(instance + instance_inventory_offset,
                              inventory) ||
        inventory == 0U) {
      return;
    }
    static_cast<void>(vm_.runtime().read32(
        inventory + inventory_equipped_offset,
        result.player_equipped_item));
    for (auto index = std::size_t{};
         index < result.player_owned_items.size(); ++index) {
      static_cast<void>(vm_.runtime().read32(
          inventory + inventory_owned_offset +
              static_cast<std::uint32_t>(index * 4U),
          result.player_owned_items[index]));
    }
    for (auto item = std::size_t{}; item < sf2_inventory_item_count; ++item) {
      const auto address =
          inventory + inventory_ammo_offset +
          static_cast<std::uint32_t>(item * 4U);
      static_cast<void>(
          vm_.runtime().read16(address, result.player_reserves[item]));
      static_cast<void>(
          vm_.runtime().read16(address + 2U, result.player_magazines[item]));
    }
  }

  [[nodiscard]] bool readPlayerThreatState(
      std::uint32_t player_instance,
      Sf2GuestRuntimeDiagnostics &result) const noexcept {
    constexpr std::uint32_t object_records_pointer = 0x8011eef8U;
    constexpr std::uint32_t object_count_address = 0x8011f564U;
    constexpr std::uint32_t object_record_stride = 0x4cU;
    constexpr std::uint32_t object_instance_offset = 0x34U;
    constexpr std::uint32_t object_class_offset = 0x2aU;
    constexpr std::uint32_t object_flags_offset = 0x27U;
    constexpr std::uint32_t object_maximum_health_offset = 0x3eU;
    constexpr std::uint32_t object_health_offset = 0x40U;
    constexpr std::uint32_t instance_node_offset = 0x08U;
    constexpr std::uint32_t instance_target_offset = 0x14U;
    constexpr std::uint32_t target_slot_offset = 0U;
    // SF2 expands the target controller relative to SF1. Dynamic scans of
    // living actors targeting Gabe place the retail Q12 danger scalar at
    // +0xE8 (0x1000 at full threat); SF1's +0xD4 field remains zero here.
    constexpr std::uint32_t target_danger_offset = 0xe8U;
    constexpr std::uint32_t maximum_objects = 1024U;
    constexpr std::uint32_t fixed_one = 0x1000U;
    constexpr std::uint32_t retail_bar_maximum = 50U;

    std::uint32_t records{};
    std::uint32_t count_bits{};
    if (player_instance == 0U ||
        !vm_.runtime().read32(object_records_pointer, records) ||
        !vm_.runtime().read32(object_count_address, count_bits) ||
        records == 0U) {
      return false;
    }
    const auto count = std::bit_cast<std::int32_t>(count_bits);
    if (count <= 0 ||
        static_cast<std::uint32_t>(count) > maximum_objects) {
      return false;
    }
    result.object_record_count = static_cast<std::uint32_t>(count);

    result.player_object_slot = -1;
    for (auto slot = 0; slot < count; ++slot) {
      std::uint32_t instance{};
      if (!vm_.runtime().read32(
              records + static_cast<std::uint32_t>(slot) *
                            object_record_stride +
                  object_instance_offset,
              instance)) {
        return false;
      }
      if (instance == player_instance) {
        result.player_object_slot = static_cast<std::int16_t>(slot);
        break;
      }
    }
    if (result.player_object_slot < 0) {
      return false;
    }

    // PSYQ MATRIX stores its 3x3 Q12 rotation at +0 and translation at
    // +0x14. Local +Z is the actor's forward axis in the sequel instance
    // nodes, so m02/m22 provide a stable radar basis without deriving facing
    // from movement (which fails while Gabe is standing still).
    std::uint32_t player_node{};
    std::uint32_t player_matrix{};
    std::uint16_t forward_x_bits{};
    std::uint16_t forward_z_bits{};
    if (vm_.runtime().read32(
            player_instance + instance_node_offset, player_node) &&
        player_node != 0U &&
        vm_.runtime().read32(player_node + 0x0cU, player_matrix) &&
        player_matrix != 0U &&
        vm_.runtime().read16(player_matrix + 0x04U, forward_x_bits) &&
        vm_.runtime().read16(player_matrix + 0x10U, forward_z_bits)) {
      result.player_forward_x =
          std::bit_cast<std::int16_t>(forward_x_bits);
      result.player_forward_z =
          std::bit_cast<std::int16_t>(forward_z_bits);
    }

    result.radar_actor_count = 0U;
    auto radar_distance_squared =
        std::array<std::uint64_t, 16U>{};
    radar_distance_squared.fill(
        std::numeric_limits<std::uint64_t>::max());
    auto safe_q12 = fixed_one;
    result.player_threat_count = 0U;
    for (auto slot = 0; slot < count; ++slot) {
      const auto record =
          records + static_cast<std::uint32_t>(slot) * object_record_stride;
      std::uint32_t instance{};
      std::uint16_t health_bits{};
      std::uint8_t object_class{};
      std::uint8_t object_flags{};
      if (!vm_.runtime().read32(record + object_instance_offset, instance) ||
          !vm_.runtime().read16(record + object_health_offset, health_bits) ||
          !vm_.runtime().read8(record + object_class_offset, object_class) ||
          !vm_.runtime().read8(record + object_flags_offset, object_flags)) {
        return false;
      }
      const auto actor_class = object_class == 0x01U ||
                               object_class == 0x02U ||
                               object_class == 0x03U;
      if (actor_class) {
        ++result.actor_record_count;
        if (instance != 0U) {
          ++result.actor_instance_count;
        }
        if ((object_flags & 0x02U) != 0U) {
          ++result.actor_dormant_count;
        }
      }
      if (instance == 0U ||
          std::bit_cast<std::int16_t>(health_bits) <= 0) {
        continue;
      }
      if (slot == result.player_target_slot) {
        std::uint16_t maximum_health_bits{};
        if (!vm_.runtime().read16(
                record + object_maximum_health_offset,
                maximum_health_bits)) {
          return false;
        }
        const auto health =
            static_cast<std::uint32_t>(
                std::bit_cast<std::int16_t>(health_bits));
        const auto signed_maximum =
            std::bit_cast<std::int16_t>(maximum_health_bits);
        const auto maximum = std::max<std::uint32_t>(
            health, signed_maximum > 0
                        ? static_cast<std::uint32_t>(signed_maximum)
                        : 1U);
        result.player_target_health_percent =
            static_cast<std::uint8_t>(std::min<std::uint32_t>(
                100U, (health * 100U + maximum - 1U) / maximum));
        result.player_target_active = true;
      }
      std::uint32_t node{};
      std::uint32_t target{};
      if (!vm_.runtime().read32(instance + instance_node_offset, node) ||
          !vm_.runtime().read32(instance + instance_target_offset, target)) {
        return false;
      }
      if (actor_class && target != 0U) {
        ++result.actor_target_controller_count;
      }
      if (node == 0U || target == 0U) {
        continue;
      }

      std::uint16_t target_slot_bits{};
      std::uint32_t danger_bits{};
      if (!vm_.runtime().read16(target + target_slot_offset,
                                target_slot_bits) ||
          !vm_.runtime().read32(target + target_danger_offset,
                                danger_bits)) {
        return false;
      }
      const auto threat_q12 = static_cast<std::uint16_t>(
          std::min(danger_bits & 0xffff3fffU, fixed_one));
      if (std::bit_cast<std::int16_t>(target_slot_bits) ==
          result.player_object_slot) {
        ++result.player_threat_count;
        safe_q12 = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(safe_q12) *
             (fixed_one - threat_q12)) >>
            12U);
      }

      // The retail object dispatcher reads the faction/class byte from
      // record +0x2A. SF2 class 01 and AIRBASE's class 03 are opposition;
      // class 02 is allied. Keep both factions (retail plots Chance in blue)
      // while excluding unrelated target-capable object classes.
      const auto radar_hostile = object_class == 0x01U ||
                                 object_class == 0x03U;
      const auto radar_allied = object_class == 0x02U;
      std::uint32_t matrix{};
      std::uint32_t actor_x_bits{};
      std::uint32_t actor_z_bits{};
      if ((radar_hostile || radar_allied) &&
          vm_.runtime().read32(node + 0x0cU, matrix) &&
          matrix != 0U &&
          vm_.runtime().read32(matrix + 0x14U, actor_x_bits) &&
          vm_.runtime().read32(matrix + 0x1cU, actor_z_bits)) {
        const auto actor_x = std::bit_cast<std::int32_t>(actor_x_bits);
        const auto actor_z = std::bit_cast<std::int32_t>(actor_z_bits);
        const auto delta_x =
            static_cast<std::int64_t>(actor_x) - result.player_x;
        const auto delta_z =
            static_cast<std::int64_t>(actor_z) - result.player_z;
        const auto square = [](std::int64_t value) {
          const auto magnitude = static_cast<std::uint64_t>(
              value < 0 ? -value : value);
          return magnitude * magnitude;
        };
        const auto distance_squared =
            square(delta_x) + square(delta_z);
        auto insertion = radar_distance_squared.size();
        for (auto index = std::size_t{};
             index < radar_distance_squared.size(); ++index) {
          if (distance_squared < radar_distance_squared[index]) {
            insertion = index;
            break;
          }
        }
        if (insertion < radar_distance_squared.size()) {
          for (auto index = radar_distance_squared.size() - 1U;
               index > insertion; --index) {
            radar_distance_squared[index] =
                radar_distance_squared[index - 1U];
            result.radar_actors[index] =
                result.radar_actors[index - 1U];
          }
          radar_distance_squared[insertion] = distance_squared;
          result.radar_actors[insertion] = Sf2GuestRadarActor{
              .x = actor_x,
              .z = actor_z,
              .object_slot = static_cast<std::int16_t>(slot),
              .threat_q12 = threat_q12,
              .allied = radar_allied,
              .selected = result.player_target_slot == slot,
          };
          result.radar_actor_count = static_cast<std::uint8_t>(
              std::min<std::size_t>(
                  result.radar_actor_count + 1U,
                  result.radar_actors.size()));
        }
      }

    }
    const auto endpoint =
        retail_bar_maximum - ((safe_q12 * retail_bar_maximum) >> 12U);
    result.player_danger =
        static_cast<std::uint8_t>(endpoint * 2U);
    return true;
  }


  [[nodiscard]] static std::size_t
  authoredWorldPolygonCount(const Sf2PresentationFrame &frame) noexcept {
    // The retail mission loader submits a sizeable card-and-line list before
    // the first authored scene.  It has hundreds of draw commands, so a raw
    // draw-count threshold mistakes it for gameplay.  Mission geometry uses
    // the textured gouraud polygon families (0x34/0x36/0x3c/0x3e); require a
    // meaningful population of those before handing a frame to the product.
    return static_cast<std::size_t>(std::ranges::count_if(
        frame.packets, [](const Sf2GpuPacket &packet) {
          if (packet.gp0_words.empty()) {
            return false;
          }
          const auto opcode = packet.gp0_words.front() >> 24U;
          return (opcode & 0xfcU) == 0x34U ||
                 (opcode & 0xfcU) == 0x3cU;
        }));
  }

  [[nodiscard]] static bool
  isAuthoredWorldFrame(const Sf2PresentationFrame &frame) noexcept {
    return authoredWorldPolygonCount(frame) >= 100U;
  }

  [[nodiscard]] static bool
  isAuthoredPresentationBase(const Sf2PresentationFrame &frame) noexcept {
    // MENU.OVL (application state 7) owns a complete retail screen but does
    // not contain a 3D world OT. Its first per-page submission is the small
    // base/menu-status list, followed by the larger text/map list in the same
    // clock. Treat that first visible list as a composition base; retaining
    // the gameplay-only polygon threshold here freezes presentation on the
    // preceding loading frame and leaves the host target black.
    return isAuthoredWorldFrame(frame) ||
           (frame.application_state == 7U &&
            frame.draw_command_count != 0U);
  }

  [[nodiscard]] std::vector<std::uint32_t> captureGpuSideEffects() {
    auto words = vm_.machine().takeGpuGp0Words();
    auto gp1_words = vm_.machine().takeGpuGp1Words();
    gpu_gp0_stream_.insert(gpu_gp0_stream_.end(), words.begin(), words.end());
    const auto compact_consumed_words = [this]() {
      if (gpu_gp0_scan_ == 0U) {
        return;
      }
      if (gpu_gp0_scan_ == gpu_gp0_stream_.size()) {
        gpu_gp0_stream_.clear();
        gpu_gp0_scan_ = 0U;
        return;
      }
      if (gpu_gp0_scan_ >= 4096U) {
        gpu_gp0_stream_.erase(
            gpu_gp0_stream_.begin(),
            gpu_gp0_stream_.begin() +
                static_cast<std::ptrdiff_t>(gpu_gp0_scan_));
        gpu_gp0_scan_ = 0U;
      }
    };
    while (gpu_gp0_scan_ < gpu_gp0_stream_.size()) {
      const auto remaining =
          std::span<const std::uint32_t>{gpu_gp0_stream_}.subspan(
              gpu_gp0_scan_);
      const auto command_words = sf2Gp0CommandWordCount(remaining);
      if (!command_words) {
        compact_consumed_words();
        return gp1_words;
      }
      const auto opcode =
          static_cast<std::uint8_t>(remaining.front() >> 24U);
      if (opcode == 0x02U) {
        Sf2GpuPacket immediate;
        immediate.gp0_words.assign(
            remaining.begin(),
            remaining.begin() +
                static_cast<std::ptrdiff_t>(*command_words));
        if (sf2GpuTransfer(immediate)) {
          pending_immediate_gpu_packets_.push_back(std::move(immediate));
        }
      } else if (opcode == 0x80U || opcode == 0xa0U) {
        Sf2GpuPacket setup;
        setup.gp0_words.assign(
            remaining.begin(),
            remaining.begin() +
                static_cast<std::ptrdiff_t>(*command_words));
        rememberVramSetupPacket(std::move(setup));
      }
      gpu_gp0_scan_ += *command_words;
    }
    compact_consumed_words();
    return gp1_words;
  }

  void attachVramSetup(Sf2PresentationFrame &frame) const {
    if (vram_setup_packets_.empty()) {
      return;
    }
    std::vector<Sf2GpuPacket> packets;
    packets.reserve(vram_setup_packets_.size() + frame.packets.size());
    for (const auto &upload : vram_setup_packets_) {
      frame.gp0_word_count += upload.gp0_words.size();
      ++frame.gpu_command_count;
      packets.push_back(upload);
    }
    packets.insert(packets.end(),
                   std::make_move_iterator(frame.packets.begin()),
                   std::make_move_iterator(frame.packets.end()));
    frame.packets = std::move(packets);
  }

  void makeDrawEnvironmentSelfContained(Sf2PresentationFrame &frame) {
    std::vector<Sf2GpuPacket> packets;
    packets.reserve(gpu_draw_environment_words_.size() +
                    frame.packets.size());
    for (auto index = std::size_t{};
         index < gpu_draw_environment_words_.size(); ++index) {
      if (!gpu_draw_environment_valid_[index]) {
        continue;
      }
      packets.push_back(Sf2GpuPacket{
          .guest_address = 0U,
          .gp0_words = {gpu_draw_environment_words_[index]},
      });
      ++frame.gp0_word_count;
      ++frame.gpu_command_count;
    }
    packets.insert(packets.end(),
                   std::make_move_iterator(frame.packets.begin()),
                   std::make_move_iterator(frame.packets.end()));
    frame.packets = std::move(packets);

    // Advance the retained GPU state through the authored environment
    // commands so the next OT receives the state this submission leaves.
    for (const auto &packet : frame.packets) {
      if (packet.gp0_words.size() != 1U) {
        continue;
      }
      const auto opcode = packet.gp0_words.front() >> 24U;
      if (opcode >= 0xe1U && opcode <= 0xe6U) {
        const auto index = static_cast<std::size_t>(opcode - 0xe1U);
        gpu_draw_environment_words_[index] = packet.gp0_words.front();
        gpu_draw_environment_valid_[index] = true;
      }
    }
  }

  void appendPresentationSubmission(Sf2PresentationFrame &composition,
                                    Sf2PresentationFrame submission) {
    if (submission.guest_frame >= composition.guest_frame) {
      composition.guest_frame = submission.guest_frame;
      composition.application_state = submission.application_state;
      composition.retail_global_pointer =
          submission.retail_global_pointer;
      composition.retail_draw_buffer_index =
          submission.retail_draw_buffer_index;
      composition.retail_display_buffer_index =
          submission.retail_display_buffer_index;
    }
    composition.gp0_word_count += submission.gp0_word_count;
    composition.gpu_command_count += submission.gpu_command_count;
    composition.draw_command_count += submission.draw_command_count;
    composition.gp1_words.insert(
        composition.gp1_words.end(),
        std::make_move_iterator(submission.gp1_words.begin()),
        std::make_move_iterator(submission.gp1_words.end()));
    const auto packet_offset = composition.packets.size();
    composition.packets.insert(
        composition.packets.end(),
        std::make_move_iterator(submission.packets.begin()),
        std::make_move_iterator(submission.packets.end()));
    if (submission.submission_packet_ends.empty()) {
      composition.submission_packet_ends.push_back(
          composition.packets.size());
    } else {
      for (const auto end : submission.submission_packet_ends) {
        composition.submission_packet_ends.push_back(packet_offset + end);
      }
    }
    composition.submission_roots.insert(
        composition.submission_roots.end(),
        submission.submission_roots.begin(),
        submission.submission_roots.end());
    composition.submission_draw_counts.insert(
        composition.submission_draw_counts.end(),
        submission.submission_draw_counts.begin(),
        submission.submission_draw_counts.end());
  }

  void publishPendingPresentation() {
    if (!pending_presentation_ ||
        !isAuthoredPresentationBase(*pending_presentation_)) {
      pending_presentation_.reset();
      projected_vertices_.clear();
      projected_vertex_stores_.clear();
      projected_vertices_overflow_ = false;
      return;
    }
    if (!projected_vertices_overflow_) {
      static_cast<void>(attachSf2ProjectionProvenance(
          *pending_presentation_, projected_vertices_,
          projected_vertex_stores_));
    }
    projected_vertices_.clear();
    projected_vertex_stores_.clear();
    projected_vertices_overflow_ = false;
    pending_presentation_->sequence = ++published_sequence_;
    presentation_frame_ = std::make_shared<const Sf2PresentationFrame>(
        std::move(*pending_presentation_));
    pending_presentation_.reset();
  }

  void rememberVramSetupPacket(Sf2GpuPacket packet) {
    const auto transfer = sf2GpuTransfer(packet);
    const auto kind = sf2GpuCommandKind(packet);
    if (!transfer ||
        (kind != Sf2GpuCommandKind::copy_vram &&
         kind != Sf2GpuCommandKind::upload_vram)) {
      return;
    }
    if (kind == Sf2GpuCommandKind::copy_vram) {
      const auto source = packet.gp0_words[1U];
      const auto destination = packet.gp0_words[2U];
      // Retail emits a two-pixel self-copy while synchronizing the GPU. It
      // has no retained-state effect, and replaying it before every host
      // frame only duplicates a command that is already present in the
      // authored ordering table. Keep meaningful VRAM moves, but discard
      // exact no-ops.
      if ((source & 0x000fffffU) ==
          (destination & 0x000fffffU)) {
        return;
      }
    }
    // These packets describe retained VRAM state, not a command transcript.
    // Replace an older upload to the same rectangle and move the replacement
    // to the end so overlaps retain their authored chronological ordering.
    const auto previous = std::ranges::find_if(
        vram_setup_packets_, [&](const Sf2GpuPacket &candidate) {
          const auto existing = sf2GpuTransfer(candidate);
          return existing && existing->x == transfer->x &&
                 existing->y == transfer->y &&
                 existing->width == transfer->width &&
                 existing->height == transfer->height;
        });
    if (previous != vram_setup_packets_.end()) {
      vram_setup_packets_.erase(previous);
    }
    vram_setup_packets_.push_back(std::move(packet));
  }

  void captureOrderingTableUploads(const Sf2PresentationFrame &frame) {
    for (const auto &packet : frame.packets) {
      const auto kind = sf2GpuCommandKind(packet);
      if ((kind == Sf2GpuCommandKind::copy_vram ||
           kind == Sf2GpuCommandKind::upload_vram) &&
          sf2GpuTransfer(packet)) {
        rememberVramSetupPacket(packet);
      }
    }
  }

  void attachImmediateGpuPackets(Sf2PresentationFrame &frame) {
    if (pending_immediate_gpu_packets_.empty()) {
      return;
    }
    std::vector<Sf2GpuPacket> packets;
    packets.reserve(pending_immediate_gpu_packets_.size() +
                    frame.packets.size());
    for (auto &packet : pending_immediate_gpu_packets_) {
      frame.gp0_word_count += packet.gp0_words.size();
      ++frame.gpu_command_count;
      packets.push_back(std::move(packet));
    }
    pending_immediate_gpu_packets_.clear();
    packets.insert(packets.end(),
                   std::make_move_iterator(frame.packets.begin()),
                   std::make_move_iterator(frame.packets.end()));
    frame.packets = std::move(packets);
  }

  [[nodiscard]] bool startMissionScriptsIfReady() noexcept {
    if (scripts_started_) {
      return true;
    }
    constexpr std::uint32_t program_count_address = 0x8011fa14U;
    std::uint16_t count{};
    if (!vm_.runtime().read16(program_count_address, count)) {
      markFault("could not read the SF2 mission-script registry");
      return false;
    }
    if (count == 0U) {
      return true;
    }
    const auto global_pointer = vm_.runtime().state().gpr[28U];
    const std::array lookup_arguments{global_pointer + 0x06e8U};
    const auto lookup = invokeNested(0x800b3920U, lookup_arguments);
    if (!lookup.completed() && !lookup.stoppedAtHostBoundary()) {
      markFault("SF2 LEVEL program lookup failed");
      return false;
    }
    const auto level_program = lookup.return_value;
    if (level_program == 0U) {
      return true;
    }
    script_active_programs_at_start_check_ =
        static_cast<std::uint16_t>(std::min<std::size_t>(
            active_script_programs_.size(),
            std::numeric_limits<std::uint16_t>::max()));
    script_level_active_at_start_check_ =
        std::ranges::find(active_script_programs_, level_program) !=
        active_script_programs_.end();
    script_program_count_at_start_ = count;
    script_start_guest_frame_ = guest_frame_;
    // The retail mission handoff normally calls ResetAndStartLevel itself.
    // Repeating it here after LEVEL is already active can erase one-shot
    // activation side effects. Only repair the historical direct-handoff case
    // where the registry is ready but LEVEL is not active.
    if (script_level_active_at_start_check_) {
      scripts_started_ = true;
      return true;
    }
    const auto reset =
        invokeNested(0x800b3d34U, std::span<const std::uint32_t>{});
    if (!reset.completed() && !reset.stoppedAtHostBoundary()) {
      markFault("SF2 mission-script reset failed");
      return false;
    }
    // ResetAndStartLevel resolves and activates LEVEL internally. Do not call
    // MissionScript_Activate a second time after that authentic operation.
    scripts_started_ = true;
    return true;
  }

  [[nodiscard]] bool
  advanceGuestBoundary(bool &display_submitted) noexcept {
    display_submitted = false;
    const auto hex = [] (std::uint32_t value) {
      constexpr char digits[] = "0123456789ABCDEF";
      std::string text(8U, '0');
      for (auto index = std::size_t{}; index < text.size(); ++index) {
        text[text.size() - index - 1U] =
            digits[(value >> (index * 4U)) & 0x0fU];
      }
      return text;
    };
    const auto boundary = runUntilBoundary(profile_.gpu_submission_entry);
    if (const auto &trace = vm_.runtime().writeTraceHit();
        trace.width != 0U) {
      if (completion_flow_trace_active_) {
        ++movie_selection_writes_;
        last_movie_selection_writer_pc_ = trace.pc;
        last_movie_selection_writer_instruction_ = trace.instruction;
        last_movie_selection_write_value_ = trace.value;
        const auto slot = static_cast<std::size_t>(
            (movie_selection_writes_ - 1U) %
            movie_selection_writer_pcs_.size());
        movie_selection_writer_pcs_[slot] = trace.pc;
        movie_selection_write_values_[slot] = trace.value;
      } else {
        ++hud_primitive_writes_;
      auto slot = hud_primitive_writer_pcs_.size();
      for (auto index = std::size_t{};
           index < hud_primitive_writer_pcs_.size(); ++index) {
        if (hud_primitive_writer_pcs_[index] == trace.pc ||
            hud_primitive_writer_pcs_[index] == 0U) {
          slot = index;
          break;
        }
      }
      if (slot < hud_primitive_writer_pcs_.size()) {
        hud_primitive_writer_pcs_[slot] = trace.pc;
        hud_primitive_writer_addresses_[slot] = trace.address;
        hud_primitive_writer_instructions_[slot] = trace.instruction;
        ++hud_primitive_writer_counts_[slot];
        std::uint16_t build_buffer{};
        static_cast<void>(vm_.runtime().read16(
            vm_.runtime().state().gpr[28U] + 0x2aU, build_buffer));
        if (build_buffer < 8U) {
          hud_primitive_writer_buffer_masks_[slot] |=
              static_cast<std::uint8_t>(1U << build_buffer);
        }
      }
      }
      vm_.runtime().clearWriteTraceHit();
    }
    auto write_watch = vm_.runtime().writeWatchHit();
    // Retail's exception/callback trampoline intentionally self-patches the
    // instruction eight bytes before its continuation on first use. It is a
    // bounded zero write, not an ordering-table escape; keep watching for the
    // first unrelated store.
    if (write_watch.width == 4U && write_watch.pc == 0x80011294U &&
        write_watch.value == 0U) {
      vm_.runtime().clearWriteWatchHit();
      write_watch = {};
    }
    if (write_watch.width != 0U) {
      ++renderer_text_repairs_;
      last_renderer_text_repair_address_ = write_watch.address;
      last_renderer_text_actual_ = write_watch.value;
      last_renderer_text_writer_pc_ = write_watch.pc;
      last_renderer_text_writer_instruction_ =
          write_watch.instruction;
      const auto aligned_address = write_watch.address & ~3U;
      if (!vm_.runtime().read32(aligned_address,
                                last_renderer_text_expected_)) {
        markFault("could not read protected SF2 renderer word after "
                  "suppressing a write");
        return false;
      }
      vm_.runtime().clearWriteWatchHit();
    }
    if (boundary.yieldedAfterHostCall() &&
        (mission_restart_requested_ || quit_to_title_requested_)) {
      // No GPU boundary belongs to a retired mission generation. A retail
      // checkpoint-without-state request and MENU's Restart Mission / Save and
      // Quit callbacks all publish an explicit outer-lifecycle request before
      // yielding. Treat that semantic request—not a particular host-call
      // address—as the boundary contract so the product can reconstruct the
      // package or return to TITLE without misreporting a guest execution
      // fault.
      return true;
    }
    if (boundary.yieldedAfterHostCall() &&
        boundary.yielded_host_call == 0x80142e60U &&
        pending_scripted_movie_catalog_index_) {
      // The decoder-init replacement deliberately yields before MOVIE.OVL's
      // continuation can install or service decoder callbacks. Return to the
      // product so it can present the STR, then resume this exact guest stack
      // through completeScriptedMovie(). No GPU boundary was retired here.
      scripted_movie_host_yielded_ = true;
      return true;
    }
    if (!boundary.stoppedAtHostBoundary()) {
      if (faulted_) {
        return false;
      }
      auto detail = "guest stopped before the next GPU submission: " +
                    std::string{psx::toString(boundary.execution.reason)} +
                    " at 0x" + hex(boundary.execution.pc) +
                    " instruction=0x" + hex(boundary.execution.instruction);
      const auto &state = vm_.runtime().state();
      const auto opcode = boundary.execution.instruction >> 26U;
      if (opcode >= 0x20U && opcode <= 0x3aU) {
        const auto base =
            (boundary.execution.instruction >> 21U) & 0x1fU;
        const auto immediate = static_cast<std::int16_t>(
            boundary.execution.instruction & 0xffffU);
        detail += " address=0x" +
                  hex(state.gpr[base] +
                      static_cast<std::uint32_t>(
                          static_cast<std::int32_t>(immediate)));
      }
      detail += " a0=0x" + hex(state.gpr[4U]) +
                " a1=0x" + hex(state.gpr[5U]) +
                " v0=0x" + hex(state.gpr[2U]) +
                " v1=0x" + hex(state.gpr[3U]) +
                " s0=0x" + hex(state.gpr[16U]) +
                " s1=0x" + hex(state.gpr[17U]) +
                " s2=0x" + hex(state.gpr[18U]) +
                " ra=0x" + hex(state.gpr[31U]) +
                " sp=0x" + hex(state.gpr[29U]) +
                " opening-sp=0x" + hex(opening_stack_);
      if (boundary.execution.pc == 0x800fa750U) {
        detail += " bank=";
        for (auto offset = std::uint32_t{}; offset < 0x30U;
             offset += sizeof(std::uint32_t)) {
          std::uint32_t value{};
          if (vm_.runtime().read32(state.gpr[4U] + offset, value)) {
            detail += (offset == 0U ? "" : "/") + hex(value);
          } else {
            detail += (offset == 0U ? "" : "/") + std::string{"????????"};
          }
        }
        detail += " table-window=";
        const auto entries = state.gpr[2U];
        const auto aligned_entries = entries & ~3U;
        for (auto offset = std::int32_t{-16}; offset <= 16; offset += 4) {
          std::uint32_t value{};
          if (vm_.runtime().read32(
                  aligned_entries + static_cast<std::uint32_t>(offset),
                  value)) {
            detail += (offset == -16 ? "" : "/") + hex(value);
          } else {
            detail += (offset == -16 ? "" : "/") +
                      std::string{"????????"};
          }
        }
      }
      if (boundary.execution.pc == 0x800b3cd8U) {
        std::uint32_t command{};
        std::uint32_t previous{};
        static_cast<void>(
            vm_.runtime().read32(state.gpr[16U] - 4U, command));
        static_cast<void>(
            vm_.runtime().read32(state.gpr[16U] - 8U, previous));
        detail += " command=" + hex(previous) + "/" + hex(command) +
                  " script=";
        for (auto offset = std::uint32_t{}; offset < 0x18U;
             offset += sizeof(std::uint32_t)) {
          std::uint32_t value{};
          if (vm_.runtime().read32(state.gpr[17U] + offset, value)) {
            detail += (offset == 0U ? "" : "/") + hex(value);
          }
        }
      }
      if (!scheduler_fault_detail_.empty()) {
        detail += " (" + scheduler_fault_detail_ + ")";
      }
      markFault(detail);
      return false;
    }
    const auto boundary_sp = vm_.runtime().state().gpr[29U];
    diagnostics_.minimum_stack_pointer =
        diagnostics_.minimum_stack_pointer == 0U
            ? boundary_sp
            : std::min(diagnostics_.minimum_stack_pointer, boundary_sp);
    diagnostics_.maximum_stack_pointer =
        std::max(diagnostics_.maximum_stack_pointer, boundary_sp);
    auto boundary_gp1_words = captureGpuSideEffects();
    stabilizeCollisionRoom();
    std::uint32_t application_state{};
    if (!vm_.runtime().read32(profile_.application_state, application_state)) {
      markFault("could not read SF2 application state");
      return false;
    }
    // MENU uses transient uploads in the gameplay texture region. The real
    // GPU retains each application's authored VRAM contents, while the native
    // bridge replays a single retained upload cache before every frame. Keep
    // the pre-menu cache isolated so MENU.TIM/SCOPED.TIM cannot remain resident
    // over actor textures after state 7 hands control back to gameplay.
    if (previous_application_state_ == 0U && application_state == 9U &&
        !menu_gameplay_vram_setup_packets_) {
      menu_gameplay_vram_setup_packets_ = vram_setup_packets_;
      menu_transition_active_ = false;
      ++menu_vram_snapshots_;
    }
    if (application_state == 7U && menu_gameplay_vram_setup_packets_) {
      menu_transition_active_ = true;
    }
    std::uint32_t player_instance{};
    const auto player_state_available =
        vm_.runtime().read32(0x8012a574U, player_instance) &&
        player_instance != 0U;
    if (application_state == 7U) {
      pause_menu_lifecycle_active_ = true;
      pause_menu_player_was_present_ =
          pause_menu_player_was_present_ || player_state_available;
    }
    if (application_state == 0U && previous_application_state_ != 0U &&
        menu_gameplay_vram_setup_packets_) {
      if (menu_transition_active_) {
        vram_setup_packets_ =
            std::move(*menu_gameplay_vram_setup_packets_);
        ++menu_vram_restores_;
      }
      menu_gameplay_vram_setup_packets_.reset();
      menu_transition_active_ = false;
    }
    if (application_state == 0U && previous_application_state_ != 0U) {
      // Returning to gameplay without invoking the quit transition closes the
      // pause lifecycle. Save and Quit calls the shared transition before a
      // state-0 presentation boundary, so the latch remains live there.
      pause_menu_lifecycle_active_ = false;
      pause_menu_player_was_present_ = false;
    }
    previous_application_state_ = application_state;
    if (retail_restore_active_ && application_state == 0U &&
        guest_frame_ > retail_restore_start_frame_ + 60U) {
      retail_restore_active_ = false;
      if (!installSoundServiceCallback()) {
        markFault("could not restore SF2 sound callback after checkpoint");
        return false;
      }
    }
    display_submitted =
        vm_.runtime().state().gpr[31U] == render_submission_return_;
    const auto submission_root = vm_.runtime().state().gpr[5U];
    auto captured_submission = captureSf2PresentationFrame(
        vm_.runtime().ram(), submission_root, application_state,
        presentation_sequence_ + 1U, guest_frame_ + 1U);
    if (start_mode_ == Sf2GuestRuntimeStartMode::retail_briefing &&
        application_state == 8U) {
      ++briefing_state8_boundaries_;
      briefing_state8_maximum_draws_ = std::max(
          briefing_state8_maximum_draws_,
          captured_submission ? captured_submission->draw_command_count : 0U);
    }
    if (start_mode_ == Sf2GuestRuntimeStartMode::retail_briefing &&
        application_state == 8U && captured_submission &&
        captured_submission->draw_command_count != 0U) {
      // State 8 submits through its loading renderer rather than gameplay's
      // common return site. It is still an authored display boundary; promote
      // it only for the disposable briefing presenter so the exact OT can be
      // retained without weakening gameplay's proven boundary contract.
      display_submitted = true;
    }
    if (display_submitted) {
      ++presentation_sequence_;
      ++guest_frame_;
      std::uint32_t system_clock{};
      if (!vm_.runtime().read32(profile_.system_clock, system_clock)) {
        markFault("could not read the SF2 presentation clock");
        return false;
      }
      ++observed_gpu_submissions_;
      last_gpu_submission_root_ = submission_root;
      last_gpu_submission_draw_count_ =
          captured_submission ? captured_submission->draw_command_count : 0U;
      last_gpu_submission_packet_count_ =
          captured_submission ? captured_submission->packets.size() : 0U;
      last_gpu_submission_copy_count_ = 0U;
      last_gpu_submission_upload_count_ = 0U;
      last_gpu_submission_first_draw_packet_ = 0U;
      last_gpu_submission_last_draw_packet_ = 0U;
      last_gpu_submission_copy_ = {};
      last_gpu_submission_copy_source_x_ = 0U;
      last_gpu_submission_copy_source_y_ = 0U;
      if (captured_submission) {
        for (const auto &packet : captured_submission->packets) {
          if (packet.gp0_words.empty()) {
            continue;
          }
          const auto opcode = packet.gp0_words.front() >> 24U;
          if (sf2GpuCommandKind(packet) == Sf2GpuCommandKind::draw) {
            if (last_gpu_submission_first_draw_packet_ == 0U) {
              last_gpu_submission_first_draw_packet_ = packet.guest_address;
            }
            last_gpu_submission_last_draw_packet_ = packet.guest_address;
          }
          last_gpu_submission_copy_count_ += opcode == 0x80U ? 1U : 0U;
          last_gpu_submission_upload_count_ += opcode == 0xa0U ? 1U : 0U;
          if (opcode == 0x80U) {
            if (const auto transfer = sf2GpuTransfer(packet)) {
              last_gpu_submission_copy_ = *transfer;
              if (packet.gp0_words.size() >= 2U) {
                last_gpu_submission_copy_source_x_ =
                    static_cast<std::uint16_t>(packet.gp0_words[1U] &
                                               0x3ffU);
                last_gpu_submission_copy_source_y_ =
                    static_cast<std::uint16_t>(
                        (packet.gp0_words[1U] >> 16U) & 0x1ffU);
              }
            }
          }
        }
      }
      last_gpu_submission_clock_ = system_clock;
      static_cast<void>(vm_.runtime().read16(
          vm_.runtime().state().gpr[28U] + 0x26U,
          last_gpu_submission_draw_buffer_));
      static_cast<void>(vm_.runtime().read16(
          vm_.runtime().state().gpr[28U] + 0x2aU,
          last_gpu_submission_build_buffer_));
      // A clock transition closes the previous composition even when the
      // boundary which revealed it is a utility list that cannot itself be
      // captured as an authored OT. Waiting for the next capturable list
      // accidentally accumulated several retail ticks and overran audio.
      if (pending_presentation_ &&
          system_clock != pending_presentation_clock_) {
        publishPendingPresentation();
      }
      auto frame = std::move(captured_submission);
      if (frame) {
        frame->gp1_words = std::move(boundary_gp1_words);
        const auto invalid_packet = std::ranges::find_if(
            frame->packets, [](const Sf2GpuPacket &packet) {
              return packet.guest_address >= protected_renderer_begin_ &&
                     packet.guest_address <
                         protected_renderer_begin_ +
                             protected_renderer_size_;
            });
        if (invalid_packet != frame->packets.end()) {
          ++rejected_renderer_ordering_tables_;
          last_rejected_renderer_packet_ = invalid_packet->guest_address;
          last_rejected_renderer_root_ = frame->ordering_table_root;
          last_rejected_renderer_frame_ = frame->guest_frame;
          frame.reset();
        }
      }
      if (frame) {
        frame->retail_global_pointer = vm_.runtime().state().gpr[28U];
        const auto retail_draw_buffer_index =
            frame->retail_global_pointer + 0x26U;
        const auto retail_display_buffer_index =
            frame->retail_global_pointer + 0x28U;
        static_cast<void>(vm_.runtime().read16(
            retail_draw_buffer_index, frame->retail_draw_buffer_index));
        static_cast<void>(vm_.runtime().read16(
            retail_display_buffer_index,
            frame->retail_display_buffer_index));
        if (isAuthoredPresentationBase(*frame)) {
          // ClearImage/GP0(02) is emitted immediately before DrawOTag rather
          // than linked into the OT. It belongs at the front of this world
          // composition; dropping it leaves both persistent native pages to
          // accumulate snow, transparency and old map pixels indefinitely.
          attachImmediateGpuPackets(*frame);
        }
        makeDrawEnvironmentSelfContained(*frame);
        captureOrderingTableUploads(*frame);
      }
      if (frame &&
          start_mode_ == Sf2GuestRuntimeStartMode::retail_briefing &&
          application_state == 8U && frame->draw_command_count != 0U) {
        frame->submission_roots.push_back(frame->ordering_table_root);
        frame->submission_draw_counts.push_back(frame->draw_command_count);
        attachVramSetup(*frame);
        frame->submission_packet_ends.push_back(frame->packets.size());
        if (!presentation_frame_ ||
            presentation_frame_->application_state != 8U ||
            frame->draw_command_count >=
                presentation_frame_->draw_command_count) {
          presentation_frame_ = std::make_shared<const Sf2PresentationFrame>(
              std::move(*frame));
        }
        frame.reset();
      }
      // Only authored world/UI lists belong in the visible composition.
      // Retail also queues maintenance OTs whose commands are not display
      // content; drawing them into the native target darkens the scene.
      const auto auxiliary =
          frame && pending_presentation_ &&
          system_clock == pending_presentation_clock_ &&
          frame->draw_command_count != 0U;
      const auto auxiliary_targets_world_page =
          auxiliary && frame->retail_draw_buffer_index ==
                           pending_presentation_->retail_draw_buffer_index;
      if (frame && (isAuthoredPresentationBase(*frame) || auxiliary)) {
        frame->submission_roots.push_back(frame->ordering_table_root);
        frame->submission_draw_counts.push_back(
            frame->draw_command_count);
        attachVramSetup(*frame);
        frame->submission_packet_ends.push_back(frame->packets.size());
        if (auxiliary) {
          // SF2 submits matching UI OTs for both PS1 framebuffer pages. The
          // native compositor has one target, so appending the opposite page
          // superimposes stale map/UI state and duplicates notifications.
          if (!disable_retail_auxiliary_ui_ && pending_presentation_ &&
              auxiliary_targets_world_page) {
            appendPresentationSubmission(*pending_presentation_,
                                         std::move(*frame));
          }
        } else if (!pending_presentation_) {
          pending_presentation_clock_ = system_clock;
          pending_presentation_ = std::move(*frame);
        } else {
          appendPresentationSubmission(*pending_presentation_,
                                       std::move(*frame));
        }
      }
    }
    const auto retired = vm_.resumeCurrentPcClockNeutral(1U);
    if (retired.execution.reason !=
        psx::R3000StopReason::instruction_budget) {
      markFault("could not retire SF2 GPU submission");
      return false;
    }
    // Guest gameplay execution is atomic. A published display frame advances
    // one authored 60 Hz interval; utility DrawOTag submissions consume no
    // additional hardware/audio time.
    // Guest execution consumes CPU-domain time in complete scheduler quanta
    // inside runUntilBoundary(). A display submission waits only for the next
    // 60 Hz event after those cycles; it must not add a second full retrace.
    // Ordinary frames which never exhaust a quantum retain the exact previous
    // one-retrace cadence, while long code and device waits can naturally miss
    // one or more VBlanks without recognizing their PC or polling function.
    const auto scheduler_ticks =
        !display_submitted
            ? 0U
            : retrace_ticks_ == 0U ? retrace_period_
                                   : retrace_period_ - retrace_ticks_;
    if (!serviceScheduler(scheduler_ticks, scheduler_ticks)) {
      markFault("SF2 device callback scheduler failed");
      return false;
    }
    if (!startMissionScriptsIfReady()) {
      return false;
    }
    // Observe completion of a checkpoint capture initiated by retail mission
    // logic. This is deliberately independent of a frame/camera threshold:
    // the checkpoint-present flag and serializer command stream are the
    // authoritative lifecycle state.
    if (!checkpoint_captured_ && retail_checkpoint_capture_pending_ &&
        checkpointStateReady()) {
      checkpoint_captured_ = true;
      checkpoint_capture_frame_ = guest_frame_;
      retail_checkpoint_capture_pending_ = false;
    }

    // Camera ownership remains presentation/input evidence only. Observe it
    // independently from checkpoint creation so an authored scripted camera
    // cannot move the replaced frontend's baseline past one-shot LEVEL events.
    if (!scripted_camera_observed_ && scripts_started_ &&
        application_state == 0U) {
      std::uint32_t player{};
      std::int32_t player_x{};
      std::int32_t player_y{};
      std::int32_t player_z{};
      std::uint16_t player_health{};
      std::uint16_t player_armor{};
      std::uint32_t camera_wrapper{};
      std::uint32_t camera_owner{};
      readPlayerState(player, player_x, player_y, player_z, player_health,
                      player_armor);
      if (readPlayerCameraOwnership(player, camera_wrapper, camera_owner) &&
          camera_owner != 0U && camera_owner != player) {
        scripted_camera_observed_ = true;
        pc_chase_scripted_camera_seen_ = true;
      }
    }

    // The direct TITLE-to-mission bridge replaces a frontend handoff which
    // normally supplies an initial restart state. The serializer is not ready
    // at bootstrap, and capturing after an authored opening object event would
    // persist that one-shot side effect as already consumed. Only manufacture
    // the baseline when no opening event has run. Otherwise death/restart owns
    // a clean same-package reconstruction until retail creates a checkpoint.
    // Observe the first authored script interval before deciding whether a
    // replacement-frontend baseline is legal. TRAIN's 0x62 transfer arrives
    // at guest frame 13 and COLO's 0x72 transfer at frame 22; committing at
    // the historical frame-16 boundary raced both semantics.
    constexpr auto initial_checkpoint_observation_frames = std::uint64_t{32U};
    if (!checkpoint_captured_ && scripts_started_ && application_state == 0U &&
        guest_frame_ >= initial_checkpoint_observation_frames &&
        !initial_checkpoint_deferred_by_opening_event_) {
      const auto checkpoint =
          invokeNested(0x800ad48cU, std::span<const std::uint32_t>{});
      if (!checkpoint.completed() && !checkpoint.stoppedAtHostBoundary()) {
        markFault("SF2 initial retail checkpoint capture failed");
        return false;
      }
      if (checkpointStateReady()) {
        checkpoint_captured_ = true;
        checkpoint_capture_frame_ = guest_frame_;
        retail_checkpoint_capture_pending_ = false;
      }
    }

    return true;
  }

  [[nodiscard]] bool checkpointStateReady() const noexcept {
    constexpr std::uint32_t state = 0x8013b27cU;
    constexpr std::uint32_t size = 0x848U;
    std::uint32_t first{};
    if (!vm_.runtime().read32(state, first) || (first & 0x8000U) == 0U) {
      return false;
    }
    for (auto offset = std::uint32_t{}; offset < size;
         offset += sizeof(std::uint32_t)) {
      std::uint32_t word{};
      if (!vm_.runtime().read32(state + offset, word)) {
        return false;
      }
      if (word == 0xffffffffU) {
        return true;
      }
    }
    return false;
  }

  void stabilizeCollisionRoom() noexcept {
    // This containment is backed by the captured HWAY room-14 failure. Other
    // missions can legitimately publish no current room during authored
    // traversal states (notably WRECK's airborne opening), so do not rewrite
    // their collision ownership without equivalent evidence.
    if (mission_index_ != 2U) {
      return;
    }
    constexpr std::uint32_t collision_owner_handle_address = 0x8012a654U;
    constexpr std::uint32_t collision_room_count_address = 0x8011f660U;
    constexpr std::uint32_t owner_room_offset = 0x1a0U;
    std::uint32_t owner_handle{};
    std::uint32_t owner{};
    std::uint32_t room_count{};
    std::uint16_t room{};
    if (!vm_.runtime().read32(collision_owner_handle_address, owner_handle) ||
        owner_handle == 0U ||
        !vm_.runtime().read32(owner_handle, owner) || owner == 0U ||
        !vm_.runtime().read32(collision_room_count_address, room_count) ||
        room_count == 0U ||
        !vm_.runtime().read16(owner + owner_room_offset, room)) {
      return;
    }
    auto dialogue_active = false;
    for (auto index = std::uint32_t{}; index < 3U; ++index) {
      std::uint32_t dialogue_state{};
      if (vm_.runtime().read32(0x80134d1cU + index * 0x10U,
                               dialogue_state) &&
          dialogue_state != 0xffffffffU) {
        dialogue_active = true;
      }
    }
    if (room < room_count) {
      last_valid_collision_room_ = room;
      return;
    }
    // Mission 3 intermittently publishes 0xFFFF for one or more render
    // boundaries while the player remains inside the same loaded room. The
    // following floor scan then has no collision list and can drop Gabe
    // through valid geometry. Retain only the immediately preceding retail
    // room and let any subsequent valid resolver result replace it.
    if (!dialogue_active && room == 0xffffU &&
        last_valid_collision_room_ < room_count &&
        vm_.runtime().write16(owner + owner_room_offset,
                              last_valid_collision_room_)) {
      ++collision_room_fallbacks_;
      last_collision_room_fallback_ = last_valid_collision_room_;
    }
  }

  void stabilizePlayerCollisionRequest(
      LegacyHostCallContext &context, std::uint32_t request) noexcept {
    constexpr std::uint32_t request_room_offset = 0x0cU;
    constexpr std::uint32_t collision_room_count_address = 0x8011f660U;
    std::uint32_t request_room{};
    std::uint32_t room_count{};
    ++player_collision_requests_;
    if (!context.read32(request + request_room_offset, request_room)) {
      return;
    }
    if (request_room != 0xffffU && request_room != 0xffffffffU) {
      return;
    }
    ++invalid_player_collision_requests_;
    if (last_valid_collision_room_ == 0xffffU ||
        !context.read32(collision_room_count_address, room_count) ||
        last_valid_collision_room_ >= room_count ||
        !context.write32(request + request_room_offset,
                         last_valid_collision_room_)) {
      return;
    }
    ++collision_request_fallbacks_;
    last_collision_request_fallback_ = last_valid_collision_room_;
  }

  struct OpenFile {
    const std::vector<std::byte> *bytes{};
    std::size_t offset{};
  };

  struct QuickState {
    LegacyGameplayVmSnapshot vm;
    std::map<std::uint32_t, OpenFile> open_files;
    std::vector<std::uint32_t> gpu_gp0_stream;
    std::size_t gpu_gp0_scan{};
    std::array<std::uint32_t, 6U> gpu_draw_environment_words{};
    std::array<bool, 6U> gpu_draw_environment_valid{};
    std::vector<Sf2GpuPacket> vram_setup_packets;
    std::optional<std::vector<Sf2GpuPacket>>
        menu_gameplay_vram_setup_packets;
    std::uint32_t previous_application_state{
        std::numeric_limits<std::uint32_t>::max()};
    bool menu_transition_active{};
    bool pause_menu_lifecycle_active{};
    bool pause_menu_player_was_present{};
    std::vector<Sf2GpuPacket> pending_immediate_gpu_packets;
    std::shared_ptr<const Sf2PresentationFrame> presentation_frame;
    std::optional<Sf2PresentationFrame> pending_presentation;
    std::uint32_t pending_presentation_clock{};
    std::uint64_t published_sequence{};
    std::vector<std::uint32_t> active_script_programs;
    std::uint64_t callback_ticks{};
    std::uint64_t audio_callback_ticks{};
    std::uint64_t retrace_ticks{};
    std::uint64_t presentation_sequence{};
    std::uint64_t guest_frame{};
    std::size_t mission_pad_polls{};
    std::uint64_t host_pad_samples{};
    bool suppress_interrupts{};
    bool realtime_display_clock{};
    bool scripts_started{};
    bool loading_confirm_sent{};
    bool checkpoint_captured{};
    std::uint64_t checkpoint_capture_frame{};
    std::uint64_t retail_checkpoint_capture_calls{};
    bool retail_checkpoint_capture_pending{};
    bool mission_restart_requested{};
    bool quit_to_title_requested{};
    bool scripted_camera_observed{};
    bool initial_checkpoint_deferred_by_opening_event{};
    bool retail_restore_active{};
    std::uint64_t retail_restore_start_frame{};
    bool xa_absolute_disc_active{};
    bool mission_success_pending{};
    bool mission_complete_requested{};
    std::uint64_t mission_success_events{};
    std::uint64_t mission_failure_events{};
    std::uint64_t scripted_movie_handoffs{};
    std::uint32_t last_scripted_movie_catalog_index{0xffffffffU};
    std::optional<std::uint8_t> pending_scripted_movie_catalog_index;
    std::optional<std::uint8_t> active_scripted_movie_catalog_index;
    bool scripted_movie_host_yielded{};
    std::uint64_t ui_text_event_count{};
    std::array<Sf2GuestUiTextEvent, 64U> ui_text_events{};
    std::uint16_t mission_timer_handle{0xffffU};
    std::array<char, 9U> mission_timer_text{};
    std::uint64_t mission_timer_text_updates{};
  };

  static constexpr auto profile_ = sf2UsaGuestRuntimeProfile();
  static constexpr std::uint32_t protected_renderer_begin_ = 0x8001d000U;
  static constexpr std::size_t protected_renderer_size_ = 0x2000U;
  static constexpr std::uint64_t scheduler_slice_budget_ = 50'000U;
  static constexpr std::uint64_t execution_budget_ = 100'000'000U;
  static constexpr std::uint64_t callback_period_ =
      psx::CdRomController::cpu_clock_hz /
      LegacyGameplayVm::updates_per_second;
  static constexpr std::uint64_t audio_callback_period_ =
      psx::CdRomController::cpu_clock_hz / 120U;
  static constexpr std::uint64_t retrace_period_ =
      psx::CdRomController::cpu_clock_hz / 60U;
  // Keep interrupt callbacks below the executable at 0x80010000. The old
  // 0x807F0000 address mirrored onto physical RAM at 0x001F0000, where large
  // checkpoint CD transfers could overwrite saved callback return addresses.
  static constexpr std::uint32_t callback_stack_ = 0x8000b000U;
  static constexpr std::uint32_t return_trampoline_ = 0x8000c000U;
  static constexpr std::uint32_t exception_trampoline_ = 0x8000c100U;
  static constexpr std::uint32_t processed_pad_records_ = 0x80122fecU;
  static constexpr std::uint32_t render_submission_return_ = 0x800f181cU;

  void markFault(std::string_view detail) noexcept {
    faulted_ = true;
    ready_ = false;
    try {
      fault_detail_.assign(detail);
    } catch (...) {
      fault_detail_ = "SF2 runtime fault";
    }
  }

  void setXaAbsoluteDiscActive(bool active) noexcept {
    xa_absolute_disc_active_ = active;
    xa_stream_observed_active_ = false;
    if (active) {
      // The XA archive handle stores the ISO file's absolute extent LBA at
      // +0x84. XaCue_ResolveAndPlay passes that value (plus an authored group
      // offset) directly to CdlSetloc. Disable the mission-relative FOG window
      // while speech owns the drive; mapping onto SCENES*.XA here would add
      // its base twice and begin every cue at the wrong dialogue group.
      cdrom_media_.clearRelativeExtent();
      return;
    }
    cdrom_media_.mapRelativeExtent(mission_relative_extent_base_,
                                   mission_relative_extent_sector_count_);
  }

  void setStage(std::string_view stage) {
    stage_.assign(stage);
  }

  void loadAssets() {
    fog_bytes_ = disc_.image().readFile(fog_path_);
    init_overlay_ = disc_.image().readFile("BIN/INIT.OVL");
    const auto mission_extent = disc_.image().find(fog_path_);
    mission_relative_extent_base_ = mission_extent.extent_lba;
    mission_relative_extent_sector_count_ = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(mission_extent.size) +
         assets::FogArchive::sector_size - 1U) /
        assets::FogArchive::sector_size);
    const auto resident =
        parseEmbeddedHog(disc_.executable(), "BEEPSX.VB");
    for (const auto &entry : resident.entries()) {
      const auto file = resident.file(entry.name);
      resident_files_.emplace(
          entry.name, std::vector<std::byte>{file.begin(), file.end()});
    }
    const auto fog = assets::FogArchive::parse(fog_bytes_);
    fog_entries_ = fog.entries();
    for (const auto &entry : fog.entries()) {
      const auto file = fog.file(entry.name);
      fog_files_.emplace(
          entry.name, std::vector<std::byte>{file.begin(), file.end()});
    }
    // Some sequel missions put their character/model archives one level
    // deeper (for example AIRBASEX.FOG:NPC.HOG:BUDDY.HOG). Retail opens the
    // nested names through the same file API after parsing the container.
    // Keep those authored files addressable by name so the host-backed CD
    // bridge preserves that behavior instead of returning an empty model.
    for (const auto &[name, bytes] : fog_files_) {
      if (!name.ends_with(".HOG")) {
        continue;
      }
      try {
        const auto outer = assets::HogArchive::parse(bytes);
        const auto contains_nested_archive = std::ranges::any_of(
            outer.entries(), [](const assets::HogEntry &entry) {
              return entry.name.ends_with(".HOG");
            });
        if (!contains_nested_archive) {
          continue;
        }
        for (const auto &entry : outer.entries()) {
          const auto file = outer.file(entry.name);
          auto payload =
              std::vector<std::byte>{file.begin(), file.end()};
          nested_files_.insert_or_assign(entry.name, payload);
          if (!entry.name.ends_with(".HOG")) {
            continue;
          }
          try {
            const auto inner = assets::HogArchive::parse(std::move(payload));
            for (const auto &inner_entry : inner.entries()) {
              const auto inner_file = inner.file(inner_entry.name);
              nested_files_.insert_or_assign(
                  inner_entry.name,
                  std::vector<std::byte>{inner_file.begin(),
                                         inner_file.end()});
            }
          } catch (const core::Error &) {
            // A .HOG-named payload that is not itself an archive remains
            // available as the direct child collected above.
          }
        }
      } catch (const core::Error &) {
        // Ordinary flat mission HOGs continue through retail's existing
        // archive path and do not need host-side name exposure.
      }
    }
  }

  void bindPlatform() {
    vm_.machine().setCdRomMedia(&cdrom_media_);
    vm_.bindPsxBiosCoreVector(true);
    installExceptionBridge();
    // Exact instruction-aligned counterpart of SF1's proven 0x80037B08
    // mouse camera/facing boundary. Unlike the structurally similar
    // 0x80049940 routine, this path is live in ordinary player gameplay.
    vm_.bindHostCall(
        0x80053464U, [this](LegacyHostCallContext &context) {
          applyPcChaseCameraPitch();
          applyPcMouseFacingVector(context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800539d0U, [this](LegacyHostCallContext &context) {
          applyPcManualAim(context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x801669ccU, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t source123_physics = 0x801a70a4U;
          if (mission_index_ == 4U &&
              context.argument(0U) == source123_physics) {
            ++airbasex_attachment_init_calls_;
            for (auto index = std::size_t{};
                 index < airbasex_attachment_init_arguments_.size();
                 ++index) {
              airbasex_attachment_init_arguments_[index] =
                  context.argument(static_cast<std::uint32_t>(index));
            }
            static_cast<void>(context.read32(
                context.argument(1U) + 0x10U,
                airbasex_attachment_existing_link_));
            if (context.read32(context.argument(0U) + 0x10U,
                               airbasex_attachment_node_) &&
                airbasex_attachment_node_ != 0U) {
              static_cast<void>(context.read32(
                  airbasex_attachment_node_ + 0x28U,
                  airbasex_attachment_node_flags_));
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a3f90U, [this](LegacyHostCallContext &context) {
          std::string name;
          if (!context.readCString(context.argument(0U), name, 128U)) {
            context.continueGuestInstruction();
            return;
          }
          std::ranges::transform(
              name, name.begin(), [](unsigned char value) {
                return static_cast<char>(std::toupper(value));
              });
          const auto nested = nested_files_.find(name);
          if (nested == nested_files_.end() ||
              context.argument(1U) == 0U ||
              nested->second.size() > context.argument(2U)) {
            context.continueGuestInstruction();
            return;
          }
          const auto callback = context.argument(4U);
          if (!context.writeBytes(context.argument(1U), nested->second) ||
              callback == 0U || pending_archive_callback_return_ != 0U) {
            context.setReturnValue(5U);
            return;
          }
          // Retail action 0x79 immediately drains these nested NPC-resource
          // reads before it activates their actors. The host scheduler cannot
          // advance CD hardware from inside that synchronous guest spin, so
          // deliver the already-authored FOG:HOG extent now and preserve the
          // retail completion callback as the next guest continuation.
          pending_archive_callback_return_ = context.returnAddress();
          context.setRegister(4U, context.argument(3U));
          context.setRegister(31U, callback);
          context.setReturnValue(0U);
        });
    vm_.bindHostCall(
        0x800afd6cU, [this](LegacyHostCallContext &context) {
          if (pending_archive_callback_return_ != 0U) {
            context.setRegister(31U, pending_archive_callback_return_);
            pending_archive_callback_return_ = 0U;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80166728U, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t source123_physics = 0x801a70a4U;
          if (mission_index_ == 4U &&
              context.argument(1U) == source123_physics) {
            ++airbasex_attachment_link_calls_;
            for (auto index = std::size_t{};
                 index < airbasex_attachment_link_arguments_.size();
                 ++index) {
              airbasex_attachment_link_arguments_[index] =
                  context.argument(static_cast<std::uint32_t>(index));
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x801058acU, [this](LegacyHostCallContext &context) {
          // Retail SpuSetVoicePitch accepts one of the PS1's 24 hardware
          // voices. A released sound-bank allocation can remain linked long
          // enough for the sequence player to reuse and overwrite its
          // variant record; LABS2 then presents 0xD0 here. The original
          // routine would address beyond the SPU MMIO bank. Reject only that
          // impossible hardware index and leave valid retail updates intact.
          constexpr std::uint32_t spu_voice_count = 24U;
          const auto voice = context.argument(0U);
          if (voice >= spu_voice_count) {
            ++rejected_sound_voice_updates_;
            last_rejected_sound_voice_ = voice;
            last_rejected_sound_voice_caller_ =
                context.registerValue(31U);
            context.setReturnValue(0U);
            return;
          }
          context.continueGuestInstruction();
        });
    // The guest's synchronous drain spins inside one emulated instruction
    // slice, while the host CD device advances between slices. Leave queued
    // requests with the retail asynchronous owner instead of deadlocking the
    // application-state transition.
    vm_.bindHostCall(0x800a3ed0U,
                     [](LegacyHostCallContext &context) {
                       context.setReturnValue(0U);
                     });
    vm_.bindHostCall(
        0x800ad48cU, [this](LegacyHostCallContext &context) {
          ++retail_checkpoint_capture_calls_;
          retail_checkpoint_capture_pending_ = true;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800ad9f4U, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t checkpoint_present = 0x8011f61cU;
          std::uint32_t present{};
          if (!context.read32(checkpoint_present, present) || present == 0U ||
              !checkpoint_captured_ || !checkpointStateReady()) {
            // Retail distinguishes Restart Mission from Restart at Last
            // Checkpoint by clearing this flag. A death before the first real
            // checkpoint has the same clean-restart ownership. Yield before
            // entering the restore loader so the host can reconstruct only
            // the active mission package (no title, SOL, or briefing replay).
            mission_restart_requested_ = true;
            context.setReturnValue(0U);
            context.yieldAfterHostCall();
            return;
          }
          last_restore_caller_ = context.registerValue(31U);
          std::uint32_t player{};
          std::uint16_t armor{};
          readPlayerState(player, pre_restore_player_x_,
                          pre_restore_player_y_, pre_restore_player_z_,
                          pre_restore_player_health_, armor);
          ++alpha_checkpoint_restores_;
          retail_restore_active_ = true;
          retail_restore_start_frame_ = guest_frame_;
          // A checkpoint restore owns the mission file loader. If death or
          // failure interrupted speech, abandon the gameplay-XA mount before
          // retail starts issuing relative FOG reads.
          setXaAbsoluteDiscActive(false);
          // The corrected CD/XA scheduler can now service the retail
          // checkpoint loader. Preserve this boundary as an observation hook
          // and execute the original restore instead of rewinding a host
          // whole-machine snapshot from an unrelated instruction boundary.
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002c8a0U, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t menu_restart_mission_caller = 0x80143654U;
          if (context.registerValue(31U) == menu_restart_mission_caller) {
            // MENU.OVL's Restart Mission callback tears down the current
            // menu state before this call and then immediately reopens its
            // retail mission index. The architectural return address is the
            // authored discriminator; application state is intentionally no
            // longer 7 here. The outer native campaign host owns the package
            // boundary, so yield before mission teardown and reconstruct the
            // same mission without replaying SOL or the briefing.
            mission_restart_requested_ = true;
            context.setReturnValue(0U);
            context.yieldAfterHostCall();
            return;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002d6d8U, [this](LegacyHostCallContext &context) {
          ++mission_success_events_;
          mission_success_pending_ = true;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002da38U, [this](LegacyHostCallContext &context) {
          ++mission_failure_events_;
          // Both outcomes later request application state 3. A failure must
          // explicitly cancel a pending success before that shared handoff.
          mission_success_pending_ = false;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80143568U, [this](LegacyHostCallContext &context) {
          // MENU.OVL invokes this callback only after presenting the authored
          // Save and Quit confirmation. Argument zero is the selected answer:
          // zero returns to MENU, while one transfers ownership to the
          // memory-card frontend. Some live overlay paths wait before reaching
          // the later shared application transition, so hand the affirmative
          // result directly to the native campaign/save owner at this first
          // unambiguous retail boundary. The negative path remains retail.
          std::uint32_t application_state{};
          const auto menu_overlay_active =
              context.read32(profile_.application_state, application_state) &&
              application_state == 7U;
          if (menu_overlay_active && context.argument(0U) != 0U) {
            quit_to_title_requested_ = true;
            context.setReturnValue(0U);
            context.yieldAfterHostCall();
            return;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002d8d0U, [this](LegacyHostCallContext &context) {
          if (mission_success_pending_) {
            mission_complete_requested_ = true;
            mission_success_pending_ = false;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002c95cU, [this](LegacyHostCallContext &context) {
          ++campaign_advance_calls_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80154054U, [this](LegacyHostCallContext &context) {
          ++movie_request_calls_;
          for (auto index = std::size_t{};
               index < last_movie_request_arguments_.size(); ++index) {
            last_movie_request_arguments_[index] = context.argument(index);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80142e60U, [this](LegacyHostCallContext &context) {
          ++movie_playback_init_calls_;
          for (auto index = std::size_t{};
               index < last_movie_playback_arguments_.size(); ++index) {
            last_movie_playback_arguments_[index] = context.argument(index);
          }
          constexpr auto movie_catalog_begin = 0x801279d0U;
          constexpr auto movie_catalog_end_begin = 0x80127a3cU;
          constexpr auto movie_catalog_capacity = 27U;
          selected_movie_catalog_index_ = 0xffffffffU;
          for (auto index = 0U; index < movie_catalog_capacity; ++index) {
            std::uint32_t begin{};
            std::uint32_t end{};
            if (context.read32(movie_catalog_begin + index * 4U, begin) &&
                context.read32(movie_catalog_end_begin + index * 4U, end) &&
                begin == last_movie_playback_arguments_[0U] &&
                end == last_movie_playback_arguments_[1U]) {
              selected_movie_catalog_index_ = index;
              break;
            }
          }
          const auto history_slot = static_cast<std::size_t>(
              (movie_playback_init_calls_ - 1U) %
              movie_playback_catalog_history_.size());
          movie_playback_catalog_history_[history_slot] =
              selected_movie_catalog_index_;
          const auto mapped = missionScriptedMovieCatalogIndices(
              GameId::syphon_filter_2, mission_index_);
          const auto mapped_movie =
              selected_movie_catalog_index_ <= 0xffU &&
              std::ranges::find(
                  mapped,
                  static_cast<std::uint8_t>(selected_movie_catalog_index_)) !=
                  mapped.end();
          if (selected_movie_catalog_index_ <= 0xffU &&
              (mapped_movie || completion_flow_trace_active_)) {
            const auto catalog_index =
                static_cast<std::uint8_t>(selected_movie_catalog_index_);
            // Retail has already copied the full request, pushed state 9,
            // restored the prior state at MovieLoader, and prepared its
            // completion callback. Substitute only the decoder itself; the
            // host presents the exact embedded STR, then invokes retail's
            // normal MovieCompletion routine. The completion-flow probe also
            // accepts an unmapped catalog index here solely to discover the
            // exact retail-selected inter-mission movies; product playback
            // remains restricted to the active mission's checked mapping.
            if (!pending_scripted_movie_catalog_index_ &&
                !active_scripted_movie_catalog_index_) {
              pending_scripted_movie_catalog_index_ = catalog_index;
              ++scripted_movie_handoffs_;
              last_scripted_movie_catalog_index_ = catalog_index;
            }
            context.setReturnValue(0U);
            context.yieldAfterHostCall();
            return;
          }
          context.continueGuestInstruction();
        });
    // Observe retail actor-damage dispatch without replacing it. This gives
    // the product probe enough evidence to distinguish scripted restart from
    // hostile-fire damage during the authored opening.
    vm_.bindHostCall(
        0x80082510U, [this](LegacyHostCallContext &context) {
          last_damage_caller_ = context.registerValue(31U);
          for (auto index = std::size_t{}; index < last_damage_arguments_.size();
               ++index) {
            last_damage_arguments_[index] =
                context.registerValue(static_cast<std::uint8_t>(4U + index));
          }
          ++damage_events_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8002e6e8U, [this](LegacyHostCallContext &context) {
          ++objective_completion_events_;
          last_objective_completion_index_ = context.argument(0U);
          last_objective_completion_text_ =
              static_cast<std::int32_t>(context.argument(1U));
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800abc48U, [this](LegacyHostCallContext &context) {
          ++pickup_presentation_events_;
          last_pickup_actor_ = context.argument(0U);
          last_pickup_text_ = context.argument(1U);
          last_pickup_item_ = context.argument(2U);
          for (auto index = std::size_t{};
               index < last_pickup_text_words_.size(); ++index) {
            static_cast<void>(context.read32(
                last_pickup_text_ + static_cast<std::uint32_t>(index * 4U),
                last_pickup_text_words_[index]));
          }
          last_pickup_text_bytes_.fill('\0');
          for (auto index = std::size_t{};
               index + 1U < last_pickup_text_bytes_.size(); ++index) {
            std::uint8_t value{};
            if (!context.read8(last_pickup_text_ +
                                   static_cast<std::uint32_t>(index),
                               value) ||
                value == 0U) {
              break;
            }
            last_pickup_text_bytes_[index] = static_cast<char>(value);
          }
          context.continueGuestInstruction();
        });
    // SF2's text system is not layout-compatible with SF1: handles resolve
    // through the 64-entry pool at 0x80137B04 and each glyph is 0x10 bytes.
    // Observe the shared create/update/remove boundaries so native
    // presentation receives the exact retail-authored string regardless of
    // whether it originated in mission bytecode, an objective, or a pickup.
    vm_.bindHostCall(
        0x800a6b7cU, [this](LegacyHostCallContext &context) {
          recordUiTextEvent(Sf2GuestUiTextEventKind::create, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a8068U, [this](LegacyHostCallContext &context) {
          recordUiTextEvent(Sf2GuestUiTextEventKind::update, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a716cU, [this](LegacyHostCallContext &context) {
          recordUiTextEvent(Sf2GuestUiTextEventKind::remove, context);
          context.continueGuestInstruction();
        });
    // Observe invocations of renderer objects which own a manual 0x66 glyph
    // list at +0x94. This catches the HEALTH producer at the routine entry
    // while remaining callable through the VM's normal host-call boundary.
    vm_.bindHostCall(
        0x80013040U, [this](LegacyHostCallContext &context) {
          const auto renderer = context.argument(0U);
          const auto global_pointer = context.registerValue(28U);
          if (renderer != 0U) {
            std::array<std::uint32_t, 3U> lists{};
            static_cast<void>(context.read32(renderer + 0x90U, lists[0U]));
            static_cast<void>(context.read32(renderer + 0x94U, lists[1U]));
            static_cast<void>(context.read32(renderer + 0x98U, lists[2U]));
            if (lists[1U] == 0U) {
              context.continueGuestInstruction();
              return;
            }
            std::uint16_t build_buffer{};
            static_cast<void>(
                context.read16(global_pointer + 0x2aU, build_buffer));
            if (build_buffer < text_renderer_calls_.size()) {
              ++text_renderer_calls_[build_buffer];
            }
            text_renderer_ = renderer;
            static_cast<void>(context.read16(renderer + 0x06U,
                                             text_renderer_flags_));
            text_renderer_list_heads_ = lists;
          }
          context.continueGuestInstruction();
        });
    // Track the retail callers which register primitives from the small
    // mission-HUD allocation immediately preceding the radar packets.  This
    // is observation-only: it identifies the real producer of the six
    // HEALTH glyph sprites that currently populate only one display page.
    vm_.bindHostCall(
        0x80013000U, [this](LegacyHostCallContext &context) {
          const auto packet = context.argument(1U);
          if (packet >= 0x8014f000U && packet < 0x80169020U) {
            const auto caller = context.returnAddress();
            ++hud_primitive_registrations_;
            auto slot = hud_primitive_registration_callers_.size();
            for (auto index = std::size_t{};
                 index < hud_primitive_registration_callers_.size();
                 ++index) {
              if (hud_primitive_registration_callers_[index] == caller ||
                  hud_primitive_registration_callers_[index] == 0U) {
                slot = index;
                break;
              }
            }
            if (slot < hud_primitive_registration_callers_.size()) {
              hud_primitive_registration_callers_[slot] = caller;
              hud_primitive_registration_packets_[slot] = packet;
              hud_primitive_registration_roots_[slot] = context.argument(0U);
              ++hud_primitive_registration_counts_[slot];
              std::uint16_t build_buffer{};
              static_cast<void>(context.read16(
                  context.registerValue(28U) + 0x2aU, build_buffer));
              if (build_buffer < 8U) {
                hud_primitive_registration_buffer_masks_[slot] |=
                    static_cast<std::uint8_t>(1U << build_buffer);
              }
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80013354U, [](LegacyHostCallContext &context) {
          // The retained auxiliary source primitives live at
          // 0x80168AE8..0x80169778. Retail's odd-page packet cursor begins at
          // 0x801689FC, immediately below that pool. Once combat HUD and text
          // increase the copied packet volume, the output overtakes unread
          // source records and turns their DMA lengths into GP0 E1 words.
          //
          // Native presentation snapshots every completed OT before the next
          // retail tick, so both auxiliary pages can safely use the retired
          // even-page packet workspace. Keep the odd-page glyph packets at
          // their authored addresses, but relocate the general-list copy
          // cursor before the renderer loads it at this instruction.
          constexpr std::uint16_t odd_auxiliary_table = 3U;
          constexpr std::uint32_t auxiliary_renderer = 0x80120ae0U;
          constexpr std::uint32_t packet_cursor = 0x8013e6dcU;
          constexpr std::uint32_t safe_packet_workspace = 0x8014fc5cU;
          std::uint16_t table_index{};
          if (context.registerValue(19U) == auxiliary_renderer &&
              context.read16(context.registerValue(28U) + 0x2aU,
                             table_index) &&
              table_index == odd_auxiliary_table) {
            static_cast<void>(
                context.write32(packet_cursor, safe_packet_workspace));
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80078c24U, [this](LegacyHostCallContext &context) {
          const auto request = context.argument(0U);
          if (mission_index_ == 4U) {
            const auto homan = objectStateForProbe(123U);
            if (homan && context.registerValue(17U) == homan->instance) {
              ++airbasex_actor_collision_request_count_;
              airbasex_actor_collision_request_caller_ =
                  context.returnAddress();
              static_cast<void>(context.read32(
                  request + 0x08U,
                  airbasex_actor_collision_request_object_));
              static_cast<void>(context.read32(
                  request + 0x0cU,
                  airbasex_actor_collision_request_room_));
            }
          }
          std::uint16_t event{};
          if (context.read16(request, event) && event == 12U) {
            last_player_damage_caller_ = context.registerValue(31U);
            for (auto index = std::size_t{};
                 index < last_player_damage_request_.size(); ++index) {
              static_cast<void>(context.read32(
                  request + static_cast<std::uint32_t>(index * 4U),
                  last_player_damage_request_[index]));
            }
            ++player_damage_events_;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80042cf4U, [this](LegacyHostCallContext &context) {
          // Retail's opening-transfer events create resources and attachments
          // which are not represented by a baseline captured afterward. Keep
          // those packages checkpoint-free until the authored serializer runs.
          // The policy follows event semantics rather than mission identity:
          // 0x72 transfers parachute ownership; 0x62 starts an authored opening
          // vehicle/actor sequence (including TRAIN's helicopter flyby).
          const auto event_id = context.argument(1U);
          if (!checkpoint_captured_ &&
              (event_id == 0x62U || event_id == 0x72U)) {
            initial_checkpoint_deferred_by_opening_event_ = true;
          }
          auto &event = object_event_dispatches_[
              object_event_dispatch_count_ % object_event_dispatches_.size()];
          event.guest_frame = guest_frame_;
          for (auto index = std::size_t{}; index < event.arguments.size();
               ++index) {
            event.arguments[index] =
                context.argument(static_cast<std::uint32_t>(index));
          }
          ++object_event_dispatch_count_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80086d4cU, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U) {
            auto &event = actor_activations_[
                actor_activation_count_ % actor_activations_.size()];
            event = Sf2GuestActorActivationEvent{
                .guest_frame = guest_frame_,
                .instance = context.argument(0U),
                .return_address = context.returnAddress(),
            };
            ++actor_activation_count_;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80067830U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U || mission_index_ == 2U) {
            const auto source = objectStateForProbe(
                mission_index_ == 4U ? 122U : 45U);
            if (source && context.argument(0U) == source->instance &&
                source->render_node != 0U) {
              auto &event = airbasex_motion_updates_[
                  airbasex_motion_update_count_ %
                  airbasex_motion_updates_.size()];
              event.guest_frame = guest_frame_;
              event.caller = context.returnAddress();
              event.driver = context.registerValue(19U);
              event.driver_state = context.registerValue(18U);
              for (auto index = std::size_t{};
                   index < event.control_words.size(); ++index) {
                static_cast<void>(context.read32(
                    source->render_node + 0x100U +
                        static_cast<std::uint32_t>(index * 4U),
                    event.control_words[index]));
              }
              for (auto index = std::size_t{};
                   index < event.driver_state_words.size(); ++index) {
                static_cast<void>(context.read32(
                    event.driver_state + 0x78U +
                        static_cast<std::uint32_t>(index * 4U),
                    event.driver_state_words[index]));
              }
              event.outer_arguments = {
                  context.registerValue(20U), context.registerValue(21U),
                  context.registerValue(23U)};
              event.mode = context.argument(1U);
              event.flags = context.argument(2U);
              for (auto component = std::size_t{}; component < 3U;
                   ++component) {
                std::uint32_t position{};
                std::uint32_t velocity{};
                static_cast<void>(context.read32(
                    source->render_node + 0x40U +
                        static_cast<std::uint32_t>(component * 4U),
                    position));
                static_cast<void>(context.read32(
                    source->render_node + 0x50U +
                        static_cast<std::uint32_t>(component * 4U),
                    velocity));
                event.position[component] =
                    std::bit_cast<std::int32_t>(position);
                event.velocity[component] =
                    std::bit_cast<std::int32_t>(velocity);
              }
              ++airbasex_motion_update_count_;
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80066cd4U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U || mission_index_ == 2U) {
            const auto homan = objectStateForProbe(123U);
            const auto chance = objectStateForProbe(45U);
            const auto instance = context.argument(0U);
            if ((mission_index_ == 4U && homan &&
                 instance == homan->instance) ||
                (mission_index_ == 2U && chance &&
                 instance == chance->instance)) {
              airbasex_bounds_update_caller_ = context.returnAddress();
              airbasex_bounds_update_instance_ = instance;
              for (auto index = std::size_t{};
                   index < airbasex_bounds_instance_words_.size(); ++index) {
                static_cast<void>(context.read32(
                    instance + static_cast<std::uint32_t>(index * 4U),
                    airbasex_bounds_instance_words_[index]));
              }
              const auto physics = airbasex_bounds_instance_words_[2U];
              for (auto index = std::size_t{};
                   physics != 0U &&
                   index < airbasex_bounds_physics_words_.size(); ++index) {
                static_cast<void>(context.read32(
                    physics + static_cast<std::uint32_t>(index * 4U),
                    airbasex_bounds_physics_words_[index]));
              }
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80066f18U, [this](LegacyHostCallContext &context) {
          if ((mission_index_ == 4U || mission_index_ == 2U) &&
              context.registerValue(18U) ==
                  airbasex_bounds_update_instance_) {
            const auto instance = context.registerValue(18U);
            std::uint16_t minimum_y{};
            std::uint32_t motion{};
            if (context.read32(instance + 0x0cU, motion) && motion != 0U &&
                context.read16(motion + 0x10aU, minimum_y)) {
              airbasex_bounds_minimum_y_ =
                  std::bit_cast<std::int16_t>(minimum_y);
            }
            ++airbasex_bounds_update_count_;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80092aa0U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U || mission_index_ == 2U) {
            const auto source = objectStateForProbe(
                mission_index_ == 4U ? 122U : 45U);
            const auto instance = context.registerValue(18U);
            if (source && instance == source->instance) {
              auto &event = actor_collision_responses_[
                  actor_collision_response_count_ %
                  actor_collision_responses_.size()];
              event = {};
              event.guest_frame = guest_frame_;
              event.instance = instance;
              event.position_lookup = last_actor_position_lookup_;
              event.motion = source->render_node;
              static_cast<void>(context.read32(source->render_node + 0x158U,
                                               event.driver_state));
              const auto stack = context.registerValue(29U);
              static_cast<void>(
                  context.read32(stack + 0x140U, event.pipeline_caller));
              static_cast<void>(context.read32(stack + 0x00U, event.score));
              const auto read_signed_words =
                  [&context, stack](std::uint32_t offset, auto &words) {
                    for (auto index = std::size_t{}; index < words.size();
                         ++index) {
                      std::uint32_t word{};
                      static_cast<void>(context.read32(
                          stack + offset +
                              static_cast<std::uint32_t>(index * 4U),
                          word));
                      words[index] = std::bit_cast<std::int32_t>(word);
                    }
                  };
              read_signed_words(0x40U, event.response);
              read_signed_words(0x20U, event.contact_state);
              read_signed_words(0x30U, event.velocity);
              read_signed_words(0x50U, event.contact_delta);
              const auto pipeline_stack = stack + 0x148U;
              const auto read_pipeline_words =
                  [&context, pipeline_stack](std::uint32_t offset,
                                             auto &words) {
                    for (auto index = std::size_t{}; index < words.size();
                         ++index) {
                      std::uint32_t word{};
                      static_cast<void>(context.read32(
                          pipeline_stack + offset +
                              static_cast<std::uint32_t>(index * 4U),
                          word));
                      words[index] = std::bit_cast<std::int32_t>(word);
                    }
                  };
              read_pipeline_words(0x18U, event.root_point);
              read_pipeline_words(0x28U, event.reference_point);
              ++actor_collision_response_count_;
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800933e0U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U || mission_index_ == 2U) {
            const auto source = objectStateForProbe(
                mission_index_ == 4U ? 122U : 45U);
            if (source && context.registerValue(17U) == source->instance) {
              last_actor_position_lookup_ = context.registerValue(2U);
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80066b54U, [this](LegacyHostCallContext &context) {
          ++render_view_adds_;
          last_render_view_added_ = context.argument(0U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80066be0U, [this](LegacyHostCallContext &context) {
          ++render_view_removes_;
          last_render_view_removed_ = context.argument(0U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80164fdcU, [this](LegacyHostCallContext &context) {
          ++script_archive_loads_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b3d34U, [this](LegacyHostCallContext &context) {
          // 0x8011f638 is the retail Hard-difficulty predicate, not a New Game
          // latch. The direct bootstrap already inherits TITLE's Normal
          // default; writing one here silently selected Hard mission branches
          // (including Falkan's shortened helicopter escape).
          if (script_level_starts_ != 0U && !checkpoint_captured_ &&
              initial_checkpoint_deferred_by_opening_event_) {
            // A second LEVEL start in a generation which could not safely
            // capture its frontend baseline would retain one-shot opening
            // flags and resource ownership. Yield before ResetAndStartLevel so
            // the outer host can construct the same package from a clean guest
            // generation instead.
            mission_restart_requested_ = true;
            context.setReturnValue(0U);
            context.yieldAfterHostCall();
            return;
          }
          ++script_level_starts_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b38b0U, [this](LegacyHostCallContext &context) {
          ++script_dispatches_;
          for (auto index = std::size_t{};
               index < last_script_dispatch_arguments_.size(); ++index) {
            last_script_dispatch_arguments_[index] =
                context.argument(static_cast<std::uint32_t>(index));
          }
          if (last_script_dispatch_arguments_[0U] != 0U) {
            auto &event = script_dispatch_events_[
                script_dispatch_event_count_ % script_dispatch_events_.size()];
            event = Sf2GuestScriptDispatchEvent{
                .guest_frame = guest_frame_,
                .event = last_script_dispatch_arguments_[0U],
                .selector = last_script_dispatch_arguments_[1U],
            };
            ++script_dispatch_event_count_;
          }
          if (last_script_dispatch_arguments_[0U] == 5U) {
            ++script_event5_dispatches_;
            recordTimelineEvent(
                Sf2GuestTimelineEventKind::script_event5, context);
          }
          context.continueGuestInstruction();
        });
    // Observe the FIRST_ZONE predicate/action surface without replacing
    // retail execution. Mission 5's absent opening actors are selected by
    // these handlers; retaining their resolved arguments makes the authored
    // branch independently reproducible instead of guessing from visuals.
    constexpr std::array airbasex_script_handlers{
        0x800aff4cU, // action 0x79: NPC resource lifecycle
        0x800b03bcU, // predicate 0x04: program variable equals
        0x800b07a4U, // predicate 0x16: object node flag
        0x800b0a14U, // predicate 0x1e: Hard difficulty
        0x800b0c30U, // action 0x06: clear scripted inactive bit
        0x800b0e20U, // action 0x42: actor activation/state
        0x800b1030U, // action 0x13: actor relationship
        0x800b1320U, // action 0x21
        0x800b1588U, // action 0x19: actor state
        0x800b1644U, // action 0x56
        0x800b22a4U, // action/predicate 0x01/0x31: variable assignment
    };
    for (const auto handler : airbasex_script_handlers) {
      vm_.bindHostCall(
          handler, [this, handler](LegacyHostCallContext &context) {
            if (mission_index_ == 4U) {
              if (handler == 0x800b0a14U) {
                ++airbasex_hard_difficulty_reads_;
                static_cast<void>(context.read8(
                    0x8011f638U, airbasex_hard_difficulty_));
              }
              auto &event = script_handler_events_[
                  script_handler_event_count_ %
                  script_handler_events_.size()];
              event.guest_frame = guest_frame_;
              event.handler = handler;
              event.return_address = context.returnAddress();
              event.result = 0U;
              event.has_result = false;
              for (auto index = std::size_t{};
                   index < event.arguments.size(); ++index) {
                event.arguments[index] =
                    context.argument(static_cast<std::uint32_t>(index));
              }
              if (event.return_address == 0x800b2d14U) {
                pending_script_predicate_event_ =
                    script_handler_event_count_ %
                    script_handler_events_.size();
              }
              ++script_handler_event_count_;
            }
            context.continueGuestInstruction();
        });
    }
    vm_.bindHostCall(
        0x800b0c84U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U) {
            const auto source = static_cast<std::uint16_t>(
                context.argument(0U) & 0xffffU);
            if (source == 148U || source == 149U) {
              airbasex_actor_removal_source_ = source;
              if (const auto state = objectStateForProbe(source)) {
                airbasex_actor_target_before_ = state->target;
                airbasex_actor_target_word_before_ =
                    state->target_words[0U];
              }
              ++airbasex_actor_removal_count_;
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b0cbcU, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U &&
              (airbasex_actor_removal_source_ == 148U ||
               airbasex_actor_removal_source_ == 149U)) {
            if (const auto state =
                    objectStateForProbe(airbasex_actor_removal_source_)) {
              airbasex_actor_target_after_ = state->target;
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b2d14U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U && pending_script_predicate_event_) {
            auto &event = script_handler_events_[
                *pending_script_predicate_event_];
            event.result = context.registerValue(2U);
            event.has_result = true;
            pending_script_predicate_event_.reset();
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800570a0U, [this](LegacyHostCallContext &context) {
          // HWAY can transiently publish 0xFFFF before execution returns to
          // a public display boundary. Apply its captured owner-room repair
          // at the collision call edge as well as at that boundary. This
          // intentionally does not classify or rewrite individual requests:
          // the observed return site is shared actor logic.
          stabilizeCollisionRoom();
          ++world_collision_scans_;
          last_world_collision_caller_ = context.returnAddress();
          const auto request = context.argument(0U);
          // 0x80084E7C is shared actor logic, so the return address alone is
          // not a player-floor identity. The caller iterates s1 over actors
          // while retaining Gabe in s0; its own branch at 0x80084E18 treats
          // s1 == s0 as the player case. Repair only that exact request.
          const auto player_request =
              context.returnAddress() == 0x80084e7cU &&
              context.registerValue(16U) != 0U &&
              context.registerValue(17U) ==
                  context.registerValue(16U);
          if (player_request) {
            stabilizePlayerCollisionRequest(context, request);
          }
          std::uint32_t object{};
          std::uint32_t room{};
          if (context.read32(request + 0x08U, object)) {
            last_world_collision_object_ =
                static_cast<std::int32_t>(object);
          }
          if (context.read32(request + 0x0cU, room)) {
            last_world_collision_room_ = static_cast<std::int32_t>(room);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80011028U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U) {
            const auto destination = context.registerValue(7U);
            const auto source = context.registerValue(6U);
            const auto state = objectStateForProbe(123U);
            if (state && state->matrix == destination) {
              std::uint32_t y{};
              ++airbasex_source123_matrix_copies_;
              airbasex_source123_matrix_copy_source_ = source;
              airbasex_source123_matrix_copy_caller_ =
                  context.returnAddress();
              if (context.read32(source + 0x18U, y)) {
                airbasex_source123_matrix_copy_y_ =
                    std::bit_cast<std::int32_t>(y);
              }
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80025188U, [this](LegacyHostCallContext &context) {
          if (mission_index_ == 4U) {
            const auto state = objectStateForProbe(123U);
            if (state && state->actor_controller + 0x1cU ==
                             context.argument(0U)) {
              (void)context.read32(context.registerValue(29U) + 0x34U,
                                   airbasex_source123_local_writer_caller_);
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80013000U, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t first_renderer_add_prim_return =
              0x8001354cU;
          constexpr std::uint32_t second_renderer_add_prim_return =
              0x800135c0U;
          const auto caller = context.returnAddress();
          if (caller != first_renderer_add_prim_return &&
              caller != second_renderer_add_prim_return) {
            context.continueGuestInstruction();
            return;
          }
          const auto requested = context.argument(0U);
          const auto bucket_count = context.registerValue(22U);
          std::uint16_t table_index{};
          std::uint32_t base{};
          const auto gp = context.registerValue(28U);
          const auto valid =
              bucket_count != 0U &&
              bucket_count <= 0x10000U &&
              context.read16(gp + 0x2aU, table_index) &&
              context.read32(
                  0x80120514U +
                      static_cast<std::uint32_t>(table_index) * 20U,
                  base) &&
              base >= 0x80000000U && base < 0x80200000U;
          const auto span =
              valid ? static_cast<std::uint64_t>(bucket_count) * 4U : 0U;
          const auto requested_in_table =
              valid &&
              static_cast<std::uint64_t>(requested) >= base &&
              static_cast<std::uint64_t>(requested) <
                  static_cast<std::uint64_t>(base) + span &&
              (requested & 3U) == 0U;
          if (valid && !requested_in_table) {
            const auto clamped =
                base + (bucket_count - 1U) * 4U;
            context.setRegister(4U, clamped);
            ++clamped_renderer_ordering_table_entries_;
            last_renderer_ordering_table_requested_ = requested;
            last_renderer_ordering_table_clamped_ = clamped;
            last_renderer_ordering_table_base_ = base;
            last_renderer_ordering_table_buckets_ = bucket_count;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f4b00U, [this](LegacyHostCallContext &context) {
          // Retail merges a per-object packet list into a display OT here.
          // The packet links are 24-bit physical RAM addresses terminated by
          // 0x00ffffff. A poisoned/freed list (observed as 0x005a5a5a after
          // long play and quick-load) otherwise faults at 0x800f4b54. Reject
          // only the malformed object list; valid retail merges still run
          // entirely in guest code.
          constexpr std::uint32_t dma_end = 0x00ffffffU;
          constexpr std::uint32_t ram_size =
              static_cast<std::uint32_t>(psx::R3000Runtime::ram_size);
          constexpr std::size_t maximum_nodes = 65'536U;
          const auto descriptor = context.argument(0U);
          std::uint32_t root{};
          auto cursor = std::uint32_t{};
          auto tag = std::uint32_t{};
          auto valid = context.read32(descriptor + 4U, root);
          cursor = root & dma_end;
          for (auto node = std::size_t{}; valid && node < maximum_nodes;
               ++node) {
            if (cursor == dma_end) {
              break;
            }
            if ((cursor & 3U) != 0U || cursor >= ram_size ||
                !context.read32(cursor, tag)) {
              valid = false;
              break;
            }
            cursor = tag & dma_end;
            if (node + 1U == maximum_nodes && cursor != dma_end) {
              valid = false;
            }
          }
          if (!valid) {
            ++rejected_renderer_list_merges_;
            last_rejected_renderer_list_descriptor_ = descriptor;
            last_rejected_renderer_list_root_ = root;
            last_rejected_renderer_list_cursor_ = cursor;
            last_rejected_renderer_list_tag_ = tag;
            // The retail return value is the destination descriptor (a1).
            // Skipping one corrupt object's list is the narrowest recoverable
            // behavior and avoids mutating the destination OT.
            context.setReturnValue(context.argument(1U));
            return;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a21b4U, [this](LegacyHostCallContext &context) {
          ++room_texture_activations_;
          last_room_texture_activation_ = context.argument(0U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a212cU, [this](LegacyHostCallContext &context) {
          ++room_texture_page_requests_;
          last_room_texture_page_ = context.argument(0U);
          last_room_texture_bank_ = context.argument(1U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a1b44U, [this](LegacyHostCallContext &context) {
          ++room_texture_upload_completions_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f1598U, [this](LegacyHostCallContext &context) {
          constexpr std::array load_image_call_sites{
              0x800257c4U, 0x80025818U, 0x800a1c40U, 0x800c2334U,
              0x800c29f4U, 0x801c11c8U, 0x801c1260U, 0x801c9a24U,
              0x801cbe68U, 0x801ceb9cU,
          };
          ++retail_load_image_calls_;
          const auto caller = context.returnAddress() - 8U;
          last_retail_load_image_caller_ = caller;
          const auto caller_entry =
              std::ranges::find(load_image_call_sites, caller);
          if (caller_entry == load_image_call_sites.end()) {
            ++unknown_retail_load_image_call_sites_;
          } else {
            ++retail_load_image_call_sites_[
                static_cast<std::size_t>(
                    caller_entry - load_image_call_sites.begin())];
          }
          const auto rectangle = context.argument(0U);
          const auto pixels = context.argument(1U);
          std::uint16_t x{};
          std::uint16_t y{};
          std::uint16_t width{};
          std::uint16_t height{};
          if (!context.read16(rectangle, x) ||
              !context.read16(rectangle + 2U, y) ||
              !context.read16(rectangle + 4U, width) ||
              !context.read16(rectangle + 6U, height) ||
              width == 0U || height == 0U ||
              x >= 1024U || y >= 512U ||
              static_cast<std::uint32_t>(x) + width > 1024U ||
              static_cast<std::uint32_t>(y) + height > 512U) {
            context.continueGuestInstruction();
            return;
          }
          const auto halfwords =
              static_cast<std::size_t>(width) * height;
          last_retail_load_image_transfer_ = Sf2GpuTransfer{
              .x = x,
              .y = y,
              .width = width,
              .height = height,
          };
          const auto payload_bytes = halfwords * sizeof(std::uint16_t);
          if (pixels >
              std::numeric_limits<std::uint32_t>::max() -
                  payload_bytes) {
            context.continueGuestInstruction();
            return;
          }
          Sf2GpuPacket upload;
          upload.guest_address = caller;
          upload.gp0_words.reserve(3U + (halfwords + 1U) / 2U);
          upload.gp0_words.push_back(0xa0000000U);
          upload.gp0_words.push_back(
              static_cast<std::uint32_t>(x) |
              (static_cast<std::uint32_t>(y) << 16U));
          upload.gp0_words.push_back(
              static_cast<std::uint32_t>(width) |
              (static_cast<std::uint32_t>(height) << 16U));
          auto readable = true;
          for (auto index = std::size_t{}; index < halfwords;
               index += 2U) {
            std::uint16_t low{};
            std::uint16_t high{};
            readable = context.read16(
                pixels + static_cast<std::uint32_t>(index * 2U), low);
            if (readable && index + 1U < halfwords) {
              readable = context.read16(
                  pixels +
                      static_cast<std::uint32_t>((index + 1U) * 2U),
                  high);
            }
            if (!readable) {
              break;
            }
            upload.gp0_words.push_back(
                static_cast<std::uint32_t>(low) |
                (static_cast<std::uint32_t>(high) << 16U));
          }
          if (readable) {
            rememberVramSetupPacket(std::move(upload));
            ++retained_retail_load_images_;
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8001dc50U, [this](LegacyHostCallContext &context) {
          // The large resident world renderer walks a sentinel-terminated
          // array of vertex-pair pointers here. Mission 3's truck/cutscene
          // path has produced values such as 0xFFFF0823; the unmodified
          // renderer later executes LH at 0x8001DC84 and faults before an OT
          // can be published. Validate exactly the halfwords consumed by
          // that loop and turn only unreadable entries into retail's -1
          // terminator, dropping the malformed tail for this object/frame.
          const auto cursor = context.registerValue(9U);
          std::uint32_t candidate{};
          if (context.read32(cursor + 4U, candidate) &&
              candidate != 0xffffffffU) {
            std::uint16_t value{};
            const auto readable =
                (candidate & 1U) == 0U &&
                context.read16(candidate + 0x08U, value) &&
                context.read16(candidate + 0x0aU, value) &&
                context.read16(candidate + 0x0cU, value) &&
                context.read16(candidate + 0x0eU, value) &&
                context.read16(candidate + 0x10U, value) &&
                context.read16(candidate + 0x12U, value);
            if (!readable && context.write32(cursor + 4U, 0xffffffffU)) {
              ++rejected_renderer_vertex_entries_;
              last_rejected_renderer_vertex_cursor_ = cursor + 4U;
              last_rejected_renderer_vertex_address_ = candidate;
            }
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b36a0U, [this](LegacyHostCallContext &context) {
          ++script_program_dispatches_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b39e4U, [this](LegacyHostCallContext &context) {
          ++script_activations_;
          last_script_activation_program_ = context.argument(0U);
          if (std::ranges::find(active_script_programs_,
                                last_script_activation_program_) ==
              active_script_programs_.end()) {
            active_script_programs_.push_back(
                last_script_activation_program_);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b3a30U, [this](LegacyHostCallContext &context) {
          std::erase(active_script_programs_, context.argument(0U));
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b3a84U, [this](LegacyHostCallContext &context) {
          active_script_programs_.clear();
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008cafcU, [this](LegacyHostCallContext &context) {
          ++scene_xa_archive_opens_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008cdd0U, [this](LegacyHostCallContext &context) {
          ++spatial_sound_starts_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008d21cU, [this](LegacyHostCallContext &context) {
          ++scene_sound_cue_plays_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008dbe4U, [this](LegacyHostCallContext &context) {
          ++scene_speech_starts_;
          for (auto index = std::size_t{};
               index < last_scene_speech_arguments_.size(); ++index) {
            last_scene_speech_arguments_[index] =
                context.argument(static_cast<std::uint32_t>(index));
          }
          recordTimelineEvent(
              Sf2GuestTimelineEventKind::scene_speech_start, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008d8a8U, [this](LegacyHostCallContext &context) {
          ++scene_speech_callbacks_;
          for (auto index = std::size_t{};
               index < last_scene_speech_callback_arguments_.size();
               ++index) {
            last_scene_speech_callback_arguments_[index] =
                context.argument(static_cast<std::uint32_t>(index));
          }
          constexpr std::uint64_t speech_callback_trace_window = 30U;
          if (last_scene_speech_timeline_frame_ &&
              guest_frame_ >= *last_scene_speech_timeline_frame_ &&
              guest_frame_ - *last_scene_speech_timeline_frame_ <=
                  speech_callback_trace_window) {
            recordTimelineEvent(
                Sf2GuestTimelineEventKind::scene_speech_callback, context);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008df60U, [this](LegacyHostCallContext &context) {
          ++scene_speech_stops_;
          for (auto index = std::size_t{};
               index < last_scene_speech_stop_arguments_.size(); ++index) {
            last_scene_speech_stop_arguments_[index] =
                context.argument(static_cast<std::uint32_t>(index));
          }
          recordTimelineEvent(
              Sf2GuestTimelineEventKind::scene_speech_stop, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008dc9cU, [this](LegacyHostCallContext &context) {
          scene_speech_io_ready_ =
              static_cast<std::uint8_t>(context.registerValue(2U) != 0U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800fa720U, [this](LegacyHostCallContext &context) {
          // SoundBank_SelectVariants indexes an eight-byte table and then
          // walks 24-byte variant records. AIRBASE retains a linked PL02
          // bank whose allocation has already been repurposed during the
          // authored opening; its stale table pointer is unaligned. Retail
          // reaches it on the first sustained footstep request and would
          // issue an unaligned LW. Treat only an unreadable table as an empty
          // selection, matching the routine's existing out-of-range result.
          constexpr std::uint32_t ram_begin = 0x80000000U;
          constexpr std::uint32_t ram_end = 0x80200000U;
          const auto bank = context.argument(0U);
          const auto index = context.argument(1U);
          std::uint32_t magic{};
          std::uint16_t entry_count{};
          std::uint32_t table{};
          std::uint8_t variant_count{};
          std::uint32_t variants{};
          auto valid =
              bank >= ram_begin && bank < ram_end &&
              context.read16(bank + 0x1aU, entry_count) &&
              index < entry_count &&
              context.read32(bank + 0x24U, table) &&
              (table & 3U) == 0U && table >= ram_begin &&
              table <= ram_end - 8U;
          const auto resolved_entry =
              valid && index <=
                           (std::numeric_limits<std::uint32_t>::max() -
                            table) /
                               8U
                  ? table + index * 8U
                  : 0U;
          valid = valid && resolved_entry >= ram_begin &&
                  resolved_entry <= ram_end - 8U &&
                  context.read8(resolved_entry, variant_count) &&
                  context.read32(resolved_entry + 4U, variants) &&
                  (variant_count == 0U ||
                   ((variants & 3U) == 0U && variants >= ram_begin &&
                    variants <= ram_end - 0x18U));
          if (!valid) {
            ++rejected_sound_bank_lookups_;
            last_rejected_sound_bank_ = bank;
            last_rejected_sound_bank_table_ = table;
            last_rejected_sound_bank_index_ = index;
            last_rejected_sound_bank_caller_ = context.returnAddress();
            static_cast<void>(context.read32(bank, magic));
            last_rejected_sound_bank_magic_ = magic;
            last_rejected_sound_bank_entry_count_ = entry_count;
            context.setReturnValue(0U);
            return;
          }
          context.continueGuestInstruction();
        });
    constexpr std::array scene_speech_stages{
        0x8008dc24U, 0x8008dc4cU, 0x8008dc94U,
        0x8008dca4U, 0x8008dcb4U, 0x8008dcdcU,
    };
    for (auto index = std::size_t{}; index < scene_speech_stages.size();
         ++index) {
      vm_.bindHostCall(
          scene_speech_stages[index],
          [this, stage = static_cast<std::uint8_t>(index + 1U)](
              LegacyHostCallContext &context) {
            scene_speech_stage_ = std::max(scene_speech_stage_, stage);
            context.continueGuestInstruction();
          });
    }
    vm_.bindHostCall(
        0x800f9458U, [this](LegacyHostCallContext &context) {
          ++xa_cue_plays_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f957cU, [this](LegacyHostCallContext &context) {
          // Retail passes an absolute ISO sector from the SCENES archive
          // handle. Temporarily disable the mission-relative FOG namespace at
          // the authentic XA entry boundary.
          setXaAbsoluteDiscActive(true);
          ++xa_stream_starts_;
          recordTimelineEvent(
              Sf2GuestTimelineEventKind::xa_stream_start, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f97a8U, [this](LegacyHostCallContext &context) {
          // XaFilteredStream_Start has one return. A negative result means no
          // XA lifecycle owns the CD path, so immediately restore the mission
          // extent; successful streams retain SCENES*.XA until XaStream_Stop.
          if (static_cast<std::int32_t>(context.registerValue(2U)) < 0) {
            setXaAbsoluteDiscActive(false);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f9a10U, [this](LegacyHostCallContext &context) {
          ++xa_stream_stops_;
          recordTimelineEvent(
              Sf2GuestTimelineEventKind::xa_stream_stop, context);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f9a48U, [this](LegacyHostCallContext &context) {
          // This JR RA is the single return from XaStream_Stop, after its CD
          // disable and scheduler-reset calls have completed.
          setXaAbsoluteDiscActive(false);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f9a68U, [this](LegacyHostCallContext &context) {
          xa_status_source_ = context.registerValue(2U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800f9a78U, [this](LegacyHostCallContext &context) {
          xa_status_result_ = context.registerValue(2U);
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a4560U, [this](LegacyHostCallContext &context) {
          ++async_file_services_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800a2370U, [this](LegacyHostCallContext &context) {
          ++async_file_completions_;
          last_async_completion_caller_ = context.returnAddress();
          context.continueGuestInstruction();
        });
    const auto &layout = disc_.game()->executable_layout;
    vm_.bindPsxVideoTimingCall(layout.vsync_address,
                               layout.retrace_counter_address);
    vm_.bindPsxCdPendingCommandCall(
        layout.cd_pending_command_address, layout.cd_pending_command_state,
        layout.cd_response_pointer, layout.cd_completion_state,
        profile_.cd_completion_result);
    vm_.bindPsxCdControlCall(layout.cd_control_address,
                             profile_.cd_setloc_state,
                             profile_.cd_mode_state);
    vm_.bindPsxCdReadyCallback(
        layout.cd_ready_callback_address, layout.cd_ready_result_address,
        layout.cd_ready_state_address,
        layout.cd_ready_callback_is_pointer);
    vm_.bindPsxCdCompletionCallback(0x8011d1bcU,
                                    profile_.cd_completion_result, true);
    bindSearchFile();
    bindResidentFiles();
    vm_.bindHostCall(profile_.gpu_submission_entry,
                     [](LegacyHostCallContext &context) {
                       context.setReturnValue(0U);
                     });
    vm_.bindHostCall(profile_.state_loop_entry,
                     [](LegacyHostCallContext &context) {
                       context.continueGuestInstruction();
                     });
  }

  void applyPcManualAim(LegacyHostCallContext &context) noexcept {
    if (!pc_manual_aim_enabled_ || !pc_manual_aim_snap_yaw_) {
      return;
    }
    constexpr std::uint32_t yaw_offset = 0x14U;
    const auto stack = context.registerValue(29U);
    const auto normalize = [](std::int32_t angle) {
      angle &= 0xfff;
      return angle > 0x7ff ? angle - 0x1000 : angle;
    };
    const auto yaw = normalize(*pc_manual_aim_snap_yaw_);
    static_cast<void>(context.write32(
        stack + yaw_offset, std::bit_cast<std::uint32_t>(yaw)));
    pc_manual_aim_snap_yaw_.reset();
  }

  void applyPcMouseFacingVector(LegacyHostCallContext &context) noexcept {
    if (!pc_manual_aim_enabled_ && !pc_chase_yaw_enabled_) {
      return;
    }
    // SF1's accepted 0x80037B08 hook writes the processed mouse vector to
    // controller +0xCC/+0xD4. SF2 0x80053464 is instruction-identical; its
    // controller is preserved in s2 at this boundary.
    constexpr std::uint32_t horizontal_offset = 0xccU;
    constexpr std::uint32_t middle_offset = 0xd0U;
    constexpr std::uint32_t vertical_offset = 0xd4U;
    constexpr std::int32_t fixed_one = 4096;
    const auto controller = context.registerValue(18U);
    const auto manual_aim = pc_manual_aim_enabled_;
    const auto yaw = std::clamp(
        manual_aim ? pc_manual_aim_yaw_pending_ : pc_chase_yaw_pending_,
        -256, 256);
    const auto pitch = manual_aim
                           ? std::clamp(pc_manual_aim_pitch_pending_, -96, 96)
                           : 0;
    // Normal-play mouse yaw is an additive aftermarket command. With no
    // pending motion, leave retail's keyboard/controller facing vector alone.
    if (!manual_aim && yaw == 0) {
      return;
    }
    if (controller == 0U ||
        !context.write32(controller + horizontal_offset,
                         std::bit_cast<std::uint32_t>(yaw * fixed_one)) ||
        !context.write32(controller + middle_offset, 0U) ||
        !context.write32(controller + vertical_offset,
                         std::bit_cast<std::uint32_t>(pitch * fixed_one))) {
      return;
    }
    if (manual_aim) {
      ++pc_manual_aim_hook_calls_;
      pc_manual_aim_yaw_command_ = yaw;
      pc_manual_aim_pitch_command_ = pitch;
      pc_manual_aim_yaw_pending_ = 0;
      pc_manual_aim_pitch_pending_ = 0;
    } else {
      ++pc_chase_yaw_hook_calls_;
      pc_chase_yaw_command_ = yaw;
      pc_chase_yaw_pending_ = 0;
    }
  }

  void applyPcChaseCameraPitch() noexcept {
    // SF1's proven desired/rendered pitch pair (+0x954/+0x984) maps through
    // the structurally matched SF2 camera routine to +0x8E8/+0x918. The
    // wrapper/base indirection is authored by 0x80049940 itself.
    constexpr std::uint32_t player_pointer = 0x8012a574U;
    constexpr std::uint32_t instance_player_state_offset = 0x20U;
    constexpr std::uint32_t player_state_camera_offset = 0xe0U;
    constexpr std::uint32_t camera_wrapper_base_offset = 0xa4U;
    constexpr std::uint32_t camera_wrapper_owner_offset = 0xdcU;
    constexpr std::uint32_t camera_wrapper_mode_flags_offset = 0x13cU;
    constexpr std::uint32_t camera_desired_pitch_offset = 0x8e8U;
    constexpr std::uint32_t camera_rendered_pitch_offset = 0x918U;
    constexpr std::int32_t maximum_pitch = 512;

    std::uint32_t player{};
    std::uint32_t player_state{};
    std::uint32_t camera_wrapper{};
    std::uint32_t camera_base{};
    std::uint32_t camera_owner{};
    std::uint32_t camera_flags{};
    if (!pc_chase_pitch_enabled_ ||
        !vm_.runtime().read32(player_pointer, player) || player == 0U ||
        !vm_.runtime().read32(player + instance_player_state_offset,
                              player_state) ||
        player_state == 0U ||
        !vm_.runtime().read32(player_state + player_state_camera_offset,
                              camera_wrapper) ||
        camera_wrapper == 0U ||
        !vm_.runtime().read32(camera_wrapper + camera_wrapper_base_offset,
                              camera_base) ||
        camera_base == 0U ||
        !vm_.runtime().read32(camera_wrapper + camera_wrapper_owner_offset,
                              camera_owner) ||
        !vm_.runtime().read32(camera_wrapper + camera_wrapper_mode_flags_offset,
                              camera_flags)) {
      // The camera base survives retail ownership transfers. During an
      // in-engine cinematic wrapper+0xDC names the scripted camera actor;
      // ordinary chase gameplay names the live player instance. Never carry
      // mouse motion accumulated under scripted ownership into the handoff.
      pc_chase_pitch_pending_ = 0;
      pc_chase_pitch_valid_ = false;
      pc_chase_camera_base_ = 0U;
      return;
    }
    pc_chase_camera_flags_ = camera_flags;
    if (camera_owner != player) {
      pc_chase_pitch_pending_ = 0;
      pc_chase_pitch_valid_ = false;
      pc_chase_scripted_camera_seen_ = true;
      pc_chase_camera_base_ = 0U;
      return;
    }
    // PlayerObjectInteraction_Start retains player camera ownership while
    // wrapper mode bit 0x40 gives the authored pickup/C4 interaction full
    // control of its camera curve. Rewriting the chase pitch through this
    // interval carries the last mouse angle into that close framing and
    // produces the apparent autonomous pan. Suspend until retail clears the
    // mode, then reacquire from neutral on the next vertical mouse command.
    if ((camera_flags & 0x40U) != 0U) {
      pc_chase_pitch_pending_ = 0;
      pc_chase_pitch_valid_ = false;
      pc_chase_scripted_camera_seen_ = true;
      pc_chase_camera_base_ = 0U;
      ++pc_chase_interaction_suspensions_;
      return;
    }
    ++pc_chase_pitch_hook_calls_;

    const auto normalize_angle = [](std::uint32_t value) {
      auto angle = static_cast<std::int32_t>(value & 0xfffU);
      if (angle > 0x7ff) {
        angle -= 0x1000;
      }
      return angle;
    };
    if (!pc_chase_pitch_valid_ || pc_chase_camera_base_ != camera_base) {
      // Do not seize a newly returned retail chase camera merely because the
      // PC pitch feature is enabled.  At an in-engine-cutscene handoff the
      // authored camera can still carry the outgoing shot's pitch for one or
      // more guest ticks; caching and rewriting that value with no mouse
      // command freezes the transient angle and produces a visible snap.  Let
      // retail finish the handoff, and only establish the aftermarket pitch
      // target when the player actually moves the mouse vertically.
      if (pc_chase_pitch_pending_ == 0) {
        pc_chase_camera_base_ = 0U;
        return;
      }
      pc_chase_camera_base_ = camera_base;
      if (pc_chase_scripted_camera_seen_) {
        // The first vertical mouse command after a scripted-camera handoff
        // starts from the neutral chase-camera horizon. The authored field can
        // still contain the outgoing shot's upward pitch at this exact point;
        // adopting it made mouse-look permanently inherit that stale angle.
        pc_chase_pitch_target_ = 0;
        pc_chase_scripted_camera_seen_ = false;
      } else {
        std::uint32_t authored_pitch{};
        if (!vm_.runtime().read32(camera_base + camera_desired_pitch_offset,
                                  authored_pitch)) {
          return;
        }
        pc_chase_pitch_target_ = std::clamp(
            normalize_angle(authored_pitch), -maximum_pitch, maximum_pitch);
      }
      pc_chase_pitch_valid_ = true;
    }
    pc_chase_pitch_target_ = std::clamp(
        pc_chase_pitch_target_ + pc_chase_pitch_pending_,
        -maximum_pitch, maximum_pitch);
    pc_chase_pitch_pending_ = 0;
    static_cast<void>(vm_.runtime().write32(
        camera_base + camera_desired_pitch_offset,
        static_cast<std::uint32_t>(pc_chase_pitch_target_)));
    static_cast<void>(vm_.runtime().write32(
        camera_base + camera_rendered_pitch_offset,
        static_cast<std::uint32_t>(pc_chase_pitch_target_)));
    std::uint32_t rendered_pitch{};
    if (vm_.runtime().read32(camera_base + camera_rendered_pitch_offset,
                             rendered_pitch)) {
      pc_chase_rendered_pitch_ = normalize_angle(rendered_pitch);
    }
  }

  void installExceptionBridge() {
    constexpr std::array return_code{
        0x03600008U, // jr k1
        0x0340f821U, // addu ra,k0,zero
    };
    std::array<std::byte, return_code.size() * sizeof(std::uint32_t)> bytes{};
    for (std::size_t word = 0U; word < return_code.size(); ++word) {
      for (std::size_t byte = 0U; byte < sizeof(std::uint32_t); ++byte) {
        bytes[word * sizeof(std::uint32_t) + byte] =
            static_cast<std::byte>(return_code[word] >> (byte * 8U));
      }
    }
    if (!vm_.runtime().loadBytes(exception_trampoline_, bytes)) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Could not install SF2 exception trampoline"};
    }
    vm_.bindHostCall(exception_trampoline_,
                     [](LegacyHostCallContext &context) {
                       context.continueGuestInstruction();
                     });
    vm_.bindHostCall(
        0x80000080U, [this](LegacyHostCallContext &context) {
          constexpr std::uint32_t mode_stack_mask = 0x0fU;
          constexpr std::uint32_t interrupt_status = 0x1f801070U;
          auto state = vm_.runtime().state();
          const auto resume_pc = state.cop0_epc;
          const auto interrupted_return = state.gpr[31U];
          state.cop0_status =
              (state.cop0_status & ~mode_stack_mask) |
              ((state.cop0_status >> 2U) & mode_stack_mask);
          vm_.runtime().restoreCpuState(state);
          if (!context.write16(interrupt_status, 0U)) {
            context.rejectHostCall();
            return;
          }
          vm_.runtime().setExternalInterrupt(false);
          context.setRegister(26U, interrupted_return);
          context.setRegister(27U, resume_pc);
          context.setRegister(31U, exception_trampoline_);
          context.setReturnValue(0U);
        });
  }

  void bindSearchFile() {
    vm_.bindHostCall(
        profile_.cd_search_file_entry,
        [this](LegacyHostCallContext &context) {
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
          try {
            const auto entry = disc_.image().find(path);
            if (entry.is_directory) {
              context.setReturnValue(0U);
              return;
            }
            constexpr std::uint32_t pregap = 150U;
            constexpr std::uint32_t sectors_per_second = 75U;
            const auto absolute = entry.extent_lba + pregap;
            const auto bcd = [](std::uint32_t value) {
              return static_cast<std::byte>(((value / 10U) << 4U) |
                                            (value % 10U));
            };
            std::array<std::byte, 24U> file{};
            file[0] = bcd(absolute / (60U * sectors_per_second));
            file[1] = bcd((absolute / sectors_per_second) % 60U);
            file[2] = bcd(absolute % sectors_per_second);
            for (std::size_t index = 0U; index < sizeof(entry.size); ++index) {
              file[4U + index] =
                  static_cast<std::byte>(entry.size >> (index * 8U));
            }
            const auto name_size =
                std::min(entry.name.size(), file.size() - 8U);
            for (std::size_t index = 0U; index < name_size; ++index) {
              file[8U + index] = static_cast<std::byte>(entry.name[index]);
            }
            context.setReturnValue(
                context.writeBytes(destination, file) ? destination : 0U);
          } catch (const core::Error &) {
            context.setReturnValue(0U);
          }
        });

    vm_.bindHostCall(
        0x80010750U, [this](LegacyHostCallContext &context) {
          if (context.argument(0) != 0x80126058U ||
              context.argument(2) != 0x90U) {
            context.continueGuestInstruction();
            return;
          }
          if (!context.readBytes(context.argument(1), catalog_copy_) ||
              !context.writeBytes(context.argument(0), catalog_copy_)) {
            context.setReturnValue(0xffffffffU);
            return;
          }
          catalog_copied_ = true;
          context.setReturnValue(0U);
        });
    vm_.bindHostCall(
        0x800260e4U, [this](LegacyHostCallContext &context) {
          if (!catalog_copied_) {
            context.continueGuestInstruction();
            return;
          }
          try {
            const auto entry = disc_.image().find(fog_path_);
            const auto stack = context.registerValue(29U);
            std::uint32_t caller{};
            std::uint32_t saved_s0{};
            if (!context.writeBytes(0x80126058U, catalog_copy_) ||
                !context.write32(0x8011ee68U, 0x80126058U) ||
                !context.write32(0x80126060U, entry.extent_lba) ||
                !context.read32(stack + 0x81cU, caller) ||
                !context.read32(stack + 0x818U, saved_s0)) {
              context.setReturnValue(0U);
              return;
            }
            context.setRegister(16U, saved_s0);
            context.setRegister(29U, stack + 0x820U);
            context.setRegister(31U, caller);
            context.setReturnValue(1U);
          } catch (const core::Error &) {
            context.setReturnValue(0U);
          }
        });
  }

  void bindResidentFiles() {
    vm_.bindHostCall(
        0x80026414U, [this](LegacyHostCallContext &context) {
          auto open = open_files_.find(context.argument(0));
          if (open == open_files_.end()) {
            std::uint32_t handle_size{};
            const auto movie = disc_.image().find(
                disc_.game()->disc_number == 2U ? "MOVIE2.HOG"
                                                : "MOVIE1.HOG");
            if (context.argument(1) != 0U &&
                context.argument(2) == 0x800U &&
                context.read32(context.argument(0) + 4U, handle_size) &&
                handle_size == movie.size) {
              std::array<std::byte, 0x800U> sector{};
              if (!cdrom_media_.readDataSector(movie.extent_lba, sector) ||
                  !context.writeBytes(context.argument(1), sector) ||
                  (context.argument(3) != 0U &&
                   !context.write32(context.argument(3), 0U))) {
                context.setReturnValue(3U);
                return;
              }
              context.setReturnValue(0U);
              return;
            }
            const std::vector<std::byte> *bytes{};
            auto matches = std::size_t{};
            if (context.read32(context.argument(0) + 4U, handle_size)) {
              for (const auto &[name, candidate] : resident_files_) {
                static_cast<void>(name);
                if (candidate.size() == handle_size) {
                  bytes = &candidate;
                  ++matches;
                }
              }
            }
            if (matches == 1U) {
              open =
                  open_files_
                      .insert_or_assign(context.argument(0),
                                        OpenFile{bytes, 0U})
                      .first;
            }
          }
          if (open == open_files_.end()) {
            context.continueGuestInstruction();
            return;
          }
          const auto requested =
              static_cast<std::size_t>(context.argument(2));
          const auto &bytes = *open->second.bytes;
          const auto available = open->second.offset < bytes.size()
                                     ? bytes.size() - open->second.offset
                                     : 0U;
          const auto copied = std::min(requested, available);
          std::vector<std::byte> payload(requested);
          std::ranges::copy_n(
              bytes.begin() +
                  static_cast<std::ptrdiff_t>(open->second.offset),
              copied, payload.begin());
          if (context.argument(1) == 0U ||
              !context.writeBytes(context.argument(1), payload) ||
              (context.argument(3) != 0U &&
               !context.write32(context.argument(3), 0U))) {
            context.setReturnValue(3U);
            return;
          }
          open->second.offset += copied;
          if (open->second.offset >= bytes.size()) {
            open_files_.erase(open);
          }
          context.setReturnValue(0U);
        });
    vm_.bindHostCall(
        0x8002662cU, [this](LegacyHostCallContext &context) {
          std::uint32_t handle{};
          if (context.argument(0) != 0U &&
              context.read32(context.argument(0), handle)) {
            open_files_.erase(handle);
          }
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80026234U, [this](LegacyHostCallContext &context) {
          std::string path;
          if (!context.readCString(context.argument(0), path, 256U) ||
              context.argument(1) == 0U) {
            context.continueGuestInstruction();
            return;
          }
          if (path.ends_with(";1")) {
            path.resize(path.size() - 2U);
          }
          const auto separator = path.find_last_of("\\/");
          if (separator != std::string::npos) {
            path.erase(0U, separator + 1U);
          }
          std::ranges::transform(
              path, path.begin(), [](unsigned char value) {
                return static_cast<char>(std::toupper(value));
              });
          const auto entry =
              std::ranges::find_if(fog_entries_, [&path](const auto &item) {
                auto name = item.name;
                std::ranges::transform(
                    name, name.begin(), [](unsigned char value) {
                      return static_cast<char>(std::toupper(value));
                    });
                return name == path;
              });
          const auto file = fog_files_.find(path);
          const auto top_level = entry != fog_entries_.end() &&
                                 file != fog_files_.end();
          if (!top_level) {
            context.continueGuestInstruction();
            return;
          }
          auto handle = std::uint32_t{};
          for (std::size_t index = 0U; index < 5U; ++index) {
            const auto candidate =
                0x80125ff4U + static_cast<std::uint32_t>(index * 0x14U);
            std::uint32_t state{};
            if (context.read32(candidate + 4U, state) &&
                state == 0xcacacacaU) {
              handle = candidate;
              break;
            }
          }
          if (handle == 0U) {
            context.setReturnValue(3U);
            return;
          }
          const auto fog = disc_.image().find(fog_path_);
          const auto sector = fog.extent_lba + entry->start_sector;
          const auto size = entry->sector_count << 11U;
          const auto absolute = sector + 150U;
          const auto bcd = [](std::uint32_t value) {
            return static_cast<std::byte>(((value / 10U) << 4U) |
                                          (value % 10U));
          };
          std::array<std::byte, 20U> descriptor{};
          descriptor[0] = bcd(absolute / (60U * 75U));
          descriptor[1] = bcd((absolute / 75U) % 60U);
          descriptor[2] = bcd(absolute % 75U);
          descriptor[12] = descriptor[0];
          descriptor[13] = descriptor[1];
          descriptor[14] = descriptor[2];
          const auto write32 = [&descriptor](std::size_t offset,
                                              std::uint32_t value) {
            for (std::size_t index = 0U; index < sizeof(value); ++index) {
              descriptor[offset + index] =
                  static_cast<std::byte>(value >> (index * 8U));
            }
          };
          write32(4U, size);
          write32(8U, sector + entry->sector_count - 1U);
          write32(16U, size);
          if (!context.writeBytes(handle, descriptor) ||
              !context.write32(context.argument(1), handle)) {
            context.setReturnValue(3U);
            return;
          }
          open_files_.insert_or_assign(handle, OpenFile{&file->second, 0U});
          context.setReturnValue(0U);
        });
  }

  [[nodiscard]] bool serviceScheduler(
      std::uint64_t ticks,
      std::uint64_t unexecuted_hardware_ticks = 0U) noexcept {
    if (unexecuted_hardware_ticks != 0U) {
      vm_.machine().advanceHardwareTicks(unexecuted_hardware_ticks);
    }
    retrace_ticks_ += ticks;
    const auto retraces = retrace_ticks_ / retrace_period_;
    retrace_ticks_ %= retrace_period_;
    if (retraces != 0U) {
      const auto address =
          disc_.game()->executable_layout.retrace_counter_address;
      std::uint32_t counter{};
      if (!vm_.runtime().read32(address, counter) ||
          !vm_.runtime().write32(
              address, counter + static_cast<std::uint32_t>(retraces))) {
        scheduler_fault_detail_ = "retrace counter update";
        return false;
      }
      // Retail's XA scheduler maintains a second software retrace clock.
      // Hardware VBlank delivery normally advances it; this runtime
      // deliberately suppresses guest interrupts and owns that boundary, so
      // mirror the same 60 Hz increments alongside the PsyQ VSync counter.
      constexpr std::uint32_t xa_retrace_clock = 0x8011f464U;
      if (!vm_.runtime().read32(xa_retrace_clock, counter) ||
          !vm_.runtime().write32(
              xa_retrace_clock,
              counter + static_cast<std::uint32_t>(retraces))) {
        scheduler_fault_detail_ = "XA retrace clock update";
        return false;
      }
    }
    LegacyGameplayVmResult cd_ready_result{};
    if (!vm_.servicePsxCdReadyCallback(&cd_ready_result, callback_stack_)) {
      scheduler_fault_detail_ =
          "CD ready callback reason=" +
          std::string{psx::toString(cd_ready_result.execution.reason)} +
          " pc=" + std::to_string(cd_ready_result.execution.pc) +
          " instruction=" +
          std::to_string(cd_ready_result.execution.instruction);
      return false;
    }
    if (!serviceDmaCallback(1U << (24U + 3U), 0x8011d114U)) {
      scheduler_fault_detail_ = "CD DMA callback";
      return false;
    }
    if (!serviceDmaCallback(1U << (24U + 4U), 0x8011e3acU)) {
      scheduler_fault_detail_ = "SPU DMA callback";
      return false;
    }
    callback_ticks_ += ticks;
    constexpr std::array callback_slots{
        profile_.interrupt_callback_table + 4U * 4U,
        profile_.interrupt_callback_table + 7U * 4U,
    };
    while (callback_ticks_ >= callback_period_) {
      callback_ticks_ -= callback_period_;
      for (const auto slot : callback_slots) {
        if (!vm_.servicePsxCallbackSlot(slot, callback_stack_)) {
          scheduler_fault_detail_ =
              slot == callback_slots.front() ? "root task callback"
                                             : "retail slot-7 callback";
          return false;
        }
      }
    }
    // The checkpoint loader consumes instruction-budget slices while waiting
    // synchronously for CD hardware. It is not a normal interruptible
    // gameplay boundary: entering the sound flush there can re-enter retail
    // resource state while executable/mission data is being restored. Resume
    // sound service only after the loader has returned to stable app-state-0
    // display submissions and the callback slot has been reinstalled.
    if (retail_restore_active_) {
      audio_callback_ticks_ = 0U;
    } else {
      audio_callback_ticks_ += ticks;
      constexpr auto audio_callback_slot =
          profile_.interrupt_callback_table + 6U * 4U;
      while (audio_callback_ticks_ >= audio_callback_period_) {
        audio_callback_ticks_ -= audio_callback_period_;
        if (!vm_.servicePsxCallbackSlot(audio_callback_slot,
                                        callback_stack_)) {
          scheduler_fault_detail_ = "retail SPU voice flush";
          return false;
        }
      }
    }
    if (xa_absolute_disc_active_) {
      const auto audio = vm_.audioDiagnostics();
      if (audio.xa_stream_set != 0U) {
        xa_stream_observed_active_ = true;
      } else if (xa_stream_observed_active_) {
        // One-sector EOF markers can retire without calling XaStream_Stop.
        // Restore relative reads to the mission FOG after any observed XA
        // stream naturally clears, matching the explicit stop return hook.
        setXaAbsoluteDiscActive(false);
      }
    }
    return true;
  }

  [[nodiscard]] bool installSoundServiceCallback() noexcept {
    // The shortened TITLE-to-mission handoff omits the outer lifecycle which
    // services this exact sequel sound-command/sequence flush at 120 Hz.
    constexpr std::uint32_t sound_service = 0x80104b40U;
    constexpr auto sound_callback_slot =
        profile_.interrupt_callback_table + 6U * 4U;
    return vm_.runtime().write32(sound_callback_slot, sound_service);
  }

  [[nodiscard]] bool serviceDmaCallback(std::uint32_t flag,
                                        std::uint32_t slot) noexcept {
    constexpr std::uint32_t interrupt_control = 0x1f8010f4U;
    std::uint32_t control{};
    if (!vm_.runtime().read32(interrupt_control, control)) {
      return false;
    }
    if ((control & flag) == 0U) {
      return true;
    }
    return vm_.runtime().write32(
               interrupt_control, (control & 0x00ffffffU) | flag) &&
           vm_.servicePsxCallbackSlot(slot, callback_stack_);
  }

  [[nodiscard]] LegacyGameplayVmResult
  runUntilBoundary(std::uint32_t address) noexcept {
    constexpr std::uint32_t interrupt_status = 0x1f801070U;
    if (suppress_interrupts_) {
      static_cast<void>(vm_.runtime().write16(interrupt_status, 0U));
      vm_.runtime().setExternalInterrupt(false);
    }
    auto remaining = execution_budget_;
    auto total = std::uint64_t{};
    std::array<std::uint32_t, 256U> exhausted_slice_pcs{};
    auto exhausted_slice_pc_count = std::size_t{};
    auto exhausted_slice_pc_cursor = std::size_t{};
    scheduler_fault_detail_.clear();
    LegacyGameplayVmResult result;
    for (;;) {
      const auto slice = std::min(remaining, scheduler_slice_budget_);
      result = suppress_interrupts_
                   ? vm_.runCurrentPcUntilHostBoundaryClockNeutral(address,
                                                                   slice)
                   : vm_.runCurrentPcUntilHostBoundary(address, slice);
      total += result.execution.instructions;
      const auto exhausted_slice =
          realtime_display_clock_ &&
          result.execution.reason == psx::R3000StopReason::instruction_budget;
      const auto repeated_execution =
          exhausted_slice &&
          std::find(exhausted_slice_pcs.begin(),
                    exhausted_slice_pcs.begin() +
                        static_cast<std::ptrdiff_t>(
                            exhausted_slice_pc_count),
                    result.execution.pc) !=
              exhausted_slice_pcs.begin() +
                  static_cast<std::ptrdiff_t>(exhausted_slice_pc_count);
      if (exhausted_slice) {
        exhausted_slice_pcs[exhausted_slice_pc_cursor] = result.execution.pc;
        exhausted_slice_pc_cursor =
            (exhausted_slice_pc_cursor + 1U) % exhausted_slice_pcs.size();
        exhausted_slice_pc_count = std::min(
            exhausted_slice_pc_count + 1U, exhausted_slice_pcs.size());
      }
      // A complete quantum alone can be ordinary instruction-heavy gameplay;
      // charging all such work made audio/device time scene-dependent. A PC
      // recurring at a quantum boundary within this one host-boundary search
      // proves that guest execution has entered a cycle. Advance every device
      // by the retired guest cycles until hardware state lets that cycle exit.
      // This recognizes no function, return address, MMIO register or display
      // poll, so card events, CD/DMA completion, timers and future device waits
      // share one deterministic rule. A boundary reached inside a quantum
      // remains atomic and is padded to its next VBlank by the caller.
      const auto scheduler_ticks =
          !realtime_display_clock_ || repeated_execution
              ? result.execution.instructions
              : 0U;
      if (scheduler_ticks != 0U) {
        ++device_wait_scheduler_slices_;
      }
      if (!serviceScheduler(scheduler_ticks,
                            suppress_interrupts_ ? scheduler_ticks : 0U)) {
        result.execution.reason = psx::R3000StopReason::memory_fault;
        break;
      }
      if (result.stoppedAtHostBoundary() ||
          result.execution.reason != psx::R3000StopReason::instruction_budget ||
          remaining <= slice) {
        break;
      }
      remaining -= slice;
    }
    result.execution.instructions = total;
    return result;
  }

  [[nodiscard]] LegacyGameplayVmResult
  invokeNested(std::uint32_t address,
               std::span<const std::uint32_t> arguments) noexcept {
    const auto continuation = vm_.runtime().state();
    vm_.bindHostCall(return_trampoline_,
                     [](LegacyHostCallContext &context) {
                       context.continueGuestInstruction();
                     });
    LegacyGameplayVmResult result;
    if (!vm_.runtime().beginCall(address, arguments)) {
      result.execution.reason = psx::R3000StopReason::memory_fault;
      result.execution.pc = address;
      return result;
    }
    vm_.runtime().setRegister(31U, return_trampoline_);
    result = runUntilBoundary(return_trampoline_);
    if (result.stoppedAtHostBoundary() || result.completed()) {
      vm_.runtime().restoreCpuState(continuation);
    }
    return result;
  }

  [[nodiscard]] bool retireBoundary(
      const LegacyGameplayVmResult &boundary) noexcept {
    const auto retired = vm_.resumeCurrentPcClockNeutral(1U);
    const auto padding =
        boundary.execution.instructions < retrace_period_
            ? retrace_period_ - boundary.execution.instructions
            : 0U;
    return retired.execution.reason ==
               psx::R3000StopReason::instruction_budget &&
           serviceScheduler(retired.execution.instructions + padding,
                            suppress_interrupts_
                                ? retired.execution.instructions + padding
                                : 0U);
  }

  [[nodiscard]] bool bootstrap() {
    setStage("executable state loop");
    const auto state_loop =
        vm_.runCurrentPcUntilHostBoundary(profile_.state_loop_entry,
                                           execution_budget_);
    if (!state_loop.stoppedAtHostBoundary() || !serviceScheduler(0U, 0U)) {
      return false;
    }
    suppress_interrupts_ = true;
    setStage("frontend quiescence");
    constexpr std::uint32_t interrupt_status = 0x1f801070U;
    static_cast<void>(vm_.runtime().write16(interrupt_status, 0U));
    vm_.runtime().setExternalInterrupt(false);

    auto settled = false;
    for (std::size_t boundary_index = 0U; boundary_index < 2'000U;
         ++boundary_index) {
      const auto boundary = runUntilBoundary(profile_.gpu_submission_entry);
      if (!boundary.stoppedAtHostBoundary()) {
        return false;
      }
      const auto cd = vm_.machine().cdrom().captureState();
      std::uint8_t cleanup{};
      std::uint8_t disc_index{};
      std::uint32_t task_callback{};
      settled = vm_.runtime().read8(0x8011cf6cU, cleanup) &&
                vm_.runtime().read8(0x8011f608U, disc_index) &&
                vm_.runtime().read32(
                    profile_.interrupt_callback_table + 4U * 4U,
                    task_callback) &&
                cleanup == 0U && disc_index < 2U &&
                task_callback == 0x80022584U &&
                (cd.interrupt_flags & 7U) == 0U &&
                cd.pending_command == 0U && cd.reading == 0U &&
                cd.seeking == 0U;
      if (!retireBoundary(boundary)) {
        return false;
      }
      if (settled) {
        break;
      }
    }
    setStage("TITLE frontend");
    if (!settled || !driveTitleFrontend()) {
      return false;
    }

    setStage("TITLE cleanup");
    const auto cleanup =
        invokeNested(0x8014db50U, std::span<const std::uint32_t>{});
    std::uint32_t slot7{};
    if ((!cleanup.completed() && !cleanup.stoppedAtHostBoundary()) ||
        !vm_.runtime().read32(
            profile_.interrupt_callback_table + 7U * 4U, slot7) ||
        slot7 != 0U) {
      return false;
    }
    constexpr std::array release_arguments{0U, 0U};
    setStage("frontend graphics release");
    const auto release = invokeNested(0x80015510U, release_arguments);
    if (!release.completed() && !release.stoppedAtHostBoundary()) {
      return false;
    }
    // Disc 2's campaign order and MissionArchive table order differ. Passing
    // a campaign index directly opens an unrelated FOG whose first sector is
    // then misinterpreted as common descriptors.
    const auto archive_selection = missionArchiveSelection(
        disc_.game()->id, disc_.game()->disc_number,
        runtime_selection_);
    if (!archive_selection ||
        !vm_.runtime().write32(0x801582d4U, *archive_selection) ||
        !vm_.runtime().write32(0x80156bdcU, 17U) ||
        // Gameplay is a fresh TITLE-to-mission handoff: mission overlays use
        // the checkpoint-present flag to select clean-start choreography, so
        // it stays zero there. The disposable briefing presenter deliberately
        // takes retail's state-8 loading route (the same continuous frontend
        // route proven by sf_tool) and never supplies state to gameplay.
        !vm_.runtime().write32(
            0x8011f61cU,
            start_mode_ == Sf2GuestRuntimeStartMode::retail_briefing ? 1U
                                                                     : 0U)) {
      return false;
    }
    setXaAbsoluteDiscActive(false);
    setStage("mission archive handoff");
    if (mission_index_ == 4U) {
      // Direct AIRBASEX construction is deterministic. Observe the first
      // initialization of source 123's model-matrix table pointer while the
      // archive handoff still owns object allocation.
      vm_.runtime().setWriteTrace(0x801a70bcU, 0x801a70c0U);
      vm_.runtime().setWriteTracePc(0U, 0U);
    }
    const auto mission =
        invokeNested(0x80153d30U, std::span<const std::uint32_t>{});
    if (mission_index_ == 4U) {
      if (const auto &trace = vm_.runtime().lastWriteTraceHit();
          trace.width != 0U) {
        airbasex_attachment_writer_pc_ = trace.pc;
        airbasex_attachment_writer_instruction_ = trace.instruction;
        airbasex_attachment_writer_value_ = trace.value;
      }
      airbasex_attachment_write_count_ = vm_.runtime().writeTraceCount();
    }
    if (!mission.completed() && !mission.stoppedAtHostBoundary()) {
      return false;
    }
    if (!installSoundServiceCallback()) {
      return false;
    }
    bindMissionPad();
    for (const auto reg : {29U, 30U}) {
      const auto value = vm_.runtime().state().gpr[reg];
      if (value >= 0x80200000U && value < 0x80800000U) {
        vm_.runtime().setRegister(
            static_cast<std::uint8_t>(reg),
            0x80000000U | (value & 0x001fffffU));
      }
    }
    setStage("authored opening");
    opening_stack_ = vm_.runtime().state().gpr[29U];
    auto last_opening_application_state = std::uint32_t{};
    auto opening_display_submissions = std::size_t{};
    for (std::size_t index = 0U; index < 512U; ++index) {
      auto display_submitted = false;
      if (!advanceGuestBoundary(display_submitted)) {
        return false;
      }
      std::uint32_t application_state{};
      if (!vm_.runtime().read32(profile_.application_state,
                                application_state)) {
        return false;
      }
      last_opening_application_state = application_state;
      opening_display_submissions += display_submitted ? 1U : 0U;
      if (display_submitted &&
          presentation_frame_ &&
          presentation_frame_->draw_command_count != 0U &&
          (start_mode_ != Sf2GuestRuntimeStartMode::retail_briefing ||
           presentation_frame_->application_state == 8U)) {
        if (mission_index_ == 4U) {
          if (const auto &trace = vm_.runtime().lastWriteTraceHit();
              trace.width != 0U) {
            airbasex_attachment_writer_pc_ = trace.pc;
            airbasex_attachment_writer_instruction_ = trace.instruction;
            airbasex_attachment_writer_value_ = trace.value;
            const auto code_begin = trace.pc - 8U * 4U;
            for (auto word_index = std::size_t{};
                 word_index < airbasex_attachment_writer_code_.size();
                 ++word_index) {
              static_cast<void>(vm_.runtime().read32(
                  code_begin +
                      static_cast<std::uint32_t>(word_index * 4U),
                  airbasex_attachment_writer_code_[word_index]));
            }
          }
          airbasex_attachment_write_count_ = vm_.runtime().writeTraceCount();
          vm_.runtime().setWriteTrace(0x8014f000U, 0x80169000U);
          vm_.runtime().setWriteTracePc(0x800133b4U, 0x800133dcU);
        }
        return true;
      }
    }
    setStage("authored opening state=" +
             std::to_string(last_opening_application_state) +
             " submissions=" +
             std::to_string(opening_display_submissions) + " state8=" +
             std::to_string(briefing_state8_boundaries_) + "/" +
             std::to_string(briefing_state8_maximum_draws_));
    return false;
  }

  [[nodiscard]] bool driveTitleFrontend() {
    enum class Pulse { waiting, pressed, released };
    auto pulse = Pulse::waiting;
    auto samples = std::size_t{};
    const auto write_pad = [&pulse, &samples](
                               LegacyHostCallContext &context,
                               std::uint32_t record) {
      if (++samples >= 16U && pulse == Pulse::waiting) {
        pulse = Pulse::pressed;
      }
      const auto buttons = static_cast<std::uint16_t>(
          pulse == Pulse::pressed ? 0x0008U : 0U);
      if (!context.write8(record, 0U) ||
          !context.write16(record + 4U, buttons)) {
        context.rejectHostCall();
        return;
      }
      if (pulse == Pulse::pressed && samples >= 18U) {
        pulse = Pulse::released;
      }
      context.continueGuestInstruction();
    };
    vm_.bindHostCall(
        0x80153930U, [&write_pad](LegacyHostCallContext &context) {
          std::uint32_t record{};
          if (!context.read32(context.registerValue(29U) + 0x10U, record) ||
              record == 0U) {
            context.rejectHostCall();
            return;
          }
          write_pad(context, record);
        });
    vm_.bindHostCall(
        0x800222bcU, [&write_pad](LegacyHostCallContext &context) {
          std::uint32_t title_state{};
          const auto index = context.argument(1);
          if ((index != 0U && index != 4U) ||
              !context.read32(0x80156bdcU, title_state) ||
              title_state != 3U) {
            context.continueGuestInstruction();
            return;
          }
          write_pad(context, processed_pad_records_ + index * 60U);
        });
    auto title_seen = false;
    for (std::size_t index = 0U; index < 3'000U; ++index) {
      const auto boundary = runUntilBoundary(profile_.gpu_submission_entry);
      if (!boundary.stoppedAtHostBoundary()) {
        return false;
      }
      std::uint32_t title_state{};
      std::uint32_t application_state{};
      if (!vm_.runtime().read32(0x80156bdcU, title_state) ||
          !vm_.runtime().read32(profile_.application_state,
                                application_state)) {
        return false;
      }
      title_seen = title_seen || title_state == 3U;
      if (!retireBoundary(boundary)) {
        return false;
      }
      if (title_seen && pulse == Pulse::released && samples >= 24U) {
        break;
      }
    }
    static_cast<void>(vm_.unbindHostCall(0x80153930U));
    static_cast<void>(vm_.unbindHostCall(0x800222bcU));
    return title_seen && pulse == Pulse::released;
  }

  void bindMissionPad() {
    if (mission_index_ == 0U) {
      vm_.bindHostCall(
          0x80029c0cU, [this](LegacyHostCallContext &context) {
            if (!loading_confirm_sent_ &&
                start_mode_ == Sf2GuestRuntimeStartMode::gameplay) {
              // COLO shares its loading dispatcher with the unusually early
              // skippable parachute scene. The former bridge forced s0=1,
              // which is the dispatcher's decoded Cross-press condition and
              // consequently skipped the authored scene. Retail's adjacent
              // loading-ready byte reaches the same accepted path without
              // synthesizing input; the dispatcher clears it after setup.
              if (!context.write8(0x8011f684U, 1U)) {
                context.rejectHostCall();
                return;
              }
              loading_confirm_sent_ = true;
            }
            context.continueGuestInstruction();
          });
    }
    vm_.bindHostCall(
        0x800222bcU, [this](LegacyHostCallContext &context) {
          const auto index = context.argument(1);
          if (index != 0U && index != 4U) {
            context.continueGuestInstruction();
            return;
          }
          mission_pad_record_ =
              processed_pad_records_ + index * 60U;
          last_pad_caller_ = context.returnAddress();
          last_pad_index_ = index;
          const auto state =
              index == 0U ? host_pad_ : LegacyHostPadState{};
          if (!writeMissionPadRecord(state)) {
            mission_pad_record_ = 0U;
            context.rejectHostCall();
            return;
          }
          mission_pad_record_ = 0U;
          ++host_pad_samples_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x80029b28U, [this](LegacyHostCallContext &context) {
          std::uint32_t record{};
          if (!context.read32(context.registerValue(29U) + 0x14U, record) ||
              record == 0U) {
            context.continueGuestInstruction();
            return;
          }
          mission_pad_record_ = record;
          ++mission_pad_polls_;
          auto state = host_pad_;
          if (briefing_confirm_pulse_pending_) {
            state.buttons = 0x4000U;
            briefing_confirm_pulse_pending_ = false;
          }
          // Colorado Mountains begins its skippable parachute choreography
          // unusually early. Send its required loading confirmation on the
          // first poll so the release edge retires before the scene becomes
          // skippable; the later generic pulse can land inside that intro.
          constexpr auto loading_confirm_poll = 8U;
          if ((mission_index_ != 0U ||
               start_mode_ == Sf2GuestRuntimeStartMode::retail_briefing) &&
              !loading_confirm_sent_ &&
              mission_pad_polls_ >= loading_confirm_poll &&
              (start_mode_ == Sf2GuestRuntimeStartMode::gameplay ||
               briefing_confirm_requested_)) {
            state.buttons = 0x4000U;
            loading_confirm_sent_ = true;
          }
          if (!writeMissionPadRecord(state)) {
            mission_pad_record_ = 0U;
            context.rejectHostCall();
            return;
          }
          mission_pad_record_ = 0U;
          context.continueGuestInstruction();
        });
  }

  void recordTimelineEvent(
      Sf2GuestTimelineEventKind kind,
      const LegacyHostCallContext &context) noexcept {
    auto &event =
        timeline_events_[timeline_event_count_ % timeline_events_.size()];
    event = {};
    event.kind = kind;
    event.guest_frame = guest_frame_;
    static_cast<void>(vm_.runtime().read32(
        profile_.system_clock, event.system_clock));
    for (auto index = std::size_t{}; index < event.arguments.size();
         ++index) {
      event.arguments[index] =
          context.argument(static_cast<std::uint32_t>(index));
    }
    const auto audio = vm_.audioDiagnostics();
    event.cd_lba = audio.cd_lba;
    event.spu_cd_frames = static_cast<std::uint32_t>(
        std::min<std::size_t>(audio.spu_cd_frames,
                              std::numeric_limits<std::uint32_t>::max()));
    event.cd_reading = audio.cd_reading;
    event.cd_muted = audio.cd_muted;
    event.cd_adpcm_muted = audio.cd_adpcm_muted;
    event.xa_stream_set = audio.xa_stream_set;
    event.xa_file = audio.xa_file;
    event.xa_channel = audio.xa_channel;
    std::uint32_t mission_progress{};
    if (vm_.runtime().read32(0x8011f570U, mission_progress) &&
        mission_progress != 0U) {
      static_cast<void>(vm_.runtime().read32(
          mission_progress + 0x04U,
          event.mission_progress_visible_bits));
    }
    std::uint32_t player_state_root{};
    std::uint32_t player_packed_state_record{};
    if (vm_.runtime().read32(0x8012a574U, player_state_root) &&
        player_state_root != 0U &&
        vm_.runtime().read32(player_state_root + 0x20U,
                             player_packed_state_record) &&
        player_packed_state_record != 0U) {
      static_cast<void>(vm_.runtime().read32(
          player_packed_state_record + 0x540U,
          event.player_packed_state_pointer));
      constexpr auto predicate_operand = std::uint32_t{51U};
      const auto tagged_slot = event.player_packed_state_pointer & 3U;
      const auto bit = predicate_operand + tagged_slot * 8U;
      const auto bitset = event.player_packed_state_pointer & ~3U;
      if (bitset != 0U && vm_.runtime().read32(
                              bitset + (bit >> 5U) * 4U,
                              event.player_packed_state_bit51_word)) {
        event.player_packed_state_bit51 =
            (event.player_packed_state_bit51_word &
             (1U << (bit & 31U))) != 0U;
      }
    }
    const auto &xa = vm_.machine().xaSectorAdmissionDiagnostics();
    event.xa_sectors_received = xa.received;
    event.xa_sectors_admitted = xa.admitted;
    if (kind == Sf2GuestTimelineEventKind::scene_speech_start ||
        kind == Sf2GuestTimelineEventKind::scene_speech_stop) {
      last_scene_speech_timeline_frame_ = guest_frame_;
    }
    ++timeline_event_count_;
  }

  void recordUiTextEvent(
      Sf2GuestUiTextEventKind kind,
      const LegacyHostCallContext &context) noexcept {
    Sf2GuestUiTextEvent event;
    event.kind = kind;
    event.guest_frame = guest_frame_;
    event.return_address = context.returnAddress();
    static_cast<void>(vm_.runtime().read32(
        profile_.system_clock, event.system_clock));
    for (auto index = std::size_t{}; index < event.arguments.size(); ++index) {
      event.arguments[index] =
          context.argument(static_cast<std::uint32_t>(index));
    }
    if (kind != Sf2GuestUiTextEventKind::remove &&
        event.arguments[1U] != 0U) {
      for (auto index = std::size_t{}; index + 1U < event.text.size();
           ++index) {
        std::uint8_t value{};
        if (!context.read8(event.arguments[1U] +
                               static_cast<std::uint32_t>(index),
                           value) ||
            value == 0U) {
          break;
        }
        event.text[index] = static_cast<char>(value);
      }
    }
    const auto timer_text =
        event.text[0U] >= '0' && event.text[0U] <= '9' &&
        event.text[1U] >= '0' && event.text[1U] <= '9' &&
        event.text[2U] == ':' &&
        event.text[3U] >= '0' && event.text[3U] <= '9' &&
        event.text[4U] >= '0' && event.text[4U] <= '9' &&
        event.text[5U] == ':' &&
        event.text[6U] >= '0' && event.text[6U] <= '9' &&
        event.text[7U] >= '0' && event.text[7U] <= '9' &&
        event.text[8U] == '\0';
    if (timer_text) {
      // Text_Update supplies the generation-tagged handle. The preceding
      // create call only carries a template index and must not be mistaken
      // for an object handle.
      if (kind == Sf2GuestUiTextEventKind::update) {
        mission_timer_handle_ =
            static_cast<std::uint16_t>(event.arguments[0U]);
        std::ranges::copy_n(event.text.begin(), mission_timer_text_.size(),
                            mission_timer_text_.begin());
        ++mission_timer_text_updates_;
      }
      return;
    }
    if (kind == Sf2GuestUiTextEventKind::remove &&
        static_cast<std::uint16_t>(event.arguments[0U]) ==
            mission_timer_handle_) {
      mission_timer_handle_ = 0xffffU;
      mission_timer_text_.fill('\0');
      return;
    }
    ui_text_events_[ui_text_event_count_ % ui_text_events_.size()] = event;
    ++ui_text_event_count_;
  }

  [[nodiscard]] bool
  writeMissionPadRecord(const LegacyHostPadState &state) noexcept {
    if (mission_pad_record_ == 0U) {
      return false;
    }
    constexpr std::uint32_t face_horizontal_offset = 0x0cU;
    constexpr std::uint32_t face_vertical_offset = 0x14U;
    constexpr std::uint32_t left_analog_x_offset = 0x1cU;
    constexpr std::uint32_t left_analog_y_offset = 0x24U;
    constexpr std::uint32_t right_analog_x_offset = 0x2cU;
    constexpr std::uint32_t right_analog_y_offset = 0x34U;
    constexpr std::uint16_t triangle = 0x1000U;
    constexpr std::uint16_t circle = 0x2000U;
    constexpr std::uint16_t cross = 0x4000U;
    constexpr std::uint16_t square = 0x8000U;
    const auto buttons = static_cast<std::uint16_t>(
        (state.buttons << 8U) | (state.buttons >> 8U));
    if (!vm_.runtime().write8(mission_pad_record_, 0U) ||
        !vm_.runtime().write8(mission_pad_record_ + 1U, 7U) ||
        !vm_.runtime().write8(mission_pad_record_ + 2U, 1U) ||
        !vm_.runtime().write8(mission_pad_record_ + 3U, 0U) ||
        !vm_.runtime().write16(mission_pad_record_ + 4U, buttons) ||
        !vm_.runtime().write8(mission_pad_record_ + 6U, state.right_x) ||
        !vm_.runtime().write8(mission_pad_record_ + 7U, state.right_y) ||
        !vm_.runtime().write8(mission_pad_record_ + 8U, state.left_x) ||
        !vm_.runtime().write8(mission_pad_record_ + 9U, state.left_y)) {
      return false;
    }
    for (std::uint32_t offset = 0x0aU; offset < 0x3cU; ++offset) {
      if (!vm_.runtime().write8(mission_pad_record_ + offset, 0U)) {
        return false;
      }
    }
    const auto axis_x = [](std::uint8_t value) {
      return static_cast<std::int32_t>(value) - 0x80;
    };
    const auto axis_y = [](std::uint8_t value) {
      return 0x80 - static_cast<std::int32_t>(value);
    };
    const auto face_buttons = state.use_explicit_face_axis_buttons
                                  ? state.face_axis_buttons
                                  : state.buttons;
    const auto face_horizontal =
        ((face_buttons & circle) != 0U ? 0x7f : 0) -
        ((face_buttons & square) != 0U ? 0x7f : 0);
    const auto face_vertical =
        ((face_buttons & triangle) != 0U ? 0x7f : 0) -
        ((face_buttons & cross) != 0U ? 0x7f : 0);
    return vm_.runtime().write32(
               mission_pad_record_ + face_horizontal_offset,
               std::bit_cast<std::uint32_t>(face_horizontal)) &&
           vm_.runtime().write32(
               mission_pad_record_ + face_vertical_offset,
               std::bit_cast<std::uint32_t>(face_vertical)) &&
           vm_.runtime().write32(
               mission_pad_record_ + left_analog_x_offset,
               std::bit_cast<std::uint32_t>(axis_x(state.left_x))) &&
           vm_.runtime().write32(
               mission_pad_record_ + left_analog_y_offset,
               std::bit_cast<std::uint32_t>(axis_y(state.left_y))) &&
           vm_.runtime().write32(
               mission_pad_record_ + right_analog_x_offset,
               std::bit_cast<std::uint32_t>(axis_x(state.right_x))) &&
           vm_.runtime().write32(
               mission_pad_record_ + right_analog_y_offset,
               std::bit_cast<std::uint32_t>(axis_y(state.right_y)));
  }

  GameDisc disc_;
  LegacyGameplayVm vm_;
  std::ofstream ui_instruction_trace_;
  std::vector<UiInstructionTraceRecord> ui_instruction_trace_buffer_;
  bool ui_instruction_trace_control_flow_only_{};
  std::uint64_t ui_instruction_trace_events_{};
  std::uint64_t ui_instruction_trace_max_bytes_{};
  std::uint32_t ui_instruction_trace_begin_clock_{};
  std::uint32_t ui_instruction_trace_end_clock_{
      std::numeric_limits<std::uint32_t>::max()};
  bool ui_instruction_trace_capped_{};
  DiscCdRomMedia cdrom_media_;
  std::uint32_t mission_relative_extent_base_{};
  std::uint32_t mission_relative_extent_sector_count_{};
  bool xa_absolute_disc_active_{};
  bool xa_stream_observed_active_{};
  std::uint32_t mission_index_{};
  Sf2GuestRuntimeStartMode start_mode_{
      Sf2GuestRuntimeStartMode::gameplay};
  bool briefing_confirm_requested_{};
  bool briefing_confirm_pulse_pending_{};
  std::size_t briefing_state8_boundaries_{};
  std::size_t briefing_state8_maximum_draws_{};
  std::uint16_t runtime_selection_{};
  std::string fog_path_;
  std::vector<std::byte> fog_bytes_;
  std::vector<std::byte> init_overlay_;
  std::array<std::byte, protected_renderer_size_>
      protected_renderer_baseline_{};
  std::map<std::string, std::vector<std::byte>> resident_files_;
  std::vector<assets::FogEntry> fog_entries_;
  std::map<std::string, std::vector<std::byte>> fog_files_;
  std::map<std::string, std::vector<std::byte>> nested_files_;
  std::map<std::uint32_t, OpenFile> open_files_;
  std::uint32_t pending_archive_callback_return_{};
  std::array<std::byte, 0x240U> catalog_copy_{};
  LegacyHostPadState host_pad_{};
  bool pc_manual_aim_enabled_{};
  std::int32_t pc_manual_aim_yaw_pending_{};
  std::int32_t pc_manual_aim_pitch_pending_{};
  std::optional<std::int32_t> pc_manual_aim_snap_yaw_;
  std::uint64_t pc_manual_aim_hook_calls_{};
  std::int32_t pc_manual_aim_yaw_command_{};
  std::int32_t pc_manual_aim_pitch_command_{};
  bool pc_chase_pitch_enabled_{};
  bool pc_chase_pitch_valid_{};
  bool pc_chase_scripted_camera_seen_{};
  std::int32_t pc_chase_pitch_pending_{};
  std::int32_t pc_chase_pitch_target_{};
  std::uint32_t pc_chase_camera_base_{};
  std::uint32_t pc_chase_camera_flags_{};
  std::uint64_t pc_chase_interaction_suspensions_{};
  std::uint64_t pc_chase_pitch_hook_calls_{};
  std::int32_t pc_chase_rendered_pitch_{};
  bool pc_chase_yaw_enabled_{};
  std::int32_t pc_chase_yaw_pending_{};
  std::uint64_t pc_chase_yaw_hook_calls_{};
  std::int32_t pc_chase_yaw_command_{};
  std::shared_ptr<const Sf2PresentationFrame> presentation_frame_;
  std::optional<Sf2PresentationFrame> pending_presentation_;
  std::uint32_t pending_presentation_clock_{};
  static constexpr std::size_t maximum_projected_vertices_ = 262'144U;
  std::vector<psx::GteProjectedVertex> projected_vertices_;
  std::vector<psx::GteVertexStoreTrace> projected_vertex_stores_;
  bool projected_vertices_overflow_{};
  bool disable_retail_auxiliary_ui_{};
  std::uint64_t published_sequence_{};
  std::vector<std::uint32_t> gpu_gp0_stream_;
  std::size_t gpu_gp0_scan_{};
  std::array<std::uint32_t, 6U> gpu_draw_environment_words_{};
  std::array<bool, 6U> gpu_draw_environment_valid_{};
  std::vector<Sf2GpuPacket> vram_setup_packets_;
  std::optional<std::vector<Sf2GpuPacket>>
      menu_gameplay_vram_setup_packets_;
  std::uint32_t previous_application_state_{
      std::numeric_limits<std::uint32_t>::max()};
  bool menu_transition_active_{};
  bool pause_menu_lifecycle_active_{};
  bool pause_menu_player_was_present_{};
  std::uint64_t menu_vram_snapshots_{};
  std::uint64_t menu_vram_restores_{};
  std::vector<Sf2GpuPacket> pending_immediate_gpu_packets_;
  std::uint64_t callback_ticks_{};
  std::uint64_t audio_callback_ticks_{};
  std::uint64_t retrace_ticks_{};
  std::uint64_t presentation_sequence_{};
  std::uint64_t guest_frame_{};
  std::uint32_t opening_stack_{};
  std::size_t mission_pad_polls_{};
  std::uint32_t mission_pad_record_{};
  std::uint64_t host_pad_samples_{};
  std::uint32_t last_pad_caller_{};
  std::uint32_t last_pad_index_{};
  std::uint32_t last_restore_caller_{};
  std::int32_t pre_restore_player_x_{};
  std::int32_t pre_restore_player_y_{};
  std::int32_t pre_restore_player_z_{};
  std::uint16_t pre_restore_player_health_{};
  std::uint32_t last_damage_caller_{};
  std::array<std::uint32_t, 4U> last_damage_arguments_{};
  std::uint64_t damage_events_{};
  std::uint32_t last_player_damage_caller_{};
  std::array<std::uint32_t, 8U> last_player_damage_request_{};
  std::uint64_t player_damage_events_{};
  std::uint64_t world_collision_scans_{};
  std::uint32_t last_world_collision_caller_{};
  std::int32_t last_world_collision_object_{};
  std::int32_t last_world_collision_room_{};
  std::uint32_t last_player_floor_request_{};
  std::uint64_t player_floor_probes_{};
  std::uint64_t render_view_adds_{};
  std::uint64_t render_view_removes_{};
  std::uint32_t last_render_view_added_{};
  std::uint32_t last_render_view_removed_{};
  std::uint64_t player_floor_probe_true_{};
  std::uint64_t player_floor_probe_false_{};
  std::uint32_t player_floor_false_streak_{};
  std::uint32_t maximum_player_floor_false_streak_{};
  std::uint16_t last_valid_collision_room_{0xffffU};
  std::uint64_t collision_room_fallbacks_{};
  std::uint16_t last_collision_room_fallback_{};
  std::uint64_t collision_request_fallbacks_{};
  std::uint16_t last_collision_request_fallback_{};
  std::uint64_t player_collision_requests_{};
  std::uint64_t invalid_player_collision_requests_{};
  std::uint64_t renderer_text_repairs_{};
  std::uint32_t last_renderer_text_repair_address_{};
  std::uint32_t last_renderer_text_expected_{};
  std::uint32_t last_renderer_text_actual_{};
  std::uint32_t last_renderer_text_writer_pc_{};
  std::uint32_t last_renderer_text_writer_instruction_{};
  std::uint64_t rejected_renderer_ordering_tables_{};
  std::uint64_t observed_gpu_submissions_{};
  std::uint32_t last_gpu_submission_root_{};
  std::size_t last_gpu_submission_draw_count_{};
  std::size_t last_gpu_submission_packet_count_{};
  std::size_t last_gpu_submission_copy_count_{};
  std::size_t last_gpu_submission_upload_count_{};
  Sf2GpuTransfer last_gpu_submission_copy_{};
  std::uint16_t last_gpu_submission_copy_source_x_{};
  std::uint16_t last_gpu_submission_copy_source_y_{};
  std::uint32_t last_gpu_submission_clock_{};
  std::uint16_t last_gpu_submission_draw_buffer_{};
  std::uint16_t last_gpu_submission_build_buffer_{};
  std::uint32_t last_gpu_submission_first_draw_packet_{};
  std::uint32_t last_gpu_submission_last_draw_packet_{};
  std::array<std::uint64_t, 2U> text_renderer_calls_{};
  std::uint32_t text_renderer_{};
  std::uint16_t text_renderer_flags_{};
  std::array<std::uint32_t, 3U> text_renderer_list_heads_{};
  std::uint64_t hud_primitive_registrations_{};
  std::array<std::uint32_t, 8U> hud_primitive_registration_callers_{};
  std::array<std::uint32_t, 8U> hud_primitive_registration_packets_{};
  std::array<std::uint32_t, 8U> hud_primitive_registration_roots_{};
  std::array<std::uint64_t, 8U> hud_primitive_registration_counts_{};
  std::array<std::uint8_t, 8U> hud_primitive_registration_buffer_masks_{};
  std::uint64_t hud_primitive_writes_{};
  std::array<std::uint32_t, 12U> hud_primitive_writer_pcs_{};
  std::array<std::uint32_t, 12U> hud_primitive_writer_addresses_{};
  std::array<std::uint32_t, 12U> hud_primitive_writer_instructions_{};
  std::array<std::uint64_t, 12U> hud_primitive_writer_counts_{};
  std::array<std::uint8_t, 12U> hud_primitive_writer_buffer_masks_{};
  std::uint32_t last_rejected_renderer_packet_{};
  std::uint32_t last_rejected_renderer_root_{};
  std::uint64_t last_rejected_renderer_frame_{};
  std::uint64_t rejected_renderer_vertex_entries_{};
  std::uint32_t last_rejected_renderer_vertex_cursor_{};
  std::uint32_t last_rejected_renderer_vertex_address_{};
  std::uint64_t clamped_renderer_ordering_table_entries_{};
  std::uint32_t last_renderer_ordering_table_requested_{};
  std::uint32_t last_renderer_ordering_table_clamped_{};
  std::uint32_t last_renderer_ordering_table_base_{};
  std::uint32_t last_renderer_ordering_table_buckets_{};
  std::uint64_t rejected_renderer_list_merges_{};
  std::uint32_t last_rejected_renderer_list_descriptor_{};
  std::uint32_t last_rejected_renderer_list_root_{};
  std::uint32_t last_rejected_renderer_list_cursor_{};
  std::uint32_t last_rejected_renderer_list_tag_{};
  std::uint64_t room_texture_activations_{};
  std::uint64_t room_texture_page_requests_{};
  std::uint64_t room_texture_upload_completions_{};
  std::uint64_t retail_load_image_calls_{};
  std::uint64_t retained_retail_load_images_{};
  std::array<std::uint64_t, 10U> retail_load_image_call_sites_{};
  std::uint64_t unknown_retail_load_image_call_sites_{};
  std::uint64_t rejected_sound_bank_lookups_{};
  std::uint32_t last_rejected_sound_bank_{};
  std::uint32_t last_rejected_sound_bank_table_{};
  std::uint32_t last_rejected_sound_bank_index_{};
  std::uint32_t last_rejected_sound_bank_caller_{};
  std::uint32_t last_rejected_sound_bank_magic_{};
  std::uint16_t last_rejected_sound_bank_entry_count_{};
  std::uint64_t rejected_sound_voice_updates_{};
  std::uint32_t last_rejected_sound_voice_{};
  std::uint32_t last_rejected_sound_voice_caller_{};
  std::uint32_t last_room_texture_activation_{};
  std::uint32_t last_room_texture_page_{};
  std::uint32_t last_room_texture_bank_{};
  std::uint32_t last_retail_load_image_caller_{};
  Sf2GpuTransfer last_retail_load_image_transfer_{};
  std::uint64_t script_archive_loads_{};
  std::uint64_t objective_completion_events_{};
  std::uint32_t last_objective_completion_index_{};
  std::int32_t last_objective_completion_text_{-1};
  std::uint64_t pickup_presentation_events_{};
  std::uint32_t last_pickup_actor_{};
  std::uint32_t last_pickup_text_{};
  std::uint32_t last_pickup_item_{};
  std::array<std::uint32_t, 8U> last_pickup_text_words_{};
  std::array<char, 64U> last_pickup_text_bytes_{};
  std::uint64_t script_level_starts_{};
  std::uint64_t airbasex_hard_difficulty_reads_{};
  std::uint8_t airbasex_hard_difficulty_{};
  std::uint16_t script_program_count_at_start_{};
  std::uint64_t script_start_guest_frame_{};
  std::uint16_t script_active_programs_at_start_check_{};
  bool script_level_active_at_start_check_{};
  std::uint64_t script_dispatches_{};
  std::uint64_t script_event5_dispatches_{};
  std::array<std::uint32_t, 2U> last_script_dispatch_arguments_{};
  std::uint64_t script_dispatch_event_count_{};
  std::array<Sf2GuestScriptDispatchEvent, 128U> script_dispatch_events_{};
  std::uint64_t script_handler_event_count_{};
  std::array<Sf2GuestScriptHandlerEvent, 128U> script_handler_events_{};
  std::optional<std::size_t> pending_script_predicate_event_{};
  std::uint64_t object_event_dispatch_count_{};
  std::array<Sf2GuestObjectEventDispatch, 128U> object_event_dispatches_{};
  std::uint64_t actor_activation_count_{};
  std::array<Sf2GuestActorActivationEvent, 64U> actor_activations_{};
  std::uint64_t airbasex_motion_update_count_{};
  std::array<Sf2GuestMotionUpdateEvent, 16U> airbasex_motion_updates_{};
  std::uint64_t actor_collision_response_count_{};
  std::array<Sf2GuestCollisionResponseEvent, 8U>
      actor_collision_responses_{};
  std::uint32_t last_actor_position_lookup_{};
  std::uint64_t airbasex_actor_removal_count_{};
  std::uint16_t airbasex_actor_removal_source_{};
  std::uint32_t airbasex_actor_target_before_{};
  std::uint32_t airbasex_actor_target_word_before_{};
  std::uint32_t airbasex_actor_target_after_{};
  std::uint64_t airbasex_bounds_update_count_{};
  std::uint32_t airbasex_bounds_update_caller_{};
  std::uint32_t airbasex_bounds_update_instance_{};
  std::int16_t airbasex_bounds_minimum_y_{};
  std::array<std::uint32_t, 8U> airbasex_bounds_instance_words_{};
  std::array<std::uint32_t, 16U> airbasex_bounds_physics_words_{};
  std::array<std::uint64_t, 8U> auxiliary_packet_cursor_calls_{};
  std::array<std::uint32_t, 8U> auxiliary_packet_cursor_minimum_{};
  std::array<std::uint32_t, 8U> auxiliary_packet_cursor_maximum_{};
  std::array<std::uint32_t, 8U> auxiliary_packet_output_maximum_{};
  std::uint32_t airbasex_attachment_writer_pc_{};
  std::uint32_t airbasex_attachment_writer_instruction_{};
  std::uint32_t airbasex_attachment_writer_value_{};
  std::uint64_t airbasex_attachment_write_count_{};
  std::array<std::uint32_t, 16U> airbasex_attachment_writer_code_{};
  std::uint64_t airbasex_attachment_init_calls_{};
  std::array<std::uint32_t, 4U> airbasex_attachment_init_arguments_{};
  std::uint64_t airbasex_attachment_link_calls_{};
  std::array<std::uint32_t, 4U> airbasex_attachment_link_arguments_{};
  std::uint32_t airbasex_attachment_existing_link_{};
  std::uint32_t airbasex_attachment_node_{};
  std::uint32_t airbasex_attachment_node_flags_{};
  std::uint64_t airbasex_actor_collision_request_count_{};
  std::uint32_t airbasex_actor_collision_request_caller_{};
  std::uint32_t airbasex_actor_collision_request_object_{};
  std::uint32_t airbasex_actor_collision_request_room_{};
  std::uint64_t airbasex_source123_matrix_copies_{};
  std::uint32_t airbasex_source123_matrix_copy_source_{};
  std::uint32_t airbasex_source123_matrix_copy_caller_{};
  std::int32_t airbasex_source123_matrix_copy_y_{};
  std::uint32_t airbasex_source123_local_writer_caller_{};
  std::uint64_t script_program_dispatches_{};
  std::uint64_t script_activations_{};
  std::uint32_t last_script_activation_program_{};
  std::vector<std::uint32_t> active_script_programs_;
  std::uint64_t scene_xa_archive_opens_{};
  std::uint64_t scene_speech_starts_{};
  std::uint64_t scene_speech_callbacks_{};
  std::uint64_t scene_speech_stops_{};
  std::uint8_t scene_speech_stage_{};
  std::uint8_t scene_speech_io_ready_{};
  std::array<std::uint32_t, 4U> last_scene_speech_arguments_{};
  std::array<std::uint32_t, 4U>
      last_scene_speech_callback_arguments_{};
  std::array<std::uint32_t, 4U> last_scene_speech_stop_arguments_{};
  std::uint64_t spatial_sound_starts_{};
  std::uint64_t scene_sound_cue_plays_{};
  std::uint64_t xa_cue_plays_{};
  std::uint64_t xa_stream_starts_{};
  std::uint64_t xa_stream_stops_{};
  std::uint64_t timeline_event_count_{};
  std::array<Sf2GuestTimelineEvent, 128U> timeline_events_{};
  std::optional<std::uint64_t> last_scene_speech_timeline_frame_;
  std::uint64_t ui_text_event_count_{};
  std::array<Sf2GuestUiTextEvent, 64U> ui_text_events_{};
  std::uint16_t mission_timer_handle_{0xffffU};
  std::array<char, 9U> mission_timer_text_{};
  std::uint64_t mission_timer_text_updates_{};
  std::uint64_t async_file_services_{};
  std::uint64_t async_file_completions_{};
  std::uint32_t last_async_completion_caller_{};
  std::uint64_t device_wait_scheduler_slices_{};
  std::uint32_t xa_status_source_{};
  std::uint32_t xa_status_result_{};
  std::optional<QuickState> quick_state_;
  Sf2GuestRuntimeDiagnostics diagnostics_{};
  bool catalog_copied_{};
  bool suppress_interrupts_{};
  bool realtime_display_clock_{};
  bool scripts_started_{};
  bool loading_confirm_sent_{};
  bool checkpoint_captured_{};
  std::uint64_t checkpoint_capture_frame_{};
  std::uint64_t retail_checkpoint_capture_calls_{};
  bool retail_checkpoint_capture_pending_{};
  bool mission_restart_requested_{};
  bool quit_to_title_requested_{};
  bool scripted_camera_observed_{};
  bool initial_checkpoint_deferred_by_opening_event_{};
  bool retail_restore_active_{};
  std::uint64_t retail_restore_start_frame_{};
  std::uint64_t alpha_checkpoint_restores_{};
  std::uint64_t checkpoint_audio_discarded_frames_{};
  bool mission_success_pending_{};
  bool mission_complete_requested_{};
  std::uint64_t mission_success_events_{};
  std::uint64_t mission_failure_events_{};
  std::uint64_t campaign_advance_calls_{};
  std::uint64_t movie_request_calls_{};
  std::uint64_t movie_playback_init_calls_{};
  std::uint64_t scripted_movie_handoffs_{};
  std::uint32_t last_scripted_movie_catalog_index_{0xffffffffU};
  std::optional<std::uint8_t> pending_scripted_movie_catalog_index_;
  std::optional<std::uint8_t> active_scripted_movie_catalog_index_;
  std::uint32_t selected_movie_catalog_index_{0xffffffffU};
  std::array<std::uint32_t, 4U> last_movie_request_arguments_{};
  std::array<std::uint32_t, 4U> last_movie_playback_arguments_{};
  std::uint64_t movie_selection_writes_{};
  std::uint32_t last_movie_selection_writer_pc_{};
  std::uint32_t last_movie_selection_writer_instruction_{};
  std::uint32_t last_movie_selection_write_value_{};
  std::array<std::uint32_t, 4U> movie_selection_writer_pcs_{};
  std::array<std::uint32_t, 4U> movie_selection_write_values_{};
  std::array<std::uint32_t, 8U> movie_playback_catalog_history_{};
  bool completion_flow_trace_active_{};
  bool scripted_movie_host_yielded_{};
  bool ready_{};
  bool faulted_{};
  std::string stage_{"construction"};
  std::string fault_detail_;
  std::string scheduler_fault_detail_;
};

Sf2GuestMissionRuntime::Sf2GuestMissionRuntime(
    const std::filesystem::path &cue_path, std::uint32_t mission_index,
    Sf2GuestRuntimeStartMode start_mode)
    : impl_(std::make_unique<Impl>(cue_path, mission_index, start_mode)) {}

Sf2GuestMissionRuntime::~Sf2GuestMissionRuntime() = default;

bool Sf2GuestMissionRuntime::ready() const noexcept {
  return impl_->ready();
}

bool Sf2GuestMissionRuntime::faulted() const noexcept {
  return impl_->faulted();
}

std::string_view Sf2GuestMissionRuntime::faultDetail() const noexcept {
  return impl_->faultDetail();
}

void Sf2GuestMissionRuntime::setHostPadState(
    const LegacyHostPadState &state) noexcept {
  impl_->setHostPadState(state);
}

void Sf2GuestMissionRuntime::requestRetailBriefingConfirm() noexcept {
  impl_->requestRetailBriefingConfirm();
}

void Sf2GuestMissionRuntime::setRetailAuxiliaryUiEnabled(
    bool enabled) noexcept {
  impl_->setRetailAuxiliaryUiEnabled(enabled);
}

bool Sf2GuestMissionRuntime::dispatchScriptEventForProbe(
    std::uint32_t event, std::uint32_t selector) noexcept {
  return impl_->dispatchScriptEventForProbe(event, selector);
}

bool Sf2GuestMissionRuntime::activateScriptProgramForProbe(
    std::string_view name) noexcept {
  return impl_->activateScriptProgramForProbe(name);
}

bool Sf2GuestMissionRuntime::startSceneSpeechForProbe(
    std::uint16_t cue) noexcept {
  return impl_->startSceneSpeechForProbe(cue);
}

bool Sf2GuestMissionRuntime::stopSceneSpeechForProbe() noexcept {
  return impl_->stopSceneSpeechForProbe();
}

bool Sf2GuestMissionRuntime::setPlayerPositionForProbe(
    std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
  return impl_->setPlayerPositionForProbe(x, y, z);
}

bool Sf2GuestMissionRuntime::setPlayerRoomForProbe(
    std::uint16_t room) noexcept {
  return impl_->setPlayerRoomForProbe(room);
}

bool Sf2GuestMissionRuntime::exerciseRawCdSyncWaitForProbe() noexcept {
  return impl_->exerciseRawCdSyncWaitForProbe();
}

bool Sf2GuestMissionRuntime::exerciseCleanMissionRestartForProbe() noexcept {
  return impl_->exerciseCleanMissionRestartForProbe();
}

bool Sf2GuestMissionRuntime::exerciseOpeningMissionRestartForProbe() noexcept {
  return impl_->exerciseOpeningMissionRestartForProbe();
}

bool Sf2GuestMissionRuntime::exercisePauseMenuLifecycleForProbe(
    bool save_and_quit) noexcept {
  return impl_->exercisePauseMenuLifecycleForProbe(save_and_quit);
}

bool Sf2GuestMissionRuntime::setPlayerHealthForProbe(
    std::uint16_t health) noexcept {
  return impl_->setPlayerHealthForProbe(health);
}

bool Sf2GuestMissionRuntime::alignPlayerAimToLockedTarget() noexcept {
  return impl_->alignPlayerAimToLockedTarget();
}

void Sf2GuestMissionRuntime::setPcManualAimInput(
    std::int32_t yaw_delta, std::int32_t pitch_delta,
    bool enabled) noexcept {
  impl_->setPcManualAimInput(yaw_delta, pitch_delta, enabled);
}

void Sf2GuestMissionRuntime::setPcChaseCameraYawInput(
    std::int32_t delta, bool enabled) noexcept {
  impl_->setPcChaseCameraYawInput(delta, enabled);
}

void Sf2GuestMissionRuntime::setPcChaseCameraPitchInput(
    std::int32_t delta, bool enabled) noexcept {
  impl_->setPcChaseCameraPitchInput(delta, enabled);
}

void Sf2GuestMissionRuntime::setPcHorizontalProjectionScale(
    std::uint32_t scale_q16) noexcept {
  impl_->setPcHorizontalProjectionScale(scale_q16);
}

bool Sf2GuestMissionRuntime::setObjectRecordHealthForProbe(
    std::uint16_t source_index, std::int16_t health) noexcept {
  return impl_->setObjectRecordHealthForProbe(source_index, health);
}

std::optional<Sf2GuestObjectProbeState>
Sf2GuestMissionRuntime::objectStateForProbe(
    std::uint16_t source_index) const noexcept {
  return impl_->objectStateForProbe(source_index);
}

bool Sf2GuestMissionRuntime::traceObjectMatrixYForProbe(
    std::uint16_t source_index) noexcept {
  return impl_->traceObjectMatrixYForProbe(source_index);
}

std::optional<std::uint16_t>
Sf2GuestMissionRuntime::scriptProgramVariableForProbe(
    std::string_view name, std::uint16_t index) noexcept {
  return impl_->scriptProgramVariableForProbe(name, index);
}

bool Sf2GuestMissionRuntime::setMissionProgressBitForProbe(
    std::uint16_t bit, bool enabled) noexcept {
  return impl_->setMissionProgressBitForProbe(bit, enabled);
}

bool Sf2GuestMissionRuntime::startPlayerObjectInteractionForProbe(
    std::uint32_t selector) noexcept {
  return impl_->startPlayerObjectInteractionForProbe(selector);
}

bool Sf2GuestMissionRuntime::requestScriptedMovieForProbe(
    std::uint8_t catalog_index) noexcept {
  return impl_->requestScriptedMovieForProbe(catalog_index);
}

std::optional<std::uint8_t>
Sf2GuestMissionRuntime::consumeScriptedMovieRequest() noexcept {
  return impl_->consumeScriptedMovieRequest();
}

bool Sf2GuestMissionRuntime::completeScriptedMovie(
    std::uint8_t catalog_index) noexcept {
  return impl_->completeScriptedMovie(catalog_index);
}

bool Sf2GuestMissionRuntime::requestMissionSuccessForProbe() noexcept {
  return impl_->requestMissionSuccessForProbe();
}

bool Sf2GuestMissionRuntime::resumeMissionShellForProbe() noexcept {
  return impl_->resumeMissionShellForProbe();
}

bool Sf2GuestMissionRuntime::applyCampaignCarryState(
    const CampaignCarryState &state) noexcept {
  return impl_->applyCampaignCarryState(state);
}

bool Sf2GuestMissionRuntime::advanceHostUpdate() noexcept {
  return impl_->advanceHostUpdate();
}

bool Sf2GuestMissionRuntime::advanceRetailBriefingAudioSlice(
    bool dispatch_sound_callback) noexcept {
  return impl_->advanceRetailBriefingAudioSlice(dispatch_sound_callback);
}

bool Sf2GuestMissionRuntime::missionCompleteRequested() const noexcept {
  return impl_->missionCompleteRequested();
}

bool Sf2GuestMissionRuntime::missionRestartRequested() const noexcept {
  return impl_->missionRestartRequested();
}

bool Sf2GuestMissionRuntime::quitToTitleRequested() const noexcept {
  return impl_->quitToTitleRequested();
}

const std::shared_ptr<const Sf2PresentationFrame> &
Sf2GuestMissionRuntime::presentationFrame() const noexcept {
  return impl_->presentationFrame();
}

std::size_t Sf2GuestMissionRuntime::takePcm(
    std::span<psx::SpuPcmFrame> destination) noexcept {
  return impl_->takePcm(destination);
}

void Sf2GuestMissionRuntime::clearPcm() noexcept {
  impl_->clearPcm();
}

std::uint64_t Sf2GuestMissionRuntime::inputSampleCount() const noexcept {
  return impl_->inputSampleCount();
}

Sf2GuestRuntimeDiagnostics
Sf2GuestMissionRuntime::diagnostics() const noexcept {
  return impl_->diagnostics();
}

bool Sf2GuestMissionRuntime::captureQuickState() noexcept {
  return impl_->captureQuickState();
}

bool Sf2GuestMissionRuntime::restoreQuickState() noexcept {
  return impl_->restoreQuickState();
}

bool Sf2GuestMissionRuntime::hasQuickState() const noexcept {
  return impl_->hasQuickState();
}

core::Sha256Digest
Sf2GuestMissionRuntime::guestRamDigestForProbe() const noexcept {
  return impl_->guestRamDigestForProbe();
}

bool Sf2GuestMissionRuntime::copyGuestRamForProbe(
    std::span<std::byte> destination) const noexcept {
  return impl_->copyGuestRamForProbe(destination);
}

const GameRuntimeProfile &sf2RuntimeProfile() noexcept {
  static constexpr GameRuntimeProfile profile{
      .game = GameId::syphon_filter_2,
      .kind = GameRuntimeKind::sf2,
      .hud_atlas = HudAtlasKind::sf2,
      .emd_vertex_index_stride = 3U,
      .hmd_vertex_index_stride = 0U,
      .first_person_eye_height = 220.0,
      .player_collision_radius = 32.0,
      .player_collision_height = 260.0,
      .uses_legacy_guest_runtime = false,
      .supports_native_mission_items = true,
      .supports_native_mission_interactions = true,
      .uses_sf1_environment_atlas = false,
  };
  return profile;
}

std::optional<WeaponId> sf2WeaponForItem(std::uint8_t item) noexcept {
  switch (item) {
  case 1U:
    return WeaponId::silenced_9mm;
  case 2U:
    return WeaponId::pistol_9mm;
  case 3U:
    return WeaponId::pistol_45;
  case 4U:
    return WeaponId::m_16;
  case 5U:
  case 6U:
    return WeaponId::hk_5;
  case 7U:
    return WeaponId::pk_102;
  case 8U:
    return WeaponId::shotgun;
  case 9U:
    return WeaponId::combat_shotgun;
  case 10U:
    return WeaponId::g_18;
  case 11U:
    return WeaponId::biz_2;
  case 12U:
    return WeaponId::k3g4;
  case 14U:
  case 15U:
    return WeaponId::sniper_rifle;
  case 16U:
    return WeaponId::nightvision_rifle;
  case 18U:
  case 19U:
    return WeaponId::taser;
  case 20U:
    return WeaponId::knife;
  case 21U:
    return WeaponId::m_79;
  case 22U:
    return WeaponId::fragmentation_grenade;
  case 23U:
    return WeaponId::gas_grenade;
  case 24U:
    return WeaponId::c4_explosives;
  default:
    // H11, crossbow and non-weapon mission tools do not yet have a native
    // representation. Never alias them to similarly numbered SF1 slots.
    return std::nullopt;
  }
}

} // namespace sf::game
