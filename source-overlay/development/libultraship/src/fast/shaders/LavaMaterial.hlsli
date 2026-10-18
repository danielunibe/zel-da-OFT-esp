// ============================================================
// OCZ-LAVA-01 - Hyperrealistic Magma Material (HLSL)
// Global molten-rock optical model for the Couch Edition DXR library.
//
// Contract:
//   * The original Fast3D lava texture stays the artistic identity: it is
//     sampled with the captured UVs and the game's own UV scroll, so the
//     pattern and its animation are the ones Ocarina of Time ships.
//   * The physical layer (two-phase viscous advection, cooled-crust plates,
//     thermal chroma, absorption, micro-relief normals) is added on top of
//     that identity, never instead of it. Water IOR / Fresnel / refraction
//     are never used: melt is opaque and emissive, not a dielectric liquid.
//   * Temperature is never inferred from hue. It comes from the authored art
//     luminance plus advected flow noise, and the chroma follows the shared
//     blackbody-ish anchors so CPU and GPU always agree.
//
// The profile rides in the same float4 slot water uses in the 128-byte
// record (ABI v5); it is keyed by material type on the GPU, so no record
// resize and no ABI bump are required. Keep the tables in sync with
// LavaMaterialContract.h.
// ============================================================

#ifndef COUCH_LAVA_MATERIAL_HLSLI
#define COUCH_LAVA_MATERIAL_HLSLI

static const uint LAVA_CLASS_NONE = 0u;
static const uint LAVA_CLASS_SURFACE = 1u;
static const uint LAVA_CLASS_FLOW = 2u;
static const uint LAVA_CLASS_FALL = 3u;
static const uint LAVA_CLASS_CRUST = 4u;
static const uint LAVA_CLASS_SPECIAL = 5u;
static const uint LAVA_CLASS_HOT_ROCK = 6u;

// Per-channel absorption of a photon crossing a short path through melt.
// Melt is nearly opaque in blue and green; this is absorption, never a hue
// tint and never a roughness.
static const float LAVA_SIGMA_R = 0.18f;
static const float LAVA_SIGMA_G = 1.30f;
static const float LAVA_SIGMA_B = 3.10f;

// Thermal anchors in the shared 0-1 heat coordinate.
static const float LAVA_HEAT_ROCK = 0.22f;
static const float LAVA_HEAT_DULL_RED = 0.42f;
static const float LAVA_HEAT_ORANGE = 0.68f;
static const float LAVA_HEAT_YELLOW = 0.86f;

struct LavaProfile {
    uint cls;
    float flowSpeed;
    float viscosity;
    float crustAmount;
    float emissionGain;
    float artWeight;
    float opticsBlend;
    float roughness;
};

// Decode the profile published by the CPU. Matches LavaProfileForClass in
// LavaMaterialContract.h; a zero optics channel means the record did not
// carry a lava profile, so the surface keeps the original Fast3D pixels.
LavaProfile LavaProfileFromRecord(float4 lavaOptics, float recordRoughness) {
    LavaProfile profile;
    profile.cls = (uint)max(lavaOptics.x, 0.0f);
    profile.flowSpeed = max(lavaOptics.y, 0.0f);
    profile.viscosity = saturate(lavaOptics.z);
    profile.crustAmount = saturate(lavaOptics.w);
    profile.emissionGain = 1.0f;
    profile.artWeight = 0.80f;
    profile.opticsBlend = 1.0f;
    profile.roughness = clamp(recordRoughness, 0.20f, 0.95f);
    switch (profile.cls) {
        case LAVA_CLASS_FLOW:
            profile.emissionGain = 1.05f; profile.artWeight = 0.78f;
            profile.roughness = clamp(profile.roughness, 0.25f, 0.80f); break;
        case LAVA_CLASS_FALL:
            profile.emissionGain = 1.20f; profile.artWeight = 0.72f;
            profile.opticsBlend = 0.90f;
            profile.roughness = clamp(profile.roughness, 0.18f, 0.60f); break;
        case LAVA_CLASS_CRUST:
            profile.emissionGain = 0.75f; profile.artWeight = 0.88f;
            profile.roughness = clamp(profile.roughness, 0.55f, 0.95f); break;
        case LAVA_CLASS_SPECIAL:
            profile.emissionGain = 1.35f; profile.artWeight = 0.90f;
            profile.opticsBlend = 0.55f; break;
        case LAVA_CLASS_HOT_ROCK:
            profile.emissionGain = 0.45f; profile.artWeight = 0.95f;
            profile.opticsBlend = 0.85f;
            profile.roughness = clamp(profile.roughness, 0.60f, 0.95f); break;
        case LAVA_CLASS_SURFACE:
        default:
            profile.emissionGain = 1.00f; profile.artWeight = 0.80f; break;
    }
    if (profile.cls == LAVA_CLASS_NONE) profile.opticsBlend = 0.0f;
    return profile;
}

bool LavaProfileActive(LavaProfile profile) {
    return profile.cls != LAVA_CLASS_NONE && profile.opticsBlend > 0.0f;
}

float LavaLuminance(float3 color) {
    return dot(max(color, 0.0.xxx), float3(0.2126f, 0.7152f, 0.0722f));
}

// Thermal chroma at a given heat. Mirrors LavaThermalChroma in the contract
// exactly, so a diagnostic CPU render and a GPU render share one ramp.
float3 LavaThermalChroma(float heat) {
    // Non-finite heat resolves to cold rock rather than leaking NaN into the
    // radiance buffer. Written explicitly: HLSL min/max with NaN is
    // implementation-defined, so saturate alone is not a guarantee.
    float h = (isfinite(heat) && heat > 0.0f) ? (heat > 1.0f ? 1.0f : heat) : 0.0f;
    if (h < LAVA_HEAT_ROCK) {
        float t = h / LAVA_HEAT_ROCK;
        return float3(0.055f + 0.115f * t, 0.020f + 0.035f * t, 0.012f + 0.014f * t);
    } else if (h < LAVA_HEAT_DULL_RED) {
        float t = (h - LAVA_HEAT_ROCK) / (LAVA_HEAT_DULL_RED - LAVA_HEAT_ROCK);
        return float3(0.170f + 0.520f * t, 0.055f + 0.055f * t, 0.026f + 0.010f * t);
    } else if (h < LAVA_HEAT_ORANGE) {
        float t = (h - LAVA_HEAT_DULL_RED) / (LAVA_HEAT_ORANGE - LAVA_HEAT_DULL_RED);
        return float3(0.690f + 0.310f * t, 0.110f + 0.430f * t, 0.036f + 0.030f * t);
    } else if (h < LAVA_HEAT_YELLOW) {
        float t = (h - LAVA_HEAT_ORANGE) / (LAVA_HEAT_YELLOW - LAVA_HEAT_ORANGE);
        return float3(1.0f, 0.540f + 0.410f * t, 0.066f + 0.180f * t);
    }
    float t = (h - LAVA_HEAT_YELLOW) / max(1.0f - LAVA_HEAT_YELLOW, 1.0e-4f);
    return float3(1.0f, 0.950f + 0.050f * t, 0.246f + 0.754f * t);
}

// Beer-Lambert transmittance over an optical path in metres.
float3 LavaAbsorbTransmittance(float distance, float scale) {
    float d = (distance > 0.0f && distance < 1.0e6f) ? distance : 0.0f;
    float s = saturate(scale * 0.25f) * 8.0f;
    return float3(exp(-LAVA_SIGMA_R * s * d),
                  exp(-LAVA_SIGMA_G * s * d),
                  exp(-LAVA_SIGMA_B * s * d));
}

// Deterministic value noise. Phase-continuous in the advection domain so the
// surface never pops between frames.
float LavaHash21(float2 p) {
    p = frac(p * float2(123.34f, 345.45f));
    p += dot(p, p + 34.345f);
    return frac(p.x * p.y);
}

float LavaValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = LavaHash21(i);
    float b = LavaHash21(i + float2(1.0f, 0.0f));
    float c = LavaHash21(i + float2(0.0f, 1.0f));
    float d = LavaHash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float LavaFbm(float2 p) {
    float sum = 0.0f;
    float amp = 0.5f;
    for (int octave = 0; octave < 4; ++octave) {
        sum += LavaValueNoise(p) * amp;
        p = p * 2.03f + 17.13f;
        amp *= 0.5f;
    }
    return sum;
}

// Two-phase viscous advection in the texture's own UV domain. A fast deep
// layer and a slow surface skin are blended by a noise field, so the pattern
// creeps like magma instead of scrolling like a river. `flowSpeed` is in
// cycles per second; `viscosity` slows the whole field and thickens the blend.
// The caller passes the game's own UV so the authored pattern is preserved.
float2 LavaAdvectUv(float2 uv, float time, float flowSpeed, float viscosity, uint cls) {
    // A fall runs down its own sheet; a pool drifts isotropically. Both stay
    // sub-texel per frame so the authored pattern never smears.
    const bool falling = cls == LAVA_CLASS_FALL;
    float2 dirDeep = falling ? float2(0.0f, 1.0f) : normalize(float2(0.78f, 0.62f));
    float2 dirSkin = falling ? float2(0.0f, 0.72f) : normalize(float2(-0.55f, 0.84f));
    float speed = flowSpeed * (1.0f - 0.55f * viscosity);
    float phase = time * speed;
    float2 deep = uv + dirDeep * phase;
    float2 skin = uv + dirSkin * phase * (0.35f * (1.0f - viscosity) + 0.08f);
    float blend = LavaFbm(uv * 3.1f + dirDeep * phase * 0.6f);
    // Viscosity pulls the blend toward the slow skin, so a thick melt moves
    // as one plate rather than as two independent sheets.
    blend = saturate(lerp(blend, 0.15f, viscosity * 0.6f));
    return lerp(skin, deep, blend);
}

// Cooled-crust mask over the advected UV. Plates are dark, non-emissive
// rock; the bright molten cracks between them carry the heat. Thresholded
// noise, not a hue pick.
float LavaCrustMask(float2 advectedUv, float crustAmount, uint cls) {
    if (crustAmount <= 0.0f) return 0.0f;
    if (cls == LAVA_CLASS_FALL) return saturate(crustAmount * 0.25f);
    float field = LavaFbm(advectedUv * 2.6f);
    float plates = smoothstep(0.34f, 0.52f, field);
    // A hot-rock surface is entirely plate; a pool keeps wide open melt.
    float coverage = saturate(crustAmount);
    return saturate(plates * coverage + coverage * coverage * 0.35f);
}

// Molten-crack edge: the gradient of the crust field, used to brighten the
// seams where plates pull apart and to perturb the normal at plate edges.
float LavaCrustEdge(float2 advectedUv, float crustAmount, uint cls) {
    if (crustAmount <= 0.0f || cls == LAVA_CLASS_FALL) return 0.0f;
    const float e = 0.012f;
    float c = LavaCrustMask(advectedUv, 1.0f, cls);
    float cx = LavaCrustMask(advectedUv + float2(e, 0.0f), 1.0f, cls);
    float cy = LavaCrustMask(advectedUv + float2(0.0f, e), 1.0f, cls);
    float gradient = length(float2(cx - c, cy - c)) / e;
    return saturate(gradient * 0.35f);
}

// Stable tangent frame around a shading normal.
void LavaTangentFrame(float3 n, out float3 t, out float3 b) {
    t = normalize(abs(n.y) < 0.9f ? cross(n, float3(0.0f, 1.0f, 0.0f))
                                   : cross(n, float3(1.0f, 0.0f, 0.0f)));
    b = cross(n, t);
}

// Viscous micro-relief: the art luminance gradient plus the crust-edge seams.
// Bounded so a high-contrast tile can never flip the shading normal. The art
// gradient is taken in the advected domain, so the perturbation follows the
// creep instead of floating over it.
float3 LavaShadeNormal(MaterialRecord material, float2 advectedUv, float2 uvStep, float mipLevel,
                       float3 geometricNormal, LavaProfile profile, float crust) {
    float3 n = geometricNormal;
    float3 t, b;
    LavaTangentFrame(n, t, b);

    float4 artCenter = SampleFast3DTexture(material, 0u, advectedUv, mipLevel);
    float4 artRight = SampleFast3DTexture(material, 0u, advectedUv + float2(uvStep.x, 0.0f), mipLevel);
    float4 artDown = SampleFast3DTexture(material, 0u, advectedUv + float2(0.0f, uvStep.y), mipLevel);
    float lumCenter = LavaLuminance(artCenter.rgb);
    float lumRight = LavaLuminance(artRight.rgb);
    float lumDown = LavaLuminance(artDown.rgb);
    if (!isfinite(lumCenter) || !isfinite(lumRight) || !isfinite(lumDown)) return n;

    // Central difference keeps the perturbation symmetric about the texel, so
    // a flat painted region stays flat instead of tilting one way.
    float2 artGradient = float2(lumRight - lumCenter, lumDown - lumCenter);
    float artLength = length(artGradient);
    if (artLength > 1.0e-4f) {
        // A painted gradient is not a height field. Bound the reconstructed
        // slope so a high-contrast art tile can never flip the shading normal.
        artGradient *= min(1.0f / artLength, 5.0f) * min(artLength, 0.40f);
    }

    // Crust seams dig in; the melt between plates bulges slightly.
    const float seamEps = 1.0e-3f;
    float2 seam = float2(
        LavaCrustEdge(advectedUv + float2(seamEps, 0.0f), profile.crustAmount, profile.cls) -
            LavaCrustEdge(advectedUv - float2(seamEps, 0.0f), profile.crustAmount, profile.cls),
        LavaCrustEdge(advectedUv + float2(0.0f, seamEps), profile.crustAmount, profile.cls) -
            LavaCrustEdge(advectedUv - float2(0.0f, seamEps), profile.crustAmount, profile.cls));
    float reliefScale = 0.55f * (1.0f - 0.5f * profile.viscosity);
    float2 slope = artGradient * 0.75f + seam * reliefScale;

    float3 perturbed = n + (t * slope.x + b * slope.y) * 0.9f;
    float lengthSq = dot(perturbed, perturbed);
    n = lengthSq > 1.0e-12f ? perturbed * rsqrt(lengthSq) : n;
    // Never let the shading normal cross the geometric horizon: a crossing
    // normal produces wrong specular and mirror-flip artefacts.
    if (dot(n, geometricNormal) < 0.10f) {
        n = normalize(geometricNormal + (n - geometricNormal) * 0.25f);
    }
    return normalize(n);
}

// Guard against a NaN/Inf escaping into the reflection buffer. Melt is
// bounded in [0, 1] before it ever leaves this module.
float3 LavaSanitize(float3 v) {
    float3 r;
    r.x = isfinite(v.x) ? clamp(v.x, 0.0f, 1.0f) : 0.0f;
    r.y = isfinite(v.y) ? clamp(v.y, 0.0f, 1.0f) : 0.0f;
    r.z = isfinite(v.z) ? clamp(v.z, 0.0f, 1.0f) : 0.0f;
    return r;
}

// The full lava surface evaluation. `artColor` is the authored Fast3D texel
// already converted to linear by the caller (same contract the water model
// uses). The output is the linear radiance the surface emits plus the modified
// albedo / roughness the primary and secondary shading paths should use.
struct LavaSurfaceResult {
    float3 albedo;        // linear albedo of the (possibly crusted) surface
    float roughness;      // microfacet roughness for reflection rays
    float3 emission;      // linear thermal radiance leaving the surface
    float coverage;       // geometric coverage, from the authored alpha
    float heat;           // 0-1 heat coordinate, for diagnostics
    float3 shadeNormal;   // viscous micro-relief normal (already world-ready)
};

LavaSurfaceResult LavaEvaluate(LavaProfile profile, MaterialRecord material, float3 artColor,
                               float artAlpha, float2 uv, float mipLevel, float time,
                               float3 geometricNormal) {
    LavaSurfaceResult result;
    result.albedo = 0.0.xxx;
    result.roughness = profile.roughness;
    result.emission = 0.0.xxx;
    result.coverage = saturate(artAlpha);
    result.heat = 0.0f;
    result.shadeNormal = geometricNormal;
    if (!LavaProfileActive(profile)) return result;

    uint textureWidth = 32u, textureHeight = 32u, mipCount = 1u;
    const bool hasArt = (material.flags & 1u) != 0u && material.textureIndex != 0u &&
                        material.textureIndex < gDiagnosticBounds.w;
    if (hasArt) {
        gMaterialTextures[NonUniformResourceIndex(material.textureIndex)]
            .GetDimensions(0u, textureWidth, textureHeight, mipCount);
    }
    const float2 texelCount = max(float2(textureWidth, textureHeight), 1.0.xx);
    const float2 uvStep = 1.0f / texelCount;

    // 1. Preserve the authored pattern; advect the field the crust and heat
    // are derived from so the plates creep while the painted silhouette stays.
    float2 advectedUv = LavaAdvectUv(uv, time, profile.flowSpeed, profile.viscosity, profile.cls);
    float3 art = max(artColor, 0.0.xxx);
    float artLum = saturate(LavaLuminance(art));

    // 2. Heat from the authored art plus advected flow noise. Never from hue.
    float flowNoise = LavaFbm(advectedUv * 4.3f + time * 0.15f);
    float heat = saturate(artLum * (0.72f + 0.45f * flowNoise) + 0.12f * flowNoise);
    // The crust cools the surface it covers; the seams between plates stay hot.
    float crust = LavaCrustMask(advectedUv, profile.crustAmount, profile.cls);
    float seam = LavaCrustEdge(advectedUv, profile.crustAmount, profile.cls);
    heat = saturate(lerp(heat, heat * 0.18f, crust * (1.0f - seam)));
    // A hot-rock surface is basalt lit by nearby melt: low, even heat.
    if (profile.cls == LAVA_CLASS_HOT_ROCK) heat = saturate(heat * 0.55f + 0.10f);
    result.heat = heat;

    // 3. Thermal chroma follows the shared anchors.
    float3 thermal = LavaThermalChroma(heat);
    // Blend toward the authored chroma so the game's own orange survives.
    float3 artMatched = art * (LavaLuminance(thermal) / max(LavaLuminance(art), 1.0e-4f));
    float3 albedo = lerp(artMatched, thermal, saturate(1.0f - profile.artWeight));
    // Crust plates darken the albedo to basalt; open melt keeps its glow.
    float3 crustAlbedo = albedo * 0.16f;
    albedo = lerp(albedo, crustAlbedo, crust * (1.0f - seam));
    result.albedo = LavaSanitize(albedo);

    // 4. Emission. Melt is a blackbody-ish emitter; crust emits almost nothing.
    float emitMask = saturate(heat * heat * (1.0f - crust * (1.0f - seam * 0.6f)));
    // The seams between plates are the hottest visible feature.
    emitMask = saturate(emitMask + seam * (1.0f - crust) * 0.55f);
    result.emission = LavaSanitize(thermal * emitMask * profile.emissionGain);

    // 5. Roughness: open melt is a glossy sludge, crust is rough basalt.
    result.roughness = saturate(lerp(profile.roughness, min(profile.roughness + 0.35f, 1.0f),
                                     crust * (1.0f - seam)));

    // 6. Coverage comes from the authored alpha; melt never fills its holes.
    result.coverage = saturate(artAlpha);

    // 7. Viscous micro-relief normal.
    result.shadeNormal = LavaShadeNormal(material, advectedUv, uvStep, mipLevel,
                                         geometricNormal, profile, crust);
    if (any(!isfinite(result.shadeNormal)) || dot(result.shadeNormal, result.shadeNormal) <= 1.0e-12f) {
        result.shadeNormal = geometricNormal;
    }
    return result;
}

#endif // COUCH_LAVA_MATERIAL_HLSLI
