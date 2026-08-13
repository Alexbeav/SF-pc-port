#include "sf/platform/baseline_policy.hpp"
#include "sf/platform/host.hpp"
#include "sf/platform/player_input.hpp"

#include <iostream>

int main() {
  const sf::platform::GraphicsSettings graphics;
  const auto input = sf::platform::defaultKeyboardMouseBindings();
  const auto faithful =
      graphics.width == 640 && graphics.height == 480 &&
      graphics.msaa_samples == 0 && !graphics.bilinear_filtering &&
      !graphics.anisotropic_filtering && !graphics.pgxp_geometry &&
      graphics.aspect_ratio == sf::platform::AspectRatioMode::original_4_3 &&
      graphics.vsync && graphics.frame_limit == 20U &&
      !graphics.fullscreen && !input.mouse_chase_look;
  const auto identity =
      sf::platform::baseline_title == "Syphon Filter" &&
      sf::platform::baseline_region == "USA / NTSC-U" &&
      sf::platform::baseline_disc_serial == "SCUS-94240" &&
      sf::platform::baseline_disc_revision == "v1.1" &&
      sf::platform::baseline_retail_executable_sha256.size() == 64U &&
      sf::platform::baseline_selected_source_base.size() == 40U &&
      sf::platform::baseline_bios_policy == "not-consumed-native-runtime" &&
      sf::platform::baseline_fast_boot_policy ==
          "unavailable-no-bios-boot-path";
  if (!faithful || !identity) {
    std::cerr << "SF1 Phase 1 baseline policy mismatch\n";
    return 1;
  }
  return 0;
}
