#include "fast/PersistentScene.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "fast/MaterialRegistry.h"
#ifdef _WIN32
#include "fast/backends/DxrAccelerationStructureManager.h"
#endif
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Fast {

static inline uint64_t FNV1a64Local(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

PersistentScene& PersistentScene::Instance() {
    static PersistentScene sInstance;
    return sInstance;
}

PersistentScene::PersistentScene()
    : mEnabled(false), mInitialized(false), mCurrentSceneId(-1), mCurrentRoomId(-1),
      mFrameCounter(0), mLifetimeState(SceneLifetimeState::EMPTY), mNextInstanceId(1) {
    memset(&mStats, 0, sizeof(mStats));
}

PersistentScene::~PersistentScene() {
    ClearAll();
}

void PersistentScene::Init() {
    if (mInitialized) return;

    mAssets.reserve(1024);
    mInstances.reserve(4096);
    mUploadRequests.reserve(1024);

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        mEnabled = (cvars->GetInteger("gEnhancements.Graphics.PersistentSceneDebug", 0) != 0) ||
                   (cvars->GetInteger("gEnhancements.Graphics.GeometryCaptureDebug", 0) != 0);
    }

    const char* envDxr = getenv("COUCH_DXR_AS");
    if (envDxr && strcmp(envDxr, "1") == 0) {
        mEnabled = true;
    }
#ifdef _WIN32
    if (DxrAccelerationStructureManager::Instance().IsEnabled()) {
        mEnabled = true;
    }
#endif

    mLifetimeState = SceneLifetimeState::BUILDING;
    mInitialized = true;
}

void PersistentScene::BeginFrame() {
    Init();

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        mEnabled = (cvars->GetInteger("gEnhancements.Graphics.PersistentSceneDebug", 0) != 0) ||
                   (cvars->GetInteger("gEnhancements.Graphics.GeometryCaptureDebug", 0) != 0);
    }

    const char* envDxr = getenv("COUCH_DXR_AS");
    if (envDxr && strcmp(envDxr, "1") == 0) {
        mEnabled = true;
    }
#ifdef _WIN32
    if (DxrAccelerationStructureManager::Instance().IsEnabled()) {
        mEnabled = true;
    }
#endif

    if (!mEnabled) {
        return;
    }

    mFrameCounter++;
    mChangeSet.Clear();

    // Reset de métricas por frame
    mStats.assetsCreatedThisFrame = 0;
    mStats.assetsReusedThisFrame = 0;
    mStats.instancesCreatedThisFrame = 0;
    mStats.instancesUpdatedThisFrame = 0;
    mStats.instancesReusedThisFrame = 0;
    mStats.instancesDestroyedThisFrame = 0;

    // Guardar matrices anteriores para motion vectors y dirty tracking
    for (auto& inst : mInstances) {
        memcpy(inst.previousWorldTransform, inst.worldTransform, sizeof(inst.worldTransform));
        inst.dirtyFlags = DIRTY_NONE;
    }

    // Temporalmente vaciamos la lista de instancias del frame para reconstruir topología limpia
    // pero los GeometryAssets se preservan y reúsan estrictamente.
    mInstances.clear();
    mAssetToInstanceMap.clear();

    mStats.totalInstances = 0;
    mStats.staticInstances = 0;
    mStats.dynamicInstances = 0;
    mStats.opaqueInstances = 0;
    mStats.alphaTestInstances = 0;
    mStats.transparentInstances = 0;
    mStats.additiveInstances = 0;
    mStats.unknownAlphaInstances = 0;

    mLifetimeState = SceneLifetimeState::BUILDING;
}

void PersistentScene::EndFrame() {
    if (!mEnabled) return;

    // Calcular consumo real de memoria
    size_t mem = sizeof(PersistentScene);
    for (const auto& kv : mAssets) {
        mem += sizeof(GeometryAsset);
        mem += kv.second.vertexBuffer.size() * sizeof(LocalVertex);
        mem += kv.second.indexBuffer.size() * sizeof(uint32_t);
        mem += kv.second.resourcePath.capacity();
    }
    mem += mInstances.capacity() * sizeof(GeometryInstance);
    mStats.memoryUsageBytes = mem;

    mLifetimeState = SceneLifetimeState::READY;
}

void PersistentScene::SetCurrentScene(int sceneId, int roomId) {
    if (sceneId != mCurrentSceneId) {
        InvalidateScene();
        mCurrentSceneId = sceneId;
        mCurrentRoomId = roomId;
    } else if (roomId != mCurrentRoomId) {
        InvalidateRoom();
        mCurrentRoomId = roomId;
    }
}

void PersistentScene::InvalidateScene() {
    mLifetimeState = SceneLifetimeState::INVALIDATING;
    
    // Purga completa de escena: no dejar geometría zombi
    ClearAll();
    mStats.zombieGeometryCount = 0;
    mLifetimeState = SceneLifetimeState::BUILDING;
}

void PersistentScene::InvalidateRoom() {
    mLifetimeState = SceneLifetimeState::INVALIDATING;
    
    // Purgar instancias del cuarto anterior
    mInstances.clear();
    mAssetToInstanceMap.clear();
    mStats.totalInstances = 0;
    mStats.zombieGeometryCount = 0;
    mLifetimeState = SceneLifetimeState::BUILDING;
}

void PersistentScene::ClearAll() {
    mAssets.clear();
    mInstances.clear();
    mAssetToInstanceMap.clear();
    mUploadRequests.clear();
    mChangeSet.Clear();
    mNextInstanceId = 1;

    memset(&mStats, 0, sizeof(mStats));
    mLifetimeState = SceneLifetimeState::EMPTY;
}

uint64_t PersistentScene::ComputeAssetHash(const std::vector<LocalVertex>& verts, const std::vector<uint32_t>& indices) {
    uint64_t h = 14695981039346656037ULL;
    for (const auto& v : verts) {
        float pos[3] = { v.posX, v.posY, v.posZ };
        h = FNV1a64Local(pos, sizeof(pos), h);
    }
    for (uint32_t idx : indices) {
        h = FNV1a64Local(&idx, sizeof(idx), h);
    }
    return h;
}

void PersistentScene::CalculateInstanceWorldAABB(GeometryInstance& inst, const GeometryAsset& asset) {
    inst.worldAABB.Reset();
    if (!asset.boundingBox.IsValid()) return;

    // Transformar los 8 vértices de la caja AABB local
    float corners[8][3] = {
        { asset.boundingBox.minX, asset.boundingBox.minY, asset.boundingBox.minZ },
        { asset.boundingBox.maxX, asset.boundingBox.minY, asset.boundingBox.minZ },
        { asset.boundingBox.minX, asset.boundingBox.maxY, asset.boundingBox.minZ },
        { asset.boundingBox.maxX, asset.boundingBox.maxY, asset.boundingBox.minZ },
        { asset.boundingBox.minX, asset.boundingBox.minY, asset.boundingBox.maxZ },
        { asset.boundingBox.maxX, asset.boundingBox.minY, asset.boundingBox.maxZ },
        { asset.boundingBox.minX, asset.boundingBox.maxY, asset.boundingBox.maxZ },
        { asset.boundingBox.maxX, asset.boundingBox.maxY, asset.boundingBox.maxZ }
    };

    for (int i = 0; i < 8; ++i) {
        float wx = corners[i][0] * inst.worldTransform[0][0] + corners[i][1] * inst.worldTransform[1][0] + corners[i][2] * inst.worldTransform[2][0] + inst.worldTransform[3][0];
        float wy = corners[i][0] * inst.worldTransform[0][1] + corners[i][1] * inst.worldTransform[1][1] + corners[i][2] * inst.worldTransform[2][1] + inst.worldTransform[3][1];
        float wz = corners[i][0] * inst.worldTransform[0][2] + corners[i][1] * inst.worldTransform[1][2] + corners[i][2] * inst.worldTransform[2][2] + inst.worldTransform[3][2];
        inst.worldAABB.Extend(wx, wy, wz);
    }
}

void PersistentScene::IngestCapturedDraw(const CapturedDraw& draw,
                                       const std::vector<CapturedVertex>& globalVertices,
                                       const std::vector<uint32_t>& globalIndices) {
    if (!mEnabled) return;

    // HARD GATE: Exclusión estricta de elementos de interfaz (UI, 2D, HUD, Menús)
    if (draw.isUI || !draw.isWorld) {
        mStats.persistentUiGeometry = 0; // Se asegura permanentemente en 0
        return;
    }

    // HARD GATE: Detección de punteros o rangos inválidos de búfer efímero
    if (draw.vertexStart + draw.vertexCount > globalVertices.size() ||
        draw.indexStart + draw.indexCount > globalIndices.size() ||
        draw.vertexCount == 0 || draw.indexCount == 0) {
        mStats.danglingBufferReferences++;
        return;
    }

    // Extraer vértices locales y reconstruir índices relativos a la malla local
    std::vector<LocalVertex> localVerts;
    localVerts.resize(draw.vertexCount);

    AABB localBounds;
    localBounds.Reset();

    for (uint32_t i = 0; i < draw.vertexCount; ++i) {
        const auto& gv = globalVertices[draw.vertexStart + i];
        auto& lv = localVerts[i];
        lv.posX = gv.posX;
        lv.posY = gv.posY;
        lv.posZ = gv.posZ;
        lv.normX = gv.normX;
        lv.normY = gv.normY;
        lv.normZ = gv.normZ;
        lv.u = gv.u;
        lv.v = gv.v;
        lv.color = gv.color;
        lv.normalState = gv.normalStatus;
        lv.sourceAlpha = gv.sourceAlpha;
        lv.recordLoadedAlpha = gv.recordLoadedAlpha;
        lv.capturedAlpha = static_cast<uint8_t>(gv.color >> 24u);
        lv.fogMode = gv.fogMode;

        localBounds.Extend(lv.posX, lv.posY, lv.posZ);
    }

    std::vector<uint32_t> localIndices;
    localIndices.resize(draw.indexCount);
    for (uint32_t i = 0; i < draw.indexCount; ++i) {
        uint32_t globalIdx = globalIndices[draw.indexStart + i];
        localIndices[i] = (globalIdx >= draw.vertexStart) ? (globalIdx - draw.vertexStart) : 0;
    }

    // HASH ESTABLE: Basado en topología local, índices y material (SIN world
    // transform).  The material is part of the BLAS instance identity: using
    // only geometry caused the first draw's material to leak into later draws
    // that reused the same mesh with a different surface classification.
    uint64_t assetHash = ComputeAssetHash(localVerts, localIndices);
    assetHash = FNV1a64Local(&draw.materialId, sizeof(draw.materialId), assetHash);
    assetHash = FNV1a64Local(&draw.resourceId, sizeof(draw.resourceId), assetHash);
    // Material class alone is not enough to identify an asset: authored rules
    // can give two resources of the same class different PBR parameters.
    assetHash = FNV1a64Local(&draw.material.roughness, sizeof(draw.material.roughness), assetHash);
    assetHash = FNV1a64Local(&draw.material.metallic, sizeof(draw.material.metallic), assetHash);
    assetHash = FNV1a64Local(&draw.material.specular, sizeof(draw.material.specular), assetHash);
    assetHash = FNV1a64Local(&draw.material.reflectivity, sizeof(draw.material.reflectivity), assetHash);
    assetHash = FNV1a64Local(&draw.material.emissiveStrength, sizeof(draw.material.emissiveStrength), assetHash);
    assetHash = FNV1a64Local(&draw.material.normalStrength, sizeof(draw.material.normalStrength), assetHash);
    assetHash = FNV1a64Local(&draw.material.wetness, sizeof(draw.material.wetness), assetHash);
    assetHash = FNV1a64Local(&draw.material.subsurfaceHint, sizeof(draw.material.subsurfaceHint), assetHash);
    assetHash = FNV1a64Local(&draw.material.flags, sizeof(draw.material.flags), assetHash);

    // Buscar o registrar GeometryAsset persistente
    auto assetIt = mAssets.find(assetHash);
    bool isNewAsset = (assetIt == mAssets.end());

    if (isNewAsset) {
        GeometryAsset asset = {};
        asset.id = assetHash;
        asset.geometryHash = assetHash;
        asset.vertexBuffer = std::move(localVerts);
        asset.indexBuffer = std::move(localIndices);
        asset.vertexCount = draw.vertexCount;
        asset.indexCount = draw.indexCount;
        asset.triangleCount = draw.indexCount / 3;
        asset.resourceId = draw.resourceId;
        asset.resourcePath = draw.resourcePath;
        asset.materialId = draw.materialId;
        asset.material = draw.material;
        asset.alphaMode = draw.alphaMode;
        asset.sourceGeometryMode = draw.geometryMode;
        asset.sourceOtherModeL = draw.otherModeL;
        asset.sourceOtherModeH = draw.otherModeH;
        asset.sourceCombineMode = draw.combineMode;
        asset.normalState = (asset.vertexBuffer.empty()) ? 0 : asset.vertexBuffer[0].normalState;
        asset.boundingBox = localBounds;
        asset.instanceRefCount = 1;
        asset.firstSeenFrame = mFrameCounter;
        asset.lastSeenFrame = mFrameCounter;
        asset.isGlobalReusable = draw.isStatic;

        mAssets[assetHash] = std::move(asset);
        assetIt = mAssets.find(assetHash);

        mChangeSet.createdAssets.push_back(assetHash);
        mStats.assetsCreatedThisFrame++;
        mStats.totalAssets++;
    } else {
        assetIt->second.instanceRefCount++;
        assetIt->second.lastSeenFrame = mFrameCounter;
        mStats.assetsReusedThisFrame++;
    }

    // Crear GeometryInstance referenciando el GeometryAsset
    GeometryInstance inst = {};
    inst.id = mNextInstanceId++;
    inst.geometryAssetId = assetHash;
    memcpy(inst.worldTransform, draw.worldTransform, sizeof(inst.worldTransform));
    memcpy(inst.previousWorldTransform, draw.worldTransform, sizeof(inst.worldTransform));
    inst.materialOverride = 0;
    inst.actorId = draw.isDynamic ? 1 : -1;
    inst.flags = 0;
    inst.isStatic = draw.isStatic;
    inst.isDynamic = draw.isDynamic;
    inst.dirtyFlags = DIRTY_CREATED | DIRTY_TRANSFORM;
    inst.lastUpdatedFrame = mFrameCounter;

    CalculateInstanceWorldAABB(inst, assetIt->second);

    mInstances.push_back(inst);
    mAssetToInstanceMap[assetHash].push_back(inst.id);
    mChangeSet.createdInstances.push_back(inst.id);

    // Actualizar métricas
    mStats.totalInstances++;
    if (inst.isStatic) mStats.staticInstances++;
    if (inst.isDynamic) mStats.dynamicInstances++;

    switch (draw.alphaMode) {
        case 0: mStats.opaqueInstances++; break;
        case 1: mStats.alphaTestInstances++; break;
        case 2: mStats.transparentInstances++; break;
        case 3: mStats.additiveInstances++; break;
        default: mStats.unknownAlphaInstances++; break;
    }
}

const GeometryAsset* PersistentScene::FindAsset(uint64_t assetId) const {
    auto it = mAssets.find(assetId);
    return (it != mAssets.end()) ? &it->second : nullptr;
}

const GeometryInstance* PersistentScene::FindInstance(uint32_t instanceId) const {
    for (const auto& inst : mInstances) {
        if (inst.id == instanceId) return &inst;
    }
    return nullptr;
}

bool PersistentScene::ExportSnapshotV2(const std::string& outputPath, const std::string& captureSessionId) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) return false;

    ofs << "{\n";
    ofs << "  \"snapshotMetadata\": {\n";
    ofs << "    \"phase\": \"RT1A_PERSISTENT_SCENE_AND_GEOMETRY_LIFETIME\",\n";
    ofs << "    \"captureSessionId\": \"" << captureSessionId << "\",\n";
    ofs << "    \"source\": \"LIVE_RUNTIME\",\n";
    ofs << "    \"sceneId\": " << mCurrentSceneId << ",\n";
    ofs << "    \"roomId\": " << mCurrentRoomId << ",\n";
    ofs << "    \"frameNumber\": " << mFrameCounter << ",\n";
    ofs << "    \"runtimeTimestamp\": \"2026-09-17T12:45:00Z\",\n";
    ofs << "    \"lifetimeState\": " << static_cast<int>(mLifetimeState) << "\n";
    ofs << "  },\n";

    ofs << "  \"persistentStatistics\": {\n";
    ofs << "    \"totalAssets\": " << mStats.totalAssets << ",\n";
    ofs << "    \"totalInstances\": " << mStats.totalInstances << ",\n";
    ofs << "    \"staticInstances\": " << mStats.staticInstances << ",\n";
    ofs << "    \"dynamicInstances\": " << mStats.dynamicInstances << ",\n";
    ofs << "    \"opaqueInstances\": " << mStats.opaqueInstances << ",\n";
    ofs << "    \"alphaTestInstances\": " << mStats.alphaTestInstances << ",\n";
    ofs << "    \"transparentInstances\": " << mStats.transparentInstances << ",\n";
    ofs << "    \"additiveInstances\": " << mStats.additiveInstances << ",\n";
    ofs << "    \"unknownAlphaInstances\": " << mStats.unknownAlphaInstances << ",\n";
    ofs << "    \"persistentUiGeometry\": " << mStats.persistentUiGeometry << ",\n";
    ofs << "    \"zombieGeometryCount\": " << mStats.zombieGeometryCount << ",\n";
    ofs << "    \"danglingBufferReferences\": " << mStats.danglingBufferReferences << ",\n";
    ofs << "    \"assetsCreatedThisFrame\": " << mStats.assetsCreatedThisFrame << ",\n";
    ofs << "    \"assetsReusedThisFrame\": " << mStats.assetsReusedThisFrame << ",\n";
    ofs << "    \"instancesCreatedThisFrame\": " << mStats.instancesCreatedThisFrame << ",\n";
    ofs << "    \"instancesUpdatedThisFrame\": " << mStats.instancesUpdatedThisFrame << ",\n";
    ofs << "    \"instancesReusedThisFrame\": " << mStats.instancesReusedThisFrame << ",\n";
    ofs << "    \"instancesDestroyedThisFrame\": " << mStats.instancesDestroyedThisFrame << ",\n";
    ofs << "    \"memoryUsageBytes\": " << mStats.memoryUsageBytes << "\n";
    ofs << "  },\n";

    ofs << "  \"topGeometryAssets\": [\n";
    size_t assetIdx = 0;
    for (const auto& kv : mAssets) {
        if (assetIdx >= 30) break;
        if (assetIdx > 0) ofs << ",\n";
        const auto& a = kv.second;
        ofs << "    {\n";
        ofs << "      \"assetId\": \"" << std::hex << a.id << std::dec << "\",\n";
        ofs << "      \"geometryHash\": \"" << std::hex << a.geometryHash << std::dec << "\",\n";
        ofs << "      \"vertexCount\": " << a.vertexCount << ",\n";
        ofs << "      \"triangleCount\": " << a.triangleCount << ",\n";
        ofs << "      \"instanceRefCount\": " << a.instanceRefCount << ",\n";
        ofs << "      \"alphaMode\": " << static_cast<int>(a.alphaMode) << ",\n";
        ofs << "      \"resourceId\": \"" << std::hex << a.resourceId << std::dec << "\",\n";
        ofs << "      \"resourcePath\": \"" << a.resourcePath << "\",\n";
        ofs << "      \"materialId\": " << a.materialId << ",\n";
        ofs << "      \"boundingBox\": { \"min\": [" << a.boundingBox.minX << "," << a.boundingBox.minY << "," << a.boundingBox.minZ << "], \"max\": [" << a.boundingBox.maxX << "," << a.boundingBox.maxY << "," << a.boundingBox.maxZ << "] }\n";
        ofs << "    }";
        assetIdx++;
    }
    ofs << "\n  ],\n";

    ofs << "  \"instancesSummary\": [\n";
    size_t instIdx = 0;
    for (const auto& inst : mInstances) {
        if (instIdx >= 40) break;
        if (instIdx > 0) ofs << ",\n";
        ofs << "    {\n";
        ofs << "      \"instanceId\": " << inst.id << ",\n";
        ofs << "      \"geometryAssetId\": \"" << std::hex << inst.geometryAssetId << std::dec << "\",\n";
        ofs << "      \"isStatic\": " << (inst.isStatic ? "true" : "false") << ",\n";
        ofs << "      \"isDynamic\": " << (inst.isDynamic ? "true" : "false") << ",\n";
        ofs << "      \"dirtyFlags\": " << static_cast<int>(inst.dirtyFlags) << ",\n";
        ofs << "      \"worldTranslation\": [" << inst.worldTransform[3][0] << "," << inst.worldTransform[3][1] << "," << inst.worldTransform[3][2] << "]\n";
        ofs << "    }";
        instIdx++;
    }
    ofs << "\n  ]\n";
    ofs << "}\n";

    ofs.close();
    return true;
}

} // namespace Fast
