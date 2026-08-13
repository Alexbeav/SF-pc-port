#pragma once

#include <string_view>

namespace sf::platform {

inline constexpr std::string_view baseline_title{"Syphon Filter"};
inline constexpr std::string_view baseline_region{"USA / NTSC-U"};
inline constexpr std::string_view baseline_disc_serial{"SCUS-94240"};
inline constexpr std::string_view baseline_disc_revision{"v1.1"};
inline constexpr std::string_view baseline_retail_executable_sha256{
    "bac292061ad5bc718ce137ef5b43d3d7e9b1b65248fb0d52229f328ccfe4ab4e"};
inline constexpr std::string_view baseline_selected_source_base{
    "c24ce313b1356da2e3d5615f3001f6000e399f99"};
inline constexpr std::string_view baseline_bios_policy{
    "not-consumed-native-runtime"};
inline constexpr std::string_view baseline_fast_boot_policy{
    "unavailable-no-bios-boot-path"};

} // namespace sf::platform
