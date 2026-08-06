#include "sf/assets/fog_archive.hpp"
#include "sf/core/error.hpp"
#include "sf/game/disc_cdrom_media.hpp"
#include "sf/game/game_disc.hpp"
#include "sf/game/legacy_gameplay_vm.hpp"
#include "sf/game/runtime_profile.hpp"
#include "sf/game/sf3_runtime.hpp"
#include "sf/core/fixed_rate_clock.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sf::game {

const GameRuntimeProfile &sf3RuntimeProfile() noexcept {
  static constexpr GameRuntimeProfile profile{
      .game = GameId::syphon_filter_3,
      .kind = GameRuntimeKind::sf3,
      .hud_atlas = HudAtlasKind::sf3,
      .emd_vertex_index_stride = 2U,
      .hmd_vertex_index_stride = 8U,
      .first_person_eye_height = 220.0,
      .player_collision_radius = 32.0,
      .player_collision_height = 260.0,
      .uses_legacy_guest_runtime = false,
      // SF3 must opt into its own translated mission runtime. It no longer
      // inherits SF2 pickup/interaction scaffolding merely by being a sequel.
      .supports_native_mission_items = false,
      .supports_native_mission_interactions = false,
      .uses_sf1_environment_atlas = false,
  };
  return profile;
}

std::optional<Sf2PresentationFrame>
captureSf3PresentationFrame(std::span<const std::byte> guest_ram,
                            std::uint32_t ordering_table_root,
                            std::uint32_t application_state,
                            std::uint64_t sequence,
                            std::uint64_t guest_frame) noexcept {
  return captureSf2PresentationFrame(guest_ram, ordering_table_root,
                                     application_state, sequence, guest_frame);
}

class Sf3GuestMissionRuntime::Impl final {
public:
  explicit Impl(const std::filesystem::path &cue_path)
      : disc_(GameDisc::open(cue_path)), vm_(disc_.executable()),
        cdrom_media_(disc_.image()) {
    try {
      if (!disc_.game() || disc_.game()->id != GameId::syphon_filter_3) {
        markFault("SF3 guest runtime requires SCUS-94640");
        return;
      }
      loadMissionAssets();
      bindPlatform();
      if (!runUntilMissionGameplay()) {
        if (!faulted_) {
          markFault("SF3 retail Mission 1 bootstrap did not reach gameplay");
        }
        return;
      }
      vm_.clearPcm();
      platform_slices_ = 0U;
      platform_ticks_ = 0U;
      retrace_increments_ = 0U;
      vsync_queries_ = 0U;
      vsync_nonnegative_ = 0U;
      vsync_mode_zero_ = 0U;
      vsync_mode_one_ = 0U;
      vsync_mode_multiple_ = 0U;
      ready_ = true;
    } catch (const std::exception &error) {
      markFault(error.what());
    }
  }

  ~Impl() {
    vm_.setHostCallObserver({});
    vm_.runtime().setExecutionObserver({});
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
  [[nodiscard]] bool advanceHostUpdate() noexcept {
    if (!ready_ || faulted_) {
      return false;
    }
    // A 20 Hz product update owns exactly one 20 Hz interval of guest CPU and
    // device time.  Presentation is an asynchronous publication: retail may
    // intentionally retain the previous ordering table while a dialogue/CD
    // transition runs.  Waiting here for DrawOTag couples host input/audio to
    // presentation and turns a legitimate no-new-frame interval into a long
    // frontend stall followed by a watchdog exit.
    return runGuestInterval(task_scheduler_period_);
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
  [[nodiscard]] Sf3GuestRuntimeDiagnostics diagnostics() const noexcept {
    Sf3GuestRuntimeDiagnostics result{
        .guest_frames = guest_frames_,
        .input_samples = input_samples_,
        .gpu_submissions = gpu_submissions_,
        .presentation_frames = presentation_frames_,
        .platform_slices = platform_slices_,
        .platform_ticks = platform_ticks_,
        .retrace_increments = retrace_increments_,
        .vsync_queries = vsync_queries_,
        .vsync_nonnegative = vsync_nonnegative_,
        .vsync_mode_zero = vsync_mode_zero_,
        .vsync_mode_one = vsync_mode_one_,
        .vsync_mode_multiple = vsync_mode_multiple_,
        .cd_control_calls = cd_control_calls_,
        .cd_readn_calls = cd_readn_calls_,
    };
    static_cast<void>(vm_.runtime().read32(profile_.application_state,
                                           result.application_state));
    static_cast<void>(vm_.runtime().read32(profile_.application_state_depth,
                                           result.application_depth));
    const auto &cpu = vm_.runtime().state();
    result.cpu_pc = cpu.pc;
    result.cpu_sp = cpu.gpr[29U];
    result.cpu_ra = cpu.gpr[31U];
    result.machine_tick = vm_.machine().currentTick();
    const auto &layout = disc_.game()->executable_layout;
    static_cast<void>(vm_.runtime().read32(layout.retrace_counter_address,
                                           result.retrace_counter));
    const auto cd = vm_.machine().cdrom().captureState();
    result.cd_target_lba = cd.target_lba;
    result.cd_current_lba = cd.current_lba;
    result.cd_mode = cd.mode;
    result.cd_filter_file = cd.filter_file;
    result.cd_filter_channel = cd.filter_channel;
    result.cd_xa_set = cd.xa_current_set;
    result.cd_xa_file = cd.xa_current_file;
    result.cd_xa_channel = cd.xa_current_channel;
    result.cd_interrupt_flags = cd.interrupt_flags;
    result.cd_interrupt_enable = cd.interrupt_enable;
    result.cd_pending_command = cd.pending_command;
    result.cd_sector_pending = cd.sector_event.pending;
    result.cd_sector_delay_ticks = cd.sector_event.delay_ticks;
    const auto machine = vm_.machine().captureState();
    result.scheduler_now = machine.scheduler.now;
    for (std::size_t index = 0U; index < machine.scheduler.event_count; ++index) {
      const auto &event = machine.scheduler.events[index];
      if (event.type == psx::MachineEventType::cdrom_sector &&
          event.payload == cd.sector_event.generation) {
        result.cd_sector_event_found = 1U;
        result.cd_sector_deadline = event.deadline;
        break;
      }
    }
    result.cd_reading = cd.reading != 0U;
    result.spu_queued_pcm_frames = static_cast<std::uint32_t>(
        vm_.machine().spu().queuedPcmFrames());
    result.spu_queued_cd_frames = static_cast<std::uint32_t>(
        vm_.machine().spu().queuedCdFrames());
    const auto xa = vm_.machine().xaSectorAdmissionDiagnostics();
    result.xa_sectors_received = xa.received;
    result.xa_sectors_admitted = xa.admitted;
    return result;
  }

private:
  static constexpr auto profile_ = sf3UsaGuestRuntimeProfile();
  static constexpr std::uint32_t bootstrap_poll_word_ = 0x8012214cU;
  static constexpr std::uint32_t return_trampoline_ = 0x8000c000U;
  static constexpr std::uint64_t scheduler_slice_budget_ = 50'000U;
  static constexpr std::uint64_t task_scheduler_period_ =
      psx::CdRomController::cpu_clock_hz /
      LegacyGameplayVm::updates_per_second;

  void markFault(std::string_view detail) noexcept {
    faulted_ = true;
    ready_ = false;
    try {
      fault_detail_.assign(detail);
    } catch (...) {
      fault_detail_ = "SF3 runtime fault";
    }
  }

  void loadMissionAssets() {
    const auto fog_bytes = disc_.image().readFile("FOG/TOKYO.FOG");
    const auto archive = assets::FogArchive::parse(fog_bytes);
    for (const auto &entry : archive.entries()) {
      const auto file = archive.file(entry.name);
      tokyo_files_.emplace(
          entry.name, std::vector<std::byte>{file.begin(), file.end()});
    }
    const auto slf = archive.file("SLF.RFF");
    tokyo_slf_.assign(slf.begin(), slf.end());
  }

  [[nodiscard]] bool writePadRecord(LegacyHostCallContext &context,
                                    std::uint32_t record,
                                    const LegacyHostPadState &state) {
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
    if (record == 0U || !context.write8(record, 0U) ||
        !context.write8(record + 1U, 7U) ||
        !context.write8(record + 2U, 1U) ||
        !context.write8(record + 3U, 0U) ||
        !context.write16(record + 4U, buttons) ||
        !context.write8(record + 6U, state.right_x) ||
        !context.write8(record + 7U, state.right_y) ||
        !context.write8(record + 8U, state.left_x) ||
        !context.write8(record + 9U, state.left_y)) {
      return false;
    }
    for (auto offset = 0x0aU; offset < 0x3cU; ++offset) {
      if (!context.write8(record + offset, 0U)) {
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
    return context.write32(
               record + face_horizontal_offset,
               std::bit_cast<std::uint32_t>(face_horizontal)) &&
           context.write32(record + face_vertical_offset,
                           std::bit_cast<std::uint32_t>(face_vertical)) &&
           context.write32(record + left_analog_x_offset,
                           std::bit_cast<std::uint32_t>(axis_x(state.left_x))) &&
           context.write32(record + left_analog_y_offset,
                           std::bit_cast<std::uint32_t>(axis_y(state.left_y))) &&
           context.write32(record + right_analog_x_offset,
                           std::bit_cast<std::uint32_t>(axis_x(state.right_x))) &&
           context.write32(record + right_analog_y_offset,
                           std::bit_cast<std::uint32_t>(axis_y(state.right_y)));
  }

  void bindPlatform() {
    vm_.machine().setCdRomMedia(&cdrom_media_);
    vm_.bindPsxBiosCoreVector(false, true);
    const auto &layout = disc_.game()->executable_layout;
    vm_.bindPsxVideoTimingCall(layout.vsync_address,
                              layout.retrace_counter_address);
    vm_.bindPsxCdPendingCommandCall(
        layout.cd_pending_command_address, layout.cd_pending_command_state,
        layout.cd_response_pointer, layout.cd_completion_state);
    vm_.bindPsxCdControlCall(layout.cd_control_address,
                             profile_.cd_setloc_state,
                             profile_.cd_mode_state);
    vm_.bindPsxCdReadyCallback(
        layout.cd_ready_callback_address, layout.cd_ready_result_address,
        layout.cd_ready_state_address, layout.cd_ready_callback_is_pointer);
    vm_.bindPsxCdCompletionCallback(profile_.cd_completion_callback,
                                    layout.cd_ready_result_address, false);

    vm_.setHostCallObserver(
        [this, vsync_address = layout.vsync_address,
         cd_control_address = layout.cd_control_address](
            std::uint32_t pc, const psx::R3000State &state) {
          if (pc == cd_control_address) {
            ++cd_control_calls_;
            if ((state.gpr[4U] & 0xffU) == 0x06U) {
              ++cd_readn_calls_;
            }
          }
          if (pc == vsync_address) {
            const auto mode = std::bit_cast<std::int32_t>(state.gpr[4U]);
            if (mode < 0) {
              ++vsync_queries_;
            } else {
              ++vsync_nonnegative_;
              if (mode == 0) {
                ++vsync_mode_zero_;
              } else if (mode == 1) {
                ++vsync_mode_one_;
              } else {
                ++vsync_mode_multiple_;
              }
            }
          }
          if (pc == 0x000000a0U &&
              (state.gpr[9U] == 0xabU || state.gpr[9U] == 0xacU)) {
            memory_card_info_event_pending_ = true;
            const auto card_info = state.gpr[9U] == 0xabU;
            const auto initial_info =
                card_info && state.gpr[31U] == 0x8014840cU &&
                !blank_card_initial_info_seen_;
            memory_card_high_event_callback_ =
                !card_info || initial_info ? 0x80149b4cU : 0x80149b10U;
            blank_card_initial_info_seen_ |= initial_info;
          }
          if (pc == 0x000000b0U &&
              (state.gpr[9U] == 0x4eU || state.gpr[9U] == 0x4fU)) {
            memory_card_io_event_pending_ = true;
          }
        });

    bindFileTransport();
    bindInput();
    vm_.bindHostCall(
        profile_.movie_playback_update_entry,
        [this](LegacyHostCallContext &context) {
          if (!movie_presenter_pending_) {
            context.continueGuestInstruction();
            return;
          }
          context.setReturnValue(0U);
        });
    vm_.bindHostCall(return_trampoline_, [](LegacyHostCallContext &context) {
      context.continueGuestInstruction();
    });
    vm_.runtime().setExecutionObserver(
        [this](const psx::R3000State &state, std::uint32_t pc,
               std::uint32_t) {
          observeExecution(state, pc);
        });
  }

  void bindFileTransport() {
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
            constexpr std::uint32_t pregap_sectors = 150U;
            constexpr std::uint32_t sectors_per_second = 75U;
            const auto absolute_sector = entry.extent_lba + pregap_sectors;
            const auto bcd = [](std::uint32_t value) {
              return static_cast<std::byte>(((value / 10U) << 4U) |
                                            (value % 10U));
            };
            std::array<std::byte, 24U> file{};
            file[0U] = bcd(absolute_sector / (60U * sectors_per_second));
            file[1U] = bcd((absolute_sector / sectors_per_second) % 60U);
            file[2U] = bcd(absolute_sector % sectors_per_second);
            for (auto index = std::size_t{}; index < sizeof(entry.size);
                 ++index) {
              file[4U + index] =
                  static_cast<std::byte>(entry.size >> (index * 8U));
            }
            const auto name_size =
                std::min(entry.name.size(), file.size() - 8U);
            for (auto index = std::size_t{}; index < name_size; ++index) {
              file[8U + index] = static_cast<std::byte>(entry.name[index]);
            }
            if (!context.writeBytes(destination, file)) {
              context.setReturnValue(0U);
              return;
            }
            context.setReturnValue(destination);
          } catch (const core::Error &) {
            context.setReturnValue(0U);
          }
        });

    vm_.bindHostCall(
        profile_.resident_file_read_entry,
        [this](LegacyHostCallContext &context) {
          const auto handle = context.argument(0);
          const auto destination = context.argument(1);
          const auto requested = context.argument(2);
          const auto completion = context.argument(3);
          std::uint32_t handle_size{};
          if (handle == 0U || destination == 0U ||
              !context.read32(handle + 4U, handle_size)) {
            context.continueGuestInstruction();
            return;
          }
          const auto title_entry = disc_.image().find("TITLE.HOG");
          const auto movie_entry = disc_.image().find("MOVIE1.HOG");
          const auto tokyo_entry = disc_.image().find("FOG/TOKYO.FOG");
          std::vector<std::byte> payload;
          if (destination == 0x80150950U && requested == 0xe800U) {
            payload.resize(requested);
            const auto bytes = disc_.image().readFile("BIN/TITLE2.OVL");
            std::ranges::copy(bytes, payload.begin());
          } else if (destination == 0x8015e978U && requested == 0x12000U) {
            payload.resize(requested);
            const auto bytes = disc_.image().readFile("BIN/INIT.OVL");
            std::ranges::copy(bytes, payload.begin());
          } else if (destination == 0x801afd0cU && requested == 0x27800U) {
            payload.resize(requested);
            const auto bytes = disc_.image().readFile("MPTITLE2.HOG");
            std::ranges::copy(bytes, payload.begin());
          } else if (handle_size == title_entry.size &&
                     requested >= title_entry.size) {
            payload.resize(requested);
            const auto bytes = disc_.image().readFile("TITLE.HOG");
            std::ranges::copy(bytes, payload.begin());
          } else if (handle_size == movie_entry.size && requested == 0x800U) {
            payload.resize(requested);
            auto sector = std::span<std::byte, 0x800U>{payload};
            if (!cdrom_media_.readDataSector(movie_entry.extent_lba, sector)) {
              context.setReturnValue(3U);
              return;
            }
          } else if (handle_size == tokyo_entry.size &&
                     requested == 0x800U) {
            payload.resize(requested);
            auto sector = std::span<std::byte, 0x800U>{payload};
            if (!cdrom_media_.readDataSector(tokyo_entry.extent_lba, sector)) {
              context.setReturnValue(3U);
              return;
            }
          } else if (handle_size == tokyo_slf_.size() &&
                     requested >= tokyo_slf_.size()) {
            payload.resize(requested);
            std::ranges::copy(tokyo_slf_, payload.begin());
          } else if (!readTokyoMember(handle, handle_size, requested,
                                      payload)) {
            context.continueGuestInstruction();
            return;
          }
          if (!context.writeBytes(destination, payload) ||
              (completion != 0U && !context.write32(completion, 0U))) {
            context.setReturnValue(3U);
            return;
          }
          context.setReturnValue(0U);
        });
  }

  [[nodiscard]] bool readTokyoMember(std::uint32_t handle,
                                     std::uint32_t handle_size,
                                     std::uint32_t requested,
                                     std::vector<std::byte> &payload) {
    auto open_file = tokyo_open_files_.find(handle);
    if (open_file != tokyo_open_files_.end() &&
        open_file->second.first->size() != handle_size) {
      tokyo_open_files_.erase(open_file);
      open_file = tokyo_open_files_.end();
    }
    if (open_file == tokyo_open_files_.end()) {
      const std::vector<std::byte> *unique_member{};
      auto matches = std::size_t{};
      for (const auto &[name, bytes] : tokyo_files_) {
        static_cast<void>(name);
        if (bytes.size() == handle_size) {
          unique_member = &bytes;
          ++matches;
        }
      }
      if (matches != 1U) {
        return false;
      }
      open_file = tokyo_open_files_
                      .insert_or_assign(
                          handle, std::pair{unique_member, std::size_t{}})
                      .first;
    }
    payload.resize(requested);
    const auto &bytes = *open_file->second.first;
    const auto available = open_file->second.second < bytes.size()
                               ? bytes.size() - open_file->second.second
                               : 0U;
    const auto copied = std::min<std::size_t>(requested, available);
    std::ranges::copy_n(
        bytes.begin() + static_cast<std::ptrdiff_t>(open_file->second.second),
        copied, payload.begin());
    open_file->second.second += copied;
    if (open_file->second.second >= bytes.size()) {
      tokyo_open_files_.erase(open_file);
    }
    return true;
  }

  void bindInput() {
    vm_.bindHostCall(0x8002a4acU, [this](LegacyHostCallContext &context) {
      std::uint32_t state{};
      if (!context.read32(profile_.application_state, state) || state != 8U) {
        context.continueGuestInstruction();
        return;
      }
      ++state8_pad_samples_;
      auto pad = LegacyHostPadState{};
      if (state8_pad_samples_ == 120U) {
        pad.buttons = 0x4000U;
        state8_confirmed_ = true;
      }
      std::uint32_t record{};
      if (!context.read32(context.registerValue(29U) + 0x14U, record) ||
          !writePadRecord(context, record, pad)) {
        context.rejectHostCall();
        return;
      }
      context.continueGuestInstruction();
    });
    vm_.bindHostCall(0x80050da8U, [this](LegacyHostCallContext &context) {
      std::uint32_t state{};
      if (!context.read32(profile_.application_state, state) || state != 0U) {
        context.continueGuestInstruction();
        return;
      }
      std::uint32_t record{};
      if (!context.read32(context.registerValue(29U) + 0x20U, record) ||
          !writePadRecord(context, record, host_pad_)) {
        context.rejectHostCall();
        return;
      }
      ++input_samples_;
      context.continueGuestInstruction();
    });

    vm_.bindHostCall(profile_.title_pad_poll_return,
                     [this](LegacyHostCallContext &context) {
      std::uint32_t state{};
      if (!context.read32(profile_.application_state, state) || state != 4U ||
          context.registerValue(5U) != 0U) {
        context.continueGuestInstruction();
        return;
      }
      const auto title_mode = context.registerValue(19U);
      std::uint32_t title_substate{};
      const auto ready =
          context.read32(profile_.title_substate, title_substate) &&
          title_substate == 3U;
      title_ready_pad_samples_ += ready ? 1U : 0U;
      std::uint32_t title_heap{};
      std::uint32_t widget_state{};
      const auto mode0_interactive =
          title_mode == 0U && context.read32(0x8015e5f0U, title_heap) &&
          title_heap != 0U &&
          context.read32(title_heap + 0x9a7cU, widget_state) &&
          widget_state == 4U;
      title_mode0_interactive_samples_ += mode0_interactive ? 1U : 0U;
      std::uint32_t mode1_gate{};
      std::uint32_t mode1_callback{};
      if (title_mode == 1U) {
        static_cast<void>(context.read32(0x80157374U, mode1_gate));
        static_cast<void>(context.read32(0x80157378U, mode1_callback));
      }
      const auto mode1_interactive =
          title_mode == 1U && mode1_gate != 4U && mode1_callback != 0U;
      if (mode1_interactive) {
        if (!title_mode1_dialog_active_) {
          title_mode1_dialog_samples_ = 0U;
        }
        title_mode1_dialog_active_ = true;
        ++title_mode1_dialog_samples_;
      } else {
        title_mode1_dialog_active_ = false;
      }
      const auto mode1_continue = title_mode == 1U && mode1_gate == 1U &&
                                  mode1_callback == 0U &&
                                  title_mode1_seen_;
      title_mode1_seen_ |= mode1_interactive;
      title_mode1_continue_samples_ += mode1_continue ? 1U : 0U;
      if (title_mode == 10U) {
        ++title_mode10_pad_samples_;
      }
      const auto cross =
          title_ready_pad_samples_ == 3U || title_ready_pad_samples_ == 5U ||
          title_ready_pad_samples_ == 7U || title_ready_pad_samples_ == 9U ||
          (mode0_interactive && title_mode0_interactive_samples_ == 3U) ||
          (mode1_interactive && title_mode1_dialog_samples_ == 7U) ||
          (mode1_continue && title_mode1_continue_samples_ == 3U) ||
          (title_mode == 10U && title_mode10_pad_samples_ == 3U);
      const auto down =
          mode1_interactive && title_mode1_dialog_samples_ == 3U;
      auto pad = LegacyHostPadState{};
      if (title_ready_pad_samples_ == 15U) {
        pad.buttons = 0x0008U;
      } else if (cross) {
        pad.buttons = 0x4000U;
      } else if (down) {
        pad.buttons = 0x0040U;
      }
      std::uint32_t record{};
      if (!context.read32(context.registerValue(29U) + 0x10U, record) ||
          !writePadRecord(context, record, pad)) {
        context.rejectHostCall();
        return;
      }
      context.continueGuestInstruction();
    });
    vm_.bindHostCall(profile_.title2_pad_poll_return,
                     [this](LegacyHostCallContext &context) {
      std::uint32_t state{};
      if (!context.read32(profile_.application_state, state) || state == 0U ||
          context.registerValue(5U) != 0U) {
        context.continueGuestInstruction();
        return;
      }
      auto pad = LegacyHostPadState{};
      if (++title2_pad_samples_ == 10U) {
        pad.buttons = 0x4000U;
      }
      std::uint32_t record{};
      if (!context.read32(context.registerValue(29U) + 0x10U, record) ||
          !writePadRecord(context, record, pad)) {
        context.rejectHostCall();
        return;
      }
      context.continueGuestInstruction();
    });
    vm_.bindHostCall(profile_.menu_pad_poll_return,
                     [this](LegacyHostCallContext &context) {
      std::uint32_t state{};
      if (!context.read32(profile_.application_state, state) || state == 0U ||
          context.registerValue(5U) != 0U) {
        context.continueGuestInstruction();
        return;
      }
      ++menu_pad_samples_;
      const auto cross =
          (menu_pad_samples_ >= 10U && menu_pad_samples_ <= 12U) ||
          (menu_pad_samples_ >= 30U && menu_pad_samples_ <= 32U) ||
          (menu_pad_samples_ >= 50U && menu_pad_samples_ <= 52U) ||
          (menu_pad_samples_ >= 70U && menu_pad_samples_ <= 72U);
      auto pad = LegacyHostPadState{};
      pad.buttons = cross ? 0x4000U : 0U;
      std::uint32_t record{};
      if (!context.read32(context.registerValue(29U) + 0x10U, record) ||
          !writePadRecord(context, record, pad)) {
        context.rejectHostCall();
        return;
      }
      context.continueGuestInstruction();
    });
  }

  void observeExecution(const psx::R3000State &state, std::uint32_t pc) {
    if (pc == profile_.movie_playback_init_entry &&
        !movie_presenter_pending_) {
      std::uint32_t completion{};
      if (vm_.runtime().read32(state.gpr[29U] + 0x14U, completion)) {
        movie_presenter_pending_ = true;
        movie_presenter_slices_ = 0U;
        movie_presenter_completion_ = completion;
      }
    }
    if (pc != profile_.gpu_submission_entry ||
        state.gpr[31U] != profile_.render_submission_return) {
      return;
    }
    ++gpu_submissions_;
    std::uint32_t application_state{};
    if (!vm_.runtime().read32(profile_.application_state,
                              application_state)) {
      return;
    }
    auto frame = captureSf3PresentationFrame(
        vm_.runtime().ram(), state.gpr[5U], application_state,
        gpu_submissions_, guest_frames_ + 1U);
    if (!frame || frame->draw_command_count == 0U) {
      return;
    }
    ++presentation_frames_;
    const auto publish =
        application_state == 7U ||
        (application_state == 0U && frame->draw_command_count >= 100U);
    if (!publish) {
      return;
    }
    ++guest_frames_;
    presentation_frame_ =
        std::make_shared<const Sf2PresentationFrame>(std::move(*frame));
  }

  [[nodiscard]] bool completeMovie() {
    if (!movie_presenter_pending_) {
      return true;
    }
    const auto continuation = vm_.runtime().state();
    LegacyGameplayVmResult completion;
    if (!vm_.runtime().write8(0x8014af10U, 0U) ||
        movie_presenter_completion_ == 0U ||
        !vm_.runtime().beginCall(movie_presenter_completion_, {})) {
      return false;
    }
    vm_.runtime().setRegister(31U, return_trampoline_);
    completion = vm_.runCurrentPcUntilHostBoundaryClockNeutral(
        return_trampoline_, 5'000'000U);
    vm_.runtime().restoreCpuState(continuation);
    if (!completion.completed() && !completion.stoppedAtHostBoundary()) {
      return false;
    }
    movie_presenter_pending_ = false;
    movie_presenter_slices_ = 0U;
    return true;
  }

  [[nodiscard]] bool servicePlatform(std::uint64_t consumed) {
    ++platform_slices_;
    platform_ticks_ += consumed;
    std::uint32_t poll_word{};
    if (vm_.runtime().read32(bootstrap_poll_word_, poll_word) &&
        poll_word != 0U &&
        vm_.machine().dma().scheduledToken(psx::DmaChannel::spu) == 0U) {
      LegacyGameplayVmResult callback;
      if (!vm_.servicePsxCallback(profile_.spu_dma_completion_callback,
                                  profile_.interrupt_stack, &callback)) {
        return false;
      }
    }
    vm_.machine().advanceHardwareTicks(consumed);
    const auto &layout = disc_.game()->executable_layout;
    const auto elapsed_retraces = retrace_clock_.advance(consumed);
    if (elapsed_retraces != 0U) {
      std::uint32_t retrace{};
      if (!vm_.runtime().read32(layout.retrace_counter_address, retrace) ||
          !vm_.runtime().write32(
              layout.retrace_counter_address,
              retrace + static_cast<std::uint32_t>(elapsed_retraces))) {
        return false;
      }
      retrace_increments_ += elapsed_retraces;
    }
    LegacyGameplayVmResult cd_callback;
    if (!vm_.servicePsxCdReadyCallback(&cd_callback,
                                       profile_.interrupt_stack)) {
      return false;
    }
    task_scheduler_ticks_ += consumed;
    while (task_scheduler_ticks_ >= task_scheduler_period_) {
      task_scheduler_ticks_ -= task_scheduler_period_;
      LegacyGameplayVmResult task_callback;
      if (!vm_.servicePsxCallback(profile_.task_scheduler_tick_entry,
                                  profile_.interrupt_stack,
                                  &task_callback)) {
        return false;
      }
      if (memory_card_info_event_pending_) {
        LegacyGameplayVmResult card_callback;
        if (!vm_.servicePsxCallback(memory_card_high_event_callback_,
                                    profile_.interrupt_stack,
                                    &card_callback)) {
          return false;
        }
        memory_card_info_event_pending_ = false;
      }
      if (memory_card_io_event_pending_) {
        LegacyGameplayVmResult card_callback;
        if (!vm_.servicePsxCallback(0x80149b60U, profile_.interrupt_stack,
                                    &card_callback)) {
          return false;
        }
        memory_card_io_event_pending_ = false;
      }
    }
    if (movie_presenter_pending_ && ++movie_presenter_slices_ >= 300U &&
        !completeMovie()) {
      return false;
    }
    return true;
  }

  template <typename StopPredicate>
  [[nodiscard]] bool runGuest(std::uint64_t budget, StopPredicate stop) {
    auto remaining = budget;
    auto result = vm_.resumeCurrentPcClockNeutral(
        std::min(remaining, scheduler_slice_budget_));
    for (;;) {
      const auto consumed =
          std::min(remaining, scheduler_slice_budget_);
      remaining -= consumed;
      if (stop()) {
        return true;
      }
      if (result.execution.reason != psx::R3000StopReason::instruction_budget ||
          remaining == 0U || !servicePlatform(consumed)) {
        if (result.execution.reason !=
                psx::R3000StopReason::instruction_budget ||
            remaining != 0U) {
          markFault("SF3 guest execution or platform callback stopped");
        }
        return false;
      }
      result = vm_.resumeCurrentPcClockNeutral(
          std::min(remaining, scheduler_slice_budget_));
    }
  }

  [[nodiscard]] bool runGuestInterval(std::uint64_t budget) {
    auto remaining = budget;
    while (remaining != 0U) {
      const auto slice = std::min(remaining, scheduler_slice_budget_);
      const auto result = vm_.resumeCurrentPcClockNeutral(slice);
      if (result.execution.reason !=
              psx::R3000StopReason::instruction_budget ||
          !servicePlatform(slice)) {
        markFault("SF3 guest execution or platform callback stopped");
        return false;
      }
      remaining -= slice;
    }
    return true;
  }

  [[nodiscard]] bool runUntilMissionGameplay() {
    return runGuest(200'000'000U, [this] {
      std::uint32_t state{};
      std::uint32_t depth{};
      return state8_confirmed_ && input_samples_ != 0U &&
             presentation_frame_ &&
             vm_.runtime().read32(profile_.application_state, state) &&
             vm_.runtime().read32(profile_.application_state_depth, depth) &&
             state == 0U && depth == 1U;
    });
  }

  GameDisc disc_;
  LegacyGameplayVm vm_;
  DiscCdRomMedia cdrom_media_;
  std::map<std::string, std::vector<std::byte>> tokyo_files_;
  std::vector<std::byte> tokyo_slf_;
  std::map<std::uint32_t,
           std::pair<const std::vector<std::byte> *, std::size_t>>
      tokyo_open_files_;
  LegacyHostPadState host_pad_;
  std::shared_ptr<const Sf2PresentationFrame> presentation_frame_;
  std::string fault_detail_;
  bool ready_{};
  bool faulted_{};
  bool state8_confirmed_{};
  std::uint64_t state8_pad_samples_{};
  std::uint64_t input_samples_{};
  std::uint64_t gpu_submissions_{};
  std::uint64_t presentation_frames_{};
  std::uint64_t guest_frames_{};
  std::uint64_t task_scheduler_ticks_{};
  sf::core::FixedCycleRateClock retrace_clock_{
      psx::CdRomController::cpu_clock_hz, 60U};
  std::uint64_t platform_slices_{};
  std::uint64_t platform_ticks_{};
  std::uint64_t retrace_increments_{};
  std::uint64_t vsync_queries_{};
  std::uint64_t vsync_nonnegative_{};
  std::uint64_t vsync_mode_zero_{};
  std::uint64_t vsync_mode_one_{};
  std::uint64_t vsync_mode_multiple_{};
  std::uint64_t cd_control_calls_{};
  std::uint64_t cd_readn_calls_{};
  bool memory_card_info_event_pending_{};
  bool memory_card_io_event_pending_{};
  bool blank_card_initial_info_seen_{};
  std::uint32_t memory_card_high_event_callback_{};
  bool movie_presenter_pending_{};
  std::uint64_t movie_presenter_slices_{};
  std::uint32_t movie_presenter_completion_{};
  std::uint64_t title_ready_pad_samples_{};
  std::uint64_t title_mode0_interactive_samples_{};
  std::uint64_t title_mode1_dialog_samples_{};
  std::uint64_t title_mode1_continue_samples_{};
  std::uint64_t title_mode10_pad_samples_{};
  std::uint64_t title2_pad_samples_{};
  std::uint64_t menu_pad_samples_{};
  bool title_mode1_dialog_active_{};
  bool title_mode1_seen_{};
};

Sf3GuestMissionRuntime::Sf3GuestMissionRuntime(
    const std::filesystem::path &cue_path)
    : impl_(std::make_unique<Impl>(cue_path)) {}

Sf3GuestMissionRuntime::~Sf3GuestMissionRuntime() = default;
Sf3GuestMissionRuntime::Sf3GuestMissionRuntime(
    Sf3GuestMissionRuntime &&) noexcept = default;
Sf3GuestMissionRuntime &Sf3GuestMissionRuntime::operator=(
    Sf3GuestMissionRuntime &&) noexcept = default;

bool Sf3GuestMissionRuntime::ready() const noexcept { return impl_->ready(); }
bool Sf3GuestMissionRuntime::faulted() const noexcept {
  return impl_->faulted();
}
std::string_view Sf3GuestMissionRuntime::faultDetail() const noexcept {
  return impl_->faultDetail();
}
void Sf3GuestMissionRuntime::setHostPadState(
    const LegacyHostPadState &state) noexcept {
  impl_->setHostPadState(state);
}
bool Sf3GuestMissionRuntime::advanceHostUpdate() noexcept {
  return impl_->advanceHostUpdate();
}
const std::shared_ptr<const Sf2PresentationFrame> &
Sf3GuestMissionRuntime::presentationFrame() const noexcept {
  return impl_->presentationFrame();
}
std::size_t Sf3GuestMissionRuntime::takePcm(
    std::span<psx::SpuPcmFrame> destination) noexcept {
  return impl_->takePcm(destination);
}
void Sf3GuestMissionRuntime::clearPcm() noexcept { impl_->clearPcm(); }
Sf3GuestRuntimeDiagnostics
Sf3GuestMissionRuntime::diagnostics() const noexcept {
  return impl_->diagnostics();
}

} // namespace sf::game
