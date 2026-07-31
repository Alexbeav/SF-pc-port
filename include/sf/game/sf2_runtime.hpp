#pragma once

#include "sf/core/sha256.hpp"
#include "sf/game/hud.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sf::psx {
struct SpuPcmFrame;
}

namespace sf::game {

struct LegacyHostPadState;

inline constexpr std::size_t sf2_inventory_item_count = 34U;

// Verified resident-executable boundaries for the USA SF2 executable shared
// by both discs. Mission/overlay bridge addresses will be added only after a
// deterministic guest probe establishes their live contract.
struct Sf2GuestRuntimeProfile {
  std::uint32_t executable_entry;
  std::uint32_t game_main_entry;
  std::uint32_t state_loop_entry;
  std::uint32_t state_loop_dispatch_entry;
  std::uint32_t common_init_entry;
  std::uint32_t mission_archive_open_entry;
  std::uint32_t application_state_push_entry;
  std::uint32_t application_state_pop_entry;
  std::uint32_t gpu_submission_entry;
  std::uint32_t cd_search_file_entry;
  std::uint32_t mission_overlay_load_address;
  std::uint32_t application_state;
  std::uint32_t application_state_depth;
  std::uint32_t application_state_stack;
  std::uint32_t application_transition;
  std::uint32_t system_clock;
  std::uint32_t task_scheduler_init_entry;
  std::uint32_t interrupt_callback_table;
  std::uint32_t cd_completion_result;
  std::uint32_t cd_setloc_state;
  std::uint32_t cd_mode_state;
};

// Immutable guest-to-native presentation handoff. SF2's renderer callback
// supplies the root of a PSX DMA linked list; native presentation receives a
// bounded deep copy, never a pointer or view into mutable guest RAM.
struct Sf2GpuPacket {
  std::uint32_t guest_address{};
  std::vector<std::uint32_t> gp0_words;
};

struct Sf2PresentationFrame {
  std::uint64_t sequence{};
  std::uint64_t guest_frame{};
  std::uint32_t application_state{};
  std::uint32_t ordering_table_root{};
  std::vector<Sf2GpuPacket> packets;
  std::size_t gp0_word_count{};
  std::size_t gpu_command_count{};
  std::size_t draw_command_count{};

  [[nodiscard]] bool valid() const noexcept {
    return sequence != 0U && ordering_table_root != 0U && !packets.empty() &&
           gp0_word_count != 0U && gpu_command_count == packets.size();
  }
};

enum class Sf2GpuCommandKind : std::uint8_t {
  unsupported,
  draw,
  draw_environment,
  fill_vram,
  copy_vram,
  upload_vram,
};

struct Sf2GpuTransfer {
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::span<const std::uint32_t> payload;
};

enum class Sf2GuestTimelineEventKind : std::uint8_t {
  script_event5 = 1U,
  scene_speech_start,
  scene_speech_stop,
  xa_stream_start,
  xa_stream_stop,
};

struct Sf2GuestTimelineEvent {
  Sf2GuestTimelineEventKind kind{};
  std::uint64_t guest_frame{};
  std::uint32_t system_clock{};
  std::array<std::uint32_t, 4U> arguments{};
};

struct Sf2GuestRadarActor {
  std::int32_t x{};
  std::int32_t z{};
  std::int16_t object_slot{-1};
  std::uint16_t threat_q12{};
  bool allied{};
  bool selected{};
};

struct Sf2GuestScriptTimer {
  std::uint32_t program{};
  std::uint16_t timer_index{};
  std::int16_t remaining_ticks{};
  std::array<std::uint32_t, 2U> program_name_words{};
};

struct Sf2GuestRuntimeDiagnostics {
  std::uint32_t pc{};
  std::uint32_t stack_pointer{};
  std::uint32_t minimum_stack_pointer{};
  std::uint32_t maximum_stack_pointer{};
  std::uint32_t return_address{};
  std::uint32_t global_pointer{};
  std::uint32_t application_state{};
  std::uint32_t selected_mission_index{};
  std::uint32_t system_clock{};
  std::uint32_t last_pad_caller{};
  std::uint32_t last_pad_index{};
  std::uint32_t player_instance{};
  std::uint16_t guest_current_room{};
  std::uint32_t guest_collision_room_count{};
  std::uint32_t guest_collision_room_record{};
  std::uint32_t guest_collision_list{};
  std::uint64_t collision_room_fallbacks{};
  std::uint16_t last_collision_room_fallback{};
  std::uint64_t collision_request_fallbacks{};
  std::uint16_t last_collision_request_fallback{};
  std::uint64_t player_collision_requests{};
  std::uint64_t invalid_player_collision_requests{};
  std::int32_t player_x{};
  std::int32_t player_y{};
  std::int32_t player_z{};
  std::int16_t player_forward_x{};
  std::int16_t player_forward_z{4096};
  std::array<Sf2GuestRadarActor, 16U> radar_actors{};
  std::uint8_t radar_actor_count{};
  std::uint16_t player_health{};
  std::uint16_t player_armor{};
  std::int16_t player_target_slot{-1};
  std::int16_t player_target_meter{};
  std::uint8_t player_target_health_percent{};
  std::uint32_t player_target_flags{};
  bool player_target_active{};
  std::uint32_t objective_completion_bits{};
  bool objective_state_valid{};
  std::uint64_t objective_completion_events{};
  std::uint32_t last_objective_completion_index{};
  std::int32_t last_objective_completion_text{-1};
  std::uint64_t pickup_presentation_events{};
  std::uint32_t last_pickup_actor{};
  std::uint32_t last_pickup_text{};
  std::uint32_t last_pickup_item{};
  std::array<std::uint32_t, 8U> last_pickup_text_words{};
  std::array<char, 64U> last_pickup_text_bytes{};
  std::int16_t player_object_slot{-1};
  std::uint8_t player_danger{};
  std::uint16_t player_threat_count{};
  bool threat_state_valid{};
  std::array<std::uint32_t, 3U> dialogue_state_words{
      0xffffffffU, 0xffffffffU, 0xffffffffU};
  std::uint32_t dialogue_state_word{};
  bool dialogue_state_valid{};
  std::uint32_t player_equipped_item{};
  std::array<std::uint32_t, 2U> player_owned_items{};
  std::array<std::uint16_t, sf2_inventory_item_count> player_reserves{};
  std::array<std::uint16_t, sf2_inventory_item_count> player_magazines{};
  std::uint32_t last_restore_caller{};
  std::int32_t pre_restore_player_x{};
  std::int32_t pre_restore_player_y{};
  std::int32_t pre_restore_player_z{};
  std::uint16_t pre_restore_player_health{};
  std::uint32_t last_damage_caller{};
  std::array<std::uint32_t, 4U> last_damage_arguments{};
  std::uint64_t damage_events{};
  std::uint32_t last_player_damage_caller{};
  std::array<std::uint32_t, 8U> last_player_damage_request{};
  std::uint64_t player_damage_events{};
  std::uint64_t world_collision_scans{};
  std::uint32_t last_world_collision_caller{};
  std::int32_t last_world_collision_object{};
  std::int32_t last_world_collision_room{};
  std::uint64_t player_floor_probes{};
  std::uint64_t player_floor_probe_true{};
  std::uint64_t player_floor_probe_false{};
  std::uint32_t player_floor_false_streak{};
  std::uint32_t maximum_player_floor_false_streak{};
  std::uint64_t renderer_text_repairs{};
  std::uint32_t last_renderer_text_repair_address{};
  std::uint32_t last_renderer_text_expected{};
  std::uint32_t last_renderer_text_actual{};
  std::uint32_t last_renderer_text_writer_pc{};
  std::uint32_t last_renderer_text_writer_instruction{};
  std::uint64_t rejected_renderer_ordering_tables{};
  std::uint32_t last_rejected_renderer_packet{};
  std::uint32_t last_rejected_renderer_root{};
  std::uint64_t last_rejected_renderer_frame{};
  std::uint64_t rejected_renderer_vertex_entries{};
  std::uint32_t last_rejected_renderer_vertex_cursor{};
  std::uint32_t last_rejected_renderer_vertex_address{};
  std::uint64_t clamped_renderer_ordering_table_entries{};
  std::uint32_t last_renderer_ordering_table_requested{};
  std::uint32_t last_renderer_ordering_table_clamped{};
  std::uint32_t last_renderer_ordering_table_base{};
  std::uint32_t last_renderer_ordering_table_buckets{};
  std::uint64_t rejected_renderer_list_merges{};
  std::uint32_t last_rejected_renderer_list_descriptor{};
  std::uint32_t last_rejected_renderer_list_root{};
  std::uint32_t last_rejected_renderer_list_cursor{};
  std::uint32_t last_rejected_renderer_list_tag{};
  std::uint64_t room_texture_activations{};
  std::uint64_t room_texture_page_requests{};
  std::uint64_t room_texture_upload_completions{};
  std::uint64_t retail_load_image_calls{};
  std::uint64_t retained_retail_load_images{};
  std::array<std::uint64_t, 10U> retail_load_image_call_sites{};
  std::uint64_t unknown_retail_load_image_call_sites{};
  std::uint32_t last_room_texture_activation{};
  std::uint32_t last_room_texture_page{};
  std::uint32_t last_room_texture_bank{};
  std::uint32_t last_retail_load_image_caller{};
  Sf2GpuTransfer last_retail_load_image_transfer{};
  std::uint32_t retained_retail_texture_page_mask{};
  std::uint64_t retained_retail_upload_halfwords{};
  std::uint32_t retained_retail_framebuffer_rectangles{};
  std::uint32_t retained_retail_fullscreen_rectangles{};
  std::uint32_t retained_retail_clut_rectangles{};
  std::array<std::uint64_t, 8U> retained_retail_clut_transfers{};
  std::uint64_t spu_mixed_frames{};
  std::uint64_t spu_key_on_writes{};
  std::uint64_t spu_key_off_writes{};
  std::uint32_t spu_last_key_on_mask{};
  std::uint32_t spu_last_key_off_mask{};
  std::size_t active_spu_voices{};
  std::uint16_t spu_control{};
  std::uint16_t spu_status{};
  std::size_t spu_cd_frames{};
  std::uint8_t cd_muted{};
  std::uint8_t cd_adpcm_muted{};
  std::uint32_t cd_lba{};
  std::uint8_t cd_reading{};
  std::uint8_t cd_interrupt_flags{};
  std::uint8_t cd_pending_command{};
  std::uint8_t cd_command_phase{};
  std::uint8_t cd_data_valid{};
  std::uint8_t cd_sector_event_pending{};
  std::uint8_t xa_stream_set{};
  std::uint8_t xa_file{};
  std::uint8_t xa_channel{};
  std::uint64_t script_archive_loads{};
  std::uint16_t script_program_count{};
  std::uint32_t script_level_program{};
  std::uint32_t script_level_name_pointer{};
  std::array<std::uint32_t, 2U> script_level_name_words{};
  std::array<std::uint32_t, 2U> script_lookup_name_words{};
  std::uint64_t script_level_starts{};
  std::uint64_t script_dispatches{};
  std::uint64_t script_event5_dispatches{};
  std::array<std::uint32_t, 2U> last_script_dispatch_arguments{};
  std::uint64_t script_program_dispatches{};
  std::uint64_t script_activations{};
  std::uint32_t last_script_activation_program{};
  std::array<Sf2GuestScriptTimer, 16U> active_script_timers{};
  std::uint8_t active_script_timer_count{};
  std::int16_t mission_timer_ticks{};
  bool mission_timer_visible{};
  std::uint64_t scene_xa_archive_opens{};
  std::uint64_t scene_speech_starts{};
  std::uint64_t scene_speech_callbacks{};
  std::uint64_t scene_speech_stops{};
  std::uint8_t scene_speech_stage{};
  std::uint8_t scene_speech_io_ready{};
  std::array<std::uint32_t, 4U> last_scene_speech_arguments{};
  std::array<std::uint32_t, 4U> last_scene_speech_callback_arguments{};
  std::array<std::uint32_t, 4U> last_scene_speech_stop_arguments{};
  std::array<std::uint32_t, 20U> scene_speech_io_state{};
  std::uint64_t spatial_sound_starts{};
  std::uint64_t scene_sound_cue_plays{};
  std::uint64_t rejected_sound_bank_lookups{};
  std::uint32_t last_rejected_sound_bank{};
  std::uint32_t last_rejected_sound_bank_table{};
  std::uint32_t last_rejected_sound_bank_index{};
  std::uint32_t last_rejected_sound_bank_caller{};
  std::uint32_t last_rejected_sound_bank_magic{};
  std::uint16_t last_rejected_sound_bank_entry_count{};
  std::uint64_t rejected_sound_voice_updates{};
  std::uint32_t last_rejected_sound_voice{};
  std::uint32_t last_rejected_sound_voice_caller{};
  std::array<std::uint32_t, 11U> interrupt_callbacks{};
  std::array<std::uint32_t, 5U> xa_globals{};
  std::uint32_t xa_status_source{};
  std::uint32_t xa_status_result{};
  std::uint64_t xa_cue_plays{};
  std::uint64_t xa_stream_starts{};
  std::uint64_t xa_stream_stops{};
  std::uint64_t timeline_event_count{};
  std::array<Sf2GuestTimelineEvent, 64U> timeline_events{};
  std::uint64_t async_file_services{};
  std::uint64_t async_file_completions{};
  std::uint32_t last_async_completion_caller{};
  std::uint64_t input_samples{};
  std::uint64_t checkpoint_restores{};
};

// Projects only authoritative guest-owned player state into the native SF2
// HUD presentation model. It never writes inventory, health or selection back
// to the guest.
void projectSf2GuestHud(GameplayHud &hud,
                        const Sf2GuestRuntimeDiagnostics &guest,
                        bool first_person_aim = false) noexcept;

// Retains relative mouse motion until the retail 20 Hz PAD sampler advances.
// This prevents motion collected on the other two 60 Hz presentation frames
// from being discarded.
class Sf2SampledMouseAccumulator final {
public:
  explicit Sf2SampledMouseAccumulator(
      std::uint64_t initial_sample = 0U) noexcept;
  void add(std::uint64_t sample, int delta_x, int delta_y) noexcept;
  [[nodiscard]] int x() const noexcept { return x_; }
  [[nodiscard]] int y() const noexcept { return y_; }

private:
  std::uint64_t sample_{};
  int x_{};
  int y_{};
};

// Converts host weapon-cycle impulses into retail Select edges. Every press is
// held until sampled and separated from the next queued press by one sampled
// release.
class Sf2WeaponSelectPulseQueue final {
public:
  static constexpr unsigned int maximum_pending =
      static_cast<unsigned int>(sf2_inventory_item_count);

  explicit Sf2WeaponSelectPulseQueue(
      std::uint64_t initial_sample = 0U) noexcept;
  void enqueue(unsigned int count = 1U) noexcept;
  [[nodiscard]] bool update(std::uint64_t sample) noexcept;
  [[nodiscard]] unsigned int pending() const noexcept { return pending_; }

private:
  std::uint64_t sample_{};
  unsigned int pending_{};
  bool down_{};
  bool may_press_{true};
};

// The retail Change Weapon action advances through owned, equipable item IDs in
// ascending order and wraps. These helpers translate a signed host carousel
// movement or a PC quick-slot index into the number of forward retail Select
// edges required from the authoritative guest selection. A missing quick slot
// is rejected instead of mutating guest inventory.
[[nodiscard]] unsigned int sf2WeaponCyclePulseCount(
    const Sf2GuestRuntimeDiagnostics &guest, std::int32_t steps) noexcept;
[[nodiscard]] std::optional<unsigned int> sf2WeaponSlotPulseCount(
    const Sf2GuestRuntimeDiagnostics &guest, std::size_t slot) noexcept;

// Classifies one bounded DMA packet for the native presentation backend.
// Drawing and draw-environment packets retain their exact GP0 words. VRAM
// commands are exposed separately because PsyCross represents uploads with a
// host pointer rather than the PSX packet's inline pixel payload.
[[nodiscard]] Sf2GpuCommandKind
sf2GpuCommandKind(const Sf2GpuPacket &packet) noexcept;

// Returns the complete length of the first GP0 command, or nullopt when the
// supplied stream ends inside that command.
[[nodiscard]] std::optional<std::size_t>
sf2Gp0CommandWordCount(std::span<const std::uint32_t> words) noexcept;

[[nodiscard]] std::optional<Sf2GpuTransfer>
sf2GpuTransfer(const Sf2GpuPacket &packet) noexcept;

// Follows the retail GPU DMA chain in a 2 MiB RAM snapshot. Every address,
// packet length, cycle, and terminator is validated before a frame is
// published. draw_command_count separately identifies GP0 polygon/line/
// rectangle opcodes 0x20..0x7f; clear, transfer, and environment-only retail
// frames remain valid presentation commands.
[[nodiscard]] std::optional<Sf2PresentationFrame>
captureSf2PresentationFrame(std::span<const std::byte> guest_ram,
                            std::uint32_t ordering_table_root,
                            std::uint32_t application_state,
                            std::uint64_t sequence,
                            std::uint64_t guest_frame) noexcept;

// Production owner for the Disc 1 TITLE -> mission transition. It keeps
// executable, overlays, CD/SPU state, collision, scripts, HUD and effects in
// the retail guest; the host supplies only a standard pad sample and consumes
// immutable GPU/SPU output.
class Sf2GuestMissionRuntime final {
public:
  Sf2GuestMissionRuntime(const std::filesystem::path &cue_path,
                         std::uint32_t mission_index);
  ~Sf2GuestMissionRuntime();

  Sf2GuestMissionRuntime(const Sf2GuestMissionRuntime &) = delete;
  Sf2GuestMissionRuntime &
  operator=(const Sf2GuestMissionRuntime &) = delete;
  Sf2GuestMissionRuntime(Sf2GuestMissionRuntime &&) = delete;
  Sf2GuestMissionRuntime &operator=(Sf2GuestMissionRuntime &&) = delete;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool faulted() const noexcept;
  [[nodiscard]] std::string_view faultDetail() const noexcept;
  void setHostPadState(const LegacyHostPadState &state) noexcept;
  // Diagnostic-only authored event injection used by sf_tool to exercise
  // mission transitions without an interactive traversal.
  [[nodiscard]] bool
  dispatchScriptEventForProbe(std::uint32_t event,
                              std::uint32_t selector) noexcept;
  [[nodiscard]] bool
  activateScriptProgramForProbe(std::string_view name) noexcept;
  [[nodiscard]] bool setPlayerPositionForProbe(
      std::int32_t x, std::int32_t y, std::int32_t z) noexcept;
  [[nodiscard]] bool
  setPlayerRoomForProbe(std::uint16_t room) noexcept;
  [[nodiscard]] bool
  setPlayerHealthForProbe(std::uint16_t health) noexcept;
  [[nodiscard]] bool
  startPlayerObjectInteractionForProbe(std::uint32_t selector) noexcept;
  [[nodiscard]] bool advanceHostUpdate() noexcept;
  [[nodiscard]] const std::shared_ptr<const Sf2PresentationFrame> &
  presentationFrame() const noexcept;
  [[nodiscard]] std::size_t
  takePcm(std::span<psx::SpuPcmFrame> destination) noexcept;
  void clearPcm() noexcept;
  [[nodiscard]] std::uint64_t inputSampleCount() const noexcept;
  [[nodiscard]] Sf2GuestRuntimeDiagnostics diagnostics() const noexcept;
  // Process-local testing checkpoint. Captures/restores the complete guest
  // machine plus SF2 host scheduling and retained presentation state.
  [[nodiscard]] bool captureQuickState() noexcept;
  [[nodiscard]] bool restoreQuickState() noexcept;
  [[nodiscard]] bool hasQuickState() const noexcept;
  // Read-only deterministic probe surface; the product does not consume it.
  [[nodiscard]] core::Sha256Digest guestRamDigestForProbe() const noexcept;
  [[nodiscard]] bool
  copyGuestRamForProbe(std::span<std::byte> destination) const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] constexpr Sf2GuestRuntimeProfile
sf2UsaGuestRuntimeProfile() noexcept {
  return {
      .executable_entry = 0x800f8598U,
      .game_main_entry = 0x80029624U,
      .state_loop_entry = 0x80029700U,
      .state_loop_dispatch_entry = 0x800297dcU,
      .common_init_entry = 0x8002a518U,
      .mission_archive_open_entry = 0x8002a338U,
      .application_state_push_entry = 0x8002bc44U,
      .application_state_pop_entry = 0x8002bc80U,
      .gpu_submission_entry = 0x800f2e24U,
      .cd_search_file_entry = 0x800f78b8U,
      .mission_overlay_load_address = 0x8014b978U,
      .application_state = 0x8011ee90U,
      .application_state_depth = 0x8011ee8cU,
      .application_state_stack = 0x8010c5e4U,
      .application_transition = 0x8011ee94U,
      .system_clock = 0x8011f668U,
      .task_scheduler_init_entry = 0x800226a0U,
      .interrupt_callback_table = 0x8011d0d4U,
      .cd_completion_result = 0x80141a10U,
      .cd_setloc_state = 0x8011d1d4U,
      .cd_mode_state = 0x8011d1d8U,
  };
}

// SF2 executable item IDs are not a generic sequel ABI. SF3 must supply its
// own translation before native actors or pickups can use it.
[[nodiscard]] std::optional<WeaponId>
sf2WeaponForItem(std::uint8_t item) noexcept;

[[nodiscard]] constexpr bool
sf2ActorInitiallyDormant(std::uint32_t mission_index,
                         std::uint16_t source_index) noexcept {
  return mission_index == 1U &&
         (source_index == 84U || source_index == 85U ||
          source_index == 86U);
}

} // namespace sf::game
