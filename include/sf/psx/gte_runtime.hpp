#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sf::psx {

struct GteState {
    std::array<std::uint32_t, 32> data{};
    std::array<std::uint32_t, 32> control{};
    // PC presentation extension.  0x10000 is retail-exact; values below it
    // widen the horizontal camera cone by compressing only the projected X
    // component around OFX.  Y and every non-projection GTE operation remain
    // bit-exact.
    std::uint32_t horizontal_projection_scale{0x10000U};
};

// Presentation-only provenance for one RTPS/RTPT result. These are the exact
// fixed-point intermediates before the GTE packs and saturates SXY/SZ for the
// guest. Keeping this data outside GteState ensures modern presentation can
// observe a transform without changing snapshots or any guest-visible value.
struct GteProjectedVertex {
    std::int64_t camera_x_q12{};
    std::int64_t camera_y_q12{};
    std::int64_t camera_z_q12{};
    std::int64_t screen_x_q16{};
    std::int64_t screen_y_q16{};
    std::int32_t offset_x_q16{};
    std::int32_t offset_y_q16{};
    std::uint32_t packed_sxy{};
    std::uint16_t projection{};
};

struct GteProjectionTrace {
    std::array<GteProjectedVertex, 3U> vertices{};
    std::size_t count{};
};

struct GteVertexStoreTrace {
    std::uint32_t address{};
    GteProjectedVertex vertex{};
};

// Integer Geometry Transformation Engine state used by original gameplay math.
// Unsupported commands remain an explicit deterministic VM stop.
class GteRuntime final {
public:
    [[nodiscard]] static std::uint32_t readData(
        const GteState& state,
        std::uint8_t index) noexcept;
    [[nodiscard]] static std::uint32_t readControl(
        const GteState& state,
        std::uint8_t index) noexcept;
    static void writeData(
        GteState& state,
        std::uint8_t index,
        std::uint32_t value) noexcept;
    static void writeControl(
        GteState& state,
        std::uint8_t index,
        std::uint32_t value) noexcept;
    [[nodiscard]] static bool executeCommand(
        GteState& state,
        std::uint32_t instruction,
        GteProjectionTrace* projection_trace = nullptr) noexcept;
};

} // namespace sf::psx
