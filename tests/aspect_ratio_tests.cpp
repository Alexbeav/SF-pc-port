#include "PsyX/PsyX_public.h"
#include "sf/platform/sf2_widescreen_policy.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

bool near(float first, float second) {
    return std::abs(first - second) < 0.0001F;
}

void testOriginalFourThree() {
    const auto widescreen = PsyX_CalculatePresentationViewport(
        1920, 1080, PSYX_ASPECT_ORIGINAL_4_3);
    require(
        widescreen.x == 240 && widescreen.y == 0 &&
            widescreen.w == 1440 && widescreen.h == 1080,
        "Original mode did not pillarbox a 16:9 drawable to 4:3");

    const auto tall = PsyX_CalculatePresentationViewport(
        1280, 1024, PSYX_ASPECT_ORIGINAL_4_3);
    require(
        tall.x == 0 && tall.y == 32 && tall.w == 1280 && tall.h == 960,
        "Original mode did not letterbox a tall drawable to 4:3");
}

void testAdaptiveUsesEntireDrawable() {
    const auto widescreen = PsyX_CalculatePresentationViewport(
        1920, 1080, PSYX_ASPECT_ADAPTIVE);
    require(
        widescreen.x == 0 && widescreen.y == 0 &&
            widescreen.w == 1920 && widescreen.h == 1080,
        "Adaptive mode did not use the complete 16:9 drawable");

    const auto ultrawide = PsyX_CalculatePresentationViewport(
        3440, 1440, PSYX_ASPECT_ADAPTIVE);
    require(
        ultrawide.x == 0 && ultrawide.y == 0 &&
            ultrawide.w == 3440 && ultrawide.h == 1440,
        "Adaptive mode unexpectedly clamped an ultrawide drawable");
}

void testAdaptivePreservesPixelAspect() {
    const auto widescreen = PsyX_CalculatePresentationScale(
        1920, 1080, PSYX_ASPECT_ADAPTIVE);
    require(
        near(widescreen.x, 0.75F) && near(widescreen.y, 1.0F),
        "Adaptive widescreen did not use an undistorted Hor+ scale");

    const auto tall = PsyX_CalculatePresentationScale(
        1280, 1024, PSYX_ASPECT_ADAPTIVE);
    require(
        near(tall.x, 1.0F) && near(tall.y, 0.9375F),
        "Adaptive narrow output did not use an undistorted Vert+ scale");

    const auto original = PsyX_CalculatePresentationScale(
        3440, 1440, PSYX_ASPECT_ORIGINAL_4_3);
    require(
        near(original.x, 1.0F) && near(original.y, 1.0F),
        "Original mode unexpectedly changed its presentation scale");
}

void testInvalidSizeIsBounded() {
    const auto viewport = PsyX_CalculatePresentationViewport(
        0, -1, PSYX_ASPECT_ADAPTIVE);
    require(
        viewport.x == 0 && viewport.y == 0 &&
            viewport.w == 1 && viewport.h == 1,
        "Viewport dimensions were not bounded to one pixel");
}

void testSf2FullscreenEffectOwnership() {
    using sf::platform::Sf2PrimitiveBounds;
    require(sf::platform::sf2AuxiliaryPrimitiveOwnsFullWidth(
                Sf2PrimitiveBounds{-192, -120, 192, 120}),
            "SF2 full-frame effect lost adaptive-width ownership");
    require(sf::platform::sf2AuxiliaryPrimitiveOwnsFullWidth(
                Sf2PrimitiveBounds{-192, 70, 192, 120}),
            "SF2 cinematic bar lost adaptive-width ownership");
    require(!sf::platform::sf2AuxiliaryPrimitiveOwnsFullWidth(
                Sf2PrimitiveBounds{-64, -32, 64, 32}),
            "Centered SF2 HUD primitive was mistaken for a fullscreen effect");
    require(sf::platform::sf2AnimatedCinematicMatteOwnsFullWidth(
                Sf2PrimitiveBounds{-160, -120, 160, -104}, 0U) &&
                !sf::platform::sf2AnimatedCinematicMatteOwnsFullWidth(
                    Sf2PrimitiveBounds{-160, -120, 160, -104}, 0x202020U),
            "SF2 animated matte policy accepted a non-black UI primitive");
}

} // namespace

int main() {
    try {
        testOriginalFourThree();
        testAdaptiveUsesEntireDrawable();
        testAdaptivePreservesPixelAspect();
        testInvalidSizeIsBounded();
        testSf2FullscreenEffectOwnership();
        std::cout << "Aspect ratio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Aspect ratio tests failed: " << error.what() << '\n';
        return 1;
    }
}
