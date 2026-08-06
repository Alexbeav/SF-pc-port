#pragma once

#include "sf/game/sf2_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sf::game {

// Verified NTSC-U SCUS-94640 retail guest boundaries. SF3 is a separate
// executable profile: no address here is inherited from the SF2 runtime.
struct Sf3GuestRuntimeProfile {
  std::uint32_t executable_entry;
  std::uint32_t game_main_entry;
  std::uint32_t state_loop_entry;
  std::uint32_t title_state_frame_entry;
  std::uint32_t application_state_push_entry;
  std::uint32_t application_state_pop_entry;
  std::uint32_t processed_pad_entry;
  std::uint32_t title_pad_poll_return;
  std::uint32_t title2_pad_poll_return;
  std::uint32_t menu_pad_poll_return;
  std::uint32_t gpu_submission_entry;
  std::uint32_t render_submission_return;
  std::uint32_t cd_completion_callback;
  std::uint32_t spu_dma_completion_callback;
  std::uint32_t cd_setloc_state;
  std::uint32_t cd_mode_state;
  std::uint32_t interrupt_stack;
  std::uint32_t application_state;
  std::uint32_t application_state_depth;
  std::uint32_t application_state_stack;
  std::uint32_t application_transition;
  std::uint32_t title_mode;
  std::uint32_t title_substate;
  std::uint32_t title_state_setter;
  std::uint32_t title_mode3_press_callback;
  std::uint32_t cd_search_file_entry;
  std::uint32_t resident_file_read_entry;
  std::uint32_t movie_playback_init_entry;
  std::uint32_t movie_playback_update_entry;
  std::uint32_t movie_completion_entry;
  std::uint32_t task_scheduler_tick_entry;
  std::uint32_t task_callback_table;
};

[[nodiscard]] constexpr Sf3GuestRuntimeProfile
sf3UsaGuestRuntimeProfile() noexcept {
  return {
      .executable_entry = 0x800fb368U,
      .game_main_entry = 0x80029ed8U,
      .state_loop_entry = 0x80029fb8U,
      .title_state_frame_entry = 0x8002a388U,
      .application_state_push_entry = 0x8002c6ecU,
      .application_state_pop_entry = 0x8002c728U,
      .processed_pad_entry = 0x80022a60U,
      .title_pad_poll_return = 0x801565b4U,
      .title2_pad_poll_return = 0x801597ecU,
      .menu_pad_poll_return = 0x8014db80U,
      .gpu_submission_entry = 0x800f5b94U,
      .render_submission_return = 0x800f458cU,
      .cd_completion_callback = 0x800f9dd8U,
      .spu_dma_completion_callback = 0x800ff728U,
      // CdControl command 2 mirrors four bytes at 0x80106774; command 0x0e
      // stores the active mode in the immediately following byte.
      .cd_setloc_state = 0x8011fea8U,
      .cd_mode_state = 0x8011feacU,
      // Keep interrupt frames below the executable. TITLE.HOG spans physical
      // 0x001e31fc..0x001f09fb and invalidates the earlier 0x807ef000 mirror.
      .interrupt_stack = 0x8000b000U,
      .application_state = 0x80121b88U,
      .application_state_depth = 0x80121b84U,
      .application_state_stack = 0x8010f304U,
      .application_transition = 0x80121b8cU,
      .title_mode = 0x80157418U,
      .title_substate = 0x8015741cU,
      .title_state_setter = 0x80151af4U,
      .title_mode3_press_callback = 0x8015450cU,
      .cd_search_file_entry = 0x800fa688U,
      .resident_file_read_entry = 0x80026c7cU,
      .movie_playback_init_entry = 0x80147660U,
      .movie_playback_update_entry = 0x80147960U,
      .movie_completion_entry = 0x8002c930U,
      .task_scheduler_tick_entry = 0x80103be4U,
      .task_callback_table = 0x8011fda8U,
  };
}

// SF3 independently opts into the verified sequel GPU DMA-chain decoder.
// The returned immutable frame is the existing presentation transport type;
// no SF2 address, state or gameplay behavior participates in capture.
[[nodiscard]] std::optional<Sf2PresentationFrame>
captureSf3PresentationFrame(std::span<const std::byte> guest_ram,
                            std::uint32_t ordering_table_root,
                            std::uint32_t application_state,
                            std::uint64_t sequence,
                            std::uint64_t guest_frame) noexcept;

struct Sf3GuestRuntimeDiagnostics {
  std::uint32_t application_state{};
  std::uint32_t application_depth{};
  std::uint64_t guest_frames{};
  std::uint64_t input_samples{};
  std::uint64_t gpu_submissions{};
  std::uint64_t presentation_frames{};
  std::uint64_t xa_sectors_received{};
  std::uint64_t xa_sectors_admitted{};
  std::uint64_t platform_slices{};
  std::uint64_t platform_ticks{};
  std::uint64_t retrace_increments{};
  std::uint64_t vsync_queries{};
  std::uint64_t vsync_nonnegative{};
  std::uint64_t vsync_mode_zero{};
  std::uint64_t vsync_mode_one{};
  std::uint64_t vsync_mode_multiple{};
  std::uint64_t machine_tick{};
  std::uint64_t scheduler_now{};
  std::uint64_t cd_sector_deadline{};
  std::uint64_t cd_control_calls{};
  std::uint64_t cd_readn_calls{};
  std::uint32_t cpu_pc{};
  std::uint32_t cpu_sp{};
  std::uint32_t cpu_ra{};
  std::uint32_t retrace_counter{};
  std::uint32_t cd_target_lba{};
  std::uint32_t cd_current_lba{};
  std::uint32_t cd_sector_delay_ticks{};
  std::uint32_t spu_queued_pcm_frames{};
  std::uint32_t spu_queued_cd_frames{};
  std::uint8_t cd_mode{};
  std::uint8_t cd_filter_file{};
  std::uint8_t cd_filter_channel{};
  std::uint8_t cd_xa_set{};
  std::uint8_t cd_xa_file{};
  std::uint8_t cd_xa_channel{};
  std::uint8_t cd_interrupt_flags{};
  std::uint8_t cd_interrupt_enable{};
  std::uint8_t cd_pending_command{};
  std::uint8_t cd_sector_pending{};
  std::uint8_t cd_sector_event_found{};
  bool cd_reading{};
};

// Continuous SCUS-94640 runtime used by the PC product. The current product
// slice deterministically follows the retail frontend into Mission 1, then
// exposes only processed PAD, authored GPU and PCM platform boundaries.
class Sf3GuestMissionRuntime final {
public:
  explicit Sf3GuestMissionRuntime(const std::filesystem::path &cue_path);
  ~Sf3GuestMissionRuntime();
  Sf3GuestMissionRuntime(Sf3GuestMissionRuntime &&) noexcept;
  Sf3GuestMissionRuntime &operator=(Sf3GuestMissionRuntime &&) noexcept;
  Sf3GuestMissionRuntime(const Sf3GuestMissionRuntime &) = delete;
  Sf3GuestMissionRuntime &operator=(const Sf3GuestMissionRuntime &) = delete;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool faulted() const noexcept;
  [[nodiscard]] std::string_view faultDetail() const noexcept;
  void setHostPadState(const LegacyHostPadState &state) noexcept;
  [[nodiscard]] bool advanceHostUpdate() noexcept;
  [[nodiscard]] const std::shared_ptr<const Sf2PresentationFrame> &
  presentationFrame() const noexcept;
  [[nodiscard]] std::size_t
  takePcm(std::span<psx::SpuPcmFrame> destination) noexcept;
  void clearPcm() noexcept;
  [[nodiscard]] Sf3GuestRuntimeDiagnostics diagnostics() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::game
