// ============================================================
// Ocarina of Time PC — Couch Edition
// RT3: Native DXR Ray-Traced Shadow Shader (HLSL SM 6.3)
// ============================================================

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gDepthTexture : register(t1);
RWTexture2D<float> gShadowMask : register(u0);
RWTexture2D<float4> gSurfaceMask : register(u1);
RWTexture2D<float> gEmissiveMask : register(u2);
RWTexture2D<float4> gIndirectMask : register(u3);
RWTexture2D<float4> gReflectionColor : register(u4);
RWStructuredBuffer<uint> gPrimaryGateCounters : register(u5);
ByteAddressBuffer gHitVertexData : register(t2);
ByteAddressBuffer gHitIndexData : register(t3);
ByteAddressBuffer gInstanceMaterialData : register(t4);
Texture2D<float4> gMaterialTextures[4096] : register(t5);
Texture2D<float4> gRasterEnvironment : register(t4101);
Texture2D<float> gRasterSoftShadow : register(t4102);
SamplerState gMaterialSampler : register(s0);

cbuffer FrameConstants : register(b0) {
    float4x4 gInvProjection;
    float4x4 gInvView;
    float4 gLightDir;
    float4 gScreenSize;
    float4 gParams; // pass-specific: bias, maxDist, clipXScale, shadow/composite control
    float4 gQuality; // indirect sample count, reflection enable, relief scale, point-light count
    float4 gSceneLightColor;
    float4 gAmbientLightColor;
    float4 gFrameInfo; // monotonically changing path sample index
    float4 gDiagnosticInfo; // flags, material-record count, SRV count, view
    float4 gSecondaryLightDir;
    float4 gSecondaryLightColor;
    float4 gPointLightPosition[32]; // view-space xyz, w: authored influence radius
    float4 gPointLightOptics[32];   // x: reserved, yzw: linear RGB color
    float4x4 gProjection;
    uint4 gDiagnosticBounds; // logical vertex, index, material-record counts, reserved
};

struct ShadowPayload {
    float visibility;
};

[shader("raygeneration")]
void RayGenShadow() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    uint2 depthPixel = min(uint2((float2(launchIndex) + 0.5f) / float2(launchDim) * gScreenSize.xy),
                           uint2(gScreenSize.xy) - 1u);
    float depth = gDepthTexture.Load(int3(depthPixel, 0));

    // Si la profundidad es 1.0f (plano lejano / fondo / cielo), no hay sombra
    if (depth >= 1.0f) {
        gShadowMask[launchIndex] = 1.0f;
        return;
    }

    // Reconstrucción NDC -> World Space
    float2 uv = (float2(launchIndex) + 0.5f) / float2(launchDim);
    float clipXScale = gParams.z > 1e-4f ? gParams.z : 1.0f;
    // Fast3D emits clip-space Z in [-w, w]; D3D12 maps it to [0, 1] for
    // the depth buffer. Restore the original NDC range before inverting P.
    float4 clipPos = float4((uv.x * 2.0f - 1.0f) / clipXScale,
                            1.0f - uv.y * 2.0f, depth * 2.0f - 1.0f, 1.0f);
    float4 worldPos = mul(gInvProjection, clipPos);

    if (abs(worldPos.w) > 1e-6f) {
        worldPos.xyz /= worldPos.w;
    }

    float bias = gParams.x > 0.0f ? gParams.x : 0.15f;
    float maxDist = gParams.y > 0.0f ? gParams.y : 10000.0f;

    float3 lightDir = normalize(gLightDir.xyz);
    float3 lTangent = normalize(abs(lightDir.y) < 0.95f ? cross(lightDir, float3(0.0f, 1.0f, 0.0f))
                                                        : cross(lightDir, float3(1.0f, 0.0f, 0.0f)));
    float3 lBitangent = cross(lightDir, lTangent);

    // Low-discrepancy disk samples reduce penumbra noise as the RTX quality
    // tier raises the per-pixel shadow-ray budget.
    float2 seed = float2(launchIndex);
    float jitter0 = frac(sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453f);
    float jitter1 = frac(sin(dot(seed + float2(1.7f, 3.1f), float2(39.346f, 11.135f))) * 23421.631f);
    float lightRadius = 0.026f;
    float totalVisibility = 0.0f;

    uint shadowSamples = min(16u, max(1u, (uint)max(gQuality.x, 1.0f)));
    [loop] for (uint s = 0u; s < shadowSamples; ++s) {
        float sampleIndex = float(s);
        float angle = frac(jitter0 + sampleIndex * 0.61803398875f) * 6.2831853f;
        float radial = sqrt((sampleIndex + jitter1) / float(shadowSamples));
        float r = radial * lightRadius;
        float3 rayDir = normalize(lightDir + (lTangent * cos(angle) + lBitangent * sin(angle)) * r);

        RayDesc ray;
        ray.Origin = worldPos.xyz + rayDir * bias;
        ray.Direction = rayDir;
        ray.TMin = max(bias * 0.1f, 0.001f);
        ray.TMax = maxDist;

        ShadowPayload payload;
        payload.visibility = 0.0f;

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

[shader("closesthit")]
void ClosestHitShadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.visibility = 0.0f;
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
};

// Keep the enum value synchronized with MaterialType::GENERIC_MATTE. This
// fallback has no reliable surface classification and must not replace the
// original Fast3D pixel in the primary path-traced composite.
static const uint MATERIAL_TYPE_GENERIC_MATTE = 18u;
static const uint MATERIAL_TYPE_WATER = 4u;
static const uint MATERIAL_TYPE_GLASS = 5u;
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
    float4 color;
    uint normalState;
    uint sourceAlpha;
    uint recordLoadedAlpha;
    uint capturedAlpha;
    uint fogMode;
    uint localAlpha;
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
}

MaterialRecord LoadMaterialRecord(uint instanceIndex) {
    uint address = instanceIndex * 80u;
    uint4 ids = gInstanceMaterialData.Load4(address);
    uint4 counts = gInstanceMaterialData.Load4(address + 16u);
    float4 optics = asfloat(gInstanceMaterialData.Load4(address + 32u));
    float4 secondary = asfloat(gInstanceMaterialData.Load4(address + 48u));
    float4 baseColor = asfloat(gInstanceMaterialData.Load4(address + 64u));
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
    uint address = vertexIndex * 48u;
    uint4 packed = gHitVertexData.Load4(address + 32u);
    HitVertex result;
    result.position = asfloat(gHitVertexData.Load3(address));
    result.normal = asfloat(gHitVertexData.Load3(address + 12u));
    result.uv = asfloat(gHitVertexData.Load2(address + 24u));
    result.color = float4(packed.x & 255u, (packed.x >> 8u) & 255u,
                          (packed.x >> 16u) & 255u, (packed.x >> 24u) & 255u) / 255.0f;
    result.normalState = packed.y;
    uint forensic = gHitVertexData.Load(address + 40u);
    result.sourceAlpha = forensic & 0xFFu;
    result.recordLoadedAlpha = (forensic >> 8u) & 0xFFu;
    result.capturedAlpha = (forensic >> 16u) & 0xFFu;
    result.fogMode = (forensic >> 24u) & 0xFFu;
    result.localAlpha = gHitVertexData.Load(address + 44u) & 0xFFu;
    return result;
}

// Diagnostic-only guards run before DecodeMaterialHit can dereference the
// instance record, triangle indices, or vertex data. Error values are copied
// to the per-pixel gate map; no index is clamped into a different record.
uint ValidatePrimaryHit(uint instanceIndex, uint geometryIndex, uint primitiveIndex,
                        out uint validFlags) {
    validFlags = 0u;
    MaterialRecord material;
    if ((uint)gDiagnosticInfo.y != gDiagnosticBounds.z) return 10u; // CPU metadata disagreement
    if (instanceIndex >= gDiagnosticBounds.z) return 2u; // beyond logical material-record count
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
         material.textureIndex >= 4096u)) return 8u;
    return 0u;
}

float4 SampleMaterialTexture(MaterialRecord material, float2 uv, float mipLevel) {
    if ((material.flags & 1u) == 0u || material.textureIndex >= 4096u) return 1.0f;
    Texture2D<float4> albedoTexture = gMaterialTextures[NonUniformResourceIndex(material.textureIndex)];
    float4 sampleColor = albedoTexture.SampleLevel(gMaterialSampler, uv, mipLevel);
    // Keep authored alpha coverage stable for cutouts and blended Fast3D art;
    // only the filtered color channels use the ray-footprint mip.
    if (material.alphaMode != 0u) {
        sampleColor.a = albedoTexture.SampleLevel(gMaterialSampler, uv, 0.0f).a;
    }
    // Fast3D texture SRVs are UNORM rather than UNORM_SRGB. Decode albedo
    // before the DXR BRDF; alpha stays encoded so cutout coverage is unchanged.
    sampleColor.rgb = SrgbToLinear(sampleColor.rgb);
    return sampleColor;
}

float3 DecodeMaterialHit(uint instanceIndex, uint primitiveIndex, uint probeMode,
                         BuiltInTriangleIntersectionAttributes attribs,
                         out MaterialRecord material, out float2 uv,
                         out float3 surfaceNormal, out float4 vertexColor,
                         out float4 baseColor, out float vertexAlpha,
                         out float sampledTextureAlpha, out float textureLod,
                         out float3 surfaceTangent, out float3 surfaceBitangent,
                         out bool frontFace) {
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
    float3x3 normalToView = transpose((float3x3)WorldToObject3x4());
    // DXR classifies the triangle after applying its instance transform,
    // including reflected transforms that can invert a manually transformed
    // geometric normal. Use that native facing result for refraction IOR.
    frontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
    surfaceNormal = normalize(mul(normalToView, normalize(localNormal)));
    if (dot(surfaceNormal, -WorldRayDirection()) < 0.0f) surfaceNormal = -surfaceNormal;

    float3 edge1 = v1.position - v0.position;
    float3 edge2 = v2.position - v0.position;
    float2 uvEdge1 = v1.uv - v0.uv;
    float2 uvEdge2 = v2.uv - v0.uv;
    textureLod = 0.0f;
    if ((material.flags & 1u) != 0u && material.textureIndex < 4096u) {
        Texture2D<float4> albedoTexture = gMaterialTextures[NonUniformResourceIndex(material.textureIndex)];
        uint textureWidth, textureHeight, mipCount;
        albedoTexture.GetDimensions(0u, textureWidth, textureHeight, mipCount);
        if (textureWidth > 0u && textureHeight > 0u && mipCount > 1u) {
            float3 worldEdge1 = mul((float3x3)ObjectToWorld3x4(), edge1);
            float3 worldEdge2 = mul((float3x3)ObjectToWorld3x4(), edge2);
            float density1 = length(uvEdge1 * float2(textureWidth, textureHeight)) /
                             max(length(worldEdge1), 1.0e-5f);
            float density2 = length(uvEdge2 * float2(textureWidth, textureHeight)) /
                             max(length(worldEdge2), 1.0e-5f);
            float pixelsPerWorldUnit = max(abs(gProjection[1][1]) * gScreenSize.y, 1.0f) * 0.5f;
            float worldFootprint = max(RayTCurrent(), 1.0e-3f) / pixelsPerWorldUnit;
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
    const float4 sampledMaterialColor = SampleMaterialTexture(material, uv, textureLod);
    sampledTextureAlpha = sampledMaterialColor.a;
    baseColor = saturate(sampledMaterialColor * vertexColor * material.baseColor);
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

[shader("anyhit")]
void AnyHitShadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    MaterialRecord material = LoadMaterialRecord(InstanceID());
    if (material.alphaMode == 1u) {
        MaterialRecord sampledMaterial;
        float2 uv;
        float3 normal;
        float3 tangent, bitangent;
        float4 vertexColor;
        float4 baseColor;
        float vertexAlpha;
        float sampledTextureAlpha;
        float textureLod;
        bool frontFace;
        DecodeMaterialHit(InstanceID(), PrimitiveIndex(), 0u, attribs, sampledMaterial,
                          uv, normal, vertexColor, baseColor, vertexAlpha, sampledTextureAlpha, textureLod,
                          tangent, bitangent, frontFace);
        if (baseColor.a < 0.5f) IgnoreHit();
    } else if (material.alphaMode == 2u || material.alphaMode == 3u) {
        IgnoreHit();
    }
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

float3 EvaluateMaterialDirectLight(float3 baseColor, float3 normal, float3 viewDirection,
                                   float3 lightDirection, float roughness, float metallic,
                                   float specular) {
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;

    float3 halfVector = normalize(viewDirection + lightDirection);
    float nDotH = saturate(dot(normal, halfVector));
    float vDotH = saturate(dot(viewDirection, halfVector));
    float3 f0 = lerp((0.04f * saturate(specular)).xxx, baseColor, saturate(metallic));
    float3 fresnel = MaterialFresnelSchlick(vDotH, f0);
    float distribution = MaterialNdfGGX(nDotH, roughness);
    float geometry = MaterialGeometrySchlickGGX(nDotV, roughness) *
                     MaterialGeometrySchlickGGX(nDotL, roughness);
    float3 specularBrdf = distribution * geometry * fresnel /
                          max(4.0f * nDotV * nDotL, 1.0e-4f);
    float3 diffuseColor = baseColor * (1.0f - saturate(metallic));
    float3 diffuseBrdf = diffuseColor * (1.0f - fresnel) * (1.0f / 3.14159265f);
    return (diffuseBrdf + specularBrdf) * nDotL;
}

float TraceMaterialVisibility(float3 hitPosition, float3 normal, float3 lightDirection,
                              float maxDistance) {
    ReflectionPayload visibility;
    InitializeReflectionPayload(visibility, 2u);
    // Start blocked: accepted opaque hits keep zero. Only MissReflection
    // marks an unobstructed path visible; alpha cutouts can still IgnoreHit.
    visibility.surfaceOptics.w = 0.0f;
    RayDesc shadowRay;
    shadowRay.Direction = normalize(lightDirection);
    float normalSide = dot(normal, shadowRay.Direction) >= 0.0f ? 1.0f : -1.0f;
    shadowRay.Origin = hitPosition + normal * (normalSide * 0.025f);
    shadowRay.TMin = 0.01f;
    shadowRay.TMax = max(maxDistance, 0.02f);
    TraceRay(gScene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0x03, 0, 1, 0, shadowRay, visibility);
    return saturate(visibility.surfaceOptics.w);
}

float3 EvaluateMaterialPointLight(float3 baseColor, float3 normal, float3 viewDirection,
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
    float attenuation = radialFalloff * radialFalloff;
    // Avoid a visibility ray when the largest possible RGB contribution is
    // already below the scene-linear noise floor. N64 light colors are
    // normalized to [0, 1] when captured, so this bound is conservative.
    if (attenuation * max(lightOptics.y, max(lightOptics.z, lightOptics.w)) <= 1.0e-4f ||
        dot(normal, lightDirection) <= 0.0f) return 0.0f;
    float visibility = TraceMaterialVisibility(hitPosition, normal, lightDirection,
                                               distanceToLight - 0.025f);
    return EvaluateMaterialDirectLight(baseColor, normal, viewDirection, lightDirection,
                                       roughness, metallic, specular) * lightOptics.yzw *
           (attenuation * visibility);
}

float3 EvaluateMaterialPointBackscatter(float3 baseColor, float3 normal, float3 hitPosition,
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
    float attenuation = radialFalloff * radialFalloff;
    if (attenuation * amount * max(lightOptics.y, max(lightOptics.z, lightOptics.w)) <= 1.0e-4f) {
        return 0.0f;
    }
    float visibility = TraceMaterialVisibility(hitPosition, normal, lightDirection,
                                               distanceToLight - 0.025f);
    float backScatter = pow(saturate(dot(-normal, lightDirection)), 2.0f);
    return baseColor * lightOptics.yzw * (backScatter * attenuation * amount * visibility);
}

void ClearSurfaceSignals(uint2 pixel) {
    gSurfaceMask[pixel] = 0.0f;
    gEmissiveMask[pixel] = 0.0f;
    gIndirectMask[pixel] = 0.0f;
    gReflectionColor[pixel] = 0.0f;
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
    uint2 launchDim = DispatchRaysDimensions().xy;
    uint2 screenDim = max(uint2(gScreenSize.xy), uint2(1, 1));
    uint2 screenPixel = min(uint2((float2(launchIndex) + 0.5f) * (float2(screenDim) / float2(launchDim))), screenDim - 1);
    const bool gateDiagnostic = (((uint)gDiagnosticInfo.x & 1u) != 0u);
    const bool gateCountersEnabled = (((uint)gDiagnosticInfo.x & 4u) != 0u);
    float depth = gDepthTexture.Load(int3(screenPixel, 0));
    if (depth >= 1.0f) {
        if (gateDiagnostic && gateCountersEnabled) IncrementPrimaryGateCounter(PG_RASTER_BACKGROUND, launchIndex);
        ClearSurfaceSignals(launchIndex);
        if (gateDiagnostic) gReflectionColor[launchIndex] = float4(1.0f / 255.0f, 10.0f / 255.0f, 0.0f, 1.0f);
        return;
    }
    if (gateDiagnostic && gateCountersEnabled) IncrementPrimaryGateCounter(PG_RASTER_GEOMETRY, launchIndex);

    float2 uv = (float2(screenPixel) + 0.5f) / float2(screenDim);
    float clipXScale = gParams.z > 1e-4f ? gParams.z : 1.0f;
    uint debugMode = (uint)max(gLightDir.w, 0.0f);
    // In material-class debug, divide the image into bands to compare ray
    // origins and transform spaces in one deterministic frame.
    uint rayVariant = debugMode == 1u ? min(5u, launchIndex.x * 6u / max(launchDim.x, 1u)) : 0u;
    float ndcX = uv.x * 2.0f - 1.0f;
    float ndcY = 1.0f - uv.y * 2.0f;
    // Match Interpreter::GfxSpTri1, which maps Fast3D clip Z [-w, w] to
    // D3D12's depth range [0, 1] before rasterization.
    float4 clipPos = float4(ndcX / clipXScale, ndcY, depth * 2.0f - 1.0f, 1.0f);
    float4 viewPosH = rayVariant == 4u || rayVariant == 5u
        ? mul(clipPos, gInvProjection) : mul(gInvProjection, clipPos);
    float3 viewPos = abs(viewPosH.w) > 1e-6f ? viewPosH.xyz / viewPosH.w : viewPosH.xyz;
    float distanceToRaster = length(viewPos);
    if (distanceToRaster < 1e-4f) {
        ClearSurfaceSignals(launchIndex);
        if (gateDiagnostic) gReflectionColor[launchIndex] = float4(1.0f / 255.0f, 9.0f / 255.0f, 0.0f, 1.0f);
        return;
    }

    // The first hit now follows the exact raster camera ray and stops at the
    // depth-buffer surface. This gives ClosestHit the actual visible triangle,
    // instead of guessing material from a short inward normal probe.
    RayDesc primaryRay;
    primaryRay.Origin = float3(0.0f, 0.0f, 0.0f);
    primaryRay.Direction = viewPos / distanceToRaster;
    if (rayVariant == 1u) {
        float4 originWorldH = mul(gInvView, float4(0.0f, 0.0f, 0.0f, 1.0f));
        float4 directionWorldH = mul(gInvView, float4(primaryRay.Direction, 0.0f));
        primaryRay.Origin = abs(originWorldH.w) > 1e-6f
            ? originWorldH.xyz / originWorldH.w : originWorldH.xyz;
        primaryRay.Direction = normalize(directionWorldH.xyz);
    } else if (rayVariant >= 2u) {
        float4 nearClip = float4(ndcX / clipXScale, ndcY, -1.0f, 1.0f);
        float4 nearPosH = rayVariant >= 4u
            ? mul(nearClip, gInvProjection) : mul(gInvProjection, nearClip);
        float3 nearPos = abs(nearPosH.w) > 1e-6f ? nearPosH.xyz / nearPosH.w : nearPosH.xyz;
        primaryRay.Direction = normalize(viewPos - nearPos);
        primaryRay.Origin = nearPos;
        if (rayVariant == 3u || rayVariant == 5u) {
            float4 originWorldH = mul(gInvView, float4(nearPos, 1.0f));
            float4 directionWorldH = mul(gInvView, float4(primaryRay.Direction, 0.0f));
            primaryRay.Origin = abs(originWorldH.w) > 1e-6f
                ? originWorldH.xyz / originWorldH.w : originWorldH.xyz;
            primaryRay.Direction = normalize(directionWorldH.xyz);
        }
        if (rayVariant == 5u) primaryRay.Direction = -primaryRay.Direction;
    }
    primaryRay.TMin = 0.001f;
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
    if (gateDiagnostic) {
        const bool primaryHit = (surface.diagnosticFlags & 2u) != 0u;
        if (gateCountersEnabled) {
            if (primaryHit) IncrementPrimaryGateCounter(PG_GEOMETRY_PRIMARY_HIT, launchIndex);
            else IncrementPrimaryGateCounter(PG_GEOMETRY_PRIMARY_MISS, launchIndex);
            if (primaryHit) IncrementPrimaryGateCounter(2u, launchIndex);
            else IncrementPrimaryGateCounter(1u, launchIndex);
            if ((surface.diagnosticFlags & 4u) != 0u) IncrementPrimaryGateCounter(3u, launchIndex);
            if ((surface.diagnosticFlags & 8u) != 0u) IncrementPrimaryGateCounter(4u, launchIndex);
            if (surface.diagnosticError >= 2u && surface.diagnosticError <= 10u) {
                IncrementPrimaryGateCounter(8u, launchIndex);
                IncrementPrimaryGateCounter(10u + surface.diagnosticError - 2u, launchIndex);
            }
            if (surface.diagnosticError == 9u) IncrementPrimaryGateCounter(9u, launchIndex);
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

    float4 worldSeedH = mul(gInvView, float4(viewPos, 1.0f));
    float3 worldSeedPosition = abs(worldSeedH.w) > 1.0e-6f
        ? worldSeedH.xyz / worldSeedH.w : worldSeedH.xyz;
    float2 stableSampleSeed = float2(
        worldSeedPosition.x + worldSeedPosition.z * 17.0f + float(surface.instanceIndex) * 0.37f,
        worldSeedPosition.y + worldSeedPosition.z * 7.0f + float(surface.instanceIndex) * 0.73f);

    float3 normal = normalize(surface.normalRoughness.xyz);
    if (surface.materialIndex == 4u) {
        // Organic subtle ripples for water reflection & refraction
        float2 waveCoord = surface.detail.xy * 12.0f;
        float wave = sin(waveCoord.x * 3.14159f + waveCoord.y * 2.0f) * 0.035f +
                     cos(waveCoord.y * 4.0f - waveCoord.x * 2.5f) * 0.035f;
        normal = normalize(normal + float3(wave, 0.0f, wave * 0.7f));
    }
    float3 viewDirection = -primaryRay.Direction;
    float roughness = saturate(surface.normalRoughness.w);
    float metallic = saturate(surface.baseColorMetallic.w);
    float3 baseColor = saturate(surface.baseColorMetallic.rgb);
    float reflectivity = saturate(surface.surfaceOptics.x);
    float emissiveStrength = max(surface.surfaceOptics.y, 0.0f);
    float3 f0 = lerp(0.04f * surface.detail.z.xxx, baseColor, metallic);
    float fresnelPower = pow(1.0f - saturate(dot(normal, viewDirection)), 5.0f);
    float3 fresnel = f0 + (1.0f - f0) * fresnelPower;
    // detail.w is geometric coverage computed from the material alpha class
    // in ClosestHit, never raster fog alpha or a material-name whitelist.
    float materialCoverage = saturate(surface.detail.w);
    // Alpha-blended/additive Fast3D geometry has no physically based opacity
    // model here. Keep the original raster pixel for those effects instead of
    // replacing it with a solid path-traced hit. ClosestHitReflection encodes
    // that primary-hit state in surfaceOptics.w (values 3/4); secondary rays
    // still ignore blended geometry in AnyHitReflection.
    const bool genericMatte = surface.materialIndex == MATERIAL_TYPE_GENERIC_MATTE;
    if (surface.surfaceOptics.w > 2.5f) {
        materialCoverage = 0.0f;
    }
    const bool forceAnyHitCoverage = gateDiagnostic && (((uint)gDiagnosticInfo.x & 2u) != 0u);
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
    float3 reflectionWeight = saturate(fresnel * reflectivity) * materialCoverage *
                              saturate(gQuality.y) * (genericMatte ? 0.0f : 1.0f);
    float transmissiveFactor = (surface.materialIndex == 4u ? 1.0f :
                                surface.materialIndex == 5u ? (1.0f - reflectivity) : 0.0f) *
                               materialCoverage;
    float reflectionContribution = Luminance(reflectionWeight);
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
    if (debugMode != 0u) {
        surface.radiance.a = reflectionContribution;
        float3 debugColor = MaterialDebugColor(debugMode, surface);
        gSurfaceMask[launchIndex] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        gReflectionColor[launchIndex] = float4(debugColor, 1.0f);
        gEmissiveMask[launchIndex] = 0.0f;
        gIndirectMask[launchIndex] = 0.0f;
        return;
    }

    // Alpha-blended/additive hits retain their Fast3D pixels. The generic
    // matte fallback still receives conservative diffuse path lighting using
    // its captured albedo, but it does not invent a reflective surface.
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
            if (dot(reflectionDirection, normal) > 1.0e-4f) {
                RayDesc reflectionRay;
                reflectionRay.Origin = viewPos + normal * 0.025f;
                reflectionRay.Direction = reflectionDirection;
                reflectionRay.TMin = 0.02f;
                reflectionRay.TMax = max(gParams.y, 128.0f);
                ReflectionPayload reflected;
                InitializeReflectionPayload(reflected, 0u);
                TraceRay(gScene, RAY_FLAG_NONE,
                         0x03, 0, 1, 0, reflectionRay, reflected);
                reflectedRadiance += reflected.radiance.rgb;
            }
        }
        reflectedRadiance /= reflectionSamples;
    }

    float3 transmittedRadiance = baseColor;
    if (surface.materialIndex == 4u || surface.materialIndex == 5u) {
        float eta = surface.materialIndex == 4u ? (1.0f / 1.333f) : (1.0f / 1.45f);
        // The primary side is encoded as 2=front/entering, 1=back/exiting.
        // Snell's ratio reverses at the far interface of water or glass.
        if (surface.surfaceOptics.w < 1.5f) eta = rcp(eta);
        float3 transmittedDirection = refract(primaryRay.Direction, normal, eta);
        if (dot(transmittedDirection, transmittedDirection) < 1e-4f)
            transmittedDirection = reflect(primaryRay.Direction, normal);
        RayDesc transmittedRay;
        transmittedRay.Origin = viewPos - normal * 0.025f;
        transmittedRay.Direction = normalize(transmittedDirection);
        transmittedRay.TMin = 0.03f;
        transmittedRay.TMax = max(gParams.y, 128.0f);
        ReflectionPayload transmitted;
        InitializeReflectionPayload(transmitted, 3u);
        TraceRay(gScene, RAY_FLAG_NONE,
                 0x03, 0, 1, 0, transmittedRay, transmitted);
        transmittedRadiance = transmitted.radiance.rgb;
        reflectedRadiance = lerp(transmittedRadiance, reflectedRadiance, saturate(Luminance(reflectionWeight)));
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
    for (uint sampleIndex=0u; sampleIndex<samples; ++sampleIndex) {
        float xi = frac(giRadialSeed + sampleIndex * 0.61803398875f);
        float angle = frac(giRotation + sampleIndex * 0.75487766625f) * 6.2831853f;
        float radial = sqrt(xi);
        float hemisphereZ = sqrt(max(0.0f, 1.0f - xi));
        RayDesc bounce;
        bounce.Origin = viewPos + normal*0.03f;
        bounce.Direction = normalize(normal * hemisphereZ +
                                     (giTangent * cos(angle) + giBitangent * sin(angle)) * radial);
        bounce.TMin=0.025f; bounce.TMax=256.0f;
        ReflectionPayload hit; InitializeReflectionPayload(hit,4u);
        TraceRay(gScene,RAY_FLAG_NONE,0x03,0,1,0,bounce,hit);
        indirectRadiance += max(0.0f, hit.radiance.rgb);
    }
    float3 diffuseReflectance = baseColor * (1.0f - metallic);
    float indirectScale = gParams.w > 0.5f ? 1.0f : 0.28f;
    float3 indirectContribution = samples > 0u
        ? (indirectRadiance / samples) * diffuseReflectance * (indirectScale * materialCoverage)
        : 0.0f;
    float3 reflectionResult = max(0.0f, reflectedRadiance + baseColor * emissiveStrength);
    float3 pathResult = max(0.0f, surface.radiance.rgb) + indirectContribution;
    if (any(isnan(reflectionResult)) || any(isinf(reflectionResult)) ||
        any(isnan(pathResult)) || any(isinf(pathResult))) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    gSurfaceMask[launchIndex] = float4(reflectionContribution, transmissiveFactor, materialCoverage, 1.0f);
    // FP16 has a finite maximum; reject overflow rather than storing infinity.
    if (any(reflectionResult > 65504.0f) || any(pathResult > 65504.0f)) {
        ClearSurfaceSignals(launchIndex);
        return;
    }
    gReflectionColor[launchIndex] = float4(reflectionResult, reflectionContribution);
    if (gParams.w > 0.5f) {
        gIndirectMask[launchIndex] = float4(pathResult, 2.0f);
    } else {
        gIndirectMask[launchIndex] = float4(indirectContribution, samples > 0u ? 1.0f : 0.0f);
    }
    gEmissiveMask[launchIndex] = max(emissiveStrength, 0.0f) * materialCoverage;
}

[shader("miss")]
void MissReflection(inout ReflectionPayload payload) {
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
        float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        if (all(uv >= 0.0f) && all(uv <= 1.0f)) {
            uv = clamp(uv, gScreenSize.zw * 0.5f, 1.0f - gScreenSize.zw * 0.5f);
            float3 environmentColor = max(gRasterEnvironment.SampleLevel(gMaterialSampler, uv, 0.0f).rgb,
                                          0.0f.xxx);
            environmentColor = SrgbToLinear(environmentColor);
            payload.radiance = float4(environmentColor, Luminance(environmentColor));
            payload.surfaceOptics.w = 0.0f;
            return;
        }
    }
    // Rays that leave the rasterized view use a procedural environment tied
    // to the current Fast3D lighting instead of a fixed blue sky. This keeps
    // off-screen reflections and GI responsive to room/time-of-day changes.
    float3 ambient = max(gAmbientLightColor.rgb, 0.0f.xxx);
    float3 keyLight = max(gSceneLightColor.rgb, 0.0f.xxx);
    float3 secondaryLight = max(gSecondaryLightColor.rgb, 0.0f.xxx);
    float secondaryLightLength = length(gSecondaryLightDir.xyz);
    float3 secondaryDirection = secondaryLightLength > 1.0e-6f
        ? gSecondaryLightDir.xyz / secondaryLightLength : float3(0.0f, 1.0f, 0.0f);
    float daylight = saturate(Luminance(keyLight + secondaryLight));
    float skyFactor = smoothstep(-0.12f, 0.92f, direction.y);
    float3 horizonColor = ambient * 0.55f + float3(0.018f, 0.021f, 0.028f);
    float3 nightZenith = float3(0.006f, 0.010f, 0.026f);
    float3 dayZenith = float3(0.075f, 0.16f, 0.34f);
    float3 zenithColor = ambient * 0.22f + lerp(nightZenith, dayZenith, daylight);
    float3 skyColor = lerp(horizonColor, zenithColor, skyFactor);
    skyColor += keyLight * pow(saturate(dot(direction, normalize(gLightDir.xyz))), 20.0f) * 0.12f;
    skyColor += keyLight * pow(saturate(dot(direction, normalize(gLightDir.xyz))), 1024.0f) * 2.0f;
    skyColor += secondaryLight * pow(saturate(dot(direction, secondaryDirection)), 20.0f) * 0.12f;
    skyColor += secondaryLight * pow(saturate(dot(direction, secondaryDirection)), 1024.0f) * 2.0f;
    payload.radiance = float4(skyColor, Luminance(skyColor));
    payload.surfaceOptics.w = 0.0f;
}

[shader("anyhit")]
void AnyHitReflection(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // The primary diagnostic pass must reach ClosestHit so it can validate
    // every ID before reading the corresponding record/index/vertex buffers.
    // It intentionally bypasses alpha filtering for that one diagnostic ray.
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) return;
    MaterialRecord material = LoadMaterialRecord(InstanceID());
    const bool pbrTransmissiveMaterial = material.materialType == MATERIAL_TYPE_WATER ||
                                         material.materialType == MATERIAL_TYPE_GLASS;
    const bool pbrSurfaceRay = payload.probeMode == 0u || payload.probeMode == 1u ||
                               payload.probeMode == 4u || payload.probeMode == 5u;
    if (material.alphaMode == 1u) {
        MaterialRecord sampledMaterial;
        float2 uv;
        float3 normal;
        float3 tangent, bitangent;
        float4 vertexColor;
        float4 baseColor;
        float vertexAlpha;
        float sampledTextureAlpha;
        float textureLod;
        bool frontFace;
        DecodeMaterialHit(InstanceID(), PrimitiveIndex(), payload.probeMode, attribs, sampledMaterial,
                          uv, normal, vertexColor, baseColor, vertexAlpha, sampledTextureAlpha, textureLod,
                          tangent, bitangent, frontFace);
        float alpha = baseColor.a;
        if (alpha < 0.5f) IgnoreHit();
    } else if (payload.probeMode != 1u &&
               (material.alphaMode == 2u || material.alphaMode == 3u) &&
               (!pbrTransmissiveMaterial || !pbrSurfaceRay)) {
        // Transparent/additive geometry remains visible in the original
        // Fast3D pass and does not become an opaque blocker in secondary rays.
        // Water and glass retain their PBR surface response for reflection and
        // indirect rays even when Fast3D renders them with alpha blending.
        IgnoreHit();
    }
}

[shader("closesthit")]
void ClosestHitReflection(inout ReflectionPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    if (payload.probeMode == 1u && (((uint)gDiagnosticInfo.x & 1u) != 0u)) {
        payload.diagnosticFlags |= 2u;
        uint validatedFlags = 0u;
        const uint validationError = ValidatePrimaryHit(InstanceID(), GeometryIndex(), PrimitiveIndex(),
                                                        validatedFlags);
        payload.diagnosticFlags |= validatedFlags;
        if (validationError != 0u) {
            payload.diagnosticError = validationError;
            payload.materialIndex = 20u;
            payload.surfaceOptics.w = 4.0f;
            return;
        }
        MaterialRecord hitMaterial = LoadMaterialRecord(InstanceID());
        const bool gateCountersEnabled = (((uint)gDiagnosticInfo.x & 4u) != 0u);
        if (gateCountersEnabled) {
            IncrementPrimaryGateCounter(PG_RECORD_HIST_BASE + min(InstanceID(), 256u), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_ALPHA_MODE_HIST_BASE + min(hitMaterial.alphaMode, 4u), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_TYPE_HIST_BASE + min(hitMaterial.materialType, 21u), DispatchRaysIndex().xy);
            IncrementPrimaryGateCounter(PG_FLAGS_HIST_BASE + min(hitMaterial.flags, 16u), DispatchRaysIndex().xy);
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
            if (alphaBlendCandidate) IncrementPrimaryGateCounter(PG_ALPHA_BLEND_CANDIDATE, DispatchRaysIndex().xy);
            if (alphaBlendCandidate && transmissiveException)
                IncrementPrimaryGateCounter(PG_TRANSMISSIVE_EXCEPTION, DispatchRaysIndex().xy);
            if (payload.probeMode == 1u && alphaBlendCandidate && !transmissiveException)
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
    MaterialRecord material;
    float2 uv;
    float3 normal;
    float3 tangent, bitangent;
    float4 vertexColor;
    float4 baseColor;
    float vertexAlpha;
    float sampledTextureAlpha;
    float textureLod;
    bool frontFace;
    DecodeMaterialHit(InstanceID(), PrimitiveIndex(), payload.probeMode, attribs, material,
                      uv, normal, vertexColor, baseColor, vertexAlpha, sampledTextureAlpha, textureLod,
                      tangent, bitangent, frontFace);
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
    float wetness = saturate(material.wetness);
    float roughness = clamp(material.roughness, 0.04f, 1.0f);
    roughness = lerp(roughness, min(roughness, 0.08f), wetness);
    float reflectivity = lerp(saturate(material.reflectivity),
                              max(saturate(material.reflectivity), 0.45f), wetness * 0.65f);
    payload.materialIndex = material.materialType;
    payload.instanceIndex = InstanceID();
    payload.textureIndex = material.textureIndex;
    payload.normalRoughness = float4(normal, roughness);
    payload.baseColorMetallic = float4(baseColor.rgb, material.metallic);
    const bool pbrTransmissiveMaterial = material.materialType == MATERIAL_TYPE_WATER ||
                                         material.materialType == MATERIAL_TYPE_GLASS;
    const bool bypassPrimaryRasterBlend = payload.probeMode == 1u &&
        (((uint)gDiagnosticInfo.x & 8u) != 0u);
    const uint candidateAlphaMode = (material.flags >> 2u) & 3u;
    const bool useCandidateAlphaPolicy = payload.probeMode == 1u &&
        (((uint)gDiagnosticInfo.x & 128u) != 0u);
    const uint policyAlphaMode = useCandidateAlphaPolicy ? candidateAlphaMode : material.alphaMode;
    const bool rawPrimaryRasterBlend = payload.probeMode == 1u &&
                                    (policyAlphaMode == 2u || policyAlphaMode == 3u) &&
                                    !pbrTransmissiveMaterial;
    const bool primaryRasterBlend = rawPrimaryRasterBlend && !bypassPrimaryRasterBlend;
    payload.surfaceOptics = float4(reflectivity, material.emissiveStrength, RayTCurrent(),
                                   primaryRasterBlend ? 3.0f : (frontFace ? 2.0f : 1.0f));
    float coverage = 0.0f;
    const bool opacityValid = !isnan(baseColor.a) && !isinf(baseColor.a);
    if (opacityValid && policyAlphaMode == 0u) coverage = 1.0f; // opaque alpha is not opacity
    else if (opacityValid && policyAlphaMode == 1u) coverage = baseColor.a >= 0.5f ? 1.0f : 0.0f;
    else if (opacityValid && pbrTransmissiveMaterial) coverage = saturate(baseColor.a);
    payload.detail = float4(uv, material.specular, coverage);

    if (payload.probeMode == 1u && gParams.w <= 0.5f) {
        payload.radiance = float4(baseColor.rgb, Luminance(baseColor.rgb));
        return;
    }

    float3 lightDirection = normalize(gLightDir.xyz);
    float3 viewDirection = normalize(-WorldRayDirection());
    float3 hitPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float directionalVisibility = 1.0f;
    if (dot(normal, lightDirection) > 0.0f) {
        bool usedRasterSoftShadow = false;
        if (payload.probeMode == 1u && gSceneLightColor.w > 0.5f) {
            float4 rasterClip = mul(gProjection, float4(hitPosition, 1.0f));
            if (rasterClip.w > 1.0e-5f) {
                float2 rasterNdc = rasterClip.xy / rasterClip.w;
                float2 rasterUv = float2(rasterNdc.x * gParams.z * 0.5f + 0.5f,
                                         0.5f - rasterNdc.y * 0.5f);
                if (all(rasterUv >= 0.0f) && all(rasterUv <= 1.0f)) {
                    uint2 shadowSize = max(uint2(gFrameInfo.yz), uint2(1u, 1u));
                    uint2 shadowPixel = min(uint2(rasterUv * float2(shadowSize)), shadowSize - 1u);
                    directionalVisibility = saturate(gRasterSoftShadow.Load(int3(shadowPixel, 0)));
                    usedRasterSoftShadow = true;
                }
            }
        }
        if (!usedRasterSoftShadow) {
            directionalVisibility = TraceMaterialVisibility(hitPosition, normal, lightDirection,
                                                            max(gParams.y, 10000.0f));
        }
    }
    float3 direct = EvaluateMaterialDirectLight(baseColor.rgb, normal, viewDirection,
                                                lightDirection, roughness,
                                                material.metallic, material.specular) *
                    max(gSceneLightColor.rgb, 0.0f.xxx) * directionalVisibility;
    float secondaryLightLength = length(gSecondaryLightDir.xyz);
    float3 secondaryLightDirection = secondaryLightLength > 1.0e-6f
        ? gSecondaryLightDir.xyz / secondaryLightLength : float3(0.0f, 1.0f, 0.0f);
    float3 secondaryLightColor = max(gSecondaryLightColor.rgb, 0.0f.xxx);
    float secondaryVisibility = 1.0f;
    float subsurface = saturate(material.subsurfaceHint);
    float secondaryNdotL = dot(normal, secondaryLightDirection);
    bool secondaryContributes = secondaryNdotL > 0.0f ||
                               (subsurface > 0.0f && dot(-normal, secondaryLightDirection) > 0.0f);
    if (max(secondaryLightColor.r, max(secondaryLightColor.g, secondaryLightColor.b)) > 1.0e-4f &&
        secondaryContributes) {
        secondaryVisibility = TraceMaterialVisibility(hitPosition, normal, secondaryLightDirection,
                                                     max(gParams.y, 10000.0f));
        if (secondaryNdotL > 0.0f) {
            direct += EvaluateMaterialDirectLight(baseColor.rgb, normal, viewDirection,
                                                 secondaryLightDirection, roughness,
                                                 material.metallic, material.specular) *
                      secondaryLightColor * secondaryVisibility;
        }
    }
    // The first GI hit keeps complete point-light shading. At the final
    // diffuse bounce, omit point-light shadow rays to bound worst-case work;
    // ambient and the shadowed directional light still contribute there.
    uint pointLightCount = payload.probeMode == 5u
        ? 0u : min((uint)max(gQuality.w, 0.0f), 32u);
    [loop] for (uint lightIndex = 0u; lightIndex < pointLightCount; ++lightIndex) {
        direct += EvaluateMaterialPointLight(baseColor.rgb, normal, viewDirection, hitPosition,
                                             gPointLightPosition[lightIndex], gPointLightOptics[lightIndex],
                                             roughness, material.metallic, material.specular);
    }
    float directionalBackscatter = pow(saturate(dot(-normal, lightDirection)), 2.0f);
    float3 backscatter = baseColor.rgb * max(gSceneLightColor.rgb, 0.0f.xxx) *
                         (directionalBackscatter * subsurface * 0.18f * directionalVisibility);
    float secondaryBackscatter = pow(saturate(dot(-normal, secondaryLightDirection)), 2.0f);
    backscatter += baseColor.rgb * secondaryLightColor *
                   (secondaryBackscatter * subsurface * 0.18f * secondaryVisibility);
    [loop] for (uint lightIndex = 0u; lightIndex < pointLightCount; ++lightIndex) {
        backscatter += EvaluateMaterialPointBackscatter(baseColor.rgb, normal, hitPosition,
                                                         gPointLightPosition[lightIndex],
                                                         gPointLightOptics[lightIndex],
                                                         subsurface * 0.18f);
    }
    float3 ambient = baseColor.rgb * (1.0f - saturate(material.metallic)) *
                     max(gAmbientLightColor.rgb, 0.025f.xxx);
    float3 radiance = ambient + direct + backscatter +
                      baseColor.rgb * max(material.emissiveStrength, 0.0f);
    if (payload.probeMode == 1u) {
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
        bounceRay.Origin = hitPosition + normal * 0.025f;
        bounceRay.Direction = bounceDirection;
        bounceRay.TMin = 0.02f;
        bounceRay.TMax = 256.0f;
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
