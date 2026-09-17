#include "fast/MaterialRegistry.h"
#include "fast/MaterialRulesPilot.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace Fast {

MaterialRegistry& MaterialRegistry::Instance() {
    static MaterialRegistry instance;
    return instance;
}

static void AppendCouchDiag(const char* msg) {
    printf("%s\n", msg);
    fflush(stdout);
    FILE* f = fopen("logs/couch_diagnostics.txt", "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

void MaterialRegistry::Init() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mInitialized) return;
    mInitialized = true;
    PopulateMaterialRulesTable(mExactRules, mStringRules);
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
    uint64_t key = info.combineMode;
    key ^= (uint64_t(info.geometryMode) << 32);
    key ^= (uint64_t(info.otherModeL) << 16);
    key ^= (uint64_t(info.textureHash) << 48);
    return key;
}

MaterialType MaterialRegistry::ClassifyFromState(const DrawCallInfo& info) const {
    if (info.is2D || info.isRect) {
        return MaterialType::PROTECTED_2D;
    }

    bool hasAlpha = (info.otherModeL & (3 << 20)) == (0x04 << 20) &&
                    (info.otherModeL & (3 << 16)) == (0x01 << 16);
    bool hasTextureEdge = (info.otherModeL & 0x00001000) != 0;
    bool use2Cycle = (info.otherModeH & (3 << 20)) == (0x08 << 20);
    bool hasNoise = (info.otherModeL & (3 << 28)) == (0x03 << 28);
    bool hasFog = (info.otherModeL >> 30) == 0x01;
    bool hasLighting = (info.geometryMode & 0x00010000) != 0;

    if (hasNoise || use2Cycle) {
        return MaterialType::MAGIC;
    }

    float avgPrim = (info.primR + info.primG + info.primB) / 3.0f;
    float avgEnv = (info.envR + info.envG + info.envB) / 3.0f;

    if (info.envR > 200 && info.envG < 50 && info.envB < 50) {
        return MaterialType::LAVA;
    }
    if (info.envR < 50 && info.envG < 50 && info.envB > 200) {
        return MaterialType::WATER;
    }
    if (info.envR > 150 && info.envG > 150 && info.envB > 150 &&
        info.primR > 150 && info.primG > 150 && info.primB > 150) {
        return MaterialType::EMISSIVE;
    }

    return MaterialType::UNKNOWN;
}

MaterialType MaterialRegistry::ClassifyMaterial(const DrawCallInfo& info) const {
    return ClassifyFromState(info);
}

const MaterialDefinition* MaterialRegistry::GetMaterial(uint64_t key) const {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mMaterials.find(key);
    if (it != mMaterials.end()) {
        return &it->second;
    }
    return nullptr;
}

void MaterialRegistry::RecordDrawCall(const DrawCallInfo& info) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mInitialized) {
        mInitialized = true;
        PopulateMaterialRulesTable(mExactRules, mStringRules);
    }

    mTotalDrawCalls++;
    mCounters.materialDrawCallCount++;
    mFrameDrawCalls.push_back(info);

    // Priority 1: UI Protection
    if (info.is2D || info.isRect) {
        mUiProtectedCount++;
        mFrameClassifications.push_back(MaterialType::PROTECTED_2D);
        return;
    }

    // Check material cache first (fast path)
    uint64_t texHash = info.textureHash;
    if (texHash != 0) {
        auto itCache = mMaterialCache.find(texHash);
        if (itCache != mMaterialCache.end()) {
            mCacheHits++;
            mFrameClassifications.push_back(itCache->second.type);
            return;
        }
    }
    mCacheMisses++;

    MaterialDefinition mat;
    bool foundExact = false;
    if (texHash != 0) {
        auto itRule = mExactRules.find(texHash);
        if (itRule != mExactRules.end()) {
            mat = itRule->second;
            foundExact = true;
            mRulesActivated++;
        }
    }

    if (!foundExact) {
        mat.type = ClassifyFromState(info);
        if (mat.type == MaterialType::UNKNOWN || mat.type == MaterialType::CLASSIC) {
            mUnknownCount++;
        }
        switch (mat.type) {
            case MaterialType::STONE:
                mat.roughness = 0.85f;
                mat.metallic = 0.0f;
                mat.specular = 0.25f;
                break;
            case MaterialType::WOOD:
                mat.roughness = 0.75f;
                mat.metallic = 0.0f;
                mat.specular = 0.15f;
                break;
            case MaterialType::METAL:
                mat.roughness = 0.35f;
                mat.metallic = 0.9f;
                mat.specular = 0.8f;
                break;
            case MaterialType::WATER:
                mat.roughness = 0.05f;
                mat.metallic = 0.0f;
                mat.specular = 0.5f;
                break;
            case MaterialType::LAVA:
                mat.roughness = 0.3f;
                mat.metallic = 0.0f;
                mat.emissiveStrength = 0.85f;
                mat.specular = 0.2f;
                break;
            case MaterialType::EMISSIVE:
                mat.roughness = 0.5f;
                mat.metallic = 0.0f;
                mat.emissiveStrength = 0.8f;
                mat.specular = 0.1f;
                break;
            case MaterialType::GRASS:
                mat.roughness = 0.9f;
                mat.metallic = 0.0f;
                mat.specular = 0.08f;
                break;
            case MaterialType::FOLIAGE:
                mat.roughness = 0.8f;
                mat.metallic = 0.0f;
                mat.specular = 0.1f;
                break;
            case MaterialType::EARTH:
                mat.roughness = 0.95f;
                mat.metallic = 0.0f;
                mat.specular = 0.05f;
                break;
            case MaterialType::SAND:
                mat.roughness = 0.95f;
                mat.metallic = 0.0f;
                mat.specular = 0.05f;
                break;
            case MaterialType::GLASS:
                mat.roughness = 0.1f;
                mat.metallic = 0.0f;
                mat.specular = 0.7f;
                break;
            case MaterialType::ICE:
                mat.roughness = 0.15f;
                mat.metallic = 0.0f;
                mat.specular = 0.6f;
                break;
            case MaterialType::MAGIC:
                mat.roughness = 0.4f;
                mat.metallic = 0.0f;
                mat.specular = 0.3f;
                mat.emissiveStrength = 0.5f;
                break;
            case MaterialType::PROTECTED_2D:
                mat.roughness = 0.5f;
                mat.metallic = 0.0f;
                mat.specular = 0.0f;
                break;
            default:
                mat.type = MaterialType::UNKNOWN;
                mat.roughness = 0.5f;
                mat.metallic = 0.0f;
                mat.specular = 0.0f;
                break;
        }
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

void MaterialRegistry::SetProfile(int profile) {
    if (mProfile != profile || !mProfileLogged) {
        mProfile = profile;
        mProfileLogged = true;
        if (profile == 1) {
            AppendCouchDiag("[COUCH] VisualProfile = ENHANCED");
        } else {
            AppendCouchDiag("[COUCH] VisualProfile = CLASSIC");
        }
    }
}

void MaterialRegistry::LoadRegistry(const std::string& path) {
    // TODO: JSON loading for material definitions
}

void MaterialRegistry::SaveRegistry(const std::string& path) const {
    // TODO: JSON saving for material definitions
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
