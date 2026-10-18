#pragma once

// OCZ-LAVA-01 global lava material contract.
//
// Header-only so classification and thermal behaviour can be verified without
// a D3D12 device, a DXR adapter, or the game runtime. CPU classification
// publishes one packed float4 per lava instance; the DXR library reconstructs
// the same profile in LavaMaterial.hlsli. Keep the two default tables in sync.
//
// A surface is never claimed from a texture name alone. The classifier runs
// exclusions first (torch/flame billboards, 2D, UI, water, ice), then requires
// either a semantic lava token or an exact runtime-verified rule whose render
// state also agrees. Everything else keeps the original Fast3D pixels.

#include <cmath>
#include <cstdint>
#include <string>

namespace Fast {

// Physical categories of a molten surface. Semantic identities, not visual
// presets: the thermal model is shared, the flow/crust/emission weights differ
// so a pool, a river of melt and a falling stream never collapse into one
// identical orange sheet.
enum class LavaClass : uint8_t {
    NONE = 0,               // not lava / excluded surface
    LAVA_SURFACE = 1,       // pools and lakes of melt (Dodongo's Cavern floors)
    LAVA_FLOW = 2,          // directional melt channels and crusty rivers
    LAVA_FALL = 3,          // falling or pouring streams of melt
    LAVA_CRUST = 4,         // cooled skin drifting over melt (slow plates)
    LAVA_SPECIAL = 5,       // authored/magic melt and effect overlays
    VOLCANIC_HOT_ROCK = 6,  // hot basalt lit by nearby melt, not melt itself
};

inline constexpr uint32_t LavaClassOrdinal(LavaClass cls) {
    return static_cast<uint32_t>(cls);
}

// Thermal model constants. These are temperature anchors in an arbitrary
// 0-1 "heat" coordinate derived from the authored art plus flow noise; they
// are never hue picks. Chroma follows the anchors in LavaThermalChroma so the
// renderer and the contract agree.
inline constexpr float kLavaAbsoluteZeroHeat = 0.00f;
inline constexpr float kLavaRockHeat = 0.22f;   // dark basalt, no visible glow
inline constexpr float kLavaDullRedHeat = 0.42f;
inline constexpr float kLavaOrangeHeat = 0.68f;
inline constexpr float kLavaYellowHeat = 0.86f;
inline constexpr float kLavaWhiteHeat = 1.00f;

// Per-channel absorption of a photon crossing a short path through melt.
// Melt is nearly opaque in blue and green; this is absorption, never a hue
// tint and never a roughness.
inline constexpr float kLavaSigmaR = 0.18f;
inline constexpr float kLavaSigmaG = 1.30f;
inline constexpr float kLavaSigmaB = 3.10f;

struct LavaProfile {
    LavaClass cls = LavaClass::NONE;
    float flowSpeed = 0.22f;    // viscous surface drift, world units per second
    float viscosity = 0.70f;    // 0 = free liquid, 1 = creeping sludge
    float crustAmount = 0.35f;  // how much of the surface is cooled skin
    float emissionGain = 1.00f; // thermal radiance multiplier
    float artWeight = 0.80f;    // how much of the authored art survives
    float opticsBlend = 1.00f;  // how much of the physical model replaces art
    float roughness = 0.55f;    // microfacet roughness used by reflection rays
};

// A draw's render-state evidence, used to corroborate a classification that
// cannot rest on the texture name. Values are the authored Fast3D prim/env
// colors normalized to 0-1.
struct LavaStateEvidence {
    bool is2D = false;
    bool isRect = false;
    uint32_t alphaMode = 0u;
    float primColor[3] = { 0.0f, 0.0f, 0.0f };
    float envColor[3] = { 0.0f, 0.0f, 0.0f };
};

// Thermal chroma at a given heat. Matches the blackbody-ish anchors above and
// is the single source of truth for the melt colour ramp on both CPU and GPU.
inline void LavaThermalChroma(float heat, float outRgb[3]) {
    // std::max/min do not propagate NaN the way saturate does on the GPU, so
    // the guard is written explicitly: a non-finite heat resolves to cold rock
    // instead of leaking NaN into the radiance buffer.
    const float h = (heat == heat && heat > 0.0f) ? (heat > 1.0f ? 1.0f : heat) : 0.0f;
    float r, g, b;
    if (h < kLavaRockHeat) {
        const float t = h / kLavaRockHeat;
        r = 0.055f + 0.115f * t;
        g = 0.020f + 0.035f * t;
        b = 0.012f + 0.014f * t;
    } else if (h < kLavaDullRedHeat) {
        const float t = (h - kLavaRockHeat) / (kLavaDullRedHeat - kLavaRockHeat);
        r = 0.170f + 0.520f * t;
        g = 0.055f + 0.055f * t;
        b = 0.026f + 0.010f * t;
    } else if (h < kLavaOrangeHeat) {
        const float t = (h - kLavaDullRedHeat) / (kLavaOrangeHeat - kLavaDullRedHeat);
        r = 0.690f + 0.310f * t;
        g = 0.110f + 0.430f * t;
        b = 0.036f + 0.030f * t;
    } else if (h < kLavaYellowHeat) {
        const float t = (h - kLavaOrangeHeat) / (kLavaYellowHeat - kLavaOrangeHeat);
        r = 1.000f;
        g = 0.540f + 0.410f * t;
        b = 0.066f + 0.180f * t;
    } else {
        const float t = (h - kLavaYellowHeat) / (1.0f - kLavaYellowHeat);
        r = 1.000f;
        g = 0.950f + 0.050f * t;
        b = 0.246f + 0.754f * t;
    }
    outRgb[0] = r;
    outRgb[1] = g;
    outRgb[2] = b;
}

// Beer-Lambert transmittance over an optical path expressed in metres.
inline void LavaAbsorb(float distance, float scale, float outTransmittance[3]) {
    const float d = (distance > 0.0f && distance < 1.0e6f) ? distance : 0.0f;
    const float s = scale < 0.0f ? 0.0f : (scale > 8.0f ? 8.0f : scale);
    outTransmittance[0] = expf(-kLavaSigmaR * s * d);
    outTransmittance[1] = expf(-kLavaSigmaG * s * d);
    outTransmittance[2] = expf(-kLavaSigmaB * s * d);
}

// Asset tokens that mark a resource as genuinely molten. Matching a token is
// necessary but not sufficient: exclusions below run first, and render state
// still has to agree.
inline bool LavaPathHasVolcanicToken(const std::string& lowerPath) {
    static const char* const kTokens[] = {
        "lava", "magma", "hono", "kazan", "molten", "melt", "pyroclastic",
        "basalt", "obsidian", "volcanic", "hotrock", "crater",
    };
    for (const char* token : kTokens) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

// Tokens that mean the asset is NOT a molten liquid even when its name
// contains a lava word: torch and flame billboards, fireballs, smoke, magic
// sprites, UI, water, ice and sky. Torch fire is the critical exclusion: the
// pilot table historically mapped several flame textures to LAVA, and those
// must never inherit the magma model.
inline bool LavaPathIsExcluded(const std::string& lowerPath) {
    static const char* const kExcluded[] = {
        "torch", "flame", "fireball", "bonfire", "campfire", "candle",
        "lantern", "sconce", "brazier", "smoke", "ember", "spark", "ash_",
        "magic", "spell", "nautilus", "ui_", "hud", "icon", "menu", "title",
        "sky", "cloud", "star", "moon", "sun_", "logo",
        "water", "river", "lake", "pond", "ocean", "mizu", "waterfall",
        "taki", "ice", "frozen", "glacier", "snow",
        "glass", "window", "pane", "lens",
    };
    for (const char* token : kExcluded) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

// Effect overlays drawn on top of melt (bubble sprites, heat shimmer cards,
// particle quads) keep their native presentation: they receive a minimal share
// of the physical optics so the hyperreal model never turns a translucent
// sprite into an opaque glowing sheet.
inline bool LavaPathIsEffectOverlay(const std::string& lowerPath) {
    static const char* const kOverlay[] = {
        "bubble", "effmagma", "geff", "particle", "shimmer", "heatspark",
        "glow", "halo",
    };
    for (const char* token : kOverlay) {
        if (lowerPath.find(token) != std::string::npos) return true;
    }
    return false;
}

// Runtime-verified exact rules. Keys are full lower-cased resource paths that
// were confirmed by scene-harness draw inventories in authentic volcanic
// scenes (whose textures carry no semantic names, e.g. HIDAN_room_2Tex_009628).
// A rule is only added once the harness evidence shows the draw is opaque,
// uses a warm authored prim/env tint, and is drawn as world geometry rather
// than a 2D billboard.
struct LavaExactRule {
    const char* lowerPath;
    LavaClass cls;
};
inline const LavaExactRule* LavaExactRules(size_t& count) {
    // Verified by decoding the CI8 textures out of oot.o2r against
    // ddan_boss_sceneTLUT_000E50 and by the draw inventory in
    // 20261016_190847_168_KING_DODONGO_LAVA_ARENA_NORMAL (opaque, is2D=0,
    // isRect=0, neutral vertex tint, alphaMode 0).
    //
    // King Dodongo's entry corridor carries its melt as unnamed scene
    // textures, so no path token can claim them:
    //   room_1Tex_0039D8 decodes to a blurry orange/yellow molten field.
    //   room_1Tex_0031D8 decodes to cobble rock over a lava band: the hot
    //     rock at the pool's edge, never a pool itself.
    // The cobble and light-marbled wall textures (sceneTex_001858,
    // sceneTex_001058, sceneTex_002058) are deliberately absent: they are
    // rock, not melt.
    static const LavaExactRule kRules[] = {
        { "scenes/shared/ddan_boss_scene/ddan_boss_room_1tex_0039d8", LavaClass::LAVA_SURFACE },
        { "scenes/shared/ddan_boss_scene/ddan_boss_room_1tex_0031d8", LavaClass::VOLCANIC_HOT_ROCK },
    };
    count = sizeof(kRules) / sizeof(kRules[0]);
    return kRules;
}

// Warm authored tint evidence: molten rock in OoT is modulated with an orange
// prim/env color. This is supporting evidence for an exact rule, never a
// standalone claim (torch flame shares the tint).
inline bool LavaStateSuggestsMoltenSurface(const LavaStateEvidence& state) {
    if (state.is2D || state.isRect) return false;
    if (state.alphaMode >= 2u) return false; // translucent billboards are effects
    const float hueBand[3] = { 0.55f, 0.18f, 0.04f }; // authored orange anchor
    const auto warmEnough = [&hueBand](const float c[3]) {
        const float chroma = (c[0] > c[2] ? c[0] - c[2] : 0.0f);
        return c[0] > 0.30f && chroma > hueBand[1] * 0.5f && c[1] < c[0] && c[2] < c[0] * 0.55f;
    };
    return warmEnough(state.primColor) || warmEnough(state.envColor);
}

// The corroborating state an exact-rule draw must have. OoT's volcanic scenes
// author the melt's orange directly into the texture pixels and leave the
// vertex tint neutral (prim white, env mid-grey), so a warm-tint requirement
// rejects real lava floors. What actually separates a melt surface from an
// effect sheet is that it is opaque world geometry rather than a billboard or
// a translucent quad, and that is what an exact rule requires. The rule's own
// exact path is the primary evidence; this only rules out a mis-keyed path
// that happens to point at a sprite.
inline bool LavaStateIsOpaqueWorldGeometry(const LavaStateEvidence& state) {
    if (state.is2D || state.isRect) return false;
    // Blended (translucent/additive) draws are effect sheets over the melt.
    if (state.alphaMode >= 2u) return false;
    return true;
}

inline LavaClass LavaClassFromPath(const std::string& lowerPath) {
    // A "lava rock" pair is the cooled plate over a melt pool (the King
    // Dodongo arena floor alternates the two). The rock half is basalt lit by
    // the melt beside it, never a pool of melt itself.
    if (lowerPath.find("lava") != std::string::npos &&
        (lowerPath.find("rock") != std::string::npos ||
         lowerPath.find("stone") != std::string::npos ||
         lowerPath.find("basalt") != std::string::npos)) {
        return LavaClass::VOLCANIC_HOT_ROCK;
    }
    // Falls and streams before surfaces: a path can contain both words.
    static const char* const kFalling[] = { "lavafall", "lavafalls", "lavapour", "lavastream", "lavacascade" };
    for (const char* token : kFalling) {
        if (lowerPath.find(token) != std::string::npos) return LavaClass::LAVA_FALL;
    }
    static const char* const kCrust[] = { "lava crust", "lava_crust", "crust", "cooled", "skin" };
    for (const char* token : kCrust) {
        if (lowerPath.find(token) != std::string::npos) return LavaClass::LAVA_CRUST;
    }
    static const char* const kFlowing[] = { "lavaflow", "lava_flow", "lavariver", "lavarun", "channel" };
    for (const char* token : kFlowing) {
        if (lowerPath.find(token) != std::string::npos) return LavaClass::LAVA_FLOW;
    }
    static const char* const kHotRock[] = { "hotrock", "hot_rock", "basalt", "volcanic" };
    for (const char* token : kHotRock) {
        if (lowerPath.find(token) != std::string::npos) return LavaClass::VOLCANIC_HOT_ROCK;
    }
    return LavaClass::LAVA_SURFACE;
}

inline LavaProfile LavaProfileForClass(LavaClass cls) {
    LavaProfile profile;
    profile.cls = cls;
    switch (cls) {
        case LavaClass::LAVA_FLOW:
            profile.flowSpeed = 0.38f;
            profile.viscosity = 0.62f;
            profile.crustAmount = 0.28f;
            profile.emissionGain = 1.05f;
            profile.artWeight = 0.78f;
            profile.opticsBlend = 1.00f;
            profile.roughness = 0.48f;
            break;
        case LavaClass::LAVA_FALL:
            profile.flowSpeed = 0.95f;
            profile.viscosity = 0.42f;
            profile.crustAmount = 0.08f;
            profile.emissionGain = 1.20f;
            profile.artWeight = 0.72f;
            profile.opticsBlend = 0.90f;
            profile.roughness = 0.34f;
            break;
        case LavaClass::LAVA_CRUST:
            profile.flowSpeed = 0.09f;
            profile.viscosity = 0.92f;
            profile.crustAmount = 0.78f;
            profile.emissionGain = 0.75f;
            profile.artWeight = 0.88f;
            profile.opticsBlend = 1.00f;
            profile.roughness = 0.72f;
            break;
        case LavaClass::LAVA_SPECIAL:
            profile.flowSpeed = 0.26f;
            profile.viscosity = 0.70f;
            profile.crustAmount = 0.15f;
            profile.emissionGain = 1.35f;
            profile.artWeight = 0.90f;
            profile.opticsBlend = 0.55f;
            profile.roughness = 0.50f;
            break;
        case LavaClass::VOLCANIC_HOT_ROCK:
            profile.flowSpeed = 0.05f;
            profile.viscosity = 1.00f;
            profile.crustAmount = 1.00f;
            profile.emissionGain = 0.45f;
            profile.artWeight = 0.95f;
            profile.opticsBlend = 0.85f;
            profile.roughness = 0.80f;
            break;
        case LavaClass::LAVA_SURFACE:
        default:
            profile.flowSpeed = 0.22f;
            profile.viscosity = 0.70f;
            profile.crustAmount = 0.35f;
            profile.emissionGain = 1.00f;
            profile.artWeight = 0.80f;
            profile.opticsBlend = 1.00f;
            profile.roughness = 0.55f;
            break;
    }
    return profile;
}

// Exact-rule lookup over the full resource path (not just the basename) so a
// scene-scoped lava floor cannot be claimed by a same-named prop elsewhere.
inline LavaClass LavaClassFromExactRules(const std::string& lowerPath) {
    size_t count = 0;
    const LavaExactRule* rules = LavaExactRules(count);
    for (size_t i = 0; i < count; ++i) {
        if (lowerPath == rules[i].lowerPath) return rules[i].cls;
    }
    return LavaClass::NONE;
}

// Classify one draw whose resolved material type is LAVA. `lowerPath` must
// already be lower-cased. The returned profile is conservative: an unproven
// surface keeps NONE so the renderer preserves the original Fast3D pixels.
inline LavaProfile ClassifyLavaDraw(const std::string& lowerPath, const LavaStateEvidence& state) {
    LavaProfile none;
    none.cls = LavaClass::NONE;
    none.opticsBlend = 0.0f;
    if (lowerPath.empty()) return none;
    if (state.is2D || state.isRect) return none;
    // A translucent draw is an effect sheet over the melt, never the melt.
    if (state.alphaMode >= 2u) return none;
    if (LavaPathIsExcluded(lowerPath)) return none;

    LavaClass cls = LavaClass::NONE;
    if (LavaPathHasVolcanicToken(lowerPath)) {
        cls = LavaPathIsEffectOverlay(lowerPath) ? LavaClass::LAVA_SPECIAL
                                                 : LavaClassFromPath(lowerPath);
    } else {
        // No semantic token. Accept only an exact runtime-verified rule, and
        // only when the draw is opaque world geometry. OoT authors the melt's
        // orange into the texture and leaves the vertex tint neutral, so a
        // warm-tint requirement would reject real lava floors; the exact path
        // plus a decoded texture is the evidence instead.
        cls = LavaClassFromExactRules(lowerPath);
        if (cls != LavaClass::NONE && !LavaStateIsOpaqueWorldGeometry(state)) {
            return none;
        }
    }
    if (cls == LavaClass::NONE) return none;

    LavaProfile profile = LavaProfileForClass(cls);
    // A cutout surface (alpha mode 1) has honest holes; keep milder optics so
    // the holes stay honest and the melt never fills them.
    if (state.alphaMode == 1u) profile.opticsBlend *= 0.80f;
    return profile;
}

inline const char* LavaClassName(LavaClass cls) {
    switch (cls) {
        case LavaClass::LAVA_SURFACE: return "LAVA_SURFACE";
        case LavaClass::LAVA_FLOW: return "LAVA_FLOW";
        case LavaClass::LAVA_FALL: return "LAVA_FALL";
        case LavaClass::LAVA_CRUST: return "LAVA_CRUST";
        case LavaClass::LAVA_SPECIAL: return "LAVA_SPECIAL";
        case LavaClass::VOLCANIC_HOT_ROCK: return "VOLCANIC_HOT_ROCK";
        default: return "NONE";
    }
}

} // namespace Fast
