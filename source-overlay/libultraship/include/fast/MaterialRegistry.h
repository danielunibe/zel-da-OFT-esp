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
    PROTECTED_2D,
    UNKNOWN
};

struct MaterialDefinition {
    MaterialType type = MaterialType::CLASSIC;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float specular = 0.0f;
    float emissiveStrength = 0.0f;
    float normalStrength = 0.0f;
    float wetness = 0.0f;
    float subsurfaceHint = 0.0f;
    uint32_t flags = 0;
};

struct DrawCallInfo {
    uint64_t textureHash;
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

    const MaterialDefinition* GetMaterial(uint64_t key) const;
    MaterialType ClassifyMaterial(const DrawCallInfo& info) const;

    void SetProfile(int profile); // 0=CLASSIC, 1=ENHANCED
    int GetProfile() const { return mProfile; }

    void LoadRegistry(const std::string& path);
    void SaveRegistry(const std::string& path) const;

    const std::vector<DrawCallInfo>& GetCurrentFrameDrawCalls() const { return mFrameDrawCalls; }
    const std::vector<MaterialType>& GetCurrentFrameClassifications() const { return mFrameClassifications; }

    static bool IsEnhancedVisualProfile() { return Instance().GetProfile() == 1; }

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
    std::unordered_map<uint64_t, MaterialDefinition> mMaterials;
    std::unordered_map<uint64_t, MaterialDefinition> mExactRules;
    std::unordered_map<std::string, MaterialDefinition> mStringRules;
    std::unordered_map<uint64_t, MaterialDefinition> mMaterialCache;
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
