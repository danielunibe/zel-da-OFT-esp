#pragma once

#include <cstdint>
#include <string>

namespace Fast {

// ==============================================================================
// OCZ-NIGHTLY-14: SEMANTIC RECONSTRUCTION CONTRACT
// Interoperability contract between Unreal Engine 5.8.3 physical laboratory
// and native Direct3D 12 / DXR C++/HLSL renderer.
// ==============================================================================

enum class SemanticSurfaceClass : uint32_t {
    UNKNOWN = 0,
    OPAQUE_MATTE,                   // Stone, masonry, terrain, dirt, brick
    SPECULAR_DIELECTRIC,            // Ceramic tile, glazed mosaic, polished stone
    SPECULAR_METALLIC,              // Iron bars, bronze, chains, metal doors
    TRANSMISSIVE_DIELECTRIC,        // Water bodies, clear glass
    DECORATIVE_BEAM_TRANSLUCENT,    // N64 additive clerestory window sunbeams
    ARCHITECTURAL_APERTURE,         // Window frame openings acting as light portals
    GEOMETRIC_EMITTER,              // Torches, beacons, lanterns, magic runes
    POINT_LIGHT_ACTOR,              // Navi fairy, fireballs, isolated point emitters
    ALPHA_TESTED_FOLIAGE            // Cutout foliage, grass billboards, leaf planes
};

enum class OcclusionPolicy : uint32_t {
    BLOCKS_SHADOW = 0,              // Solid shadow caster (BVH opaque / cutout test)
    TRANSPARENT_PASS_THROUGH,       // Non-occluding (IgnoreHit in AnyHitShadow)
    ALPHA_TESTED                    // Evaluates texture alpha threshold (0.5)
};

enum class ReflectionPolicy : uint32_t {
    NONE = 0,                       // Matte diffuse only, no specular bounce
    ROUGH_SPECULAR_GGX,             // Cook-Torrance GGX microfacet specular
    DIELECTRIC_FRESNEL,             // Schlick Fresnel F0 in [0.02 - 0.05] (ceramic, water)
    METALLIC_FRESNEL                // Complex conductor Fresnel with base color F0
};

enum class TransmissionPolicy : uint32_t {
    NONE = 0,                       // Non-transmissive
    FRESNEL_WATER_COMPOSITE,        // Dual-layer: animated raster + grazing reflection
    GLASS_REFRACTION                // Dielectric transmission with IOR
};

enum class LightingPolicy : uint32_t {
    FAST3D_DIFFUSE = 0,             // Preserves native vertex lighting / shade
    DXR_HYBRID_RELIGHT,             // Evaluates scene directional + point light rays
    UNLIT_ADDITIVE,                 // Self-illuminated additive quad (no shadow/light)
    EMISSIVE_ONLY                   // Surface emits radiance to camera and secondary rays
};

struct SemanticObjectInstance {
    int32_t sceneId = -1;
    int32_t roomId = -1;
    uint32_t drawId = 0;
    uint64_t materialId = 0;
    std::string resourcePath;
    uint32_t actorId = 0;
    const char* provenance = "CONSERVATIVE_FALLBACK";
    float confidence = 0.5f;

    SemanticSurfaceClass surfaceClass = SemanticSurfaceClass::OPAQUE_MATTE;
    OcclusionPolicy occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
    ReflectionPolicy reflectionPolicy = ReflectionPolicy::NONE;
    TransmissionPolicy transmissionPolicy = TransmissionPolicy::NONE;
    LightingPolicy lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
};

// Evaluates semantic policy for recognized scene elements
inline SemanticObjectInstance ClassifySemanticInstance(int32_t sceneId, int32_t roomId,
                                                       const std::string& resourcePath) {
    SemanticObjectInstance inst;
    inst.sceneId = sceneId;
    inst.roomId = roomId;
    inst.resourcePath = resourcePath;

    // 1. TEMPLE OF TIME (Scene 0x43 / 67)
    if (resourcePath.find("tokinoma_room_0Tex_0161D8") != std::string::npos ||
        resourcePath.find("006AF8") != std::string::npos ||
        resourcePath.find("006D50") != std::string::npos) {
        // Decorative Clerestory Sunbeams: must not become a solid white block or shadow blocker!
        inst.provenance = "GAME_ORIGINAL_CLASSIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::DECORATIVE_BEAM_TRANSLUCENT;
        inst.occlusionPolicy = OcclusionPolicy::TRANSPARENT_PASS_THROUGH;
        inst.reflectionPolicy = ReflectionPolicy::NONE;
        inst.transmissionPolicy = TransmissionPolicy::NONE;
        inst.lightingPolicy = LightingPolicy::UNLIT_ADDITIVE;
        return inst;
    }

    if (resourcePath.find("tokinoma_sceneTex_011FA0") != std::string::npos) {
        // Temple Ceramic Mosaic Floor
        inst.provenance = "CALIBRATED_LAB_VERIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::SPECULAR_DIELECTRIC;
        inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
        inst.reflectionPolicy = ReflectionPolicy::DIELECTRIC_FRESNEL;
        inst.transmissionPolicy = TransmissionPolicy::NONE;
        inst.lightingPolicy = LightingPolicy::DXR_HYBRID_RELIGHT;
        return inst;
    }

    // 2. GORON CITY (Scene 0x62 / 98) & DARUNIA'S ROOM
    if (sceneId == 0x62 || resourcePath.find("spot18") != std::string::npos) {
        if (resourcePath.find("gGoronCitySpearTipTex") != std::string::npos) {
            inst.provenance = "CALIBRATED_LAB_VERIFIED";
            inst.confidence = 1.0f;
            inst.surfaceClass = SemanticSurfaceClass::SPECULAR_METALLIC;
            inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
            inst.reflectionPolicy = ReflectionPolicy::METALLIC_FRESNEL;
            inst.transmissionPolicy = TransmissionPolicy::NONE;
            inst.lightingPolicy = LightingPolicy::DXR_HYBRID_RELIGHT;
            return inst;
        }
        if (resourcePath.find("gGoronCityVase") != std::string::npos) {
            inst.provenance = "CALIBRATED_LAB_VERIFIED";
            inst.confidence = 1.0f;
            inst.surfaceClass = SemanticSurfaceClass::SPECULAR_DIELECTRIC;
            inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
            inst.reflectionPolicy = ReflectionPolicy::DIELECTRIC_FRESNEL;
            inst.transmissionPolicy = TransmissionPolicy::NONE;
            inst.lightingPolicy = LightingPolicy::DXR_HYBRID_RELIGHT;
            return inst;
        }
        // Volcanic rock, basalt pillars, masonry walls, and Goron stone emblems
        inst.provenance = "CALIBRATED_LAB_VERIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::OPAQUE_MATTE;
        inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
        inst.reflectionPolicy = ReflectionPolicy::ROUGH_SPECULAR_GGX;
        inst.transmissionPolicy = TransmissionPolicy::NONE;
        inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
        return inst;
    }

    // 3. KOKIRI FOREST (Scene 0x55 / 85)
    if (sceneId == 0x55 || resourcePath.find("spot04") != std::string::npos) {
        if (resourcePath.find("019290") != std::string::npos ||
            resourcePath.find("019A90") != std::string::npos ||
            resourcePath.find("01A290") != std::string::npos ||
            resourcePath.find("01AC90") != std::string::npos ||
            resourcePath.find("01B090") != std::string::npos) {
            // Cutout foliage billboards
            inst.provenance = "CALIBRATED_LAB_VERIFIED";
            inst.confidence = 1.0f;
            inst.surfaceClass = SemanticSurfaceClass::ALPHA_TESTED_FOLIAGE;
            inst.occlusionPolicy = OcclusionPolicy::ALPHA_TESTED;
            inst.reflectionPolicy = ReflectionPolicy::NONE;
            inst.transmissionPolicy = TransmissionPolicy::NONE;
            inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
            return inst;
        }
        // Organic timber, bark, grass ground, earthen trails
        inst.provenance = "CALIBRATED_LAB_VERIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::OPAQUE_MATTE;
        inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
        inst.reflectionPolicy = ReflectionPolicy::ROUGH_SPECULAR_GGX;
        inst.transmissionPolicy = TransmissionPolicy::NONE;
        inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
        return inst;
    }

    // 4. LAKE HYLIA (Scene 0x57 / 87)
    if (resourcePath.find("gLakeHyliaWaterTempleGateTex") != std::string::npos) {
        // Submerged Water Temple Gate / Column Structure: STONE/METAL, NOT WATER!
        inst.provenance = "CALIBRATED_LAB_VERIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::OPAQUE_MATTE;
        inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
        inst.reflectionPolicy = ReflectionPolicy::ROUGH_SPECULAR_GGX;
        inst.transmissionPolicy = TransmissionPolicy::NONE;
        inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
        return inst;
    }

    if (resourcePath.find("gLakeHyliaWaterTex") != std::string::npos ||
        resourcePath.find("spot06_sceneTex_00DCB8") != std::string::npos) {
        // Lake Water Surface
        inst.provenance = "CALIBRATED_LAB_VERIFIED";
        inst.confidence = 1.0f;
        inst.surfaceClass = SemanticSurfaceClass::TRANSMISSIVE_DIELECTRIC;
        inst.occlusionPolicy = OcclusionPolicy::TRANSPARENT_PASS_THROUGH;
        inst.reflectionPolicy = ReflectionPolicy::DIELECTRIC_FRESNEL;
        inst.transmissionPolicy = TransmissionPolicy::FRESNEL_WATER_COMPOSITE;
        inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
        return inst;
    }

    // Conservative default fallback
    inst.provenance = "FALLBACK_GENERIC";
    inst.confidence = 0.5f;
    inst.surfaceClass = SemanticSurfaceClass::OPAQUE_MATTE;
    inst.occlusionPolicy = OcclusionPolicy::BLOCKS_SHADOW;
    inst.reflectionPolicy = ReflectionPolicy::NONE;
    inst.transmissionPolicy = TransmissionPolicy::NONE;
    inst.lightingPolicy = LightingPolicy::FAST3D_DIFFUSE;
    return inst;
}

} // namespace Fast
