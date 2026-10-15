#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace Fast {

using Fast3DCombinerColor = std::array<float, 4>;

struct Fast3DCombinerInputs {
    Fast3DCombinerColor texel0{};
    Fast3DCombinerColor texel1{};
    Fast3DCombinerColor primitive{};
    Fast3DCombinerColor shade{};
    Fast3DCombinerColor environment{};
};

inline uint32_t Fast3DCombinerField(uint64_t mode, uint32_t offset, uint32_t count) {
    return static_cast<uint32_t>((mode >> offset) & ((1ull << count) - 1ull));
}

inline float Fast3DCombinerSaturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline bool Fast3DCombinerColorMux(uint32_t mux, uint32_t cycle, uint32_t muxKind,
                                   const Fast3DCombinerColor& combined,
                                   const Fast3DCombinerInputs& inputs,
                                   Fast3DCombinerColor& value) {
    value = {};
    const auto& cycleTexel0 = cycle == 0u ? inputs.texel0 : inputs.texel1;
    const auto& cycleTexel1 = cycle == 0u ? inputs.texel1 : inputs.texel0;
    if (mux == 31u) return true;
    if (mux == 0u) {
        if (cycle == 0u) return false;
        value = combined;
        return true;
    }
    if (mux == 1u) { value = cycleTexel0; return true; }
    if (mux == 2u) { value = cycleTexel1; return true; }
    if (mux == 3u) { value = inputs.primitive; return true; }
    if (mux == 4u) { value = inputs.shade; return true; }
    if (mux == 5u) { value = inputs.environment; return true; }
    if (mux == 6u) { value = {1.0f, 1.0f, 1.0f, 1.0f}; return true; }
    float alpha = 0.0f;
    if (muxKind == 1u && mux == 7u) alpha = combined[3];
    else if (muxKind == 1u && mux == 8u) alpha = cycleTexel0[3];
    else if (muxKind == 1u && mux == 9u) alpha = cycleTexel1[3];
    else if (muxKind == 1u && mux == 10u) alpha = inputs.primitive[3];
    else if (muxKind == 1u && mux == 11u) alpha = inputs.shade[3];
    else if (muxKind == 1u && mux == 12u) alpha = inputs.environment[3];
    else return false;
    value = {alpha, alpha, alpha, alpha};
    return true;
}

inline bool Fast3DCombinerAlphaMux(uint32_t mux, uint32_t cycle, float combined,
                                  const Fast3DCombinerInputs& inputs, float& value) {
    if (mux == 0u) {
        if (cycle == 0u) return false;
        value = combined;
    } else if (mux == 1u) value = cycle == 0u ? inputs.texel0[3] : inputs.texel1[3];
    else if (mux == 2u) value = cycle == 0u ? inputs.texel1[3] : inputs.texel0[3];
    else if (mux == 3u) value = inputs.primitive[3];
    else if (mux == 4u) value = inputs.shade[3];
    else if (mux == 5u) value = inputs.environment[3];
    else if (mux == 6u) value = 1.0f;
    else if (mux == 7u) value = 0.0f;
    else return false;
    return true;
}

// CPU reference for the supported Fast3D subset evaluated by dxr_shadow.hlsl.
// Unsupported LOD/prim-LOD/noise selectors deliberately return false so the
// caller can retain the raster's original material color.
inline bool EvaluateFast3DCombinerReference(uint64_t mode, uint32_t otherModeH,
                                            const Fast3DCombinerInputs& inputs,
                                            Fast3DCombinerColor& result) {
    result = {1.0f, 1.0f, 1.0f, 1.0f};
    const uint32_t cycleType = (otherModeH >> 20u) & 3u;
    const uint32_t cycleCount = cycleType == 1u ? 2u : 1u;
    Fast3DCombinerColor combined{1.0f, 1.0f, 1.0f, 1.0f};
    for (uint32_t cycle = 0; cycle < cycleCount; ++cycle) {
        const uint32_t baseBit = cycle * 28u;
        uint32_t rgbA = Fast3DCombinerField(mode, baseBit, 4u);
        uint32_t rgbB = Fast3DCombinerField(mode, baseBit + 4u, 4u);
        uint32_t rgbC = Fast3DCombinerField(mode, baseBit + 8u, 5u);
        uint32_t rgbD = Fast3DCombinerField(mode, baseBit + 13u, 3u);
        const uint32_t alphaA = Fast3DCombinerField(mode, baseBit + 16u, 3u);
        const uint32_t alphaB = Fast3DCombinerField(mode, baseBit + 19u, 3u);
        const uint32_t alphaC = Fast3DCombinerField(mode, baseBit + 22u, 3u);
        const uint32_t alphaD = Fast3DCombinerField(mode, baseBit + 25u, 3u);
        if (rgbA >= 8u) rgbA = 31u;
        if (rgbB >= 8u) rgbB = 31u;
        if (rgbC >= 16u) rgbC = 31u;
        if (rgbD == 7u) rgbD = 31u;

        Fast3DCombinerColor color{};
        if (rgbA == rgbB || rgbC == 31u) {
            Fast3DCombinerColor d{};
            if (!Fast3DCombinerColorMux(rgbD, cycle, 2u, combined, inputs, d)) return false;
            color = d;
        } else {
            Fast3DCombinerColor a{}, b{}, c{}, d{};
            if (!Fast3DCombinerColorMux(rgbA, cycle, 0u, combined, inputs, a) ||
                !Fast3DCombinerColorMux(rgbB, cycle, 0u, combined, inputs, b) ||
                !Fast3DCombinerColorMux(rgbC, cycle, 1u, combined, inputs, c) ||
                !Fast3DCombinerColorMux(rgbD, cycle, 2u, combined, inputs, d)) return false;
            for (size_t channel = 0; channel < 3; ++channel) {
                color[channel] = Fast3DCombinerSaturate((a[channel] - b[channel]) * c[channel] + d[channel]);
            }
        }

        float alpha = 0.0f;
        if (alphaA == alphaB || alphaC == 7u) {
            if (!Fast3DCombinerAlphaMux(alphaD, cycle, combined[3], inputs, alpha)) return false;
        } else {
            // These C-slot values are LOD/primitive-LOD fractions in Fast3D.
            if (alphaC == 0u || alphaC == 6u) return false;
            float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
            if (!Fast3DCombinerAlphaMux(alphaA, cycle, combined[3], inputs, a) ||
                !Fast3DCombinerAlphaMux(alphaB, cycle, combined[3], inputs, b) ||
                !Fast3DCombinerAlphaMux(alphaC, cycle, combined[3], inputs, c) ||
                !Fast3DCombinerAlphaMux(alphaD, cycle, combined[3], inputs, d)) return false;
            alpha = Fast3DCombinerSaturate((a - b) * c + d);
        }
        color[3] = alpha;
        combined = color;
    }
    result = combined;
    return true;
}

} // namespace Fast
