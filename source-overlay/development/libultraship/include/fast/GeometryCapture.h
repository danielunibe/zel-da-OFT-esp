#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "fast/MaterialRegistry.h"

namespace Fast {

enum NormalStatus : uint8_t {
    NORMAL_SOURCE_UNAVAILABLE = 0,
    NORMAL_SOURCE_AVAILABLE   = 1,
    NORMAL_DERIVED            = 2
};

enum AlphaMode : uint8_t {
    ALPHA_OPAQUE      = 0,
    ALPHA_TEST        = 1,
    ALPHA_TRANSPARENT = 2,
    ALPHA_ADDITIVE    = 3,
    ALPHA_UNKNOWN     = 4
};

enum SurfaceCategory : uint8_t {
    SURFACE_WORLD_STATIC   = 0,
    SURFACE_ACTOR_DYNAMIC  = 1,
    SURFACE_BILLBOARD      = 2,
    SURFACE_SPRITE         = 3,
    SURFACE_PARTICLE       = 4,
    SURFACE_SKYBOX         = 5,
    SURFACE_UI_2D          = 6
};

struct CapturedVertex {
    float posX, posY, posZ;         // Espacio Local de Objeto / Modelo
    float worldX, worldY, worldZ;   // Espacio de Mundo (sin matriz de proyección)
    float normX, normY, normZ;      // Normal unitaria
    float u, v;                     // Coordenadas UV normalizadas
    uint32_t color;                 // Color RGBA
    uint8_t normalStatus;           // NormalStatus
    uint8_t sourceAlpha;            // Original F3DVtx.cn[3] before fog reuse
    uint8_t recordLoadedAlpha;      // LoadedVertex.color.a passed to GeometryCapture
    uint8_t fogMode;                // G_FOG active when this RSP vertex was loaded
};

struct CapturedDraw {
    uint32_t vertexStart;
    uint32_t vertexCount;
    uint32_t indexStart;
    uint32_t indexCount;

    float worldTransform[4][4];     // Matriz ModelView de instancia sin proyección

    uint64_t geometryHash;          // Hash único de geometría local e índices
    uint64_t materialId;            // Identificador de MaterialRegistry
    MaterialDefinition material;     // Snapshot de parámetros del draw capturado
    uint64_t resourceId;            // Hash CRC64 estable del recurso de textura
    std::string resourcePath;       // Ruta OTR si está disponible

    uint32_t geometryMode;
    uint32_t otherModeL;
    uint32_t otherModeH;
    uint64_t combineMode;
    uint8_t alphaMode;              // AlphaMode
    uint8_t surfaceCategory;        // SurfaceCategory

    bool isStatic;
    bool isDynamic;
    bool isUI;
    bool isWorld;
};

struct CapturedAssetSummary {
    uint64_t geometryHash;
    uint32_t vertexCount;
    uint32_t triangleCount;
    uint32_t instanceCount;
    uint64_t firstMaterialId;
    std::string firstResourcePath;
};

struct SceneCaptureStats {
    uint32_t totalDraws;
    uint32_t worldDraws;
    uint32_t uiDraws;

    uint32_t totalVertices;
    uint32_t totalIndices;
    uint32_t totalTriangles;

    uint32_t staticDraws;
    uint32_t dynamicDraws;

    uint32_t opaqueDraws;
    uint32_t alphaTestDraws;
    uint32_t transparentDraws;
    uint32_t unknownAlphaDraws;

    uint32_t uniqueGeometryAssets;
    uint32_t instanceCount;

    uint32_t normalAvailableCount;
    uint32_t normalDerivedCount;
    uint32_t normalUnavailableCount;

    uint32_t materialCount;
    uint32_t unknownMaterialCount;
};

class GeometryCapture {
public:
    static GeometryCapture& Instance();

    void Init();
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }

    void BeginFrame();
    void EndFrame();

    void SetCurrentScene(int sceneId, int roomId);
    int GetCurrentSceneId() const { return mCurrentSceneId; }
    int GetCurrentRoomId() const { return mCurrentRoomId; }

    // Intercepción durante el procesamiento de vértices y triángulos
    void RecordLoadedVertex(uint16_t vtxIdx, float objX, float objY, float objZ,
                            float normX, float normY, float normZ, bool hasNormal,
                            float u, float v, uint32_t color,
                            uint8_t sourceAlpha, bool fogMode);

    void RecordTriangle(uint16_t idx0, uint16_t idx1, uint16_t idx2);

    void FlushDraw(const float transform[4][4], uint64_t materialId,
                   const MaterialDefinition& material,
                   uint64_t resourceId, const std::string& resourcePath,
                   uint32_t geometryMode, uint32_t otherModeL, uint32_t otherModeH,
                   uint64_t combineMode, bool isDepthTest, bool isDecal);

    // Exportación de snapshots
    bool ExportSnapshot(const std::string& outputPath);
    void RequestSnapshot(const std::string& outputPath) {
        mRequestedSnapshotPath = outputPath;
        mSnapshotRequested = true;
    }

    const SceneCaptureStats& GetStats() const { return mStats; }
    const std::vector<CapturedDraw>& GetDraws() const { return mDraws; }
    const std::vector<CapturedVertex>& GetVertices() const { return mVertices; }
    const std::string& GetCaptureSessionId() const { return mCaptureSessionId; }

    void ClearScene();

private:
    GeometryCapture();
    ~GeometryCapture();

    uint64_t ComputeGeometryHash(uint32_t vtxStart, uint32_t vtxCount,
                                 uint32_t idxStart, uint32_t idxCount);

    bool mEnabled;
    bool mInitialized;
    std::string mCaptureSessionId;
    int mCurrentSceneId;
    int mCurrentRoomId;
    uint64_t mFrameCounter;

    bool mSnapshotRequested;
    std::string mRequestedSnapshotPath;

    // Buffers persistentes preasignados
    std::vector<CapturedVertex> mVertices;
    std::vector<uint32_t> mIndices;
    std::vector<CapturedDraw> mDraws;

    // Buffer temporal de vértices cargados en RSP para el microcódigo actual
    struct RSPVertexCache {
        float objX, objY, objZ;
        float normX, normY, normZ;
        bool hasNormal;
        float u, v;
        uint32_t color;
        uint8_t sourceAlpha;
        bool fogMode;
        bool valid;
    };
    RSPVertexCache mRspVertices[128];

    // Acumulador de triángulos pendientes antes de Flush
    std::vector<uint32_t> mPendingIndices;
    std::vector<CapturedVertex> mPendingVertices;

    // Deduplicación e instancing
    std::unordered_map<uint64_t, CapturedAssetSummary> mUniqueAssets;

    SceneCaptureStats mStats;
};

} // namespace Fast
