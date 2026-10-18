#pragma once

// OCZ-WATER-01 global water material contract.
//
// The contract is header-only so classification and Fresnel behaviour can be
// verified without a D3D12 device, a DXR adapter, or the game runtime. CPU
// classification publishes one packed float4 per water instance; the DXR
// library reconstructs the same profile in WaterMaterial.hlsli. Keep the two
// default tables in sync.

#include <cmath>
#include <cstdint>
#include <string>

namespace Fast {

// Physical categories of an aquatic surface. These are semantic identities,
// not visual presets: the optical model is shared, the motion/absorption/
// blend weights differ so a lake, a river and a waterfall never collapse into
// one identical blue sheet.
enum class WaterClass : uint8_t {
    NONE = 0,               // not water / excluded surface
    WATER_SURFACE = 1,      // lakes, ponds and other still horizontal masses
    WATER_FLOWING = 2,      // rivers and directional currents
    WATER_FALLING = 3,      // waterfalls and vertical sheets
    WATER_SHALLOW = 4,      // water with a nearby bed
    WATER_SPECIAL = 5,      // authored/magic pools and effect overlays
    WATER_UNDERWATER_VIEW = 6, // camera below the surface
};

inline constexpr uint32_t WaterClassOrdinal(WaterClass cls) {
    return static_cast<uint32_t>(cls);
}

// Air/water interface. 1.333 gives a normal-incidence reflectance of 0.0204;
// this is a reflectance, never a roughness, alpha, or artistic mix weight.
inline constexpr float kWaterIor = 1.333f;
inline constexpr float kWaterF0 = 0.0204f;
inline constexpr float kWaterSigmaR = 0.42f; // per-metre absorption of red
inline constexpr float kWaterSigmaG = 0.11f;
inline constexpr float kWaterSigmaB = 0.07f;

struct WaterProfile {
    WaterClass cls = WaterClass::NONE;
    float waveScale = 1.0f;    // micro-wave amplitude multiplier
    float flowSpeed = 0.18f;   // analytic wave phase advance per frame unit
    float absorption = 0.30f;  // Beer-Lambert scale of the per-channel sigma
    float artWeight = 0.55f;   // chromatic match to the original Fast3D art
    float opticsBlend = 1.00f; // how much of the raster pixel optics replace
    float roughness = 0.035f;  // microfacet roughness used by reflection rays
};

// Schlick Fresnel for the air/water interface. cosTheta is the absolute
// cosine between the shading normal and the direction toward the observer.
// A non-finite cosine resolves to F0 (normal incidence) instead of leaking
// NaN into the radiance buffer, matching LavaThermalChroma's guard.
inline float WaterFresnelSchlick(float cosTheta, float f0 = kWaterF0) {
    const float c = (cosTheta == cosTheta)
        ? (cosTheta < 0.0f ? 0.0f : (cosTheta > 1.0f ? 1.0f : cosTheta))
        : 1.0f;
    const float m = 1.0f - c;
    const float m2 = m * m;
    return f0 + (1.0f - f0) * (m2 * m2 * m);
}

// Refractive index ratio for a ray crossing the air/water interface.
// enteringWater: the ray travels from air into water.
inline float WaterEta(bool enteringWater) {
    return enteringWater ? (1.0f / kWaterIor) : kWaterIor;
}

// Beer-Lambert transmittance over an optical path expressed in metres.
inline void WaterAbsorb(float distance, float scale, float outTransmittance[3]) {
    const float d = (distance > 0.0f && distance < 1.0e6f) ? distance : 0.0f;
    const float s = scale < 0.0f ? 0.0f : (scale > 4.0f ? 4.0f : scale);
    outTransmittance[0] = expf(-kWaterSigmaR * s * d);
    outTransmittance[1] = expf(-kWaterSigmaG * s * d);
    outTransmittance[2] = expf(-kWaterSigmaB * s * d);
}

// Asset tokens that mark a resource as genuinely aquatic. Matching a token is
// necessary but not sufficient: exclusions below run first, and geometry/
// render state still have to agree.
inline bool WaterPathHasAquaticToken(const std::string& lowerPath) {
    static const char* const kTokens[] = {
        "water", "river", "lake", "pond", "ocean", "liquid", "mizu", "stream",
        "aqua", "sea", "taki", "kawa", "umi", "wave", "ripple", "splash",
        "fountain", "oasis", "well", "aquarium", "pool", "cascade", "current",
    };
    for (const char* token : kTokens) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

// Tokens that mean the asset is NOT an aquatic liquid even when its name
// contains a water word: solid ice geometry, lava, glass, stone masonry,
// sky domes and 2D effect/prop billboards.
inline bool WaterPathIsExcluded(const std::string& lowerPath) {
    static const char* const kExcluded[] = {
        "iceberg", "iceramp", "icewall", "iceblock", "icefloor", "iceslab",
        "ice_", "_ice", "icicle", "glacier", "frozen",
        "lava", "magma", "hono", "kazan",
        "glass", "window", "pane", "lens", "garasu",
        "gate", "key", "door", "wall", "brick", "stone", "rock", "column",
        "platform", "medallion", "statue", "shockwave", "bombable",
        "skybox", "sky_", "cloud", "star", "moon", "sun",
    };
    for (const char* token : kExcluded) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

// Effect overlays drawn on top of water (splash sheets, ripple rings, foam
// sprites, dust) keep their native presentation: they receive a minimal share
// of the physical optics so the hyperreal model never turns a translucent
// sprite into an opaque mirror.
inline bool WaterPathIsEffectOverlay(const std::string& lowerPath) {
    static const char* const kOverlay[] = {
        "splash", "ripple", "foam", "spray", "mist", "drip", "drop",
        "effwater", "g effwater", "geff", "dust", "particle",
    };
    for (const char* token : kOverlay) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

inline WaterClass WaterClassFromPath(const std::string& lowerPath) {
    // Falling water first: "waterfall" also contains "waterfall"/"fall" and
    // must not be classified as a still surface.
    static const char* const kFalling[] = { "waterfall", "taki", "cascade", "fall", "waterdrop", "spout" };
    for (const char* token : kFalling) {
        if (lowerPath.find(token) != std::string::npos) return WaterClass::WATER_FALLING;
    }
    static const char* const kFlowing[] = { "river", "stream", "kawa", "flow", "current", "rapids", "goron" };
    for (const char* token : kFlowing) {
        if (lowerPath.find(token) != std::string::npos) return WaterClass::WATER_FLOWING;
    }
    static const char* const kShallow[] = { "shallow", "puddle", "spring", "ford", "wading", "well" };
    for (const char* token : kShallow) {
        if (lowerPath.find(token) != std::string::npos) return WaterClass::WATER_SHALLOW;
    }
    return WaterClass::WATER_SURFACE;
}

inline WaterProfile WaterProfileForClass(WaterClass cls) {
    WaterProfile profile;
    profile.cls = cls;
    switch (cls) {
        case WaterClass::WATER_FLOWING:
            profile.waveScale = 1.20f;
            profile.flowSpeed = 1.00f;
            profile.absorption = 0.26f;
            profile.artWeight = 0.55f;
            profile.opticsBlend = 1.00f;
            profile.roughness = 0.045f;
            break;
        case WaterClass::WATER_FALLING:
            profile.waveScale = 1.45f;
            profile.flowSpeed = 2.40f;
            profile.absorption = 0.10f;
            profile.artWeight = 0.72f;
            profile.opticsBlend = 0.85f;
            profile.roughness = 0.120f;
            break;
        case WaterClass::WATER_SHALLOW:
            profile.waveScale = 0.70f;
            profile.flowSpeed = 0.10f;
            profile.absorption = 0.08f;
            profile.artWeight = 0.50f;
            profile.opticsBlend = 0.95f;
            profile.roughness = 0.028f;
            break;
        case WaterClass::WATER_SPECIAL:
            profile.waveScale = 0.65f;
            profile.flowSpeed = 0.35f;
            profile.absorption = 0.12f;
            profile.artWeight = 0.80f;
            profile.opticsBlend = 0.35f;
            profile.roughness = 0.050f;
            break;
        case WaterClass::WATER_UNDERWATER_VIEW:
            profile.waveScale = 0.90f;
            profile.flowSpeed = 0.25f;
            profile.absorption = 0.34f;
            profile.artWeight = 0.45f;
            profile.opticsBlend = 0.90f;
            profile.roughness = 0.040f;
            break;
        case WaterClass::WATER_SURFACE:
        default:
            profile.waveScale = 1.00f;
            profile.flowSpeed = 0.18f;
            profile.absorption = 0.30f;
            profile.artWeight = 0.55f;
            profile.opticsBlend = 1.00f;
            profile.roughness = 0.035f;
            break;
    }
    return profile;
}

// Classify one draw whose resolved material type is WATER. `lowerPath` must
// already be lower-cased. The returned profile is conservative: an unproven
// surface keeps NONE so the renderer preserves the original Fast3D pixels.
inline WaterProfile ClassifyWaterDraw(const std::string& lowerPath, bool is2D, bool isRect,
                                      uint32_t alphaMode) {
    WaterProfile none;
    none.cls = WaterClass::NONE;
    none.opticsBlend = 0.0f;
    if (lowerPath.empty()) return none;
    if (WaterPathIsExcluded(lowerPath)) return none;
    // A 2D/rect draw is UI or a screen-space effect, never a world liquid.
    if (is2D || isRect) return none;
    if (!WaterPathHasAquaticToken(lowerPath)) {
        // A render state alone does not prove water; the type may have come
        // from a pilot rule that no longer matches the live resource.
        return none;
    }
    WaterClass cls = WaterPathIsEffectOverlay(lowerPath) ? WaterClass::WATER_SPECIAL
                                                         : WaterClassFromPath(lowerPath);
    // Cutout water (alpha mode 1) is a sheet whose holes reveal the bed; keep
    // a milder optics blend so those holes stay honest.
    WaterProfile profile = WaterProfileForClass(cls);
    if (alphaMode == 1u) profile.opticsBlend *= 0.75f;
    return profile;
}

inline const char* WaterClassName(WaterClass cls) {
    switch (cls) {
        case WaterClass::WATER_SURFACE: return "WATER_SURFACE";
        case WaterClass::WATER_FLOWING: return "WATER_FLOWING";
        case WaterClass::WATER_FALLING: return "WATER_FALLING";
        case WaterClass::WATER_SHALLOW: return "WATER_SHALLOW";
        case WaterClass::WATER_SPECIAL: return "WATER_SPECIAL";
        case WaterClass::WATER_UNDERWATER_VIEW: return "WATER_UNDERWATER_VIEW";
        default: return "NONE";
    }
}

} // namespace Fast
