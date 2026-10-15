#include "fast/MaterialRegistry.h"
#include "fast/couch_diagnostics.h"
#include "fast/MaterialRulesPilot.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"

namespace Fast {

MaterialRegistry& MaterialRegistry::Instance() {
    static MaterialRegistry instance;
    return instance;
}

static void AppendCouchDiag(const char* msg) {
    printf("%s\n", msg);
    fflush(stdout);
    FILE* f = fopen(couch_diagnostics_path(), "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

void MaterialRegistry::Init() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mInitialized) return;
        mInitialized = true;
        PopulateMaterialRulesTable(mExactRules, mStringRules);
    }
    const char* registryPath = std::getenv("COUCH_MATERIAL_REGISTRY");
    if (registryPath && *registryPath) LoadRegistry(registryPath);
    char buf[128];
    snprintf(buf, sizeof(buf), "[COUCH] Material rules loaded = %zu", mExactRules.size());
    AppendCouchDiag(buf);
}

void MaterialRegistry::ResetMetrics() {
    std::lock_guard<std::mutex> lock(mMutex);
    mTotalDrawCalls = 0;
    mCacheHits = 0;
    mCacheMisses = 0;
    mRulesActivated = 0;
    mUiProtectedCount = 0;
    mUnknownCount = 0;
}

uint64_t MaterialRegistry::MakeKey(const DrawCallInfo& info) const {
    return Fast3DMaterialStateKey(info);
}

MaterialType MaterialRegistry::ClassifyFromState(const DrawCallInfo& info) const {
    if (info.is2D || info.isRect) {
        return MaterialType::PROTECTED_2D;
    }

    std::string path = info.resourcePath;
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto has = [&path](std::initializer_list<const char*> words) {
        for (const char* word : words) {
            if (path.find(word) != std::string::npos) return true;
        }
        return false;
    };

    // 1. Semantic keyword matching (English & Japanese romanized asset names)
    if (has({"water", "river", "lake", "pond", "ocean", "liquid", "mizu", "stream", "aqua", "sea", "taki", "kawa", "umi", "wave", "ripple", "splash", "fountain"})) {
        return MaterialType::WATER;
    }
    if (has({"lava", "magma", "hono", "kazan"})) return MaterialType::LAVA;
    if (has({"magic", "spell", "energy", "fairy", "glow", "hikari"})) return MaterialType::MAGIC;
    if (has({"emissive", "emission", "light_source", "lantern", "lamp", "torch", "candle", "beacon", "flame", "fire"})) {
        return MaterialType::EMISSIVE;
    }
    if (has({"glass", "crystal", "window", "lens", "pane", "garasu"})) return MaterialType::GLASS;
    if (has({"ceramic", "pottery", "clay_pot"})) return MaterialType::CERAMIC;
    if (has({"steel", "iron", "metal", "bronze", "copper", "silver", "gold", "chain", "blade", "sword", "shield", "armor", "tetsu", "kinzoku", "brass", "gate", "grate"})) {
        return MaterialType::METAL;
    }
    if (has({"wood", "timber", "plank", "log", "trunk", "barrel", "crate", "chest", "wooden_door", "bridge", "hashi", "moku", "branch", "fence", "ladder", "raft", "board"})) {
        return MaterialType::WOOD;
    }
    if (has({"grass", "leaf", "leaves", "foliage", "bush", "vine", "flower", "plant", "kusa", "tree", "bark", "moss", "ivy", "shrub", "forest"})) {
        return has({"grass", "kusa"}) ? MaterialType::GRASS : MaterialType::FOLIAGE;
    }
    if (has({"sand", "desert", "suna"})) return MaterialType::SAND;
    if (has({"dirt", "soil", "mud", "earth", "tsuchi"})) return MaterialType::EARTH;
    if (has({"stone", "rock", "brick", "marble", "granite", "boulder", "pillar", "cave", "dungeon", "ruin", "wall", "floor", "iwa", "ishi", "tile", "pave", "cobble", "step"})) {
        return MaterialType::STONE;
    }
    if (has({"cloth", "fabric", "carpet", "rug", "curtain", "banner", "flag", "robe", "tunic", "shirt", "nuno"})) {
        return MaterialType::FABRIC;
    }
    if (has({"skin", "face", "hand", "body_skin", "hada"})) return MaterialType::CHARACTER;

    // 2. Fast3D render-state heuristics for anonymous scene/room textures (e.g., spot04_room_0Tex_...)
    // Tint and blend state alone do not prove water, lava, magic, or emission.
    // Those guesses turned ordinary colored geometry into false ray blockers,
    // reflectors, and light sources. Use semantic resources or explicit rules.

    // Wooden bridges/walkways: warm brown tone
    if (info.primR > info.primB + 35 && info.primG > info.primB + 10 && info.primR < 210 && info.primG < 180) {
        return MaterialType::WOOD;
    }

    // Scene grass/foliage
    if (info.primG > info.primR + 20 && info.primG > info.primB + 20) {
        return MaterialType::GRASS;
    }

    // Scene geometry fallback
    if (path.find("scene") != std::string::npos || path.find("room") != std::string::npos) {
        return MaterialType::STONE;
    }

    return MaterialType::UNKNOWN;
}

MaterialType MaterialRegistry::ClassifyMaterial(const DrawCallInfo& info) const {
    return ClassifyFromState(info);
}

MaterialDefinition MaterialRegistry::GetMaterial(uint64_t key) const {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mMaterials.find(key);
    if (it != mMaterials.end()) {
        return it->second;
    }
    // Draw-call classification is cached by texture hash so the renderer can
    // reuse the same material identity for raster and DXR paths.
    auto cacheIt = mMaterialCache.find(key);
    if (cacheIt != mMaterialCache.end()) {
        return cacheIt->second;
    }
    return MaterialDefinition{};
}

MaterialDefinition MaterialRegistry::GetMaterialForDraw(const DrawCallInfo& info) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mMaterials.find(MakeKey(info));
    if (it != mMaterials.end()) return it->second;
    MaterialDefinition fallback;
    fallback.type = MaterialType::GENERIC_MATTE;
    fallback.roughness = 0.95f;
    fallback.specular = 0.04f;
    fallback.reflectivity = 0.01f;
    fallback.normalStrength = 0.02f;
    return fallback;
}

uint64_t MaterialRegistry::GetMaterialIdForDraw(const DrawCallInfo& info) const {
    return MakeKey(info);
}

MaterialDefinition MaterialRegistry::GetMaterialForTexture(uint64_t textureHash) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mMaterialCache.find(textureHash);
    if (it != mMaterialCache.end()) return it->second;

    MaterialDefinition fallback;
    fallback.type = MaterialType::GENERIC_MATTE;
    fallback.roughness = 0.95f;
    fallback.metallic = 0.0f;
    fallback.specular = 0.04f;
    fallback.reflectivity = 0.01f;
    fallback.normalStrength = 0.02f;
    return fallback;
}

void MaterialRegistry::SetTextureSrvIndex(uint64_t textureHash, uint32_t srvIndex) {
    if (textureHash == 0) return;
    std::lock_guard<std::mutex> lock(mMutex);
    mTextureSrvIndices[textureHash] = srvIndex;
}

uint32_t MaterialRegistry::GetTextureSrvIndex(uint64_t textureHash) const {
    if (textureHash == 0) return std::numeric_limits<uint32_t>::max();
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mTextureSrvIndices.find(textureHash);
    return it == mTextureSrvIndices.end() ? std::numeric_limits<uint32_t>::max() : it->second;
}

void MaterialRegistry::RecordDrawCall(const DrawCallInfo& info) {
    Init();
    std::lock_guard<std::mutex> lock(mMutex);

    mTotalDrawCalls++;
    mCounters.materialDrawCallCount++;
    mFrameDrawCalls.push_back(info);

    // Priority 1: UI Protection
    if (info.is2D || info.isRect) {
        mUiProtectedCount++;
        mFrameClassifications.push_back(MaterialType::PROTECTED_2D);
        return;
    }

    // A texture hash is not a material identity: the same texture can be
    // reused by opaque, translucent, emissive, or differently tinted draws.
    // Reclassify each draw so later semantic rules and render state are not
    // permanently masked by whichever use of the texture appeared first.
    uint64_t texHash = info.textureHash;
    mCacheMisses++;

    MaterialDefinition mat;
    mat.type = ClassifyFromState(info);
    const auto authoredRule = mAuthoredOverrides.find(info.resourcePath);
    const auto pilotRule = mStringRules.find(info.resourcePath);
    const bool forcePilotType = pilotRule != mStringRules.end() &&
                                (pilotRule->second.flags & MATERIAL_FLAG_EXACT_PILOT_TYPE) != 0;
    bool authoredMaterialApplied = false;
    bool pilotMaterialApplied = false;
    // Explicit user-authored entries override heuristic classification.
    // Pilot entries remain advisory except for their explicit, source-verified
    // exact-type opt-ins; protected UI is never reclassified by a pilot rule.
    if (authoredRule != mAuthoredOverrides.end() && authoredRule->second.type != MaterialType::PROTECTED_2D) {
        mat = authoredRule->second;
        authoredMaterialApplied = true;
        mRulesActivated++;
    } else if (pilotRule != mStringRules.end() &&
               mat.type != MaterialType::PROTECTED_2D &&
               (forcePilotType || mat.type == MaterialType::UNKNOWN || mat.type == MaterialType::CLASSIC ||
                mat.type == MaterialType::GENERIC_MATTE || pilotRule->second.type == mat.type)) {
        // Exact resource rules carry stronger provenance than a generic
        // render-state fallback. Only explicitly source-verified Temple assets
        // may override a conflicting semantic hint; other pilot rules still
        // require agreement with the live draw classification.
        mat = pilotRule->second;
        mat.flags &= ~MATERIAL_FLAG_EXACT_PILOT_TYPE;
        pilotMaterialApplied = true;
        mRulesActivated++;
    }
    if (mat.type == MaterialType::UNKNOWN || mat.type == MaterialType::CLASSIC) {
        mUnknownCount++;
    }
    if (pilotMaterialApplied) {
        // Static pilot entries define roughness, metalness, specular response,
        // emission, and optionally an exact reflectivity. A zero reflectivity
        // inherits the category default; replacing the whole material here
        // used to erase the per-resource values from nearly every rule.
        if (mat.reflectivity == 0.0f) {
            switch (mat.type) {
                case MaterialType::STONE: mat.reflectivity = 0.04f; break;
                case MaterialType::WOOD: mat.reflectivity = 0.05f; break;
                case MaterialType::METAL: mat.reflectivity = 0.88f; break;
                case MaterialType::WATER: mat.reflectivity = 0.88f; break;
                case MaterialType::LAVA: mat.reflectivity = 0.08f; break;
                case MaterialType::EMISSIVE: mat.reflectivity = 0.08f; break;
                case MaterialType::GRASS: mat.reflectivity = 0.008f; break;
                case MaterialType::FOLIAGE: mat.reflectivity = 0.012f; break;
                case MaterialType::EARTH: mat.reflectivity = 0.006f; break;
                case MaterialType::SAND: mat.reflectivity = 0.004f; break;
                case MaterialType::GLASS: mat.reflectivity = 0.18f; break;
                case MaterialType::ICE: mat.reflectivity = 0.38f; break;
                case MaterialType::MAGIC: mat.reflectivity = 0.18f; break;
                case MaterialType::FABRIC: mat.reflectivity = 0.012f; break;
                case MaterialType::CHARACTER: mat.reflectivity = 0.025f; break;
                case MaterialType::CERAMIC: mat.reflectivity = 0.10f; break;
                case MaterialType::GENERIC_MATTE: mat.reflectivity = 0.01f; break;
                case MaterialType::PROTECTED_2D: mat.reflectivity = 0.0f; break;
                default: mat.reflectivity = 0.005f; break;
            }
        }
        if (mat.normalStrength == 0.0f) {
            switch (mat.type) {
                case MaterialType::STONE: mat.normalStrength = 0.08f; break;
                case MaterialType::WOOD: mat.normalStrength = 0.06f; break;
                case MaterialType::GRASS: mat.normalStrength = 0.035f; break;
                case MaterialType::FOLIAGE: mat.normalStrength = 0.035f; break;
                case MaterialType::EARTH: mat.normalStrength = 0.08f; break;
                case MaterialType::SAND: mat.normalStrength = 0.03f; break;
                case MaterialType::FABRIC: mat.normalStrength = 0.025f; break;
                case MaterialType::CHARACTER: mat.normalStrength = 0.012f; break;
                case MaterialType::CERAMIC: mat.normalStrength = 0.04f; break;
                case MaterialType::GENERIC_MATTE: mat.normalStrength = 0.02f; break;
                default: break;
            }
        }
    } else if (!authoredMaterialApplied && !pilotMaterialApplied && mat.reflectivity == 0.0f) {
        switch (mat.type) {
            case MaterialType::STONE:
                mat.roughness = 0.80f;
                mat.metallic = 0.0f;
                mat.specular = 0.25f;
                mat.reflectivity = 0.04f;
                mat.normalStrength = 0.08f;
                break;
            case MaterialType::WOOD:
                mat.roughness = 0.65f;
                mat.metallic = 0.0f;
                mat.specular = 0.20f;
                mat.reflectivity = 0.05f;
                mat.normalStrength = 0.06f;
                break;
            case MaterialType::METAL:
                mat.roughness = 0.18f;
                mat.metallic = 0.94f;
                mat.specular = 0.95f;
                mat.reflectivity = 0.88f;
                break;
            case MaterialType::WATER:
                mat.roughness = 0.02f;
                mat.metallic = 0.0f;
                mat.specular = 0.95f;
                mat.reflectivity = 0.88f;
                break;
            case MaterialType::LAVA:
                mat.roughness = 0.25f;
                mat.metallic = 0.0f;
                mat.emissiveStrength = 1.35f;
                mat.specular = 0.30f;
                mat.reflectivity = 0.08f;
                break;
            case MaterialType::EMISSIVE:
                mat.roughness = 0.35f;
                mat.metallic = 0.0f;
                mat.emissiveStrength = 1.20f;
                mat.specular = 0.20f;
                mat.reflectivity = 0.08f;
                break;
            case MaterialType::GRASS:
                mat.roughness = 0.96f;
                mat.metallic = 0.0f;
                mat.specular = 0.04f;
                mat.reflectivity = 0.008f;
                mat.normalStrength = 0.035f;
                break;
            case MaterialType::FOLIAGE:
                mat.roughness = 0.92f;
                mat.metallic = 0.0f;
                mat.specular = 0.05f;
                mat.reflectivity = 0.012f;
                mat.normalStrength = 0.035f;
                break;
            case MaterialType::EARTH:
                mat.roughness = 0.98f;
                mat.metallic = 0.0f;
                mat.specular = 0.05f;
                mat.reflectivity = 0.006f;
                mat.normalStrength = 0.08f;
                break;
            case MaterialType::SAND:
                mat.roughness = 0.99f;
                mat.metallic = 0.0f;
                mat.specular = 0.05f;
                mat.reflectivity = 0.004f;
                mat.normalStrength = 0.03f;
                break;
            case MaterialType::GLASS:
                mat.roughness = 0.1f;
                mat.metallic = 0.0f;
                mat.specular = 0.7f;
                mat.reflectivity = 0.18f;
                break;
            case MaterialType::ICE:
                mat.roughness = 0.15f;
                mat.metallic = 0.0f;
                mat.specular = 0.6f;
                mat.reflectivity = 0.38f;
                break;
            case MaterialType::MAGIC:
                mat.roughness = 0.4f;
                mat.metallic = 0.0f;
                mat.specular = 0.3f;
                mat.emissiveStrength = 0.5f;
                mat.reflectivity = 0.18f;
                break;
            case MaterialType::FABRIC:
                mat.roughness = 0.96f;
                mat.metallic = 0.0f;
                mat.specular = 0.04f;
                mat.reflectivity = 0.012f;
                mat.normalStrength = 0.025f;
                break;
            case MaterialType::CHARACTER:
                mat.roughness = 0.78f;
                mat.metallic = 0.0f;
                mat.specular = 0.12f;
                mat.reflectivity = 0.025f;
                mat.normalStrength = 0.012f;
                break;
            case MaterialType::CERAMIC:
                mat.roughness = 0.48f;
                mat.metallic = 0.0f;
                mat.specular = 0.25f;
                mat.reflectivity = 0.10f;
                mat.normalStrength = 0.04f;
                break;
            case MaterialType::GENERIC_MATTE:
                mat.roughness = 0.95f;
                mat.metallic = 0.0f;
                mat.specular = 0.04f;
                mat.reflectivity = 0.01f;
                mat.normalStrength = 0.02f;
                break;
            case MaterialType::PROTECTED_2D:
                mat.roughness = 0.5f;
                mat.metallic = 0.0f;
                mat.specular = 0.0f;
                mat.reflectivity = 0.0f;
                break;
            default:
                mat.roughness = 0.5f;
                mat.metallic = 0.0f;
                mat.specular = 0.025f;
                mat.reflectivity = 0.005f;
                mat.normalStrength = 0.015f;
                break;
        }
    }

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars && mat.type != MaterialType::PROTECTED_2D) {
        const std::string typeName = [&mat] {
            switch (mat.type) {
                case MaterialType::STONE: return std::string("Stone");
                case MaterialType::WOOD: return std::string("Wood");
                case MaterialType::METAL: return std::string("Metal");
                case MaterialType::WATER: return std::string("Water");
                case MaterialType::LAVA: return std::string("Lava");
                case MaterialType::EMISSIVE: return std::string("Emissive");
                case MaterialType::GLASS: return std::string("Glass");
                case MaterialType::GRASS: return std::string("Grass");
                case MaterialType::FOLIAGE: return std::string("Foliage");
                case MaterialType::EARTH: return std::string("Earth");
                case MaterialType::SAND: return std::string("Sand");
                case MaterialType::ICE: return std::string("Ice");
                case MaterialType::MAGIC: return std::string("Magic");
                case MaterialType::FABRIC: return std::string("Fabric");
                case MaterialType::CHARACTER: return std::string("Character");
                case MaterialType::CERAMIC: return std::string("Ceramic");
                case MaterialType::SKY: return std::string("Sky");
                case MaterialType::GENERIC_MATTE: return std::string("GenericMatte");
                default: return std::string("GenericMatte");
            }
        }();
        const std::string prefix = "gEnhancements.Graphics.DXR.Materials." + typeName + ".";
        mat.roughness = std::clamp(cvars->GetFloat((prefix + "Roughness").c_str(), mat.roughness), 0.0f, 1.0f);
        mat.metallic = std::clamp(cvars->GetFloat((prefix + "Metallic").c_str(), mat.metallic), 0.0f, 1.0f);
        mat.reflectivity = std::clamp(cvars->GetFloat((prefix + "Reflectivity").c_str(), mat.reflectivity), 0.0f, 1.0f);
        mat.specular = std::clamp(cvars->GetFloat((prefix + "Specular").c_str(), mat.specular), 0.0f, 1.0f);
        mat.normalStrength = std::clamp(cvars->GetFloat((prefix + "NormalStrength").c_str(), mat.normalStrength),
                                        0.0f, 1.0f);
    }

    if (texHash != 0) {
        mMaterialCache[texHash] = mat;
    }
    uint64_t key = MakeKey(info);
    mMaterials[key] = mat;
    mFrameClassifications.push_back(mat.type);
}

void MaterialRegistry::FlushFrame() {
    std::lock_guard<std::mutex> lock(mMutex);
    mFrameDrawCalls.clear();
    mFrameClassifications.clear();
    mFrameCount++;
}

const char* MaterialRegistry::GetProfileName() const {
    switch (static_cast<VisualProfileId>(mProfile)) {
        case VisualProfileId::CLASSIC: return "CLASSIC";
        case VisualProfileId::ENHANCED: return "ENHANCED";
        case VisualProfileId::HYBRID_RT: return "HYBRID_RT";
        case VisualProfileId::PATH_TRACED_EXPERIMENTAL: return "PATH_TRACED_EXPERIMENTAL";
        default: return "CLASSIC";
    }
}

void MaterialRegistry::SetProfile(int profile) {
    profile = std::max(0, std::min(3, profile));
    if (mProfile != profile || !mProfileLogged) {
        mProfile = profile;
        mProfileLogged = true;
        const std::string profileMessage = std::string("[COUCH] VisualProfile = ") + GetProfileName();
        AppendCouchDiag(profileMessage.c_str());
    }
}

void MaterialRegistry::BeginNativeSceneLightingCapture(uint64_t gameFrame, const float ambientColor[3]) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCurrentGameFrame = gameFrame;
    mSceneLighting = SceneLightingSnapshot{};
    std::fill(mSceneLighting.worldDirection, mSceneLighting.worldDirection + 3, 0.0f);
    std::fill(mSceneLighting.directionalColor, mSceneLighting.directionalColor + 3, 0.0f);
    std::fill(mSceneLighting.secondaryWorldDirection, mSceneLighting.secondaryWorldDirection + 3, 0.0f);
    std::fill(mSceneLighting.secondaryDirectionalColor, mSceneLighting.secondaryDirectionalColor + 3, 0.0f);
    mSceneLighting.directionalLightCount = 0;
    mSceneLighting.pointLightCount = 0;
    mSceneLighting.capturedFromScene = true;
    mSceneLighting.productiveGameFrame = gameFrame;
    if (ambientColor) {
        std::copy(ambientColor, ambientColor + 3, mSceneLighting.ambientColor);
    }
    mSceneLightingCapturedThisFrame = false;
    mSceneAmbientCapturedThisFrame = ambientColor != nullptr;
    mSceneDirectionalCapturedThisFrame = false;
    mSceneSecondaryDirectionalCapturedThisFrame = false;
}

void MaterialRegistry::BeginLightingFrame() {
    std::lock_guard<std::mutex> lock(mMutex);
    // Native lighting is captured while the game builds this frame's command
    // list, before the renderer begins executing it. Reset only interpreter
    // observations here; resetting the whole snapshot discarded the original
    // point positions and directions before DXR could consume them.
    mSceneLighting.gameProbeWorldDirection[0] = 0.0f;
    mSceneLighting.gameProbeWorldDirection[1] = 0.0f;
    mSceneLighting.gameProbeWorldDirection[2] = 0.0f;
    std::fill(mSceneLighting.gameProbeColor, mSceneLighting.gameProbeColor + 3, 0.0f);
    mSceneLighting.gameProbeRawLength = 0.0f;
    mSceneLighting.gameProbeLuminance = 0.0f;
    mSceneLighting.gameProbeDirectionalCount = 0;
    mSceneLighting.gameProbePointFlushMax = 0;
    mSceneLighting.gameProbeValid = false;
    mSceneLighting.renderFrame = ++mLightingRenderFrame;
    if (mSceneLighting.capturedFromScene) {
        mSceneLighting.productiveRenderFrame = mSceneLighting.renderFrame;
    }
}

void MaterialRegistry::SetCurrentGameFrame(uint64_t gameFrame) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCurrentGameFrame = gameFrame;
}

void MaterialRegistry::CaptureSceneLighting(const float worldDirection[3],
                                           const float directionalColor[3],
                                           const float ambientColor[3],
                                           const float pointPosition[32][4],
                                           const float pointOptics[32][4],
                                           uint32_t pointLightCount,
                                           bool hasDirectionalLight) {
    if (hasDirectionalLight && (!worldDirection || !directionalColor)) return;
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mSceneLightingCapturedThisFrame && !hasDirectionalLight) {
        // An ambient-only scene should not inherit the unverified fallback sun.
        std::fill(mSceneLighting.directionalColor, mSceneLighting.directionalColor + 3, 0.0f);
    }
    if (ambientColor && !mSceneAmbientCapturedThisFrame) {
        std::copy(ambientColor, ambientColor + 3, mSceneLighting.ambientColor);
        mSceneAmbientCapturedThisFrame = true;
    }
    if (hasDirectionalLight) {
        float directionLengthSquared = 0.0f;
        bool finiteDirectional = true;
        for (int axis = 0; axis < 3; ++axis) {
            finiteDirectional = finiteDirectional && std::isfinite(worldDirection[axis]) &&
                                std::isfinite(directionalColor[axis]);
            directionLengthSquared += worldDirection[axis] * worldDirection[axis];
        }
        const float incomingLuminance = directionalColor[0] * 0.2126f +
                                        directionalColor[1] * 0.7152f +
                                        directionalColor[2] * 0.0722f;
        if (finiteDirectional && directionLengthSquared > 1.0e-8f &&
            std::isfinite(incomingLuminance) && incomingLuminance > 0.0f) {
            struct DirectionalCandidate {
                float direction[3];
                float color[3];
                float luminance;
            };
            DirectionalCandidate candidates[3] = {};
            uint32_t candidateCount = 0;
            auto addCandidate = [&](const float* direction, const float* color) {
                float luminance = color[0] * 0.2126f + color[1] * 0.7152f + color[2] * 0.0722f;
                float lengthSquared = direction[0] * direction[0] +
                                      direction[1] * direction[1] + direction[2] * direction[2];
                if (!std::isfinite(luminance) || luminance <= 0.0f ||
                    !std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f) {
                    return;
                }
                float inverseLength = 1.0f / std::sqrt(lengthSquared);
                float normalized[3] = { direction[0] * inverseLength,
                                        direction[1] * inverseLength,
                                        direction[2] * inverseLength };
                for (uint32_t index = 0; index < candidateCount; ++index) {
                    float alignment = normalized[0] * candidates[index].direction[0] +
                                      normalized[1] * candidates[index].direction[1] +
                                      normalized[2] * candidates[index].direction[2];
                    // Repeated scene/actor draws re-submit the same N64
                    // directional lights. Merge those entries instead of
                    // mistaking duplicate submissions for a second light.
                    if (alignment > 0.9995f) {
                        if (luminance > candidates[index].luminance) {
                            std::copy(normalized, normalized + 3, candidates[index].direction);
                            std::copy(color, color + 3, candidates[index].color);
                            candidates[index].luminance = luminance;
                        }
                        return;
                    }
                }
                if (candidateCount >= 3) return;
                std::copy(normalized, normalized + 3, candidates[candidateCount].direction);
                std::copy(color, color + 3, candidates[candidateCount].color);
                candidates[candidateCount].luminance = luminance;
                ++candidateCount;
            };
            if (mSceneDirectionalCapturedThisFrame) {
                addCandidate(mSceneLighting.worldDirection, mSceneLighting.directionalColor);
            }
            if (mSceneSecondaryDirectionalCapturedThisFrame) {
                addCandidate(mSceneLighting.secondaryWorldDirection,
                             mSceneLighting.secondaryDirectionalColor);
            }
            addCandidate(worldDirection, directionalColor);

            for (uint32_t left = 0; left < candidateCount; ++left) {
                for (uint32_t right = left + 1; right < candidateCount; ++right) {
                    if (candidates[right].luminance > candidates[left].luminance) {
                        std::swap(candidates[left], candidates[right]);
                    }
                }
            }
            if (candidateCount > 0) {
                std::copy(candidates[0].direction, candidates[0].direction + 3,
                          mSceneLighting.worldDirection);
                std::copy(candidates[0].color, candidates[0].color + 3,
                          mSceneLighting.directionalColor);
                mSceneDirectionalCapturedThisFrame = true;
            }
            if (candidateCount > 1) {
                std::copy(candidates[1].direction, candidates[1].direction + 3,
                          mSceneLighting.secondaryWorldDirection);
                std::copy(candidates[1].color, candidates[1].color + 3,
                          mSceneLighting.secondaryDirectionalColor);
                mSceneSecondaryDirectionalCapturedThisFrame = true;
            } else {
                std::fill(mSceneLighting.secondaryWorldDirection,
                          mSceneLighting.secondaryWorldDirection + 3, 0.0f);
                std::fill(mSceneLighting.secondaryDirectionalColor,
                          mSceneLighting.secondaryDirectionalColor + 3, 0.0f);
                mSceneSecondaryDirectionalCapturedThisFrame = false;
            }
            mSceneLighting.directionalLightCount = (mSceneDirectionalCapturedThisFrame ? 1u : 0u) +
                                                   (mSceneSecondaryDirectionalCapturedThisFrame ? 1u : 0u);
            mSceneLighting.productiveGameFrame = mCurrentGameFrame;
            mSceneLighting.productiveRenderFrame = mLightingRenderFrame;
        }
    }
    for (int point = 0; point < 32; ++point) {
        if (!pointPosition || !pointOptics) break;
        if (static_cast<uint32_t>(point) >= pointLightCount || mSceneLighting.pointLightCount >= 32) continue;
        // Keep the scene-wide light budget for lights that can contribute.
        // The interpreter filters these too for Fast3D's two per-draw slots,
        // but the registry is the authority that merges lights across draws.
        if (pointOptics[point][1] <= 0.0f && pointOptics[point][2] <= 0.0f &&
            pointOptics[point][3] <= 0.0f) {
            continue;
        }
        bool finiteLight = true;
        for (int component = 0; component < 4; ++component) {
            finiteLight = finiteLight && std::isfinite(pointPosition[point][component]) &&
                          std::isfinite(pointOptics[point][component]);
        }
        if (!finiteLight) continue;
        bool duplicate = false;
        for (uint32_t existing = 0; existing < mSceneLighting.pointLightCount; ++existing) {
            bool sameLight = true;
            for (int component = 0; component < 4; ++component) {
                if (std::fabs(mSceneLighting.pointPosition[existing][component] - pointPosition[point][component]) > 0.01f ||
                    std::fabs(mSceneLighting.pointOptics[existing][component] - pointOptics[point][component]) > 1.0e-4f) {
                    sameLight = false;
                    break;
                }
            }
            if (sameLight) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        const uint32_t destination = mSceneLighting.pointLightCount++;
        std::copy(pointPosition[point], pointPosition[point] + 4, mSceneLighting.pointPosition[destination]);
        std::copy(pointOptics[point], pointOptics[point] + 4, mSceneLighting.pointOptics[destination]);
    }
    mSceneLighting.capturedFromScene = true;
    mSceneLightingCapturedThisFrame = true;
}

SceneLightingSnapshot MaterialRegistry::GetSceneLighting() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSceneLighting;
}

// DXR TRUTH MODE (V2): read-only game-light probe. Never alters the
// productive snapshot fields (worldDirection/directionalColor/counts).
void MaterialRegistry::NoteGameDirectionalProbe(const float worldDirection[3], const float color[3],
                                                float rawLength, float luminance) {
    if (!worldDirection || !color) return;
    if (!std::isfinite(rawLength) || rawLength <= 1.0e-6f) return;
    if (!std::isfinite(luminance) || luminance <= 0.0f) return;
    float invLen = 1.0f / rawLength;
    float normalized[3] = { worldDirection[0] * invLen, worldDirection[1] * invLen,
                            worldDirection[2] * invLen };
    if (!std::isfinite(normalized[0]) || !std::isfinite(normalized[1]) ||
        !std::isfinite(normalized[2])) return;
    std::lock_guard<std::mutex> lock(mMutex);
    bool distinct = true;
    if (mSceneLighting.gameProbeValid) {
        const float* prev = mSceneLighting.gameProbeWorldDirection;
        const float alignment = normalized[0] * prev[0] + normalized[1] * prev[1] +
                                normalized[2] * prev[2];
        if (alignment > 0.9995f) {
            distinct = false;
            if (luminance <= mSceneLighting.gameProbeLuminance) return;
        }
    }
    if (distinct) ++mSceneLighting.gameProbeDirectionalCount;
    std::copy(normalized, normalized + 3, mSceneLighting.gameProbeWorldDirection);
    std::copy(color, color + 3, mSceneLighting.gameProbeColor);
    mSceneLighting.gameProbeGameFrame = mCurrentGameFrame;
    mSceneLighting.gameProbeRenderFrame = mLightingRenderFrame;
    mSceneLighting.gameProbeRawLength = rawLength;
    mSceneLighting.gameProbeLuminance = luminance;
    mSceneLighting.gameProbeValid = true;
}

void MaterialRegistry::NoteGamePointProbe(uint32_t perFlushPointCount) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (perFlushPointCount > mSceneLighting.gameProbePointFlushMax)
        mSceneLighting.gameProbePointFlushMax = perFlushPointCount;
}

extern "C" void CouchCaptureNativeLight(int type, float x, float y, float z, uint32_t gameFrame,
                                          float red, float green, float blue, float radius) {
    MaterialRegistry::Instance().SetCurrentGameFrame(gameFrame);
    float direction[3] = { x, y, z };
    float color[3] = { red / 255.0f, green / 255.0f, blue / 255.0f };
    if (type == 1) {
        MaterialRegistry::Instance().CaptureSceneLighting(
            direction, color, nullptr, nullptr, nullptr, 0, true);
        return;
    }
    if (type != 0 && type != 2) return;

    const float effectiveRadius = std::fabs(radius);
    if (!std::isfinite(effectiveRadius) || effectiveRadius <= 0.0f) return;
    const float noDirection[3] = {};
    const float noColor[3] = {};
    float pointPosition[32][4] = {};
    float pointOptics[32][4] = {};
    pointPosition[0][0] = x;
    pointPosition[0][1] = y;
    pointPosition[0][2] = z;
    pointPosition[0][3] = effectiveRadius;
    pointOptics[0][1] = color[0];
    pointOptics[0][2] = color[1];
    pointOptics[0][3] = color[2];
    MaterialRegistry::Instance().CaptureSceneLighting(
        noDirection, noColor, nullptr, pointPosition, pointOptics, 1, false);
}

extern "C" void CouchBeginNativeLightCapture(uint32_t gameFrame, float ambientRed,
                                               float ambientGreen, float ambientBlue) {
    const float ambientColor[3] = { ambientRed / 255.0f, ambientGreen / 255.0f,
                                    ambientBlue / 255.0f };
    MaterialRegistry::Instance().BeginNativeSceneLightingCapture(gameFrame, ambientColor);
}

void MaterialRegistry::LoadRegistry(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        const nlohmann::json root = nlohmann::json::parse(file);
        if (!root.is_object() || root.value("version", 0) != 1 ||
            !root.contains("materials") || !root["materials"].is_object()) return;

        std::unordered_map<std::string, MaterialDefinition> loaded;
        for (const auto& entry : root["materials"].items()) {
            if (entry.key().empty() || !entry.value().is_object()) continue;
            const auto& value = entry.value();
            if (!value.contains("type") || !value["type"].is_number_integer()) continue;
            const int type = value["type"].get<int>();
            if (type < 0 || type > static_cast<int>(MaterialType::UNKNOWN)) continue;

            MaterialDefinition material;
            material.type = static_cast<MaterialType>(type);
            const auto readUnit = [&value](const char* key, float fallback) {
                if (!value.contains(key) || !value[key].is_number()) return fallback;
                const float number = value[key].get<float>();
                return std::isfinite(number) ? std::clamp(number, 0.0f, 1.0f) : fallback;
            };
            material.roughness = readUnit("roughness", material.roughness);
            material.metallic = readUnit("metallic", material.metallic);
            material.specular = readUnit("specular", material.specular);
            material.reflectivity = readUnit("reflectivity", material.reflectivity);
            material.normalStrength = readUnit("normalStrength", material.normalStrength);
            material.wetness = readUnit("wetness", material.wetness);
            material.subsurfaceHint = readUnit("subsurfaceHint", material.subsurfaceHint);
            if (value.contains("emissiveStrength") && value["emissiveStrength"].is_number()) {
                const float strength = value["emissiveStrength"].get<float>();
                if (std::isfinite(strength)) material.emissiveStrength = std::clamp(strength, 0.0f, 100.0f);
            }
            if (value.contains("flags") && value["flags"].is_number_unsigned() &&
                value["flags"].get<uint64_t>() <= std::numeric_limits<uint32_t>::max()) {
                material.flags = value["flags"].get<uint32_t>();
            }
            loaded.emplace(entry.key(), material);
        }

        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& entry : loaded) {
            mAuthoredOverrides[entry.first] = entry.second;
            mStringRules[entry.first] = entry.second;
        }
        mMaterialCache.clear();
        mMaterials.clear();
    } catch (const nlohmann::json::exception&) {
        // Keep the current registry intact if the authored file is malformed.
    }
}

void MaterialRegistry::SaveRegistry(const std::string& path) const {
    nlohmann::json root = { { "version", 1 }, { "materials", nlohmann::json::object() } };
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& entry : mAuthoredOverrides) {
            const auto& material = entry.second;
            root["materials"][entry.first] = {
                { "type", static_cast<int>(material.type) },
                { "roughness", material.roughness }, { "metallic", material.metallic },
                { "specular", material.specular }, { "reflectivity", material.reflectivity },
                { "emissiveStrength", material.emissiveStrength },
                { "normalStrength", material.normalStrength }, { "wetness", material.wetness },
                { "subsurfaceHint", material.subsurfaceHint }, { "flags", material.flags }
            };
        }
    }

    const std::filesystem::path destination(path);
    const auto parent = destination.parent_path();
    std::error_code error;
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return;
    }
    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;
    file << root.dump(2) << '\n';
    file.flush();
}

void MaterialRegistry::WriteDrawCallInventory(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mMutex);

    std::ofstream file(path);
    if (!file.is_open()) return;

    file << "frame,draw_index,texture_hash,combine_mode,geometry_mode,other_mode_l,other_mode_h,"
         << "prim_r,prim_g,prim_b,prim_a,env_r,env_g,env_b,env_a,fog_mul,fog_offset,"
         << "is_rect,is_2d,classification\n";

    size_t index = 0;
    for (const auto& dc : mFrameDrawCalls) {
        MaterialType matType = (index < mFrameClassifications.size()) ? mFrameClassifications[index] : MaterialType::UNKNOWN;
        const char* typeName = "UNKNOWN";
        switch (matType) {
            case MaterialType::CLASSIC: typeName = "CLASSIC"; break;
            case MaterialType::STONE: typeName = "STONE"; break;
            case MaterialType::WOOD: typeName = "WOOD"; break;
            case MaterialType::METAL: typeName = "METAL"; break;
            case MaterialType::WATER: typeName = "WATER"; break;
            case MaterialType::GLASS: typeName = "GLASS"; break;
            case MaterialType::FABRIC: typeName = "FABRIC"; break;
            case MaterialType::EARTH: typeName = "EARTH"; break;
            case MaterialType::GRASS: typeName = "GRASS"; break;
            case MaterialType::FOLIAGE: typeName = "FOLIAGE"; break;
            case MaterialType::SAND: typeName = "SAND"; break;
            case MaterialType::LAVA: typeName = "LAVA"; break;
            case MaterialType::ICE: typeName = "ICE"; break;
            case MaterialType::MAGIC: typeName = "MAGIC"; break;
            case MaterialType::EMISSIVE: typeName = "EMISSIVE"; break;
            case MaterialType::SKY: typeName = "SKY"; break;
            case MaterialType::CHARACTER: typeName = "CHARACTER"; break;
            case MaterialType::PROTECTED_2D: typeName = "PROTECTED_2D"; break;
            default: typeName = "UNKNOWN"; break;
        }

        file << mFrameCount << "," << index << ","
             << dc.textureHash << "," << dc.combineMode << ","
             << dc.geometryMode << "," << dc.otherModeL << "," << dc.otherModeH << ","
             << int(dc.primR) << "," << int(dc.primG) << "," << int(dc.primB) << "," << int(dc.primA) << ","
             << int(dc.envR) << "," << int(dc.envG) << "," << int(dc.envB) << "," << int(dc.envA) << ","
             << dc.fogMul << "," << dc.fogOffset << ","
             << dc.isRect << "," << dc.is2D << ","
             << typeName << "\n";

        index++;
    }
    file.flush();
}

} // namespace Fast
