#include "psycross_mission_start.hpp"
#include "psycross_audio_output.hpp"
#include "psycross_retail_briefing.hpp"
#include "psycross_scene_viewer.hpp"
#include "psycross_window_mode.hpp"

#include "sf/core/error.hpp"
#include "sf/game/gameplay.hpp"
#include "sf/game/legacy_first_mission_runtime.hpp"
#include "sf/game/mission.hpp"
#include "sf/game/mission_start.hpp"
#include "sf/game/sf2_runtime.hpp"

#include <PsyX/PsyX_globals.h>
#include <PsyX/PsyX_public.h>
#include <PsyX/PsyX_render.h>
#include <SDL.h>
#include <psx/libetc.h>
#include <psx/libgpu.h>
#include <psx/libpad.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <utility>

void PsyX_TakeScreenshot();

namespace sf::platform::detail {
namespace {

constexpr std::uint16_t confirm_buttons = 0x4000U | 0x08U;

std::uint16_t readButtons(const PADRAW &pad) noexcept {
  return static_cast<std::uint16_t>(pad.buttons[0]) |
         (static_cast<std::uint16_t>(pad.buttons[1]) << 8U);
}

bool sf2RetailPromptVisible(const game::Sf2PresentationFrame &frame) noexcept {
  auto prompt_glyphs = std::size_t{};
  for (const auto &packet : frame.packets) {
    if (game::sf2GpuCommandKind(packet) != game::Sf2GpuCommandKind::draw ||
        packet.gp0_words.size() != 4U) {
      continue;
    }
    const auto opcode =
        static_cast<std::uint8_t>(packet.gp0_words.front() >> 24U);
    if ((opcode & 0xfcU) != 0x64U) {
      continue;
    }
    const auto xy = packet.gp0_words[1U];
    const auto x = static_cast<std::int16_t>(xy);
    const auto y = static_cast<std::int16_t>(xy >> 16U);
    if (y == 85 && x >= 53 && x <= 166) {
      ++prompt_glyphs;
    }
  }
  return prompt_glyphs >= 16U;
}

} // namespace

PsyCrossMissionStart::PsyCrossMissionStart() = default;
PsyCrossMissionStart::~PsyCrossMissionStart() = default;

std::unique_ptr<game::GameplaySession>
PsyCrossMissionStart::takePreloadedGameplay() noexcept {
  return std::move(preloaded_gameplay_);
}

std::unique_ptr<PsyCrossAudioOutput>
PsyCrossMissionStart::takePreloadedAudio() noexcept {
  return std::move(preloaded_audio_);
}

std::unique_ptr<game::Sf2GuestMissionRuntime>
PsyCrossMissionStart::takePreloadedSf2Runtime() noexcept {
  return std::move(preloaded_sf2_runtime_);
}

std::uint16_t
PsyCrossMissionStart::run(const game::MissionPackage &mission, PADRAW &pad,
                          std::uint16_t previous_buttons,
                          const KeyboardMouseBindings &bindings,
                          const std::filesystem::path &cue_path,
                          std::optional<game::CampaignCarryState> carry) {
  // The retail briefing is also the level-loading boundary. Remove the STR
  // framebuffer and its texture-page residue before presenting it; the
  // scene viewer uploads a fresh mission working set after confirmation.
  RECT16 whole_vram{0, 0, 1024, 512};
  ClearImage(&whole_vram, 0, 0, 0);
  DrawSync(0);
  PsyCrossRetailBriefing retail_briefing{mission, bindings};
  auto guest_briefing = std::unique_ptr<game::Sf2GuestMissionRuntime>{};
  if (mission.gameId() == game::GameId::syphon_filter_2) {
    guest_briefing = std::make_unique<game::Sf2GuestMissionRuntime>(
        cue_path, mission.definition().index,
        game::Sf2GuestRuntimeStartMode::retail_briefing);
    if (!guest_briefing->ready()) {
      throw core::Error{
          core::ErrorCode::invalid_format,
          "Authentic SF2 retail briefing failed: " +
              std::string{guest_briefing->faultDetail()}};
    }
    guest_briefing->setHostPadState({});
    prepareSf2GuestRetailPresentation();
  }
  preloaded_gameplay_.reset();
  preloaded_sf2_runtime_.reset();
  preloaded_audio_ = std::make_unique<PsyCrossAudioOutput>(
      12U, mission.gameId() == game::GameId::syphon_filter_2
               ? "sf2-briefing"
               : "briefing");
  struct MissionPreload {
    std::unique_ptr<game::GameplaySession> gameplay;
    std::unique_ptr<game::Sf2GuestMissionRuntime> sf2_runtime;
  };
  auto preload = std::async(std::launch::async, [&mission, &cue_path, carry] {
    auto result = MissionPreload{};
    result.gameplay = std::make_unique<game::GameplaySession>(mission);
    // SF2's preloaded GameplaySession is only the native texture/residency
    // shell. Its authoritative player state lives in Sf2GuestMissionRuntime,
    // which applies the exact sequel carry after its retail mission bootstrap.
    // Applying it here as well routes the SF2 payload through the older
    // LegacyFirstMissionRuntime bridge; Mission 2 has not constructed that
    // bridge's player inventory at this loading boundary and rejects an
    // otherwise valid Mission 1 continuation before gameplay can start.
    if (mission.gameId() != game::GameId::syphon_filter_2 && carry &&
        !result.gameplay->applyCampaignCarryState(*carry)) {
      throw core::Error{core::ErrorCode::invalid_format,
                        "Campaign carry could not be applied to retail RAM"};
    }
    if (mission.gameId() == game::GameId::syphon_filter_2) {
      result.sf2_runtime = std::make_unique<game::Sf2GuestMissionRuntime>(
          cue_path, mission.definition().index);
      if (!result.sf2_runtime->ready()) {
        throw core::Error{
            core::ErrorCode::invalid_format,
            "SF2 gameplay preload failed: " +
                std::string{result.sf2_runtime->faultDetail()}};
      }
    }
    return result;
  });
  game::MissionStartGate gate;
  static_cast<void>(gate.update(
      (static_cast<std::uint16_t>(~previous_buttons) & confirm_buttons) != 0U,
      false));
  const auto performance_frequency = SDL_GetPerformanceFrequency();
  auto animation_start = std::optional<std::uint64_t>{};
  auto audio_callback_tick = std::optional<std::uint64_t>{};
  auto audio_clock_started = false;
  constexpr std::uint32_t retail_audio_callback_hz = 120U;
  constexpr std::uint64_t maximum_audio_updates_per_iteration = 30U;
  std::array<psx::SpuPcmFrame, 4096U> briefing_pcm{};
  std::uint64_t audio_slices_completed{};
  std::uint64_t audio_pcm_frames_pumped{};
  std::uint64_t audio_pcm_blocks_pumped{};
  std::uint64_t audio_diagnostic_sequence{};
  const auto periodic_audio_diagnostics = psyCrossAudioDiagnosticsEnabled();
  const auto capture_completed_briefing =
      SDL_getenv("SF2_CAPTURE_COMPLETED_BRIEFING") != nullptr;
  auto completed_briefing_captured = false;
  auto next_audio_diagnostic_counter =
      SDL_GetPerformanceCounter() + performance_frequency;
  const auto guest_clock_start = SDL_GetPerformanceCounter();
  auto guest_updates = std::uint64_t{};
  auto guest_confirmation_requested = false;
  auto drawn_guest_sequence = std::uint64_t{};
  const auto pump_audio = [&] {
    while (const auto count = preloaded_gameplay_->takePcm(briefing_pcm)) {
      audio_pcm_frames_pumped += count;
      ++audio_pcm_blocks_pumped;
      preloaded_audio_->queue(
          std::span<const psx::SpuPcmFrame>{briefing_pcm}.first(count));
    }
    preloaded_audio_->flush();
    preloaded_audio_->update();
  };
  PsyX_Log_Info(
      "Mission briefing: retail transition and gameplay preload started\n");
  for (;;) {
    PsyX_UpdateInput();
    previous_buttons = readButtons(pad);
    const auto held = static_cast<std::uint16_t>(~previous_buttons);
    int keyboard_count{};
    const auto *keyboard = SDL_GetKeyboardState(&keyboard_count);
    const auto mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    const auto keyboard_state =
        keyboard != nullptr && keyboard_count > 0
            ? std::span<const std::uint8_t>{
                  keyboard, static_cast<std::size_t>(keyboard_count)}
            : std::span<const std::uint8_t>{};
    const auto bound_actions = sampleKeyboardMouseActions(
        bindings,
        KeyboardMouseDeviceState{
            .keyboard = keyboard_state,
            .mouse_left = (mouse_buttons & SDL_BUTTON_LMASK) != 0U,
            .mouse_right = (mouse_buttons & SDL_BUTTON_RMASK) != 0U,
            .mouse_middle = (mouse_buttons & SDL_BUTTON_MMASK) != 0U,
            .mouse_x1 = (mouse_buttons & SDL_BUTTON_X1MASK) != 0U,
            .mouse_x2 = (mouse_buttons & SDL_BUTTON_X2MASK) != 0U,
            .mouse_wheel_delta = consumePsyCrossMouseWheel(),
        });

    if (!preloaded_gameplay_ && preload.wait_for(std::chrono::seconds{0}) ==
                                    std::future_status::ready) {
      auto loaded = preload.get();
      preloaded_gameplay_ = std::move(loaded.gameplay);
      preloaded_sf2_runtime_ = std::move(loaded.sf2_runtime);
      animation_start = SDL_GetPerformanceCounter();
      // The native SF2 package is renderable before its executable audio
      // callbacks are mapped. Keep the briefing responsive and enter the
      // scene without advancing the SF1-only guest audio clock.
      audio_clock_started =
          mission.gameId() == game::GameId::syphon_filter;
      PsyX_Log_Info("Mission briefing: gameplay preload complete\n");
    }

    const auto current_counter = SDL_GetPerformanceCounter();
    if (guest_briefing && performance_frequency != 0U) {
      const auto target_updates = static_cast<std::uint64_t>(
          static_cast<long double>(current_counter - guest_clock_start) *
          20.0L / static_cast<long double>(performance_frequency));
      auto updates_this_iteration = std::uint32_t{};
      while (guest_updates < target_updates &&
             updates_this_iteration < 4U) {
        if (!guest_briefing->advanceHostUpdate()) {
          if (guest_confirmation_requested &&
              guest_briefing->diagnostics().application_state != 8U &&
              preloaded_gameplay_) {
            PsyX_Log_Info(
                "Mission briefing: authentic retail state released; "
                "entering gameplay\n");
            return previous_buttons;
          }
          throw core::Error{
              core::ErrorCode::invalid_format,
              "Authentic SF2 retail briefing stopped: " +
                  std::string{guest_briefing->faultDetail()}};
        }
        ++guest_updates;
        ++updates_this_iteration;
        // State 8 is intentionally retired at 20 Hz. Its guest boundary
        // already produces one mixer slice, so supply the remaining five of
        // the six 120 Hz slices required by each 50 ms interval. The boundary
        // also dispatches two sequence callbacks; only four added slices need
        // the retail sound callback to reach the same six-per-tick cadence.
        for (auto audio_slice = 0U; audio_slice < 5U; ++audio_slice) {
          if (!guest_briefing->advanceRetailBriefingAudioSlice(
                  audio_slice < 4U)) {
            throw core::Error{
                core::ErrorCode::invalid_format,
                "Authentic SF2 briefing audio clock stopped: " +
                    std::string{guest_briefing->faultDetail()}};
          }
        }
      }
      while (const auto count = guest_briefing->takePcm(briefing_pcm)) {
        preloaded_audio_->queue(
            std::span<const psx::SpuPcmFrame>{briefing_pcm}.first(count));
      }
      preloaded_audio_->flush();
      preloaded_audio_->update();
      if (guest_confirmation_requested &&
          guest_briefing->diagnostics().application_state == 0U &&
          preloaded_gameplay_) {
        PsyX_Log_Info(
            "Mission briefing: authentic retail state confirmed; entering "
            "gameplay\n");
        return previous_buttons;
      }
    }
    const auto elapsed =
        animation_start ? current_counter - *animation_start : 0U;
    const auto retail_time =
        performance_frequency == 0U
            ? 0.0
            : static_cast<double>(elapsed) * 20.0 /
                  static_cast<double>(performance_frequency);
    if (audio_clock_started) {
      const auto audio_time =
          performance_frequency == 0U
              ? 0.0
              : static_cast<double>(elapsed) *
                    static_cast<double>(retail_audio_callback_hz) /
                    static_cast<double>(performance_frequency);
      const auto callback_tick = static_cast<std::uint64_t>(audio_time);
      if (!audio_callback_tick) {
        audio_callback_tick = callback_tick;
      }
      const auto pending_audio_updates =
          static_cast<std::size_t>(callback_tick - *audio_callback_tick);
      auto audio_updates = std::uint64_t{};
      while (*audio_callback_tick < callback_tick &&
             audio_updates < maximum_audio_updates_per_iteration) {
        if (!preloaded_gameplay_->advanceAudioSliceClock()) {
          throw core::Error{core::ErrorCode::invalid_format,
                            "Mission briefing audio clock failed"};
        }
        ++*audio_callback_tick;
        ++audio_slices_completed;
        ++audio_updates;
        pump_audio();
      }
      if (periodic_audio_diagnostics && performance_frequency != 0U &&
          current_counter >= next_audio_diagnostic_counter) {
        ++audio_diagnostic_sequence;
        preloaded_audio_->logDiagnostics("briefing-periodic");
        PsyX_Log_Info(
            "[AudioDiag][briefing-clock] sequence=%llu callback_tick=%llu "
            "pending=%zu slices=%llu pcm_frames=%llu pcm_blocks=%llu "
            "remaining=%llu\n",
            static_cast<unsigned long long>(audio_diagnostic_sequence),
            static_cast<unsigned long long>(*audio_callback_tick),
            pending_audio_updates,
            static_cast<unsigned long long>(audio_slices_completed),
            static_cast<unsigned long long>(audio_pcm_frames_pumped),
            static_cast<unsigned long long>(audio_pcm_blocks_pumped),
            static_cast<unsigned long long>(callback_tick -
                                            *audio_callback_tick));
        if (const auto guest = preloaded_gameplay_->audioDiagnostics()) {
          PsyX_Log_Info(
              "[AudioDiag][briefing-guest] sequence=%llu machine_tick=%llu "
              "audio_tick=%llu spu_sample=%llu mixed=%llu pcm_queued=%zu "
              "pcm_dropped=%llu cd_queued=%zu voices=%zu cd_read=%u "
              "cd_lba=%u xa_set=%u xa_file=%u xa_channel=%u\n",
              static_cast<unsigned long long>(audio_diagnostic_sequence),
              static_cast<unsigned long long>(guest->machine_tick),
              static_cast<unsigned long long>(guest->audio_frame_tick),
              static_cast<unsigned long long>(guest->spu_sample_clock),
              static_cast<unsigned long long>(guest->spu_mixed_frames),
              guest->spu_pcm_frames,
              static_cast<unsigned long long>(guest->spu_dropped_pcm_frames),
              guest->spu_cd_frames, guest->active_spu_voices,
              static_cast<unsigned int>(guest->cd_reading), guest->cd_lba,
              static_cast<unsigned int>(guest->xa_stream_set),
              static_cast<unsigned int>(guest->xa_file),
              static_cast<unsigned int>(guest->xa_channel));
        }
        next_audio_diagnostic_counter = current_counter + performance_frequency;
      }
    }
    auto text_animation_complete = false;
    if (!guest_briefing) {
      retail_briefing.prepare(retail_time);
    }
    if (guest_briefing) {
      if (const auto &frame = guest_briefing->presentationFrame()) {
        const auto fresh_presentation =
            frame->sequence != drawn_guest_sequence;
        if (beginSf2GuestFrame(*frame, fresh_presentation)) {
          if (fresh_presentation) {
            drawSf2GuestFrame(*frame, 0U,
                              preloaded_gameplay_ != nullptr);
            drawn_guest_sequence = frame->sequence;
          }
          text_animation_complete =
              frame->application_state == 8U &&
              sf2RetailPromptVisible(*frame);
          if (capture_completed_briefing && text_animation_complete &&
              guest_updates >= 180U &&
              !completed_briefing_captured) {
            PsyX_TakeScreenshot();
            completed_briefing_captured = true;
            PsyX_Log_Info(
                "Mission briefing: captured completed presentation\n");
          }
          PsyX_EndScene();
        }
      }
    } else if (PsyX_BeginScene() != 0) {
        text_animation_complete =
            retail_briefing.draw(mission.briefing(), retail_time);
      if (capture_completed_briefing && text_animation_complete &&
          !completed_briefing_captured) {
        PsyX_TakeScreenshot();
        completed_briefing_captured = true;
        PsyX_Log_Info("Mission briefing: captured completed presentation\n");
      }
      PsyX_EndScene();
    }
    if (gate.update((held & confirm_buttons) != 0U ||
                        bound_actions[KeyboardMouseAction::interact],
                    text_animation_complete &&
                        preloaded_gameplay_ != nullptr) &&
        preloaded_gameplay_) {
      if (guest_briefing) {
        // The state-8 VM exists only to present the authentic briefing. The
        // separately preloaded gameplay VM is already at Mission 1's playable
        // boundary; asking this disposable VM to bootstrap the mission again
        // creates a long, invisible post-X load that is immediately discarded.
        PsyX_Log_Info(
            "Mission briefing confirmed; entering preloaded SF2 gameplay\n");
        return previous_buttons;
      } else {
        PsyX_Log_Info("Mission briefing confirmed; entering gameplay\n");
        return previous_buttons;
      }
    }
  }
}

} // namespace sf::platform::detail
