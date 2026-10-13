#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace Fast {

enum class MaterialType : uint8_t {
    CLASSIC = 0,
    STONE,
    WOOD,
    METAL,
    WATER,
    GLASS,
    FABRIC,
    EARTH,
    GRASS,
    FOLIAGE,
    SAND,
    LAVA,
    ICE,
    MAGIC,
    EMISSIVE,
    SKY,
    CHARACTER,
    CERAMIC,
    GENERIC_MATTE,
    PROTECTED_2D,
    UNKNOWN
};

// Classic and Enhanced preserve the original presentation. The two RTX tiers
// require a supported DXR adapter; path lighting is an opt-in experimental
// profile exposed in the graphics settings.
enum class VisualProfileId : int {
    CLASSIC = 0,
    ENHANCED = 1,
    HYBRID_RT = 2,
    PATH_TRACED_EXPERIMENTAL = 3,
};

struct MaterialDefinition {
    MaterialType type = MaterialType::CLASSIC;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float specular = 0.0f;
    float reflectivity = 0.0f;
    float emissiveStrength = 0.0f;
    float normalStrength = 0.0f;
    float wetness = 0.0f;
    float subsurfaceHint = 0.0f;
    uint32_t flags = 0;
};

inline constexpr uint32_t MATERIAL_FLAG_ALBEDO_TEXTURE_VALID = 1u << 0;
// Opt in only when the authored albedo intentionally encodes relief detail.
inline constexpr uint32_t MATERIAL_FLAG_ALBEDO_AS_HEIGHT = 1u << 1;

struct SceneLightingSnapshot {
    float worldDirection[3] = { 0.22f, 0.94f, 0.26f };
    float directionalColor[3] = { 1.0f, 1.0f, 1.0f };
    float secondaryWorldDirection[3] = {};
    float secondaryDirectionalColor[3] = {};
    float ambientColor[3] = { 0.12f, 0.12f, 0.12f };
    float pointPosition[32][4] = {}; // world-space xyz, w: authored influence radius
    float pointOptics[32][4] = {};   // x: reserved, yzw: linear RGB color
    uint32_t directionalLightCount = 0;
    uint32_t pointLightCount = 0;
    bool capturedFromScene = false;
    uint64_t gameProbeGameFrame = 0;
    uint64_t gameProbeRenderFrame = 0;
    uint64_t productiveGameFrame = 0;
    uint64_t productiveRenderFrame = 0;
    uint64_t renderFrame = 0;
    // DXR TRUTH MODE (V2) game-light probe: read-only observation of the real
    // OoT/Fast3D directionals captured per draw-flush by the interpreter
    // (brightest-by-luminance wins across flushes of the frame). This probe is
    // consumed ONLY by the GAME_LIGHT truth view + provenance log; it never
    // feeds productive lighting (fc.reservedLight/sceneLightColor paths above
    // are untouched). Reset every frame by BeginLightingFrame (snapshot {}).
    float gameProbeWorldDirection[3] = {};
    float gameProbeColor[3] = {};
    float gameProbeRawLength = 0.0f; // |dir| BEFORE interpreter normalize
    float gameProbeLuminance = 0.0f;
    uint32_t gameProbeDirectionalCount = 0; // distinct dirs seen this frame
    uint32_t gameProbePointFlushMax = 0;    // max per-flush point count seen
    bool gameProbeValid = false;
};

struct DrawCallInfo {
    uint64_t textureHash;
    std::string resourcePath;
    uint64_t combineMode;
    uint32_t geometryMode;
    uint32_t otherModeL;
    uint32_t otherModeH;
    uint8_t primR, primG, primB, primA;
    uint8_t envR, envG, envB, envA;
    uint16_t fogMul, fogOffset;
    bool isRect;
    bool is2D;
};

class MaterialRegistry {
public:
    static MaterialRegistry& Instance();

    void Init();
    void RecordDrawCall(const DrawCallInfo& info);
    void FlushFrame();

    MaterialDefinition GetMaterial(uint64_t key) const;
    MaterialDefinition GetMaterialForTexture(uint64_t textureHash) const;
    void SetTextureSrvIndex(uint64_t textureHash, uint32_t srvIndex);
    uint32_t GetTextureSrvIndex(uint64_t textureHash) const;
    uint64_t GetMaterialId(uint64_t key) const {
        return static_cast<uint64_t>(GetMaterial(key).type);
    }
    MaterialType ClassifyMaterial(const DrawCallInfo& info) const;

    void SetProfile(int profile); // 0=CLASSIC, 1=ENHANCED, 2=HYBRID_RT, 3=PATH_TRACED_EXPERIMENTAL
    void BeginLightingFrame();
    void SetCurrentGameFrame(uint64_t gameFrame);
    void CaptureSceneLighting(const float worldDirection[3], const float directionalColor[3],
                              const float ambientColor[3], const float pointPosition[32][4],
                              const float pointOptics[32][4], uint32_t pointLightCount,
                              bool hasDirectionalLight);
    SceneLightingSnapshot GetSceneLighting() const;
    // DXR TRUTH MODE (V2) instrumentation only. Records one interpreter-side
    // directional (worldDirection claim: WORLD space, surface->light
    // convention, same as physicalDraw.viewLightDirection pre-transform).
    // Keeps the brightest (luminance) across flushes; counts distinct dirs.
    void NoteGameDirectionalProbe(const float worldDirection[3], const float color[3],
                                  float rawLength, float luminance);
    void NoteGamePointProbe(uint32_t perFlushPointCount);
    int GetProfile() const { return mProfile; }
    const char* GetProfileName() const;

    void LoadRegistry(const std::string& path);
    void SaveRegistry(const std::string& path) const;

    const std::vector<DrawCallInfo>& GetCurrentFrameDrawCalls() const { return mFrameDrawCalls; }
    const std::vector<MaterialType>& GetCurrentFrameClassifications() const { return mFrameClassifications; }

    static bool IsEnhancedVisualProfile() { return Instance().GetProfile() >= static_cast<int>(VisualProfileId::ENHANCED); }
    static bool IsHybridRtProfile() {
        const int profile = Instance().GetProfile();
        return profile == static_cast<int>(VisualProfileId::HYBRID_RT) ||
               profile == static_cast<int>(VisualProfileId::PATH_TRACED_EXPERIMENTAL);
    }
    static bool IsPathTracedExperimentalProfile() {
        return Instance().GetProfile() == static_cast<int>(VisualProfileId::PATH_TRACED_EXPERIMENTAL);
    }

    void WriteDrawCallInventory(const std::string& path) const;

    // Metrics for reporting and validation
    uint64_t GetTotalDrawCalls() const { return mTotalDrawCalls; }
    uint64_t GetCacheHits() const { return mCacheHits; }
    uint64_t GetCacheMisses() const { return mCacheMisses; }
    float GetCacheHitRate() const {
        uint64_t total = mCacheHits + mCacheMisses;
        return total > 0 ? (float)mCacheHits / (float)total : 1.0f;
    }
    uint64_t GetRulesActivatedCount() const { return mRulesActivated; }
    uint64_t GetUiProtectedCount() const { return mUiProtectedCount; }
    uint64_t GetUnknownCount() const { return mUnknownCount; }
    uint64_t GetExactRulesLoadedCount() const { return mExactRules.size(); }
    void ResetMetrics();

    struct CouchCounters {
        uint64_t framesRendered = 0;
        uint64_t enhancedFrames = 0;
        uint64_t tonemapPassCount = 0;
        uint64_t atmosphericPassCount = 0;
        uint64_t materialDrawCallCount = 0;
    };
    CouchCounters& GetCounters() { return mCounters; }
    const CouchCounters& GetCounters() const { return mCounters; }

    static void IncrTonemapPass() { Instance().mCounters.tonemapPassCount++; }
    static void IncrAtmosphericPass() { Instance().mCounters.atmosphericPassCount++; }
    static void IncrFrame(bool enhanced) {
        Instance().mCounters.framesRendered++;
        if (enhanced) Instance().mCounters.enhancedFrames++;
    }
    static void IncrDrawCall() { Instance().mCounters.materialDrawCallCount++; }

private:
    MaterialRegistry() = default;
    uint64_t MakeKey(const DrawCallInfo& info) const;
    MaterialType ClassifyFromState(const DrawCallInfo& info) const;

    int mProfile = 0;
    bool mProfileLogged = false;
    bool mInitialized = false;
    mutable std::mutex mMutex;
    CouchCounters mCounters;
    SceneLightingSnapshot mSceneLighting;
    uint64_t mCurrentGameFrame = 0;
    uint64_t mLightingRenderFrame = 0;
    bool mSceneLightingCapturedThisFrame = false;
    bool mSceneAmbientCapturedThisFrame = false;
    bool mSceneDirectionalCapturedThisFrame = false;
    bool mSceneSecondaryDirectionalCapturedThisFrame = false;
    std::unordered_map<uint64_t, MaterialDefinition> mMaterials;
    std::unordered_map<uint64_t, MaterialDefinition> mExactRules;
    std::unordered_map<std::string, MaterialDefinition> mStringRules;
    std::unordered_map<std::string, MaterialDefinition> mAuthoredOverrides;
    std::unordered_map<uint64_t, MaterialDefinition> mMaterialCache;
    std::unordered_map<uint64_t, uint32_t> mTextureSrvIndices;
    std::vector<DrawCallInfo> mFrameDrawCalls;
    std::vector<MaterialType> mFrameClassifications;
    uint64_t mFrameCount = 0;

    // Running metrics
    uint64_t mTotalDrawCalls = 0;
    uint64_t mCacheHits = 0;
    uint64_t mCacheMisses = 0;
    uint64_t mRulesActivated = 0;
    uint64_t mUiProtectedCount = 0;
    uint64_t mUnknownCount = 0;
};

} // namespace Fast
