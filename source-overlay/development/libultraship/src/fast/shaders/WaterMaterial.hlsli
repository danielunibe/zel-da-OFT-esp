// ============================================================
// OCZ-WATER-01 — Hyperrealistic Water Material (HLSL)
// Global water optical model for the Couch Edition DXR library.
//
// Contract:
//   * The original Fast3D water texture stays the artistic identity: it is
//     sampled with the captured UVs, wrapping, and the game's own UV scroll,
//     so the pattern and its animation are the ones Ocarina of Time ships.
//   * The optical layer (Fresnel, reflection, refraction, absorption,
//     micro-wave normals, native lights) is physically motivated and is added
//     on top of that identity, never instead of it.
//   * WaterClass selects motion and absorption only; the Fresnel/BRDF core is
//     shared so a lake, a river and a waterfall stay one coherent material.
//
// Keep the default tables in sync with WaterMaterialContract.h.
// ============================================================

#ifndef COUCH_WATER_MATERIAL_HLSLI
#define COUCH_WATER_MATERIAL_HLSLI

static const uint WATER_CLASS_NONE = 0u;
static const uint WATER_CLASS_SURFACE = 1u;
static const uint WATER_CLASS_FLOWING = 2u;
static const uint WATER_CLASS_FALLING = 3u;
static const uint WATER_CLASS_SHALLOW = 4u;
static const uint WATER_CLASS_SPECIAL = 5u;
static const uint WATER_CLASS_UNDERWATER = 6u;

// Air/water interface (IOR 1.333). F0 is a reflectance, not a roughness,
// alpha, or artistic mix weight.
static const float WATER_IOR = 1.333f;
static const float WATER_F0 = 0.0204f;
static const float WATER_SIGMA_R = 0.42f;
static const float WATER_SIGMA_G = 0.11f;
static const float WATER_SIGMA_B = 0.07f;

struct WaterProfile {
    uint cls;
    float waveScale;
    float flowSpeed;
    float absorption;
    float artWeight;
    float opticsBlend;
    float roughness;
};

WaterProfile WaterProfileFromRecord(float4 waterOptics, float recordSpecular, float recordRoughness) {
    WaterProfile profile;
    profile.cls = (uint)max(waterOptics.x, 0.0f);
    profile.waveScale = max(waterOptics.y, 0.0f);
    profile.flowSpeed = max(waterOptics.z, 0.0f);
    profile.absorption = max(waterOptics.w, 0.0f);
    // Defaults match WaterProfileForClass; a zero optics channel means the
    // record did not carry a water profile, so fall back to the surface
    // behaviour that shipped before OCZ-WATER-01. The record roughness is
    // the classified microfacet value (0.03-0.12); the specular slot holds
    // the 0.51 F0 scale and must never be mistaken for roughness.
    profile.artWeight = 0.55f;
    profile.opticsBlend = 1.0f;
    profile.roughness = clamp(recordRoughness, 0.02f, 0.35f);
    switch (profile.cls) {
        case WATER_CLASS_FLOWING:
            profile.artWeight = 0.55f; profile.opticsBlend = 1.00f; break;
        case WATER_CLASS_FALLING:
            profile.artWeight = 0.72f; profile.opticsBlend = 0.85f;
            profile.roughness = clamp(profile.roughness * 3.4f, 0.05f, 0.45f); break;
        case WATER_CLASS_SHALLOW:
            profile.artWeight = 0.50f; profile.opticsBlend = 0.95f; break;
        case WATER_CLASS_SPECIAL:
            profile.artWeight = 0.80f; profile.opticsBlend = 0.35f; break;
        case WATER_CLASS_UNDERWATER:
            profile.artWeight = 0.45f; profile.opticsBlend = 0.90f; break;
        case WATER_CLASS_SURFACE:
        default:
            profile.artWeight = 0.55f; profile.opticsBlend = 1.00f; break;
    }
    if (profile.cls == WATER_CLASS_NONE) profile.opticsBlend = 0.0f;
    return profile;
}

bool WaterProfileActive(WaterProfile profile) {
    return profile.cls != WATER_CLASS_NONE && profile.opticsBlend > 0.0f;
}

float WaterLuminance(float3 color) {
    return dot(max(color, 0.0.xxx), float3(0.2126f, 0.7152f, 0.0722f));
}

// Schlick Fresnel for the air/water interface. Non-finite cosTheta resolves
// to F0 (normal incidence) instead of leaking NaN, matching the C++ guard in
// WaterMaterialContract.h.
float WaterFresnelSchlick(float cosTheta, float f0) {
    float c = (cosTheta == cosTheta) ? saturate(cosTheta) : 1.0f;
    float m = 1.0f - c;
    float m2 = m * m;
    return f0 + (1.0f - f0) * (m2 * m2 * m);
}

// Stable tangent frame around a view-space shading normal.
void WaterTangentFrame(float3 n, out float3 t, out float3 b) {
    t = normalize(abs(n.y) < 0.9f ? cross(n, float3(0.0f, 1.0f, 0.0f))
                                   : cross(n, float3(1.0f, 0.0f, 0.0f)));
    b = cross(n, t);
}

// Micro-normal rebuilt from the original water texture. The gradient is taken
// in the texture's own UV domain, so the perturbation follows the game's UV
// scroll/rotation exactly and never drifts away from the painted pattern.
// `uvStep` is one texel in UV space, already wrapped by the caller.
float3 WaterDetailNormalFromArt(MaterialRecord material, float2 uv, float2 uvStep, float mipLevel,
                                float3 n, float3 t, float3 b, float strength, float anisotropy) {
    if (strength <= 0.0f) return n;
    float4 artCenter = SampleFast3DTexture(material, 0u, uv, mipLevel);
    float4 artRight = SampleFast3DTexture(material, 0u, uv + float2(uvStep.x, 0.0f), mipLevel);
    float4 artDown = SampleFast3DTexture(material, 0u, uv + float2(0.0f, uvStep.y), mipLevel);
    float lCenter = WaterLuminance(artCenter.rgb);
    float lRight = WaterLuminance(artRight.rgb);
    float lDown = WaterLuminance(artDown.rgb);
    if (!isfinite(lCenter) || !isfinite(lRight) || !isfinite(lDown)) return n;
    // Central difference keeps the perturbation symmetric about the texel, so
    // a flat painted region stays flat instead of tilting one way.
    float2 gradient = float2(lRight - lCenter, lDown - lCenter);
    float gradientLength = length(gradient);
    if (gradientLength > 1.0e-4f) {
        // A painted gradient is not a height field. Bound the reconstructed
        // slope so a high-contrast art tile can never flip the shading normal.
        gradient *= min(1.0f / gradientLength, 6.0f) * min(gradientLength, 0.35f);
    }
    float3 perturbed = n + (t * (-gradient.x * anisotropy) + b * (-gradient.y)) * strength;
    float lengthSq = dot(perturbed, perturbed);
    return lengthSq > 1.0e-12f ? perturbed * rsqrt(lengthSq) : n;
}

// Multi-scale analytic liquid waves (OCZ-WATER-02). Deterministic,
// phase-continuous in world space, and oriented by the water class: a lake
// swells broadly, a river runs along its flow, and a waterfall's sheet runs
// down its own plane. Four octaves with a physically falling energy
// spectrum:
//   A — large slow swells: broad optical undulation that tilts reflections
//   B — mid waves: natural reflection distortion
//   C — micro ripples: fine specular detail, distance-faded
//   D — sub-micro shimmer: grazing-angle sparkle, heavily distance-faded
// `plane` is the hit position projected on the geometric tangent plane
// (world units), so the pattern is continuous across draw calls and never
// repeats with the texture UVs. `distance` is the primary-ray hit distance
// used only for the LOD fade of the high octaves.
float3 WaterAnalyticWaveNormal(float2 plane, float time, uint cls, float scale,
                               float distance, float3 n, float3 t, float3 b) {
    if (scale <= 0.0f) return n;
    const bool falling = cls == WATER_CLASS_FALLING;
    const bool flowing = cls == WATER_CLASS_FLOWING;
    // Primary direction: the game's own scroll axis for flowing water, the
    // down-slope axis for a falling sheet, a neutral axis for a still lake.
    float2 dirA = falling ? float2(0.0f, 1.0f) : (flowing ? float2(1.0f, 0.0f) : float2(0.7071f, 0.7071f));
    float2 dirB = normalize(float2(0.83f, 0.56f));
    float2 dirC = normalize(float2(-0.41f, 0.91f));
    float2 dirD = normalize(float2(0.58f, -0.82f));
    // Spatial frequencies (cycles per world unit). Long wavelengths for A so
    // a distant lake still reads as a moving liquid; short for D so the
    // surface never looks like a plastic sheet up close.
    const float kWaveA = falling ? 0.55f : (flowing ? 0.10f : 0.065f);
    const float kWaveB = falling ? 1.30f : (flowing ? 0.32f : 0.22f);
    const float kWaveC = falling ? 3.10f : (flowing ? 0.95f : 0.75f);
    const float kWaveD = falling ? 6.50f : (flowing ? 2.40f : 2.10f);
    // Distance LOD: micro first, then sub-micro. The large octaves stay
    // visible at any range so the water never freezes into a plane.
    const float lodC = saturate(1.0f - distance * 0.016f);
    const float lodD = saturate(1.0f - distance * 0.042f);
    const float speedA = falling ? 2.60f : (flowing ? 1.50f : 0.55f);
    const float speedB = falling ? 2.10f : (flowing ? 1.90f : 1.15f);
    const float speedC = falling ? 3.00f : (flowing ? 2.60f : 2.05f);
    const float speedD = falling ? 3.60f : (flowing ? 3.10f : 2.80f);
    float phaseA = dot(plane, dirA) * 6.2831853f * kWaveA + time * speedA;
    float phaseB = dot(plane, dirB) * 6.2831853f * kWaveB - time * speedB;
    float phaseC = dot(plane, dirC) * 6.2831853f * kWaveC + time * speedC;
    float phaseD = dot(plane, dirD) * 6.2831853f * kWaveD - time * speedD;
    // Slope weights fall with frequency like a real ocean spectrum.
    float weightA = falling ? 1.00f : (flowing ? 0.85f : 0.70f);
    float weightB = falling ? 0.55f : (flowing ? 0.60f : 0.55f);
    float weightC = (falling ? 0.26f : 0.30f) * lodC;
    float weightD = (falling ? 0.12f : 0.15f) * lodD;
    float2 slope = dirA * (cos(phaseA) * weightA) +
                   dirB * (cos(phaseB) * weightB) +
                   dirC * (cos(phaseC) * weightC) +
                   dirD * (cos(phaseD) * weightD);
    // Peak tangent tilt. Larger than OCZ-WATER-01's 0.055 so the undulation
    // is visibly liquid, but well below the horizon clamp so Fresnel stays
    // continuous and reflections never invert.
    float3 perturbed = n + (t * slope.x + b * slope.y) * (0.14f * scale);
    float lengthSq = dot(perturbed, perturbed);
    return lengthSq > 1.0e-12f ? perturbed * rsqrt(lengthSq) : n;
}

// Full water micro-normal: geometric reference, art-driven detail, analytic
// multi-scale waves. The result is guaranteed to keep the original hemisphere.
// `worldPos`/`hitDistance` drive the world-space wave field and its LOD fade;
// the original UVs keep driving the art detail so the N64 pattern and its
// scroll animation stay intact.
float3 WaterShadeNormal(MaterialRecord material, float2 uv, float mipLevel,
                        float3 geometricNormal, WaterProfile profile, float time,
                        float3 worldPos, float hitDistance) {
    float3 n = geometricNormal;
    float3 t, b;
    WaterTangentFrame(n, t, b);
    // Project the world hit onto the geometric tangent plane so the analytic
    // waves are continuous across draw calls and never repeat with UVs. The
    // projection is computed from the geometric frame, before any shading
    // perturbation, so the phase is stable under normal animation.
    const bool worldFinite = all(isfinite(worldPos));
    float2 wavePlane = worldFinite
        ? float2(dot(worldPos, t), dot(worldPos, b))
        : uv;

    uint textureWidth = 32u, textureHeight = 32u, mipCount = 1u;
    const bool hasArt = (material.flags & 1u) != 0u && material.textureIndex != 0u &&
                        material.textureIndex < gDiagnosticBounds.w;
    if (hasArt) {
        gMaterialTextures[NonUniformResourceIndex(material.textureIndex)]
            .GetDimensions(0u, textureWidth, textureHeight, mipCount);
    }
    float2 texelCount = max(float2(textureWidth, textureHeight), 1.0.xx);
    float2 uvStep = 1.0f / texelCount;

    // Art-driven detail: fade with distance-mip so minified water keeps its
    // silhouette instead of boiling, and scale by the class amplitude.
    float lodFade = saturate(1.0f - mipLevel * 0.35f);
    // OCZ-WATER-02 shore attenuation: a geometric normal that is already far
    // from world-up sits on a steep bank or edge. Fade the wave amplitude
    // there so the surface meets the shore without a hard normal discontinuity.
    const float shoreUp = saturate(geometricNormal.y);
    const float shoreFade = lerp(0.35f, 1.0f, shoreUp * shoreUp);
    float artStrength = 0.85f * profile.waveScale * lodFade * shoreFade;
    float anisotropy = profile.cls == WATER_CLASS_FALLING ? 0.55f : 1.0f;
    n = WaterDetailNormalFromArt(material, uv, uvStep, mipLevel, n, t, b,
                                 artStrength, anisotropy);
    // Re-orthogonalize after the first perturbation so the analytic waves
    // still see a consistent tangent basis.
    WaterTangentFrame(normalize(n), t, b);

    float analyticStrength = profile.waveScale * lodFade * shoreFade *
        (profile.cls == WATER_CLASS_FALLING ? 1.25f : 1.0f);
    const float waveDistance = (isfinite(hitDistance) && hitDistance > 0.0f)
        ? hitDistance : 0.0f;
    n = WaterAnalyticWaveNormal(wavePlane, time, profile.cls, analyticStrength,
                                waveDistance, n, t, b);

    // Never let the shading normal cross the geometric horizon: a crossing
    // normal produces wrong Fresnel, inverted refraction, and mirror-flip
    // artefacts at grazing angles.
    if (dot(n, geometricNormal) < 0.08f) {
        n = normalize(geometricNormal + (n - geometricNormal) * 0.25f);
    }
    return normalize(n);
}

// Beer-Lambert transmittance over an optical path. OoT world units are
// roughly 10 per metre; the sigma values in the contract are per-metre, so
// the path is scaled here. Without this a 127-unit Lake Hylia depth becomes
// exp(-0.42*0.30*127)≈0 and the entire lake turns opaque black.
float3 WaterAbsorbTransmittance(float distance, float scale) {
    float d = (distance > 0.0f && distance < 1.0e6f) ? distance : 0.0f;
    float s = saturate(scale * 0.25f) * 4.0f;
    float metres = d * 0.1f;
    return float3(exp(-WATER_SIGMA_R * s * metres),
                  exp(-WATER_SIGMA_G * s * metres),
                  exp(-WATER_SIGMA_B * s * metres));
}

// Depth-aware body tint for water. Shallow water keeps the refracted scene
// bright and readable; deep water converges toward the class absorption
// colour without inventing a flat blue gradient. Used when the refracted ray
// misses or when the path is very long. Path is in OoT world units.
float3 WaterDepthTint(float pathDistance, float3 absorptionColor, float artWeight) {
    float depth = saturate(pathDistance * 0.004f);
    float3 body = max(absorptionColor, 0.0.xxx);
    float bodyLum = WaterLuminance(body);
    if (bodyLum > 1.0e-4f) body *= 0.85f / bodyLum;
    return lerp(body, body * 0.55f, depth * saturate(artWeight));
}

// Match the refracted scene to the original painted water hue without
// replacing its detail. The scene keeps its own luminance (so the real floor,
// shadows and caustic-less geometry stay readable) while the chroma converges
// to the art the game shipped. This is what keeps Lake Hylia looking like Lake
// Hylia instead of a uniform modern blue.
float3 WaterMatchArtChroma(float3 sceneColor, float3 artColor, float artWeight) {
    float w = saturate(artWeight);
    if (w <= 0.0f) return sceneColor;
    float3 art = max(SrgbToLinear(saturate(artColor)), 0.0.xxx);
    float sceneLum = WaterLuminance(sceneColor);
    float artLum = max(WaterLuminance(art), 1.0e-4f);
    // Scale the art so it carries the scene's brightness, then blend chroma.
    float3 artMatched = art * (sceneLum / artLum);
    return lerp(sceneColor, artMatched, w);
}

// Native point-light specular for the water surface. A light near the water
// contributes a Fresnel-weighted GGX glint instead of a flat luminous disc,
// and its contribution vanishes when the light is off or out of range.
float3 WaterPointLightSpecular(float3 normal, float3 viewDirection, float3 geometricNormal,
                               float3 hitPosition, float roughness, float3 f0,
                               float4 lightPosition, float4 lightOptics) {
    float3 toLight = lightPosition.xyz - hitPosition;
    float distanceToLight = length(toLight);
    float radius = lightPosition.w;
    if (!isfinite(distanceToLight) || distanceToLight <= 1.0e-4f || radius <= 0.0f) return 0.0.xxx;
    if (distanceToLight >= radius) return 0.0.xxx;
    float3 lightColor = max(lightOptics.yzw, 0.0.xxx);
    if (!any(lightColor > 0.0f)) return 0.0.xxx;
    float3 lightDirection = toLight / distanceToLight;
    float nDotL = dot(normal, lightDirection);
    if (nDotL <= 0.0f || dot(geometricNormal, lightDirection) <= 0.0f) return 0.0.xxx;
    float3 halfVector = normalize(lightDirection + viewDirection);
    float nDotH = max(dot(normal, halfVector), 1.0e-4f);
    float vDotH = max(dot(viewDirection, halfVector), 1.0e-4f);
    float alpha = max(roughness * roughness, 1.0e-4f);
    float alphaSq = alpha * alpha;
    float nDotHSq = nDotH * nDotH;
    float denominator = nDotHSq * (alphaSq - 1.0f) + 1.0f;
    float distribution = alphaSq / max(3.14159265f * denominator * denominator, 1.0e-7f);
    float3 fresnel = f0 + (1.0f - f0) * pow(1.0f - vDotH, 5.0f);
    float attenuation = saturate(1.0f - (distanceToLight / radius) * (distanceToLight / radius));
    return lightColor * (distribution * fresnel * nDotL * attenuation * 0.25f);
}

// Directional (sun/moon) GGX glint on the water surface.
float3 WaterDirectionalGlint(float3 normal, float3 viewDirection, float3 geometricNormal,
                             float3 lightDirection, float3 lightColor, float roughness, float3 f0) {
    if (!any(lightColor > 0.0f)) return 0.0.xxx;
    float nDotL = dot(normal, lightDirection);
    if (nDotL <= 0.0f || dot(geometricNormal, lightDirection) <= 0.0f) return 0.0.xxx;
    float3 halfVector = normalize(lightDirection + viewDirection);
    float nDotH = max(dot(normal, halfVector), 1.0e-4f);
    float vDotH = max(dot(viewDirection, halfVector), 1.0e-4f);
    float alpha = max(roughness * roughness, 1.0e-4f);
    float alphaSq = alpha * alpha;
    float nDotHSq = nDotH * nDotH;
    float denominator = nDotHSq * (alphaSq - 1.0f) + 1.0f;
    float distribution = alphaSq / max(3.14159265f * denominator * denominator, 1.0e-7f);
    float3 fresnel = f0 + (1.0f - f0) * pow(1.0f - vDotH, 5.0f);
    float nDotV = max(dot(normal, viewDirection), 1.0e-4f);
    float geometry = 0.5f / max(lerp(2.0f * nDotL * nDotV, nDotL + nDotV, alpha), 1.0e-4f);
    return lightColor * (distribution * geometry * fresnel * nDotL);
}

// Optical path inside the water for absorption. The refracted ray's hit
// distance is the real path; a miss uses the authored transmission range so
// a ray that escapes through a hole does not report zero thickness.
float WaterOpticalPath(float refractedHitDistance, bool refractedHitSomething, float fallbackRange) {
    if (refractedHitSomething && isfinite(refractedHitDistance) && refractedHitDistance > 0.0f)
        return min(refractedHitDistance, 400.0f);
    float fallback = isfinite(fallbackRange) ? max(fallbackRange, 0.0f) : 0.0f;
    // A miss through a thin sheet is shallower than a miss through deep water;
    // use a bounded fraction of the authored range so the fallback stays
    // conservative instead of inventing an ocean of absorption.
    return min(fallback * 0.35f, 40.0f);
}

#endif // COUCH_WATER_MATERIAL_HLSLI
