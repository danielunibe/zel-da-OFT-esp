#pragma once

#if defined(_WIN32)

#include <d3d12.h>
#include <array>
#include <cstddef>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <limits>
#include "fast/PersistentScene.h"

namespace Fast {

constexpr uint32_t kDxrSurfaceFrameCount = 3;
constexpr uint32_t kDxrSurfaceTextureDescriptorCount = 4096;
constexpr uint32_t kDxrFrameConstantStride = 1536; // 256-byte aligned CBV slice
constexpr uint32_t kDxrShaderAbiVersion = 3; // Fast3D dual-tile/material-combiner record layout

// One CPU layout for both DXR pipelines. AnyHitShadow also evaluates material
// data and reads projection/bounds near the END of FrameConstants in HLSL.
struct DxrFrameConstants {
    float invProjection[4][4];
    float invView[4][4];
    float lightDir[4];
    float screenSize[4];
    float params[4];
    float quality[4];
    float rasterViewport[4]; // normalized left, bottom, width, height
    float sceneLightColor[4];
    float ambientLightColor[4];
    float frameInfo[4];
    float diagnosticInfo[4];
    float secondaryLightDirection[4];
    float secondaryLightColor[4];
    float pointLightPosition[32][4];
    float pointLightOptics[32][4];
    float projection[4][4];
    uint32_t diagnosticBounds[4];
    float traceSettings[4]; // reflection/transmission/GI ranges; w: reflection strength for premixed dielectric optics
};
static_assert(sizeof(DxrFrameConstants) == 1424 && sizeof(DxrFrameConstants) <= kDxrFrameConstantStride,
              "CPU FrameConstants must match the shared DXR HLSL layout");
static_assert(offsetof(DxrFrameConstants, rasterViewport) == 192 &&
              offsetof(DxrFrameConstants, projection) == 1328 &&
              offsetof(DxrFrameConstants, diagnosticBounds) == 1392 &&
              offsetof(DxrFrameConstants, traceSettings) == 1408,
              "DXR constants offsets must match dxr_shadow.hlsl");

struct DxrHitVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float uv1[2];
    uint32_t color;
    uint32_t normalState;
    uint32_t reserved[2];
};
static_assert(sizeof(DxrHitVertex) == 56 && offsetof(DxrHitVertex, uv1) == 32 &&
              offsetof(DxrHitVertex, color) == 40 && offsetof(DxrHitVertex, reserved) == 48,
              "DXR hit vertex layout must match the HLSL 56-byte record");

struct DxrInstanceMaterialData {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t materialType;
    uint32_t textureIndex;
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t alphaMode;
    uint32_t flags;
    float roughness;
    float metallic;
    float specular;
    float reflectivity;
    float emissiveStrength;
    float normalStrength;
    float wetness;
    float subsurfaceHint;
    float baseColor[4];
    uint32_t textureIndex1;
    uint32_t samplerFlags;
    uint32_t combineModeLow;
    uint32_t combineModeHigh;
    uint32_t primitiveColor;
    uint32_t environmentColor;
    uint32_t reserved[2];
};
static_assert(sizeof(DxrInstanceMaterialData) == 112 &&
              offsetof(DxrInstanceMaterialData, baseColor) == 64 &&
              offsetof(DxrInstanceMaterialData, textureIndex1) == 80 &&
              offsetof(DxrInstanceMaterialData, primitiveColor) == 96,
              "DXR material record layout must match the HLSL 112-byte record");

struct DxrSurfaceFrameBuffers {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexData;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexData;
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceData;
    std::vector<uint32_t> textureSrvIndices;
    std::vector<DxrInstanceMaterialData> materialRecords;
    uint32_t instanceCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

inline bool DxrSurfaceFrameBoundsValid(const DxrSurfaceFrameBuffers& frame) {
    // Root SRVs expose GPU addresses without buffer sizes. Validate physical
    // allocations on CPU, then publish logical counts for shader index guards.
    return frame.vertexData && frame.indexData && frame.instanceData &&
        frame.vertexCount != 0 && frame.indexCount >= 3 && frame.indexCount % 3 == 0 &&
        frame.instanceCount != 0 && frame.instanceCount == frame.materialRecords.size() &&
        frame.vertexCount <= UINT32_MAX / sizeof(DxrHitVertex) &&
        frame.indexCount <= UINT32_MAX / sizeof(uint32_t) &&
        frame.instanceCount <= UINT32_MAX / sizeof(DxrInstanceMaterialData) &&
        frame.vertexCount <= frame.vertexData->GetDesc().Width / sizeof(DxrHitVertex) &&
        frame.indexCount <= frame.indexData->GetDesc().Width / sizeof(uint32_t) &&
        frame.instanceCount <= frame.instanceData->GetDesc().Width / sizeof(DxrInstanceMaterialData);
}

enum class GeometryClassification : uint8_t {
    STATIC = 0,
    RIGID_DYNAMIC = 1,
    DEFORMING_DYNAMIC = 2,
    BILLBOARD = 3,
    PARTICLE = 4,
    UI = 5,
    UNKNOWN = 6
};

struct DxrBlasRecord {
    uint64_t geometryAssetId;
    uint64_t resourceIdentity;
    std::string resourcePath;

    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t triangleCount;
    uint32_t vertexStride;
    DXGI_FORMAT vertexFormat;
    DXGI_FORMAT indexFormat;

    AABB bounds;
    uint64_t materialIdentity;
    GeometryClassification classification;

    Microsoft::WRL::ComPtr<ID3D12Resource> vbResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> ibResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> blasBuffer;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags;

    bool dirtyState;
    uint64_t lastBuildFrame;
    uint64_t lastUsedFrame;
    uint64_t geometryMemory;
    uint64_t buildTimeUs;
};

struct DxrAccelerationStructureStats {
    bool d3d12Device5Passed = false;
    bool d3d12CommandList4Passed = false;
    bool firstStaticBlasPassed = false;
    bool firstTlasPassed = false;

    uint32_t blasCreated = 0;
    uint32_t blasReused = 0;
    uint32_t blasEvictions = 0;
    uint32_t staticBlasCount = 0;
    uint32_t dynamicBlasCount = 0;
    uint32_t dynamicBlasRebuilds = 0;
    uint32_t dynamicBlasUpdates = 0;

    uint32_t tlasBuilds = 0;
    uint32_t tlasUpdates = 0;
    uint32_t tlasInstanceCount = 0;

    uint32_t staticInstances = 0;
    uint32_t rigidDynamicInstances = 0;
    uint32_t deformingDynamicInstances = 0;
    uint32_t billboardInstances = 0;
    uint32_t uiDxrInstances = 0;         // HARD GATE: 0
    uint32_t unknownDxrInstances = 0;

    uint32_t zombieDxrInstances = 0;       // HARD GATE: 0
    uint32_t danglingBlasReferences = 0;   // HARD GATE: 0

    uint64_t blasMemory = 0;
    uint64_t tlasMemory = 0;
    uint64_t scratchPeak = 0;
    uint64_t dxrGeometryMemory = 0;
    uint32_t dxrDebugErrors = 0;           // HARD GATE: 0

    // Pilot static BLAS telemetría
    uint64_t firstStaticBlasId = 0;
    uint32_t firstStaticBlasVertices = 0;
    uint32_t firstStaticBlasTriangles = 0;
    uint64_t firstStaticBlasResultSize = 0;
    uint64_t firstStaticBlasScratchSize = 0;
    uint64_t firstStaticBlasBuildTimeUs = 0;

    // Trazabilidad de entidades
    bool masterSwordFound = false;
    uint64_t masterSwordAssetId = 0;
    float masterSwordTransform[3][4] = {};

    bool linkFound = false;
    uint32_t linkPartCount = 0;
    bool linkIsDeforming = false;
};

class DxrAccelerationStructureManager {
public:
    static DxrAccelerationStructureManager& Instance();

    bool Init(ID3D12Device* device);
    bool IsSupported() const { return mSupported; }
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled && mInitialized && mSupported; }

    void ProcessFrame(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                      uint32_t frameIndex, uint64_t fenceValue, uint64_t completedFenceValue);

    void HandleSceneTransition();
    void Reset();

    const DxrAccelerationStructureStats& GetStats() const { return mStats; }
    ID3D12Resource* GetTlasBuffer() const { return mTlasBuffer.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetTlasGpuVA() const {
        return mTlasBuffer ? mTlasBuffer->GetGPUVirtualAddress() : 0;
    }
    const DxrSurfaceFrameBuffers& GetSurfaceFrameBuffers(uint32_t frameIndex) const {
        return mSurfaceFrames[frameIndex % kDxrSurfaceFrameCount];
    }

    void DumpReport() const;

private:
    DxrAccelerationStructureManager();
    ~DxrAccelerationStructureManager();

    GeometryClassification ClassifyGeometry(const GeometryAsset& asset, const GeometryInstance* inst) const;

    bool BuildBlasForAsset(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmdList4,
                           const GeometryAsset& asset, DxrBlasRecord& record,
                           uint32_t frameIndex);

    bool BuildTlas(ID3D12Device5* device5, ID3D12GraphicsCommandList4* cmdList4,
                   const std::vector<GeometryInstance>& instances,
                   const std::unordered_map<uint64_t, GeometryAsset>& assets,
                   uint32_t frameIndex);

    bool UpdateSurfaceFrameBuffers(ID3D12Device* device, uint32_t frameIndex,
                                   const std::vector<DxrHitVertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   const std::vector<DxrInstanceMaterialData>& materials,
                                   const std::vector<uint32_t>& textureSrvIndices);

    void EvictStaleBlas(const std::unordered_set<uint64_t>& activeAssetIds, uint64_t frameIndex);
    void RetireBlasRecord(DxrBlasRecord& record, uint64_t retireFrame);

    void AllocateOrGrowBuffer(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
                              size_t requiredSize, D3D12_RESOURCE_FLAGS flags,
                              D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType);

    bool mInitialized;
    bool mSupported;
    bool mEnabled;

    Microsoft::WRL::ComPtr<ID3D12Device5> mDevice5;

    // Cache de BLAS por GeometryAssetId
    std::unordered_map<uint64_t, DxrBlasRecord> mBlasCache;

    // TLAS resources
    Microsoft::WRL::ComPtr<ID3D12Resource> mTlasBuffer;
    size_t mTlasAllocatedSize;
    uint64_t mTlasSignature;
    bool mTlasValid;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTlasScratchBuffer;
    size_t mTlasScratchAllocatedSize;

    // Instance Descriptors buffer (Upload heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceDescBuffer;
    size_t mInstanceDescAllocatedSize;

    // Frame-ring bridge from Fast3D CPU geometry/materials to closest-hit.
    DxrSurfaceFrameBuffers mSurfaceFrames[kDxrSurfaceFrameCount];

    // Shared scratch buffer para BLAS builds
    Microsoft::WRL::ComPtr<ID3D12Resource> mBlasScratchBuffer;
    size_t mBlasScratchAllocatedSize;

    // Recolección diferida de recursos sustituidos / liberados
    struct DeferredReleaseResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint64_t fenceValue;
    };
    std::vector<DeferredReleaseResource> mDeferredReleases;

    DxrAccelerationStructureStats mStats;
    uint64_t mFrameIndexCounter;
    uint64_t mSubmissionFence = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 3> mInstanceDescFrames;
    std::array<uint64_t, 3> mInstanceDescFrameSizes{};
    uint64_t mLastTelemetryFrame;
    uint32_t mLastTelemetryBlasCreated;
    uint32_t mLastTelemetryBlasEvictions;
};

} // namespace Fast

#endif
