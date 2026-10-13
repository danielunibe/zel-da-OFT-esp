#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fast {
struct RtxQualitySettings {
    float shadowScale;
    float surfaceScale;
    unsigned indirectSamples;
    unsigned shadowSamples;
};
inline RtxQualitySettings GetRtxQualitySettings(int level) {
    constexpr RtxQualitySettings levels[] = {
        { 0.75f, 0.5f, 1, 1 }, // Low
        { 1.0f,  0.5f, 2, 2 }, // Medium
        { 1.0f,  0.75f, 4, 4 }, // High
        { 1.0f,  1.0f,  8, 8 } // Ultra: native masks, smoother soft shadows and higher-quality GI
    };
    return levels[std::clamp(level, 0, 3)];
}
inline uint32_t RtxScaledExtent(uint32_t size, float scale) {
    return std::max(1u, static_cast<uint32_t>(std::ceil(size * scale)));
}
// The elapsed interval comes from a monotonic clock, not FPS-scaled counters.
// Only completed GPU frame samples may be passed to this controller.
class RtxQualityController {
public:
    int level = 3;
    float lowTierResolutionScale = 1.0f;
    void Reset() { overSeconds = underSeconds = 0; filteredCost=0; hasFilteredCost=false; lowTierResolutionScale=1.0f; }
    bool Update(double gpuMs, double cpuMs, double dt, double targetHz, bool suspended) {
        (void)cpuMs;
        if (suspended || !std::isfinite(gpuMs) ||
            gpuMs <= 0 || dt <= 0 || dt > 0.25) {
            Reset(); return false;
        }
        const double budget = 1000.0 / std::clamp(targetHz, 1.0, 60.0);
        // Budget strictly against DXR GPU execution time; CPU wall time includes 20Hz/60Hz frame pacing
        const double rawCost = gpuMs;
        filteredCost = hasFilteredCost ? filteredCost+(1.0-std::exp(-dt/0.25))*(rawCost-filteredCost) : rawCost;
        hasFilteredCost=true;
        const double cost=filteredCost;
        overSeconds = cost > budget * 0.95 ? overSeconds + dt : 0;
        underSeconds = cost < budget * 0.70 ? underSeconds + dt : 0;
        if (overSeconds >= 2.0 && level > 0) { --level; Reset(); return true; }
        if (underSeconds >= 10.0 && level < 3) { ++level; Reset(); return true; }
        // Keep the rays enabled when even the lowest quality tier misses the
        // frame budget. Scale only the DXR masks; the original raster image
        // remains at native resolution and temporal filtering can reuse the
        // additional headroom without oscillating between named tiers.
        if (level == 0 && overSeconds >= 0.75 && lowTierResolutionScale > 0.5f) {
            lowTierResolutionScale = std::max(0.5f, lowTierResolutionScale * 0.92f);
            overSeconds = underSeconds = 0;
            return true;
        }
        if (level == 0 && underSeconds >= 5.0 && lowTierResolutionScale < 1.0f) {
            lowTierResolutionScale = std::min(1.0f, lowTierResolutionScale + 0.025f);
            overSeconds = underSeconds = 0;
            return true;
        }
        return false;
    }
private:
    double overSeconds = 0, underSeconds = 0, filteredCost=0;
    bool hasFilteredCost=false;
};
} // namespace Fast
