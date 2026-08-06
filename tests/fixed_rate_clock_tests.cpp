#include "sf/core/fixed_rate_clock.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

std::size_t updatesAfterOneSecond(std::size_t presentation_hz) {
  sf::core::FixedStepClock clock{20.0, 5U};
  std::size_t updates{};
  for (std::size_t frame = 0; frame < presentation_hz; ++frame) {
    clock.addElapsed(1.0 / static_cast<double>(presentation_hz));
    updates += clock.takeReadyUpdates(4U);
  }
  return updates;
}

void testPresentationRateDoesNotAccelerateRetailSimulation() {
  for (const auto presentation_hz : std::array<std::size_t, 4U>{30U, 60U,
                                                                120U, 240U}) {
    require(updatesAfterOneSecond(presentation_hz) == 20U,
            "Host presentation frequency changed the SF3 retail cadence");
  }
}

void testInstructionSlicesDoNotAccelerateRetrace() {
  constexpr std::uint64_t cpu_hz = 33'868'800U;
  sf::core::FixedCycleRateClock retrace{cpu_hz, 60U};
  std::uint64_t events{};
  auto remaining = cpu_hz;
  while (remaining != 0U) {
    const auto slice = std::min<std::uint64_t>(remaining, 50'000U);
    events += retrace.advance(slice);
    remaining -= slice;
  }
  require(events == 60U,
          "Scheduler slice count leaked into the PS1 retrace cadence");
}

} // namespace

int main() {
  testPresentationRateDoesNotAccelerateRetailSimulation();
  testInstructionSlicesDoNotAccelerateRetrace();
  return 0;
}
