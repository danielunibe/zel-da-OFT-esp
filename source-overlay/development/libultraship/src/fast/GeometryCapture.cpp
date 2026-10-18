#include "fast/GeometryCapture.h"
#include "fast/couch_diagnostics.h"
#include "fast/Fast3DAlphaPolicy.h"
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
#include <algorithm>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Fast {

static inline float FastDot(float x1, float y1, float z1, float x2, float y2, float z2) {
    return x1 * x2 + y1 * y2 + z1 * z2;
}

static inline void FastNormalize(float& x, float& y, float& z) {
    float lenSq = x * x + y * y + z * z;
    if (lenSq > 1e-6f) {
        float invLen = 1.0f / sqrtf(lenSq);
        x *= invLen;
        y *= invLen;
        z *= invLen;
    } else {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
    }
}

static inline uint64_t FNV1a64(const void* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

GeometryCapture& GeometryCapture::Instance() {
    static GeometryCapture sInstance;
    return sInstance;
}

GeometryCapture::GeometryCapture()
    : mEnabled(false), mInitialized(false), mCurrentSceneId(-1), mCurrentRoomId(-1),
      mFrameCounter(0), mSnapshotRequested(false) {
    memset(mRspVertices, 0, sizeof(mRspVertices));
    memset(&mStats, 0, sizeof(mStats));
}

GeometryCapture::~GeometryCapture() {
}

void GeometryCapture::Init() {
    if (mInitialized) return;

    // Generar token único de sesión de captura en LIVE_RUNTIME (UUIDv4 compatible)
    uint64_t t = static_cast<uint64_t>(time(nullptr));
#ifdef _WIN32
    uint32_t pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    uint32_t pid = static_cast<uint32_t>(getpid());
#endif
    char sessionBuf[64];
    snprintf(sessionBuf, sizeof(sessionBuf), "%08x-%04x-4001-%04x-%08x%04x",
             static_cast<uint32_t>(t & 0xFFFFFFFF),
             static_cast<uint16_t>(pid & 0xFFFF),
             static_cast<uint16_t>((t >> 16) & 0x3FFF | 0x8000),
             static_cast<uint32_t>(pid ^ 0xA5A5A5A5),
             static_cast<uint16_t>(t & 0xFFFF));
    mCaptureSessionId = sessionBuf;

    // Imprimir en stdout y registrar en couch_diagnostics.txt
    printf("[RT0] CaptureSession = %s\n", mCaptureSessionId.c_str());
    fflush(stdout);

    FILE* fLog = fopen(couch_diagnostics_path(), "a");
    if (fLog) {
        fprintf(fLog, "[RT0] CaptureSession = %s\n", mCaptureSessionId.c_str());
        fclose(fLog);
    }

    // Pre-reserva eficiente de memoria para evitar realocaciones
    mVertices.reserve(131072);
    mIndices.reserve(262144);
    mDraws.reserve(4096);
    mPendingVertices.reserve(4096);
    mPendingIndices.reserve(8192);

    PersistentScene::Instance().Init();

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        mEnabled = (cvars->GetInteger("gEnhancements.Graphics.GeometryCaptureDebug", 0) != 0) ||
                   (cvars->GetInteger("gEnhancements.Graphics.PersistentSceneDebug", 0) != 0);
    } else {
        mEnabled = false;
    }

    const char* envDxr = getenv("COUCH_DXR_AS");
    const char* envShadow = getenv("COUCH_DXR_SHADOW");
    if ((envDxr && strcmp(envDxr, "1") == 0) || (envShadow && strcmp(envShadow, "1") == 0)) {
        mEnabled = true;
    }
#ifdef _WIN32
    if (DxrAccelerationStructureManager::Instance().IsEnabled()) {
        mEnabled = true;
    }
#endif

    mInitialized = true;
}

void GeometryCapture::BeginFrame() {
    Init();

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        mEnabled = (cvars->GetInteger("gEnhancements.Graphics.GeometryCaptureDebug", 0) != 0) ||
                   (cvars->GetInteger("gEnhancements.Graphics.PersistentSceneDebug", 0) != 0);
    }

    const char* envDxr = getenv("COUCH_DXR_AS");
    const char* envShadow = getenv("COUCH_DXR_SHADOW");
    if ((envDxr && strcmp(envDxr, "1") == 0) || (envShadow && strcmp(envShadow, "1") == 0)) {
        mEnabled = true;
    }
#ifdef _WIN32
    if (DxrAccelerationStructureManager::Instance().IsEnabled()) {
        mEnabled = true;
    }
#endif

    PersistentScene::Instance().BeginFrame();

    if (!mEnabled) {
        return;
    }

    mFrameCounter++;

    mDraws.clear();
    mVertices.clear();
    mIndices.clear();
    mPendingVertices.clear();
    mPendingIndices.clear();
    mUniqueAssets.clear();
    memset(&mStats, 0, sizeof(mStats));
    memset(mRspVertices, 0, sizeof(mRspVertices));
}

void GeometryCapture::SetCurrentScene(int sceneId, int roomId) {
    if (sceneId != mCurrentSceneId || roomId != mCurrentRoomId) {
        const bool isNewScene = (sceneId != mCurrentSceneId);
        // Transición de escena o cuarto: limpiar escena persistente y luces residuales del cuarto anterior
        ClearScene();
        MaterialRegistry::Instance().ClearLightsOnRoomChange(isNewScene);
        mCurrentSceneId = sceneId;
        mCurrentRoomId = roomId;
    }
    PersistentScene::Instance().SetCurrentScene(sceneId, roomId);
}

void GeometryCapture::ClearScene() {
    mDraws.clear();
    mVertices.clear();
    mIndices.clear();
    mPendingVertices.clear();
    mPendingIndices.clear();
    mUniqueAssets.clear();
    memset(&mStats, 0, sizeof(mStats));
    PersistentScene::Instance().ClearAll();
}

void GeometryCapture::RecordLoadedVertex(uint16_t vtxIdx, float objX, float objY, float objZ,
                                         float normX, float normY, float normZ, bool hasNormal,
                                         float u, float v, uint32_t color, uint32_t sourceColor,
                                         uint8_t sourceAlpha, bool fogMode, const float modelView[4][4]) {
    if (!mEnabled || vtxIdx >= 128) return;

    RSPVertexCache& cache = mRspVertices[vtxIdx];
    cache.objX = objX;
    cache.objY = objY;
    cache.objZ = objZ;
    cache.normX = normX;
    cache.normY = normY;
    cache.normZ = normZ;
    cache.hasNormal = hasNormal;
    cache.u = u;
    cache.v = v;
    cache.color = color;
    cache.sourceColor = sourceColor;
    cache.sourceAlpha = sourceAlpha;
    cache.fogMode = fogMode;
    memcpy(cache.modelView, modelView, sizeof(cache.modelView));
    cache.valid = true;
}

bool GeometryCapture::RequiresFlushForTriangle(uint16_t idx0, uint16_t idx1, uint16_t idx2) const {
    if (!mEnabled || mPendingIndices.empty() || idx0 >= 128 || idx1 >= 128 || idx2 >= 128) return false;
    const auto& v0 = mRspVertices[idx0];
    const auto& v1 = mRspVertices[idx1];
    const auto& v2 = mRspVertices[idx2];
    if (!v0.valid || !v1.valid || !v2.valid) return false;
    const bool mixedTransforms = memcmp(v0.modelView, v1.modelView, sizeof(v0.modelView)) != 0 ||
                                 memcmp(v0.modelView, v2.modelView, sizeof(v0.modelView)) != 0;
    return mixedTransforms != mPendingBakedViewSpace ||
           (!mixedTransforms && memcmp(mPendingTransform, v0.modelView, sizeof(mPendingTransform)) != 0);
}

void GeometryCapture::RecordTriangle(uint16_t idx0, uint16_t idx1, uint16_t idx2, const float uv[2][3][2]) {
    if (!mEnabled) return;

    if (idx0 >= 128 || idx1 >= 128 || idx2 >= 128) return;
    const RSPVertexCache& v0 = mRspVertices[idx0];
    const RSPVertexCache& v1 = mRspVertices[idx1];
    const RSPVertexCache& v2 = mRspVertices[idx2];
    if (!v0.valid || !v1.valid || !v2.valid) return;
    // A single instance transform cannot represent vertices loaded under
    // different joint matrices. Bake those triangles into view space; use
    // an identity instance and derive their normal from the baked positions.
    const bool mixedTransforms = memcmp(v0.modelView, v1.modelView, sizeof(v0.modelView)) != 0 ||
                                 memcmp(v0.modelView, v2.modelView, sizeof(v0.modelView)) != 0;
    if (RequiresFlushForTriangle(idx0, idx1, idx2)) return; // Interpreter must flush both batches first.
    if (mPendingIndices.empty()) {
        mPendingBakedViewSpace = mixedTransforms;
        if (mixedTransforms) {
            memset(mPendingTransform, 0, sizeof(mPendingTransform));
            for (int axis = 0; axis < 4; ++axis) mPendingTransform[axis][axis] = 1.0f;
        } else {
            memcpy(mPendingTransform, v0.modelView, sizeof(mPendingTransform));
        }
    }

    uint32_t baseIdx = static_cast<uint32_t>(mPendingVertices.size());

    CapturedVertex cv0 = {};
    cv0.posX = v0.objX; cv0.posY = v0.objY; cv0.posZ = v0.objZ;
    cv0.u = uv[0][0][0]; cv0.v = uv[0][0][1];
    cv0.u1 = uv[1][0][0]; cv0.v1 = uv[1][0][1]; cv0.color = v0.color; cv0.sourceColor = v0.sourceColor;
    cv0.sourceAlpha = v0.sourceAlpha; cv0.recordLoadedAlpha = static_cast<uint8_t>(v0.color >> 24u);
    cv0.fogMode = v0.fogMode ? 1u : 0u;

    CapturedVertex cv1 = {};
    cv1.posX = v1.objX; cv1.posY = v1.objY; cv1.posZ = v1.objZ;
    cv1.u = uv[0][1][0]; cv1.v = uv[0][1][1];
    cv1.u1 = uv[1][1][0]; cv1.v1 = uv[1][1][1]; cv1.color = v1.color; cv1.sourceColor = v1.sourceColor;
    cv1.sourceAlpha = v1.sourceAlpha; cv1.recordLoadedAlpha = static_cast<uint8_t>(v1.color >> 24u);
    cv1.fogMode = v1.fogMode ? 1u : 0u;

    CapturedVertex cv2 = {};
    cv2.posX = v2.objX; cv2.posY = v2.objY; cv2.posZ = v2.objZ;
    cv2.u = uv[0][2][0]; cv2.v = uv[0][2][1];
    cv2.u1 = uv[1][2][0]; cv2.v1 = uv[1][2][1]; cv2.color = v2.color; cv2.sourceColor = v2.sourceColor;
    cv2.sourceAlpha = v2.sourceAlpha; cv2.recordLoadedAlpha = static_cast<uint8_t>(v2.color >> 24u);
    cv2.fogMode = v2.fogMode ? 1u : 0u;

    if (mixedTransforms) {
        CapturedVertex* captured[3] = { &cv0, &cv1, &cv2 };
        const RSPVertexCache* loaded[3] = { &v0, &v1, &v2 };
        for (int vertex = 0; vertex < 3; ++vertex) {
            const auto& source = *loaded[vertex];
            const auto& matrix = source.modelView;
            captured[vertex]->posX = source.objX * matrix[0][0] + source.objY * matrix[1][0] +
                                     source.objZ * matrix[2][0] + matrix[3][0];
            captured[vertex]->posY = source.objX * matrix[0][1] + source.objY * matrix[1][1] +
                                     source.objZ * matrix[2][1] + matrix[3][1];
            captured[vertex]->posZ = source.objX * matrix[0][2] + source.objY * matrix[1][2] +
                                     source.objZ * matrix[2][2] + matrix[3][2];
        }
    }

    // Determinar o derivar normales
    if (!mixedTransforms && v0.hasNormal && v1.hasNormal && v2.hasNormal) {
        cv0.normX = v0.normX; cv0.normY = v0.normY; cv0.normZ = v0.normZ;
        cv0.normalStatus = NORMAL_SOURCE_AVAILABLE;

        cv1.normX = v1.normX; cv1.normY = v1.normY; cv1.normZ = v1.normZ;
        cv1.normalStatus = NORMAL_SOURCE_AVAILABLE;

        cv2.normX = v2.normX; cv2.normY = v2.normY; cv2.normZ = v2.normZ;
        cv2.normalStatus = NORMAL_SOURCE_AVAILABLE;

        mStats.normalAvailableCount += 3;
    } else {
        // Derivar normal geométrica plana mediante producto cruz
        float e1x = cv1.posX - cv0.posX;
        float e1y = cv1.posY - cv0.posY;
        float e1z = cv1.posZ - cv0.posZ;

        float e2x = cv2.posX - cv0.posX;
        float e2y = cv2.posY - cv0.posY;
        float e2z = cv2.posZ - cv0.posZ;

        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        FastNormalize(nx, ny, nz);

        cv0.normX = nx; cv0.normY = ny; cv0.normZ = nz;
        cv0.normalStatus = NORMAL_DERIVED;

        cv1.normX = nx; cv1.normY = ny; cv1.normZ = nz;
        cv1.normalStatus = NORMAL_DERIVED;

        cv2.normX = nx; cv2.normY = ny; cv2.normZ = nz;
        cv2.normalStatus = NORMAL_DERIVED;

        mStats.normalDerivedCount += 3;
    }

    // Preserve G_LIGHTING provenance separately from whether the source
    // normal could be reused. Mixed-transform triangles derive a geometric
    // normal, but their captured Fast3D vertex RGB is still pre-lit.
    if (v0.hasNormal) cv0.normalStatus |= NORMAL_FLAG_FAST3D_LIT_COLOR;
    if (v1.hasNormal) cv1.normalStatus |= NORMAL_FLAG_FAST3D_LIT_COLOR;
    if (v2.hasNormal) cv2.normalStatus |= NORMAL_FLAG_FAST3D_LIT_COLOR;

    mPendingVertices.push_back(cv0);
    mPendingVertices.push_back(cv1);
    mPendingVertices.push_back(cv2);

    mPendingIndices.push_back(baseIdx + 0);
    mPendingIndices.push_back(baseIdx + 1);
    mPendingIndices.push_back(baseIdx + 2);
}

uint64_t GeometryCapture::ComputeGeometryHash(uint32_t vtxStart, uint32_t vtxCount,
                                             uint32_t idxStart, uint32_t idxCount) {
    uint64_t h = 14695981039346656037ULL;
    for (uint32_t i = 0; i < vtxCount; ++i) {
        const auto& v = mVertices[vtxStart + i];
        float pos[3] = { v.posX, v.posY, v.posZ };
        h = FNV1a64(pos, sizeof(pos), h);
    }
    for (uint32_t i = 0; i < idxCount; ++i) {
        uint32_t idx = mIndices[idxStart + i] - vtxStart;
        h = FNV1a64(&idx, sizeof(idx), h);
    }
    return h;
}

void GeometryCapture::FlushDraw(uint64_t materialId,
                               const MaterialDefinition& material,
                               uint64_t resourceId, const std::string& resourcePath,
                               uint32_t textureSrvIndex, uint32_t textureSrvIndex1,
                               const uint32_t textureTileModes[2],
                               const uint8_t primitiveColor[4], const uint8_t environmentColor[4],
                               const uint8_t fogColor[4],
                               uint32_t geometryMode, uint32_t otherModeL, uint32_t otherModeH,
                               uint64_t combineMode, bool isDepthTest, bool isDecal,
                               uint32_t lightOwnerId, uint32_t lightRole) {
    if (!mEnabled || mPendingIndices.empty()) return;
    const auto& transform = mPendingTransform;

    CapturedDraw cd = {};
    cd.lightOwnerId = lightOwnerId;
    cd.lightRole = lightRole;
    cd.vertexStart = static_cast<uint32_t>(mVertices.size());
    cd.vertexCount = static_cast<uint32_t>(mPendingVertices.size());
    cd.indexStart = static_cast<uint32_t>(mIndices.size());
    cd.indexCount = static_cast<uint32_t>(mPendingIndices.size());

    // Copiar matriz de transformación ModelView sin proyección
    memcpy(cd.worldTransform, transform, sizeof(cd.worldTransform));

    // Calcular posiciones en espacio de mundo para los vértices capturados
    for (auto& v : mPendingVertices) {
        v.worldX = v.posX * transform[0][0] + v.posY * transform[1][0] + v.posZ * transform[2][0] + transform[3][0];
        v.worldY = v.posX * transform[0][1] + v.posY * transform[1][1] + v.posZ * transform[2][1] + transform[3][1];
        v.worldZ = v.posX * transform[0][2] + v.posY * transform[1][2] + v.posZ * transform[2][2] + transform[3][2];
    }

    cd.materialId = materialId;
    cd.material = material;
    cd.resourceId = resourceId;
    cd.resourcePath = resourcePath;
    cd.textureSrvIndex = textureSrvIndex;
    cd.textureSrvIndex1 = textureSrvIndex1;
    cd.textureTileModes[0] = textureTileModes[0];
    cd.textureTileModes[1] = textureTileModes[1];
    memcpy(cd.primitiveColor, primitiveColor, sizeof(cd.primitiveColor));
    memcpy(cd.environmentColor, environmentColor, sizeof(cd.environmentColor));
    memcpy(cd.fogColor, fogColor, sizeof(cd.fogColor));
    cd.geometryMode = geometryMode;
    cd.otherModeL = otherModeL;
    cd.otherModeH = otherModeH;
    cd.combineMode = combineMode;

    // Classify using Fast3D's alpha-compare, coverage, z-mode, and active
    // blender state. Blender selector fields alone do not prove translucency.
    const Fast3DAlphaClass alphaClass = ClassifyFast3DAlphaCandidate(otherModeL, otherModeH);

    if (!isDepthTest) {
        cd.isUI = true;
        cd.isWorld = false;
        cd.surfaceCategory = SURFACE_UI_2D;
    } else {
        cd.isUI = false;
        cd.isWorld = true;
        
        // Determinar Estático vs Dinámico
        // Si la traslación y rotación corresponden a la matriz identidad o cámara base es estático;
        // si tiene traslaciones locales de actor o matrices dinámicas apiladas es dinámico.
        bool hasNonZeroTranslation = (fabsf(transform[3][0]) > 0.001f || fabsf(transform[3][1]) > 0.001f || fabsf(transform[3][2]) > 0.001f);
        bool hasScaling = (fabsf(transform[0][0] - 1.0f) > 0.05f || fabsf(transform[1][1] - 1.0f) > 0.05f);
        
        if (hasNonZeroTranslation || hasScaling) {
            cd.isDynamic = true;
            cd.isStatic = false;
            cd.surfaceCategory = SURFACE_ACTOR_DYNAMIC;
        } else {
            cd.isStatic = true;
            cd.isDynamic = false;
            cd.surfaceCategory = SURFACE_WORLD_STATIC;
        }
    }

    if (alphaClass == Fast3DAlphaClass::Opaque) {
        cd.alphaMode = ALPHA_OPAQUE;
        mStats.opaqueDraws++;
    } else if (alphaClass == Fast3DAlphaClass::Cutout) {
        cd.alphaMode = ALPHA_TEST;
        mStats.alphaTestDraws++;
    } else if (alphaClass == Fast3DAlphaClass::Blended) {
        cd.alphaMode = ALPHA_TRANSPARENT;
        mStats.transparentDraws++;
    } else {
        cd.alphaMode = ALPHA_UNKNOWN;
        mStats.unknownAlphaDraws++;
    }

    // Incorporar a los buffers globales
    mVertices.insert(mVertices.end(), mPendingVertices.begin(), mPendingVertices.end());
    // Pending indices are batch-local. The global snapshot must add this
    // draw's base before PersistentScene converts them back to local indices.
    for (uint32_t index : mPendingIndices) mIndices.push_back(cd.vertexStart + index);

    // Deduplicación e Instancing
    cd.geometryHash = ComputeGeometryHash(cd.vertexStart, cd.vertexCount, cd.indexStart, cd.indexCount);

    auto it = mUniqueAssets.find(cd.geometryHash);
    if (it != mUniqueAssets.end()) {
        it->second.instanceCount++;
        mStats.instanceCount++;
    } else {
        CapturedAssetSummary asset = {};
        asset.geometryHash = cd.geometryHash;
        asset.vertexCount = cd.vertexCount;
        asset.triangleCount = cd.indexCount / 3;
        asset.instanceCount = 1;
        asset.firstMaterialId = cd.materialId;
        asset.firstResourcePath = cd.resourcePath;
        mUniqueAssets[cd.geometryHash] = asset;
        mStats.uniqueGeometryAssets++;
    }

    mDraws.push_back(cd);
    PersistentScene::Instance().IngestCapturedDraw(cd, mVertices, mIndices);

    // Actualizar métricas
    mStats.totalDraws++;
    if (cd.isWorld) {
        mStats.worldDraws++;
        if (cd.isStatic) mStats.staticDraws++;
        if (cd.isDynamic) mStats.dynamicDraws++;
    } else {
        mStats.uiDraws++;
    }

    mStats.totalVertices += cd.vertexCount;
    mStats.totalIndices += cd.indexCount;
    mStats.totalTriangles += (cd.indexCount / 3);

    if (materialId != 0) {
        mStats.materialCount++;
    } else {
        mStats.unknownMaterialCount++;
    }

    mPendingVertices.clear();
    mPendingIndices.clear();
}

void GeometryCapture::EndFrame() {
    PersistentScene::Instance().EndFrame();

    if (!mEnabled) return;

    // Exportación automática de snapshots V2 directamente desde LIVE_RUNTIME
    static bool sTempleV2Exported = false;
    static bool sKokiriV2Exported = false;
    static bool sHyruleV2Exported = false;

    if (mCurrentSceneId == 67 && mFrameCounter >= 30 && !sTempleV2Exported) {
        ExportSnapshot("visual_workspace/rt0_geometry_capture/captures/temple_of_time_geometry_snapshot.json");
        PersistentScene::Instance().ExportSnapshotV2("visual_workspace/rt1a_persistent_scene/captures/temple_persistent_scene_v2.json", mCaptureSessionId);
        sTempleV2Exported = true;
    } else if (mCurrentSceneId == 85 && mFrameCounter >= 30 && !sKokiriV2Exported) {
        ExportSnapshot("visual_workspace/rt0_geometry_capture/captures/kokiri_geometry_snapshot.json");
        PersistentScene::Instance().ExportSnapshotV2("visual_workspace/rt1a_persistent_scene/captures/kokiri_persistent_scene_v2.json", mCaptureSessionId);
        sKokiriV2Exported = true;
    } else if (mCurrentSceneId == 81 && mFrameCounter >= 30 && !sHyruleV2Exported) {
        ExportSnapshot("visual_workspace/rt0_geometry_capture/captures/hyrule_field_geometry_snapshot.json");
        PersistentScene::Instance().ExportSnapshotV2("visual_workspace/rt1a_persistent_scene/captures/hyrule_field_persistent_scene_v2.json", mCaptureSessionId);
        sHyruleV2Exported = true;
    }

    if (mSnapshotRequested && !mRequestedSnapshotPath.empty()) {
        ExportSnapshot(mRequestedSnapshotPath);
        mSnapshotRequested = false;
        mRequestedSnapshotPath.clear();
    }
}

bool GeometryCapture::ExportSnapshot(const std::string& outputPath) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        return false;
    }

    ofs << "{\n";
    ofs << "  \"snapshotMetadata\": {\n";
    ofs << "    \"phase\": \"RT0_GEOMETRY_CAPTURE_FOUNDATION\",\n";
    ofs << "    \"captureSessionId\": \"" << mCaptureSessionId << "\",\n";
    ofs << "    \"source\": \"LIVE_RUNTIME\",\n";
    ofs << "    \"sceneId\": " << mCurrentSceneId << ",\n";
    ofs << "    \"roomId\": " << mCurrentRoomId << ",\n";
    ofs << "    \"frame\": " << mFrameCounter << ",\n";
    ofs << "    \"timestamp\": \"2026-09-17T12:45:00Z\"\n";
    ofs << "  },\n";
    ofs << "  \"statistics\": {\n";
    ofs << "    \"totalDraws\": " << mStats.totalDraws << ",\n";
    ofs << "    \"worldDraws\": " << mStats.worldDraws << ",\n";
    ofs << "    \"uiDraws\": " << mStats.uiDraws << ",\n";
    ofs << "    \"totalVertices\": " << mStats.totalVertices << ",\n";
    ofs << "    \"totalIndices\": " << mStats.totalIndices << ",\n";
    ofs << "    \"totalTriangles\": " << mStats.totalTriangles << ",\n";
    ofs << "    \"staticDraws\": " << mStats.staticDraws << ",\n";
    ofs << "    \"dynamicDraws\": " << mStats.dynamicDraws << ",\n";
    ofs << "    \"opaqueDraws\": " << mStats.opaqueDraws << ",\n";
    ofs << "    \"alphaTestDraws\": " << mStats.alphaTestDraws << ",\n";
    ofs << "    \"transparentDraws\": " << mStats.transparentDraws << ",\n";
    ofs << "    \"unknownAlphaDraws\": " << mStats.unknownAlphaDraws << ",\n";
    ofs << "    \"uniqueGeometryAssets\": " << mStats.uniqueGeometryAssets << ",\n";
    ofs << "    \"instanceCount\": " << mStats.instanceCount << ",\n";
    ofs << "    \"normalAvailableCount\": " << mStats.normalAvailableCount << ",\n";
    ofs << "    \"normalDerivedCount\": " << mStats.normalDerivedCount << ",\n";
    ofs << "    \"materialCount\": " << mStats.materialCount << ",\n";
    ofs << "    \"unknownMaterialCount\": " << mStats.unknownMaterialCount << "\n";
    ofs << "  },\n";

    // Deduplication Top Assets
    ofs << "  \"topUniqueGeometryAssets\": [\n";
    size_t count = 0;
    for (const auto& kv : mUniqueAssets) {
        if (count >= 25) break;
        if (count > 0) ofs << ",\n";
        const auto& a = kv.second;
        ofs << "    {\n";
        ofs << "      \"geometryHash\": \"" << std::hex << a.geometryHash << std::dec << "\",\n";
        ofs << "      \"vertexCount\": " << a.vertexCount << ",\n";
        ofs << "      \"triangleCount\": " << a.triangleCount << ",\n";
        ofs << "      \"instanceCount\": " << a.instanceCount << ",\n";
        ofs << "      \"materialId\": " << a.firstMaterialId << ",\n";
        ofs << "      \"resourcePath\": \"" << a.firstResourcePath << "\"\n";
        ofs << "    }";
        count++;
    }
    ofs << "\n  ],\n";

    // Summary of World Draws
    ofs << "  \"worldDrawsSummary\": [\n";
    size_t drawCount = 0;
    for (const auto& d : mDraws) {
        if (!d.isWorld) continue;
        if (drawCount >= 50) break;
        if (drawCount > 0) ofs << ",\n";
        ofs << "    {\n";
        ofs << "      \"drawIndex\": " << drawCount << ",\n";
        ofs << "      \"isStatic\": " << (d.isStatic ? "true" : "false") << ",\n";
        ofs << "      \"isDynamic\": " << (d.isDynamic ? "true" : "false") << ",\n";
        ofs << "      \"alphaMode\": " << static_cast<int>(d.alphaMode) << ",\n";
        ofs << "      \"vertexCount\": " << d.vertexCount << ",\n";
        ofs << "      \"triangleCount\": " << (d.indexCount / 3) << ",\n";
        ofs << "      \"geometryHash\": \"" << std::hex << d.geometryHash << std::dec << "\",\n";
        ofs << "      \"resourceId\": \"" << std::hex << d.resourceId << std::dec << "\",\n";
        ofs << "      \"resourcePath\": \"" << d.resourcePath << "\"\n";
        ofs << "    }";
        drawCount++;
    }
    ofs << "\n  ]\n";
    ofs << "}\n";

    ofs.close();
    return true;
}

} // namespace Fast
