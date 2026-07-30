#include "sf/assets/fog_archive.hpp"
#include "sf/core/error.hpp"
#include "sf/game/disc_cdrom_media.hpp"
#include "sf/game/embedded_hog.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/legacy_gameplay_vm.hpp"
#include "sf/game/runtime_profile.hpp"
#include "sf/game/sf2_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <map>
#include <string>

namespace sf::game {

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
  auto width = static_cast<std::uint16_t>(size_word & 0xffffU);
  auto height = static_cast<std::uint16_t>(size_word >> 16U);
  // GP0 encodes zero as the maximum transfer dimension.
  width = width == 0U ? 1024U : width;
  height = height == 0U ? 512U : height;
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
      .x = static_cast<std::uint16_t>(coordinate_word & 0xffffU),
      .y = static_cast<std::uint16_t>(coordinate_word >> 16U),
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
  if (guest_ram.size() != psx_ram_size || sequence == 0U ||
      (ordering_table_root & 3U) != 0U) {
    return std::nullopt;
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
      return std::nullopt;
    }
    visited.push_back(physical_address);

    std::uint32_t tag{};
    if (!read_word(address, tag)) {
      return std::nullopt;
    }
    const auto word_count = static_cast<std::size_t>(tag >> 24U);
    if (frame.gp0_word_count > maximum_gp0_words - word_count) {
      return std::nullopt;
    }
    if (word_count != 0U) {
      Sf2GpuPacket packet;
      packet.guest_address = 0x80000000U | physical_address;
      packet.gp0_words.reserve(word_count);
      for (auto word_index = std::size_t{}; word_index < word_count;
           ++word_index) {
        std::uint32_t word{};
        if (!read_word(address + static_cast<std::uint32_t>(
                                     (word_index + 1U) * 4U),
                       word)) {
          return std::nullopt;
        }
        packet.gp0_words.push_back(word);
      }
      const auto opcode = packet.gp0_words.front() >> 24U;
      frame.draw_command_count += opcode >= 0x20U && opcode <= 0x7fU ? 1U : 0U;
      frame.gp0_word_count += word_count;
      ++frame.gpu_command_count;
      frame.packets.push_back(std::move(packet));
    }

    const auto next = tag & dma_end;
    if (next == dma_end) {
      return frame.valid()
                 ? std::optional<Sf2PresentationFrame>{std::move(frame)}
                 : std::nullopt;
    }
    address = 0x80000000U | next;
  }
  return std::nullopt;
}

void projectSf2GuestHud(
    GameplayHud &hud,
    const Sf2GuestRuntimeDiagnostics &guest) noexcept {
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
  hud.setVitals(PlayerVitals{
      .health = guest.player_health,
      .maximum_health = 150U,
      .armor = guest.player_armor,
      .maximum_armor = 600U,
  });
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
          static_cast<std::uint64_t>(pending_) + count, 16U));
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

class Sf2GuestMissionRuntime::Impl final {
public:
  Impl(const std::filesystem::path &cue_path, std::uint32_t mission_index)
      : disc_(GameDisc::open(cue_path)), vm_(disc_.executable()),
        cdrom_media_(disc_.image()) {
    try {
      if (!disc_.game() || disc_.game()->id != GameId::syphon_filter_2 ||
          disc_.game()->disc_number != 1U || mission_index != 2U) {
        markFault("SF2 guest alpha currently requires Disc 1 Mission 3");
        return;
      }
      loadAssets();
      bindPlatform();
      // Arm before TITLE/HWAY transition work: bootstrap itself builds and
      // submits ordering tables, and a bad tag written here can remain
      // dormant until gameplay later enters the damaged renderer branch.
      vm_.runtime().setWriteWatch(0x8001d000U, 0x8001f000U);
      vm_.runtime().addWriteWatch(0x80016000U, 0x80018000U);
      if (!bootstrap()) {
        if (!faulted_) {
          markFault("retail TITLE-to-HWAY bootstrap failed at " + stage_);
        }
        return;
      }
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
      realtime_display_clock_ = true;
      vm_.clearPcm();
      ready_ = true;
    } catch (const std::exception &error) {
      markFault(error.what());
    }
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
  [[nodiscard]] const std::shared_ptr<const Sf2PresentationFrame> &
  presentationFrame() const noexcept {
    return presentation_frame_;
  }
  [[nodiscard]] std::size_t
  takePcm(std::span<psx::SpuPcmFrame> destination) noexcept {
    return vm_.takePcm(destination);
  }
  void clearPcm() noexcept { vm_.clearPcm(); }
  [[nodiscard]] std::uint64_t inputSampleCount() const noexcept {
    return host_pad_samples_;
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
    readPlayerState(result.player_instance, result.player_x, result.player_y,
                    result.player_z, result.player_health,
                    result.player_armor);
    readPlayerHudState(result.player_instance, result);
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
    const auto audio = vm_.audioDiagnostics();
    result.spu_mixed_frames = audio.spu_mixed_frames;
    const auto &spu_state = vm_.machine().spu().state();
    result.spu_key_on_writes = spu_state.key_on_writes;
    result.spu_key_off_writes = spu_state.key_off_writes;
    result.spu_last_key_on_mask = spu_state.last_key_on_mask;
    result.spu_last_key_off_mask = spu_state.last_key_off_mask;
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
    result.script_archive_loads = script_archive_loads_;
    static_cast<void>(vm_.runtime().read16(
        state.gpr[28U] + 0x0db0U, result.script_program_count));
    constexpr std::uint32_t program_table = 0x8013c030U;
    static_cast<void>(
        vm_.runtime().read32(program_table + 7U * 4U,
                             result.script_level_program));
    if (result.script_level_program != 0U) {
      static_cast<void>(vm_.runtime().read32(
          result.script_level_program + 0x14U,
          result.script_level_name_pointer));
      for (auto index = std::size_t{};
           index < result.script_level_name_words.size(); ++index) {
        static_cast<void>(vm_.runtime().read32(
            result.script_level_name_pointer +
                static_cast<std::uint32_t>(index * 4U),
            result.script_level_name_words[index]));
        static_cast<void>(vm_.runtime().read32(
            state.gpr[28U] + 0x06e8U +
                static_cast<std::uint32_t>(index * 4U),
            result.script_lookup_name_words[index]));
      }
    }
    result.script_level_starts = script_level_starts_;
    result.script_dispatches = script_dispatches_;
    result.script_program_dispatches = script_program_dispatches_;
    result.script_activations = script_activations_;
    result.scene_xa_archive_opens = scene_xa_archive_opens_;
    result.scene_speech_starts = scene_speech_starts_;
    result.scene_speech_callbacks = scene_speech_callbacks_;
    result.scene_speech_stage = scene_speech_stage_;
    result.scene_speech_io_ready = scene_speech_io_ready_;
    result.last_scene_speech_arguments = last_scene_speech_arguments_;
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
    result.async_file_services = async_file_services_;
    result.async_file_completions = async_file_completions_;
    result.last_async_completion_caller = last_async_completion_caller_;
    result.input_samples = host_pad_samples_;
    result.checkpoint_restores = alpha_checkpoint_restores_;
    static_cast<void>(
        vm_.runtime().read32(profile_.application_state,
                             result.application_state));
    static_cast<void>(
        vm_.runtime().read32(profile_.system_clock, result.system_clock));
    return result;
  }

  [[nodiscard]] bool advanceHostUpdate() noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    // DrawOTag is also used for utility lists between display submissions.
    // Advance through those boundaries atomically so one public update always
    // means one retail 60 Hz presentation frame. SF2 derives its 20 Hz logic
    // and PAD cadence from every third presentation frame.
    constexpr auto maximum_boundaries_per_frame = 128U;
    for (auto boundary = 0U; boundary < maximum_boundaries_per_frame;
         ++boundary) {
      auto display_submitted = false;
      if (!advanceGuestBoundary(display_submitted)) {
        return false;
      }
      if (display_submitted) {
        return presentation_frame_ != nullptr;
      }
    }
    markFault("SF2 did not submit a display frame within the boundary limit");
    return false;
  }

private:
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
                              health) ||
        !vm_.runtime().read16(health_controller + health_armor_offset,
                              armor)) {
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
  }

  void readPlayerHudState(
      std::uint32_t instance,
      Sf2GuestRuntimeDiagnostics &result) const noexcept {
    constexpr std::uint32_t instance_inventory_offset = 0x20U;
    constexpr std::uint32_t inventory_owned_offset = 0x3cU;
    constexpr std::uint32_t inventory_ammo_offset = 0x44U;
    constexpr std::uint32_t inventory_equipped_offset = 0xccU;
    if (instance == 0U) {
      return;
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

  [[nodiscard]] static bool
  isAuthoredWorldFrame(const Sf2PresentationFrame &frame) noexcept {
    // The retail mission loader submits a sizeable card-and-line list before
    // the first authored scene.  It has hundreds of draw commands, so a raw
    // draw-count threshold mistakes it for gameplay.  Mission geometry uses
    // the textured gouraud polygon families (0x34/0x36/0x3c/0x3e); require a
    // meaningful population of those before handing a frame to the product.
    const auto world_polygons = std::ranges::count_if(
        frame.packets, [](const Sf2GpuPacket &packet) {
          if (packet.gp0_words.empty()) {
            return false;
          }
          const auto opcode = packet.gp0_words.front() >> 24U;
          return (opcode & 0xfcU) == 0x34U ||
                 (opcode & 0xfcU) == 0x3cU;
        });
    return world_polygons >= 100;
  }

  void captureGpuSideEffects() {
    auto words = vm_.machine().takeGpuGp0Words();
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
      const auto opcode =
          static_cast<std::uint8_t>(gpu_gp0_stream_[gpu_gp0_scan_] >> 24U);
      if (opcode != 0xa0U) {
        ++gpu_gp0_scan_;
        continue;
      }
      if (gpu_gp0_stream_.size() - gpu_gp0_scan_ < 3U) {
        compact_consumed_words();
        return;
      }
      const auto coordinate = gpu_gp0_stream_[gpu_gp0_scan_ + 1U];
      const auto encoded_size = gpu_gp0_stream_[gpu_gp0_scan_ + 2U];
      const auto x = static_cast<std::uint16_t>(coordinate);
      const auto y = static_cast<std::uint16_t>(coordinate >> 16U);
      auto width = static_cast<std::uint16_t>(encoded_size);
      auto height = static_cast<std::uint16_t>(encoded_size >> 16U);
      width = width == 0U ? 1024U : width;
      height = height == 0U ? 512U : height;
      if (x >= 1024U || y >= 512U || width > 1024U || height > 512U) {
        ++gpu_gp0_scan_;
        continue;
      }
      const auto payload_words =
          (static_cast<std::size_t>(width) *
               static_cast<std::size_t>(height) +
           1U) /
          2U;
      const auto packet_words = 3U + payload_words;
      if (gpu_gp0_stream_.size() - gpu_gp0_scan_ < packet_words) {
        compact_consumed_words();
        return;
      }
      Sf2GpuPacket upload;
      upload.gp0_words.assign(
          gpu_gp0_stream_.begin() +
              static_cast<std::ptrdiff_t>(gpu_gp0_scan_),
          gpu_gp0_stream_.begin() +
              static_cast<std::ptrdiff_t>(gpu_gp0_scan_ + packet_words));
      rememberVramSetupPacket(std::move(upload));
      gpu_gp0_scan_ += packet_words;
    }
    compact_consumed_words();
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

  void rememberVramSetupPacket(Sf2GpuPacket packet) {
    const auto transfer = sf2GpuTransfer(packet);
    if (!transfer) {
      return;
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
      if (sf2GpuCommandKind(packet) == Sf2GpuCommandKind::upload_vram &&
          sf2GpuTransfer(packet)) {
        rememberVramSetupPacket(packet);
      }
    }
  }

  [[nodiscard]] bool startMissionScriptsIfReady() noexcept {
    if (scripts_started_) {
      return true;
    }
    constexpr std::uint32_t program_count_address = 0x8011fa14U;
    constexpr std::uint32_t program_table = 0x8013c030U;
    constexpr std::uint16_t hway_program_count = 8U;
    std::uint16_t count{};
    std::uint32_t level_program{};
    if (!vm_.runtime().read16(program_count_address, count) ||
        !vm_.runtime().read32(program_table + 7U * 4U, level_program)) {
      markFault("could not read the SF2 mission-script registry");
      return false;
    }
    if (count != hway_program_count || level_program == 0U) {
      return true;
    }
    const auto reset =
        invokeNested(0x800b3d34U, std::span<const std::uint32_t>{});
    if (!reset.completed() && !reset.stoppedAtHostBoundary()) {
      markFault("SF2 mission-script reset failed");
      return false;
    }
    const std::array activation_arguments{level_program};
    const auto activation =
        invokeNested(0x800b39e4U, activation_arguments);
    if (!activation.completed() && !activation.stoppedAtHostBoundary()) {
      markFault("SF2 LEVEL program activation failed");
      return false;
    }
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
      markFault(
          "resident renderer code write: address=0x" +
          hex(write_watch.address) + " value=0x" + hex(write_watch.value) +
          " width=" + std::to_string(write_watch.width) +
          " writer-pc=0x" + hex(write_watch.pc) +
          " writer-instruction=0x" + hex(write_watch.instruction) +
          " ra=0x" + hex(vm_.runtime().state().gpr[31U]) +
          " frame=" + std::to_string(guest_frame_) +
          " restores=" + std::to_string(alpha_checkpoint_restores_));
      return false;
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
    captureGpuSideEffects();
    std::uint32_t application_state{};
    if (!vm_.runtime().read32(profile_.application_state, application_state)) {
      markFault("could not read SF2 application state");
      return false;
    }
    if (retail_restore_active_ && application_state == 0U &&
        guest_frame_ > retail_restore_start_frame_ + 60U) {
      retail_restore_active_ = false;
      if (!installSoundFlushCallback()) {
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
    if (display_submitted) {
      ++presentation_sequence_;
      ++guest_frame_;
      auto frame = std::move(captured_submission);
      if (frame) {
        captureOrderingTableUploads(*frame);
      }
      // SF2 submits several utility lists between complete display lists,
      // including a large loading-card list. Keep the last authored world/HUD
      // frame until another complete scene is ready.
      if (frame && isAuthoredWorldFrame(*frame)) {
        attachVramSetup(*frame);
        presentation_frame_ =
            std::make_shared<const Sf2PresentationFrame>(std::move(*frame));
      }
    }
    const auto retired = vm_.resumeCurrentPcClockNeutral(1U);
    if (retired.execution.reason !=
        psx::R3000StopReason::instruction_budget) {
      markFault("could not retire SF2 GPU submission");
      return false;
    }
    // Guest execution is clock-neutral after the authored transition. The
    // host display boundary is the hardware clock: utility DrawOTag calls do
    // not consume an additional video interval, while each published frame
    // advances exactly one 60 Hz slice.
    const auto scheduler_ticks =
        realtime_display_clock_
            ? (display_submitted ? retrace_period_ : 0U)
            : (display_submitted &&
                       boundary.execution.instructions < retrace_period_
                   ? retrace_period_ - boundary.execution.instructions +
                         retired.execution.instructions
                   : retired.execution.instructions);
    if (!serviceScheduler(scheduler_ticks)) {
      markFault("SF2 device callback scheduler failed");
      return false;
    }
    if (!startMissionScriptsIfReady()) {
      return false;
    }
    if (application_state == 0U && !checkpoint_captured_) {
      const auto checkpoint =
          invokeNested(0x800ad48cU, std::span<const std::uint32_t>{});
      if (!checkpoint.completed() && !checkpoint.stoppedAtHostBoundary()) {
        markFault("SF2 initial checkpoint capture failed");
        return false;
      }
      checkpoint_captured_ = checkpointStateReady();
    }
    return true;
  }

  [[nodiscard]] bool checkpointStateReady() const noexcept {
    constexpr std::uint32_t state = 0x8013b27cU;
    constexpr std::uint32_t size = 0x848U;
    std::uint32_t first{};
    if (!vm_.runtime().read32(state, first) ||
        (first & 0x8000U) == 0U) {
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

  struct OpenFile {
    const std::vector<std::byte> *bytes{};
    std::size_t offset{};
  };

  static constexpr auto profile_ = sf2UsaGuestRuntimeProfile();
  static constexpr std::uint64_t scheduler_slice_budget_ = 50'000U;
  static constexpr std::uint64_t execution_budget_ = 100'000'000U;
  static constexpr std::uint64_t callback_period_ =
      psx::CdRomController::cpu_clock_hz /
      LegacyGameplayVm::updates_per_second;
  static constexpr std::uint64_t audio_callback_period_ =
      psx::CdRomController::cpu_clock_hz / 120U;
  static constexpr std::uint64_t retrace_period_ =
      psx::CdRomController::cpu_clock_hz / 60U;
  static constexpr std::uint32_t callback_stack_ = 0x807f0000U;
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

  void setStage(std::string_view stage) {
    stage_.assign(stage);
  }

  void loadAssets() {
    fog_bytes_ = disc_.image().readFile("FOG/HWAY.FOG");
    init_overlay_ = disc_.image().readFile("BIN/INIT.OVL");
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
  }

  void bindPlatform() {
    vm_.machine().setCdRomMedia(&cdrom_media_);
    vm_.bindPsxBiosCoreVector(true);
    installExceptionBridge();
    // The guest's synchronous drain spins inside one emulated instruction
    // slice, while the host CD device advances between slices. Leave queued
    // requests with the retail asynchronous owner instead of deadlocking the
    // application-state transition.
    vm_.bindHostCall(0x800a3ed0U,
                     [](LegacyHostCallContext &context) {
                       context.setReturnValue(0U);
                     });
    vm_.bindHostCall(
        0x800ad9f4U, [this](LegacyHostCallContext &context) {
          last_restore_caller_ = context.registerValue(31U);
          std::uint32_t player{};
          std::uint16_t armor{};
          readPlayerState(player, pre_restore_player_x_,
                          pre_restore_player_y_, pre_restore_player_z_,
                          pre_restore_player_health_, armor);
          ++alpha_checkpoint_restores_;
          retail_restore_active_ = true;
          retail_restore_start_frame_ = guest_frame_;
          // The corrected CD/XA scheduler can now service the retail
          // checkpoint loader. Preserve this boundary as an observation hook
          // and execute the original restore instead of rewinding a host
          // whole-machine snapshot from an unrelated instruction boundary.
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
        0x80078c24U, [this](LegacyHostCallContext &context) {
          const auto request = context.argument(0U);
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
        0x80164fdcU, [this](LegacyHostCallContext &context) {
          ++script_archive_loads_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b3d34U, [this](LegacyHostCallContext &context) {
          ++script_level_starts_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x800b38b0U, [this](LegacyHostCallContext &context) {
          ++script_dispatches_;
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
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008d8a8U, [this](LegacyHostCallContext &context) {
          ++scene_speech_callbacks_;
          context.continueGuestInstruction();
        });
    vm_.bindHostCall(
        0x8008dc9cU, [this](LegacyHostCallContext &context) {
          scene_speech_io_ready_ =
              static_cast<std::uint8_t>(context.registerValue(2U) != 0U);
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
          ++xa_stream_starts_;
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
            const auto entry = disc_.image().find("FOG/HWAY.FOG");
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
            const auto movie = disc_.image().find("MOVIE1.HOG");
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
          if (entry == fog_entries_.end() || file == fog_files_.end()) {
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
          const auto fog = disc_.image().find("FOG/HWAY.FOG");
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

  [[nodiscard]] bool serviceScheduler(std::uint64_t ticks) noexcept {
    if (suppress_interrupts_) {
      vm_.machine().advanceHardwareTicks(ticks);
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
    if (!vm_.servicePsxCdReadyCallback()) {
      scheduler_fault_detail_ = "CD ready callback";
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
    return true;
  }

  [[nodiscard]] bool installSoundFlushCallback() noexcept {
    constexpr std::uint32_t sound_flush = 0x80104b40U;
    constexpr auto sound_callback_slot =
        profile_.interrupt_callback_table + 6U * 4U;
    return vm_.runtime().write32(sound_callback_slot, sound_flush);
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
    scheduler_fault_detail_.clear();
    LegacyGameplayVmResult result;
    for (;;) {
      const auto slice = std::min(remaining, scheduler_slice_budget_);
      result = suppress_interrupts_
                   ? vm_.runCurrentPcUntilHostBoundaryClockNeutral(address,
                                                                   slice)
                   : vm_.runCurrentPcUntilHostBoundary(address, slice);
      total += result.execution.instructions;
      const auto synchronous_wait =
          realtime_display_clock_ &&
          result.execution.reason == psx::R3000StopReason::instruction_budget &&
          retail_restore_active_;
      // Mission failure waits for two asynchronous retraces by polling
      // VSync(-1) at 0x80014D8C..0x80014DBC. No DrawOTag is submitted while
      // that loop is active, so the product-owned display clock must advance
      // from instruction slices or the retail transition can never reach the
      // checkpoint loader.
      const auto lifecycle_vsync_wait =
          realtime_display_clock_ &&
          result.execution.reason ==
              psx::R3000StopReason::instruction_budget &&
          result.execution.pc >= 0x80014d8cU &&
          result.execution.pc <= 0x80014dc0U;
      const auto scheduler_ticks =
          !realtime_display_clock_ || synchronous_wait ||
                  lifecycle_vsync_wait
              ? result.execution.instructions
              : 0U;
      if (!serviceScheduler(scheduler_ticks)) {
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
           serviceScheduler(retired.execution.instructions + padding);
  }

  [[nodiscard]] bool bootstrap() {
    setStage("executable state loop");
    const auto state_loop =
        vm_.runCurrentPcUntilHostBoundary(profile_.state_loop_entry,
                                           execution_budget_);
    if (!state_loop.stoppedAtHostBoundary() || !serviceScheduler(0U)) {
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
    if (!vm_.runtime().write32(0x801582d4U, 2U) ||
        !vm_.runtime().write32(0x80156bdcU, 17U) ||
        !vm_.runtime().write32(0x8011f61cU, 1U)) {
      return false;
    }
    const auto fog = disc_.image().find("FOG/HWAY.FOG");
    cdrom_media_.mapRelativeExtent(
        fog.extent_lba,
        (fog.size + assets::FogArchive::sector_size - 1U) /
            assets::FogArchive::sector_size);
    setStage("mission archive handoff");
    const auto mission =
        invokeNested(0x80153d30U, std::span<const std::uint32_t>{});
    if (!mission.completed() && !mission.stoppedAtHostBoundary()) {
      return false;
    }
    if (!installSoundFlushCallback()) {
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
    for (std::size_t index = 0U; index < 512U; ++index) {
      auto display_submitted = false;
      if (advanceGuestBoundary(display_submitted) && display_submitted &&
          presentation_frame_ &&
          presentation_frame_->draw_command_count != 0U) {
        return true;
      }
    }
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
          if (!loading_confirm_sent_ && mission_pad_polls_ >= 8U) {
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
  DiscCdRomMedia cdrom_media_;
  std::vector<std::byte> fog_bytes_;
  std::vector<std::byte> init_overlay_;
  std::map<std::string, std::vector<std::byte>> resident_files_;
  std::vector<assets::FogEntry> fog_entries_;
  std::map<std::string, std::vector<std::byte>> fog_files_;
  std::map<std::uint32_t, OpenFile> open_files_;
  std::array<std::byte, 0x240U> catalog_copy_{};
  LegacyHostPadState host_pad_{};
  std::shared_ptr<const Sf2PresentationFrame> presentation_frame_;
  std::vector<std::uint32_t> gpu_gp0_stream_;
  std::size_t gpu_gp0_scan_{};
  std::vector<Sf2GpuPacket> vram_setup_packets_;
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
  std::uint64_t script_archive_loads_{};
  std::uint64_t script_level_starts_{};
  std::uint64_t script_dispatches_{};
  std::uint64_t script_program_dispatches_{};
  std::uint64_t script_activations_{};
  std::uint64_t scene_xa_archive_opens_{};
  std::uint64_t scene_speech_starts_{};
  std::uint64_t scene_speech_callbacks_{};
  std::uint8_t scene_speech_stage_{};
  std::uint8_t scene_speech_io_ready_{};
  std::array<std::uint32_t, 4U> last_scene_speech_arguments_{};
  std::uint64_t spatial_sound_starts_{};
  std::uint64_t scene_sound_cue_plays_{};
  std::uint64_t xa_cue_plays_{};
  std::uint64_t xa_stream_starts_{};
  std::uint64_t async_file_services_{};
  std::uint64_t async_file_completions_{};
  std::uint32_t last_async_completion_caller_{};
  std::uint32_t xa_status_source_{};
  std::uint32_t xa_status_result_{};
  Sf2GuestRuntimeDiagnostics diagnostics_{};
  bool catalog_copied_{};
  bool suppress_interrupts_{};
  bool realtime_display_clock_{};
  bool scripts_started_{};
  bool loading_confirm_sent_{};
  bool checkpoint_captured_{};
  bool retail_restore_active_{};
  std::uint64_t retail_restore_start_frame_{};
  std::uint64_t alpha_checkpoint_restores_{};
  bool ready_{};
  bool faulted_{};
  std::string stage_{"construction"};
  std::string fault_detail_;
  std::string scheduler_fault_detail_;
};

Sf2GuestMissionRuntime::Sf2GuestMissionRuntime(
    const std::filesystem::path &cue_path, std::uint32_t mission_index)
    : impl_(std::make_unique<Impl>(cue_path, mission_index)) {}

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

bool Sf2GuestMissionRuntime::advanceHostUpdate() noexcept {
  return impl_->advanceHostUpdate();
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
