#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sf::core {

// Converts host presentation time into a bounded number of fixed simulation
// updates. Presentation frequency must not change the guest cadence.
class FixedStepClock final {
public:
  FixedStepClock(double updates_per_second,
                 std::size_t maximum_backlog_updates) noexcept
      : step_seconds_{1.0 / updates_per_second},
        maximum_backlog_seconds_{
            step_seconds_ * static_cast<double>(maximum_backlog_updates)} {}

  void addElapsed(double elapsed_seconds) noexcept {
    accumulator_seconds_ =
        std::min(accumulator_seconds_ + std::max(0.0, elapsed_seconds),
                 maximum_backlog_seconds_);
  }

  [[nodiscard]] std::size_t
  takeReadyUpdates(std::size_t maximum_updates) noexcept {
    const auto available = static_cast<std::size_t>(std::floor(
        (accumulator_seconds_ + 1.0e-9) / step_seconds_));
    const auto updates = std::min(available, maximum_updates);
    accumulator_seconds_ =
        std::max(0.0, accumulator_seconds_ -
                          step_seconds_ * static_cast<double>(updates));
    return updates;
  }

private:
  double step_seconds_{};
  double maximum_backlog_seconds_{};
  double accumulator_seconds_{};
};

// Divides an exact source clock into lower-frequency events without making
// the result depend on the caller's instruction/scheduler slice size.
class FixedCycleRateClock final {
public:
  constexpr FixedCycleRateClock(std::uint64_t source_ticks_per_second,
                                std::uint64_t events_per_second) noexcept
      : ticks_per_event_{events_per_second == 0U
                             ? 0U
                             : source_ticks_per_second / events_per_second} {}

  [[nodiscard]] constexpr std::uint64_t
  advance(std::uint64_t ticks) noexcept {
    if (ticks_per_event_ == 0U) {
      return 0U;
    }
    remainder_ticks_ += ticks;
    const auto events = remainder_ticks_ / ticks_per_event_;
    remainder_ticks_ %= ticks_per_event_;
    return events;
  }

private:
  std::uint64_t ticks_per_event_{};
  std::uint64_t remainder_ticks_{};
};

} // namespace sf::core
