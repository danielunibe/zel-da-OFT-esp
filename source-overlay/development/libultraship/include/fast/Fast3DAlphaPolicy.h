#pragma once

#include <cstdint>

namespace Fast {

enum class Fast3DAlphaClass : uint32_t { Opaque = 0, Cutout = 1, Blended = 2 };

struct Fast3DAlphaState {
    uint32_t alphaCompare;
    uint32_t coverageDestination;
    uint32_t zMode;
    bool coverageTimesAlpha;
    bool alphaFromCoverage;
    bool forceBlender;
    uint32_t cycleType;
    uint32_t otherModeL;
};

constexpr uint32_t Fast3DField(uint32_t word, uint32_t shift, uint32_t mask) {
    return (word >> shift) & mask;
}

constexpr bool Fast3DSourceOver(uint32_t word, uint32_t shift) {
    const uint32_t p = Fast3DField(word, shift, 3u);
    const uint32_t a = Fast3DField(word, shift - 4u, 3u);
    const uint32_t m = Fast3DField(word, shift - 8u, 3u);
    const uint32_t b = Fast3DField(word, shift - 12u, 3u);
    // G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA.
    return p == 0u && a == 0u && m == 1u && b == 0u;
}

constexpr bool Fast3DAdditive(uint32_t word, uint32_t shift) {
    const uint32_t p = Fast3DField(word, shift, 3u);
    const uint32_t a = Fast3DField(word, shift - 4u, 3u);
    const uint32_t m = Fast3DField(word, shift - 8u, 3u);
    const uint32_t b = Fast3DField(word, shift - 12u, 3u);
    // G_BL_CLR_IN, G_BL_A_FOG, G_BL_CLR_MEM, G_BL_1.
    return p == 0u && a == 1u && m == 1u && b == 2u;
}

constexpr Fast3DAlphaState DecodeFast3DAlphaState(uint32_t otherModeL, uint32_t otherModeH) {
    return { otherModeL & 3u, (otherModeL >> 8u) & 3u, (otherModeL >> 10u) & 3u,
             (otherModeL & 0x1000u) != 0u, (otherModeL & 0x2000u) != 0u,
             (otherModeL & 0x4000u) != 0u, (otherModeH >> 20u) & 3u, otherModeL };
}

constexpr Fast3DAlphaClass ClassifyFast3DAlphaCandidate(uint32_t otherModeL, uint32_t otherModeH) {
    const Fast3DAlphaState state = DecodeFast3DAlphaState(otherModeL, otherModeH);
    bool sourceOver = false;
    bool additive = false;
    if (state.cycleType == 0u) {
        sourceOver = Fast3DSourceOver(otherModeL, 30u);
        additive = Fast3DAdditive(otherModeL, 30u);
    } else if (state.cycleType == 1u) {
        // Two-cycle othermode stores the two blender equations in opposite
        // field groups. Either active cycle can establish a blend equation.
        sourceOver = Fast3DSourceOver(otherModeL, 28u) || Fast3DSourceOver(otherModeL, 30u);
        additive = Fast3DAdditive(otherModeL, 28u) || Fast3DAdditive(otherModeL, 30u);
    }
    if (state.forceBlender && (state.zMode == 2u || sourceOver || additive))
        return Fast3DAlphaClass::Blended;
    if (state.alphaCompare != 0u || state.coverageTimesAlpha)
        return Fast3DAlphaClass::Cutout;
    return Fast3DAlphaClass::Opaque;
}

constexpr const char* Fast3DAlphaClassName(Fast3DAlphaClass value) {
    return value == Fast3DAlphaClass::Blended ? "BLENDED" :
           value == Fast3DAlphaClass::Cutout ? "CUTOUT" : "OPAQUE";
}

static_assert((0x00000001u & 3u) == 1u, "G_AC_THRESHOLD occupies alpha-compare bits 0..1");
static_assert((0x00001000u & 0x00001000u) != 0u, "CVG_X_ALPHA is bit 12");
static_assert((0x00004000u & 3u) == 0u, "FORCE_BL is not alpha compare");
static_assert(Fast3DSourceOver((0u << 30u) | (0u << 26u) | (1u << 22u) | (0u << 18u), 30u),
              "GBL_c1 source-over selector contract");
static_assert(Fast3DAdditive((0u << 30u) | (1u << 26u) | (1u << 22u) | (2u << 18u), 30u),
              "GBL_c1 additive selector contract");
static_assert(ClassifyFast3DAlphaCandidate(0x00004000u | (3u << 26u) | (2u << 18u), 0u) ==
                  Fast3DAlphaClass::Opaque,
              "FORCE_BL with opaque blender equation must remain opaque");
static_assert(ClassifyFast3DAlphaCandidate(0x00004000u | (1u << 22u), 0u) ==
                  Fast3DAlphaClass::Blended,
              "source-over blender control must remain blended");
static_assert(ClassifyFast3DAlphaCandidate(0x00001000u, 0u) == Fast3DAlphaClass::Cutout,
              "CVG_X_ALPHA control must classify as cutout");
static_assert(ClassifyFast3DAlphaCandidate(0x00000001u, 0u) == Fast3DAlphaClass::Cutout,
              "G_AC_THRESHOLD control must classify as cutout");
static_assert(ClassifyFast3DAlphaCandidate(0x00004800u, 0u) == Fast3DAlphaClass::Blended,
              "ZMODE_XLU control must remain blended");

} // namespace Fast
