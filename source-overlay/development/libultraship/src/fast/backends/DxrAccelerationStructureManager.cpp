#if defined(_WIN32)

#include "fast/backends/DxrAccelerationStructureManager.h"
#include "fast/Fast3DAlphaPolicy.h"
#include "fast/GeometryCapture.h"
#include "fast/VertexAlphaContract.h"
#include "fast/backends/TemporalManager.h"
#include "fast/couch_diagnostics.h"
#include "fast/MaterialRegistry.h"
#include "fast/lus_gbi.h"
#include "fast/backends/TemporalManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
#include <spdlog/spdlog.h>

namespace Fast {

namespace {
constexpr uint64_t kBlasIdleFrameLimit = 300;
constexpr size_t kBlasEntryLimit = 4096;
constexpr uint64_t kBlasMemoryLimit = 128ull * 1024ull * 1024ull;
}

static void LogDxr(const std::string& msg) {
    SPDLOG_INFO(msg);
    printf("%s\n", msg.c_str());
    fflush(stdout);

    FILE* f = fopen(couch_diagnostics_path(), "a");
    if (f) {
        fprintf(f, "%s\n", msg.c_str());
        fclose(f);
    }
    const char* harness = std::getenv("COUCH_DXR_HARNESS");
    const char* outputRoot = std::getenv("COUCH_DXR_OUTPUT");
    if (harness && std::strcmp(harness, "1") == 0 && outputRoot && *outputRoot) {
        std::ofstream runLog(std::string(outputRoot) + "\\harness-dxr.log", std::ios::app);
        if (runLog) runLog << msg << '\n';
    }
}

DxrAccelerationStructureManager& DxrAccelerationStructureManager::Instance() {
    static DxrAccelerationStructureManager sInstance;
    return sInstance;
}

DxrAccelerationStructureManager::DxrAccelerationStructureManager()
    : mInitialized(false), mSupported(false), mEnabled(false),
      mTlasAllocatedSize(0), mTlasSignature(0), mTlasValid(false),
      mTlasScratchAllocatedSize(0),
      mInstanceDescAllocatedSize(0), mBlasScratchAllocatedSize(0),
      mFrameIndexCounter(0), mLastTelemetryFrame(0),
      mLastTelemetryBlasCreated(0), mLastTelemetryBlasEvictions(0) {
}

DxrAccelerationStructureManager::~DxrAccelerationStructureManager() {
    Reset();
}

bool DxrAccelerationStructureManager::Init(ID3D12Device* device) {
    if (mInitialized) return mSupported;
    mInitialized = true;
    mStats = {};

    if (!device) {
        LogDxr("[COUCH-DXR] Init failed: Device is null");
        return false;
    }

    // 1. HARD CAPABILITY GATE: Consulta de soporte DXR
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (FAILED(hr) || options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
        LogDxr("[COUCH-DXR] HARD GATE: Raytracing Tier not supported by hardware (Tier < 1.0)");
        mSupported = false;
        return false;
    }

    // 2. HARD CAPABILITY GATE: Obtener ID3D12Device5
    HRESULT hrDev5 = device->QueryInterface(IID_PPV_ARGS(&mDevice5));
    if (FAILED(hrDev5) || !mDevice5) {
        LogDxr("[COUCH-DXR] HARD GATE: ID3D12Device5 interface could not be acquired");
        mSupported = false;
        return false;
    }

    mStats.d3d12Device5Passed = true;
    mSupported = true;

    const unsigned int raytracingTier = static_cast<unsigned int>(options5.RaytracingTier);
    const std::string tierStr = raytracingTier >= 12 ? "TIER_1_2" :
                                raytracingTier >= 11 ? "TIER_1_1" : "TIER_1_0";
    LogDxr("[COUCH-DXR] Hardware DXR Capability Confirmed: " + tierStr);
    LogDxr("[COUCH-DXR] ID3D12Device5: PASS");

    const char* envDxr = getenv("COUCH_DXR_AS");
    const char* envShadow = getenv("COUCH_DXR_SHADOW");
    const bool explicitlyDisabled = envDxr && strcmp(envDxr, "0") == 0;
    const bool explicitlyRequested = (envDxr && strcmp(envDxr, "1") == 0) ||
                                     (envShadow && strcmp(envShadow, "1") == 0);
    const bool useHardwareDefault = !envDxr && !envShadow;
    if (!explicitlyDisabled && (explicitlyRequested || useHardwareDefault)) {
        mEnabled = true;
        LogDxr(std::string("[COUCH-DXR] DXR Acceleration Structures ENABLED via ") +
               (useHardwareDefault ? "supported-hardware default" : "environment"));
    }

    return true;
}

void DxrAccelerationStructureManager::AllocateOrGrowBuffer(ID3D12Device* device,
                                                           Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
                                                           size_t requiredSize,
                                                           D3D12_RESOURCE_FLAGS flags,
                                                           D3D12_RESOURCE_STATES initialState,
                                                           D3D12_HEAP_TYPE heapType) {
    // Alinear tamaño a 256 bytes conforme a especificaciones DXR
    size_t alignedSize = (requiredSize + 255) & ~255;
    if (alignedSize < 65536) alignedSize = 65536; // Mínimo 64 KB

    if (buffer) {
        D3D12_RESOURCE_DESC desc = buffer->GetDesc();
        if (desc.Width >= alignedSize) {
            return; // El buffer actual ya es suficientemente grande
        }
        // Guardar para desecho diferido
        mDeferredReleases.push_back({ buffer, mSubmissionFence });
        buffer.Reset();
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                 initialState, nullptr, IID_PPV_ARGS(&buffer));
    if (FAILED(hr)) {
        LogDxr("[COUCH-DXR][ERROR] Failed to allocate buffer of size " + std::to_string(alignedSize) +
               ", hr=0x" + std::to_string(hr));
        mStats.dxrDebugErrors++;
    }
}

GeometryClassification DxrAccelerationStructureManager::ClassifyGeometry(const GeometryAsset& asset,
                                                                         const GeometryInstance* inst) const {
    if (inst && inst->isStatic) {
        return GeometryClassification::STATIC;
    }
    if (inst && inst->isDynamic) {
        return GeometryClassification::RIGID_DYNAMIC;
    }
    if (asset.isGlobalReusable) {
        return GeometryClassification::STATIC;
    }
    return GeometryClassification::UNKNOWN;
}

bool DxrAccelerationStructureManager::BuildBlasForAsset(ID3D12Device5* device5,
                                                       ID3D12GraphicsCommandList4* cmdList4,
                                                       const GeometryAsset& asset,
                                                       DxrBlasRecord& record,
                                                       uint32_t frameIndex) {
    if (asset.vertexCount == 0 || asset.indexCount < 3 || asset.indexCount % 3 != 0 ||
        asset.vertexCount != asset.vertexBuffer.size() || asset.indexCount != asset.indexBuffer.size()) {
        return false;
    }
    for (const auto& vertex : asset.vertexBuffer) {
        if (!std::isfinite(vertex.posX) || !std::isfinite(vertex.posY) || !std::isfinite(vertex.posZ)) return false;
    }
    for (uint32_t index : asset.indexBuffer) {
        if (index >= asset.vertexCount) return false;
    }

    auto startT = std::chrono::high_resolution_clock::now();

    record.geometryAssetId = asset.geometryHash; // Shared BLAS key, independent of surface attributes
    record.resourceIdentity = asset.resourceId;
    record.resourcePath = asset.resourcePath;
    record.vertexCount = asset.vertexCount;
    record.indexCount = asset.indexCount;
    record.triangleCount = asset.triangleCount;
    record.vertexStride = sizeof(DxrHitVertex);
    record.vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    record.indexFormat = DXGI_FORMAT_R32_UINT;
    record.bounds = asset.boundingBox;
    record.materialIdentity = asset.materialId;
    record.classification = ClassifyGeometry(asset, nullptr);

    // Build flags según política: PREFER_FAST_TRACE para static, PREFER_FAST_BUILD para dynamic
    if (record.classification == GeometryClassification::STATIC) {
        record.buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    } else {
        record.buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    }

    // Keep the DXR position at byte zero for triangle intersection while
    // preserving the same vertex's normal, UV, vertex color and normal source
    // for ClosestHit material evaluation.
    std::vector<DxrHitVertex> hitVertices;
    hitVertices.reserve(asset.vertexBuffer.size());
    for (const auto& v : asset.vertexBuffer) {
        DxrHitVertex hitVertex{};
        hitVertex.position[0] = v.posX;
        hitVertex.position[1] = v.posY;
        hitVertex.position[2] = v.posZ;
        hitVertex.normal[0] = v.normX;
        hitVertex.normal[1] = v.normY;
        hitVertex.normal[2] = v.normZ;
        hitVertex.uv[0] = v.u;
        hitVertex.uv[1] = v.v;
        hitVertex.uv1[0] = v.u1;
        hitVertex.uv1[1] = v.v1;
        // Keep the captured/Fast3D raster alpha (including fog) in the asset;
        // only the DXR vertex stream substitutes the original vertex alpha.
        hitVertex.color = PackDxrVertexColor(v.color, v.sourceAlpha);
        hitVertex.normalState = v.normalState;
        hitVertex.reserved[1] = (v.sourceColor >> 24u) & 0xFFu;
        hitVertex.reserved[1] |= (v.sourceColor & 0xFFu) << 8u;
        hitVertex.reserved[1] |= (v.sourceColor & 0xFF00u) << 8u;
        hitVertex.reserved[1] |= (v.sourceColor & 0xFF0000u) << 8u;
        hitVertices.push_back(hitVertex);
    }
    const size_t vbSize = hitVertices.size() * sizeof(DxrHitVertex);
    const size_t ibSize = asset.indexBuffer.size() * sizeof(uint32_t);
    record.geometryMemory = static_cast<uint64_t>(vbSize) + static_cast<uint64_t>(ibSize);

    // 2. Crear Vertex Buffer e Index Buffer en DEFAULT heap
    AllocateOrGrowBuffer(device5, record.vbResource, vbSize,
                         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_HEAP_TYPE_DEFAULT);

    AllocateOrGrowBuffer(device5, record.ibResource, ibSize,
                         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_HEAP_TYPE_DEFAULT);

    if (!record.vbResource || !record.ibResource) {
        return false;
    }

    // 3. Crear Staging Buffers en UPLOAD heap para copiar a GPU
    Microsoft::WRL::ComPtr<ID3D12Resource> stagingVb;
    Microsoft::WRL::ComPtr<ID3D12Resource> stagingIb;
    AllocateOrGrowBuffer(device5, stagingVb, vbSize, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
    AllocateOrGrowBuffer(device5, stagingIb, ibSize, D3D12_RESOURCE_FLAG_NONE,
                         D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);

    if (!stagingVb || !stagingIb) {
        return false;
    }

    uint8_t* pMappedVb = nullptr;
    stagingVb->Map(0, nullptr, reinterpret_cast<void**>(&pMappedVb));
    memcpy(pMappedVb, hitVertices.data(), vbSize);
    stagingVb->Unmap(0, nullptr);

    uint8_t* pMappedIb = nullptr;
    stagingIb->Map(0, nullptr, reinterpret_cast<void**>(&pMappedIb));
    memcpy(pMappedIb, asset.indexBuffer.data(), ibSize);
    stagingIb->Unmap(0, nullptr);

    cmdList4->CopyBufferRegion(record.vbResource.Get(), 0, stagingVb.Get(), 0, vbSize);
    cmdList4->CopyBufferRegion(record.ibResource.Get(), 0, stagingIb.Get(), 0, ibSize);

    // Transición de destino de copia a lectura de sombreador/DXR
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = record.vbResource.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = record.ibResource.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList4->ResourceBarrier(2, barriers);

    // Preservar staging buffers para que sigan vivos hasta el fence
    mDeferredReleases.push_back({ stagingVb, mSubmissionFence });
    mDeferredReleases.push_back({ stagingIb, mSubmissionFence });

    // 4. Configurar D3D12_RAYTRACING_GEOMETRY_DESC
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Triangles.VertexBuffer.StartAddress = record.vbResource->GetGPUVirtualAddress();
    geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrHitVertex);
    geomDesc.Triangles.VertexCount = asset.vertexCount;
    geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geomDesc.Triangles.IndexBuffer = record.ibResource->GetGPUVirtualAddress();
    geomDesc.Triangles.IndexCount = asset.indexCount;
    geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geomDesc.Triangles.Transform3x4 = 0;
    geomDesc.Flags = (asset.alphaMode == 0) ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                                           : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

    // 5. Prebuild Info para BLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs = {};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.Flags = record.buildFlags;
    blasInputs.NumDescs = 1;
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.pGeometryDescs = &geomDesc;

    device5->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &record.prebuildInfo);

    // 6. Asignar BLAS Result Buffer y Scratch Buffer
    AllocateOrGrowBuffer(device5, record.blasBuffer, record.prebuildInfo.ResultDataMaxSizeInBytes,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_HEAP_TYPE_DEFAULT);

    AllocateOrGrowBuffer(device5, mBlasScratchBuffer, record.prebuildInfo.ScratchDataSizeInBytes,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_HEAP_TYPE_DEFAULT);

    if (record.prebuildInfo.ScratchDataSizeInBytes > mStats.scratchPeak) {
        mStats.scratchPeak = record.prebuildInfo.ScratchDataSizeInBytes;
    }

    if (!record.blasBuffer || !mBlasScratchBuffer) {
        return false;
    }

    // 7. Emitir Build Command en GPU
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = record.blasBuffer->GetGPUVirtualAddress();
    buildDesc.Inputs = blasInputs;
    buildDesc.ScratchAccelerationStructureData = mBlasScratchBuffer->GetGPUVirtualAddress();

    cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    auto endT = std::chrono::high_resolution_clock::now();
    record.buildTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(endT - startT).count();
    record.lastBuildFrame = frameIndex;
    record.lastUsedFrame = mFrameIndexCounter;
    record.dirtyState = false;

    mStats.blasMemory += record.prebuildInfo.ResultDataMaxSizeInBytes;
    mStats.dxrGeometryMemory += record.geometryMemory;

    if (record.classification == GeometryClassification::STATIC) {
        mStats.staticBlasCount++;
        if (!mStats.firstStaticBlasPassed) {
            mStats.firstStaticBlasPassed = true;
            mStats.firstStaticBlasId = asset.id;
            mStats.firstStaticBlasVertices = asset.vertexCount;
            mStats.firstStaticBlasTriangles = asset.triangleCount;
            mStats.firstStaticBlasResultSize = record.prebuildInfo.ResultDataMaxSizeInBytes;
            mStats.firstStaticBlasScratchSize = record.prebuildInfo.ScratchDataSizeInBytes;
            mStats.firstStaticBlasBuildTimeUs = record.buildTimeUs;

            LogDxr("[COUCH-DXR] FIRST_STATIC_BLAS = PASS | AssetId=" + std::to_string(asset.id) +
                   " | Verts=" + std::to_string(asset.vertexCount) +
                   " | Tris=" + std::to_string(asset.triangleCount) +
                   " | ResultSize=" + std::to_string(record.prebuildInfo.ResultDataMaxSizeInBytes) +
                   " B | Scratch=" + std::to_string(record.prebuildInfo.ScratchDataSizeInBytes) + " B");
        }
    } else {
        mStats.dynamicBlasCount++;
    }

    return true;
}

bool DxrAccelerationStructureManager::UpdateSurfaceFrameBuffers(
    ID3D12Device* device, uint32_t frameIndex,
    const std::vector<DxrHitVertex>& vertices,
    const std::vector<uint32_t>& indices,
    const std::vector<DxrInstanceMaterialData>& materials,
    const std::vector<uint32_t>& textureSrvIndices) {
    if (!device || frameIndex >= kDxrSurfaceFrameCount) return false;

    auto& frame = mSurfaceFrames[frameIndex];
    const auto upload = [this, device](Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
                                       const void* source, size_t byteCount) {
        const size_t allocationSize = std::max<size_t>(byteCount, 16);
        AllocateOrGrowBuffer(device, buffer, allocationSize, D3D12_RESOURCE_FLAG_NONE,
                             D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_HEAP_TYPE_UPLOAD);
        if (!buffer) return false;

        uint8_t* mapped = nullptr;
        if (FAILED(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) return false;
        if (byteCount > 0 && source) {
            memcpy(mapped, source, byteCount);
        } else {
            memset(mapped, 0, allocationSize);
        }
        const D3D12_RANGE writtenRange{ 0, byteCount > 0 ? byteCount : allocationSize };
        buffer->Unmap(0, &writtenRange);
        return true;
    };

    const bool verticesReady = upload(frame.vertexData, vertices.data(), vertices.size() * sizeof(DxrHitVertex));
    const bool indicesReady = upload(frame.indexData, indices.data(), indices.size() * sizeof(uint32_t));
    const bool materialsReady = upload(frame.instanceData, materials.data(),
                                       materials.size() * sizeof(DxrInstanceMaterialData));
    if (!verticesReady || !indicesReady || !materialsReady) return false;

    frame.textureSrvIndices = textureSrvIndices;
    frame.materialRecords = materials;
    frame.instanceCount = static_cast<uint32_t>(materials.size());
    frame.vertexCount = static_cast<uint32_t>(vertices.size());
    frame.indexCount = static_cast<uint32_t>(indices.size());
    return true;
}

bool DxrAccelerationStructureManager::BuildTlas(ID3D12Device5* device5,
                                               ID3D12GraphicsCommandList4* cmdList4,
                                               const std::vector<GeometryInstance>& instances,
                                               const std::unordered_map<uint64_t, GeometryAsset>& assets,
                                               uint32_t frameIndex) {
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    instanceDescs.reserve(instances.size());
    std::vector<DxrHitVertex> surfaceVertices;
    std::vector<uint32_t> surfaceIndices;
    std::vector<DxrInstanceMaterialData> materialRecords;
    static std::unordered_set<std::string> loggedAlphaPolicyStates;
    static std::unordered_set<std::string> loggedDxrMaterialRecords;
    std::vector<uint32_t> textureSrvIndices;
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> assetOffsets;
    std::unordered_map<uint32_t, uint32_t> compactTextureIndices;
    auto& sourceFrame = mSurfaceFrames[frameIndex];
    sourceFrame.emitters.clear();
    sourceFrame.identities.clear();
    std::vector<std::string> skyboxCandidateRecords;
    const char* skyboxAuditEnv = std::getenv("COUCH_DXR_SKYBOX_AUDIT");
    const bool skyboxAuditRequested = skyboxAuditEnv && strcmp(skyboxAuditEnv, "1") == 0;
    static bool skyboxAuditLogged = false;
    materialRecords.reserve(instances.size());
    assetOffsets.reserve(instances.size());
    compactTextureIndices.reserve(instances.size());

    mStats.staticInstances = 0;
    mStats.rigidDynamicInstances = 0;
    mStats.deformingDynamicInstances = 0;
    mStats.billboardInstances = 0;
    mStats.uiDxrInstances = 0; // Se asegura permanentemente en 0
    mStats.unknownDxrInstances = 0;

    for (const auto& inst : instances) {
        // Exclusión estricta de UI
        if (!inst.isStatic && !inst.isDynamic) {
            continue; // Excluido
        }

        const auto assetIt = assets.find(inst.geometryAssetId);
        if (assetIt == assets.end()) {
            ++mStats.danglingBlasReferences;
            continue;
        }
        auto it = mBlasCache.find(assetIt->second.geometryHash);
        if (it == mBlasCache.end() || !it->second.blasBuffer) {
            mStats.danglingBlasReferences++;
            continue;
        }

        const auto& blasRec = it->second;
        if (assetIt->second.vertexBuffer.empty() ||
            assetIt->second.indexBuffer.empty() || instanceDescs.size() >= 0x01000000u) {
            ++mStats.danglingBlasReferences;
            continue;
        }
        const auto& asset = assetIt->second;
        bool finiteTransform = true;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col)
                finiteTransform = finiteTransform && std::isfinite(inst.worldTransform[row][col]);
        }
        const auto& transform = inst.worldTransform;
        const float determinant =
            transform[0][0] * (transform[1][1] * transform[2][2] - transform[1][2] * transform[2][1]) -
            transform[0][1] * (transform[1][0] * transform[2][2] - transform[1][2] * transform[2][0]) +
            transform[0][2] * (transform[1][0] * transform[2][1] - transform[1][1] * transform[2][0]);
        if (!finiteTransform || !std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-12f) {
            ++mStats.danglingBlasReferences;
            continue;
        }

        auto offsetsIt = assetOffsets.find(asset.id);
        if (offsetsIt == assetOffsets.end()) {
            if (surfaceVertices.size() > UINT32_MAX - asset.vertexBuffer.size() ||
                surfaceIndices.size() > UINT32_MAX - asset.indexBuffer.size()) {
                LogDxr("[COUCH-DXR-MATERIAL][ERROR] Frame geometry exceeds 32-bit bridge offsets");
                continue;
            }
            const uint32_t vertexOffset = static_cast<uint32_t>(surfaceVertices.size());
            const uint32_t indexOffset = static_cast<uint32_t>(surfaceIndices.size());
            for (const auto& vertex : asset.vertexBuffer) {
                DxrHitVertex hitVertex{};
                hitVertex.position[0] = vertex.posX;
                hitVertex.position[1] = vertex.posY;
                hitVertex.position[2] = vertex.posZ;
                hitVertex.normal[0] = vertex.normX;
                hitVertex.normal[1] = vertex.normY;
                hitVertex.normal[2] = vertex.normZ;
                hitVertex.uv[0] = vertex.u;
                hitVertex.uv[1] = vertex.v;
                hitVertex.uv1[0] = vertex.u1;
                hitVertex.uv1[1] = vertex.v1;
                // Raster color.a remains the post-interpreter fog factor under
                // G_FOG; DXR coverage alpha comes from original F3DVtx.cn[3].
                hitVertex.color = PackDxrVertexColor(vertex.color, vertex.sourceAlpha);
                hitVertex.normalState = vertex.normalState;
                hitVertex.reserved[0] = static_cast<uint32_t>(vertex.sourceAlpha) |
                    (static_cast<uint32_t>(vertex.recordLoadedAlpha) << 8u) |
                    (static_cast<uint32_t>(vertex.capturedAlpha) << 16u) |
                    (static_cast<uint32_t>(vertex.fogMode) << 24u);
                hitVertex.reserved[1] = (vertex.color >> 24u) & 0xFFu;
                hitVertex.reserved[1] |= (vertex.sourceColor & 0xFFu) << 8u;
                hitVertex.reserved[1] |= (vertex.sourceColor & 0xFF00u) << 8u;
                hitVertex.reserved[1] |= (vertex.sourceColor & 0xFF0000u) << 8u;
                surfaceVertices.push_back(hitVertex);
            }
            surfaceIndices.insert(surfaceIndices.end(), asset.indexBuffer.begin(), asset.indexBuffer.end());
            offsetsIt = assetOffsets.emplace(asset.id, std::make_pair(vertexOffset, indexOffset)).first;
        }

        // Geometry assets carry the material snapshot captured with the draw.
        // Re-querying by texture hash here can assign one shared texture's
        // most recent classification to every object that uses that texture.
        MaterialDefinition material = asset.material;
        const bool unknownMaterial = material.type == MaterialType::UNKNOWN ||
                                     material.type == MaterialType::CLASSIC ||
                                     material.type == MaterialType::GENERIC_MATTE ||
                                     material.type == MaterialType::PROTECTED_2D;
        if (unknownMaterial) ++mStats.unknownDxrInstances;

        DxrInstanceMaterialData materialData{};
        materialData.vertexOffset = offsetsIt->second.first;
        materialData.indexOffset = offsetsIt->second.second;
        materialData.materialType = static_cast<uint32_t>(
            unknownMaterial ? MaterialType::GENERIC_MATTE : material.type);
        materialData.textureIndex = UINT32_MAX;
        materialData.vertexCount = asset.vertexCount;
        materialData.indexCount = asset.indexCount;
        materialData.alphaMode = asset.alphaMode;
        materialData.roughness = material.roughness;
        materialData.metallic = material.metallic;
        materialData.specular = material.specular;
        materialData.reflectivity = material.reflectivity;
        materialData.emissiveStrength = material.emissiveStrength;
        materialData.normalStrength = material.normalStrength;
        materialData.wetness = material.wetness;
        materialData.subsurfaceHint = material.subsurfaceHint;
        // Keep authored shader policies separate from texture presence and
        // Fast3D alpha-candidate bits. Temple window portals use bit 5 and
        // the exact mosaic relight opt-in uses bit 6.
        materialData.flags = material.flags &
            (MATERIAL_FLAG_ALBEDO_AS_HEIGHT | MATERIAL_FLAG_DXR_WINDOW_PORTAL |
             MATERIAL_FLAG_DXR_PRIMARY_RELIGHT | MATERIAL_FLAG_DECORATIVE_BEAM);
        const bool allVerticesHaveFast3DLitColor = !asset.vertexBuffer.empty() &&
            std::all_of(asset.vertexBuffer.begin(), asset.vertexBuffer.end(), [](const LocalVertex& vertex) {
                return (vertex.normalState & NORMAL_FLAG_FAST3D_LIT_COLOR) != 0u;
            });
        if (DxrPrimaryRelightEligible(static_cast<MaterialType>(materialData.materialType), asset.alphaMode,
                                      (asset.sourceGeometryMode & G_LIGHTING) != 0u,
                                      allVerticesHaveFast3DLitColor)) {
            materialData.flags |= MATERIAL_FLAG_DXR_PRIMARY_RELIGHT;
        }
        // Static world and dynamic actor draws bind scene lighting without a baked
        // point light attached to their Fast3D Shade. Reconstruct the surface albedo
        // in DXR from original source tint and textures while keeping the raster's
        // pre-lit Shade intact. Physical surface eligibility is evaluated by
        // DxrNativeLightReceiverEligible, separating physical opaque/cutout surfaces
        // from emitters, water, glass, sky, decorative beams, and 2D UI.
        if ((inst.isStatic || inst.isDynamic) &&
            DxrNativeLightReceiverEligible(static_cast<MaterialType>(materialData.materialType),
                                           asset.alphaMode, materialData.flags, asset.lightOwnerId,
                                           asset.lightRole, materialData.emissiveStrength)) {
            materialData.flags |= MATERIAL_FLAG_NATIVE_LIGHT_RECEIVER;
        }
        const Fast3DAlphaClass alphaPolicyCandidate =
            ClassifyFast3DAlphaCandidate(asset.sourceOtherModeL, asset.sourceOtherModeH);
        materialData.flags |= static_cast<uint32_t>(alphaPolicyCandidate) << 2u;
        // Match Z_UPD (0x20) in lus_gbi.h. Transmissive/non-updating draws
        // legitimately sit in front of the surface stored in raster depth.
        if ((asset.sourceOtherModeL & 0x20u) != 0u) materialData.flags |= 1u << 4u;
        materialData.baseColor[0] = 1.0f;
        materialData.baseColor[1] = 1.0f;
        materialData.baseColor[2] = 1.0f;
        materialData.baseColor[3] = 1.0f;
        materialData.textureIndex1 = UINT32_MAX;
        materialData.samplerFlags = (asset.textureTileModes[0] & 31u) |
                                    ((asset.textureTileModes[1] & 31u) << 5u) |
                                    ((asset.lightOwnerId & 0xFFFFu) << 16u);
        if (asset.lightRole == COUCH_LIGHT_NAVI_GLOW) materialData.flags |= 1u << 8u;
        materialData.combineModeLow = static_cast<uint32_t>(asset.sourceCombineMode & 0xFFFFFFFFu);
        materialData.combineModeHigh = static_cast<uint32_t>(asset.sourceCombineMode >> 32u);
        const auto packColor = [](const uint8_t color[4]) {
            return static_cast<uint32_t>(color[0]) |
                   (static_cast<uint32_t>(color[1]) << 8u) |
                   (static_cast<uint32_t>(color[2]) << 16u) |
                   (static_cast<uint32_t>(color[3]) << 24u);
        };
        materialData.primitiveColor = packColor(asset.primitiveColor);
        materialData.environmentColor = packColor(asset.environmentColor);
        materialData.reserved[0] = packColor(asset.fogColor);
        materialData.reserved[1] = asset.sourceOtherModeH;
        // OCZ-WATER-01: publish the classified aquatic optics as the final
        // float4 of the 128-byte record (ABI v5). x is the WaterClass ordinal
        // so the shader can key motion/blend defaults; y/z/w carry the
        // per-instance wave scale, flow speed and absorption the classifier
        // and console variables resolved. Non-water instances stay zero and
        // the shader keeps the legacy optical path.
        materialData.waterOptics[0] = 0.0f;
        materialData.waterOptics[1] = 0.0f;
        materialData.waterOptics[2] = 0.0f;
        materialData.waterOptics[3] = 0.0f;
        if (materialData.materialType == static_cast<uint32_t>(MaterialType::WATER)) {
            const WaterProfile waterProfile = material.GetWaterProfile();
            materialData.waterOptics[0] = static_cast<float>(WaterClassOrdinal(waterProfile.cls));
            materialData.waterOptics[1] = waterProfile.waveScale;
            materialData.waterOptics[2] = waterProfile.flowSpeed;
            materialData.waterOptics[3] = waterProfile.absorption;
            if (waterProfile.cls != WaterClass::NONE) {
                // The record roughness is the classified microfacet value; it
                // must reach the shader even when a material cvar overrode the
                // registry default after classification.
                materialData.roughness = waterProfile.roughness;
            }
        }
        // OCZ-LAVA-01: molten optics reuse the same float4 slot the water
        // profile uses, keyed by material type, so no record resize and no
        // ABI bump are required. x is the LavaClass ordinal; y/z/w carry the
        // per-instance flow speed, viscosity and crust amount. Non-lava
        // instances are already zero from the block above, so water and lava
        // can never both claim the slot on one record.
        if (materialData.materialType == static_cast<uint32_t>(MaterialType::LAVA)) {
            const LavaProfile lavaProfile = material.GetLavaProfile();
            materialData.waterOptics[0] = static_cast<float>(LavaClassOrdinal(lavaProfile.cls));
            materialData.waterOptics[1] = lavaProfile.flowSpeed;
            materialData.waterOptics[2] = lavaProfile.viscosity;
            materialData.waterOptics[3] = lavaProfile.crustAmount;
            if (lavaProfile.cls != LavaClass::NONE) {
                materialData.roughness = lavaProfile.roughness;
            }
        }

        // The same resource hash can name several raster tile/palette variants.
        // Use the exact descriptor captured with this draw, including cache hits.
        const auto compactTextureIndex = [&](uint32_t sourceSrvIndex) {
            if (sourceSrvIndex == UINT32_MAX || sourceSrvIndex == 0) return UINT32_MAX;
            auto textureIt = compactTextureIndices.find(sourceSrvIndex);
            if (textureIt == compactTextureIndices.end() &&
                textureSrvIndices.size() < kDxrSurfaceTextureDescriptorCount - 1) {
                const uint32_t compactIndex = static_cast<uint32_t>(textureSrvIndices.size()) + 1;
                textureSrvIndices.push_back(sourceSrvIndex);
                textureIt = compactTextureIndices.emplace(sourceSrvIndex, compactIndex).first;
            }
            return textureIt == compactTextureIndices.end() ? UINT32_MAX : textureIt->second;
        };
        materialData.textureIndex = compactTextureIndex(asset.textureSrvIndex);
        if (materialData.textureIndex != UINT32_MAX) {
            materialData.flags |= MATERIAL_FLAG_ALBEDO_TEXTURE_VALID;
        }
        materialData.textureIndex1 = compactTextureIndex(asset.textureSrvIndex1);
        if (materialData.textureIndex1 != UINT32_MAX) {
            materialData.flags |= MATERIAL_FLAG_SECOND_ALBEDO_TEXTURE_VALID;
        }

        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        // Matriz afín 3x4 row-major para DXR
        desc.Transform[0][0] = inst.worldTransform[0][0];
        desc.Transform[0][1] = inst.worldTransform[1][0];
        desc.Transform[0][2] = inst.worldTransform[2][0];
        desc.Transform[0][3] = inst.worldTransform[3][0];

        desc.Transform[1][0] = inst.worldTransform[0][1];
        desc.Transform[1][1] = inst.worldTransform[1][1];
        desc.Transform[1][2] = inst.worldTransform[2][1];
        desc.Transform[1][3] = inst.worldTransform[3][1];

        desc.Transform[2][0] = inst.worldTransform[0][2];
        desc.Transform[2][1] = inst.worldTransform[1][2];
        desc.Transform[2][2] = inst.worldTransform[2][2];
        desc.Transform[2][3] = inst.worldTransform[3][2];

        // InstanceID indexes this frame's full material + geometry record.
        // The previous packed class/transient-id scheme could not identify a
        // triangle's UV, vertex normal, texture descriptor or optical values.
        desc.InstanceID = static_cast<UINT>(materialRecords.size());

        // InstanceMask: 0x01 = WORLD_STATIC, 0x02 = WORLD_DYNAMIC
        desc.InstanceMask = inst.isStatic ? 0x01 : 0x02;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        // The owning light's proxy sprite must participate in AnyHit so a
        // point-visibility ray does not treat its own emitter as a blocker.
        if (asset.lightOwnerId != 0) desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
        desc.AccelerationStructure = blasRec.blasBuffer->GetGPUVirtualAddress();

        if (skyboxAuditRequested && !skyboxAuditLogged) {
            std::string lowerPath = blasRec.resourcePath;
            for (char& ch : lowerPath) {
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            }
            const bool namedEnvironmentGeometry =
                lowerPath.find("sky") != std::string::npos ||
                lowerPath.find("background") != std::string::npos ||
                lowerPath.find("envmap") != std::string::npos ||
                lowerPath.find("cloud") != std::string::npos;
            if (namedEnvironmentGeometry) {
                const char* geometryFlags = asset.alphaMode == 0 ? "OPAQUE" : "NON_OPAQUE";
                skyboxCandidateRecords.push_back(
                    "path=" + blasRec.resourcePath +
                    " instanceMask=" + std::to_string(desc.InstanceMask) +
                    " instanceFlags=NONE geometryFlags=" + geometryFlags +
                    " alphaMode=" + std::to_string(asset.alphaMode) +
                    " materialType=" + std::to_string(materialData.materialType));
            }
        }

        const uint32_t sourceRecordIndex = static_cast<uint32_t>(materialRecords.size());
        sourceFrame.identities.push_back({ inst.id, asset.id, asset.materialId,
            asset.sourceGeometryMode, asset.lightOwnerId, asset.lightRole, asset.resourcePath });
        // OCZ-LAVA-02: real LAVA-classified geometry also becomes an emitter
        // triangle set so the lighting pass can sample actual melt surfaces.
        // The sentinel owner keeps these triangles out of the Navi pilot path.
        uint32_t emitterCaptureOwner = 0u;
        if (asset.lightOwnerId != 0 && materialData.emissiveStrength > 0.0f &&
            asset.lightRole == COUCH_LIGHT_NAVI_GLOW) {
            emitterCaptureOwner = asset.lightOwnerId;
        } else if (materialData.materialType == static_cast<uint32_t>(MaterialType::LAVA) &&
                   std::isfinite(materialData.waterOptics[0]) && materialData.waterOptics[0] > 0.0f) {
            emitterCaptureOwner = kDxrLavaEmitterOwnerId;
        }
        if (emitterCaptureOwner != 0u) {
            // Sample the actual captured mesh, with the EXACT affine transform used by TLAS.
            // Lava pools can be large; a soft cap bounds TLAS-build cost while the
            // area-weighted sampler still represents the whole pool fairly.
            constexpr uint32_t kMaxLavaEmittersPerFrame = 4096u;
            uint32_t capturedLavaThisFrame = 0u;
            for (uint32_t tri = 0; tri + 2 < asset.indexBuffer.size(); tri += 3) {
                if (emitterCaptureOwner == kDxrLavaEmitterOwnerId &&
                    capturedLavaThisFrame >= kMaxLavaEmittersPerFrame) break;
                DxrEmitterTriangle emitter{};
                emitter.ownerId = emitterCaptureOwner;
                emitter.recordIndex = sourceRecordIndex;
                emitter.primitiveIndex = tri / 3;
                bool valid = true;
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    const uint32_t vi = asset.indexBuffer[tri + corner];
                    if (vi >= asset.vertexBuffer.size()) { valid = false; break; }
                    const auto& v = asset.vertexBuffer[vi];
                    for (uint32_t axis = 0; axis < 3; ++axis)
                        emitter.position[corner][axis] = v.posX * inst.worldTransform[0][axis] +
                            v.posY * inst.worldTransform[1][axis] + v.posZ * inst.worldTransform[2][axis] +
                            inst.worldTransform[3][axis];
                }
                if (!valid) continue;
                float e1[3], e2[3], cross[3];
                for (int a = 0; a < 3; ++a) {
                    e1[a] = emitter.position[1][a] - emitter.position[0][a];
                    e2[a] = emitter.position[2][a] - emitter.position[0][a];
                }
                cross[0] = e1[1]*e2[2] - e1[2]*e2[1];
                cross[1] = e1[2]*e2[0] - e1[0]*e2[2];
                cross[2] = e1[0]*e2[1] - e1[1]*e2[0];
                emitter.area = 0.5f * std::sqrt(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
                if (std::isfinite(emitter.area) && emitter.area > 1.0e-6f) {
                    sourceFrame.emitters.push_back(emitter);
                    if (emitterCaptureOwner == kDxrLavaEmitterOwnerId) ++capturedLavaThisFrame;
                }
            }
        }
        const std::string alphaPolicyStateKey = std::to_string(asset.sourceOtherModeL) + ":" +
            std::to_string(asset.sourceOtherModeH) + ":" + std::to_string(asset.sourceCombineMode);
        const bool firstAlphaPolicyState = loggedAlphaPolicyStates.insert(alphaPolicyStateKey).second;
        instanceDescs.push_back(desc);
        materialRecords.push_back(materialData);

        const std::string& sourceMaterialPath = asset.resourcePath.empty()
            ? blasRec.resourcePath : asset.resourcePath;
        const bool exactTempleMaterial =
            sourceMaterialPath.find("tokinoma_sceneTex_011FA0") != std::string::npos ||
            sourceMaterialPath.find("object_toki_objects_Tex_002DC0") != std::string::npos ||
            sourceMaterialPath.find("object_toki_objects_Tex_0035C0") != std::string::npos ||
            sourceMaterialPath.find("object_toki_objects_Tex_003DC0") != std::string::npos ||
            sourceMaterialPath.find("gFairyWingTex") != std::string::npos;
        const bool traceMaterialRecord = sourceRecordIndex < 16u || exactTempleMaterial ||
            (materialData.flags & (MATERIAL_FLAG_DXR_WINDOW_PORTAL |
                                   MATERIAL_FLAG_DXR_PRIMARY_RELIGHT)) != 0u ||
            materialData.materialType == static_cast<uint32_t>(MaterialType::EMISSIVE);
        std::string materialTraceKey = std::to_string(asset.id) + ":" +
            std::to_string(asset.materialId) + ":" + std::to_string(materialData.materialType) + ":" +
            std::to_string(materialData.alphaMode) + ":" + std::to_string(materialData.flags) + ":" +
            std::to_string(asset.sourceCombineMode);
        if (traceMaterialRecord && loggedDxrMaterialRecords.size() < 128u &&
            loggedDxrMaterialRecords.insert(materialTraceKey).second) {
            const uint32_t fast3dLitVertexCount = static_cast<uint32_t>(std::count_if(
                asset.vertexBuffer.begin(), asset.vertexBuffer.end(), [](const LocalVertex& vertex) {
                    return (vertex.normalState & NORMAL_FLAG_FAST3D_LIT_COLOR) != 0u;
                }));
            std::ostringstream materialLine;
            materialLine << std::fixed << std::setprecision(4)
                << "[COUCH-DXR-MATERIAL][SOURCE_RECORD] instanceId=" << inst.id
                << " dxrInstanceId=" << sourceRecordIndex
                << " assetId=" << asset.id << " geometryHash=" << asset.geometryHash
                << " materialId=" << asset.materialId << " resourceId=" << asset.resourceId
                << " sourcePath=" << (asset.resourcePath.empty() ? "<empty>" : asset.resourcePath)
                << " blasPath=" << (blasRec.resourcePath.empty() ? "<empty>" : blasRec.resourcePath)
                << " declaredType=" << static_cast<uint32_t>(material.type)
                << " effectiveType=" << materialData.materialType
                << " unknownFallback=" << (unknownMaterial ? 1 : 0)
                << " alphaMode=" << materialData.alphaMode
                << " roughness=" << materialData.roughness << " metallic=" << materialData.metallic
                << " specular=" << materialData.specular << " reflectivity=" << materialData.reflectivity
                << " emissive=" << materialData.emissiveStrength << " normalStrength=" << materialData.normalStrength
                << " flags=" << materialData.flags
                << " G_LIGHTING=" << ((asset.sourceGeometryMode & G_LIGHTING) != 0u ? 1 : 0)
                << " fast3dLitVertices=" << fast3dLitVertexCount << "/" << asset.vertexBuffer.size()
                << " geometryMode=0x" << std::hex << asset.sourceGeometryMode
                << " otherModeL=0x" << asset.sourceOtherModeL << " otherModeH=0x" << asset.sourceOtherModeH
                << " combineMode=0x" << asset.sourceCombineMode
                << " textureSrv0=" << std::dec << asset.textureSrvIndex
                << " textureSrv1=" << asset.textureSrvIndex1
                << " textureRecord0=" << materialData.textureIndex
                << " textureRecord1=" << materialData.textureIndex1
                << " tileModes0=" << asset.textureTileModes[0]
                << " tileModes1=" << asset.textureTileModes[1]
                << " primitiveRGBA=" << static_cast<uint32_t>(asset.primitiveColor[0]) << ","
                << static_cast<uint32_t>(asset.primitiveColor[1]) << ","
                << static_cast<uint32_t>(asset.primitiveColor[2]) << ","
                << static_cast<uint32_t>(asset.primitiveColor[3])
                << " environmentRGBA=" << static_cast<uint32_t>(asset.environmentColor[0]) << ","
                << static_cast<uint32_t>(asset.environmentColor[1]) << ","
                << static_cast<uint32_t>(asset.environmentColor[2]) << ","
                << static_cast<uint32_t>(asset.environmentColor[3])
                << " fogRGBA=" << static_cast<uint32_t>(asset.fogColor[0]) << ","
                << static_cast<uint32_t>(asset.fogColor[1]) << ","
                << static_cast<uint32_t>(asset.fogColor[2]) << ","
                << static_cast<uint32_t>(asset.fogColor[3])
                << " vertexCount=" << asset.vertexCount << " indexCount=" << asset.indexCount;
            LogDxr(materialLine.str());
        }

        if (firstAlphaPolicyState &&
            (sourceRecordIndex < 9u || (sourceRecordIndex >= 54u && sourceRecordIndex <= 57u))) {
            uint32_t sourceAlphaMin = 255u;
            uint32_t sourceAlphaMax = 0u;
            uint32_t sourceAlphaZeroCount = 0u;
            uint32_t recordLoadedAlphaMin = 255u, recordLoadedAlphaMax = 0u;
            uint32_t capturedAlphaMin = 255u, capturedAlphaMax = 0u;
            uint32_t fogVertexCount = 0u;
            const uint32_t blender0 = (asset.sourceOtherModeL >> 20u) & 0x3u;
            const uint32_t blender1 = (asset.sourceOtherModeL >> 22u) & 0x3u;
            for (const auto& vertex : asset.vertexBuffer) {
                const uint32_t alphaByte = vertex.sourceAlpha;
                sourceAlphaMin = std::min(sourceAlphaMin, alphaByte);
                sourceAlphaMax = std::max(sourceAlphaMax, alphaByte);
                sourceAlphaZeroCount += alphaByte == 0u ? 1u : 0u;
                recordLoadedAlphaMin = std::min<uint32_t>(recordLoadedAlphaMin, vertex.recordLoadedAlpha);
                recordLoadedAlphaMax = std::max<uint32_t>(recordLoadedAlphaMax, vertex.recordLoadedAlpha);
                capturedAlphaMin = std::min<uint32_t>(capturedAlphaMin, vertex.capturedAlpha);
                capturedAlphaMax = std::max<uint32_t>(capturedAlphaMax, vertex.capturedAlpha);
                fogVertexCount += vertex.fogMode != 0u ? 1u : 0u;
            }
            char stateLine[1536]{};
            sprintf_s(stateLine,
                "[COUCH-DXR-ALPHA-FORENSIC][SOURCE_RECORD] record=%u assetId=%llu resourceId=%llu path=%s type=%u alphaMode=%u candidateAlphaClass=%s textureIndex=%u sourceGeometryMode=0x%08X fogMode=%u otherModeL=0x%08X otherModeH=0x%08X combineMode=0x%016llX alphaCompare=%u coverageDestination=%u zMode=%u cycleType=%u blenderField20=0x%X blenderField22=0x%X legacyUseAlphaPredicate=%u CVG_X_ALPHA=%u ALPHA_CVG_SEL=%u FORCE_BL=%u sourceOverEquation=%u additiveEquation=%u combinerAlphaCycle0=%03X combinerAlphaCycle1=%03X rspSourceAlphaMin=%u rspSourceAlphaMax=%u rspSourceAlphaZeroCount=%u recordLoadedAlphaMin=%u recordLoadedAlphaMax=%u capturedAlphaMin=%u capturedAlphaMax=%u fogVertexCount=%u vertexCount=%u indexCount=%u",
                sourceRecordIndex,
                static_cast<unsigned long long>(asset.id),
                static_cast<unsigned long long>(asset.resourceId),
                blasRec.resourcePath.c_str(), materialData.materialType, materialData.alphaMode,
                Fast3DAlphaClassName(alphaPolicyCandidate),
                materialData.textureIndex, asset.sourceGeometryMode,
                (asset.sourceGeometryMode & 0x00010000u) != 0u ? 1u : 0u, asset.sourceOtherModeL,
                asset.sourceOtherModeH, static_cast<unsigned long long>(asset.sourceCombineMode),
                asset.sourceOtherModeL & 3u, (asset.sourceOtherModeL >> 8u) & 3u,
                (asset.sourceOtherModeL >> 10u) & 3u,
                (asset.sourceOtherModeH >> 20u) & 3u,
                blender0, blender1,
                (((asset.sourceOtherModeL & (3u << 20u)) == 0u) ||
                 ((asset.sourceOtherModeL & (3u << 22u)) == 0u)) ? 1u : 0u,
                (asset.sourceOtherModeL & 0x1000u) != 0u ? 1u : 0u,
                (asset.sourceOtherModeL & 0x2000u) != 0u ? 1u : 0u,
                (asset.sourceOtherModeL & 0x4000u) != 0u ? 1u : 0u,
                ((asset.sourceOtherModeH >> 20u) & 3u) == 0u
                    ? Fast3DSourceOver(asset.sourceOtherModeL, 30u) ? 1u : 0u
                    : (Fast3DSourceOver(asset.sourceOtherModeL, 28u) || Fast3DSourceOver(asset.sourceOtherModeL, 30u)) ? 1u : 0u,
                ((asset.sourceOtherModeH >> 20u) & 3u) == 0u
                    ? Fast3DAdditive(asset.sourceOtherModeL, 30u) ? 1u : 0u
                    : (Fast3DAdditive(asset.sourceOtherModeL, 28u) || Fast3DAdditive(asset.sourceOtherModeL, 30u)) ? 1u : 0u,
                static_cast<unsigned>((asset.sourceCombineMode >> 16u) & 0xFFFu),
                static_cast<unsigned>((asset.sourceCombineMode >> 44u) & 0xFFFu),
                sourceAlphaMin, sourceAlphaMax, sourceAlphaZeroCount,
                recordLoadedAlphaMin, recordLoadedAlphaMax, capturedAlphaMin, capturedAlphaMax, fogVertexCount,
                asset.vertexCount, asset.indexCount);
            LogDxr(stateLine);
        }
        if (firstAlphaPolicyState) {
            LogDxr("[COUCH-DXR-ALPHA-POLICY][STATE_RECORD] record=" + std::to_string(sourceRecordIndex) +
                   " otherModeL=" + std::to_string(asset.sourceOtherModeL) +
                   " otherModeH=" + std::to_string(asset.sourceOtherModeH) +
                   " combineMode=" + std::to_string(asset.sourceCombineMode) +
                   " currentAlphaMode=" + std::to_string(asset.alphaMode) +
                   " candidateAlphaClass=" + Fast3DAlphaClassName(alphaPolicyCandidate) +
                   " candidateAlphaMode=" + std::to_string(static_cast<uint32_t>(alphaPolicyCandidate)) +
                   " flags=" + std::to_string(materialData.flags));
        }

        // Clasificación de estadísticas
        if (inst.isStatic) {
            mStats.staticInstances++;
        } else {
            mStats.rigidDynamicInstances++;
        }

        // Trazabilidad de entidades
        if (blasRec.resourcePath.find("master_sword") != std::string::npos ||
            blasRec.resourcePath.find("sword") != std::string::npos) {
            mStats.masterSwordFound = true;
            mStats.masterSwordAssetId = inst.geometryAssetId;
            memcpy(mStats.masterSwordTransform, desc.Transform, sizeof(desc.Transform));
        }

        if (blasRec.resourcePath.find("link") != std::string::npos ||
            inst.actorId == 1) {
            mStats.linkFound = true;
            mStats.linkPartCount++;
        }
    }

    if (skyboxAuditRequested && !skyboxAuditLogged && instanceDescs.size() >= 50) {
        LogDxr("[COUCH-FORENSIC][SKYBOX_AUDIT] capturedTlasInstances=" +
               std::to_string(instanceDescs.size()) + " namedEnvironmentCandidates=" +
               std::to_string(skyboxCandidateRecords.size()) +
               " masks=static:0x01,dynamic:0x02 rayMask=0x03");
        for (const auto& record : skyboxCandidateRecords) {
            LogDxr("[COUCH-FORENSIC][SKYBOX_INSTANCE] " + record);
        }
        if (skyboxCandidateRecords.empty()) {
            LogDxr("[COUCH-FORENSIC][SKYBOX_AUDIT] no candidate path named sky/background/envmap/cloud; "
                   "generic opaque environment geometry remains unclassified by name");
        }
        skyboxAuditLogged = true;
    }

    if (!UpdateSurfaceFrameBuffers(device5, frameIndex, surfaceVertices, surfaceIndices,
                                   materialRecords, textureSrvIndices)) {
        LogDxr("[COUCH-DXR-MATERIAL][ERROR] Failed to upload per-frame hit/material bridge");
    }

    if (instanceDescs.empty()) {
        return false;
    }

    mStats.tlasInstanceCount = static_cast<uint32_t>(instanceDescs.size());

    static bool matrixTraceTlasLogged = false;
    const char* matrixTrace = std::getenv("COUCH_DXR_MATRIX_TRACE");
    if (!matrixTraceTlasLogged && matrixTrace && strcmp(matrixTrace, "1") == 0 && instanceDescs.size() >= 50) {
        matrixTraceTlasLogged = true;
        LogDxr("[COUCH-DXR-MATRIX][TLAS] instances=" + std::to_string(instanceDescs.size()));
        for (size_t index = 0; index < std::min<size_t>(8, instanceDescs.size()); ++index) {
            std::string line = "[COUCH-DXR-MATRIX][INSTANCE] id=" + std::to_string(instanceDescs[index].InstanceID);
            for (int row = 0; row < 3; ++row) {
                line += " r" + std::to_string(row) + "=";
                for (int col = 0; col < 4; ++col) {
                    if (col) line += ",";
                    line += std::to_string(instanceDescs[index].Transform[row][col]);
                }
            }
            LogDxr(line);
        }
    }

    // The persistent scene rebuilds its CPU-side instance vector every frame.
    // Build a content signature that deliberately excludes the transient
    // GeometryInstance::id, while retaining every value that affects the DXR
    // instance descriptors. This lets an unchanged scene reuse the existing
    // TLAS without hiding a transform, topology, material, or BLAS change.
    uint64_t tlasSignature = 14695981039346656037ULL;
    const auto mixSignature = [&tlasSignature](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            tlasSignature ^= bytes[i];
            tlasSignature *= 1099511628211ULL;
        }
    };
    // Hash the emitted descriptors in their actual InstanceID order. Walking
    // the source list again can drift when a later validation skipped a draw.
    const uint32_t emittedInstanceCount = static_cast<uint32_t>(instanceDescs.size());
    mixSignature(&emittedInstanceCount, sizeof(emittedInstanceCount));
    for (const auto& desc : instanceDescs) {
        const uint32_t instanceId = desc.InstanceID;
        const uint32_t hitGroup = desc.InstanceContributionToHitGroupIndex;
        const uint8_t instanceFlags = static_cast<uint8_t>(desc.Flags);
        const uint8_t instanceMask = static_cast<uint8_t>(desc.InstanceMask);
        mixSignature(&instanceId, sizeof(instanceId));
        mixSignature(&hitGroup, sizeof(hitGroup));
        mixSignature(&instanceFlags, sizeof(instanceFlags));
        mixSignature(desc.Transform, sizeof(desc.Transform));
        mixSignature(&instanceMask, sizeof(instanceMask));
        mixSignature(&desc.AccelerationStructure, sizeof(desc.AccelerationStructure));
    }

    if (mTlasValid && mTlasBuffer && mInstanceDescBuffer &&
        mTlasSignature == tlasSignature) {
        ++mStats.tlasUpdates;
        return true;
    }

    // 1. Asignar y poblar buffer de Instance Descs en UPLOAD heap
    const size_t descBufferSize = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    mInstanceDescBuffer = mInstanceDescFrames[frameIndex % 3];
    mInstanceDescAllocatedSize = mInstanceDescFrameSizes[frameIndex % 3];
    AllocateOrGrowBuffer(device5, mInstanceDescBuffer, descBufferSize,
                         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ,
                         D3D12_HEAP_TYPE_UPLOAD);

    mInstanceDescFrames[frameIndex % 3] = mInstanceDescBuffer;
    mInstanceDescFrameSizes[frameIndex % 3] = mInstanceDescAllocatedSize;
    if (!mInstanceDescBuffer) return false;

    uint8_t* pMappedDescs = nullptr;
    mInstanceDescBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pMappedDescs));
    memcpy(pMappedDescs, instanceDescs.data(), descBufferSize);
    mInstanceDescBuffer->Unmap(0, nullptr);

    // 2. Prebuild Info para TLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
    tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.NumDescs = static_cast<UINT>(instanceDescs.size());
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.InstanceDescs = mInstanceDescBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuild = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasPrebuild);

    // 3. Asignar TLAS Result y Scratch Buffers
    AllocateOrGrowBuffer(device5, mTlasBuffer, tlasPrebuild.ResultDataMaxSizeInBytes,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_HEAP_TYPE_DEFAULT);

    AllocateOrGrowBuffer(device5, mTlasScratchBuffer, tlasPrebuild.ScratchDataSizeInBytes,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_HEAP_TYPE_DEFAULT);

    if (tlasPrebuild.ScratchDataSizeInBytes > mStats.scratchPeak) {
        mStats.scratchPeak = tlasPrebuild.ScratchDataSizeInBytes;
    }

    if (!mTlasBuffer || !mTlasScratchBuffer) return false;

    mStats.tlasMemory = tlasPrebuild.ResultDataMaxSizeInBytes;

    // 4. Emitir Build TLAS en CommandList
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc = {};
    tlasBuildDesc.DestAccelerationStructureData = mTlasBuffer->GetGPUVirtualAddress();
    tlasBuildDesc.Inputs = tlasInputs;
    tlasBuildDesc.ScratchAccelerationStructureData = mTlasScratchBuffer->GetGPUVirtualAddress();

    cmdList4->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);

    // 5. Emitir UAV Barrier sobre el TLAS
    D3D12_RESOURCE_BARRIER tlasBarrier = {};
    tlasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    tlasBarrier.UAV.pResource = mTlasBuffer.Get();
    cmdList4->ResourceBarrier(1, &tlasBarrier);

    mStats.tlasBuilds++;
    if (!mStats.firstTlasPassed) {
        mStats.firstTlasPassed = true;
        LogDxr("[COUCH-DXR] FIRST_TLAS = PASS | Instances=" + std::to_string(mStats.tlasInstanceCount) +
               " | TLAS Size=" + std::to_string(tlasPrebuild.ResultDataMaxSizeInBytes) +
               " B | Scratch=" + std::to_string(tlasPrebuild.ScratchDataSizeInBytes) + " B");
    }

    mTlasSignature = tlasSignature;
    mTlasValid = true;

    return true;
}

void DxrAccelerationStructureManager::RetireBlasRecord(DxrBlasRecord& record, uint64_t retireFrame) {
    auto retireResource = [this, retireFrame](Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
        if (!resource) {
            return;
        }
        mDeferredReleases.push_back({ resource, mSubmissionFence });
        resource.Reset();
    };

    // Keep every GPU resource alive until the same deferred-release fence used
    // by buffer growth.  The record is erased only after the resource handles
    // have been transferred to that queue.
    retireResource(record.blasBuffer);
    retireResource(record.vbResource);
    retireResource(record.ibResource);

    if (record.prebuildInfo.ResultDataMaxSizeInBytes <= mStats.blasMemory) {
        mStats.blasMemory -= record.prebuildInfo.ResultDataMaxSizeInBytes;
    } else {
        mStats.blasMemory = 0;
    }
    if (record.geometryMemory <= mStats.dxrGeometryMemory) {
        mStats.dxrGeometryMemory -= record.geometryMemory;
    } else {
        mStats.dxrGeometryMemory = 0;
    }

    if (record.classification == GeometryClassification::STATIC) {
        if (mStats.staticBlasCount > 0) {
            --mStats.staticBlasCount;
        }
    } else if (mStats.dynamicBlasCount > 0) {
        --mStats.dynamicBlasCount;
    }
}

void DxrAccelerationStructureManager::EvictStaleBlas(const std::unordered_set<uint64_t>& activeAssetIds,
                                                     uint64_t frameIndex) {
    // The idle-age threshold is a safety valve for a cache that has actually
    // exceeded one of its hard caps.  Evicting merely because an entry has
    // been unused for 300 frames causes rebuild/evict churn in normal scenes,
    // even when the cache is comfortably below both caps.
    const bool hardLimitExceeded = mBlasCache.size() > kBlasEntryLimit ||
                                    mStats.blasMemory > kBlasMemoryLimit;
    if (!hardLimitExceeded) {
        return;
    }

    struct Candidate {
        uint64_t assetId;
        uint64_t lastUsedFrame;
        uint64_t age;
        GeometryClassification classification;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(mBlasCache.size());
    for (const auto& kv : mBlasCache) {
        if (activeAssetIds.find(kv.first) != activeAssetIds.end()) {
            continue;
        }

        const auto& record = kv.second;
        // Static BLAS are globally reusable and are never eviction targets.
        // If active dynamic geometry alone fills the caps, retain it and
        // report the constrained cache below instead of destroying reusable
        // static acceleration structures.
        if (record.classification == GeometryClassification::STATIC) {
            continue;
        }
        const uint64_t age = frameIndex >= record.lastUsedFrame ? frameIndex - record.lastUsedFrame : 0;
        candidates.push_back({ kv.first, record.lastUsedFrame, age, record.classification });
    }

    // Prefer genuinely idle entries, then dynamic geometry, then the oldest
    // last-use timestamp.  Active assets are excluded above and are never
    // evicted by this policy.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        const bool aIdle = a.age >= kBlasIdleFrameLimit;
        const bool bIdle = b.age >= kBlasIdleFrameLimit;
        if (aIdle != bIdle) {
            return aIdle > bIdle;
        }

        const bool aDynamic = a.classification != GeometryClassification::STATIC;
        const bool bDynamic = b.classification != GeometryClassification::STATIC;
        if (aDynamic != bDynamic) {
            return aDynamic > bDynamic;
        }

        if (a.lastUsedFrame != b.lastUsedFrame) {
            return a.lastUsedFrame < b.lastUsedFrame;
        }
        return a.assetId < b.assetId;
    });

    size_t evictedThisPass = 0;
    size_t candidateIndex = 0;
    while (candidateIndex < candidates.size()) {
        const auto& candidate = candidates[candidateIndex++];
        if (candidate.age < kBlasIdleFrameLimit) {
            break;
        }

        auto it = mBlasCache.find(candidate.assetId);
        if (it == mBlasCache.end() || activeAssetIds.find(candidate.assetId) != activeAssetIds.end()) {
            continue;
        }

        RetireBlasRecord(it->second, frameIndex);
        mBlasCache.erase(it);
        ++mStats.blasEvictions;
        ++evictedThisPass;
    }

    if (evictedThisPass > 0) {
        LogDxr("[COUCH-DXR][BLAS_CACHE] Evicted=" + std::to_string(evictedThisPass) +
               " | Remaining=" + std::to_string(mBlasCache.size()) +
               " | BLAS Mem=" + std::to_string(mStats.blasMemory / 1024) + " KB");
    }

    if ((mBlasCache.size() > kBlasEntryLimit || mStats.blasMemory > kBlasMemoryLimit) &&
        frameIndex % 120 == 0) {
        LogDxr("[COUCH-DXR][BLAS_CACHE] Hard cap constrained by active entries | Count=" +
               std::to_string(mBlasCache.size()) + " | BLAS Mem=" +
               std::to_string(mStats.blasMemory / 1024) + " KB");
    }
}

void DxrAccelerationStructureManager::ProcessFrame(ID3D12Device* device,
                                                   ID3D12GraphicsCommandList* cmdList,
                                                  uint32_t frameIndex,
                                                  uint64_t fenceValue, uint64_t completedFenceValue) {
    if (!mEnabled || !mSupported || !mDevice5 || !cmdList) {
        return;
    }

    mSubmissionFence = fenceValue;
    mFrameIndexCounter++;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
    if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4))) || !cmdList4) {
        LogDxr("[COUCH-DXR][ERROR] HARD GATE: ID3D12GraphicsCommandList4 query failed");
        mStats.dxrDebugErrors++;
        return;
    }
    mStats.d3d12CommandList4Passed = true;

    const auto& persistentScene = PersistentScene::Instance();
    const auto& assets = persistentScene.GetAssets();
    const auto& instances = persistentScene.GetInstances();

    std::unordered_set<uint64_t> activeAssetIds;
    activeAssetIds.reserve(instances.size());
    for (const auto& inst : instances) {
        // Keep the UI exclusion as a hard gate: only geometry eligible for a
        // DXR instance can make an asset active or trigger a BLAS build.
        if (inst.isStatic || inst.isDynamic) {
            const auto asset = assets.find(inst.geometryAssetId);
            if (asset != assets.end()) activeAssetIds.insert(asset->second.geometryHash);
        }
    }

    if (assets.empty() && instances.empty()) {
        mTlasValid = false;
        mStats.tlasInstanceCount = 0;
        EvictStaleBlas(activeAssetIds, mFrameIndexCounter);
        return;
    }

    // PASO 1: Procesar BLAS para cada GeometryAsset nuevo o sucio
    uint32_t blasBuiltThisFrame = 0;
    for (const auto& kv : assets) {
        const auto& asset = kv.second;
        auto it = mBlasCache.find(asset.geometryHash);
        if (it == mBlasCache.end()) {
            // PersistentScene conserva un inventario histórico de assets. No
            // materializar BLAS para entradas sin instancia activa evita que
            // una evicción válida se convierta en un ciclo rebuild/evict.
            if (activeAssetIds.find(asset.geometryHash) == activeAssetIds.end()) {
                continue;
            }
            DxrBlasRecord record = {};
            if (BuildBlasForAsset(mDevice5.Get(), cmdList4.Get(), asset, record, frameIndex)) {
                mBlasCache[asset.geometryHash] = std::move(record);
                mStats.blasCreated++;
                blasBuiltThisFrame++;
            }
        } else {
            if (activeAssetIds.find(asset.geometryHash) != activeAssetIds.end()) {
                it->second.lastUsedFrame = mFrameIndexCounter;
            }
            mStats.blasReused++;
        }
    }

    // Sincronizar construcción de BLAS antes de proceder al TLAS
    if (blasBuiltThisFrame > 0) {
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = nullptr;
        cmdList4->ResourceBarrier(1, &uavBarrier);
    }

    // PASO 2: Construir TLAS con las instancias del frame
    if (!instances.empty()) {
        if (!BuildTlas(mDevice5.Get(), cmdList4.Get(), instances, assets, frameIndex)) {
            mTlasValid = false;
            mStats.tlasInstanceCount = 0;
        }
    } else {
        mTlasValid = false;
        mStats.tlasInstanceCount = 0;
    }

    // PASO 3: Retirar BLAS inactivos antes de limpiar sus recursos diferidos.
    // El TLAS de este frame ya fue construido, por lo que ningún registro
    // activo puede quedar apuntando a una entrada que esta política retire.
    EvictStaleBlas(activeAssetIds, mFrameIndexCounter);

    // PASO 4: Limpieza diferida de recursos antiguos cuyo fence haya completado
    auto itDef = mDeferredReleases.begin();
    while (itDef != mDeferredReleases.end()) {
        if (completedFenceValue >= itDef->fenceValue) {
            itDef = mDeferredReleases.erase(itDef);
        } else {
            ++itDef;
        }
    }

    if (mFrameIndexCounter % 120 == 0) {
        const uint32_t blasGrowth = mStats.blasCreated - mLastTelemetryBlasCreated;
        const uint32_t blasEvictions = mStats.blasEvictions - mLastTelemetryBlasEvictions;
        LogDxr("[COUCH-DXR-FENCE] submitted="+std::to_string(fenceValue)+" completed="+std::to_string(completedFenceValue)+" retiredPending="+std::to_string(mDeferredReleases.size()));
        LogDxr("[COUCH-DXR] BLAS Growth Rate: " + std::to_string(blasGrowth) + " new/120frames");
        LogDxr("[COUCH-DXR] BLAS Evictions: " + std::to_string(blasEvictions) + "/120frames");
        LogDxr("[COUCH-DXR] Stable BLAS: " + std::to_string(mStats.staticBlasCount) +
               " (static) + " + std::to_string(mStats.dynamicBlasCount) + " (dynamic cached)");
        LogDxr("[COUCH-DXR] TLAS Builds=" + std::to_string(mStats.tlasBuilds) +
               " Reused=" + std::to_string(mStats.tlasUpdates));
        LogDxr("[COUCH-DXR] Frame " + std::to_string(mFrameIndexCounter) +
               " | BLAS Count=" + std::to_string(mBlasCache.size()) +
                " (Created=" + std::to_string(mStats.blasCreated) +
                ", Reused=" + std::to_string(mStats.blasReused) +
                ", Evicted=" + std::to_string(mStats.blasEvictions) +
                ") | TLAS Instances=" + std::to_string(mStats.tlasInstanceCount) +
               " | BLAS Mem=" + std::to_string(mStats.blasMemory / 1024) +
               " KB | TLAS Mem=" + std::to_string(mStats.tlasMemory / 1024) + " KB");
        mLastTelemetryFrame = mFrameIndexCounter;
        mLastTelemetryBlasCreated = mStats.blasCreated;
        mLastTelemetryBlasEvictions = mStats.blasEvictions;
    }
}

void DxrAccelerationStructureManager::HandleSceneTransition() {
    LogDxr("[COUCH-DXR] Scene Transition Detected: Purging obsolete BLAS and TLAS instances");

    // RT4-A: Notify temporal manager of scene change (invalidate history)
    TemporalManager::Instance().OnSceneChange();

    // Transferir todos los recursos de BLAS mediante la misma ruta de retirada
    // que usa la evicción normal, manteniendo coherentes las métricas vivas.
    for (auto& kv : mBlasCache) {
        RetireBlasRecord(kv.second, mFrameIndexCounter);
    }
    mBlasCache.clear();

    mStats.blasMemory = 0;
    mStats.dxrGeometryMemory = 0;
    mStats.staticBlasCount = 0;
    mStats.dynamicBlasCount = 0;

    mStats.zombieDxrInstances = 0;
    mStats.danglingBlasReferences = 0;
    mStats.tlasInstanceCount = 0;
    mTlasSignature = 0;
    mTlasValid = false;
}

void DxrAccelerationStructureManager::Reset() {
    for (auto& kv : mBlasCache) {
        RetireBlasRecord(kv.second, mFrameIndexCounter);
    }
    mBlasCache.clear();
    mTlasBuffer.Reset();
    mTlasScratchBuffer.Reset();
    mInstanceDescBuffer.Reset();
    for (auto& frame : mInstanceDescFrames) frame.Reset();
    mInstanceDescFrameSizes.fill(0);
    mBlasScratchBuffer.Reset();
    mDeferredReleases.clear();
    mStats = {};
    mTlasSignature = 0;
    mTlasValid = false;
    mLastTelemetryFrame = 0;
    mLastTelemetryBlasCreated = 0;
    mLastTelemetryBlasEvictions = 0;
    mInitialized = false;
    mSupported = false;
    mEnabled = false;
}

void DxrAccelerationStructureManager::DumpReport() const {
    LogDxr("============================================================");
    LogDxr("DXR ACCELERATION STRUCTURE MANAGER TELEMETRY REPORT");
    LogDxr("============================================================");
    LogDxr("  d3d12Device5Passed           : " + std::string(mStats.d3d12Device5Passed ? "PASS" : "FAIL"));
    LogDxr("  d3d12CommandList4Passed      : " + std::string(mStats.d3d12CommandList4Passed ? "PASS" : "FAIL"));
    LogDxr("  firstStaticBlasPassed        : " + std::string(mStats.firstStaticBlasPassed ? "PASS" : "FAIL"));
    LogDxr("  firstTlasPassed              : " + std::string(mStats.firstTlasPassed ? "PASS" : "FAIL"));
    LogDxr("  blasCreated                  : " + std::to_string(mStats.blasCreated));
    LogDxr("  blasReused                   : " + std::to_string(mStats.blasReused));
    LogDxr("  blasEvictions                : " + std::to_string(mStats.blasEvictions));
    LogDxr("  staticBlasCount              : " + std::to_string(mStats.staticBlasCount));
    LogDxr("  dynamicBlasCount             : " + std::to_string(mStats.dynamicBlasCount));
    LogDxr("  dynamicBlasRebuilds          : " + std::to_string(mStats.dynamicBlasRebuilds));
    LogDxr("  tlasBuilds                   : " + std::to_string(mStats.tlasBuilds));
    LogDxr("  tlasUpdates                  : " + std::to_string(mStats.tlasUpdates) + " (reused)");
    LogDxr("  tlasInstanceCount            : " + std::to_string(mStats.tlasInstanceCount));
    LogDxr("  staticInstances              : " + std::to_string(mStats.staticInstances));
    LogDxr("  rigidDynamicInstances        : " + std::to_string(mStats.rigidDynamicInstances));
    LogDxr("  uiDxrInstances               : " + std::to_string(mStats.uiDxrInstances) + " (GATE: 0)");
    LogDxr("  zombieDxrInstances           : " + std::to_string(mStats.zombieDxrInstances) + " (GATE: 0)");
    LogDxr("  danglingBlasReferences       : " + std::to_string(mStats.danglingBlasReferences) + " (GATE: 0)");
    LogDxr("  blasMemory                   : " + std::to_string(mStats.blasMemory) + " bytes");
    LogDxr("  tlasMemory                   : " + std::to_string(mStats.tlasMemory) + " bytes");
    LogDxr("  scratchPeak                  : " + std::to_string(mStats.scratchPeak) + " bytes");
    LogDxr("  dxrGeometryMemory            : " + std::to_string(mStats.dxrGeometryMemory) + " bytes");
    LogDxr("  dxrDebugErrors               : " + std::to_string(mStats.dxrDebugErrors) + " (GATE: 0)");
    LogDxr("============================================================");
}

} // namespace Fast

#endif
