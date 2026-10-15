#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include "fast/GeometryCapture.h"

namespace Fast {

enum class SceneLifetimeState : uint8_t {
    EMPTY        = 0,
    BUILDING     = 1,
    READY        = 2,
    INVALIDATING = 3
};

enum InstanceDirtyFlags : uint8_t {
    DIRTY_NONE       = 0,
    DIRTY_TRANSFORM  = 1 << 0,
    DIRTY_GEOMETRY   = 1 << 1,
    DIRTY_MATERIAL   = 1 << 2,
    DIRTY_CREATED    = 1 << 3,
    DIRTY_DESTROYED  = 1 << 4
};

struct AABB {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    void Reset() {
        minX = minY = minZ = 1e30f;
        maxX = maxY = maxZ = -1e30f;
    }

    void Extend(float x, float y, float z) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (z < minZ) minZ = z;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
        if (z > maxZ) maxZ = z;
    }

    bool IsValid() const {
        return minX <= maxX && minY <= maxY && minZ <= maxZ;
    }
};

struct LocalVertex {
    float posX, posY, posZ;         // Espacio Local / Objeto
    float normX, normY, normZ;      // Normal
    float u, v;                     // Coordenadas del tile 0
    float u1, v1;                   // Coordenadas del tile 1
    uint32_t color;                 // Color RGBA
    uint8_t normalState;            // NormalStatus plus source-color provenance flags
    uint8_t sourceAlpha;            // Original F3DVtx.cn[3]
    uint8_t recordLoadedAlpha;      // LoadedVertex.color.a entering capture
    uint8_t capturedAlpha;          // CapturedVertex.color.a
    uint8_t fogMode;                // G_FOG active at RSP load
};

struct GeometryAsset {
    uint64_t id;                    // Hash FNV-1a64 único del asset
    uint64_t geometryHash;          // BLAS identity: local positions, indices and opaque/alpha class
    
    std::vector<LocalVertex> vertexBuffer;
    std::vector<uint32_t> indexBuffer;

    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t triangleCount;

    uint64_t resourceId;            // CRC64 OTR
    std::string resourcePath;       // Ruta canónica OTR
    uint64_t materialId;            // Identificador en MaterialRegistry
    MaterialDefinition material;     // Parámetros exactos del material de este asset
    uint32_t textureSrvIndex;       // Descriptor captured from the raster draw
    uint32_t textureSrvIndex1;      // Descriptor for the second RDP tile
    uint32_t textureTileModes[2];   // Clamp/mirror/filter state
    uint8_t primitiveColor[4];      // Fast3D combiner input
    uint8_t environmentColor[4];    // Fast3D combiner input
    uint8_t fogColor[4];            // Fast3D post-combiner fog input

    uint8_t alphaMode;              // AlphaMode (OPAQUE, ALPHA_TEST, etc.)
    uint32_t sourceGeometryMode;
    uint32_t sourceOtherModeL;
    uint32_t sourceOtherModeH;
    uint64_t sourceCombineMode;
    uint8_t normalState;            // Estado de normal
    AABB boundingBox;               // AABB en espacio local

    uint32_t instanceRefCount;
    uint64_t firstSeenFrame;
    uint64_t lastSeenFrame;
    bool isGlobalReusable;          // Si pertenece a la escena o cuarto
};

struct GeometryInstance {
    uint32_t id;                    // Identificador único de instancia
    uint64_t geometryAssetId;       // Referencia al GeometryAsset propietario

    float worldTransform[4][4];     // Matriz ModelView de mundo sin proyección
    float previousWorldTransform[4][4]; // Matriz del frame anterior (Motion Vectors / Temporal)
    AABB worldAABB;                 // Bounding box en coordenadas mundiales

    uint64_t materialOverride;      // Sobrescritura de material si existe
    int32_t actorId;                // Identificador de actor (-1 si es arquitectura)
    uint32_t flags;

    bool isStatic;                  // true = arquitectura estática
    bool isDynamic;                 // true = actor o entidad dinámica
    uint8_t dirtyFlags;             // InstanceDirtyFlags

    uint64_t lastUpdatedFrame;
};

struct SceneChangeSet {
    std::vector<uint64_t> createdAssets;
    std::vector<uint64_t> removedAssets;
    std::vector<uint32_t> createdInstances;
    std::vector<uint32_t> updatedInstances;
    std::vector<uint32_t> removedInstances;

    void Clear() {
        createdAssets.clear();
        removedAssets.clear();
        createdInstances.clear();
        updatedInstances.clear();
        removedInstances.clear();
    }
};

struct GpuGeometryUploadRequest {
    uint64_t assetId;
    const void* vertexData;
    uint32_t vertexDataSize;
    const void* indexData;
    uint32_t indexDataSize;
    uint32_t vertexCount;
    uint32_t indexCount;
    uint64_t materialId;
};

struct PersistentSceneStats {
    uint32_t totalAssets;
    uint32_t totalInstances;
    uint32_t staticInstances;
    uint32_t dynamicInstances;

    uint32_t opaqueInstances;
    uint32_t alphaTestInstances;
    uint32_t transparentInstances;
    uint32_t additiveInstances;
    uint32_t unknownAlphaInstances;

    uint32_t persistentUiGeometry;      // HARD GATE: 0
    uint32_t zombieGeometryCount;       // HARD GATE: 0
    uint32_t danglingBufferReferences;  // HARD GATE: 0

    uint32_t assetsCreatedThisFrame;
    uint32_t assetsReusedThisFrame;
    uint32_t instancesCreatedThisFrame;
    uint32_t instancesUpdatedThisFrame;
    uint32_t instancesReusedThisFrame;
    uint32_t instancesDestroyedThisFrame;

    size_t memoryUsageBytes;
};

class PersistentScene {
public:
    static PersistentScene& Instance();

    void Init();
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }

    void BeginFrame();
    void EndFrame();

    void SetCurrentScene(int sceneId, int roomId);
    int GetCurrentSceneId() const { return mCurrentSceneId; }
    int GetCurrentRoomId() const { return mCurrentRoomId; }
    SceneLifetimeState GetLifetimeState() const { return mLifetimeState; }

    // Integración directa con CapturedDraw proveniente de GeometryCapture
    void IngestCapturedDraw(const CapturedDraw& draw,
                            const std::vector<CapturedVertex>& globalVertices,
                            const std::vector<uint32_t>& globalIndices);

    // Búsqueda y consultas
    const GeometryAsset* FindAsset(uint64_t assetId) const;
    const GeometryInstance* FindInstance(uint32_t instanceId) const;

    const std::unordered_map<uint64_t, GeometryAsset>& GetAssets() const { return mAssets; }
    const std::vector<GeometryInstance>& GetInstances() const { return mInstances; }
    const SceneChangeSet& GetChangeSet() const { return mChangeSet; }
    const PersistentSceneStats& GetStats() const { return mStats; }

    // Snapshots V2
    bool ExportSnapshotV2(const std::string& outputPath, const std::string& captureSessionId);

    void InvalidateScene();
    void InvalidateRoom();
    void ClearAll();

private:
    PersistentScene();
    ~PersistentScene();

    uint64_t ComputeAssetHash(const std::vector<LocalVertex>& verts, const std::vector<uint32_t>& indices);
    void CalculateInstanceWorldAABB(GeometryInstance& inst, const GeometryAsset& asset);

    bool mEnabled;
    bool mInitialized;
    int mCurrentSceneId;
    int mCurrentRoomId;
    uint64_t mFrameCounter;

    SceneLifetimeState mLifetimeState;

    std::unordered_map<uint64_t, GeometryAsset> mAssets;
    std::vector<GeometryInstance> mInstances;
    std::unordered_map<uint64_t, std::vector<uint32_t>> mAssetToInstanceMap;

    SceneChangeSet mChangeSet;
    PersistentSceneStats mStats;

    uint32_t mNextInstanceId;

    // Buffer de pre-reserva de subida GPU conceptual
    std::vector<GpuGeometryUploadRequest> mUploadRequests;
};

} // namespace Fast
