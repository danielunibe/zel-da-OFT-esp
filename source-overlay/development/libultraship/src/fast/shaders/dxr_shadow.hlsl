// ============================================================
// Ocarina of Time PC — Couch Edition
// RT3: Native DXR Ray-Traced Shadow Shader (HLSL SM 6.3)
// ============================================================

// The generator exports this marker into bytecode headers; CPU pipelines
// refuse a header from the older 104-byte payload/1408-byte constant layout.
#define COUCH_DXR_SHADER_ABI_VERSION 5
#define MATERIAL_FLAG_NAVI_POINT_PROXY (1u << 8u)
#define MATERIAL_FLAG_NATIVE_LIGHT_RECEIVER (1u << 10u)
#define MATERIAL_FLAG_NAVI_POINT_WORLD_RECEIVER MATERIAL_FLAG_NATIVE_LIGHT_RECEIVER
#define DXR_PAYLOAD_FLAG_ALBEDO_RECONSTRUCTED (1u << 9u)
#define MATERIAL_FLAG_DXR_WINDOW_PORTAL (1u << 5u)
#define MATERIAL_FLAG_DXR_PRIMARY_RELIGHT (1u << 6u)
#define MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID (1u << 7u)
#define HIT_VERTEX_FLAG_FAST3D_LIT_COLOR (1u << 2u)
#define DXR_PAYLOAD_FLAG_HYBRID_RELIGHT (1u << 8u)
#define MATERIAL_FLAG_DECORATIVE_BEAM (1u << 11u)

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gDepthTexture : register(t1);
RWTexture2D<float> gShadowMask : register(u0);
RWTexture2D<float4> gSurfaceMask : register(u1);
RWTexture2D<float> gEmissiveMask : register(u2);
RWTexture2D<float4> gIndirectMask : register(u3);
RWTexture2D<float4> gReflectionColor : register(u4);
RWStructuredBuffer<uint> gPrimaryGateCounters : register(u5);
RWTexture2D<float4> gNativePointDirect : register(u6);
ByteAddressBuffer gHitVertexData : register(t2);
ByteAddressBuffer gHitIndexData : register(t3);
ByteAddressBuffer gInstanceMaterialData : register(t4);
Texture2D<float4> gMaterialTextures[4096] : register(t5);
Texture2D<float4> gRasterEnvironment : register(t4101);
Texture2D<float> gRasterSoftShadow : register(t4102);
SamplerState gMaterialSampler : register(s0);
SamplerState gMaterialPointSampler : register(s1);

cbuffer FrameConstants : register(b0) {
    float4x4 gInvProjection;
    float4x4 gInvView;
    float4 gLightDir;
    float4 gScreenSize;
    float4 gParams; // pass-specific: bias, maxDist, clipXScale, shadow/composite control
    float4 gQuality; // indirect sample count, reflection enable, relief scale, point-light count
    float4 gRasterViewport; // normalized left, bottom, width, height (Fast3D bottom origin)
    float4 gSceneLightColor;
    float4 gAmbientLightColor;
    float4 gFrameInfo; // sample index, soft-shadow width/height, raster environment is linear
    float4 gDiagnosticInfo; // flags, material-record count, SRV count, view
    float4 gSecondaryLightDir;
    float4 gSecondaryLightColor;
    float4 gPointLightPosition[32]; // view-space xyz, w: authored influence radius
    float4 gPointLightOptics[32];   // x: reserved, yzw: linear RGB color
    float4x4 gProjection;
    uint4 gDiagnosticBounds; // logical vertex, index, material-record and texture counts
    float4 gTraceSettings; // reflection/transmission/GI ranges; w: authored reflection strength
    uint4 gSourceControls; // selected owner, isolated harness, emitter samples, reserved
    float4 gEmitterSamplePosition[8];
    float4 gEmitterSampleNormal[8];
    uint4 gEmitterSampleIdentity[2];
};

float2 RasterToViewportUV(float2 rasterUv) {
    float2 topLeft = float2(gRasterViewport.x, 1.0f - gRasterViewport.y - gRasterViewport.w);
    return (rasterUv - topLeft) / max(gRasterViewport.zw, 1.0e-6f.xx);
}

float2 ViewportToRasterUV(float2 viewportUv) {
    float2 topLeft = float2(gRasterViewport.x, 1.0f - gRasterViewport.y - gRasterViewport.w);
    return topLeft + viewportUv * gRasterViewport.zw;
}

struct ShadowPayload {
    float visibility;
    float3 normal;
    float hitDistance;
    uint probeMode;
};

[shader("raygeneration")]
void RayGenShadow() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    uint2 depthPixel = min(uint2((float2(launchIndex) + 0.5f) / float2(launchDim) * gScreenSize.xy),
                           uint2(gScreenSize.xy) - 1u);
    float depth = gDepthTexture.Load(int3(depthPixel, 0));

    // Si la profundidad es 1.0f (plano lejano / fondo / cielo), no hay sombra
    if (!isfinite(depth) || depth < 0.0f || depth >= 1.0f) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }

    // Reconstruct the CENTER of the exact pixel used for the depth load,
    // in the selected raster viewport rather than the entire framebuffer.
    float2 uv = RasterToViewportUV((float2(depthPixel) + 0.5f) / gScreenSize.xy);
    if (any(uv < 0.0f) || any(uv > 1.0f)) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }
    float clipXScale = gParams.z > 1e-4f ? gParams.z : 1.0f;
    // Fast3D emits clip-space Z in [-w, w]; D3D12 maps it to [0, 1] for
    // the depth buffer. Restore the original NDC range before inverting P.
    float4 clipPos = float4((uv.x * 2.0f - 1.0f) / clipXScale,
                            1.0f - uv.y * 2.0f, depth * 2.0f - 1.0f, 1.0f);
    float4 worldPos = mul(gInvProjection, clipPos);

    if (any(!isfinite(worldPos)) || abs(worldPos.w) <= 1e-6f) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }
    worldPos.xyz /= worldPos.w;

    float bias = gParams.x > 0.0f ? gParams.x : 0.15f;
    float maxDist = gParams.y > 0.0f ? gParams.y : 10000.0f;

    float3 lightDir = normalize(gLightDir.xyz);
    // Anchor the shadow at the triangle that produced raster depth. Offsets
    // along the outgoing light alone can leave grazing rays inside the surface.
    RayDesc primaryRay;
    float4 nearH = mul(gInvProjection, float4(clipPos.xy, -1.0f, 1.0f));
    float4 farH = mul(gInvProjection, float4(clipPos.xy, 1.0f, 1.0f));
    if (any(!isfinite(nearH)) || any(!isfinite(farH)) ||
        abs(nearH.w) <= 1.0e-6f || abs(farH.w) <= 1.0e-6f) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }
    primaryRay.Origin = nearH.xyz / nearH.w;
    primaryRay.Direction = normalize(farH.xyz / farH.w - primaryRay.Origin);
    float reconstructedDistance = dot(worldPos.xyz - primaryRay.Origin, primaryRay.Direction);
    if (!isfinite(reconstructedDistance) || reconstructedDistance < 0.0f) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }
    primaryRay.TMin = 0.0f;
    primaryRay.TMax = reconstructedDistance + max(0.01f, reconstructedDistance * 0.001f);
    ShadowPayload primary;
    primary.visibility = 0.0f;
    primary.normal = 0.0f.xxx;
    primary.hitDistance = -1.0f;
    primary.probeMode = 1u;
    TraceRay(gScene, RAY_FLAG_NONE, 0x03, 0, 1, 0, primaryRay, primary);
    if (primary.hitDistance <= 0.0f || any(!isfinite(primary.normal)) ||
        abs(primary.hitDistance - reconstructedDistance) > max(0.25f, reconstructedDistance * 0.003f) ||
        dot(primary.normal, lightDir) <= 0.0f) {
        // Blended surfaces and surfaces facing away from the key light retain
        // their raster lighting; there is no direct-light term to shadow here.
        gShadowMask[launchIndex] = 1.0f;
        return;
    }
    float3 hitPosition = primaryRay.Origin + primaryRay.Direction * primary.hitDistance;
    float3 shadowNormal = normalize(primary.normal);
    float3 lTangent = normalize(abs(lightDir.y) < 0.95f ? cross(lightDir, float3(0.0f, 1.0f, 0.0f))
                                                        : cross(lightDir, float3(1.0f, 0.0f, 0.0f)));
    float3 lBitangent = cross(lightDir, lTangent);

    // Slope-scaled contact bias: preserve crisp contact shadows on ladder rungs
    // and actor feet near orthogonal incidence, while scaling out at grazing angles.
    float cosTheta = saturate(dot(shadowNormal, lightDir));
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    float contactScale = 0.25f + 0.75f * sinTheta;
    float effectiveBias = bias * contactScale;

    // Low-discrepancy disk samples reduce penumbra noise as the RTX quality
    // tier raises the per-pixel shadow-ray budget.
    float2 seed = float2(launchIndex);
    float jitter0 = frac(sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453f);
    float jitter1 = frac(sin(dot(seed + float2(1.7f, 3.1f), float2(39.346f, 11.135f))) * 23421.631f);
    float lightRadius = clamp(gQuality.y, 0.0f, 0.12f);
    float totalVisibility = 0.0f;

    uint shadowSamples = min(16u, max(1u, (uint)max(gQuality.x, 1.0f)));
    [loop] for (uint s = 0u; s < shadowSamples; ++s) {
        float sampleIndex = float(s);
        float angle = frac(jitter0 + sampleIndex * 0.61803398875f) * 6.2831853f;
        float radial = sqrt((sampleIndex + jitter1) / float(shadowSamples));
        float r = radial * lightRadius;
        float3 rayDir = normalize(lightDir + (lTangent * cos(angle) + lBitangent * sin(angle)) * r);

        RayDesc ray;
        ray.Origin = hitPosition + shadowNormal * effectiveBias;
        ray.Direction = rayDir;
        ray.TMin = max(effectiveBias * 0.05f, 0.0005f);
        ray.TMax = maxDist;

        ShadowPayload payload;
        payload.visibility = 0.0f;
        payload.normal = 0.0f.xxx;
        payload.hitDistance = -1.0f;
        payload.probeMode = 0u;

        // Mask 0x03 cubre STATIC (0x01) y DYNAMIC (0x02), excluyendo UI (0x00)
        TraceRay(
            gScene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0x03,
            0,
            1,
            0,
            ray,
            payload
        );
        totalVisibility += payload.visibility;
    }

    gShadowMask[launchIndex] = totalVisibility / float(shadowSamples);
}

[shader("miss")]
void MissShadow(inout ShadowPayload payload) {
    payload.visibility = 1.0f;
}

// ============================================================
// RTX SURFACE REFLECTION PROBE
// ============================================================
// The reflection pass deliberately shares this DXIL library and the same
// TLAS/depth root layout as shadows. It writes a scalar confidence mask and a
// separate DXR radiance texture; the raster composite applies those signals
// over the original color buffer, so the original textures and Fast3D draw
// payload remain untouched.

struct ReflectionPayload {
    float4 radiance;
    float4 normalRoughness;
    float4 baseColorMetallic;
    uint materialIndex;
    uint probeMode;
    uint instanceIndex;
    uint textureIndex;
    float4 surfaceOptics; // reflectivity, emission, hit distance, valid hit
    float4 detail;        // UV, specular, sampled material coverage
    uint diagnosticFlags;
    uint diagnosticError;
    float4 geometricNormal; // TLAS-space triangle normal; ray origins never use relief/ripple normals
    float4 naviPointAlbedo; // source-material albedo without prelit Fast3D Shade or fog
};

struct MaterialRecord {
    uint vertexOffset;
    uint indexOffset;
    uint materialType;
    uint textureIndex;
    uint vertexCount;
    uint indexCount;
    uint alphaMode;
    uint flags;
    float roughness;
    float metallic;
    float specular;
    float reflectivity;
    float emissiveStrength;
    float normalStrength;
    float wetness;
    float subsurfaceHint;
    float4 baseColor;
    uint textureIndex1;
    uint samplerFlags;
    uint combineModeLow;
    uint combineModeHigh;
    uint primitiveColor;
    uint environmentColor;
    uint reserved0;
    uint otherModeH;
    // ABI v5: per-instance aquatic optics. Meaningful only for WATER.
    // x: WaterClass ordinal, y: wave scale, z: flow speed, w: absorption.
    float4 waterOptics;
};

// Keep the enum value synchronized with MaterialType::GENERIC_MATTE. This
// fallback has no reliable surface classification and must not replace the
// original Fast3D pixel in the primary path-traced composite.
static const uint MATERIAL_TYPE_GENERIC_MATTE = 18u;
static const uint MATERIAL_TYPE_WATER = 4u;
static const uint MATERIAL_TYPE_GLASS = 5u;
static const uint MATERIAL_TYPE_EMISSIVE = 14u;
// MaterialType::LAVA. Molten rock is opaque and emissive, never a dielectric.
static const uint MATERIAL_TYPE_LAVA = 11u;
static const uint PRIMARY_GATE_COUNTER_SHARDS = 64u;
// GPU gate counter layout: base counters 0..18, reason counters 19..25,
// then compact histograms. Keep in sync with DxrSurfaceRenderer.cpp.
static const uint PRIMARY_GATE_COUNTER_STRIDE = 2112u;
static const uint PRIMARY_ALPHA_SAMPLE_BASE = PRIMARY_GATE_COUNTER_SHARDS * PRIMARY_GATE_COUNTER_STRIDE;
static const uint PRIMARY_ALPHA_SAMPLE_STRIDE = 32u;
static const uint PG_ELIGIBILITY_INPUT_VALID = 19u;
static const uint PG_ALPHA_BLEND_CANDIDATE = 20u;
static const uint PG_TRANSMISSIVE_EXCEPTION = 21u;
static const uint PG_RAW_PRIMARY_RASTER_BLEND = 22u;
static const uint PG_REJECT_BLEND = 23u;
static const uint PG_REJECT_COVERAGE = 24u;
static const uint PG_RASTER_GEOMETRY = 25u;
static const uint PG_RASTER_BACKGROUND = 26u;
static const uint PG_GEOMETRY_PRIMARY_HIT = 27u;
static const uint PG_GEOMETRY_PRIMARY_MISS = 28u;
static const uint PG_RECORD_HIST_BASE = 29u;
static const uint PG_RECORD_HIST_COUNT = 257u;
static const uint PG_ALPHA_MODE_HIST_BASE = 286u;
static const uint PG_TYPE_HIST_BASE = 291u;
static const uint PG_FLAGS_HIST_BASE = 313u;
static const uint PG_TEXTURE_HIST_BASE = 330u;
static const uint PG_OPTICS_HIST_BASE = 334u;
static const uint PG_COVERAGE_HIST_BASE = 339u;
static const uint PG_ROUGHNESS_HIST_BASE = 355u;
static const uint PG_METALLIC_HIST_BASE = 371u;
static const uint PG_REFLECTIVITY_HIST_BASE = 387u;
static const uint PG_SPECULAR_HIST_BASE = 403u;
static const uint PG_RECORD_ALPHA_HIST_BASE = 419u;
static const uint PG_VERTEX_ALPHA_HIST_BASE = 435u;
static const uint PG_SAMPLED_ALPHA_HIST_BASE = 451u;
static const uint PG_VERTEX_ALPHA_ZERO = 467u;
static const uint PG_TEXTURE_ALPHA_ZERO = 468u;
static const uint PG_RECORD_ALPHA_ZERO = 469u;
static const uint PG_COMBINED_ALPHA_ZERO = 470u;
static const uint PG_RASTER_DEPTH_MISMATCH = 488u; // reserved slot before per-record alpha counters
static const uint PG_COVERAGE_NONZERO = 471u;
static const uint PG_VERTEX_ALPHA_4BIN_BASE = 472u;
static const uint PG_TEXTURE_ALPHA_4BIN_BASE = 476u;
static const uint PG_RECORD_ALPHA_4BIN_BASE = 480u;
static const uint PG_COMBINED_ALPHA_4BIN_BASE = 484u;
static const uint PG_PER_RECORD_ALPHA_HIST_BASE = 512u;

uint PrimaryFloatBin(float value) {
    return min((uint)(saturate(value) * 16.0f), 15u);
}

uint PrimaryAlpha4Bin(float value) {
    value = saturate(value);
    return value <= 0.0f ? 0u : value < (1.0f / 16.0f) ? 1u :
           value < (15.0f / 16.0f) ? 2u : 3u;
}

void IncrementPrimaryGateCounter(uint counterIndex, uint2 launchIndex) {
    // Spread hot atomics across cache lines while keeping each shard integral
    // to the one primary ray represented by launchIndex.
    uint shard = launchIndex.x & (PRIMARY_GATE_COUNTER_SHARDS - 1u);
    uint ignored;
    InterlockedAdd(gPrimaryGateCounters[shard * PRIMARY_GATE_COUNTER_STRIDE + counterIndex], 1u, ignored);
}

struct HitVertex {
    float3 position;
    float3 normal;
    float2 uv;
    float2 uv1;
    float4 color;
    uint normalState;
    uint sourceAlpha;
    uint recordLoadedAlpha;
    uint capturedAlpha;
    uint fogMode;
    uint localAlpha;
    float3 sourceTint;
};

void InitializeReflectionPayload(out ReflectionPayload payload, uint mode) {
    payload.radiance = 0.0f;
    payload.normalRoughness = float4(0.0f, 1.0f, 0.0f, 1.0f);
    payload.baseColorMetallic = float4(1.0f, 1.0f, 1.0f, 0.0f);
    payload.materialIndex = 0u;
    payload.probeMode = mode;
    payload.instanceIndex = 0u;
    payload.textureIndex = 0xFFFFFFFFu;
    payload.surfaceOptics = 0.0f;
    payload.detail = 0.0f;
    payload.diagnosticFlags = (mode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) ? 1u : 0u;
    payload.diagnosticError = 0xFFFFFFFFu;
    payload.geometricNormal = 0.0f;
    payload.naviPointAlbedo = 1.0f.xxxx;
}

MaterialRecord LoadMaterialRecord(uint instanceIndex) {
    // ABI v5: 128-byte material record. Water optics occupy the final 16 bytes.
    uint address = instanceIndex * 128u;
    uint4 ids = gInstanceMaterialData.Load4(address);
    uint4 counts = gInstanceMaterialData.Load4(address + 16u);
    float4 optics = asfloat(gInstanceMaterialData.Load4(address + 32u));
    float4 secondary = asfloat(gInstanceMaterialData.Load4(address + 48u));
    float4 baseColor = asfloat(gInstanceMaterialData.Load4(address + 64u));
    uint4 combiner = gInstanceMaterialData.Load4(address + 80u);
    uint4 inputs = gInstanceMaterialData.Load4(address + 96u);
    float4 waterOptics = asfloat(gInstanceMaterialData.Load4(address + 112u));
    MaterialRecord result;
    result.vertexOffset = ids.x;
    result.indexOffset = ids.y;
    result.materialType = ids.z;
    result.textureIndex = ids.w;
    result.vertexCount = counts.x;
    result.indexCount = counts.y;
    result.alphaMode = counts.z;
    result.flags = counts.w;
    result.roughness = optics.x;
    result.metallic = optics.y;
    result.specular = optics.z;
    result.reflectivity = optics.w;
    result.emissiveStrength = secondary.x;
    result.normalStrength = secondary.y;
    result.wetness = secondary.z;
    result.subsurfaceHint = secondary.w;
    result.baseColor = baseColor;
    result.textureIndex1 = combiner.x;
    result.samplerFlags = combiner.y;
    result.combineModeLow = combiner.z;
    result.combineModeHigh = combiner.w;
    result.primitiveColor = inputs.x;
    result.environmentColor = inputs.y;
    result.reserved0 = inputs.z;
    result.otherModeH = inputs.w;
    result.waterOptics = waterOptics;
    return result;
}

float3 SrgbToLinear(float3 encodedColor) {
    float3 color = saturate(encodedColor);
    float3 lower = color / 12.92f;
    float3 higher = pow((color + 0.055f) / 1.055f, 2.4f);
    return lerp(higher, lower, 1.0f - step(0.04045f, color));
}

float SrgbLuminance(float3 encodedColor) {
    return dot(SrgbToLinear(encodedColor), float3(0.2126f, 0.7152f, 0.0722f));
}

HitVertex LoadHitVertex(uint vertexIndex) {
    uint address = vertexIndex * 56u;
    uint4 packed = gHitVertexData.Load4(address + 40u);
    HitVertex result;
    result.position = asfloat(gHitVertexData.Load3(address));
    result.normal = asfloat(gHitVertexData.Load3(address + 12u));
    result.uv = asfloat(gHitVertexData.Load2(address + 24u));
    result.uv1 = asfloat(gHitVertexData.Load2(address + 32u));
    result.color = float4(packed.x & 255u, (packed.x >> 8u) & 255u,
                          (packed.x >> 16u) & 255u, (packed.x >> 24u) & 255u) / 255.0f;
    result.normalState = packed.y;
    uint forensic = packed.z;
    result.sourceAlpha = forensic & 0xFFu;
    result.recordLoadedAlpha = (forensic >> 8u) & 0xFFu;
    result.capturedAlpha = (forensic >> 16u) & 0xFFu;
    result.fogMode = (forensic >> 24u) & 0xFFu;
    result.localAlpha = packed.w & 0xFFu;
    result.sourceTint = float3((packed.w >> 8u) & 0xFFu,
                               (packed.w >> 16u) & 0xFFu,
                               (packed.w >> 24u) & 0xFFu) / 255.0f;
    return result;
}

// Validate every productive hit before dereferencing material, index or
// vertex data. Diagnostics report the same guards, without clamping IDs.
uint ValidatePrimaryHit(uint instanceIndex, uint geometryIndex, uint primitiveIndex,
                        out uint validFlags) {
    validFlags = 0u;
    MaterialRecord material;
    if ((uint)gDiagnosticInfo.y != gDiagnosticBounds.z) return 10u; // CPU metadata disagreement
    if (instanceIndex >= gDiagnosticBounds.z) return 2u; // beyond logical material-record count
    // These are root SRVs: allocation sizes are checked on CPU in
    // DxrSurfaceFrameBoundsValid; no GetDimensions on root buffer descriptors.
    if (geometryIndex != 0u) return 3u; // current BLAS bridge has one geometry
    material = LoadMaterialRecord(instanceIndex);
    if (material.materialType > 20u || material.alphaMode > 3u ||
        material.vertexCount == 0u || material.indexCount < 3u) return 5u;
    if (primitiveIndex >= material.indexCount / 3u) return 4u;
    if (material.indexOffset > gDiagnosticBounds.y ||
        material.indexCount > gDiagnosticBounds.y - material.indexOffset) return 6u;
    if (material.vertexOffset > gDiagnosticBounds.x ||
        material.vertexCount > gDiagnosticBounds.x - material.vertexOffset) return 7u;
    uint triangleBaseIndex = material.indexOffset + primitiveIndex * 3u;
    uint i0 = gHitIndexData.Load(triangleBaseIndex * 4u);
    uint i1 = gHitIndexData.Load((triangleBaseIndex + 1u) * 4u);
    uint i2 = gHitIndexData.Load((triangleBaseIndex + 2u) * 4u);
    if (i0 >= material.vertexCount || i1 >= material.vertexCount || i2 >= material.vertexCount) return 7u;
    HitVertex v0 = LoadHitVertex(material.vertexOffset + i0);
    HitVertex v1 = LoadHitVertex(material.vertexOffset + i1);
    HitVertex v2 = LoadHitVertex(material.vertexOffset + i2);
    if (any(!isfinite(v0.position)) || any(!isfinite(v1.position)) || any(!isfinite(v2.position)) ||
        any(!isfinite(v0.uv)) || any(!isfinite(v1.uv)) || any(!isfinite(v2.uv)) ||
        any(!isfinite(v0.uv1)) || any(!isfinite(v1.uv1)) || any(!isfinite(v2.uv1)) ||
        any(!isfinite(v0.normal)) || any(!isfinite(v1.normal)) || any(!isfinite(v2.normal)) ||
        any(!isfinite(v0.color)) || any(!isfinite(v1.color)) || any(!isfinite(v2.color)) ||
        !isfinite(material.roughness) || !isfinite(material.metallic) ||
        !isfinite(material.specular) || !isfinite(material.reflectivity) ||
        !isfinite(material.emissiveStrength) || !isfinite(material.normalStrength) ||
        !isfinite(material.wetness) || !isfinite(material.subsurfaceHint) ||
        !isfinite(material.baseColor.r) || !isfinite(material.baseColor.g) ||
        !isfinite(material.baseColor.b) || !isfinite(material.baseColor.a)) return 9u;
    validFlags |= 4u;
    validFlags |= 8u;
    const bool forcedGateMode = (((uint)gDiagnosticInfo.x & 2u) != 0u);
    if (!forcedGateMode && (material.flags & 1u) != 0u &&
        (material.textureIndex == 0u || material.textureIndex >= (uint)gDiagnosticInfo.z ||
         material.textureIndex >= gDiagnosticBounds.w || material.textureIndex >= 4096u)) return 8u;
    if (!forcedGateMode && (material.flags & MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID) != 0u &&
        (material.textureIndex1 == 0u || material.textureIndex1 >= (uint)gDiagnosticInfo.z ||
         material.textureIndex1 >= gDiagnosticBounds.w || material.textureIndex1 >= 4096u)) return 8u;
    return 0u;
}

uint4 UnpackMaterialColor(uint packedColor) {
    return uint4(packedColor & 255u, (packedColor >> 8u) & 255u,
                 (packedColor >> 16u) & 255u, (packedColor >> 24u) & 255u);
}

float4 DecodePackedMaterialColor(uint packedColor) {
    return float4(UnpackMaterialColor(packedColor)) / 255.0f;
}

float AddressFast3DCoordinate(float value, bool clampValue, bool mirrorValue, float halfTexel) {
    if (clampValue) return clamp(value, halfTexel, 1.0f - halfTexel);
    if (mirrorValue) return 1.0f - abs(frac(value * 0.5f) * 2.0f - 1.0f);
    return frac(value);
}

float4 SampleFast3DTexture(MaterialRecord material, uint tile, float2 uv, float mipLevel) {
    const bool validTexture = tile == 0u
        ? ((material.flags & 1u) != 0u && material.textureIndex != 0u)
        : ((material.flags & MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID) != 0u && material.textureIndex1 != 0u);
    const uint textureIndex = tile == 0u ? material.textureIndex : material.textureIndex1;
    if (!validTexture || textureIndex >= gDiagnosticBounds.w || textureIndex >= 4096u) return 1.0f;

    const uint tileFlags = (material.samplerFlags >> (tile * 5u)) & 31u;
    Texture2D<float4> albedoTexture = gMaterialTextures[NonUniformResourceIndex(textureIndex)];
    uint textureWidth, textureHeight, mipCount;
    albedoTexture.GetDimensions(0u, textureWidth, textureHeight, mipCount);
    const float2 halfTexel = 0.5f / max(float2(textureWidth, textureHeight), 1.0f.xx);
    uv.x = AddressFast3DCoordinate(uv.x, (tileFlags & 1u) != 0u, (tileFlags & 2u) != 0u, halfTexel.x);
    uv.y = AddressFast3DCoordinate(uv.y, (tileFlags & 4u) != 0u, (tileFlags & 8u) != 0u, halfTexel.y);
    const bool linearFilter = (tileFlags & 16u) != 0u;
    float4 sampleColor = linearFilter
        ? albedoTexture.SampleLevel(gMaterialSampler, uv, mipLevel)
        : albedoTexture.SampleLevel(gMaterialPointSampler, uv, mipLevel);
    if (material.alphaMode != 0u) {
        sampleColor.a = linearFilter
            ? albedoTexture.SampleLevel(gMaterialSampler, uv, 0.0f).a
            : albedoTexture.SampleLevel(gMaterialPointSampler, uv, 0.0f).a;
    }
    return sampleColor;
}

uint ReadCombinerBits(MaterialRecord material, uint bitOffset, uint bitCount) {
    const uint mask = (1u << bitCount) - 1u;
    if (bitOffset >= 32u) return (material.combineModeHigh >> (bitOffset - 32u)) & mask;
    if (bitOffset + bitCount <= 32u) return (material.combineModeLow >> bitOffset) & mask;
    const uint lowPart = material.combineModeLow >> bitOffset;
    const uint highPart = material.combineModeHigh << (32u - bitOffset);
    return (lowPart | highPart) & mask;
}

float4 Fast3DColorMux(uint mux, uint cycle, uint muxKind, float4 combined, float4 texel0, float4 texel1,
                      float4 primitive, float4 shade, float4 environment, out bool supported) {
    supported = true;
    const float4 cycleTexel0 = cycle == 0u ? texel0 : texel1;
    const float4 cycleTexel1 = cycle == 0u ? texel1 : texel0;
    if (mux == 31u) return 0.0f;
    if (mux == 0u) {
        if (cycle == 0u) supported = false; // cycle zero has no captured prior texel.
        return combined;
    }
    if (mux == 1u) return cycleTexel0;
    if (mux == 2u) return cycleTexel1;
    if (mux == 3u) return primitive;
    if (mux == 4u) return shade;
    if (mux == 5u) return environment;
    if (mux == 6u) return 1.0f.xxxx;
    if (muxKind == 1u && mux == 7u) return combined.a.xxxx;
    if (muxKind == 1u && mux == 8u) return cycleTexel0.a.xxxx;
    if (muxKind == 1u && mux == 9u) return cycleTexel1.a.xxxx;
    if (muxKind == 1u && mux == 10u) return primitive.a.xxxx;
    if (muxKind == 1u && mux == 11u) return shade.a.xxxx;
    if (muxKind == 1u && mux == 12u) return environment.a.xxxx;
    supported = false; // LOD, primitive LOD, K5 and noise need extra RDP state.
    return 0.0f;
}

float Fast3DAlphaMux(uint mux, uint cycle, float combined, float4 texel0, float4 texel1,
                     float4 primitive, float4 shade, float4 environment, out bool supported) {
    supported = true;
    if (mux == 0u) {
        if (cycle == 0u) supported = false;
        return combined;
    }
    if (mux == 1u) return cycle == 0u ? texel0.a : texel1.a;
    if (mux == 2u) return cycle == 0u ? texel1.a : texel0.a;
    if (mux == 3u) return primitive.a;
    if (mux == 4u) return shade.a;
    if (mux == 5u) return environment.a;
    if (mux == 6u) return 1.0f;
    return 0.0f;
}

bool EvaluateFast3DCombiner(MaterialRecord material, float4 texel0, float4 texel1,
                            float4 primitive, float4 shade, float4 environment,
                            out float4 combinedResult) {
    combinedResult = 1.0f;
    const uint cycleType = (material.otherModeH >> 20u) & 3u;
    const uint cycleCount = cycleType == 1u ? 2u : 1u;
    float4 combined = 1.0f;
    [unroll] for (uint cycle = 0u; cycle < 2u; ++cycle) {
        if (cycle >= cycleCount) break;
        const uint baseBit = cycle * 28u;
        uint rgbA = ReadCombinerBits(material, baseBit, 4u);
        uint rgbB = ReadCombinerBits(material, baseBit + 4u, 4u);
        uint rgbC = ReadCombinerBits(material, baseBit + 8u, 5u);
        uint rgbD = ReadCombinerBits(material, baseBit + 13u, 3u);
        uint alphaA = ReadCombinerBits(material, baseBit + 16u, 3u);
        uint alphaB = ReadCombinerBits(material, baseBit + 19u, 3u);
        uint alphaC = ReadCombinerBits(material, baseBit + 22u, 3u);
        uint alphaD = ReadCombinerBits(material, baseBit + 25u, 3u);
        if (rgbA >= 8u) rgbA = 31u;
        if (rgbB >= 8u) rgbB = 31u;
        if (rgbC >= 16u) rgbC = 31u;
        if (rgbD == 7u) rgbD = 31u;

        float3 rgbValue;
        if (rgbA == rgbB || rgbC == 31u) {
            bool supportedD;
            rgbValue = Fast3DColorMux(rgbD, cycle, 2u, combined, texel0, texel1, primitive, shade, environment,
                                      supportedD).rgb;
            if (!supportedD) return false;
        } else {
            bool supportedA, supportedB, supportedC, supportedD;
            const float3 a = Fast3DColorMux(rgbA, cycle, 0u, combined, texel0, texel1, primitive, shade, environment,
                                            supportedA).rgb;
            const float3 b = Fast3DColorMux(rgbB, cycle, 0u, combined, texel0, texel1, primitive, shade, environment,
                                            supportedB).rgb;
            const float3 c = Fast3DColorMux(rgbC, cycle, 1u, combined, texel0, texel1, primitive, shade, environment,
                                            supportedC).rgb;
            const float3 d = Fast3DColorMux(rgbD, cycle, 2u, combined, texel0, texel1, primitive, shade, environment,
                                            supportedD).rgb;
            if (!supportedA || !supportedB || !supportedC || !supportedD) return false;
            rgbValue = saturate((a - b) * c + d);
        }

        float alphaValue;
        if (alphaA == alphaB || alphaC == 7u) {
            bool supportedD;
            alphaValue = Fast3DAlphaMux(alphaD, cycle, combined.a, texel0, texel1, primitive, shade,
                                        environment, supportedD);
            if (!supportedD) return false;
        } else {
            // The RDP C selector aliases LOD and primitive-LOD fractions.
            // Until those live fractional inputs are captured, preserve the
            // original raster fallback for these specific combiner states.
            if (alphaC == 0u || alphaC == 6u) return false;
            bool supportedA, supportedB, supportedC, supportedD;
            const float a = Fast3DAlphaMux(alphaA, cycle, combined.a, texel0, texel1, primitive, shade,
                                           environment, supportedA);
            const float b = Fast3DAlphaMux(alphaB, cycle, combined.a, texel0, texel1, primitive, shade,
                                           environment, supportedB);
            const float c = Fast3DAlphaMux(alphaC, cycle, combined.a, texel0, texel1, primitive, shade,
                                           environment, supportedC);
            const float d = Fast3DAlphaMux(alphaD, cycle, combined.a, texel0, texel1, primitive, shade,
                                           environment, supportedD);
            if (!supportedA || !supportedB || !supportedC || !supportedD) return false;
            alphaValue = saturate((a - b) * c + d);
        }
        combined = float4(rgbValue, alphaValue);
    }
    combinedResult = combined;
    return true;
}

float3 DecodeMaterialHit(uint instanceIndex, uint primitiveIndex, uint probeMode,
                         BuiltInTriangleIntersectionAttributes attribs,
                         out MaterialRecord material, out float2 uv,
                         out float3 surfaceNormal, out float4 vertexColor,
                         out float4 baseColor, out float3 naviPointAlbedo, out float vertexAlpha,
                         out float sampledTextureAlpha, out float textureLod,
                         out float3 surfaceTangent, out float3 surfaceBitangent,
                         out float3 geometricNormal, out bool frontFace) {
    material = LoadMaterialRecord(instanceIndex);
    uint triangleAddress = (material.indexOffset + primitiveIndex * 3u) * 4u;
    uint i0 = material.vertexOffset + gHitIndexData.Load(triangleAddress);
    uint i1 = material.vertexOffset + gHitIndexData.Load(triangleAddress + 4u);
    uint i2 = material.vertexOffset + gHitIndexData.Load(triangleAddress + 8u);
    HitVertex v0 = LoadHitVertex(i0);
    HitVertex v1 = LoadHitVertex(i1);
    HitVertex v2 = LoadHitVertex(i2);
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y,
                         attribs.barycentrics.x, attribs.barycentrics.y);
    uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    const float2 uv1 = v0.uv1 * bary.x + v1.uv1 * bary.y + v2.uv1 * bary.z;
    vertexColor = v0.color * bary.x + v1.color * bary.y + v2.color * bary.z;
    vertexAlpha = vertexColor.a;
    const float rawInterpolatedVertexAlpha = vertexAlpha;
    // The rasterizer reuses vertex alpha for fog. It is not opacity. Recover
    // the captured RSP alpha only on fog vertices; ordinary vertex alpha and
    // sampled texture alpha still govern cutout and transparent materials.
    float3 opacityAlpha = float3(v0.fogMode != 0u ? float(v0.sourceAlpha) / 255.0f : v0.color.a,
                                v1.fogMode != 0u ? float(v1.sourceAlpha) / 255.0f : v1.color.a,
                                v2.fogMode != 0u ? float(v2.sourceAlpha) / 255.0f : v2.color.a);
    vertexAlpha = dot(opacityAlpha, bary);
    vertexColor.a = vertexAlpha;
    if (probeMode == 1u && (((uint)gDiagnosticInfo.x & 64u) != 0u)) {
        vertexColor.a = 1.0f;
        vertexAlpha = 1.0f;
    }

    float3 localNormal;
    if (v0.normalState != 0u && v1.normalState != 0u && v2.normalState != 0u) {
        localNormal = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
    } else {
        localNormal = cross(v1.position - v0.position, v2.position - v0.position);
    }
    if (any(!isfinite(localNormal)) || dot(localNormal, localNormal) <= 1.0e-12f)
        localNormal = cross(v1.position - v0.position, v2.position - v0.position);
    float3x3 normalToView = transpose((float3x3)WorldToObject3x4());
    // DXR classifies the triangle after applying its instance transform,
    // including reflected transforms that can invert a manually transformed
    // geometric normal. Use that native facing result for refraction IOR.
    frontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
    surfaceNormal = normalize(mul(normalToView, normalize(localNormal)));
    if (dot(surfaceNormal, -WorldRayDirection()) < 0.0f) surfaceNormal = -surfaceNormal;
    geometricNormal = normalize(mul(normalToView,
        cross(v1.position - v0.position, v2.position - v0.position)));
    if (dot(geometricNormal, -WorldRayDirection()) < 0.0f) geometricNormal = -geometricNormal;

    float3 edge1 = v1.position - v0.position;
    float3 edge2 = v2.position - v0.position;
    float2 uvEdge1 = v1.uv - v0.uv;
    float2 uvEdge2 = v2.uv - v0.uv;
    const bool tile0Valid = (material.flags & 1u) != 0u && material.textureIndex < 4096u;
    const bool tile1Valid = (material.flags & MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID) != 0u &&
                            material.textureIndex1 < 4096u;
    const float2 selectedUvEdge1 = tile0Valid ? uvEdge1 : v1.uv1 - v0.uv1;
    const float2 selectedUvEdge2 = tile0Valid ? uvEdge2 : v2.uv1 - v0.uv1;
    textureLod = 0.0f;
    if (tile0Valid || tile1Valid) {
        const uint lodTextureIndex = tile0Valid ? material.textureIndex : material.textureIndex1;
        Texture2D<float4> albedoTexture = gMaterialTextures[NonUniformResourceIndex(lodTextureIndex)];
        uint textureWidth, textureHeight, mipCount;
        albedoTexture.GetDimensions(0u, textureWidth, textureHeight, mipCount);
        if (textureWidth > 0u && textureHeight > 0u && mipCount > 1u) {
            float3 worldEdge1 = mul((float3x3)ObjectToWorld3x4(), edge1);
            float3 worldEdge2 = mul((float3x3)ObjectToWorld3x4(), edge2);
            float density1 = length(selectedUvEdge1 * float2(textureWidth, textureHeight)) /
                             max(length(worldEdge1), 1.0e-5f);
            float density2 = length(selectedUvEdge2 * float2(textureWidth, textureHeight)) /
                             max(length(worldEdge2), 1.0e-5f);
            float pixelsPerWorldUnit = max(abs(gProjection[1][1]) * gScreenSize.y * gRasterViewport.w, 1.0f) * 0.5f;
            float3 hitViewPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
            float worldFootprint = max(length(hitViewPosition), 1.0e-3f) / pixelsPerWorldUnit;
            float footprintTexels = max(worldFootprint * max(density1, density2), 1.0f);
            textureLod = clamp(log2(footprintTexels), 0.0f, float(mipCount - 1u));
        }
    }
    float uvDeterminant = uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x;
    if (abs(uvDeterminant) > 1.0e-8f) {
        float3 localTangent = (edge1 * uvEdge2.y - edge2 * uvEdge1.y) / uvDeterminant;
        float3 localBitangent = (edge2 * uvEdge1.x - edge1 * uvEdge2.x) / uvDeterminant;
        float3x3 objectToView = (float3x3)ObjectToWorld3x4();
        surfaceTangent = normalize(mul(objectToView, localTangent));
        surfaceTangent = normalize(surfaceTangent - surfaceNormal * dot(surfaceNormal, surfaceTangent));
        float3 transformedBitangent = normalize(mul(objectToView, localBitangent));
        float handedness = dot(cross(surfaceNormal, surfaceTangent), transformedBitangent) < 0.0f ? -1.0f : 1.0f;
        surfaceBitangent = normalize(cross(surfaceNormal, surfaceTangent)) * handedness;
    } else {
        surfaceTangent = normalize(abs(surfaceNormal.y) < 0.9f
            ? cross(surfaceNormal, float3(0.0f, 1.0f, 0.0f))
            : cross(surfaceNormal, float3(1.0f, 0.0f, 0.0f)));
        surfaceBitangent = normalize(cross(surfaceNormal, surfaceTangent));
    }
    const float4 texel0 = SampleFast3DTexture(material, 0u, uv, textureLod);
    const float4 texel1 = SampleFast3DTexture(material, 1u, uv1, textureLod);
    const float3 sourceTint = v0.sourceTint * bary.x + v1.sourceTint * bary.y + v2.sourceTint * bary.z;
    const float4 sampledMaterialColor = tile0Valid ? texel0 : texel1;
    sampledTextureAlpha = sampledMaterialColor.a;
    // P0: Fast3D Material Correctness - Separate Albedo from Pre-lit Vertex Shade.
    // For lit surfaces (G_LIGHTING != 0), Fast3D generates vertexColor from N64
    // lights and normal vectors; sourceTint is 1.0. For unlit surfaces,
    // vertexColor and sourceTint represent authored artistic vertex tint.
    // Evaluating the combiner with unlitShade reconstructs the true unlit
    // physical albedo (baseColor) across all materials, preventing double-lighting
    // in primary relighting, secondary reflections, GI bounces, specular F0,
    // and native point lights. Fog is an atmospheric post-effect and is never
    // baked into physical surface reflectance.
    float4 unlitShade = float4(sourceTint, vertexAlpha);
    float4 fast3dCombined;
    bool combinerSupported = EvaluateFast3DCombiner(
        material, texel0, texel1, DecodePackedMaterialColor(material.primitiveColor), unlitShade,
        DecodePackedMaterialColor(material.environmentColor), fast3dCombined);
    if (combinerSupported) {
        baseColor = float4(SrgbToLinear(fast3dCombined.rgb) * material.baseColor.rgb,
                           fast3dCombined.a * material.baseColor.a);
    } else {
        // Conservative fallback preserving original unlit albedo without pre-lit lighting
        float4 fallbackAlbedo = tile0Valid ? texel0 : (tile1Valid ? texel1 : 1.0f.xxxx);
        fallbackAlbedo.rgb = SrgbToLinear(fallbackAlbedo.rgb);
        baseColor = saturate(fallbackAlbedo * float4(sourceTint, vertexAlpha) * material.baseColor);
    }
    baseColor = saturate(baseColor);
    naviPointAlbedo = baseColor.rgb;
    // One GPU-side first-hit sample per target record; tail of the existing
    // counter UAV/root binding, active only in the explicit forensic sequence.
    if (probeMode == 1u && (((uint)gDiagnosticInfo.x & 32u) != 0u)) {
        uint sampleSlot = instanceIndex == 0u ? 0u : (instanceIndex == 1u ? 1u :
                            (instanceIndex == 8u ? 2u : 0xFFFFFFFFu));
        if (sampleSlot != 0xFFFFFFFFu) {
            const uint sampleAddress = PRIMARY_ALPHA_SAMPLE_BASE + sampleSlot * PRIMARY_ALPHA_SAMPLE_STRIDE;
            uint previousClaim;
            InterlockedCompareExchange(gPrimaryGateCounters[sampleAddress], 0u, 1u, previousClaim);
            if (previousClaim == 0u) {
                gPrimaryGateCounters[sampleAddress + 1u] = instanceIndex;
                gPrimaryGateCounters[sampleAddress + 2u] = primitiveIndex;
                gPrimaryGateCounters[sampleAddress + 3u] = asuint(attribs.barycentrics.x);
                gPrimaryGateCounters[sampleAddress + 4u] = asuint(attribs.barycentrics.y);
                gPrimaryGateCounters[sampleAddress + 5u] = i0 - material.vertexOffset;
                gPrimaryGateCounters[sampleAddress + 6u] = i1 - material.vertexOffset;
                gPrimaryGateCounters[sampleAddress + 7u] = i2 - material.vertexOffset;
                gPrimaryGateCounters[sampleAddress + 8u] = (uint)(v0.color.a * 255.0f + 0.5f);
                gPrimaryGateCounters[sampleAddress + 9u] = (uint)(v1.color.a * 255.0f + 0.5f);
                gPrimaryGateCounters[sampleAddress + 10u] = (uint)(v2.color.a * 255.0f + 0.5f);
                gPrimaryGateCounters[sampleAddress + 11u] = asuint(rawInterpolatedVertexAlpha);
                gPrimaryGateCounters[sampleAddress + 12u] = asuint(sampledMaterialColor.a);
                gPrimaryGateCounters[sampleAddress + 13u] = asuint(material.baseColor.a);
                gPrimaryGateCounters[sampleAddress + 14u] = asuint(baseColor.a);
                gPrimaryGateCounters[sampleAddress + 15u] = asuint(vertexAlpha);
                gPrimaryGateCounters[sampleAddress + 16u] = v0.sourceAlpha;
                gPrimaryGateCounters[sampleAddress + 17u] = v1.sourceAlpha;
                gPrimaryGateCounters[sampleAddress + 18u] = v2.sourceAlpha;
                gPrimaryGateCounters[sampleAddress + 19u] = v0.recordLoadedAlpha;
                gPrimaryGateCounters[sampleAddress + 20u] = v1.recordLoadedAlpha;
                gPrimaryGateCounters[sampleAddress + 21u] = v2.recordLoadedAlpha;
                gPrimaryGateCounters[sampleAddress + 22u] = v0.capturedAlpha;
                gPrimaryGateCounters[sampleAddress + 23u] = v1.capturedAlpha;
                gPrimaryGateCounters[sampleAddress + 24u] = v2.capturedAlpha;
                gPrimaryGateCounters[sampleAddress + 25u] = v0.fogMode;
                gPrimaryGateCounters[sampleAddress + 26u] = v1.fogMode;
                gPrimaryGateCounters[sampleAddress + 27u] = v2.fogMode;
                gPrimaryGateCounters[sampleAddress + 28u] = v0.localAlpha;
                gPrimaryGateCounters[sampleAddress + 29u] = v1.localAlpha;
                gPrimaryGateCounters[sampleAddress + 30u] = v2.localAlpha;
            }
        }
    }
    return float3(v0.position * bary.x + v1.position * bary.y + v2.position * bary.z);
}

float3 ApplyAlbedoRelief(MaterialRecord material, float2 uv, float textureLod, float3 normal,
                         float3 tangent, float3 bitangent) {
    float strength = saturate(material.normalStrength * max(gQuality.z, 0.0f));
    // Albedo texture availability is bit 0; luminance-as-height is an
    // explicitly authored opt-in (bit 1) because ordinary color art is not
    // a physically meaningful height map.
    if (strength <= 1.0e-4f || (material.flags & 2u) == 0u ||
        (material.flags & 1u) == 0u || material.textureIndex >= 4096u) return normal;
    Texture2D<float4> albedoTexture = gMaterialTextures[NonUniformResourceIndex(material.textureIndex)];
    uint textureWidth, textureHeight;
    albedoTexture.GetDimensions(textureWidth, textureHeight);
    if (textureWidth == 0u || textureHeight == 0u) return normal;
    float2 texel = exp2(textureLod) / float2(textureWidth, textureHeight);
    float heightLeft = SrgbLuminance(albedoTexture.SampleLevel(
        gMaterialSampler, uv - float2(texel.x, 0.0f), textureLod).rgb);
    float heightRight = SrgbLuminance(albedoTexture.SampleLevel(
        gMaterialSampler, uv + float2(texel.x, 0.0f), textureLod).rgb);
    float heightUp = SrgbLuminance(albedoTexture.SampleLevel(
        gMaterialSampler, uv - float2(0.0f, texel.y), textureLod).rgb);
    float heightDown = SrgbLuminance(albedoTexture.SampleLevel(
        gMaterialSampler, uv + float2(0.0f, texel.y), textureLod).rgb);
    float gradientU = (heightRight - heightLeft) * 0.5f;
    float gradientV = (heightDown - heightUp) * 0.5f;
    return normalize(normal - (tangent * gradientU + bitangent * gradientV) * (strength * 0.35f));
}

#include "WaterMaterial.hlsli"
#include "LavaMaterial.hlsli"

[shader("anyhit")]
void AnyHitShadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    uint validatedFlags;
    if (ValidatePrimaryHit(InstanceID(), GeometryIndex(), PrimitiveIndex(), validatedFlags) != 0u) {
        IgnoreHit();
        return;
    }
    MaterialRecord material = LoadMaterialRecord(InstanceID());
    // Probe mode 1 is the camera-to-raster-depth match and must retain the
    // window surface. Lighting/shadow rays pass through only the exact
    // source-verified Temple window materials; their real triangles remain
    // the aperture geometry and surrounding frames/walls still occlude.
    if (payload.probeMode != 1u && (material.flags & MATERIAL_FLAG_DXR_WINDOW_PORTAL) != 0u) {
        IgnoreHit();
        return;
    }
    if ((material.flags & MATERIAL_FLAG_DECORATIVE_BEAM) != 0u ||
        (material.flags & MATERIAL_FLAG_NAVI_POINT_PROXY) != 0u) {
        IgnoreHit();
        return;
    }
    if (material.alphaMode == 1u) {
        MaterialRecord sampledMaterial;
        float2 uv;
        float3 normal;
        float3 tangent, bitangent;
        float3 geometricNormal;
        float4 vertexColor;
        float4 baseColor;
        float vertexAlpha;
        float sampledTextureAlpha;
        float textureLod;
        bool frontFace;
        float3 unusedNaviPointAlbedo;
        DecodeMaterialHit(InstanceID(), PrimitiveIndex(), 0u, attribs, sampledMaterial,
                          uv, normal, vertexColor, baseColor, unusedNaviPointAlbedo, vertexAlpha, sampledTextureAlpha, textureLod,
                          tangent, bitangent, geometricNormal, frontFace);
        if (baseColor.a < 0.5f) IgnoreHit();
    } else if (material.alphaMode == 2u || material.alphaMode == 3u) {
        IgnoreHit();
    }
}

[shader("closesthit")]
void ClosestHitShadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.visibility = 0.0f;
    if (payload.probeMode != 1u) return;
    uint validatedFlags;
    if (ValidatePrimaryHit(InstanceID(), GeometryIndex(), PrimitiveIndex(), validatedFlags) != 0u) return;
    MaterialRecord material;
    float2 uv;
    float3 normal, tangent, bitangent, geometricNormal;
    float4 vertexColor, baseColor;
    float vertexAlpha, sampledTextureAlpha, textureLod;
    bool frontFace;
    float3 unusedNaviPointAlbedo;
    // Mode zero decodes the same geometry without the surface pass's optional
    // primary diagnostic UAV, which is absent from the shadow root signature.
    DecodeMaterialHit(InstanceID(), PrimitiveIndex(), 0u, attribs,
                      material, uv, normal, vertexColor, baseColor, unusedNaviPointAlbedo,
                      vertexAlpha, sampledTextureAlpha, textureLod,
                      tangent, bitangent, geometricNormal, frontFace);
    payload.normal = geometricNormal;
    payload.hitDistance = RayTCurrent();
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 MaterialFresnelSchlick(float cosTheta, float3 f0) {
    float factor = pow(1.0f - saturate(cosTheta), 5.0f);
    return f0 + (1.0f - f0) * factor;
}

float MaterialNdfGGX(float nDotH, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(3.14159265f * denominator * denominator, 1.0e-5f);
}

float MaterialGeometrySchlickGGX(float nDotX, float roughness) {
    float k = (roughness + 1.0f) * (roughness + 1.0f) * 0.125f;
    return nDotX / max(nDotX * (1.0f - k) + k, 1.0e-5f);
}

float3 EvaluateMaterialSpecularLight(float3 baseColor, float3 normal, float3 viewDirection,
                                     float3 lightDirection, float roughness, float metallic,
                                     float specular) {
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;

    float3 halfVector = normalize(viewDirection + lightDirection);
    float nDotH = saturate(dot(normal, halfVector));
    float vDotH = saturate(dot(viewDirection, halfVector));
    float dielectricF0 = (specular > 0.0f) ? (0.04f * saturate(specular)) : 0.04f;
    float3 f0 = lerp(dielectricF0.xxx, baseColor, saturate(metallic));
    float3 fresnel = MaterialFresnelSchlick(vDotH, f0);
    float distribution = MaterialNdfGGX(nDotH, roughness);
    float geometry = MaterialGeometrySchlickGGX(nDotV, roughness) *
                     MaterialGeometrySchlickGGX(nDotL, roughness);
    float3 specularBrdf = distribution * geometry * fresnel /
                          max(4.0f * nDotV * nDotL, 1.0e-4f);
    return specularBrdf * nDotL;
}

float3 EvaluateMaterialDiffuseLight(float3 baseColor, float3 normal, float3 viewDirection,
                                    float3 lightDirection, float metallic, float specular) {
    float nDotL = saturate(dot(normal, lightDirection));
    if (nDotL <= 0.0f) return 0.0f;
    float dielectricF0 = (specular > 0.0f) ? (0.04f * saturate(specular)) : 0.04f;
    float3 f0 = lerp(dielectricF0.xxx, baseColor, saturate(metallic));
    float3 fresnel = MaterialFresnelSchlick(
        saturate(dot(viewDirection, normalize(viewDirection + lightDirection))), f0);
    float3 diffuseBrdf = baseColor * (1.0f - saturate(metallic)) * (1.0f - fresnel) * (1.0f / 3.14159265f);
    return diffuseBrdf * nDotL;
}

float3 EvaluateMaterialDirectLight(float3 baseColor, float3 normal, float3 viewDirection,
                                   float3 lightDirection, float roughness, float metallic,
                                   float specular) {
    return EvaluateMaterialDiffuseLight(baseColor, normal, viewDirection, lightDirection,
                                        metallic, specular) +
           EvaluateMaterialSpecularLight(baseColor, normal, viewDirection,
                                         lightDirection, roughness, metallic, specular);
}

float3 OffsetMaterialRayOrigin(float3 position, float3 geometricNormal, float3 direction, float minimumBias) {
    float positionMagnitude = max(abs(position.x), max(abs(position.y), abs(position.z)));
    float bias = max(minimumBias, positionMagnitude * 4.7683716e-7f);
    float side = dot(geometricNormal, direction) >= 0.0f ? 1.0f : -1.0f;
    return position + geometricNormal * (side * bias);
}

float TraceMaterialVisibility(float3 hitPosition, float3 geometricNormal, float3 lightDirection,
                              float maxDistance) {
    ReflectionPayload visibility;
    InitializeReflectionPayload(visibility, 2u);
    // Start blocked: accepted opaque hits keep zero. Only MissReflection
    // marks an unobstructed path visible; alpha cutouts can still IgnoreHit.
    visibility.surfaceOptics.w = 0.0f;
    RayDesc shadowRay;
    shadowRay.Direction = normalize(lightDirection);
    float cosTheta = saturate(dot(geometricNormal, shadowRay.Direction));
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    float contactScale = 0.25f + 0.75f * sinTheta;
    float adaptiveBias = clamp(0.025f * contactScale, 0.008f, 0.05f);
    shadowRay.Origin = OffsetMaterialRayOrigin(hitPosition, geometricNormal, shadowRay.Direction, adaptiveBias);
    shadowRay.TMin = max(adaptiveBias * 0.2f, 0.002f);
    if (maxDistance <= shadowRay.TMin) return 1.0f;
    shadowRay.TMax = maxDistance;
    TraceRay(gScene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0x03, 0, 1, 0, shadowRay, visibility);
    return saturate(visibility.surfaceOptics.w);
}

bool NativePointRepresentsEmitter(MaterialRecord material) {
    if ((material.flags & MATERIAL_FLAG_NAVI_POINT_PROXY) == 0u) return false;
    uint owner = material.samplerFlags >> 16u;
    if (owner == 0u) return false;
    [loop] for (uint i = 0u; i < min(32u, (uint)gQuality.w); ++i) {
        uint identity = (uint)gPointLightOptics[i].x;
        if (identity / 8u == owner && identity % 8u == 1u && gPointLightPosition[i].w > 0.0f &&
            any(gPointLightOptics[i].yzw > 0.0f)) return true;
    }
    return false;
}

// Bounded next-event estimation from the selected fairy's captured emissive mesh.
// A miss contributes ZERO: the environment and unrelated emissive materials cannot pass C.
float3 TraceSelectedEmitter(float3 position, float3 geometricNormal, float3 normal,
                            float3 albedo) {
    float3 result = 0.0f;
    [loop] for (uint i = 0u; i < min(8u, gSourceControls.z); ++i) {
        float3 toEmitter = gEmitterSamplePosition[i].xyz - position;
        float distanceSquared = dot(toEmitter, toEmitter);
        if (distanceSquared < 0.01f) continue;
        float distance = sqrt(distanceSquared);
        float3 direction = toEmitter / distance;
        float receiverCosine = saturate(dot(normal, direction));
        // Fairy sprites are double sided. This is explicit authoring, not an inferred sun.
        float emitterCosine = abs(dot(gEmitterSampleNormal[i].xyz, -direction));
        if (receiverCosine <= 0.0f || emitterCosine <= 0.0f) continue;
        RayDesc ray;
        ray.Origin = OffsetMaterialRayOrigin(position, geometricNormal, direction, 0.025f);
        float3 offsetToEmitter = gEmitterSamplePosition[i].xyz - ray.Origin;
        ray.Direction = normalize(offsetToEmitter);
        ray.TMin = 0.001f;
        ray.TMax = length(offsetToEmitter) + 0.02f;
        ReflectionPayload emitter;
        InitializeReflectionPayload(emitter, 6u);
        uint sampleIdentity = gEmitterSampleIdentity[i / 4u][i % 4u];
        emitter.instanceIndex = sampleIdentity >> 16u;
        emitter.textureIndex = sampleIdentity & 65535u;
        TraceRay(gScene, RAY_FLAG_NONE, 0x03, 0, 1, 0, ray, emitter);
        result += albedo * emitter.radiance.rgb *
            (receiverCosine * emitterCosine * gEmitterSamplePosition[i].w /
             max(3.14159265f * distanceSquared, 1.0e-5f));
    }
    return result;
}

// OCZ-LAVA-02: bounded next-event estimation from material-derived magma
// light samples. Sources are area-weighted triangle samples captured from
// real LAVA-classified geometry (never native LightInfo slots, never a
// per-texel point light). Occlusion is the same probeMode-6 exact-identity
// trace the emitter harness uses: a miss, an occluder hit, or any non-target
// triangle contributes zero radiance, so walls between the melt and a
// receiver stay dark. Receiver response is the standard diffuse+specular
// material BRDF; emitter radiance is the evaluated thermal emission of the
// sampled lava triangle, which already tracks heat, crust and flow.
float3 TraceLavaLighting(float3 position, float3 geometricNormal, float3 normal,
                         float3 viewDirection, float3 albedo, float roughness,
                         float metallic, float specular) {
    float3 result = 0.0f;
    const float lightGain = max(gEmitterSampleNormal[0].w, 0.0f);
    [loop] for (uint i = 0u; i < min(8u, (uint)gSourceControls.z); ++i) {
        float3 toEmitter = gEmitterSamplePosition[i].xyz - position;
        float distanceSquared = dot(toEmitter, toEmitter);
        if (distanceSquared < 0.01f) continue;
        float distance = sqrt(distanceSquared);
        float3 direction = toEmitter / distance;
        // EvaluateMaterialDirectLight already folds the receiver cosine.
        float receiverCosine = saturate(dot(normal, direction));
        // Melt is single-sided and faces up: never light a surface from the
        // pool's underside, and never use abs() the way the double-sided
        // fairy sprites do.
        float emitterCosine = saturate(dot(gEmitterSampleNormal[i].xyz, -direction));
        if (receiverCosine <= 0.0f || emitterCosine <= 0.0f) continue;
        RayDesc ray;
        ray.Origin = OffsetMaterialRayOrigin(position, geometricNormal, direction, 0.025f);
        float3 offsetToEmitter = gEmitterSamplePosition[i].xyz - ray.Origin;
        ray.Direction = normalize(offsetToEmitter);
        ray.TMin = 0.001f;
        ray.TMax = length(offsetToEmitter) + 0.02f;
        ReflectionPayload emitter;
        InitializeReflectionPayload(emitter, 6u);
        uint sampleIdentity = gEmitterSampleIdentity[i / 4u][i % 4u];
        emitter.instanceIndex = sampleIdentity >> 16u;
        emitter.textureIndex = sampleIdentity & 65535u;
        TraceRay(gScene, RAY_FLAG_NONE, 0x03, 0, 1, 0, ray, emitter);
        const float3 emitterRadiance = max(emitter.radiance.rgb, 0.0f.xxx);
        if (!any(emitterRadiance > 0.0f)) continue;
        result += EvaluateMaterialDirectLight(albedo, normal, viewDirection, direction,
                                              roughness, metallic, specular) *
                  emitterRadiance *
                  (emitterCosine * gEmitterSamplePosition[i].w * lightGain /
                   max(3.14159265f * distanceSquared, 1.0e-5f));
    }
    if (any(!isfinite(result))) return 0.0f;
    return max(result, 0.0f.xxx);
}

// Sparse deterministic first-hit records share the existing forensic UAV. Every
// value below is written by the GPU from this exact receiver, not inferred from PNGs.
#define NAVI_SAMPLE_BASE (64u * 2112u + 32u * 3u)
#define NAVI_SAMPLE_STRIDE 40u
void RecordNaviReceiver(uint2 pixel, ReflectionPayload receiver, float3 position,
                         float3 pointRadiance, float3 pointDiffuse, float3 pointSpecular,
                         float3 mesh, float distance, float radius,
                         float attenuation, float visibility, float cosine, uint confidence) {
    uint2 grid = uint2(48u, 27u);
    uint2 dimensions = uint2(gScreenSize.xy); // full raster dimensions; raygen can be half resolution
    uint2 dispatchDimensions = DispatchRaysDimensions().xy;
    uint2 cell = min(pixel * grid / dispatchDimensions, grid - 1u);
    uint2 samplePixel = min((cell * dispatchDimensions + dispatchDimensions / 2u) / grid,
                            dispatchDimensions - 1u);
    if (any(pixel != samplePixel)) return;
    uint address = NAVI_SAMPLE_BASE + (cell.y * grid.x + cell.x) * NAVI_SAMPLE_STRIDE;
    gPrimaryGateCounters[address] = 1u;
    gPrimaryGateCounters[address+1u] = pixel.x;
    gPrimaryGateCounters[address+2u] = pixel.y;
    gPrimaryGateCounters[address+3u] = receiver.instanceIndex;
    gPrimaryGateCounters[address+4u] = (uint)receiver.geometricNormal.w;
    gPrimaryGateCounters[address+5u] = receiver.materialIndex;
    gPrimaryGateCounters[address+6u] = confidence;
    [unroll] for (uint axis=0u; axis<3u; ++axis) {
        gPrimaryGateCounters[address+7u+axis] = asuint(receiver.baseColorMetallic[axis]);
        gPrimaryGateCounters[address+10u+axis] = asuint(receiver.normalRoughness[axis]);
        gPrimaryGateCounters[address+13u+axis] = asuint(position[axis]);
        gPrimaryGateCounters[address+16u+axis] = asuint(pointRadiance[axis]);
        gPrimaryGateCounters[address+19u+axis] = asuint(mesh[axis]);
    }
    gPrimaryGateCounters[address+22u] = asuint(distance);
    gPrimaryGateCounters[address+23u] = asuint(radius);
    gPrimaryGateCounters[address+24u] = asuint(attenuation);
    gPrimaryGateCounters[address+25u] = asuint(visibility);
    gPrimaryGateCounters[address+26u] = asuint(cosine);
    [unroll] for (uint axis=0u; axis<3u; ++axis) {
        gPrimaryGateCounters[address+27u+axis] = asuint(pointDiffuse[axis]);
        gPrimaryGateCounters[address+30u+axis] = asuint(pointSpecular[axis]);
    }
}

float3 EvaluateMaterialPointLight(float3 baseColor, float3 normal, float3 geometricNormal, float3 viewDirection,
                                  float3 hitPosition, float4 lightPosition,
                                  float4 lightOptics, float roughness,
                                  float metallic, float specular) {
    if (!any(lightOptics.yzw > 0.0f)) return 0.0f;
    float3 toLight = lightPosition.xyz - hitPosition;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 1.0e-4f) return 0.0f;
    float lightRadius = lightPosition.w;
    if (lightRadius <= 0.0f || distanceToLight >= lightRadius) return 0.0f;
    float3 lightDirection = toLight / max(distanceToLight, 1.0e-4f);
    float normalizedDistance = distanceToLight / lightRadius;
    float radialFalloff = saturate(1.0f - normalizedDistance * normalizedDistance);
    float attenuation = radialFalloff; // match Lights_BindPoint's native 1 - (d/r)^2
    // Avoid a visibility ray when the largest possible RGB contribution is
    // already below the scene-linear noise floor. N64 light colors are
    // normalized to [0, 1] when captured, so this bound is conservative.
    if (attenuation * max(lightOptics.y, max(lightOptics.z, lightOptics.w)) <= 1.0e-4f ||
        dot(normal, lightDirection) <= 0.0f) return 0.0f;
    float visibility = TraceMaterialVisibility(hitPosition, geometricNormal, lightDirection,
                                               distanceToLight - 0.025f);
    float3 linearColor = SrgbToLinear(max(lightOptics.yzw, 0.0f.xxx));
    return EvaluateMaterialDirectLight(baseColor, normal, viewDirection, lightDirection,
                                       roughness, metallic, specular) * linearColor *
           (attenuation * visibility);
}

float3 EvaluateMaterialPointBackscatter(float3 baseColor, float3 normal, float3 geometricNormal, float3 hitPosition,
                                        float4 lightPosition, float4 lightOptics, float amount) {
    if (!any(lightOptics.yzw > 0.0f) || amount <= 0.0f) return 0.0f;
    float3 toLight = lightPosition.xyz - hitPosition;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 1.0e-4f) return 0.0f;
    float lightRadius = lightPosition.w;
    if (lightRadius <= 0.0f || distanceToLight >= lightRadius) return 0.0f;
    float3 lightDirection = toLight / max(distanceToLight, 1.0e-4f);
    float normalizedDistance = distanceToLight / lightRadius;
    float radialFalloff = saturate(1.0f - normalizedDistance * normalizedDistance);
    float attenuation = radialFalloff;
    if (attenuation * amount * max(lightOptics.y, max(lightOptics.z, lightOptics.w)) <= 1.0e-4f) {
        return 0.0f;
    }
    float visibility = TraceMaterialVisibility(hitPosition, geometricNormal, lightDirection,
                                               distanceToLight - 0.025f);
    float backScatter = pow(saturate(dot(-normal, lightDirection)), 2.0f);
    float3 linearColor = SrgbToLinear(max(lightOptics.yzw, 0.0f.xxx));
    return baseColor * linearColor * (backScatter * attenuation * amount * visibility);
}

void ClearSurfaceSignals(uint2 pixel) {
    gSurfaceMask[pixel] = 0.0f;
    gEmissiveMask[pixel] = 0.0f;
    gIndirectMask[pixel] = 0.0f;
    gReflectionColor[pixel] = 0.0f;
    gNativePointDirect[pixel] = 0.0f;
}

float3 MaterialDebugColor(uint mode, ReflectionPayload surface) {
    if (mode == 1u) {
        uint id = surface.materialIndex;
        return float3(frac(id * 0.1031f), frac(id * 0.11369f + 0.33f), frac(id * 0.13787f + 0.67f));
    }
    if (mode == 2u) return surface.baseColorMetallic.rgb;
    if (mode == 3u) return surface.normalRoughness.xyz * 0.5f + 0.5f;
    if (mode == 4u) return surface.normalRoughness.www;
    if (mode == 5u) return surface.baseColorMetallic.www;
    if (mode == 6u) return surface.surfaceOptics.xxx;
    if (mode == 7u) return saturate(surface.surfaceOptics.zzz / max(gParams.y, 1.0f));
    if (mode == 8u) return surface.radiance.aaa;
    uint id = surface.instanceIndex;
    return float3(frac(id * 0.1031f), frac(id * 0.11369f + 0.33f), frac(id * 0.13787f + 0.67f));
}

[shader("raygeneration")]
void RayGenReflection() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    gNativePointDirect[launchIndex] = 0.0f;
    uint2 launchDim = DispatchRaysDimensions().xy;
    uint2 screenDim = max(uint2(gScreenSize.xy), uint2(1, 1));
    uint2 screenPixel = min(uint2((float2(launchIndex) + 0.5f) * (float2(screenDim) / float2(launchDim))), screenDim - 1);
    const bool gateDiagnostic = (((uint)gDiagnosticInfo.x & 1u) != 0u);
    const bool gateCountersEnabled = (((uint)gDiagnosticInfo.x & 4u) != 0u);
    float depth = gDepthTexture.Load(int3(screenPixel, 0));
    if (gateDiagnostic) ClearSurfaceSignals(launchIndex);
    if (!isfinite(depth) || depth < 0.0f || depth >= 1.0f) {
        if (gateDiagnostic && gateCountersEnabled) IncrementPrimaryGateCounter(PG_RASTER_BACKGROUND, launchIndex);
        ClearSurfaceSignals(launchIndex);
        if (gateDiagnostic) gReflectionColor[launchIndex] = float4(1.0f / 255.0f, 10.0f / 255.0f, 0.0f, 1.0f);
        return;
    }
    if (gateDiagnostic && gateCountersEnabled) IncrementPrimaryGateCounter(PG_RASTER_GEOMETRY, launchIndex);

    float2 uv = RasterToViewportUV((float2(screenPixel) + 0.5f) / float2(screenDim));
    if (any(uv < 0.0f) || any(uv > 1.0f)) {
        ClearSurfaceSignals(launchIndex);
        if (gateDiagnostic) gReflectionColor[launchIndex] = float4(1.0f / 255.0f, 10.0f / 255.0f, 0.0f, 1.0f);
        return;
    }
    float clipXScale = gParams.z > 1e-4f ? gParams.z : 1.0f;
    uint debugMode = (uint)max(gLightDir.w, 0.0f);
    // Material views observe the same view-space ray as the productive path.
    // Alternative world/transposed/reversed rays must not masquerade as
    // material-class diagnostics over a view-space TLAS.
    float ndcX = uv.x * 2.0f - 1.0f;
    float ndcY = 1.0f - uv.y * 2.0f;
    // Match Interpreter::GfxSpTri1, which maps Fast3D clip Z [-w, w] to
    // D3D12's depth range [0, 1] before rasterization.
    float4 clipPos = float4(ndcX / clipXScale, ndcY, depth * 2.0f - 1.0f, 1.0f);
    float4 viewPosH = mul(gInvProjection, clipPos);
    float4 nearPosH = mul(gInvProjection, float4(ndcX / clipXScale, ndcY, -1.0f, 1.0f));
    float4 farPosH = mul(gInvProjection, float4(ndcX / clipXScale, ndcY, 1.0f, 1.0f));
    if (any(!isfinite(viewPosH)) || any(!isfinite(nearPosH)) || any(!isfinite(farPosH)) ||
        abs(viewPosH.w) <= 1.0e-6f || abs(nearPosH.w) <= 1.0e-6f || abs(farPosH.w) <= 1.0e-6f) {
        ClearSurfaceSignals(launchIndex);
        if (gateDiagnostic) gReflectionColor[launchIndex] = float4(1.0f / 255.0f, 9.0f / 255.0f, 0.0f, 1.0f);
        return;
    }

    float3 viewPos = viewPosH.xyz / viewPosH.w;
    float3 nearPos = nearPosH.xyz / nearPosH.w;
    float3 raySpan = farPosH.xyz / farPosH.w - nearPos;
    float raySpanLength = length(raySpan);
    if (!isfinite(raySpanLength) || raySpanLength <= 1.0e-6f) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    // Start at the raster near plane: geometry clipped before it must not
    // steal the primary hit. This construction also supports orthographic P.
    RayDesc primaryRay;
    primaryRay.Origin = nearPos;
    primaryRay.Direction = raySpan / raySpanLength;
    float distanceToRaster = dot(viewPos - nearPos, primaryRay.Direction);
    if (!isfinite(distanceToRaster) || distanceToRaster < 0.0f) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    primaryRay.TMin = 0.0f;
    // Stop at the rasterized surface reconstructed from this pixel's depth.
    // A long diagnostic ray could hit geometry behind that visible surface,
    // assigning the wrong material and leaking hidden geometry into the
    // reflection, emissive, and indirect-light masks.
    primaryRay.TMax = distanceToRaster + max(0.01f, distanceToRaster * 0.001f);
    ReflectionPayload surface;
    InitializeReflectionPayload(surface, 1u);
    if (gateCountersEnabled) IncrementPrimaryGateCounter(0u, launchIndex);
    TraceRay(gScene, RAY_FLAG_NONE,
             0x03, 0, 1, 0, primaryRay, surface);
    // A nearest TLAS hit alone does not establish correspondence with this
    // raster pixel. Depth-writing hits must match depth before illuminating
    // it; water/glass without Z_UPD may lie in front of the depth surface.
    // Error 11 reports raster/DXR distance disagreement.
    const float depthAgreementTolerance = max(0.05f, distanceToRaster * 0.002f);
    if (surface.surfaceOptics.w >= 0.5f && (surface.diagnosticFlags & 128u) != 0u &&
        (!isfinite(surface.surfaceOptics.z) ||
         abs(surface.surfaceOptics.z - distanceToRaster) > depthAgreementTolerance)) {
        surface.surfaceOptics.w = 0.0f;
        surface.diagnosticError = 11u;
        surface.diagnosticFlags &= ~8u;
    }
    if (gateDiagnostic) {
        const bool primaryHit = (surface.diagnosticFlags & 2u) != 0u;
        if (gateCountersEnabled) {
            if (primaryHit) IncrementPrimaryGateCounter(PG_GEOMETRY_PRIMARY_HIT, launchIndex);
            else IncrementPrimaryGateCounter(PG_GEOMETRY_PRIMARY_MISS, launchIndex);
            if (primaryHit) IncrementPrimaryGateCounter(2u, launchIndex);
            else IncrementPrimaryGateCounter(1u, launchIndex);
            if ((surface.diagnosticFlags & 4u) != 0u) IncrementPrimaryGateCounter(3u, launchIndex);
            if ((surface.diagnosticFlags & 8u) != 0u) IncrementPrimaryGateCounter(4u, launchIndex);
            if (surface.diagnosticError >= 2u && surface.diagnosticError <= 11u) {
                IncrementPrimaryGateCounter(8u, launchIndex);
                if (surface.diagnosticError <= 10u)
                    IncrementPrimaryGateCounter(10u + surface.diagnosticError - 2u, launchIndex);
            }
            if (surface.diagnosticError == 9u) IncrementPrimaryGateCounter(9u, launchIndex);
            if (surface.diagnosticError == 11u) IncrementPrimaryGateCounter(PG_RASTER_DEPTH_MISMATCH, launchIndex);
        }
        uint gateFlags = surface.diagnosticFlags | 1u;
        uint gateError = surface.diagnosticError;
        if ((gateFlags & 2u) == 0u && gateError == 0xFFFFFFFFu) gateError = 1u;
        if (gateError != 0xFFFFFFFFu && gateError != 1u) {
            gReflectionColor[launchIndex] = float4(float(gateFlags) / 255.0f,
                                                  float(gateError) / 255.0f, 0.0f, 1.0f);
            return;
        }
        if ((gateFlags & 2u) == 0u) {
            gReflectionColor[launchIndex] = float4(float(gateFlags) / 255.0f, 1.0f / 255.0f, 0.0f, 1.0f);
            return;
        }
        if ((gateFlags & 8u) == 0u) {
            gReflectionColor[launchIndex] = float4(float(gateFlags) / 255.0f, 5.0f / 255.0f, 0.0f, 1.0f);
            return;
        }
    }
    if (surface.surfaceOptics.w < 0.5f) {
        ClearSurfaceSignals(launchIndex);
        if (debugMode != 0u) {
            // Distinguish a valid background pixel from a raster-depth pixel
            // whose camera ray failed to find its corresponding TLAS triangle.
            gSurfaceMask[launchIndex] = float4(1.0f, 0.0f, 0.0f, 0.0f);
            gReflectionColor[launchIndex] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        }
        return;
    }

    // Reflection, transmission and GI rays originate at the confirmed DXR
    // intersection, rather than at the nearby point estimated from depth.
    viewPos = primaryRay.Origin + primaryRay.Direction * surface.surfaceOptics.z;
    float4 worldSeedH = mul(gInvView, float4(viewPos, 1.0f));
    float3 worldSeedPosition = abs(worldSeedH.w) > 1.0e-6f
        ? worldSeedH.xyz / worldSeedH.w : worldSeedH.xyz;
    float2 stableSampleSeed = float2(
        worldSeedPosition.x + worldSeedPosition.z * 17.0f + float(surface.instanceIndex) * 0.37f,
        worldSeedPosition.y + worldSeedPosition.z * 7.0f + float(surface.instanceIndex) * 0.73f);

    float3 normal = normalize(surface.normalRoughness.xyz);
    float3 geometricNormal = surface.geometricNormal.xyz;
    float geometricLength = length(geometricNormal);
    if (any(!isfinite(geometricNormal)) || geometricLength <= 1.0e-6f) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    geometricNormal /= geometricLength;
    // OCZ-WATER-01: load the aquatic profile once and reuse it for normals,
    // Fresnel, reflection, refraction, absorption, and native lights.
    const bool isWaterSurface = surface.materialIndex == MATERIAL_TYPE_WATER;
    WaterProfile waterProfile;
    if (isWaterSurface) {
        MaterialRecord waterRecord = LoadMaterialRecord(surface.instanceIndex);
        waterProfile = WaterProfileFromRecord(waterRecord.waterOptics, waterRecord.roughness,
                                              waterRecord.roughness);
        // A WATER-typed hit with no classified profile (excluded asset, UI,
        // or an unproven surface) must keep the original Fast3D pixels; it
        // still follows the legacy dielectric path below, but never the
        // hyperreal liquid model.
        if (!WaterProfileActive(waterProfile)) {
            waterProfile.cls = WATER_CLASS_NONE;
            waterProfile.opticsBlend = 0.0f;
        }
    } else {
        waterProfile.cls = WATER_CLASS_NONE;
        waterProfile.opticsBlend = 0.0f;
    }
    const bool hyperrealWater = isWaterSurface && WaterProfileActive(waterProfile);
    // OCZ-LAVA-01: load the molten profile once and reuse it for normals,
    // albedo, roughness and thermal emission. Melt is opaque and emissive;
    // it never follows the water dielectric path below.
    const bool isLavaSurface = surface.materialIndex == MATERIAL_TYPE_LAVA;
    MaterialRecord lavaRecord;
    LavaProfile lavaProfile;
    if (isLavaSurface) {
        lavaRecord = LoadMaterialRecord(surface.instanceIndex);
        lavaProfile = LavaProfileFromRecord(lavaRecord.waterOptics, lavaRecord.roughness);
        // A LAVA-typed hit with no classified profile (a torch flame the pilot
        // table mislabelled, a 2D billboard, an effect sprite, or an unproven
        // surface) must keep the original Fast3D pixels; it never inherits the
        // hyperreal magma model.
        if (!LavaProfileActive(lavaProfile)) {
            lavaProfile.cls = LAVA_CLASS_NONE;
            lavaProfile.opticsBlend = 0.0f;
        }
    } else {
        lavaProfile.cls = LAVA_CLASS_NONE;
        lavaProfile.opticsBlend = 0.0f;
    }
    const bool hyperrealLava = isLavaSurface && LavaProfileActive(lavaProfile);
    // Time base for the viscous advection: the monotonic dispatch counter.
    // Phase-continuous, so the melt never pops between frames.
    const float waterTime = gFrameInfo.x * 0.016f;
    if (hyperrealWater) {
        normal = WaterShadeNormal(LoadMaterialRecord(surface.instanceIndex), surface.detail.xy,
                                  0.0f, geometricNormal, waterProfile, waterTime,
                                  viewPos, surface.surfaceOptics.z);
        if (any(!isfinite(normal)) || dot(normal, normal) <= 1.0e-12f) {
            ClearSurfaceSignals(launchIndex);
            return;
        }
        normal = normalize(normal);
    } else if (isWaterSurface) {
        // Legacy path for excluded/unclassified water: preserve the original
        // subtle ripple so nothing visibly regresses on DXR OFF-equivalent use.
        float2 waveCoord = surface.detail.xy * 12.0f;
        float wave = sin(waveCoord.x * 3.14159f + waveCoord.y * 2.0f) * 0.035f +
                     cos(waveCoord.y * 4.0f - waveCoord.x * 2.5f) * 0.035f;
        float3 worldNormal = normalize(mul((float3x3)gInvView, normal));
        float3 worldTangent = normalize(abs(worldNormal.y) < 0.9f
            ? cross(worldNormal, float3(0.0f, 1.0f, 0.0f))
            : cross(worldNormal, float3(1.0f, 0.0f, 0.0f)));
        float3 worldBitangent = cross(worldNormal, worldTangent);
        float3 waveTangent = normalize(mul(transpose((float3x3)gInvView), worldTangent));
        float3 waveBitangent = normalize(mul(transpose((float3x3)gInvView), worldBitangent));
        normal = normalize(normal + waveTangent * wave + waveBitangent * (wave * 0.7f));
    }
    float3 viewDirection = -primaryRay.Direction;
    float roughness = saturate(surface.normalRoughness.w);
    float metallic = saturate(surface.baseColorMetallic.w);
    float3 baseColor = saturate(surface.baseColorMetallic.rgb);
    float reflectivity = saturate(surface.surfaceOptics.x);
    float emissiveStrength = max(surface.surfaceOptics.y, 0.0f);
    // OCZ-WATER-02: reflection rays use the class microfacet roughness
    // (lake 0.030, falling 0.120 …), not the raster normalRoughness.w.
    // Without this override the multi-scale wave normals feed a 0.5-rough
    // GGX lobe and the lake reflects as a dull dielectric instead of a
    // liquid mirror.
    if (hyperrealWater) {
        roughness = waterProfile.roughness;
    }
    float dielectricF0 = (surface.detail.z > 0.0f) ? (0.04f * saturate(surface.detail.z)) : 0.04f;
    float3 f0 = lerp(dielectricF0.xxx, baseColor, metallic);
    float fresnelPower = pow(1.0f - saturate(dot(normal, viewDirection)), 5.0f);
    float3 fresnel = f0 + (1.0f - f0) * fresnelPower;
    // OCZ-WATER-01: water overrides the generic dielectric model with the
    // exact IOR-1.333 Fresnel. F0 = 0.0204 (0.04 * 0.51 specular, matching
    // the original specular response). This is what separates a real liquid
    // from a glossy dielectric: the correct F0 and a physically shaped curve.
    if (hyperrealWater) {
        const float waterF = WaterFresnelSchlick(saturate(dot(normal, viewDirection)), WATER_F0);
        fresnel = waterF.xxx;
    }
    // detail.w is geometric coverage computed from the material alpha class
    // in ClosestHit, never raster fog alpha or a material-name whitelist.
    float materialCoverage = saturate(surface.detail.w);
    // Alpha-blended/additive Fast3D geometry has no general opacity model in
    // this hybrid pass. Preserve its raster pixel; exact emissive materials
    // carry sampled alpha separately for visible emission, and secondary rays
    // accumulate their energy while continuing through the overlay.
    const bool genericMatte = surface.materialIndex == MATERIAL_TYPE_GENERIC_MATTE;
    if (surface.surfaceOptics.w > 2.5f) {
        materialCoverage = 0.0f;
    }
    // OCZ-LAVA-01: evaluate the magma surface once and reuse the result for
    // albedo, roughness, the shade normal and thermal emission. The authored
    // Fast3D texel is the artistic identity; the physical layer is added on
    // top of it, never instead of it.
    float3 lavaEmission = 0.0.xxx;
    if (hyperrealLava && debugMode == 0u) {
        const LavaSurfaceResult lava = LavaEvaluate(
            lavaProfile, lavaRecord, baseColor, materialCoverage,
            surface.detail.xy, 0.0f, waterTime, geometricNormal);
        if (any(!isfinite(lava.shadeNormal)) || dot(lava.shadeNormal, lava.shadeNormal) <= 1.0e-12f) {
            ClearSurfaceSignals(launchIndex);
            return;
        }
        normal = normalize(lava.shadeNormal);
        baseColor = lava.albedo;
        roughness = lava.roughness;
        metallic = 0.0f;
        // Melt is a rough dielectric with a faint sheen on the open surface.
        reflectivity = 0.08f;
        emissiveStrength = 0.0f;
        materialCoverage = saturate(materialCoverage * lava.coverage);
        // OCZ-LAVA-02: visual emission is the surface's own thermal glow and
        // is independently switchable from the lighting contribution, which
        // is what makes the A/B causality test possible. Bit 2 of
        // sourceControls.w gates only this additive term; albedo, normals
        // and roughness keep the magma identity either way.
        lavaEmission = ((gSourceControls.w & 2u) != 0u)
            ? lava.emission * materialCoverage : 0.0.xxx;
        // Keep the dielectric F0 consistent with the updated albedo so the
        // specular lobe on the open melt reads as a wet sludge, not as metal.
        dielectricF0 = 0.04f;
        f0 = dielectricF0.xxx;
        fresnel = f0 + (1.0f - f0) * fresnelPower;
    }
    const bool forceAnyHitCoverage = gateDiagnostic && (((uint)gDiagnosticInfo.x & 2u) != 0u);
    if (gSourceControls.y != 0u) {
        MaterialRecord receiverRecord = LoadMaterialRecord(surface.instanceIndex);
        const uint confidence = (surface.diagnosticFlags & DXR_PAYLOAD_FLAG_ALBEDO_RECONSTRUCTED) != 0u ? 1u : 0u;
        // Preserve every fairy's visible sprite and all unsupported/transparent surfaces.
        // Only a proven opaque/cutout material receives the isolated diagnostic light.
        if (confidence == 0u || receiverRecord.alphaMode >= 2u ||
            (receiverRecord.samplerFlags >> 16u) != 0u || materialCoverage <= 0.0f) {
            ClearSurfaceSignals(launchIndex);
            return;
        }
        float3 pointRadiance = 0.0f;
        float3 pointDiffuseRadiance = 0.0f;
        float3 pointSpecularRadiance = 0.0f;
        float distance = 0.0f, radius = 0.0f, attenuation = 0.0f, visibility = 0.0f, cosine = 0.0f;
        [loop] for (uint i=0u; i<min(32u,(uint)gQuality.w); ++i) {
            uint identity = (uint)gPointLightOptics[i].x;
            if (identity / 8u != gSourceControls.x || identity % 8u != 1u) continue;
            float3 offset = gPointLightPosition[i].xyz - viewPos;
            distance = length(offset); radius = gPointLightPosition[i].w;
            if (distance > 0.0001f && radius > 0.0f && distance < radius) {
                float3 direction = offset / distance;
                cosine = saturate(dot(normal, direction));
                attenuation = saturate(1.0f - distance * distance / (radius * radius));
                visibility = TraceMaterialVisibility(viewPos, geometricNormal, direction, distance - 0.025f);
                const float3 lightColor = max(gPointLightOptics[i].yzw, 0.0f.xxx);
                const float sourceVisibility = attenuation * visibility;
                pointDiffuseRadiance = EvaluateMaterialDiffuseLight(baseColor, normal, viewDirection,
                    direction, metallic, surface.detail.z) * lightColor * sourceVisibility;
                pointSpecularRadiance = EvaluateMaterialSpecularLight(baseColor, normal, viewDirection,
                    direction, roughness, metallic, surface.detail.z) * lightColor * sourceVisibility;
                pointRadiance = pointDiffuseRadiance + pointSpecularRadiance;
            }
        }
        float3 mesh = TraceSelectedEmitter(viewPos, geometricNormal, normal, baseColor);
        gSurfaceMask[launchIndex] = float4(0.0f,0.0f,0.0f,1.0f);
        gReflectionColor[launchIndex] = float4(pointRadiance, distance);
        gIndirectMask[launchIndex] = float4(mesh,1.0f);
        gEmissiveMask[launchIndex] = visibility;
        RecordNaviReceiver(launchIndex, surface, viewPos, pointRadiance, pointDiffuseRadiance,
                           pointSpecularRadiance, mesh, distance, radius,
                           attenuation, visibility, cosine, confidence);
        return;
    }
    const bool materialEligible = surface.surfaceOptics.w <= 2.5f;
    if (gateCountersEnabled && gateDiagnostic) {
        IncrementPrimaryGateCounter(PG_ELIGIBILITY_INPUT_VALID, launchIndex);
        if (materialEligible) IncrementPrimaryGateCounter(5u, launchIndex);
        else IncrementPrimaryGateCounter(PG_REJECT_BLEND, launchIndex);
    }
    if (gateCountersEnabled && gateDiagnostic && materialEligible && materialCoverage <= 0.0f)
        IncrementPrimaryGateCounter(PG_REJECT_COVERAGE, launchIndex);
    if (gateCountersEnabled && gateDiagnostic && materialCoverage > 0.0f)
        IncrementPrimaryGateCounter(PG_COVERAGE_NONZERO, launchIndex);
    const bool transmissiveMaterial = surface.materialIndex == MATERIAL_TYPE_WATER ||
                                      surface.materialIndex == MATERIAL_TYPE_GLASS;
    // Opaque reflection strength is applied by the compositor. Dielectric
    // strength belongs in the single Fresnel premix performed here.
    float reflectionStrength = transmissiveMaterial ? saturate(gTraceSettings.w) : 1.0f;
    float3 reflectionWeight = saturate(fresnel * reflectivity) * materialCoverage * reflectionStrength *
                              saturate(gQuality.y) * (genericMatte ? 0.0f : 1.0f);
    float transmissiveFactor = transmissiveMaterial ? materialCoverage : 0.0f;
    float reflectionContribution = Luminance(reflectionWeight);
    // OCZ-WATER-01: the compositor blends the traced water optics over the
    // raster water pixel with surface.r * gParams.x. Multiplying the class
    // opticsBlend here lets a special pool keep more of its authored look
    // while a lake or river takes the full physical layer, all without a
    // separate composite path.
    if (hyperrealWater) {
        reflectionContribution = saturate(materialCoverage * waterProfile.opticsBlend);
    }
    uint gateFlags = surface.diagnosticFlags | 1u | 2u | 4u | 8u;
    if (surface.surfaceOptics.w <= 2.5f) gateFlags |= 16u;
    if (forceAnyHitCoverage) {
        // Isolate geometry/hit from all material policy after structural guards.
        materialCoverage = 1.0f;
        baseColor = float3(1.0f, 0.0f, 0.0f);
        roughness = 0.5f;
        metallic = 0.0f;
        emissiveStrength = 0.0f;
        reflectionContribution = 0.0f;
        transmissiveFactor = 0.0f;
    }
    if (materialCoverage > 0.0f) gateFlags |= 32u;
    if (gateDiagnostic) {
        if (gateCountersEnabled && materialCoverage > 0.0f) IncrementPrimaryGateCounter(6u, launchIndex);
        if (gateCountersEnabled && gateDiagnostic) {
            const uint opticsBin = surface.surfaceOptics.w <= 1.0f ? 0u :
                surface.surfaceOptics.w <= 2.0f ? 1u : surface.surfaceOptics.w <= 3.0f ? 2u :
                surface.surfaceOptics.w <= 4.0f ? 3u : 4u;
            IncrementPrimaryGateCounter(PG_OPTICS_HIST_BASE + opticsBin, launchIndex);
            IncrementPrimaryGateCounter(PG_COVERAGE_HIST_BASE + PrimaryFloatBin(materialCoverage), launchIndex);
        }
        if (forceAnyHitCoverage) gateFlags |= 16u;
        const float coverage = saturate(materialCoverage);
        if (coverage > 0.0f) gateFlags |= 64u;
        gSurfaceMask[launchIndex] = float4(0.0f, 0.0f, 0.0f, coverage);
        if (gateCountersEnabled && coverage > 0.5f) IncrementPrimaryGateCounter(7u, launchIndex);
        gReflectionColor[launchIndex] = float4(float(gateFlags) / 255.0f, 0.0f, coverage, 1.0f);
        if (coverage > 0.0f) {
            gateFlags |= 64u;
            gReflectionColor[launchIndex].r = float(gateFlags) / 255.0f;
        }
        gEmissiveMask[launchIndex] = 0.0f;
        gIndirectMask[launchIndex] = 0.0f;
        return;
    }
    if (debugMode != 0u && debugMode < 10u) {
        surface.radiance.a = reflectionContribution;
        float3 debugColor = MaterialDebugColor(debugMode, surface);
        gSurfaceMask[launchIndex] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        gReflectionColor[launchIndex] = float4(debugColor, 1.0f);
        gEmissiveMask[launchIndex] = 0.0f;
        gIndirectMask[launchIndex] = 0.0f;
        return;
    }

    // Alpha-blended/additive hits retain their Fast3D pixels. Exact emissive
    // hits contribute through the separate emission signal; the generic matte
    // fallback remains diffuse-only and does not invent reflective surfaces.
    if (materialCoverage <= 0.0f) {
        ClearSurfaceSignals(launchIndex);
        return;
    }

    float3 reflectedRadiance = 0.0f;
    if (reflectionContribution > 0.001f) {
        float frameJitter = gParams.w > 0.5f ? gFrameInfo.x * 0.61803398875f : 0.0f;
        float seedX = frac(sin(dot(stableSampleSeed, float2(12.9898f, 78.233f))) * 43758.5453f + frameJitter);
        float seedY = frac(sin(dot(stableSampleSeed + float2(19.19f, 73.73f),
                                   float2(39.346f, 11.135f))) * 24634.6345f + frameJitter * 1.324717957f);
        float alpha = roughness * roughness;
        float3 tangent = normalize(abs(normal.y) < 0.9f ? cross(normal, float3(0.0f, 1.0f, 0.0f))
                                                       : cross(normal, float3(1.0f, 0.0f, 0.0f)));
        float3 bitangent = normalize(cross(normal, tangent));
        uint reflectionSamples = min(8u, max(1u, (uint)max(gQuality.x, 1.0f)));
        // Glossy surfaces converge quickly with few rays; broad highlights
        // need more independent directions to avoid a single noisy reflection.
        reflectionSamples = max(1u, (uint)ceil(roughness * reflectionSamples));
        [loop] for (uint sampleIndex = 0u; sampleIndex < reflectionSamples; ++sampleIndex) {
            float sampleOffset = float(sampleIndex);
            float sampleX = frac(seedX + sampleOffset * 0.61803398875f);
            float sampleY = frac(seedY + sampleOffset * 0.75487766625f);
            float phi = 6.2831853f * sampleX;
            float cosTheta = sqrt((1.0f - sampleY) /
                                  max(1.0f + (alpha * alpha - 1.0f) * sampleY, 1.0e-5f));
            float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
            float3 microfacetNormal = normalize(
                tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + normal * cosTheta);
            float3 reflectionDirection = normalize(reflect(primaryRay.Direction, microfacetNormal));
            // Water wave normals tilt far enough to reject valid grazing
            // reflections against the shading hemisphere; the true surface
            // horizon is the geometric normal, so water only tests that.
            const bool aboveSurface = hyperrealWater
                ? (dot(reflectionDirection, geometricNormal) > 1.0e-4f)
                : (dot(reflectionDirection, normal) > 1.0e-4f &&
                   dot(reflectionDirection, geometricNormal) > 1.0e-4f);
            if (aboveSurface) {
                RayDesc reflectionRay;
                reflectionRay.Origin = OffsetMaterialRayOrigin(viewPos, geometricNormal, reflectionDirection, 0.025f);
                reflectionRay.Direction = reflectionDirection;
                reflectionRay.TMin = 0.02f;
                reflectionRay.TMax = max(gTraceSettings.x, 1.0f);
                ReflectionPayload reflected;
                InitializeReflectionPayload(reflected, 0u);
                TraceRay(gScene, RAY_FLAG_NONE,
                         0x03, 0, 1, 0, reflectionRay, reflected);
                // The sampled GGX half-vector has PDF D(H)*N.H/(4*V.H).
                // Account for the BRDF/PDF ratio; a plain average changes
                // reflection brightness with roughness and view angle.
                float nDotV = max(saturate(dot(normal, viewDirection)), 1.0e-4f);
                float nDotL = saturate(dot(normal, reflectionDirection));
                float nDotH = max(saturate(dot(normal, microfacetNormal)), 1.0e-4f);
                float vDotH = saturate(dot(viewDirection, microfacetNormal));
                float geometry = MaterialGeometrySchlickGGX(nDotV, roughness) *
                                 MaterialGeometrySchlickGGX(nDotL, roughness);
                float3 sampleFresnel = MaterialFresnelSchlick(vDotH, f0);
                float3 relativeFresnel = sampleFresnel / max(fresnel, 1.0e-4f.xxx);
                float specularWeight = min(geometry * vDotH / (nDotV * nDotH), 4.0f);
                reflectedRadiance += reflected.radiance.rgb * relativeFresnel * specularWeight;
            }
        }
        reflectedRadiance /= reflectionSamples;
    }

    float3 transmittedRadiance = baseColor;
    float3 waterBodyColor = 0.0f.xxx;
    float waterPathDistance = 0.0f;
    bool waterRefracted = false;
    if (transmissiveMaterial) {
        float eta = surface.materialIndex == 4u ? (1.0f / 1.333f) : (1.0f / 1.45f);
        // The primary side is encoded as 2=front/entering, 1=back/exiting.
        // Snell's ratio reverses at the far interface of water or glass.
        if (surface.surfaceOptics.w < 1.5f) eta = rcp(eta);
        float3 transmittedDirection = refract(primaryRay.Direction, normal, eta);
        bool totalInternalReflection = dot(transmittedDirection, transmittedDirection) < 1e-4f;
        if (totalInternalReflection)
            transmittedDirection = reflect(primaryRay.Direction, normal);
        // Keep transmitted rays inside the geometric interface even when a
        // strongly perturbed shading normal points the lobe across its horizon.
        if (!totalInternalReflection && dot(transmittedDirection, geometricNormal) >= 0.0f) {
            transmittedDirection = refract(primaryRay.Direction, geometricNormal, eta);
            totalInternalReflection = dot(transmittedDirection, transmittedDirection) < 1e-4f;
            if (totalInternalReflection) transmittedDirection = reflect(primaryRay.Direction, geometricNormal);
        }
        if (totalInternalReflection && dot(transmittedDirection, geometricNormal) <= 0.0f)
            transmittedDirection = reflect(primaryRay.Direction, geometricNormal);
        if (totalInternalReflection && (reflectionStrength <= 0.0f || gQuality.y <= 0.0f)) {
            // No transmitted path exists. Preserve the raster interface when
            // its only valid optical contribution was explicitly disabled.
            ClearSurfaceSignals(launchIndex);
            return;
        }
        float transmittedLength = length(transmittedDirection);
        if (any(!isfinite(transmittedDirection)) || transmittedLength <= 1.0e-6f) {
            ClearSurfaceSignals(launchIndex);
            return;
        }
        float3 transmittedRayDirection = transmittedDirection / transmittedLength;
        RayDesc transmittedRay;
        transmittedRay.Origin = OffsetMaterialRayOrigin(viewPos, geometricNormal, transmittedRayDirection, 0.025f);
        transmittedRay.Direction = transmittedRayDirection;
        transmittedRay.TMin = 0.03f;
        transmittedRay.TMax = max(gTraceSettings.y, 1.0f);
        ReflectionPayload transmitted;
        InitializeReflectionPayload(transmitted, totalInternalReflection ? 0u : 3u);
        TraceRay(gScene, RAY_FLAG_NONE,
                 0x03, 0, 1, 0, transmittedRay, transmitted);
        transmittedRadiance = transmitted.radiance.rgb;
        if (hyperrealWater) {
            // OCZ-WATER-02: the refracted scene is the real underwater world.
            // Keep its luminance so the floor, walls and shadows stay readable,
            // but let Beer-Lambert absorption colour the path by depth and
            // converge the chroma to the original painted water art. The
            // refracted payload carries RayTCurrent() in surfaceOptics.z on a
            // hit; a miss uses the authored transmission range scaled down so
            // a thin sheet never inherits deep-water absorption.
            waterRefracted = !totalInternalReflection;
            const float refractedDistance = transmitted.surfaceOptics.z;
            const bool refractedHitSomething = totalInternalReflection
                ? true
                : (isfinite(refractedDistance) && refractedDistance > 0.0f);
            waterPathDistance = WaterOpticalPath(refractedDistance, refractedHitSomething,
                                                 transmittedRay.TMax);
            const float3 waterArt = max(SrgbToLinear(saturate(baseColor)), 0.0.xxx);
            if (!refractedHitSomething && !totalInternalReflection) {
                // The transmitted ray escaped into the sky or a void. Replace it
                // with a depth-tinted body colour so deep water never shows sky
                // through the surface, while shallow water keeps the art hue.
                transmittedRadiance = WaterDepthTint(waterPathDistance, baseColor,
                                                     waterProfile.artWeight);
            }
            transmittedRadiance = WaterMatchArtChroma(transmittedRadiance, waterArt,
                                                      waterProfile.artWeight);
            waterBodyColor = transmittedRadiance;
            transmittedRadiance *= WaterAbsorbTransmittance(waterPathDistance,
                                                            waterProfile.absorption);
        }
        // Radiance is premixed once with RGB Fresnel; the surface compositor
        // applies only the authored transmission strength to this result.
        // OCZ-WATER-01: water always reflects by Fresnel, so its interface
        // weight is the pure Fresnel term and is never gated by the authored
        // transmission strength or a quality toggle. This is what makes a
        // real liquid read as a liquid at every view angle, including the
        // near-total reflection at grazing incidence.
        float3 interfaceWeight = hyperrealWater
            ? fresnel
            : saturate(fresnel * reflectivity) * reflectionStrength * saturate(gQuality.y);
        if (totalInternalReflection) {
            reflectedRadiance = transmittedRadiance;
        } else {
            reflectedRadiance = transmittedRadiance * (1.0f - interfaceWeight) +
                                reflectedRadiance * interfaceWeight;
        }
    }

    float3 indirectRadiance = 0.0f;
    uint samples = min(8u, (uint)max(0.0f, gQuality.x));
    float3 giTangent = normalize(abs(normal.y) < 0.9f ? cross(normal, float3(0,1,0)) : cross(normal, float3(1,0,0)));
    float3 giBitangent = cross(normal, giTangent);
    float2 giSeed = stableSampleSeed;
    float giFrameJitter = gParams.w > 0.5f ? gFrameInfo.x * 0.61803398875f : 0.0f;
    float giRotation = frac(sin(dot(giSeed, float2(127.1f, 311.7f))) * 43758.5453f + giFrameJitter);
    float giRadialSeed = frac(sin(dot(giSeed + 17.0f, float2(269.5f, 183.3f))) * 24634.6345f +
                              giFrameJitter * 1.324717957f);
    const uint giDebugMode = (uint)max(gLightDir.w, 0.0f);
    const bool giProvenanceDebug = giDebugMode >= 14u;
    float3 giClassHit = 0.0f, giClassEnv = 0.0f, giClassProc = 0.0f;
    uint giCountHit = 0u, giCountEnv = 0u, giCountProc = 0u;
    for (uint sampleIndex=0u; sampleIndex<samples; ++sampleIndex) {
        float xi = frac(giRadialSeed + sampleIndex * 0.61803398875f);
        float angle = frac(giRotation + sampleIndex * 0.75487766625f) * 6.2831853f;
        float radial = sqrt(xi);
        float hemisphereZ = sqrt(max(0.0f, 1.0f - xi));
        RayDesc bounce;
        bounce.Direction = normalize(normal * hemisphereZ +
                                     (giTangent * cos(angle) + giBitangent * sin(angle)) * radial);
        if (dot(bounce.Direction, geometricNormal) <= 0.0f) continue;
        bounce.Origin = OffsetMaterialRayOrigin(viewPos, geometricNormal, bounce.Direction, 0.03f);
        bounce.TMin=0.025f; bounce.TMax=max(gTraceSettings.z, 1.0f);
        ReflectionPayload hit; InitializeReflectionPayload(hit,4u);
        TraceRay(gScene,RAY_FLAG_NONE,0x03,0,1,0,bounce,hit);
        indirectRadiance += max(0.0f, hit.radiance.rgb);
        if (giProvenanceDebug) {
            const float3 giSample = max(0.0f, hit.radiance.rgb);
            if (hit.surfaceOptics.w > 0.5f) { giClassHit += giSample; ++giCountHit; }
            else if (hit.surfaceOptics.w > -1.5f) { giClassEnv += giSample; ++giCountEnv; }
            else { giClassProc += giSample; ++giCountProc; }
        }
    }
    float3 diffuseReflectance = baseColor * (1.0f - metallic);
    float indirectScale = gParams.w > 0.5f ? 1.0f : 0.28f;
    float3 indirectContribution = samples > 0u
        ? (indirectRadiance / samples) * diffuseReflectance * (indirectScale * materialCoverage)
        : 0.0f;
    const float emissionCoverage = surface.materialIndex == MATERIAL_TYPE_EMISSIVE
        ? materialCoverage : 1.0f;
    // OCZ-WATER-01: native-light glints. A torch, a fairy or the sun must
    // leave a moving specular trail on the water instead of a flat luminous
    // disc. These are additive to the traced reflection and never replace it.
    if (hyperrealWater && debugMode == 0u) {
        const float3 waterF0 = WATER_F0.xxx;
        float3 waterGlints = 0.0f.xxx;
        const float3 waterLightDir = normalize(gLightDir.xyz);
        const float3 waterKeyColor = max(gSceneLightColor.rgb, 0.0.xxx);
        waterGlints += WaterDirectionalGlint(normal, viewDirection, geometricNormal,
                                             waterLightDir, waterKeyColor,
                                             waterProfile.roughness, waterF0);
        [loop] for (uint waterLightIndex = 0u;
             waterLightIndex < min((uint)max(gQuality.w, 0.0f), 32u); ++waterLightIndex) {
            const uint waterIdentity = (uint)gPointLightOptics[waterLightIndex].x;
            const uint waterRole = waterIdentity % 8u;
            if (waterRole == 2u || waterRole == 4u) continue;
            const float3 waterToLight = gPointLightPosition[waterLightIndex].xyz - viewPos;
            const float waterDistance = length(waterToLight);
            const float waterRadius = gPointLightPosition[waterLightIndex].w;
            if (!isfinite(waterDistance) || waterDistance <= 1.0e-4f ||
                !isfinite(waterRadius) || waterRadius <= 0.0f || waterDistance >= waterRadius) continue;
            const float3 waterLightDirection = waterToLight / waterDistance;
            const float3 waterLightColor = max(gPointLightOptics[waterLightIndex].yzw, 0.0.xxx);
            if (!any(waterLightColor > 0.0f)) continue;
            const float waterVisibility = TraceMaterialVisibility(
                viewPos, geometricNormal, waterLightDirection, waterDistance - 0.025f);
            if (waterVisibility <= 0.0f) continue;
            waterGlints += WaterPointLightSpecular(
                normal, viewDirection, geometricNormal, viewPos, waterProfile.roughness,
                waterF0, gPointLightPosition[waterLightIndex],
                float4(waterIdentity, waterLightColor * waterVisibility));
        }
        if (any(isfinite(waterGlints))) {
            reflectedRadiance += max(0.0f, waterGlints) * materialCoverage;
        }
    }
    // OCZ-LAVA-01: thermal emission is additive to the traced reflection.
    // Melt lights the scene through the same reflection buffer the water
    // Fresnel uses, so a lava pool throws warm radiance onto the surrounding
    // rock exactly as the original emissive lava did, but with a physically
    // shaped chroma instead of a flat orange tint.
    float3 reflectionResult = max(0.0f,
        reflectedRadiance + lavaEmission + baseColor * emissiveStrength * emissionCoverage);
    float3 pathResult = max(0.0f, surface.radiance.rgb) + indirectContribution;
    if (any(isnan(reflectionResult)) || any(isinf(reflectionResult)) ||
        any(isnan(pathResult)) || any(isinf(pathResult))) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    // gSurfaceMask.a is only a validity flag: that UAV is bound to an
    // R8G8B8A8_UNORM target, so any value above one saturates to one and can
    // never carry the indirect share. The share is published instead in
    // gIndirectMask.a as (2.0 + giFraction) for the relit class, which is a
    // float16 target; the compositor reads (alpha - 2.0) as that share, so an
    // "indirect off" control subtracts exactly the ambient/GI part of a relit
    // pixel while leaving its direct contribution untouched. The share is a
    // ratio, not an absolute radiance, so it stays representable in FP16 for
    // both very dim and very bright pixels.
    const float pathEnergy = Luminance(pathResult);
    const float giFraction = pathEnergy > 1.0e-6f
        ? saturate(Luminance(indirectContribution) / pathEnergy) : 0.0f;
    const bool hybridPrimaryRelight =
        (surface.diagnosticFlags & DXR_PAYLOAD_FLAG_HYBRID_RELIGHT) != 0u;
    float3 nativePointDirectRadiance = 0.0f;
    // Static G_LIGHTING-off draws retain their raster albedo but do not
    // evaluate dynamic point lights. Restrict this additive signal to those
    // proven receivers and Navi's identified main glow; relit/path-traced
    // receivers already evaluate their point lights in ClosestHit.
    if (debugMode == 0u && gParams.w <= 0.5f &&
        (surface.diagnosticFlags & DXR_PAYLOAD_FLAG_ALBEDO_RECONSTRUCTED) != 0u &&
        materialCoverage > 0.0f) {
        MaterialRecord receiverRecord = LoadMaterialRecord(surface.instanceIndex);
        const uint receiverOwner = receiverRecord.samplerFlags >> 16u;
        if ((receiverRecord.flags & MATERIAL_FLAG_NATIVE_LIGHT_RECEIVER) != 0u &&
            receiverRecord.alphaMode < 2u) {
            [loop] for (uint lightIndex = 0u; lightIndex < min((uint)max(gQuality.w, 0.0f), 32u); ++lightIndex) {
                const uint identity = (uint)gPointLightOptics[lightIndex].x;
                const uint lightOwner = identity / 8u;
                const uint role = identity % 8u;
                if (role == 2u || role == 4u) continue; // Inactive auxiliary sentinels
                if (lightOwner != 0u && lightOwner == receiverOwner) continue; // Prevent self-illumination
                const float3 lightColor = max(gPointLightOptics[lightIndex].yzw, 0.0f.xxx);
                const float radius = gPointLightPosition[lightIndex].w;
                if (!any(lightColor > 0.0f) || !isfinite(radius) || radius <= 0.0f) continue;
                const float3 toLight = gPointLightPosition[lightIndex].xyz - viewPos;
                const float distanceToLight = length(toLight);
                if (!isfinite(distanceToLight) || distanceToLight <= 1.0e-4f || distanceToLight >= radius) continue;
                const float3 lightDirection = toLight / distanceToLight;
                if (dot(normal, lightDirection) <= 0.0f) continue;
                const float attenuation = saturate(1.0f - (distanceToLight / radius) * (distanceToLight / radius));
                const float visibility = TraceMaterialVisibility(viewPos, geometricNormal, lightDirection,
                                                                 distanceToLight - 0.025f);
                const float3 linearLightColor = SrgbToLinear(lightColor);
                nativePointDirectRadiance += EvaluateMaterialDirectLight(surface.naviPointAlbedo.rgb, normal, viewDirection,
                    lightDirection, roughness, metallic, surface.detail.z) * linearLightColor * attenuation * visibility;
            }
            // OCZ-LAVA-02: evaluate direct magma illumination onto this physical receiver
            if ((gSourceControls.w & 1u) != 0u && (uint)gSourceControls.z > 0u &&
                surface.materialIndex != MATERIAL_TYPE_LAVA) {
                nativePointDirectRadiance += TraceLavaLighting(
                    viewPos, geometricNormal, normal, viewDirection,
                    surface.naviPointAlbedo.rgb, roughness, metallic, surface.detail.z);
            }
        }
    }
    gSurfaceMask[launchIndex] =
        float4(reflectionContribution, transmissiveFactor, materialCoverage, 1.0f);
    // FP16 has a finite maximum; reject overflow rather than storing infinity.
    if (any(reflectionResult > 65504.0f) || any(pathResult > 65504.0f) ||
        any(!isfinite(nativePointDirectRadiance)) || any(nativePointDirectRadiance > 65504.0f)) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    if (debugMode < 16u) {
        gReflectionColor[launchIndex] = float4(reflectionResult, reflectionContribution);
    }
    if (gParams.w > 0.5f || hybridPrimaryRelight) {
        gIndirectMask[launchIndex] = float4(pathResult, 2.0f + giFraction);
    } else {
        gIndirectMask[launchIndex] = float4(indirectContribution, samples > 0u ? 1.0f : 0.0f);
    }
    float effectiveEmissive = max(emissiveStrength, 0.0f);
    if (hyperrealLava) {
        effectiveEmissive = max(effectiveEmissive, Luminance(lavaEmission));
    }
    gEmissiveMask[launchIndex] = effectiveEmissive * materialCoverage;
    gNativePointDirect[launchIndex] = float4(nativePointDirectRadiance, 1.0f);
    if (debugMode >= 10u && debugMode <= 15u) {
        float4 lateDebug = float4(Luminance(max(surface.radiance.rgb, 0.0f)),
                                  Luminance(indirectContribution),
                                  Luminance(baseColor),
                                  giFraction);
        if (debugMode == 11u) {
            lateDebug = float4(baseColor, giFraction);
        } else if (debugMode == 12u) {
            lateDebug = float4(max(surface.radiance.rgb, 0.0f),
                               Luminance(indirectContribution));
        } else if (debugMode == 13u) {
            lateDebug = float4(float3(metallic, metallic, metallic),
                               Luminance(baseColor));
        } else if (debugMode == 14u) {
            const float invSamples = rcp((float)samples);
            lateDebug = float4(Luminance(giClassHit * invSamples),
                               Luminance((giClassEnv + giClassProc) * invSamples),
                               (float)(giCountHit + giCountEnv + giCountProc) * invSamples,
                               giFraction);
        } else if (debugMode == 15u) {
            const float invSamples = rcp((float)samples);
            lateDebug = float4(Luminance(giClassEnv * invSamples),
                               Luminance(giClassProc * invSamples),
                               (float)giCountEnv * invSamples,
                               (float)giCountProc * invSamples);
        }
        gReflectionColor[launchIndex] = lateDebug;
    }
}

[shader("miss")]
void MissReflection(inout ReflectionPayload payload) {
    if (payload.probeMode == 6u) { payload.radiance = 0.0f; return; }
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) {
        payload.diagnosticError = 1u;
    }
    if (payload.probeMode == 2u) {
        payload.surfaceOptics.w = 1.0f;
        return;
    }
    float3 direction = normalize(WorldRayDirection());
    float4 projectedDirection = mul(gProjection, float4(direction, 0.0f));
    if (projectedDirection.w > 1.0e-5f) {
        float2 ndc = projectedDirection.xy / projectedDirection.w;
        float clipXScale = gParams.z > 1.0e-4f ? gParams.z : 1.0f;
        float2 viewportUv = float2(ndc.x * clipXScale * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        float2 uv = ViewportToRasterUV(viewportUv);
        if (all(viewportUv >= 0.0f) && all(viewportUv <= 1.0f) && all(uv >= 0.0f) && all(uv <= 1.0f) &&
            gDepthTexture.Load(int3(min(uint2(uv * gScreenSize.xy), uint2(gScreenSize.xy) - 1u), 0)) >= 0.9999f) {
            uv = clamp(uv, gScreenSize.zw * 0.5f, 1.0f - gScreenSize.zw * 0.5f);
            float3 environmentColor = max(gRasterEnvironment.SampleLevel(gMaterialSampler, uv, 0.0f).rgb,
                                          0.0f.xxx);
            if (gFrameInfo.w < 0.5f) environmentColor = SrgbToLinear(environmentColor);
            payload.radiance = float4(payload.radiance.rgb + environmentColor,
                                      Luminance(payload.radiance.rgb + environmentColor));
            payload.surfaceOptics.w = payload.probeMode == 4u ? -1.0f : 0.0f;
            return;
        }
    }
    // Rays that leave the rasterized view use a procedural environment tied
    // to the current Fast3D lighting instead of a fixed blue sky. This keeps
    // off-screen reflections and GI responsive to room/time-of-day changes.
    // The Fast3D light constants captured from the scene are sRGB-encoded
    // values; ClosestHitReflection already proves that by evaluating
    // SrgbToLinear(fast3dShade) with fast3dShade built from these same
    // constants. A radiance model must receive linear values, so the
    // conversion happens here too. Skipping it made the virtual sky about
    // 3.2x brighter than the ambient it proxies, which dominated every
    // ground-bounce sample at night.
    float3 ambient = SrgbToLinear(max(gAmbientLightColor.rgb, 0.0f.xxx));
    float3 keyLight = SrgbToLinear(max(gSceneLightColor.rgb, 0.0f.xxx));
    float3 secondaryLight = SrgbToLinear(max(gSecondaryLightColor.rgb, 0.0f.xxx));
    float secondaryLightLength = length(gSecondaryLightDir.xyz);
    float3 secondaryDirection = secondaryLightLength > 1.0e-6f
        ? gSecondaryLightDir.xyz / secondaryLightLength : float3(0.0f, 1.0f, 0.0f);
    float daylight = saturate(Luminance(keyLight + secondaryLight));
    float3 worldEnvironmentDirection = normalize(mul((float3x3)gInvView, direction));
    float skyFactor = smoothstep(-0.12f, 0.92f, worldEnvironmentDirection.y);
    float3 horizonColor = ambient * 0.55f + float3(0.018f, 0.021f, 0.028f);
    float3 nightZenith = float3(0.006f, 0.010f, 0.026f);
    float3 dayZenith = float3(0.075f, 0.16f, 0.34f);
    float3 zenithColor = ambient * 0.22f + lerp(nightZenith, dayZenith, daylight);
    float3 skyColor = lerp(horizonColor, zenithColor, skyFactor);
    skyColor += keyLight * pow(saturate(dot(direction, normalize(gLightDir.xyz))), 20.0f) * 0.12f;
    skyColor += keyLight * pow(saturate(dot(direction, normalize(gLightDir.xyz))), 1024.0f) * 2.0f;
    skyColor += secondaryLight * pow(saturate(dot(direction, secondaryDirection)), 20.0f) * 0.12f;
    skyColor += secondaryLight * pow(saturate(dot(direction, secondaryDirection)), 1024.0f) * 2.0f;
    payload.radiance = float4(payload.radiance.rgb + skyColor,
                              Luminance(payload.radiance.rgb + skyColor));
    payload.surfaceOptics.w = payload.probeMode == 4u ? -2.0f : 0.0f;
}

[shader("anyhit")]
void AnyHitReflection(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // The primary diagnostic pass must reach ClosestHit so it can validate
    // every ID before reading the corresponding record/index/vertex buffers.
    // It intentionally bypasses alpha filtering for that one diagnostic ray.
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) return;
    uint validatedFlags;
    if (ValidatePrimaryHit(InstanceID(), GeometryIndex(), PrimitiveIndex(), validatedFlags) != 0u) {
        IgnoreHit();
        return;
    }
    MaterialRecord material = LoadMaterialRecord(InstanceID());
    if (payload.probeMode == 2u && NativePointRepresentsEmitter(material)) {
        IgnoreHit();
        return;
    }
    if (payload.probeMode == 6u && (material.samplerFlags >> 16u) == gSourceControls.x) {
        // The target's coverage/emission are decoded in ClosestHit, including Fast3D alpha.
        return;
    }
    // Direct-light visibility rays travel from the receiver toward the native
    // scene light. Treat only the authored Temple glass panes as portal
    // surfaces; the captured mesh boundaries still define the light opening.
    if (payload.probeMode == 2u && (material.flags & MATERIAL_FLAG_DXR_WINDOW_PORTAL) != 0u) {
        IgnoreHit();
        return;
    }
    if ((material.flags & MATERIAL_FLAG_DECORATIVE_BEAM) != 0u) {
        IgnoreHit();
        return;
    }
    const bool pbrTransmissiveMaterial = material.materialType == MATERIAL_TYPE_WATER ||
                                         material.materialType == MATERIAL_TYPE_GLASS;
    const bool pbrSurfaceRay = payload.probeMode == 0u || payload.probeMode == 1u ||
                               payload.probeMode == 4u || payload.probeMode == 5u;
    const bool emissiveAlphaBlend = material.materialType == MATERIAL_TYPE_EMISSIVE &&
        material.emissiveStrength > 0.0f && (material.alphaMode == 2u || material.alphaMode == 3u);
    // Non-solid translucent god rays / light beam overlays (e.g. Tex_0035C0) must not occlude shadow rays.
    if (payload.probeMode == 2u && emissiveAlphaBlend) {
        IgnoreHit();
        return;
    }
    const bool alphaBlendedMaterial = material.alphaMode == 2u || material.alphaMode == 3u;
    const bool decodeTransmissiveAlpha = pbrSurfaceRay && pbrTransmissiveMaterial && alphaBlendedMaterial;
    if ((payload.probeMode != 1u && pbrSurfaceRay && emissiveAlphaBlend) ||
        decodeTransmissiveAlpha) {
        MaterialRecord sampledMaterial;
        float2 uv;
        float3 normal;
        float3 tangent, bitangent;
        float3 geometricNormal;
        float4 vertexColor;
        float4 baseColor;
        float vertexAlpha;
        float sampledTextureAlpha;
        float textureLod;
        bool frontFace;
        float3 unusedNaviPointAlbedo;
        DecodeMaterialHit(InstanceID(), PrimitiveIndex(), payload.probeMode, attribs, sampledMaterial,
                          uv, normal, vertexColor, baseColor, unusedNaviPointAlbedo, vertexAlpha, sampledTextureAlpha, textureLod,
                          tangent, bitangent, geometricNormal, frontFace);
        const float emitterCoverage = saturate(baseColor.a);
        if (emitterCoverage <= 1.0e-4f) {
            IgnoreHit();
            return;
        }
        if (payload.probeMode != 1u && emissiveAlphaBlend) {
            float emitterWeight = 1.0f;
            if (payload.probeMode != 0u && NativePointRepresentsEmitter(sampledMaterial)) {
                emitterWeight = 0.0f;
            }
            payload.radiance.rgb += emitterWeight * max(baseColor.rgb, 0.0f.xxx) *
                                    max(sampledMaterial.emissiveStrength, 0.0f) * emitterCoverage;
            payload.radiance.a = Luminance(payload.radiance.rgb);
            // Beam overlays are additive radiance, not opaque blockers. Preserve
            // their energy in the payload and continue to the actual hit or sky.
            IgnoreHit();
            return;
        }
    }
    if (material.alphaMode == 1u) {
        MaterialRecord sampledMaterial;
        float2 uv;
        float3 normal;
        float3 tangent, bitangent;
        float3 geometricNormal;
        float4 vertexColor;
        float4 baseColor;
        float vertexAlpha;
        float sampledTextureAlpha;
        float textureLod;
        bool frontFace;
        float3 unusedNaviPointAlbedo;
        DecodeMaterialHit(InstanceID(), PrimitiveIndex(), payload.probeMode, attribs, sampledMaterial,
                          uv, normal, vertexColor, baseColor, unusedNaviPointAlbedo, vertexAlpha, sampledTextureAlpha, textureLod,
                          tangent, bitangent, geometricNormal, frontFace);
        float alpha = baseColor.a;
        if (alpha < 0.5f) IgnoreHit();
    } else if (alphaBlendedMaterial && emissiveAlphaBlend == false &&
               (!pbrTransmissiveMaterial || !pbrSurfaceRay)) {
        // Transparent/additive geometry remains visible in the original
        // Fast3D pass. Ignore it in every ray type so it cannot win the
        // camera-to-raster-depth hit and zero the opaque receiver's DXR mask.
        // Water and glass retain their PBR surface response for reflection and
        // indirect rays even when Fast3D renders them with alpha blending.
        IgnoreHit();
    }
}

[shader("closesthit")]
void ClosestHitReflection(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.diagnosticFlags |= 2u;
    uint validatedFlags = 0u;
    const uint validationError = ValidatePrimaryHit(InstanceID(), GeometryIndex(), PrimitiveIndex(), validatedFlags);
    payload.diagnosticFlags |= validatedFlags;
    if (validationError != 0u) {
        payload.diagnosticError = validationError;
        payload.materialIndex = 20u;
        payload.surfaceOptics.w = 0.0f;
        return;
    }
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) {
        MaterialRecord hitMaterial = LoadMaterialRecord(InstanceID());
        const bool gateCountersEnabled = (((uint)gDiagnosticInfo.x & 4u) != 0u);
        if (gateCountersEnabled) {
            IncrementPrimaryGateCounter(PG_RECORD_HIST_BASE + min(InstanceID(), 256u), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_ALPHA_MODE_HIST_BASE + min(hitMaterial.alphaMode, 4u), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_TYPE_HIST_BASE + min(hitMaterial.materialType, 21u), DispatchRaysIndex().xy);
            // Keep the legacy texture/alpha-policy histogram independent of
            // the new raster-depth-write bit.
            IncrementPrimaryGateCounter(PG_FLAGS_HIST_BASE + (hitMaterial.flags & 15u), DispatchRaysIndex().xy);
            const bool textureFlag = (hitMaterial.flags & 1u) != 0u;
            const bool textureIndexValid = hitMaterial.textureIndex < (uint)gDiagnosticInfo.z;
            const uint textureState = textureFlag ? (textureIndexValid ? 1u : 2u) :
                (hitMaterial.textureIndex != 0u ? 3u : 0u);
            IncrementPrimaryGateCounter(PG_TEXTURE_HIST_BASE + textureState, DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_ROUGHNESS_HIST_BASE + PrimaryFloatBin(hitMaterial.roughness), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_METALLIC_HIST_BASE + PrimaryFloatBin(hitMaterial.metallic), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_REFLECTIVITY_HIST_BASE + PrimaryFloatBin(hitMaterial.reflectivity), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_SPECULAR_HIST_BASE + PrimaryFloatBin(hitMaterial.specular), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_RECORD_ALPHA_HIST_BASE + PrimaryFloatBin(hitMaterial.baseColor.a), DispatchRaysIndex().xy);
            const bool candidateAlphaPolicy = (((uint)gDiagnosticInfo.x & 128u) != 0u);
            const uint gateAlphaMode = candidateAlphaPolicy ? ((hitMaterial.flags >> 2u) & 3u) : hitMaterial.alphaMode;
            const bool alphaBlendCandidate = gateAlphaMode == 2u || gateAlphaMode == 3u;
            const bool transmissiveException = hitMaterial.materialType == MATERIAL_TYPE_WATER ||
                                               hitMaterial.materialType == MATERIAL_TYPE_GLASS;
            const bool emissiveException = hitMaterial.materialType == MATERIAL_TYPE_EMISSIVE &&
                                           hitMaterial.emissiveStrength > 0.0f;
            if (alphaBlendCandidate) IncrementPrimaryGateCounter(PG_ALPHA_BLEND_CANDIDATE, DispatchRaysIndex().xy);
            if (alphaBlendCandidate && transmissiveException)
                IncrementPrimaryGateCounter(PG_TRANSMISSIVE_EXCEPTION, DispatchRaysIndex().xy);
            if (payload.probeMode == 1u && alphaBlendCandidate &&
                !transmissiveException && !emissiveException)
                IncrementPrimaryGateCounter(PG_RAW_PRIMARY_RASTER_BLEND, DispatchRaysIndex().xy);
        }
        const bool forcedGateMode = (((uint)gDiagnosticInfo.x & 2u) != 0u);
        if (forcedGateMode) {
            // Geometry/material-buffer indices have already passed validation.
            // Use fixed diagnostic values from here on: forced coverage must
            // not sample art textures or depend on material-family policy.
            payload.materialIndex = MATERIAL_TYPE_GENERIC_MATTE;
            payload.instanceIndex = InstanceID();
            payload.textureIndex = 0u;
            payload.normalRoughness = float4(0.0f, 1.0f, 0.0f, 0.5f);
            payload.baseColorMetallic = float4(1.0f, 1.0f, 1.0f, 0.0f);
            payload.surfaceOptics = float4(0.0f, 0.0f, RayTCurrent(), 1.0f);
            payload.detail = float4(0.0f, 0.0f, 0.0f, 1.0f);
            payload.radiance = 0.0f;
            return;
        }
    }
    const float3 accumulatedTransparentEmission = max(payload.radiance.rgb, 0.0f.xxx);
    MaterialRecord material;
    float2 uv;
    float3 normal;
    float3 tangent, bitangent;
    float3 geometricNormal;
    float4 vertexColor;
    float4 baseColor;
    float vertexAlpha;
    float sampledTextureAlpha;
    float textureLod;
    bool frontFace;
    float3 naviPointAlbedo;
    DecodeMaterialHit(InstanceID(), PrimitiveIndex(), payload.probeMode, attribs, material,
                      uv, normal, vertexColor, baseColor, naviPointAlbedo, vertexAlpha, sampledTextureAlpha, textureLod,
                      tangent, bitangent, geometricNormal, frontFace);
    if (payload.probeMode == 6u) {
        // OCZ-LAVA-02: an exact lava emitter sample returns the thermal
        // radiance of that triangle. Occluders and misses keep zero, so the
        // receiver-side TraceLavaLighting NEE is occlusion-aware through the
        // real TLAS. The sample identity was authored CPU-side from the very
        // triangle this ray was aimed at, so InstanceID/PrimitiveID must both
        // match; any other hit is a blocker, not a source.
        if (material.materialType == MATERIAL_TYPE_LAVA &&
            InstanceID() == payload.instanceIndex && PrimitiveIndex() == payload.textureIndex) {
            const LavaProfile probeLavaProfile =
                LavaProfileFromRecord(material.waterOptics, material.roughness);
            if (LavaProfileActive(probeLavaProfile)) {
                const float probeLavaTime = gFrameInfo.x * 0.016f;
                const LavaSurfaceResult probeLava = LavaEvaluate(
                    probeLavaProfile, material, max(baseColor.rgb, 0.0.xxx), saturate(baseColor.a),
                    uv, textureLod, probeLavaTime, geometricNormal);
                const float3 probeEmission = any(isfinite(probeLava.emission))
                    ? probeLava.emission : 0.0.xxx;
                payload.radiance = float4(probeEmission * saturate(probeLava.coverage), 0.0f);
                return;
            }
            payload.radiance = 0.0f;
            return;
        }
        bool selected = (material.samplerFlags >> 16u) == gSourceControls.x &&
                        InstanceID() == payload.instanceIndex && PrimitiveIndex() == payload.textureIndex &&
                        material.emissiveStrength > 0.0f;
        payload.radiance = float4(selected ? max(baseColor.rgb,0.0f) *
            material.emissiveStrength * saturate(baseColor.a) : 0.0f.xxx, 0.0f);
        return;
    }
    if (any(!isfinite(normal)) || any(!isfinite(geometricNormal)) ||
        dot(normal, normal) <= 1.0e-6f || dot(geometricNormal, geometricNormal) <= 1.0e-6f) {
        payload.diagnosticError = 5u;
        payload.radiance = 0.0f;
        payload.surfaceOptics = 0.0f;
        payload.geometricNormal = 0.0f;
        return;
    }
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u) &&
        (((uint)gDiagnosticInfo.x & 4u) != 0u)) {
        const uint2 launchIndex = DispatchRaysIndex().xy;
        IncrementPrimaryGateCounter(PG_VERTEX_ALPHA_HIST_BASE + PrimaryFloatBin(vertexAlpha), launchIndex);
        IncrementPrimaryGateCounter(PG_SAMPLED_ALPHA_HIST_BASE + PrimaryFloatBin(baseColor.a), launchIndex);
        if (vertexAlpha <= 0.0f) IncrementPrimaryGateCounter(PG_VERTEX_ALPHA_ZERO, launchIndex);
        if (sampledTextureAlpha <= 0.0f) IncrementPrimaryGateCounter(PG_TEXTURE_ALPHA_ZERO, launchIndex);
        if (material.baseColor.a <= 0.0f) IncrementPrimaryGateCounter(PG_RECORD_ALPHA_ZERO, launchIndex);
        if (baseColor.a <= 0.0f) IncrementPrimaryGateCounter(PG_COMBINED_ALPHA_ZERO, launchIndex);
        IncrementPrimaryGateCounter(PG_VERTEX_ALPHA_4BIN_BASE + PrimaryAlpha4Bin(vertexAlpha), launchIndex);
        IncrementPrimaryGateCounter(PG_TEXTURE_ALPHA_4BIN_BASE + PrimaryAlpha4Bin(sampledTextureAlpha), launchIndex);
        IncrementPrimaryGateCounter(PG_RECORD_ALPHA_4BIN_BASE + PrimaryAlpha4Bin(material.baseColor.a), launchIndex);
        IncrementPrimaryGateCounter(PG_COMBINED_ALPHA_4BIN_BASE + PrimaryAlpha4Bin(baseColor.a), launchIndex);
        const uint recordAlphaBase = PG_PER_RECORD_ALPHA_HIST_BASE + min(InstanceID(), 256u) * 6u;
        const uint textureAlphaBin = sampledTextureAlpha <= 0.0f ? 0u : sampledTextureAlpha >= 1.0f ? 2u : 1u;
        const uint combinedAlphaBin = baseColor.a <= 0.0f ? 0u : baseColor.a >= 1.0f ? 2u : 1u;
        IncrementPrimaryGateCounter(recordAlphaBase + textureAlphaBin, launchIndex);
        IncrementPrimaryGateCounter(recordAlphaBase + 3u + combinedAlphaBin, launchIndex);
    }
    normal = ApplyAlbedoRelief(material, uv, textureLod, normal, tangent, bitangent);
    if (any(!isfinite(normal)) || dot(normal, normal) <= 1.0e-6f) {
        payload.diagnosticError = 5u;
        payload.radiance = 0.0f;
        payload.surfaceOptics = 0.0f;
        return;
    }
    float wetness = saturate(material.wetness);
    float roughness = clamp(material.roughness, 0.04f, 1.0f);
    roughness = lerp(roughness, min(roughness, 0.08f), wetness);
    float reflectivity = lerp(saturate(material.reflectivity),
                              max(saturate(material.reflectivity), 0.45f), wetness * 0.65f);
    payload.materialIndex = material.materialType;
    payload.instanceIndex = InstanceID();
    payload.textureIndex = material.textureIndex;
    if ((material.flags & 16u) != 0u) payload.diagnosticFlags |= 128u; // raster draw writes depth
    payload.normalRoughness = float4(normal, roughness);
    payload.geometricNormal = float4(geometricNormal, float(PrimitiveIndex()));
    float4 reconstructed;
    if (EvaluateFast3DCombiner(material, 1.0f.xxxx, 1.0f.xxxx,
        DecodePackedMaterialColor(material.primitiveColor), 1.0f.xxxx,
        DecodePackedMaterialColor(material.environmentColor), reconstructed) ||
        any(naviPointAlbedo > 0.001f))
        payload.diagnosticFlags |= DXR_PAYLOAD_FLAG_ALBEDO_RECONSTRUCTED;
    if (payload.probeMode != 1u && payload.probeMode != 0u && NativePointRepresentsEmitter(material))
        material.emissiveStrength = 0.0f;
    payload.baseColorMetallic = float4(baseColor.rgb, material.metallic);
    payload.naviPointAlbedo = float4(naviPointAlbedo, 1.0f);
    const bool pbrTransmissiveMaterial = material.materialType == MATERIAL_TYPE_WATER ||
                                         material.materialType == MATERIAL_TYPE_GLASS;
    const bool bypassPrimaryRasterBlend = payload.probeMode == 1u &&
        (((uint)gDiagnosticInfo.x & 8u) != 0u);
    const uint candidateAlphaMode = (material.flags >> 2u) & 3u;
    const bool useCandidateAlphaPolicy = payload.probeMode == 1u &&
        (((uint)gDiagnosticInfo.x & 128u) != 0u);
    const uint policyAlphaMode = useCandidateAlphaPolicy ? candidateAlphaMode : material.alphaMode;
    const bool emissiveAlphaBlend = material.materialType == MATERIAL_TYPE_EMISSIVE &&
        material.emissiveStrength > 0.0f && (policyAlphaMode == 2u || policyAlphaMode == 3u);
    const bool isDecorativeBeam = (material.flags & MATERIAL_FLAG_DECORATIVE_BEAM) != 0u;
    const bool rawPrimaryRasterBlend = payload.probeMode == 1u &&
                                    (policyAlphaMode == 2u || policyAlphaMode == 3u) &&
                                    !pbrTransmissiveMaterial && !emissiveAlphaBlend;
    const bool primaryRasterBlend = (rawPrimaryRasterBlend || isDecorativeBeam) && !bypassPrimaryRasterBlend;
    payload.surfaceOptics = float4(reflectivity, material.emissiveStrength, RayTCurrent(),
                                   primaryRasterBlend ? 3.0f : (frontFace ? 2.0f : 1.0f));
    float coverage = 0.0f;
    const bool opacityValid = !isnan(baseColor.a) && !isinf(baseColor.a);
    if (isDecorativeBeam) coverage = 0.0f;
    else if (opacityValid && policyAlphaMode == 0u) coverage = 1.0f; // opaque alpha is not opacity
    else if (opacityValid && policyAlphaMode == 1u) coverage = baseColor.a >= 0.5f ? 1.0f : 0.0f;
    else if (opacityValid && emissiveAlphaBlend) coverage = saturate(baseColor.a);
    else if (opacityValid && pbrTransmissiveMaterial) coverage = saturate(baseColor.a);
    payload.detail = float4(uv, material.specular, coverage);
    if (gSourceControls.y != 0u && payload.probeMode == 1u) {
        payload.radiance = float4(baseColor.rgb,0.0f);
        return;
    }

    const bool hybridPrimaryRelight = payload.probeMode == 1u && gParams.w <= 0.5f &&
        (material.flags & MATERIAL_FLAG_DXR_PRIMARY_RELIGHT) != 0u &&
        (((uint)gDiagnosticInfo.x & 1u) == 0u);
    if (hybridPrimaryRelight) payload.diagnosticFlags |= DXR_PAYLOAD_FLAG_HYBRID_RELIGHT;
    if (payload.probeMode == 1u && gParams.w <= 0.5f && !hybridPrimaryRelight) {
        payload.radiance = float4(baseColor.rgb, Luminance(baseColor.rgb));
        if ((uint)max(gLightDir.w, 0.0f) >= 16u) {
            gReflectionColor[DispatchRaysIndex().xy] = 0.0f;
        }
        return;
    }

    float3 lightDirection = normalize(gLightDir.xyz);
    float3 viewDirection = normalize(-WorldRayDirection());
    float3 hitPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float directionalVisibility = 1.0f;
    if (dot(normal, lightDirection) > 0.0f) {
        bool usedRasterSoftShadow = false;
        if (payload.probeMode == 1u && !hybridPrimaryRelight && gSceneLightColor.w > 0.5f) {
            float4 rasterClip = mul(gProjection, float4(hitPosition, 1.0f));
            if (rasterClip.w > 1.0e-5f) {
                float2 rasterNdc = rasterClip.xy / rasterClip.w;
                float2 rasterUv = ViewportToRasterUV(float2(rasterNdc.x * gParams.z * 0.5f + 0.5f,
                                                           0.5f - rasterNdc.y * 0.5f));
                if (all(rasterUv >= 0.0f) && all(rasterUv <= 1.0f)) {
                    uint2 shadowSize = max(uint2(gFrameInfo.yz), uint2(1u, 1u));
                    uint2 shadowPixel = min(uint2(rasterUv * float2(shadowSize)), shadowSize - 1u);
                    directionalVisibility = saturate(gRasterSoftShadow.Load(int3(shadowPixel, 0)));
                    usedRasterSoftShadow = true;
                }
            }
        }
        if (!usedRasterSoftShadow) {
            directionalVisibility = TraceMaterialVisibility(hitPosition, geometricNormal, lightDirection,
                                                            max(gParams.y, 10000.0f));
        }
    }
    const float directionalNdotL = saturate(dot(normal, lightDirection));
    const float3 linearSceneLightColor = SrgbToLinear(max(gSceneLightColor.rgb, 0.0f.xxx));
    float3 specularDirect = EvaluateMaterialSpecularLight(baseColor.rgb, normal, viewDirection,
                                                          lightDirection, roughness,
                                                          material.metallic, material.specular) *
                            linearSceneLightColor * directionalVisibility;
    float3 fast3dShade = max(gAmbientLightColor.rgb, 0.025f.xxx) +
                         max(gSceneLightColor.rgb, 0.0f.xxx) * (directionalNdotL * directionalVisibility);
    float3 pointLights = 0.0f;
    float secondaryLightLength = length(gSecondaryLightDir.xyz);
    float3 secondaryLightDirection = secondaryLightLength > 1.0e-6f
        ? gSecondaryLightDir.xyz / secondaryLightLength : float3(0.0f, 1.0f, 0.0f);
    float3 secondaryLightColor = max(gSecondaryLightColor.rgb, 0.0f.xxx);
    const float3 linearSecondaryLightColor = SrgbToLinear(secondaryLightColor);
    float secondaryVisibility = 1.0f;
    float subsurface = saturate(material.subsurfaceHint);
    float secondaryNdotL = dot(normal, secondaryLightDirection);
    bool secondaryContributes = secondaryNdotL > 0.0f ||
                               (subsurface > 0.0f && dot(-normal, secondaryLightDirection) > 0.0f);
    if (max(secondaryLightColor.r, max(secondaryLightColor.g, secondaryLightColor.b)) > 1.0e-4f &&
        secondaryContributes) {
        secondaryVisibility = TraceMaterialVisibility(hitPosition, geometricNormal, secondaryLightDirection,
                                                     max(gParams.y, 10000.0f));
        if (secondaryNdotL > 0.0f) {
            specularDirect += EvaluateMaterialSpecularLight(baseColor.rgb, normal, viewDirection,
                                                            secondaryLightDirection, roughness,
                                                            material.metallic, material.specular) *
                              linearSecondaryLightColor * secondaryVisibility;
            fast3dShade += secondaryLightColor * (saturate(secondaryNdotL) * secondaryVisibility);
        }
    }
    // The first GI hit keeps complete point-light shading. At the final
    // diffuse bounce, omit point-light shadow rays to bound worst-case work;
    // ambient and the shadowed directional light still contribute there.
    uint pointLightCount = payload.probeMode == 5u
        ? 0u : min((uint)max(gQuality.w, 0.0f), 32u);
    const bool splitPointFromRelight = payload.probeMode == 1u && gParams.w <= 0.5f &&
        (material.flags & MATERIAL_FLAG_NATIVE_LIGHT_RECEIVER) != 0u;
    if (!splitPointFromRelight) {
        [loop] for (uint lightIndex = 0u; lightIndex < pointLightCount; ++lightIndex) {
            const uint identity = (uint)gPointLightOptics[lightIndex].x;
            const uint role = identity % 8u;
            if (role == 2u || role == 4u) continue;
            pointLights += EvaluateMaterialPointLight(baseColor.rgb, normal, geometricNormal, viewDirection, hitPosition,
                                                 gPointLightPosition[lightIndex], gPointLightOptics[lightIndex],
                                                 roughness, material.metallic, material.specular);
        }
    }
    float directionalBackscatter = pow(saturate(dot(-normal, lightDirection)), 2.0f);
    fast3dShade += max(gSceneLightColor.rgb, 0.0f.xxx) *
                   (directionalBackscatter * subsurface * 0.18f * directionalVisibility);
    float secondaryBackscatter = pow(saturate(dot(-normal, secondaryLightDirection)), 2.0f);
    fast3dShade += secondaryLightColor *
                   (secondaryBackscatter * subsurface * 0.18f * secondaryVisibility);
    float3 backscatter = 0.0f;
    if (!splitPointFromRelight) {
        [loop] for (uint lightIndex = 0u; lightIndex < pointLightCount; ++lightIndex) {
            const uint identity = (uint)gPointLightOptics[lightIndex].x;
            const uint role = identity % 8u;
            if (role == 2u || role == 4u) continue;
            backscatter += EvaluateMaterialPointBackscatter(baseColor.rgb, normal, geometricNormal, hitPosition,
                                                             gPointLightPosition[lightIndex],
                                                             gPointLightOptics[lightIndex],
                                                             subsurface * 0.18f);
        }
    }
    // P1: Energy Conservation between Specular Fresnel and Diffuse.
    // Transmitted diffuse light must equal incident energy minus reflected specular energy (1 - ks).
    float3 keyHalfVector = normalize(viewDirection + lightDirection);
    float dielectricF0 = (material.specular > 0.0f) ? (0.04f * saturate(material.specular)) : 0.04f;
    float3 f0 = lerp(dielectricF0.xxx, baseColor.rgb, saturate(material.metallic));
    float3 keyFresnel = MaterialFresnelSchlick(saturate(dot(viewDirection, keyHalfVector)), f0);
    float3 diffuseEnergyFactor = (1.0f - saturate(material.metallic)) * saturate(1.0f.xxx - keyFresnel);
    float3 diffuseFast3D = baseColor.rgb * diffuseEnergyFactor * SrgbToLinear(fast3dShade);
    // OCZ-LAVA-01: secondary rays (GI and reflections) must see the melt's
    // thermal radiance, or a lava pool would never light the rock around it.
    // The same evaluation the primary path uses, driven by the authored texel
    // this hit already sampled. accumulatedTransparentEmission is const, so
    // the thermal term rides in its own accumulator.
    float3 lavaSecondaryEmission = 0.0.xxx;
    if (material.materialType == MATERIAL_TYPE_LAVA && payload.probeMode != 6u) {
        const LavaProfile secondaryLavaProfile =
            LavaProfileFromRecord(material.waterOptics, material.roughness);
        if (LavaProfileActive(secondaryLavaProfile)) {
            const float lavaTime = gFrameInfo.x * 0.016f;
            const LavaSurfaceResult secondaryLava = LavaEvaluate(
                secondaryLavaProfile, material, baseColor.rgb, 1.0f, uv, textureLod,
                lavaTime, geometricNormal);
            if (any(isfinite(secondaryLava.emission))) {
                // Replace the diffuse term with the crusted albedo so a cooled
                // plate does not also bounce full ambient as if it were lit
                // rock, and add the thermal emission unattenuated: melt is its
                // own light source, not a lit receiver. OCZ-LAVA-02 gates the
                // additive emission with the visual-emission control so the
                // A/B causality pair can keep the material but drop the glow.
                diffuseFast3D = secondaryLava.albedo * (1.0f - saturate(material.metallic)) *
                                SrgbToLinear(fast3dShade);
                normal = secondaryLava.shadeNormal;
                roughness = secondaryLava.roughness;
                if ((gSourceControls.w & 2u) != 0u) {
                    lavaSecondaryEmission = secondaryLava.emission;
                }
            }
        }
    }
    // OCZ-LAVA-02: direct lighting FROM material-derived magma samples ONTO
    // this receiver. Primary path pixels and reflection rays evaluate it so
    // walls, floors, props and Link gain radiance that depends on emitter
    // heat, distance, orientation and TLAS occlusion. GI bounce rays are
    // excluded: they already accumulate lava radiance by hitting melt
    // geometry, and NEE there would double-count the same energy.
    float3 lavaDirectLighting = 0.0.xxx;
    if ((gSourceControls.w & 1u) != 0u && (uint)gSourceControls.z > 0u &&
        (payload.probeMode == 0u || payload.probeMode == 1u)) {
        lavaDirectLighting = TraceLavaLighting(hitPosition, geometricNormal, normal,
                                               viewDirection, baseColor.rgb, roughness,
                                               material.metallic, material.specular);
    }
    float3 radiance = accumulatedTransparentEmission + lavaSecondaryEmission + diffuseFast3D +
                      specularDirect + pointLights + lavaDirectLighting + backscatter +
                      baseColor.rgb * max(material.emissiveStrength, 0.0f);
    const uint dbgMode = (uint)max(gLightDir.w, 0.0f);
    if (payload.probeMode == 1u && dbgMode >= 16u) {
        float4 decomposed;
        if (dbgMode == 18u) {
            decomposed = float4(pointLights, Luminance(pointLights) > 1.0e-5f ? 1.0f : 0.0f);
        } else if (dbgMode == 16u) {
            decomposed = float4(Luminance(diffuseFast3D), Luminance(specularDirect),
                                Luminance(pointLights),
                                Luminance(backscatter + accumulatedTransparentEmission +
                                          baseColor.rgb * max(material.emissiveStrength, 0.0f)));
        } else {
            decomposed = float4(Luminance(fast3dShade), directionalVisibility,
                                Luminance(max(gAmbientLightColor.rgb, 0.0f.xxx)),
                                Luminance(max(gSceneLightColor.rgb, 0.0f.xxx)));
        }
        gReflectionColor[DispatchRaysIndex().xy] = decomposed;
    }
    if (payload.probeMode == 1u) {
        if (dbgMode == 18u) {
            payload.radiance = float4(pointLights, Luminance(pointLights));
            return;
        }
        // Experimental path-lighting mode uses the visible primary hit as
        // the replacement radiance for the raster material pixel.
        payload.radiance = float4(radiance, Luminance(radiance));
        return;
    }
    if (payload.probeMode == 4u) {
        // Add one diffuse bounce only to the first GI hit. Reflection rays
        // and the second GI hit stay shallow, bounding recursion cost.
        float4 bounceWorldH = mul(gInvView, float4(hitPosition, 1.0f));
        float3 bounceWorldPosition = abs(bounceWorldH.w) > 1.0e-6f
            ? bounceWorldH.xyz / bounceWorldH.w : bounceWorldH.xyz;
        float2 bounceSeed = float2(
            bounceWorldPosition.x + bounceWorldPosition.z * 17.0f + float(InstanceID()) * 0.37f,
            bounceWorldPosition.y + bounceWorldPosition.z * 7.0f + float(InstanceID()) * 0.73f);
        float sampleX = frac(sin(dot(bounceSeed, float2(127.1f, 311.7f))) * 43758.5453f);
        float sampleY = frac(sin(dot(bounceSeed + 19.19f, float2(269.5f, 183.3f))) * 24634.6345f);
        float radius = sqrt(sampleX);
        float angle = 6.2831853f * sampleY;
        float3 tangent = normalize(abs(normal.y) < 0.9f
            ? cross(normal, float3(0.0f, 1.0f, 0.0f))
            : cross(normal, float3(1.0f, 0.0f, 0.0f)));
        float3 bitangent = normalize(cross(normal, tangent));
        float3 bounceDirection = normalize(
            normal * sqrt(max(0.0f, 1.0f - sampleX)) +
            (tangent * cos(angle) + bitangent * sin(angle)) * radius);
        RayDesc bounceRay;
        if (dot(bounceDirection, geometricNormal) <= 0.0f) {
            payload.radiance = float4(radiance, Luminance(radiance));
            return;
        }
        bounceRay.Origin = OffsetMaterialRayOrigin(hitPosition, geometricNormal, bounceDirection, 0.025f);
        bounceRay.Direction = bounceDirection;
        bounceRay.TMin = 0.02f;
        bounceRay.TMax = max(gTraceSettings.z, 1.0f);
        ReflectionPayload secondBounce;
        InitializeReflectionPayload(secondBounce, 5u);
        TraceRay(gScene, RAY_FLAG_NONE,
                 0x03, 0, 1, 0, bounceRay, secondBounce);
        // Raster hybrid mode adds GI over an already lit image and keeps a
        // conservative artistic scale. Path lighting replaces that image, so
        // retain the cosine-weighted Monte Carlo throughput instead.
        float bounceWeight = gParams.w > 0.5f ? 1.0f : 0.35f;
        radiance += max(0.0f, secondBounce.radiance.rgb) *
                    baseColor.rgb * (1.0f - saturate(material.metallic)) * bounceWeight;
    }
    payload.radiance = float4(radiance, Luminance(radiance));
}
