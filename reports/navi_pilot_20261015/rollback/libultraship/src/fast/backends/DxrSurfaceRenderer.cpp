#if defined(_WIN32)

#include "fast/backends/DxrSurfaceRenderer.h"
#include "fast/backends/DxrDescriptorRing.h"
#include "fast/backends/RtxQuality.h"
#include "fast/backends/TemporalManager.h"
#include "fast/backends/DxrShadowRenderer.h"
#include "fast/couch_diagnostics.h"
#include "fast/backends/dxr_shadow_cso.h"
#include "fast/MaterialRegistry.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace Fast {

static_assert(kDxrShadowBytecodeAbiVersion == kDxrShaderAbiVersion,
              "Regenerate DXR bytecode with Generate-DxrShadowBytecode.ps1 before building this pipeline");

static void LogCouchDxrSurface(const std::string& line);

// Mirror the HLSL cbuffer's three 16-byte registers, including the gap
// before float3 gGameLightDir. Root constants do not add this padding.
struct alignas(16) DxrSurfaceCompositeConstants {
    float params[4];
    float sceneHdr;
    float diagnosticView;
    float ablationMode;
    float padding;
    float gameLightDirection[3];
    float gameLightValid;
};
static_assert(sizeof(DxrSurfaceCompositeConstants) == 48);
static_assert(offsetof(DxrSurfaceCompositeConstants, ablationMode) == 24);
static_assert(offsetof(DxrSurfaceCompositeConstants, gameLightDirection) == 32);
static_assert(offsetof(DxrSurfaceCompositeConstants, gameLightValid) == 44);
static constexpr UINT kSurfaceCompositeRootDwords = sizeof(DxrSurfaceCompositeConstants) / sizeof(float);

static const char* PrimaryGatePendingMode(bool blendBypass, bool vertexAlphaBypass, bool candidatePolicy = false) {
    if (candidatePolicy) return "CANDIDATE_ALPHA_POLICY";
    if (vertexAlphaBypass) return "BYPASS_PRIMARY_RASTER_BLEND_PLUS_VERTEX_ALPHA";
    return blendBypass ? "BYPASS_PRIMARY_RASTER_BLEND_ONLY" : "NORMAL";
}

namespace {
constexpr uint32_t kSurfaceFrameStride = kDxrFrameConstantStride;
constexpr uint32_t kPathTemporalFrameStride = 256;
constexpr uint32_t kPrimaryGateCounterShards = 64;
constexpr uint32_t kPrimaryGateCounterCount = 2112;
constexpr uint32_t kPrimaryRasterDepthMismatchCounter = 488; // matches PG_RASTER_DEPTH_MISMATCH
constexpr uint32_t kPrimaryAlphaSampleBase = kPrimaryGateCounterShards * kPrimaryGateCounterCount;
constexpr uint32_t kPrimaryAlphaSampleStride = 32;
constexpr uint32_t kPrimaryAlphaSampleCount = 3;
constexpr uint32_t kPrimaryGateCounterElements = kPrimaryAlphaSampleBase +
    kPrimaryAlphaSampleStride * kPrimaryAlphaSampleCount;
constexpr uint64_t kPrimaryGateCounterBytes = kPrimaryGateCounterElements * sizeof(uint32_t);
constexpr uint32_t kPrimaryGateCounterDescriptor = 5;
constexpr uint32_t kSurfaceSrvDescriptorBase = 6;
constexpr uint32_t kSurfaceTextureDescriptorBase = 11;
constexpr uint32_t kSurfaceEnvironmentDescriptorBase = kSurfaceTextureDescriptorBase + kDxrSurfaceTextureDescriptorCount;
static_assert((kPrimaryGateCounterShards & (kPrimaryGateCounterShards - 1)) == 0);
// Mirrors ReflectionPayload in fast/shaders/dxr_shadow.hlsl. Keep these fields
// and their order synchronized with HLSL's payload shared by the reflection exports.
struct ReflectionPayloadLayout {
    float radiance[4];
    float normalRoughness[4];
    float baseColorMetallic[4];
    uint32_t materialIndex;
    uint32_t probeMode;
    uint32_t instanceIndex;
    uint32_t textureIndex;
    float surfaceOptics[4];
    float detail[4];
    uint32_t diagnosticFlags;
    uint32_t diagnosticError;
    float geometricNormal[4];
};
static constexpr UINT kReflectionPayloadSizeBytes = sizeof(ReflectionPayloadLayout);
static_assert(kReflectionPayloadSizeBytes == 120, "ReflectionPayloadLayout must match the HLSL ReflectionPayload");
static_assert(offsetof(ReflectionPayloadLayout, geometricNormal) == 104);
HRESULT gLastInitHresult = S_OK;

static std::string SurfaceHr(HRESULT hr) {
    char value[16]{};
    snprintf(value, sizeof(value), "0x%08X", static_cast<unsigned>(hr));
    return value;
}

static bool PrimaryGateCountersRequested() {
    const char* value = std::getenv("COUCH_DXR_GPU_GATE_COUNTERS");
    return value && strcmp(value, "1") == 0;
}

static uint64_t SurfaceMatrixHash(const float matrix[4][4]) {
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const uint8_t*>(matrix);
    for (size_t i = 0; i < sizeof(float) * 16; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void LogPrimaryGatePercent(const char* gate, uint32_t count, uint32_t total, uint32_t hits) {
    const double totalPercent = total ? 100.0 * count / total : 0.0;
    const double hitPercent = hits ? 100.0 * count / hits : 0.0;
    char line[256]{};
    if (strcmp(gate, "miss") == 0 || strcmp(gate, "hit") == 0) {
        snprintf(line, sizeof(line), "[COUCH-DXR-GATE][PERCENT] gate=%s count=%u totalPct=%.4f hitPct=N/A",
                 gate, count, totalPercent);
    } else {
        snprintf(line, sizeof(line), "[COUCH-DXR-GATE][PERCENT] gate=%s count=%u totalPct=%.4f hitPct=%.4f",
                 gate, count, totalPercent, hitPercent);
    }
    LogCouchDxrSurface(line);
}

static UINT64 SurfaceInfoQueueMark(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> queue;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return 0;
    return queue->GetNumStoredMessages();
}

static void SurfaceLogInfoQueue(ID3D12Device* device, UINT64 from, const char* step) {
    ComPtr<ID3D12InfoQueue> queue;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return;
    const UINT64 end = queue->GetNumStoredMessages();
    for (UINT64 i = from; i < end; ++i) {
        SIZE_T bytes = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &bytes)) || bytes == 0) continue;
        std::vector<uint8_t> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(queue->GetMessage(i, message, &bytes)) ||
            (message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
             message->Severity != D3D12_MESSAGE_SEVERITY_WARNING)) continue;
        LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][DEBUG_LAYER] step=") + step +
            " severity=" + (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" : "WARNING") +
            " id=" + std::to_string(message->ID) + " text=" +
            (message->pDescription ? message->pDescription : "(no description)"));
    }
}
}

struct PathTemporalConstants {
    float size[4];
    float params[4];
};
static_assert(sizeof(PathTemporalConstants) <= kPathTemporalFrameStride);

static void LogCouchDxrSurface(const std::string& line) {
    SPDLOG_INFO(line);
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) ofs << line << "\n";
    const char* harness = std::getenv("COUCH_DXR_HARNESS");
    const char* outputRoot = std::getenv("COUCH_DXR_OUTPUT");
    if (harness && std::strcmp(harness, "1") == 0 && outputRoot && *outputRoot) {
        std::ofstream runLog(std::string(outputRoot) + "\\harness-dxr.log", std::ios::app);
        if (runLog) runLog << line << '\n';
    }
}

typedef HRESULT(WINAPI* PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_SURFACE)(
    const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static HRESULT SerializeSurfaceRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,
                                             ID3DBlob** blob, ID3DBlob** error) {
    static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_SURFACE fn = nullptr;
    if (!fn) {
        HMODULE d3d12 = GetModuleHandleA("d3d12.dll");
        if (!d3d12) d3d12 = LoadLibraryA("d3d12.dll");
        if (d3d12) {
            fn = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_SURFACE>(
                GetProcAddress(d3d12, "D3D12SerializeRootSignature"));
        }
    }
    return fn ? fn(desc, D3D_ROOT_SIGNATURE_VERSION_1, blob, error) : E_FAIL;
}

// This is the same cofactor inverse used by the shadow and temporal paths.
// Keeping the copy local avoids coupling the surface renderer to either
// renderer's private camera state.
static bool InvertSurfaceMatrix(const float m[4][4], float inv[4][4]) {
    float a[16];
    memcpy(a, m, sizeof(a));
    float o[16];
    o[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
          a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    o[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
           a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    o[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
           a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    o[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
            a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    o[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
           a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    o[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
           a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    o[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
           a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    o[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
            a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    o[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
           a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    o[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
           a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    o[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
            a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    o[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
            a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    o[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
           a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    o[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
           a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    o[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
            a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    o[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
            a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * o[0] + a[1] * o[4] + a[2] * o[8] + a[3] * o[12];
    if (fabsf(det) < 1e-8f) return false;
    for (int i = 0; i < 16; ++i) (&inv[0][0])[i] = o[i] / det;
    return true;
}

using SurfaceFrameConstants = DxrFrameConstants;
static_assert(sizeof(SurfaceFrameConstants) <= kSurfaceFrameStride,
              "DXR surface frame constants exceed their upload-buffer slice");

DxrSurfaceRenderer& DxrSurfaceRenderer::Instance() {
    static DxrSurfaceRenderer renderer;
    return renderer;
}

DxrSurfaceRenderer::DxrSurfaceRenderer() {
    for (int i = 0; i < 4; ++i) {
        mViewMatrix[i][i] = 1.0f;
        mProjMatrix[i][i] = 1.0f;
        mInvProjectionMatrix[i][i] = 1.0f;
        mInvViewMatrix[i][i] = 1.0f;
    }
}

DxrSurfaceRenderer::~DxrSurfaceRenderer() {
    if (mPrimaryGateFenceEvent) {
        CloseHandle(mPrimaryGateFenceEvent);
        mPrimaryGateFenceEvent = nullptr;
    }
    if (mPathTemporalConstants && mPathTemporalConstantsMapped) {
        mPathTemporalConstants->Unmap(0, nullptr);
        mPathTemporalConstantsMapped = nullptr;
    }
    if (mConstantBuffer && mConstantBufferMapped) {
        mConstantBuffer->Unmap(0, nullptr);
        mConstantBufferMapped = nullptr;
    }
}

bool DxrSurfaceRenderer::EnsurePrimaryGateCounterResources(ID3D12Device* device) {
    if (mPrimaryGateCounters && (!mPrimaryGateCountersEnabled ||
        (mPrimaryGateReadback && mPrimaryGateFence && mPrimaryGateFenceEvent)))
        return true;

    D3D12_RESOURCE_DESC counterDesc = {};
    if (!mPrimaryGateCounters) {
        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        counterDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        counterDesc.Width = kPrimaryGateCounterBytes;
        counterDesc.Height = 1;
        counterDesc.DepthOrArraySize = 1;
        counterDesc.MipLevels = 1;
        counterDesc.Format = DXGI_FORMAT_UNKNOWN;
        counterDesc.SampleDesc.Count = 1;
        counterDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        counterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const HRESULT counterHr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &counterDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mPrimaryGateCounters));
        if (FAILED(counterHr)) {
            LogCouchDxrSurface("[COUCH-DXR-GATE][INIT_FAIL] counterBuffer hr=" + SurfaceHr(counterHr));
            return false;
        }
    }
    if (!mPrimaryGateCountersEnabled) return true;

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    counterDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    counterDesc.Width = kPrimaryGateCounterBytes;
    counterDesc.Height = 1;
    counterDesc.DepthOrArraySize = 1;
    counterDesc.MipLevels = 1;
    counterDesc.Format = DXGI_FORMAT_UNKNOWN;
    counterDesc.SampleDesc.Count = 1;
    counterDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RESOURCE_DESC readbackDesc = counterDesc;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mPrimaryGateReadback));
    if (FAILED(hr)) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][INIT_FAIL] readbackBuffer hr=" + SurfaceHr(hr));
        return false;
    }
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mPrimaryGateFence));
    if (FAILED(hr)) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][INIT_FAIL] readbackFence hr=" + SurfaceHr(hr));
        return false;
    }
    mPrimaryGateFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mPrimaryGateFenceEvent) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][INIT_FAIL] readbackFenceEvent win32=" +
                           std::to_string(GetLastError()));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-GATE] COUNTERS_READY elements=" +
                       std::to_string(kPrimaryGateCounterElements) + " bytes=" +
                       std::to_string(kPrimaryGateCounterBytes));
    return true;
}

bool DxrSurfaceRenderer::Init(ID3D12Device* device) {
    if (mInitialized) return true;
    if (!device) { LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] Device = FAIL reason=null"); return false; }
    mPrimaryGateCountersEnabled = PrimaryGateCountersRequested();
    if (!EnsurePrimaryGateCounterResources(device)) return false;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] CheckFeatureSupport_OPTIONS5 = FAIL hr=" + SurfaceHr(hr));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] CheckFeatureSupport_OPTIONS5 = PASS tier=" +
        std::to_string(static_cast<unsigned>(options5.RaytracingTier)));
    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] RaytracingTier = FAIL tier=" +
            std::to_string(static_cast<unsigned>(options5.RaytracingTier)));
        return false;
    }
    hr = device->QueryInterface(IID_PPV_ARGS(&mDevice5));
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] QueryInterface_ID3D12Device5 = FAIL hr=" + SurfaceHr(hr));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] QueryInterface_ID3D12Device5 = PASS");
    mSupported = true;

    auto runStep = [&](const char* name, auto&& operation) {
        gLastInitHresult = S_OK;
        const UINT64 infoMark = SurfaceInfoQueueMark(device);
        const bool ok = operation();
        SurfaceLogInfoQueue(device, infoMark, name);
        LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_STEP] ") + name + " = " + (ok ? "PASS" : "FAIL") +
            (ok ? "" : FAILED(gLastInitHresult) ? " hr=" + SurfaceHr(gLastInitHresult) : " hr=UNAVAILABLE"));
        if (!ok) LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_ABORTED_AT] ") + name);
        return ok;
    };
    if (!runStep("RootSignature", [&] { return CreateRootSignature(device); }) ||
        !runStep("RaytracingPipelineState", [&] { return CreateRaytracingPipelineState(mDevice5.Get()); }) ||
        !runStep("ShaderTable", [&] { return CreateShaderTable(device); }) ||
        !runStep("PathTemporalPipeline", [&] { return CreatePathTemporalPipeline(device); }) ||
        !runStep("CompositePipeline_R8G8B8A8_UNORM", [&] { return CreateCompositePipeline(device, DXGI_FORMAT_R8G8B8A8_UNORM); }) ||
        !runStep("CompositePipeline_R16G16B16A16_FLOAT", [&] { return CreateCompositePipeline(device, DXGI_FORMAT_R16G16B16A16_FLOAT); })) return false;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = kSurfaceFrameStride * 3;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&mConstantBuffer));
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] SurfaceConstants_CreateCommittedResource = FAIL hr=" + SurfaceHr(hr));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] SurfaceConstants_CreateCommittedResource = PASS hr=0x00000000");
    hr = mConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mConstantBufferMapped));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] SurfaceConstants_Map = FAIL hr=" + SurfaceHr(hr)); return false; }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] SurfaceConstants_Map = PASS hr=0x00000000");

    D3D12_HEAP_PROPERTIES pathTemporalUploadHeap = {};
    pathTemporalUploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC pathTemporalCbDesc = {};
    pathTemporalCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    pathTemporalCbDesc.Width = kPathTemporalFrameStride * 3;
    pathTemporalCbDesc.Height = 1;
    pathTemporalCbDesc.DepthOrArraySize = 1;
    pathTemporalCbDesc.MipLevels = 1;
    pathTemporalCbDesc.Format = DXGI_FORMAT_UNKNOWN;
    pathTemporalCbDesc.SampleDesc.Count = 1;
    pathTemporalCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&pathTemporalUploadHeap, D3D12_HEAP_FLAG_NONE,
                                               &pathTemporalCbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr, IID_PPV_ARGS(&mPathTemporalConstants));
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] PathTemporalConstants_CreateCommittedResource = FAIL hr=" + SurfaceHr(hr));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] PathTemporalConstants_CreateCommittedResource = PASS hr=0x00000000");
    hr = mPathTemporalConstants->Map(0, nullptr, reinterpret_cast<void**>(&mPathTemporalConstantsMapped));
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] PathTemporalConstants_Map = FAIL hr=" + SurfaceHr(hr));
        return false;
    }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_STEP] PathTemporalConstants_Map = PASS hr=0x00000000");

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        cvars->RegisterInteger("gEnhancements.Graphics.DXR.MaterialDebugMode", 0);
        // DXR Truth Mode selector (V1). 0=OFF, 1=HIT_MISS, 2=NORMAL,
        // 3=DISTANCE, 4=IDS, 5=SYNTHETIC_LAMBERT. Runtime-switchable, no
        // recompile needed. Single consumer: gfx_direct3d12 maps it to a
        // DxrTruthView diagnosticView for RenderReflections.
        cvars->RegisterInteger("gEnhancements.Graphics.DXR.TruthMode", 0);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.SurfaceReflection", 1.0f);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.SurfaceTraceDistance", 10000.0f);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.IndirectTraceDistance", 256.0f);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.SurfaceTransmission", 0.45f);
    }

    const char* env = getenv("COUCH_DXR_SURFACES");
    const bool explicitOff = env && strcmp(env, "0") == 0;
    const bool explicitOn = env && strcmp(env, "1") == 0;
    // The profile is applied by Interpreter::StartFrame after backend
    // initialization. Start enabled on supported hardware, then re-check the
    // active profile at the render gate below so Classic still stays clean.
    mEnabled = !explicitOff && (explicitOn || !env);
    mInitialized = true;
    LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE] initialized | enabled=") +
                       (mEnabled ? "YES" : "NO") + " | reflection=DXR");
    LogCouchDxrSurface("[COUCH-DXR-SURFACE] DXR_SURFACE_INIT_COMPLETE = YES");
    return true;
}

bool DxrSurfaceRenderer::CreateRootSignature(ID3D12Device* device) {
    constexpr uint32_t kSurfaceRootDwordCost = 4 * 4 + 4 + 2;
    static_assert(kSurfaceRootDwordCost <= 64,
                  "DXR surface root signature must stay within D3D12's DWORD budget");
    LogCouchDxrSurface("[COUCH-DXR-GATE] ROOT_SIGNATURE_DWORD_COST_BEFORE=" +
                       std::to_string(kSurfaceRootDwordCost));
    D3D12_ROOT_PARAMETER params[9] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE ranges[4] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    // The shadow state object owns u0. Surface probes use u1/u2/u3/u4 and the
    // diagnostic counter buffer at u5 so the
    // reflection/water mask, independent emissive mask, two-bounce indirect
    // lighting, full reflected radiance, and gate counters can be written by
    // the DXIL entry. This extends the existing table and adds no root DWORD.
    ranges[1].NumDescriptors = 5;
    ranges[1].BaseShaderRegister = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for (uint32_t parameter = 3; parameter <= 5; ++parameter) {
        params[parameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[parameter].Descriptor.ShaderRegister = parameter - 1;
        params[parameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = kDxrSurfaceTextureDescriptorCount;
    ranges[2].BaseShaderRegister = 5;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[6].DescriptorTable.NumDescriptorRanges = 1;
    params[6].DescriptorTable.pDescriptorRanges = &ranges[2];
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[7].Descriptor.ShaderRegister = 0;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    // Bind the raster environment and the full-resolution DXR soft-shadow
    // mask as adjacent SRVs at t4101/t4102.
    ranges[3].NumDescriptors = 2;
    ranges[3].BaseShaderRegister = 4101;
    ranges[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[8].DescriptorTable.NumDescriptorRanges = 1;
    params[8].DescriptorTable.pDescriptorRanges = &ranges[3];
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC materialSamplers[2] = {};
    for (uint32_t samplerIndex = 0; samplerIndex < 2; ++samplerIndex) {
        auto& materialSampler = materialSamplers[samplerIndex];
        materialSampler.Filter = samplerIndex == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                                                    : D3D12_FILTER_MIN_MAG_MIP_POINT;
        materialSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        materialSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        materialSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        materialSampler.MipLODBias = 0.0f;
        materialSampler.MaxAnisotropy = 1;
        materialSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        materialSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        materialSampler.MinLOD = 0.0f;
        materialSampler.MaxLOD = D3D12_FLOAT32_MAX;
        materialSampler.ShaderRegister = samplerIndex;
        materialSampler.RegisterSpace = 0;
        materialSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 9;
    desc.pParameters = params;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = materialSamplers;
    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = SerializeSurfaceRootSignature(&desc, &blob, &error);
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RootSignature SerializeSurfaceRootSignature hr=" + SurfaceHr(hr) +
            " rootParameters=9 descriptorTables=4");
        if (error && error->GetBufferPointer()) LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] RootSignature serializeMessage=") +
            static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mGlobalRootSignature));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RootSignature CreateRootSignature hr=" + SurfaceHr(hr)); return false; }
    LogCouchDxrSurface("[COUCH-DXR-GATE] ROOT_SIGNATURE_DWORD_COST_AFTER=" +
                       std::to_string(kSurfaceRootDwordCost));
    return true;
}

bool DxrSurfaceRenderer::CreateRaytracingPipelineState(ID3D12Device5* device5) {
    D3D12_STATE_SUBOBJECT subobjects[5] = {};
    D3D12_DXIL_LIBRARY_DESC library = {};
    library.DXILLibrary.pShaderBytecode = g_pDxrShadowBytecode;
    library.DXILLibrary.BytecodeLength = sizeof(g_pDxrShadowBytecode);
    D3D12_EXPORT_DESC exports[4] = {};
    exports[0].Name = L"RayGenReflection";
    exports[1].Name = L"MissReflection";
    exports[2].Name = L"ClosestHitReflection";
    exports[3].Name = L"AnyHitReflection";
    library.NumExports = 4;
    library.pExports = exports;
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &library;

    D3D12_HIT_GROUP_DESC hitGroup = {};
    hitGroup.HitGroupExport = L"HitGroupReflection";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.ClosestHitShaderImport = L"ClosestHitReflection";
    hitGroup.AnyHitShaderImport = L"AnyHitReflection";
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hitGroup;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    // Reflection carries material, normal, albedo, instance, UV and hit data.
    shaderConfig.MaxPayloadSizeInBytes = kReflectionPayloadSizeBytes;
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][BYTECODE] abi=" + std::to_string(kDxrShadowBytecodeAbiVersion) +
        " sourceSha256=" + kDxrShadowShaderSourceSha256 +
        " dxilSha256=" + kDxrShadowBytecodeDxilSha256 +
        " buildId=" + kDxrShadowBytecodeBuildId +
        " dxc=\"" + kDxrShadowBytecodeDxcVersion + "\" flags=\"" + kDxrShadowBytecodeCompileFlags + "\"" +
        " frameConstantsBytes=" + std::to_string(sizeof(DxrFrameConstants)));
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_PAYLOAD] REFLECTION_PAYLOAD_FIELDS=float4 radiance,float4 normalRoughness,float4 baseColorMetallic,uint materialIndex,uint probeMode,uint instanceIndex,uint textureIndex,float4 surfaceOptics,float4 detail,uint diagnosticFlags,uint diagnosticError,float4 geometricNormal EXPECTED_PAYLOAD_BYTES=" +
        std::to_string(kReflectionPayloadSizeBytes) + " STATE_OBJECT_DECLARED_PAYLOAD_BYTES=" +
        std::to_string(shaderConfig.MaxPayloadSizeInBytes));
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[2].pDesc = &shaderConfig;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRoot = {};
    globalRoot.pGlobalRootSignature = mGlobalRootSignature.Get();
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[3].pDesc = &globalRoot;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    // GI rays may take a second diffuse bounce. Each hit can also trace a
    // visibility ray to active scene lights, which uses the third level.
    pipelineConfig.MaxTraceRecursionDepth = 3;
    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[4].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC stateDesc = {};
    stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateDesc.NumSubobjects = 5;
    stateDesc.pSubobjects = subobjects;
    HRESULT hr = device5->CreateStateObject(&stateDesc, IID_PPV_ARGS(&mStateObject));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RaytracingPipelineState CreateStateObject exports=RayGenReflection,MissReflection,ClosestHitReflection,AnyHitReflection payloadBytes=" + std::to_string(shaderConfig.MaxPayloadSizeInBytes) + " maxRecursion=3 hr=" + SurfaceHr(hr)); return false; }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RaytracingPipelineState CreateStateObject hr=0x00000000 payloadBytes=" + std::to_string(shaderConfig.MaxPayloadSizeInBytes));
    hr = mStateObject.As(&mStateObjectProperties);
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RaytracingPipelineState QueryStateObjectProperties hr=" + SurfaceHr(hr)); return false; }
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RaytracingPipelineState QueryStateObjectProperties hr=0x00000000");
    mStats.stateObjectCreated = mStateObjectProperties->GetShaderIdentifier(L"RayGenReflection") != nullptr;
    if (!mStats.stateObjectCreated) LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] RaytracingPipelineState missing shader identifier export=RayGenReflection");
    return mStats.stateObjectCreated;
}

bool DxrSurfaceRenderer::CreateShaderTable(ID3D12Device* device) {
    if (!mStateObjectProperties) { LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] ShaderTable missing StateObjectProperties"); return false; }
    void* rayGen = mStateObjectProperties->GetShaderIdentifier(L"RayGenReflection");
    void* miss = mStateObjectProperties->GetShaderIdentifier(L"MissReflection");
    void* hit = mStateObjectProperties->GetShaderIdentifier(L"HitGroupReflection");
    if (!rayGen || !miss || !hit) { LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] ShaderTable missing identifier raygen=" + std::to_string(rayGen != nullptr) + " miss=" + std::to_string(miss != nullptr) + " hit=" + std::to_string(hit != nullptr)); return false; }

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 768;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&mShaderTableBuffer));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] ShaderTable CreateCommittedResource bytes=768 hr=" + SurfaceHr(hr)); return false; }
    uint8_t* data = nullptr;
    hr = mShaderTableBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] ShaderTable Map hr=" + SurfaceHr(hr)); return false; }
    memset(data, 0, 768);
    memcpy(data + mRayGenRecordOffset, rayGen, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(data + mMissRecordOffset, miss, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(data + mHitGroupRecordOffset, hit, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    mShaderTableBuffer->Unmap(0, nullptr);
    mStats.shaderTableReady = true;
    return true;
}

bool DxrSurfaceRenderer::EnsureResources(ID3D12Device* device, uint32_t maskWidth, uint32_t maskHeight,
                                         uint32_t sceneWidth, uint32_t sceneHeight, DXGI_FORMAT sceneFormat) {
    if (mReflectionMask && mReflectionColor && mEmissiveMask && mIndirectMask && mSceneColorCopy &&
        mMaskWidth == maskWidth && mMaskHeight == maskHeight &&
        mSceneWidth == sceneWidth && mSceneHeight == sceneHeight && mSceneFormat == sceneFormat && mDescriptorHeap) {
        return EnsurePathTemporalResources(device, maskWidth, maskHeight);
    }
    mReflectionMask.Reset();
    mReflectionColor.Reset();
    mEmissiveMask.Reset();
    mIndirectMask.Reset();
    mSceneColorCopy.Reset();
    for (auto& target : mVisualAblationTargets) target.Reset();
    mVisualAblationRtvs.Reset();
    mVisualAblationFrame = -1;
    mVisualAblationCaptured = false;
    mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
    mMaskWidth = maskWidth;
    mMaskHeight = maskHeight;
    mSceneWidth = sceneWidth;
    mSceneHeight = sceneHeight;
    mSceneFormat = sceneFormat;

    D3D12_FEATURE_DATA_FORMAT_SUPPORT emissiveFormatSupport = {};
    emissiveFormatSupport.Format = DXGI_FORMAT_R16_FLOAT;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &emissiveFormatSupport,
                                           sizeof(emissiveFormatSupport))) ||
        (emissiveFormatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
        mEmissiveFormat = DXGI_FORMAT_R32_FLOAT;
    } else {
        mEmissiveFormat = DXGI_FORMAT_R16_FLOAT;
    }

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = maskWidth;
    desc.Height = maskHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&mReflectionMask)))) return false;

    D3D12_RESOURCE_DESC reflectionColorDesc = desc;
    reflectionColorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &reflectionColorDesc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&mReflectionColor)))) return false;

    D3D12_RESOURCE_DESC emissiveDesc = desc;
    emissiveDesc.Format = mEmissiveFormat;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &emissiveDesc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&mEmissiveMask)))) return false;

    D3D12_RESOURCE_DESC indirectDesc = desc;
    indirectDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &indirectDesc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&mIndirectMask)))) return false;

    D3D12_RESOURCE_DESC sceneDesc = desc;
    sceneDesc.Width = sceneWidth;
    sceneDesc.Height = sceneHeight;
    sceneDesc.Format = sceneFormat;
    sceneDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &sceneDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&mSceneColorCopy)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // 0: depth SRV; 1/2/3/4/5: surface, emissive, indirect-light,
    // reflected-radiance, and gate-counter UAVs; 6..10: surface, scene-color,
    // emissive, indirect-light, and reflected-radiance SRVs. The UAVs are
    // adjacent for the existing DXR output descriptor table (u1..u5).
    heapDesc.NumDescriptors = 13 + kDxrSurfaceTextureDescriptorCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)))) return false;
    InitializeDxrDescriptorTemplate(device,mDescriptorHeap.Get());
        mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Slot 11 is the null texture for untextured / unresolved materials;
    // slots 12..4106 are the compact per-frame OTR texture table.
    D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrv = {};
    nullTextureSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTextureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTextureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTextureSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE nullTextureCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    nullTextureCpu.ptr += kSurfaceTextureDescriptorBase * mDescriptorSize;
    device->CreateShaderResourceView(nullptr, &nullTextureSrv, nullTextureCpu);
    mMaskTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mReflectionColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mEmissiveTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mIndirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mSceneColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    uavCpu.ptr += mDescriptorSize;
    device->CreateUnorderedAccessView(mReflectionMask.Get(), nullptr, &uav, uavCpu);

    D3D12_UNORDERED_ACCESS_VIEW_DESC emissiveUav = {};
    emissiveUav.Format = mEmissiveFormat;
    emissiveUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE emissiveUavCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    emissiveUavCpu.ptr += 2 * mDescriptorSize;
    device->CreateUnorderedAccessView(mEmissiveMask.Get(), nullptr, &emissiveUav, emissiveUavCpu);

    D3D12_CPU_DESCRIPTOR_HANDLE indirectUavCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    indirectUavCpu.ptr += 3 * mDescriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC indirectUav = {};
    indirectUav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    indirectUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mIndirectMask.Get(), nullptr, &indirectUav, indirectUavCpu);

    D3D12_UNORDERED_ACCESS_VIEW_DESC reflectionColorUav = {};
    reflectionColorUav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    reflectionColorUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE reflectionColorUavCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    reflectionColorUavCpu.ptr += 4 * mDescriptorSize;
    device->CreateUnorderedAccessView(mReflectionColor.Get(), nullptr, &reflectionColorUav, reflectionColorUavCpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC maskSrv = {};
    maskSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    maskSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    maskSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    maskSrv.Texture2D.MipLevels = 1;
    if (mPrimaryGateCounters) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC counterUav = {};
        counterUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        counterUav.Format = DXGI_FORMAT_UNKNOWN;
        counterUav.Buffer.NumElements = kPrimaryGateCounterElements;
        counterUav.Buffer.StructureByteStride = sizeof(uint32_t);
        D3D12_CPU_DESCRIPTOR_HANDLE counterUavCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        counterUavCpu.ptr += kPrimaryGateCounterDescriptor * mDescriptorSize;
        device->CreateUnorderedAccessView(mPrimaryGateCounters.Get(), nullptr, &counterUav, counterUavCpu);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE maskSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    maskSrvCpu.ptr += kSurfaceSrvDescriptorBase * mDescriptorSize;
    device->CreateShaderResourceView(mReflectionMask.Get(), &maskSrv, maskSrvCpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrv = {};
    sceneSrv.Format = sceneFormat;
    sceneSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sceneSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sceneSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    sceneSrvCpu.ptr += (kSurfaceSrvDescriptorBase + 1) * mDescriptorSize;
    device->CreateShaderResourceView(mSceneColorCopy.Get(), &sceneSrv, sceneSrvCpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC emissiveSrv = {};
    emissiveSrv.Format = mEmissiveFormat;
    emissiveSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    emissiveSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    emissiveSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE emissiveSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    emissiveSrvCpu.ptr += (kSurfaceSrvDescriptorBase + 2) * mDescriptorSize;
    device->CreateShaderResourceView(mEmissiveMask.Get(), &emissiveSrv, emissiveSrvCpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC indirectSrv = {};
    indirectSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    indirectSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    indirectSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    indirectSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE indirectSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    indirectSrvCpu.ptr += (kSurfaceSrvDescriptorBase + 3) * mDescriptorSize;
    device->CreateShaderResourceView(mIndirectMask.Get(), &indirectSrv, indirectSrvCpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC reflectionColorSrv = maskSrv;
    reflectionColorSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_CPU_DESCRIPTOR_HANDLE reflectionColorSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    reflectionColorSrvCpu.ptr += (kSurfaceSrvDescriptorBase + 4) * mDescriptorSize;
    device->CreateShaderResourceView(mReflectionColor.Get(), &reflectionColorSrv, reflectionColorSrvCpu);
    return EnsurePathTemporalResources(device, maskWidth, maskHeight);
}

bool DxrSurfaceRenderer::CreatePathTemporalPipeline(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 7;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = params;
    ComPtr<ID3DBlob> rootBlob, rootError;
    HRESULT hr = SerializeSurfaceRootSignature(&rootDesc, &rootBlob, &rootError);
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] PathTemporalPipeline SerializeRootSignature params=3 srv=7 uav=4 hr=" + SurfaceHr(hr));
        if (rootError && rootError->GetBufferPointer()) LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] PathTemporalPipeline serializeMessage=") + static_cast<const char*>(rootError->GetBufferPointer()));
        return false;
    }
    hr = device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&mPathTemporalRootSignature));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] PathTemporalPipeline CreateRootSignature hr=" + SurfaceHr(hr)); return false; }

    const char* cs =
        "cbuffer PathTemporalConstants : register(b0) { float4 gSize; float4 gParams; };\n"
        "Texture2D<float4> gCurrent : register(t0);\n"
        "Texture2D<float> gDepth : register(t1);\n"
        "Texture2D<float2> gMotion : register(t2);\n"
        "Texture2D<float4> gHistory : register(t3);\n"
        "Texture2D<float4> gReflectionCurrent : register(t4);\n"
        "Texture2D<float4> gSurface : register(t5);\n"
        "Texture2D<float4> gReflectionHistory : register(t6);\n"
        "RWTexture2D<float4> gOutput : register(u0);\n"
        "RWTexture2D<float4> gHistoryOut : register(u1);\n"
        "RWTexture2D<float4> gReflectionOutput : register(u2);\n"
        "RWTexture2D<float4> gReflectionHistoryOut : register(u3);\n"
        "[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {\n"
        "  uint2 pathSize = uint2(gSize.xy); uint2 fullSize = uint2(gSize.zw);\n"
        "  if (id.x >= pathSize.x || id.y >= pathSize.y) return;\n"
        "  float4 current = gCurrent.Load(int3(id.xy,0));\n"
        "  float2 uv = (float2(id.xy)+0.5)/float2(pathSize);\n"
        "  uint2 fullPixel = min(uint2(uv*float2(fullSize)),fullSize-1);\n"
        "  float depth = gDepth.Load(int3(fullPixel,0));\n"
        "  float4 reflection = gReflectionCurrent.Load(int3(id.xy,0));\n"
        "  float4 surface = gSurface.Load(int3(id.xy,0));\n"
        "  float2 motion = gMotion.Load(int3(fullPixel,0));\n"
        "  float2 previousUv = uv-motion;\n"
        "  bool reproject = gParams.y > 0.5 && all(isfinite(motion)) && all(previousUv >= 0.0) && all(previousUv < 1.0);\n"
        "  float3 result = current.rgb;\n"
        "  if (current.a >= 1.5 && depth < 0.9999 && reproject) {\n"
        "    uint2 previousPixel = min(uint2(previousUv*float2(pathSize)),pathSize-1);\n"
        "    float4 previous = gHistory.Load(int3(previousPixel,0));\n"
        "    float depthTolerance = max(gParams.z,depth*0.015);\n"
        "    if (previous.a >= 0.0 && abs(depth-previous.a) <= depthTolerance) {\n"
        "      float3 low=current.rgb, high=current.rgb;\n"
        "      [unroll] for (int y=-1;y<=1;++y) [unroll] for (int x=-1;x<=1;++x) {\n"
        "        int2 p=clamp(int2(id.xy)+int2(x,y),int2(0,0),int2(pathSize)-1);\n"
        "        float4 neighbor=gCurrent.Load(int3(p,0));\n"
        "        uint2 neighborFull=min(uint2((float2(p)+0.5)/float2(pathSize)*float2(fullSize)),fullSize-1);\n"
        "        float neighborDepth=gDepth.Load(int3(neighborFull,0));\n"
        "        if (neighbor.a>=1.5 && abs(neighborDepth-depth)<=depthTolerance) {\n"
        "          low=min(low,neighbor.rgb); high=max(high,neighbor.rgb);\n"
        "        }\n"
        "      }\n"
        "      float3 margin=max((high-low)*0.20,0.01.xxx);\n"
        "      float3 clippedHistory=clamp(previous.rgb,low-margin,high+margin);\n"
        "      float motionWeight=exp(-length(motion)*8.0);\n"
        "      result=lerp(current.rgb,clippedHistory,saturate(gParams.x*motionWeight));\n"
        "    }\n"
        "  }\n"
        "  bool pathValid = current.a >= 1.5 && depth < 0.9999;\n"
        "  gOutput[id.xy]=pathValid ? float4(result,2.0) : current;\n"
        "  gHistoryOut[id.xy]=pathValid ? float4(result,depth) : float4(0,0,0,-1);\n"
        "  float signalCoverage=max(surface.r,surface.g);\n"
        "  bool reflectionValid=signalCoverage>0.001 && depth<0.9999;\n"
        "  float3 reflectionResult=reflection.rgb;\n"
        "  if (reflectionValid && reproject) {\n"
        "    uint2 previousPixel=min(uint2(previousUv*float2(pathSize)),pathSize-1);\n"
        "    float4 previous=gReflectionHistory.Load(int3(previousPixel,0));\n"
        "    float depthTolerance=max(gParams.z,depth*0.015);\n"
        "    if (previous.a>=0.0 && abs(depth-previous.a)<=depthTolerance) {\n"
        "      float3 low=reflection.rgb, high=reflection.rgb;\n"
        "      [unroll] for (int y=-1;y<=1;++y) [unroll] for (int x=-1;x<=1;++x) {\n"
        "        int2 p=clamp(int2(id.xy)+int2(x,y),int2(0,0),int2(pathSize)-1);\n"
        "        float4 neighbor=gReflectionCurrent.Load(int3(p,0));\n"
        "        float4 neighborSurface=gSurface.Load(int3(p,0));\n"
        "        uint2 neighborFull=min(uint2((float2(p)+0.5)/float2(pathSize)*float2(fullSize)),fullSize-1);\n"
        "        float neighborDepth=gDepth.Load(int3(neighborFull,0));\n"
        "        if (max(neighborSurface.r,neighborSurface.g)>0.001 && abs(neighborDepth-depth)<=depthTolerance) {\n"
        "          low=min(low,neighbor.rgb); high=max(high,neighbor.rgb);\n"
        "        }\n"
        "      }\n"
        "      float3 margin=max((high-low)*0.20,0.01.xxx);\n"
        "      float motionWeight=exp(-length(motion)*8.0);\n"
        "      reflectionResult=lerp(reflection.rgb,clamp(previous.rgb,low-margin,high+margin),saturate(gParams.x*motionWeight));\n"
        "    }\n"
        "  }\n"
        "  gReflectionOutput[id.xy]=reflectionValid ? float4(reflectionResult,reflection.a) : reflection;\n"
        "  gReflectionHistoryOut[id.xy]=reflectionValid ? float4(reflectionResult,depth) : float4(0,0,0,-1);\n"
        "}\n";
    ComPtr<ID3DBlob> csBlob, csError;
    hr = D3DCompile(cs, strlen(cs), "PathRadianceTemporalCS", nullptr, nullptr,
                    "CSMain", "cs_5_0", 0, 0, &csBlob, &csError);
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] PathTemporalPipeline D3DCompile shader=PathRadianceTemporalCS entry=CSMain profile=cs_5_0 hr=" + SurfaceHr(hr));
        if (csError) LogCouchDxrSurface("[COUCH-DXR-SURFACE][ERROR] Path temporal CS: " +
            std::string(static_cast<const char*>(csError->GetBufferPointer()), csError->GetBufferSize()));
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = mPathTemporalRootSignature.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mPathTemporalPipeline));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface("[COUCH-DXR-SURFACE][INIT_OP] PathTemporalPipeline CreateComputePipelineState shader=PathRadianceTemporalCS root=PathTemporalRootSignature hr=" + SurfaceHr(hr)); return false; }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 11;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mPathTemporalDescriptorHeap)))) return false;
    InitializeDxrDescriptorTemplate(device, mPathTemporalDescriptorHeap.Get());
    mPathTemporalDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool DxrSurfaceRenderer::EnsurePathTemporalResources(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (mPathHistory[0] && mPathHistory[1] && mPathTemporalOutput &&
        mReflectionHistory[0] && mReflectionHistory[1] && mReflectionTemporalOutput &&
        mPathHistoryWidth == width && mPathHistoryHeight == height) return true;
    for (auto& history : mPathHistory) history.Reset();
    for (auto& history : mReflectionHistory) history.Reset();
    mPathTemporalOutput.Reset();
    mReflectionTemporalOutput.Reset();
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    for (auto& history : mPathHistory) {
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&history)))) return false;
    }
    if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&mPathTemporalOutput)))) return false;
    for (auto& history : mReflectionHistory) {
        if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&history)))) return false;
    }
    if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&mReflectionTemporalOutput)))) return false;
    mPathHistoryWidth = width;
    mPathHistoryHeight = height;
    mPathHistoryIndex = 0;
    mPathHistoryValid = false;
    mPathHistoryStates[0] = mPathHistoryStates[1] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mReflectionHistoryStates[0] = mReflectionHistoryStates[1] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mPathTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    mReflectionTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][TEMPORAL] radiance/reflection histories created " +
                       std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool DxrSurfaceRenderer::DispatchPathTemporal(ID3D12GraphicsCommandList* cmdList,
                                               ID3D12Resource* depthBuffer,
                                               D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                                               uint32_t width, uint32_t height,
                                               uint32_t screenWidth, uint32_t screenHeight,
                                               uint32_t frameIndex) {
    auto& temporal = TemporalManager::Instance();
    const bool pathProfile = MaterialRegistry::IsPathTracedExperimentalProfile();
    ID3D12Resource* motionBuffer = temporal.GetMotionVectorBuffer();
    if (!pathProfile || !temporal.IsEnabled() || !motionBuffer || !mPathTemporalPipeline ||
        !mPathTemporalConstantsMapped || !cmdList) {
        mPathHistoryValid = false;
        mPathProfileWasActive = pathProfile;
        return false;
    }
    if (!mPathProfileWasActive) mPathHistoryValid = false;
    mPathProfileWasActive = true;

    const TemporalStats& temporalStats = temporal.GetStats();
    if (temporalStats.cameraCutsDetected != mLastTemporalCameraCutCount ||
        temporalStats.sceneChangesDetected != mLastTemporalSceneChangeCount) {
        mPathHistoryValid = false;
        mLastTemporalCameraCutCount = temporalStats.cameraCutsDetected;
        mLastTemporalSceneChangeCount = temporalStats.sceneChangesDetected;
    }

    PathTemporalConstants constants{};
    constants.size[0] = static_cast<float>(width);
    constants.size[1] = static_cast<float>(height);
    constants.size[2] = static_cast<float>(screenWidth);
    constants.size[3] = static_cast<float>(screenHeight);
    const float samples = static_cast<float>(std::max(1u, mIndirectSamples));
    constants.params[0] = 0.90f / std::sqrt(1.0f + samples * 0.15f);
    constants.params[1] = mPathHistoryValid ? 1.0f : 0.0f;
    constants.params[2] = 0.0025f;
    memcpy(mPathTemporalConstantsMapped + (frameIndex % 3) * kPathTemporalFrameStride,
           &constants, sizeof(constants));

    const uint32_t readIndex = mPathHistoryIndex;
    const uint32_t writeIndex = 1u - readIndex;
    ID3D12Resource* historyRead = mPathHistory[readIndex].Get();
    ID3D12Resource* historyWrite = mPathHistory[writeIndex].Get();
    ID3D12Resource* reflectionHistoryRead = mReflectionHistory[readIndex].Get();
    ID3D12Resource* reflectionHistoryWrite = mReflectionHistory[writeIndex].Get();
    if (!historyRead || !historyWrite || !mPathTemporalOutput || !mReflectionTemporalOutput ||
        !reflectionHistoryRead || !reflectionHistoryWrite || !mReflectionColor || !mReflectionMask) return false;

    D3D12_RESOURCE_BARRIER pre[7] = {};
    pre[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[0].Transition.pResource = mIndirectMask.Get();
    pre[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    pre[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    pre[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[1].Transition.pResource = depthBuffer;
    pre[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    pre[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    pre[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[2].Transition.pResource = motionBuffer;
    pre[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[2].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    pre[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[3].Transition.pResource = historyWrite;
    pre[3].Transition.StateBefore = mPathHistoryStates[writeIndex];
    pre[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    for (uint32_t i = 4; i < 6; ++i) {
        pre[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        pre[i].Transition.pResource = i == 4 ? mReflectionColor.Get() : mReflectionMask.Get();
        pre[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        pre[i].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        pre[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    pre[6].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[6].Transition.pResource = reflectionHistoryWrite;
    pre[6].Transition.StateBefore = mReflectionHistoryStates[writeIndex];
    pre[6].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[6].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(7, pre);

    if (mPathTemporalOutputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER outputToUav = {};
        outputToUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        outputToUav.Transition.pResource = mPathTemporalOutput.Get();
        outputToUav.Transition.StateBefore = mPathTemporalOutputState;
        outputToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        outputToUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &outputToUav);
        mPathTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (mReflectionTemporalOutputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER outputToUav = {};
        outputToUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        outputToUav.Transition.pResource = mReflectionTemporalOutput.Get();
        outputToUav.Transition.StateBefore = mReflectionTemporalOutputState;
        outputToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        outputToUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &outputToUav);
        mReflectionTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    SelectDxrDescriptorHeap(mDevice5.Get(), mPathTemporalDescriptorHeap,
                            mPathTemporalDescriptorTemplate, mPathTemporalDescriptorFrames, frameIndex % 3);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = mPathTemporalDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = mPathTemporalDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mDevice5->CreateShaderResourceView(mIndirectMask.Get(), &srv, cpu);
    cpu.ptr += mPathTemporalDescriptorSize;
    mDevice5->CopyDescriptorsSimple(1, cpu, depthSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu.ptr += mPathTemporalDescriptorSize;
    srv.Format = DXGI_FORMAT_R16G16_FLOAT;
    mDevice5->CreateShaderResourceView(motionBuffer, &srv, cpu);
    cpu.ptr += mPathTemporalDescriptorSize;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mDevice5->CreateShaderResourceView(historyRead, &srv, cpu);
    cpu.ptr += mPathTemporalDescriptorSize;
    mDevice5->CreateShaderResourceView(mReflectionColor.Get(), &srv, cpu);
    cpu.ptr += mPathTemporalDescriptorSize;
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    mDevice5->CreateShaderResourceView(mReflectionMask.Get(), &srv, cpu);
    cpu.ptr += mPathTemporalDescriptorSize;
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mDevice5->CreateShaderResourceView(reflectionHistoryRead, &srv, cpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = mPathTemporalDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    uavCpu.ptr += 7 * mPathTemporalDescriptorSize;
    mDevice5->CreateUnorderedAccessView(mPathTemporalOutput.Get(), nullptr, &uav, uavCpu);
    uavCpu.ptr += mPathTemporalDescriptorSize;
    mDevice5->CreateUnorderedAccessView(historyWrite, nullptr, &uav, uavCpu);
    uavCpu.ptr += mPathTemporalDescriptorSize;
    mDevice5->CreateUnorderedAccessView(mReflectionTemporalOutput.Get(), nullptr, &uav, uavCpu);
    uavCpu.ptr += mPathTemporalDescriptorSize;
    mDevice5->CreateUnorderedAccessView(reflectionHistoryWrite, nullptr, &uav, uavCpu);

    ID3D12DescriptorHeap* heaps[] = { mPathTemporalDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(mPathTemporalRootSignature.Get());
    cmdList->SetPipelineState(mPathTemporalPipeline.Get());
    cmdList->SetComputeRootConstantBufferView(0, mPathTemporalConstants->GetGPUVirtualAddress() +
        (frameIndex % 3) * kPathTemporalFrameStride);
    cmdList->SetComputeRootDescriptorTable(1, gpu);
    gpu.ptr += 7 * mPathTemporalDescriptorSize;
    cmdList->SetComputeRootDescriptorTable(2, gpu);
    cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

    D3D12_RESOURCE_BARRIER post[12] = {};
    post[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[0].UAV.pResource = mPathTemporalOutput.Get();
    post[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[1].UAV.pResource = historyWrite;
    post[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[2].UAV.pResource = mReflectionTemporalOutput.Get();
    post[3].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[3].UAV.pResource = reflectionHistoryWrite;
    post[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[4].Transition.pResource = mPathTemporalOutput.Get();
    post[4].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[4].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[5] = pre[3];
    post[5].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[5].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[6].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[6].Transition.pResource = mReflectionTemporalOutput.Get();
    post[6].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[6].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[6].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[7] = pre[6];
    post[7].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[7].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[8] = pre[0];
    post[8].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[8].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[9] = pre[1];
    post[9].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[9].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    post[10] = pre[4];
    post[10].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[10].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[11] = pre[5];
    post[11].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[11].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(12, post);
    D3D12_RESOURCE_BARRIER motionRestore = pre[2];
    motionRestore.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    motionRestore.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdList->ResourceBarrier(1, &motionRestore);
    mPathHistoryStates[writeIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mReflectionHistoryStates[writeIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mPathTemporalOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    mReflectionTemporalOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    mPathHistoryIndex = writeIndex;
    mPathHistoryValid = true;

    // Replace the composite's indirect-radiance SRV with the denoised output.
    SelectDxrDescriptorHeap(mDevice5.Get(), mDescriptorHeap, mDescriptorTemplate,
                            mDescriptorFrames, frameIndex % 3);
    D3D12_SHADER_RESOURCE_VIEW_DESC filteredSrv = {};
    filteredSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    filteredSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    filteredSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    filteredSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE filteredSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    filteredSrvCpu.ptr += (kSurfaceSrvDescriptorBase + 3) * mDescriptorSize;
    mDevice5->CreateShaderResourceView(mPathTemporalOutput.Get(), &filteredSrv, filteredSrvCpu);
    filteredSrvCpu.ptr += mDescriptorSize;
    mDevice5->CreateShaderResourceView(mReflectionTemporalOutput.Get(), &filteredSrv, filteredSrvCpu);
    ID3D12DescriptorHeap* surfaceHeaps[] = { mDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, surfaceHeaps);
    LogCouchDxrSurface("[COUCH-DXR-SURFACE][TEMPORAL] radiance/reflections reprojected frame=" +
        std::to_string(frameIndex) + " historyWeight=" + std::to_string(constants.params[0]));
    return true;
}

bool DxrSurfaceRenderer::CreateCompositePipeline(ID3D12Device* device, DXGI_FORMAT outputFormat) {
    const size_t outputIndex = outputFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1u : 0u;
    D3D12_ROOT_PARAMETER params[7] = {};
    D3D12_DESCRIPTOR_RANGE sceneRange = {};
    sceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sceneRange.NumDescriptors = 1;
    sceneRange.BaseShaderRegister = 0;
    sceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &sceneRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 1;
    depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &depthRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE maskRange = {};
    maskRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    maskRange.NumDescriptors = 1;
    maskRange.BaseShaderRegister = 2;
    maskRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &maskRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE emissiveRange = {};
    emissiveRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    emissiveRange.NumDescriptors = 1;
    emissiveRange.BaseShaderRegister = 3;
    emissiveRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &emissiveRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE indirectRange = {};
    indirectRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    indirectRange.NumDescriptors = 1;
    indirectRange.BaseShaderRegister = 4;
    indirectRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable.NumDescriptorRanges = 1;
    params[4].DescriptorTable.pDescriptorRanges = &indirectRange;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE reflectionRange = {};
    reflectionRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    reflectionRange.NumDescriptors = 1;
    reflectionRange.BaseShaderRegister = 5;
    reflectionRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].DescriptorTable.NumDescriptorRanges = 1;
    params[5].DescriptorTable.pDescriptorRanges = &reflectionRange;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // float3 gGameLightDir starts at byte 32 in HLSL, not byte 24.
    // Reserve and upload the complete 48-byte layout, including padding.
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[6].Constants.ShaderRegister = 0;
    params[6].Constants.Num32BitValues = kSurfaceCompositeRootDwords;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs = {};
    rs.NumParameters = 7;
    rs.pParameters = params;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = SerializeSurfaceRootSignature(&rs, &blob, &error);
    const char* formatName = outputFormat == DXGI_FORMAT_R8G8B8A8_UNORM ? "R8G8B8A8_UNORM" :
        outputFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? "R16G16B16A16_FLOAT" : "UNKNOWN";
    if (FAILED(hr)) {
        gLastInitHresult = hr;
        LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] CompositePipeline SerializeRootSignature format=") + formatName + " params=7 srv=6 constants=" + std::to_string(kSurfaceCompositeRootDwords) + " hr=" + SurfaceHr(hr));
        if (error && error->GetBufferPointer()) LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] CompositePipeline serializeMessage=") + static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (!mCompositeRootSig) {
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mCompositeRootSig));
        if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] CompositePipeline CreateRootSignature format=") + formatName + " hr=" + SurfaceHr(hr)); return false; }
    }

    const char* vs =
        "struct VSOut { float4 position : SV_POSITION; };\n"
        "VSOut VSMain(uint id : SV_VertexID) {\n"
        "  float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };\n"
        "  VSOut o; o.position = float4(p[id],0,1); return o;\n"
        "}\n";
    const char* ps =
        "Texture2D<float4> gSceneColor : register(t0);\n"
        "Texture2D<float> gDepth : register(t1);\n"
        "Texture2D<float4> gSurface : register(t2);\n"
        "Texture2D<float> gEmissiveMask : register(t3);\n"
        "Texture2D<float4> gIndirectMask : register(t4);\n"
        "Texture2D<float4> gReflectionColor : register(t5);\n"
        "SamplerState gSampler : register(s0);\n"
        "cbuffer SurfaceComposite : register(b0) { float4 gParams; float gSceneHdr; float gDiagnosticView; float gAblation; float gPadding; float3 gGameLightDir; float gGameLightValid; };\n"
        "float3 CompositeSrgbToLinear(float3 encodedColor) {\n"
        "  float3 color = max(encodedColor, 0.0);\n"
        "  float3 lower = color / 12.92;\n"
        "  float3 higher = pow((color + 0.055) / 1.055, 2.4);\n"
        "  return lerp(higher, lower, 1.0 - step(0.04045, color));\n"
        "}\n"
        "float3 CompositeLinearToExtendedSrgb(float3 linearColor) {\n"
        "  float3 color = max(linearColor, 0.0);\n"
        "  float3 lower = color * 12.92;\n"
        "  float3 higher = 1.055 * pow(color, 1.0 / 2.4) - 0.055;\n"
        "  return lerp(higher, lower, 1.0 - step(0.0031308, color));\n"
        "}\n"
        "float3 CompositeLinearToSrgb(float3 linearColor) {\n"
        "  float3 color = max(linearColor, 0.0);\n"
        "  float3 lower = color * 12.92;\n"
        "  float3 higher = 1.055 * pow(color, 1.0 / 2.4) - 0.055;\n"
        "  return lerp(higher, lower, 1.0 - step(0.0031308, color));\n"
        "}\n"
        "float3 ToneMapRadiance(float3 color) {\n"
        "  color = max(color, 0.0);\n"
        "  return saturate(color * (2.51 * color + 0.03) / (color * (2.43 * color + 0.59) + 0.14));\n"
        "}\n"
        "bool DiagnosticInvalid(float4 value) { return any(isnan(value)) || any(isinf(value)); }\n"
        "float4 DiagnosticRadiance(float4 value) {\n"
        "  if (DiagnosticInvalid(value)) return float4(1.0, 0.0, 1.0, 1.0);\n"
        "  float3 displayColor = CompositeLinearToSrgb(ToneMapRadiance(max(value.rgb, 0.0)));\n"
        "  return float4(saturate(displayColor), 1.0);\n"
        "}\n"
        "void SampleDepthGuided(float2 uv, float centerDepth, float2 fullSize, float2 maskSize,\n"
        "                      out float4 surface, out float emissive,\n"
        "                      out float4 indirect, out float4 reflection) {\n"
        "  surface = 0.0; emissive = 0.0; indirect = 0.0; reflection = 0.0;\n"
        "  if (maskSize.x >= fullSize.x && maskSize.y >= fullSize.y) {\n"
        "    uint2 p = min(uint2(uv * maskSize), uint2(maskSize) - 1);\n"
        "    surface = gSurface.Load(int3(p, 0));\n"
        "    emissive = gEmissiveMask.Load(int3(p, 0));\n"
        "    indirect = gIndirectMask.Load(int3(p, 0));\n"
        "    reflection = gReflectionColor.Load(int3(p, 0));\n"
        "    return;\n"
        "  }\n"
        "  float2 samplePosition = uv * maskSize - 0.5;\n"
        "  int2 basePixel = int2(floor(samplePosition));\n"
        "  float2 fraction = frac(samplePosition);\n"
        "  float4 surfaceSum = 0.0; float emissiveSum = 0.0;\n"
        "  float4 indirectSum = 0.0; float4 reflectionSum = 0.0; float totalWeight = 0.0;\n"
        "  float closestDepthDelta = 3.402823466e+38;\n"
        "  float4 fallbackSurface = 0.0; float fallbackEmissive = 0.0;\n"
        "  float4 fallbackIndirect = 0.0; float4 fallbackReflection = 0.0;\n"
        "  float depthTolerance = max(0.00015, centerDepth * 0.004);\n"
        "  [unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x) {\n"
        "    int2 p = clamp(basePixel + int2(x, y), int2(0, 0), int2(maskSize) - 1);\n"
        "    float spatialWeight = (x == 0 ? 1.0 - fraction.x : fraction.x) *\n"
        "                         (y == 0 ? 1.0 - fraction.y : fraction.y);\n"
        "    uint2 depthPixel = min(uint2((float2(p) + 0.5) / maskSize * fullSize), uint2(fullSize) - 1);\n"
        "    float sampleDepth = gDepth.Load(int3(depthPixel, 0));\n"
        "    float4 sampleSurface = gSurface.Load(int3(p, 0));\n"
        "    float sampleEmissive = gEmissiveMask.Load(int3(p, 0));\n"
        "    float4 sampleIndirect = gIndirectMask.Load(int3(p, 0));\n"
        "    float4 sampleReflection = gReflectionColor.Load(int3(p, 0));\n"
        "    float depthDelta = abs(sampleDepth - centerDepth);\n"
        "    if (depthDelta < closestDepthDelta) {\n"
        "      closestDepthDelta = depthDelta; fallbackSurface = sampleSurface;\n"
        "      fallbackEmissive = sampleEmissive; fallbackIndirect = sampleIndirect;\n"
        "      fallbackReflection = sampleReflection;\n"
        "    }\n"
        "    float depthWeight = exp(-depthDelta / depthTolerance);\n"
        "    float weight = spatialWeight * depthWeight;\n"
        "    surfaceSum += sampleSurface * weight;\n"
        "    emissiveSum += sampleEmissive * weight;\n"
        "    indirectSum += sampleIndirect * weight;\n"
        "    reflectionSum += sampleReflection * weight;\n"
        "    totalWeight += weight;\n"
        "  }\n"
        "  if (totalWeight <= 1.0e-6) {\n"
        "    surface = fallbackSurface; emissive = fallbackEmissive;\n"
        "    indirect = fallbackIndirect; reflection = fallbackReflection;\n"
        "  } else {\n"
        "    float inverseWeight = rcp(totalWeight);\n"
        "    surface = surfaceSum * inverseWeight; emissive = emissiveSum * inverseWeight;\n"
        "    indirect = indirectSum * inverseWeight; reflection = reflectionSum * inverseWeight;\n"
        "  }\n"
        "}\n"
        "float4 PSMain(float4 position : SV_POSITION) : SV_TARGET {\n"
        "  uint maskWidth, maskHeight;\n"
        "  gSurface.GetDimensions(maskWidth, maskHeight);\n"
        "  float2 fullSize = max(gParams.yz, float2(1.0, 1.0));\n"
        // SV_POSITION already addresses the pixel center. Use the exact
        // pixel for the original scene so a no-signal composite is bit-stable
        // instead of shifting and filtering every raster pixel.
        "  float2 uv = position.xy / fullSize;\n"
        "  uint2 pixel = min(uint2(position.xy), uint2(fullSize) - 1);\n"
        "  float4 originalSample = gSceneColor.Load(int3(pixel, 0));\n"
        "  float3 originalEncoded = originalSample.rgb;\n"
        "  float depth = gDepth.Load(int3(pixel, 0));\n"
        "  float4 surface = 0.0; float emissive = 0.0;\n"
        "  float4 indirectSample = 0.0; float4 reflectionSample = 0.0;\n"
        "  SampleDepthGuided(uv, depth, fullSize, float2(maskWidth, maskHeight),\n"
        "                   surface, emissive, indirectSample, reflectionSample);\n"
        // DXR TRUTH MODE (V1) post-process bypass. Every Truth branch below
        // returns BEFORE lighting, reflection/GI compositing, bloom,
        // exposure, ACES/tonemap and color grading, so diagnostic colors can
        // never be reinterpreted as HDR radiance. OFF (0) never enters here.
        // Sources: gReflectionColor holds the raygen MaterialDebugColor
        // encoding forced per Truth view (see DxrTruthMaterialDebugMode);
        // loads are nearest mask-pixel (deterministic, no depth filtering).
        // Conventions:
        //   HIT_MISS (10): HIT=green, MISS=red. Background (depth>=1) is MISS.
        //     Encoding mode 4 (roughness) is grayscale, so any magenta pixel
        //     is unambiguously the RayGen MISS sentinel (1,0,1,1).
        //   WORLD_NORMAL (11): raw normalWS*0.5+0.5 passthrough. MISS=red
        //     (pure red is unreachable: it would need nx=+1,ny=-1,nz=-1 with
        //     |N|=1... verified below by exact-sentinel logic order),
        //     invalid (NaN/Inf/zero/absurd length)=blue. Normals are NOT
        //     fixed here; this view only observes them.
        //   HIT_DISTANCE (12): v=saturate(d/256) grayscale, 256 =
        //     kDxrTruthDistanceScale = gParams.y. MISS=red (pure red never
        //     occurs on the grayscale ramp). No auto-exposure/normalization:
        //     equal geometry gives equal pixels across frames.
        //   IDS (13): deterministic instance-hash passthrough. MISS=black
        //     (hash of id 0 is (0,.33,.67); black never occurs on a hit).
        //     Primitive index is NOT carried in ReflectionPayload, so only
        //     instance/material identity is observable without a payload
        //     change + DXIL regen (deferred, not blocking V1).
//   SYNTHETIC_LAMBERT (14): N from the same encoding as (11),
//     L = normalize(float3(1,-1,1)) is the direction FROM the surface
//     TOWARD the synthetic light in the same space as N,
//     lambert = saturate(dot(N,L)), output (l,l,l). Uses no OoT
//     lights, albedo, shadows, GI, exposure or tonemap. MISS=red
//     (unreachable on the grayscale ramp), invalid normal=blue.
//   GAME_LIGHT_LAMBERT (15, V2): identical math/input as (14) except
//     L = normalize(gGameLightDir), the real OoT/Fast3D probe direction
//     (TLAS space, surface->light convention, NO color/intensity/albedo).
//     gGameLightValid<0.5 (no probe directional in scene) yields a solid
//     CYAN sentinel (unreachable on the grayscale ramp).
        "  if (gDiagnosticView > 9.5) {\n"
        "    uint2 truthPixel = min(uint2(uv * float2(maskWidth, maskHeight)), uint2(maskWidth, maskHeight) - 1);\n"
        "    float4 truth = gReflectionColor.Load(int3(truthPixel, 0));\n"
        "    float truthDepth = gDepth.Load(int3(pixel, 0));\n"
        "    bool truthBad = any(isnan(truth)) || any(isinf(truth));\n"
        "    bool truthBackground = truthDepth >= 0.9999;\n"
        "    bool truthMiss = truth.r > 0.99 && truth.g < 0.01 && truth.b > 0.99;\n"
        // A cleared/aborted raygen pixel has no closest-hit record, even if
        // its zero RGB is different from the explicit miss sentinel.
        "    bool truthNoHit = truthBackground || truthMiss || truth.a < 0.5;\n"
        "    if (gDiagnosticView < 10.5) {\n"
        "      if (truthBad) return float4(1.0, 0.0, 1.0, 1.0);\n"
        "      return truthNoHit ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);\n"
        "    }\n"
        "    if (gDiagnosticView < 11.5) {\n"
        "      if (truthNoHit) return float4(1.0, 0.0, 0.0, 1.0);\n"
        "      if (truthBad) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      float3 truthN = truth.rgb * 2.0 - 1.0;\n"
        "      float truthLen = length(truthN);\n"
        "      if (!(truthLen > 1.0e-3) || truthLen > 4.0) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      return float4(truth.rgb, 1.0);\n"
        "    }\n"
        "    if (gDiagnosticView < 12.5) {\n"
        "      if (truthBad) return float4(1.0, 0.0, 1.0, 1.0);\n"
        "      if (truthNoHit) return float4(1.0, 0.0, 0.0, 1.0);\n"
        "      return float4(truth.rrr, 1.0);\n"
        "    }\n"
        "    if (gDiagnosticView < 13.5) {\n"
        "      if (truthBad) return float4(1.0, 0.0, 1.0, 1.0);\n"
        "      if (truthNoHit) return float4(0.0, 0.0, 0.0, 1.0);\n"
        "      return float4(truth.rgb, 1.0);\n"
        "    }\n"
        "    if (gDiagnosticView < 14.5) {\n"
        "      if (truthNoHit) return float4(1.0, 0.0, 0.0, 1.0);\n"
        "      if (truthBad) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      float3 lampN = truth.rgb * 2.0 - 1.0;\n"
        "      float lampLen = length(lampN);\n"
        "      if (!(lampLen > 1.0e-3) || lampLen > 4.0) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      float3 N = lampN / lampLen;\n"
        "      float3 L = normalize(float3(1.0, -1.0, 1.0));\n"
        "      float lambert = saturate(dot(N, L));\n"
        "      return float4(lambert.xxx, 1.0);\n"
        "    }\n"
        "    {\n"
        "      if (truthNoHit) return float4(1.0, 0.0, 0.0, 1.0);\n"
        "      if (truthBad) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      if (gGameLightValid < 0.5) return float4(0.0, 1.0, 1.0, 1.0);\n"
        "      float3 gameN = truth.rgb * 2.0 - 1.0;\n"
        "      float gameLen = length(gameN);\n"
        "      if (!(gameLen > 1.0e-3) || gameLen > 4.0) return float4(0.0, 0.0, 1.0, 1.0);\n"
        "      float3 gN = gameN / gameLen;\n"
        "      float3 gL = normalize(gGameLightDir);\n"
        "      float gameLambert = saturate(dot(gN, gL));\n"
        "      return float4(gameLambert.xxx, 1.0);\n"
        "    }\n"
        "  }\n"
        "  if (gDiagnosticView > 4.5) {\n"
        "    uint2 gatePixel = min(uint2(uv * float2(maskWidth, maskHeight)), uint2(maskWidth, maskHeight) - 1);\n"
        "    float4 gate = gReflectionColor.Load(int3(gatePixel, 0));\n"
        "    float4 writtenSurface = gSurface.Load(int3(gatePixel, 0));\n"
        "    uint flags = (uint)round(gate.r * 255.0); uint error = (uint)round(gate.g * 255.0);\n"
        "    if (error == 10u) return float4(0.04, 0.04, 0.04, 1.0);\n"
        "    if (error == 11u) return float4(0.0, 1.0, 1.0, 1.0);\n"
        "    if (error == 1u) return float4(0.0, 0.0, 0.0, 1.0);\n"
        "    if (error == 2u) return float4(1.0, 0.0, 0.0, 1.0);\n"
        "    if (error == 3u) return float4(0.8, 0.0, 0.0, 1.0);\n"
        "    if (error == 4u) return float4(0.6, 0.0, 0.0, 1.0);\n"
        "    if (error == 5u) return float4(1.0, 0.35, 0.0, 1.0);\n"
        "    if (error == 6u) return float4(1.0, 0.0, 1.0, 1.0);\n"
        "    if (error == 7u) return float4(0.8, 0.0, 1.0, 1.0);\n"
        "    if (error == 8u) return float4(0.6, 0.0, 1.0, 1.0);\n"
        "    if (error == 9u) return float4(1.0, 0.3, 1.0, 1.0);\n"
        "    if ((flags & 64u) != 0u) return writtenSurface.w > 0.0 ? float4(1.0, 1.0, 1.0, 1.0) : float4(1.0, 0.0, 1.0, 1.0);\n"
        "    if ((flags & 32u) != 0u) return float4(0.0, 1.0, 0.0, 1.0);\n"
        "    if ((flags & 16u) == 0u) return float4(1.0, 1.0, 0.0, 1.0);\n"
        "    return float4(0.0, 0.0, 1.0, 1.0);\n"
        "  }\n"
        "  if (gDiagnosticView > 0.5) {\n"
        "    if (depth >= 0.9999) return float4(0.0, 0.0, 0.0, 1.0);\n"
        "    if (DiagnosticInvalid(surface) || DiagnosticInvalid(indirectSample) ||\n"
        "        DiagnosticInvalid(reflectionSample) || isnan(emissive) || isinf(emissive))\n"
        "      return float4(1.0, 0.0, 1.0, 1.0);\n"
        "    if (gDiagnosticView < 1.5) return surface.w > 0.5 ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 1.0, 1.0);\n"
        "    if (gDiagnosticView < 2.5) return DiagnosticRadiance(reflectionSample);\n"
        "    if (gDiagnosticView < 3.5) return DiagnosticRadiance(indirectSample);\n"
        "    return any(surface > 0.0) ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);\n"
        "  }\n"
        // Untouched pixels retain the exact source encoding and alpha. The
        // FP16 scene already contains linear RGB, so decoding its sky twice
        // changes the image even when no DXR signal exists at that pixel.
        "  if (depth >= 0.9999) return originalSample;\n"
        "  float3 original = gSceneHdr > 0.5 ? originalEncoded : CompositeSrgbToLinear(originalEncoded);\n"
        // Keep the coverage policy consistent with RayGenReflection.
        // surface.z is material coverage written per pixel:
        //   opaque/cutout and transmissive hits follow their material policy;
        //   exact emissive blends use sampled alpha for emission and bloom;
        //   other raster blends, misses and background remain zero coverage.
        // surface.w is the validity flag: 1.0 for a normal-path DXR surface,
        // 0.0 for cleared pixels. A non-finite sample must never drive the
        // blend (saturate(NaN) is undefined); invalid signals fall back to
        // the raster pixel. Valid materials keep their full DXR contribution.
        "  float dxrSignalInvalid = (DiagnosticInvalid(surface) || DiagnosticInvalid(indirectSample) ||\n"
        "      DiagnosticInvalid(reflectionSample) || isnan(emissive) || isinf(emissive)) ? 1.0 : 0.0;\n"
        "  if (dxrSignalInvalid > 0.5) return originalSample;\n"
        // Alpha values above one mark a valid primary relight or path-radiance result.
        "  float pathSignal = step(1.5, indirectSample.a);\n"
        "  float rawCoverage = surface.z;\n"
        "  float coverageFinite = (!isnan(rawCoverage) && !isinf(rawCoverage)) ? 1.0 : 0.0;\n"
        "  float clippedCoverage = coverageFinite > 0.5 ? saturate(rawCoverage) : 0.0;\n"
        "  float surfaceValid = step(0.5, saturate(surface.w));\n"
        "  if (surfaceValid < 0.5 || clippedCoverage <= 0.0) return originalSample;\n"
        "  float pathTrace = pathSignal;\n"
        "  float pathWeight = pathSignal * clippedCoverage * surfaceValid;\n"
        // Ablation controls act on the same frame's signals, so every control
        // stays same-frame comparable. gAblation: 1 = optics off, 2 = indirect
        // off, 3 = relight off (the raster pixel stays authoritative).
        "  float relightEnabled = gAblation > 2.5 ? 0.0 : 1.0;\n"
        "  float indirectEnabled = (gAblation > 1.5 && gAblation < 2.5) ? 0.0 : 1.0;\n"
        "  pathWeight *= relightEnabled;\n"
        "  float3 traced = max(indirectSample.rgb, 0.0);\n"
        // surface.w is only a validity flag (its UAV is R8G8B8A8_UNORM, so a
        // share packed above one saturates). The indirect share of a relit
        // pixel is published in gIndirectMask.a as (2.0 + share) on a float16
        // target, and only relit pixels carry direct+GI in one signal; the
        // other path stores pure indirect radiance and is handled by the
        // additive term below.
        "  float giFraction = saturate(indirectSample.a - 2.0) * pathSignal;\n"
        "  traced = lerp(traced, traced * (1.0 - giFraction), 1.0 - indirectEnabled);\n"
        // Non-destructive relight. The raster pixel already carries the game's
        // albedo x illumination; the traced pixel carries albedo x the traced
        // transport. Replacing one with the other discards every light the scene
        // has whenever the trace cannot yet match the game's energy, which paints
        // surfaces black (observed: ~270x darker, 99.8% of relit pixels below
        // 60% of the raster reference). Let the trace claim only the
        // illumination it can actually account for: when its energy matches the
        // raster it takes over in full (correct shadows and GI apply), and while
        // it cannot, the game's own lighting survives instead of being replaced
        // by an under-energetic result. This is an energy bound, not a brightness
        // boost: nothing is ever added, only the sampled relight is limited to
        // the energy the pixel demonstrably had.
        "  float tracedLum = dot(traced, float3(0.2126, 0.7152, 0.0722));\n"
        "  float rasterLum = dot(original, float3(0.2126, 0.7152, 0.0722));\n"
        "  float credibility = rasterLum > 1.0e-6 ? saturate(tracedLum / rasterLum) : 1.0;\n"
        "  original = lerp(original, traced, pathWeight * credibility);\n"
        // Keep the R16F DXR radiance intact until the SDR composite shoulder;
        // saturating here discarded bright specular and emissive energy.
        // RunTonemappingPass owns ACES for both main and offscreen scenes.
        // Applying it here to individual SDR signals would map them twice.
        "  float3 reflectedColor = max(reflectionSample.rgb, 0.0);\n"
        "  float opticsEnabled = gAblation > 0.5 && gAblation < 1.5 ? 0.0 : 1.0;\n"
        "  float reflection = saturate(surface.r * gParams.x) * opticsEnabled;\n"
        "  float transmissionCoverage = saturate(surface.g);\n"
        "  float3 indirect = max(indirectSample.rgb, 0.0);\n"
        "  float transmissionBlend = transmissionCoverage * saturate(gParams.w) * opticsEnabled;\n"
        // surface.r already contains Fresnel and material reflectivity. Apply
        // it once; reweighting it with reflectionSample.a suppresses the signal.
        "  float reflectionBlend = transmissionCoverage > 0.0 ? 0.0 : reflection;\n"
        "  float3 color = lerp(original, reflectedColor, saturate(max(reflectionBlend, transmissionBlend)));\n"
        "  emissive = max(emissive, 0.0);\n"
        "  color += indirect * 0.45 * (1.0 - pathTrace) * indirectEnabled;\n"
        // RayGenReflection already includes emission strength in radiance.
        // Use the emissive mask as eligibility, not a second energy factor.
        "  color += reflectedColor * 0.60 * step(0.0001, emissive) * (1.0 - pathTrace);\n"
        "  float3 bloom = 0; float bloomWeight = 0;\n"
        "  [unroll] for (int by=-3; by<=3; ++by) [unroll] for (int bx=-3; bx<=3; ++bx) {\n"
        "    float2 buv=uv+float2(bx,by)/float2(maskWidth,maskHeight);\n"
        "    float w=1.0/(1.0+bx*bx+by*by);\n"
        "    float bloomEligible = step(0.0001, gEmissiveMask.SampleLevel(gSampler,buv,0));\n"
        "    bloom += gReflectionColor.SampleLevel(gSampler,buv,0).rgb * bloomEligible * w; bloomWeight+=w;\n"
        "  }\n"
        "  color += bloom * (0.45 / max(1.0,bloomWeight));\n"
        "  return float4(gSceneHdr > 0.5 ? color : saturate(CompositeLinearToSrgb(color)), 1.0);\n"
        "}\n";
    ComPtr<ID3DBlob> vsBlob, psBlob, vsError, psError;
    const HRESULT vsResult = D3DCompile(vs, strlen(vs), "SurfaceCompositeVS", nullptr, nullptr,
                                        "VSMain", "vs_5_0", 0, 0, &vsBlob, &vsError);
    const HRESULT psResult = D3DCompile(ps, strlen(ps), "SurfaceCompositePS", nullptr, nullptr,
                                        "PSMain", "ps_5_0", 0, 0, &psBlob, &psError);
    if (FAILED(vsResult) || FAILED(psResult)) {
        gLastInitHresult = FAILED(vsResult) ? vsResult : psResult;
        LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] CompositePipeline D3DCompile format=") + formatName +
            " shader=" + (FAILED(vsResult) ? "SurfaceCompositeVS" : "SurfaceCompositePS") +
            " hr=" + SurfaceHr(gLastInitHresult));
        if (vsError && vsError->GetBufferPointer()) {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][ERROR] Composite VS compile: " +
                               std::string(static_cast<const char*>(vsError->GetBufferPointer()),
                                           vsError->GetBufferSize()));
        }
        if (psError && psError->GetBufferPointer()) {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][ERROR] Composite PS compile: " +
                               std::string(static_cast<const char*>(psError->GetBufferPointer()),
                                           psError->GetBufferSize()));
        }
        return false;
    }

    ComPtr<ID3D12ShaderReflection> compositeReflection;
    hr = D3DReflect(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                    IID_PPV_ARGS(&compositeReflection));
    D3D12_SHADER_BUFFER_DESC compositeBufferDesc{};
    D3D12_SHADER_VARIABLE_DESC gameDirectionDesc{}, gameValidDesc{};
    bool layoutMatches = false;
    if (SUCCEEDED(hr) && compositeReflection) {
        auto* constants = compositeReflection->GetConstantBufferByName("SurfaceComposite");
        layoutMatches = constants && SUCCEEDED(constants->GetDesc(&compositeBufferDesc)) &&
            SUCCEEDED(constants->GetVariableByName("gGameLightDir")->GetDesc(&gameDirectionDesc)) &&
            SUCCEEDED(constants->GetVariableByName("gGameLightValid")->GetDesc(&gameValidDesc)) &&
            compositeBufferDesc.Size == sizeof(DxrSurfaceCompositeConstants) &&
            gameDirectionDesc.StartOffset == offsetof(DxrSurfaceCompositeConstants, gameLightDirection) &&
            gameDirectionDesc.Size == sizeof(DxrSurfaceCompositeConstants::gameLightDirection) &&
            gameValidDesc.StartOffset == offsetof(DxrSurfaceCompositeConstants, gameLightValid);
    }
    LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][COMPOSITE_CONSTANT_LAYOUT] format=") + formatName +
        " result=" + (layoutMatches ? "PASS" : "FAIL") +
        " rootDwords=" + std::to_string(kSurfaceCompositeRootDwords) +
        " reflectedBytes=" + std::to_string(compositeBufferDesc.Size) +
        " gameDirOffset=" + std::to_string(gameDirectionDesc.StartOffset) +
        " gameValidOffset=" + std::to_string(gameValidDesc.StartOffset));
    if (!layoutMatches) {
        gLastInitHresult = FAILED(hr) ? hr : E_INVALIDARG;
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = mCompositeRootSig.Get();
    pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = outputFormat;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    // The pass samples a copy of the scene and writes the resolved color;
    // additive blending would leave the original water/refraction signal
    // unavailable and would also tint UI pixels outside the material mask.
    pso.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mCompositePso[outputIndex]));
    if (FAILED(hr)) { gLastInitHresult = hr; LogCouchDxrSurface(std::string("[COUCH-DXR-SURFACE][INIT_OP] CompositePipeline CreateGraphicsPipelineState format=") + formatName + " VS=SurfaceCompositeVS PS=SurfaceCompositePS root=SurfaceCompositeRootSignature hr=" + SurfaceHr(hr)); return false; }
    return true;
}

void DxrSurfaceRenderer::UpdateCameraMatrices(const float view[4][4], const float proj[4][4], float clipXScale,
                                            const float viewport[4]) {
    memcpy(mViewMatrix, view, sizeof(mViewMatrix));
    memcpy(mProjMatrix, proj, sizeof(mProjMatrix));
    mClipXScale = (std::isfinite(clipXScale) && clipXScale > 1.0e-4f) ? clipXScale : 1.0f;
    const float fullViewport[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    memcpy(mRasterViewport, viewport ? viewport : fullViewport, sizeof(mRasterViewport));
    // Captured TLAS instances use the Fast3D model-view transform, so both
    // reconstructed points and the camera origin must be in view space.
    mMatricesValid = InvertSurfaceMatrix(proj, mInvProjectionMatrix);
    for (int axis = 0; axis < 4; ++axis) {
        mMatricesValid = mMatricesValid && std::isfinite(mRasterViewport[axis]);
        for (int col = 0; col < 4; ++col)
            mMatricesValid = mMatricesValid && std::isfinite(mInvProjectionMatrix[axis][col]);
    }
    mMatricesValid = mMatricesValid && mRasterViewport[2] > 1.0e-6f && mRasterViewport[3] > 1.0e-6f;
    if (!InvertSurfaceMatrix(view, mInvViewMatrix)) {
        memset(mInvViewMatrix, 0, sizeof(mInvViewMatrix));
        for (int i = 0; i < 4; ++i) mInvViewMatrix[i][i] = 1.0f;
    }
    mCameraPosition[0] = 0.0f;
    mCameraPosition[1] = 0.0f;
    mCameraPosition[2] = 0.0f;
    mCameraPosition[3] = 1.0f;

    static uint64_t cameraMatrixTraceFrame = 0;
    const char* matrixTrace = std::getenv("COUCH_DXR_MATRIX_TRACE");
    if (matrixTrace && strcmp(matrixTrace, "1") == 0 && cameraMatrixTraceFrame++ % 120 == 0) {
        const auto logMatrix = [](const char* name, const float matrix[4][4]) {
            std::string line = std::string("[COUCH-DXR-MATRIX][") + name + "]";
            for (int row = 0; row < 4; ++row) {
                line += " r" + std::to_string(row) + "=";
                for (int col = 0; col < 4; ++col) {
                    if (col) line += ",";
                    line += std::to_string(matrix[row][col]);
                }
            }
            LogCouchDxrSurface(line);
        };
        LogCouchDxrSurface("[COUCH-DXR-MATRIX] sample=" + std::to_string(cameraMatrixTraceFrame - 1) +
                           " clipXScale=" + std::to_string(mClipXScale));
        logMatrix("VIEW", view);
        logMatrix("PROJECTION", proj);
    }
}

void DxrSurfaceRenderer::RenderReflections(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                            ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                            ID3D12Resource* depthBuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                                            D3D12_GPU_VIRTUAL_ADDRESS tlasGpuVA,
                                           uint32_t width, uint32_t height,
                                           uint32_t frameIndex, uint64_t fenceValue,
                                            const DxrSurfaceFrameBuffers& surfaceData,
                                            ID3D12DescriptorHeap* rasterTextureHeap,
                                            uint32_t rasterTextureDescriptorSize,
                                           int diagnosticView,
                                           int diagnosticFrame,
                                           int sceneId,
                                           int roomId,
                                           uint32_t tlasInstanceCount) {
    (void)frameIndex;
    (void)fenceValue;
    const char* surfaceEnv = getenv("COUCH_DXR_SURFACES");
    const bool explicitSurfaceOn = surfaceEnv && strcmp(surfaceEnv, "1") == 0;
    const bool enhancedProfile = MaterialRegistry::IsEnhancedVisualProfile();
    const bool gateReady = mInitialized && mSupported && mEnabled &&
                           (explicitSurfaceOn || enhancedProfile);
    const bool dataBoundsValid = DxrSurfaceFrameBoundsValid(surfaceData);
    const bool frameReady = device && cmdList && renderTarget && depthBuffer &&
                            tlasGpuVA != 0 && width != 0 && height != 0 && mMatricesValid &&
                            dataBoundsValid;
    static int sLastGateState = -1;
    const int gateState = (gateReady ? 1 : 0) | (frameReady ? 2 : 0);
    if (gateState != sLastGateState) {
        LogCouchDxrSurface(
            "[COUCH-DXR-SURFACE][GATE] init=" + std::to_string(mInitialized ? 1 : 0) +
            " supported=" + std::to_string(mSupported ? 1 : 0) +
            " dataBoundsValid=" + std::to_string(dataBoundsValid ? 1 : 0) +
            " enabled=" + std::to_string(mEnabled ? 1 : 0) +
            " explicit=" + std::to_string(explicitSurfaceOn ? 1 : 0) +
            " enhanced=" + std::to_string(enhancedProfile ? 1 : 0) +
            " device=" + std::to_string(device ? 1 : 0) +
            " cmd=" + std::to_string(cmdList ? 1 : 0) +
            " target=" + std::to_string(renderTarget ? 1 : 0) +
            " depth=" + std::to_string(depthBuffer ? 1 : 0) +
            " tlas=" + std::to_string(tlasGpuVA != 0 ? 1 : 0) +
            " materialRecords=" + std::to_string(surfaceData.instanceCount) +
            " materialTextures=" + std::to_string(surfaceData.textureSrvIndices.size()) +
            " size=" + std::to_string(width) + "x" + std::to_string(height) +
            " matrices=" + std::to_string(mMatricesValid ? 1 : 0));
        sLastGateState = gateState;
    }
    if (!gateReady || !frameReady) return;
    DxrShadowRenderer& shadowRenderer = DxrShadowRenderer::Instance();
    ID3D12Resource* rasterSoftShadow = shadowRenderer.GetShadowMaskForTrace();
    auto rasterSoftShadowState = static_cast<D3D12_RESOURCE_STATES>(
        shadowRenderer.GetShadowMaskStateForTrace());
    uint32_t rasterSoftShadowWidth = shadowRenderer.GetMaskWidth();
    uint32_t rasterSoftShadowHeight = shadowRenderer.GetMaskHeight();
    bool rasterSoftShadowEnabled = rasterSoftShadow && shadowRenderer.IsEnabled() &&
        shadowRenderer.IsDispatchEnabled() && shadowRenderer.GetStats().rayDispatchIssued &&
        rasterSoftShadowState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    // The path pass should consume the same temporally reprojected penumbra
    // as the raster composite. Use the raw DXR mask only when temporal output
    // is disabled, unavailable, or not in its expected read state.
    TemporalManager& temporalManager = TemporalManager::Instance();
    ID3D12Resource* temporalSoftShadow = temporalManager.GetTemporalOutputBufferForTrace();
    if (rasterSoftShadowEnabled && temporalManager.IsEnabled() && temporalSoftShadow &&
        temporalManager.GetTemporalOutputBufferStateForTrace() ==
            static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
        rasterSoftShadow = temporalSoftShadow;
        rasterSoftShadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    // Surface classification and reflection are intentionally traced at half
    // resolution. The fullscreen pass reconstructs the signal over a copy of
    // the original scene, so UI and Fast3D color payloads stay untouched.
    const uint32_t launchWidth = RtxScaledExtent(width, mQualityScale);
    const uint32_t launchHeight = RtxScaledExtent(height, mQualityScale);
    const DXGI_FORMAT sceneFormat = renderTarget->GetDesc().Format;
    if (!EnsureResources(device, launchWidth, launchHeight, width, height, sceneFormat)) {
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][GATE] EnsureResources FAILED for " +
                           std::to_string(launchWidth) + "x" + std::to_string(launchHeight) +
                           " probe / " + std::to_string(width) + "x" + std::to_string(height) +
                           " scene");
        return;
    }

    SelectDxrDescriptorHeap(device, mDescriptorHeap, mDescriptorTemplate, mDescriptorFrames, frameIndex % 3);
    D3D12_CPU_DESCRIPTOR_HANDLE textureTableCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    textureTableCpu.ptr += kSurfaceTextureDescriptorBase * mDescriptorSize;
    D3D12_CPU_DESCRIPTOR_HANDLE nullTextureCpu = mDescriptorTemplate->GetCPUDescriptorHandleForHeapStart();
    nullTextureCpu.ptr += kSurfaceTextureDescriptorBase * mDescriptorSize;
    const D3D12_DESCRIPTOR_HEAP_DESC rasterHeapDesc = rasterTextureHeap ? rasterTextureHeap->GetDesc() : D3D12_DESCRIPTOR_HEAP_DESC{};
    for (size_t i = 0; i < std::min<size_t>(surfaceData.textureSrvIndices.size(),kDxrSurfaceTextureDescriptorCount-1); ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE destination = textureTableCpu;
        destination.ptr += (i + 1) * mDescriptorSize;
        const uint32_t sourceIndex = surfaceData.textureSrvIndices[i];
        if (rasterTextureHeap && rasterTextureDescriptorSize != 0 &&
            sourceIndex < rasterHeapDesc.NumDescriptors) {
            D3D12_CPU_DESCRIPTOR_HANDLE source = rasterTextureHeap->GetCPUDescriptorHandleForHeapStart();
            source.ptr += sourceIndex * rasterTextureDescriptorSize;
            device->CopyDescriptorsSimple(1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        } else {
            device->CopyDescriptorsSimple(1, destination, nullTextureCpu,
                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE start = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    device->CopyDescriptorsSimple(1, start, depthSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = start;
    uavCpu.ptr += mDescriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mReflectionMask.Get(), nullptr, &uav, uavCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE emissiveUavCpu = start;
    emissiveUavCpu.ptr += 2 * mDescriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC emissiveUav = {};
    emissiveUav.Format = mEmissiveFormat;
    emissiveUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mEmissiveMask.Get(), nullptr, &emissiveUav, emissiveUavCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE indirectUavCpu = start;
    indirectUavCpu.ptr += 3 * mDescriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC indirectUav = {};
    indirectUav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    indirectUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mIndirectMask.Get(), nullptr, &indirectUav, indirectUavCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE reflectionColorUavCpu = start;
    reflectionColorUavCpu.ptr += 4 * mDescriptorSize;
    D3D12_UNORDERED_ACCESS_VIEW_DESC reflectionColorUav = {};
    reflectionColorUav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    reflectionColorUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(mReflectionColor.Get(), nullptr, &reflectionColorUav,
                                       reflectionColorUavCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE surfaceSrvCpu = start;
    surfaceSrvCpu.ptr += kSurfaceSrvDescriptorBase * mDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC surfaceSrv = {};
    surfaceSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    surfaceSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    surfaceSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    surfaceSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(mReflectionMask.Get(), &surfaceSrv, surfaceSrvCpu);

    if (mConstantBufferMapped) {
        SurfaceFrameConstants fc = {};
        memcpy(fc.invProjection, mInvProjectionMatrix, sizeof(fc.invProjection));
        memcpy(fc.rasterViewport, mRasterViewport, sizeof(fc.rasterViewport));
        memcpy(fc.invView, mInvViewMatrix, sizeof(fc.invView));
        const SceneLightingSnapshot sceneLighting = MaterialRegistry::Instance().GetSceneLighting();
        // Report the captured native scene light consumed by DXR separately
        // from the Fast3D per-draw probe. Positional lights can be encoded as
        // actor-relative vectors in that probe, so it is not the global source.
        {
            const float* probe = sceneLighting.gameProbeWorldDirection;
            const float probeLen = sqrtf(probe[0] * probe[0] + probe[1] * probe[1] +
                                         probe[2] * probe[2]);
            char sig[256]{};
            snprintf(sig, sizeof(sig),
                     "probeValid=%d probeDir=(%.4f,%.4f,%.4f) probeLen=%.4f rawLen=%.4f "
                     "probeCount=%u pointFlushMax=%u snapCount=%u snapCaptured=%d",
                     sceneLighting.gameProbeValid ? 1 : 0, probe[0], probe[1], probe[2],
                     probeLen, sceneLighting.gameProbeRawLength,
                     sceneLighting.gameProbeDirectionalCount,
                     sceneLighting.gameProbePointFlushMax,
                     sceneLighting.directionalLightCount,
                     sceneLighting.capturedFromScene ? 1 : 0);
            static std::string lastGameLightSig;
            const bool gameViewActive = (diagnosticView == static_cast<int>(DxrTruthView::GameLightLambert));
            static bool gameViewWasActive = false;
            // This delta reports whether the diagnostic per-draw observation
            // agrees with the native scene source. Actor-local lights may
            // change it, so it is not a correctness gate.
            const float snapDx = probe[0] - sceneLighting.worldDirection[0];
            const float snapDy = probe[1] - sceneLighting.worldDirection[1];
            const float snapDz = probe[2] - sceneLighting.worldDirection[2];
            const float snapDelta =
                sqrtf(snapDx * snapDx + snapDy * snapDy + snapDz * snapDz);
            const bool snapMatch = sceneLighting.gameProbeValid && snapDelta < 1.0e-3f;
            if (sig != lastGameLightSig || (gameViewActive && !gameViewWasActive)) {
                lastGameLightSig = sig;
                LogCouchDxrSurface(
                    std::string("[COUCH-DXR-GAME-LIGHT] source=FAST3D_PER_DRAW_PROBE selectedForDXR=false direction=(") +
                    std::to_string(probe[0]) + "," + std::to_string(probe[1]) + "," +
                    std::to_string(probe[2]) + ") space=WORLD color=(" +
                    std::to_string(sceneLighting.gameProbeColor[0]) + "," +
                    std::to_string(sceneLighting.gameProbeColor[1]) + "," +
                    std::to_string(sceneLighting.gameProbeColor[2]) +
                    ") probeValid=" +
                    std::string(sceneLighting.gameProbeValid ? "true" : "false") + " " + sig +
                    " nativeSnapshotDir=(" + std::to_string(sceneLighting.worldDirection[0]) + "," +
                    std::to_string(sceneLighting.worldDirection[1]) + "," +
                    std::to_string(sceneLighting.worldDirection[2]) +
                    ") consumerSpace=TLAS(gGameLightDir,fc.reservedLight,fc.lightDir,"
                    "gCouchViewLightDirection) vectorConvention=SURFACE_TO_LIGHT" +
                    " truthDir=(" + std::to_string(probe[0]) + "," +
                    std::to_string(probe[1]) + "," + std::to_string(probe[2]) +
                    ") productiveDir=(" + std::to_string(sceneLighting.worldDirection[0]) + "," +
                    std::to_string(sceneLighting.worldDirection[1]) + "," +
                    std::to_string(sceneLighting.worldDirection[2]) + ") probeToNativeDelta=" +
                    std::to_string(snapDelta) + " probeMatchesNative=" +
                    std::string(snapMatch ? "YES" : "NO"));
                if (gameViewActive && !sceneLighting.gameProbeValid) {
                    LogCouchDxrSurface("[COUCH-DXR-GAME-LIGHT] "
                                       "GAME_LIGHT_LAMBERT_UNAVAILABLE=NO_DIRECTIONAL_LIGHT");
                }
            }
            gameViewWasActive = gameViewActive;
        }
        const float* worldLight = sceneLighting.worldDirection;
        float viewLight[3] = {
            worldLight[0] * mViewMatrix[0][0] + worldLight[1] * mViewMatrix[1][0] + worldLight[2] * mViewMatrix[2][0],
            worldLight[0] * mViewMatrix[0][1] + worldLight[1] * mViewMatrix[1][1] + worldLight[2] * mViewMatrix[2][1],
            worldLight[0] * mViewMatrix[0][2] + worldLight[1] * mViewMatrix[1][2] + worldLight[2] * mViewMatrix[2][2],
        };
        const float* secondaryWorldLight = sceneLighting.secondaryWorldDirection;
        float viewSecondaryLight[3] = {
            secondaryWorldLight[0] * mViewMatrix[0][0] + secondaryWorldLight[1] * mViewMatrix[1][0] +
                secondaryWorldLight[2] * mViewMatrix[2][0],
            secondaryWorldLight[0] * mViewMatrix[0][1] + secondaryWorldLight[1] * mViewMatrix[1][1] +
                secondaryWorldLight[2] * mViewMatrix[2][1],
            secondaryWorldLight[0] * mViewMatrix[0][2] + secondaryWorldLight[1] * mViewMatrix[1][2] +
                secondaryWorldLight[2] * mViewMatrix[2][2],
        };
        const float viewLightLength = sqrtf(viewLight[0] * viewLight[0] + viewLight[1] * viewLight[1] +
                                            viewLight[2] * viewLight[2]);
        if (viewLightLength > 1.0e-6f) {
            fc.lightDir[0] = viewLight[0] / viewLightLength;
            fc.lightDir[1] = viewLight[1] / viewLightLength;
            fc.lightDir[2] = viewLight[2] / viewLightLength;
        }
        if (const char* harness = std::getenv("COUCH_DXR_HARNESS"); harness && std::strcmp(harness, "1") == 0) {
            // The source and productive directions are the same authoritative
            // native snapshot. The per-draw probe is diagnostic only.
            const float gpuDx = fc.lightDir[0] - (viewLightLength > 1.0e-6f ? viewLight[0] / viewLightLength : 0.0f);
            const float gpuDy = fc.lightDir[1] - (viewLightLength > 1.0e-6f ? viewLight[1] / viewLightLength : 0.0f);
            const float gpuDz = fc.lightDir[2] - (viewLightLength > 1.0e-6f ? viewLight[2] / viewLightLength : 0.0f);
            char syncRecord[1400]{};
            std::snprintf(syncRecord, sizeof(syncRecord),
                "[COUCH-DXR-LIGHT-SYNC] frame=%d gameFrame=%llu renderFrame=%llu scene=0x%02X room=%d sourceCount=%u snapshotCount=%u sourceDir=(%.6f,%.6f,%.6f) truthDir=(%.6f,%.6f,%.6f) productiveDir=(%.6f,%.6f,%.6f) gpuDir=(%.6f,%.6f,%.6f) gpuDirSpace=VIEW sourceToProductiveDelta=%s productiveToGpuDelta=%.6f gpuEvidence=CPU_UPLOAD sameFrame=%d match=%s productiveGameFrame=%llu lightingFrame=%llu sourceRenderFrame=%llu productiveRenderFrame=%llu",
                diagnosticFrame,
                static_cast<unsigned long long>(sceneLighting.productiveGameFrame),
                static_cast<unsigned long long>(diagnosticFrame),
                sceneId, roomId, sceneLighting.directionalLightCount, sceneLighting.directionalLightCount,
                sceneLighting.worldDirection[0], sceneLighting.worldDirection[1],
                sceneLighting.worldDirection[2], sceneLighting.gameProbeWorldDirection[0],
                sceneLighting.gameProbeWorldDirection[1], sceneLighting.gameProbeWorldDirection[2],
                sceneLighting.worldDirection[0], sceneLighting.worldDirection[1], sceneLighting.worldDirection[2],
                fc.lightDir[0], fc.lightDir[1], fc.lightDir[2],
                "SAME_NATIVE_SNAPSHOT",
                std::sqrt(gpuDx * gpuDx + gpuDy * gpuDy + gpuDz * gpuDz),
                sceneLighting.capturedFromScene &&
                        sceneLighting.productiveRenderFrame == sceneLighting.renderFrame ? 1 : 0,
                sceneLighting.capturedFromScene &&
                        sceneLighting.productiveRenderFrame == sceneLighting.renderFrame &&
                        sceneLighting.directionalLightCount > 0 && viewLightLength > 1.0e-6f &&
                        std::sqrt(gpuDx * gpuDx + gpuDy * gpuDy + gpuDz * gpuDz) < 1.0e-6f
                    ? "YES" : "NO",
                static_cast<unsigned long long>(sceneLighting.productiveGameFrame),
                static_cast<unsigned long long>(sceneLighting.renderFrame),
                static_cast<unsigned long long>(sceneLighting.productiveRenderFrame),
                static_cast<unsigned long long>(sceneLighting.productiveRenderFrame));
            LogCouchDxrSurface(syncRecord);
        }
        const float viewSecondaryLightLength = sqrtf(viewSecondaryLight[0] * viewSecondaryLight[0] +
                                                     viewSecondaryLight[1] * viewSecondaryLight[1] +
                                                     viewSecondaryLight[2] * viewSecondaryLight[2]);
        if (viewSecondaryLightLength > 1.0e-6f) {
            fc.secondaryLightDirection[0] = viewSecondaryLight[0] / viewSecondaryLightLength;
            fc.secondaryLightDirection[1] = viewSecondaryLight[1] / viewSecondaryLightLength;
            fc.secondaryLightDirection[2] = viewSecondaryLight[2] / viewSecondaryLightLength;
        }
        fc.screenSize[0] = static_cast<float>(width);
        fc.screenSize[1] = static_cast<float>(height);
        fc.screenSize[2] = 1.0f / static_cast<float>(width);
        fc.screenSize[3] = 1.0f / static_cast<float>(height);
        fc.params[0] = 0.02f;
        fc.params[1] = kDxrTruthDistanceScale;
        // Fast3D scales clip-space X to retain its native 4:3 framing on wide
        // windows. Undo that scale before inverse projection so camera rays
        // intersect the exact triangles represented by raster depth.
        fc.params[2] = mClipXScale;
        // Full-scene path tracing replaces raster lighting only in the
        // experimental profile. HYBRID_RT may relight explicitly tagged pilot
        // surfaces; all other unsupported pixels retain Fast3D output.
        fc.params[3] = MaterialRegistry::IsPathTracedExperimentalProfile() ? 1.0f : 0.0f;
        auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
        auto traceDistance = [&](const char* name, float fallback) {
            const float value = cvars ? cvars->GetFloat(name, fallback) : fallback;
            return std::isfinite(value) ? std::clamp(value, 1.0f, 100000.0f) : fallback;
        };
        fc.traceSettings[0] = traceDistance("gEnhancements.Graphics.DXR.SurfaceTraceDistance", 10000.0f);
        fc.traceSettings[1] = fc.traceSettings[0];
        fc.traceSettings[2] = traceDistance("gEnhancements.Graphics.DXR.IndirectTraceDistance", 256.0f);
        const float reflectionStrength = cvars ? cvars->GetFloat("gEnhancements.Graphics.DXR.SurfaceReflection", 1.0f) : 1.0f;
        fc.traceSettings[3] = std::isfinite(reflectionStrength) ? std::clamp(reflectionStrength, 0.0f, 1.0f) : 1.0f;
        int materialDebugMode = cvars ? cvars->GetInteger(
            "gEnhancements.Graphics.DXR.MaterialDebugMode", 0) : 0;
        if (const char* debugEnv = getenv("COUCH_DXR_MATERIAL_DEBUG")) {
            char* end = nullptr;
            const long parsed = strtol(debugEnv, &end, 10);
            if (end != debugEnv && end && *end == '\0' && parsed >= 0 && parsed <= 17) {
                materialDebugMode = static_cast<int>(parsed);
            }
        }
        fc.lightDir[3] = static_cast<float>(std::clamp(materialDebugMode, 0, 17));
        // DXR Truth Mode (V1): force the raygen encoding each Truth view
        // decodes in the composite. Traversal, AnyHit filtering, gate flags,
        // counters and blend policy are untouched, so the primary ray behaves
        // exactly as OFF; only the UAV-recorded diagnostic color changes.
        // DxrTruthMaterialDebugMode never returns 1 (ray-variant bands), so
        // variant 0 (the OFF ray) is always used.
        const int truthDebugMode = DxrTruthMaterialDebugMode(diagnosticView);
        if (truthDebugMode >= 0) {
            materialDebugMode = truthDebugMode;
            fc.lightDir[3] = static_cast<float>(truthDebugMode);
        }
        static int lastLoggedDebugMode = -1;
        if (materialDebugMode != lastLoggedDebugMode) {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][DEBUG] materialMode=" +
                               std::to_string(std::clamp(materialDebugMode, 0, 17)) +
                               " clipXScale=" + std::to_string(mClipXScale) +
                               " (0=off, 1=class, 2=baseColor, 3=normal, 4=roughness, "
                               "5=metallic, 6=reflectivity, 7=hitDistance, "
                               "8=reflectionContribution, 9=instanceID, "
                               "10=radianceLum/giLum/baseLum/giFraction, "
                               "11=baseColor+giFraction, 12=radianceRGB+giLum, "
                               "13=metallic+baseLum)");
            lastLoggedDebugMode = materialDebugMode;
        }
        fc.quality[0] = static_cast<float>(mIndirectSamples);
        fc.quality[1] = cvars ? static_cast<float>(cvars->GetInteger("gEnhancements.Graphics.DXR.Reflections", 1)) : 1.0f;
        fc.quality[2] = cvars ? std::clamp(cvars->GetFloat(
            "gEnhancements.Graphics.DXR.Materials.ReliefStrength", 1.0f), 0.0f, 2.0f) : 1.0f;
        fc.quality[3] = static_cast<float>(sceneLighting.pointLightCount);
        for (int channel = 0; channel < 3; ++channel) {
            fc.sceneLightColor[channel] = sceneLighting.directionalColor[channel];
            fc.ambientLightColor[channel] = sceneLighting.ambientColor[channel];
            fc.secondaryLightColor[channel] = sceneLighting.secondaryDirectionalColor[channel];
        }
        fc.sceneLightColor[3] = rasterSoftShadowEnabled ? 1.0f : 0.0f;
        fc.ambientLightColor[3] = 1.0f;
        fc.frameInfo[0] = static_cast<float>(mSampleFrameIndex++);
        fc.frameInfo[1] = rasterSoftShadowEnabled ? static_cast<float>(rasterSoftShadowWidth) : 0.0f;
        fc.frameInfo[2] = rasterSoftShadowEnabled ? static_cast<float>(rasterSoftShadowHeight) : 0.0f;
        // MissReflection samples a copy of this same scene: only UNORM
        // sources need a display-to-linear conversion, never an FP16 source.
        fc.frameInfo[3] = sceneFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1.0f : 0.0f;
        // Record the light constants the raygen actually consumes, not just
        // the direction vectors: a zero directional colour, a missing ambient
        // term or a disabled soft-shadow mask all produce a relit pixel that
        // cannot match the raster energy. Emitted only when a value changes so
        // a scene's constants are logged once and identical frames stay free.
        if (const char* harness = std::getenv("COUCH_DXR_HARNESS"); harness && std::strcmp(harness, "1") == 0) {
            static char lastLightConstants[768]{};
            char lightConstants[768]{};
            std::snprintf(lightConstants, sizeof(lightConstants),
                "sceneLight=(%.6f,%.6f,%.6f) softShadowMask=%d ambient=(%.6f,%.6f,%.6f) "
                "secondary=(%.6f,%.6f,%.6f) viewLightDir=(%.6f,%.6f,%.6f) viewLightLength=%.6f "
                "directionalCount=%u pointCount=%u pathTracedProfile=%d diagnosticView=%d "
                "indirectSamples=%u reflections=%u relief=%.3f traceDistance=%.1f indirectDistance=%.1f",
                fc.sceneLightColor[0], fc.sceneLightColor[1], fc.sceneLightColor[2],
                rasterSoftShadowEnabled ? 1 : 0,
                fc.ambientLightColor[0], fc.ambientLightColor[1], fc.ambientLightColor[2],
                fc.secondaryLightColor[0], fc.secondaryLightColor[1], fc.secondaryLightColor[2],
                fc.lightDir[0], fc.lightDir[1], fc.lightDir[2], viewLightLength,
                sceneLighting.directionalLightCount, sceneLighting.pointLightCount,
                MaterialRegistry::IsPathTracedExperimentalProfile() ? 1 : 0,
                std::clamp(diagnosticView, 0, 31),
                static_cast<unsigned>(fc.quality[0]), static_cast<unsigned>(fc.quality[1]),
                fc.quality[2], fc.traceSettings[0], fc.traceSettings[2]);
            if (std::strcmp(lightConstants, lastLightConstants) != 0) {
                LogCouchDxrSurface("[COUCH-DXR-LIGHT-CONSTANTS] frame=" +
                                   std::to_string(diagnosticFrame) + " " + lightConstants);
                std::snprintf(lastLightConstants, sizeof(lastLightConstants), "%s", lightConstants);
            }
        }
        const bool forceCoverageEnv = [] {
            const char* value = getenv("COUCH_DXR_FORCE_ANY_HIT_COVERAGE");
            return value && strcmp(value, "1") == 0;
        }();
        const bool gateDiagnosticEnv = [] {
            const char* value = getenv("COUCH_DXR_GATE_DIAGNOSTIC");
            return value && strcmp(value, "1") == 0;
        }();
        // Truth views (10..15) must NOT enter the gate-diagnostic path: gate
        // mode changes AnyHit filtering and enables counters, while Truth
        // only re-encodes the OFF-identical trace for composite display.
        const bool isGateView = diagnosticView >= 5 && diagnosticView <= 8;
        const bool gateDiagnostic = gateDiagnosticEnv || isGateView || forceCoverageEnv;
        const bool forceAnyHitCoverage = forceCoverageEnv;
    const bool candidateAlphaPolicy = diagnosticView == 8;
    const bool bypassPrimaryRasterBlend = diagnosticView == 6 || diagnosticView == 7;
    const bool primaryGateCounterMode = mPrimaryGateCountersEnabled &&
                                             (diagnosticView == 5 || diagnosticView == 6 || diagnosticView == 7 || diagnosticView == 8);
    const bool alphaForensicSamples = primaryGateCounterMode;
    const bool bypassVertexAlpha = diagnosticView == 7;
        fc.diagnosticInfo[0] = (gateDiagnostic ? 1.0f : 0.0f) +
                               (forceAnyHitCoverage ? 2.0f : 0.0f) +
                               (primaryGateCounterMode ? 4.0f : 0.0f) +
                               (bypassPrimaryRasterBlend ? 8.0f : 0.0f) +
                               (alphaForensicSamples ? 32.0f : 0.0f) +
                               (bypassVertexAlpha ? 64.0f : 0.0f) +
                               (candidateAlphaPolicy ? 128.0f : 0.0f);
        fc.diagnosticInfo[1] = static_cast<float>(surfaceData.instanceCount);
        fc.diagnosticInfo[2] = static_cast<float>(surfaceData.textureSrvIndices.size() + 1);
        fc.diagnosticInfo[3] = static_cast<float>(diagnosticView);
        fc.diagnosticBounds[0] = surfaceData.vertexCount;
        fc.diagnosticBounds[1] = surfaceData.indexCount;
        fc.diagnosticBounds[2] = surfaceData.instanceCount;
        fc.diagnosticBounds[3] = static_cast<uint32_t>(surfaceData.textureSrvIndices.size() + 1);
        for (uint32_t lightIndex = 0; lightIndex < 32; ++lightIndex) {
            const float* worldPosition = sceneLighting.pointPosition[lightIndex];
            fc.pointLightPosition[lightIndex][0] = worldPosition[0] * mViewMatrix[0][0] +
                                                   worldPosition[1] * mViewMatrix[1][0] +
                                                   worldPosition[2] * mViewMatrix[2][0] + mViewMatrix[3][0];
            fc.pointLightPosition[lightIndex][1] = worldPosition[0] * mViewMatrix[0][1] +
                                                   worldPosition[1] * mViewMatrix[1][1] +
                                                   worldPosition[2] * mViewMatrix[2][1] + mViewMatrix[3][1];
            fc.pointLightPosition[lightIndex][2] = worldPosition[0] * mViewMatrix[0][2] +
                                                   worldPosition[1] * mViewMatrix[1][2] +
                                                   worldPosition[2] * mViewMatrix[2][2] + mViewMatrix[3][2];
            fc.pointLightPosition[lightIndex][3] = worldPosition[3]; // authored influence radius
            memcpy(fc.pointLightOptics[lightIndex], sceneLighting.pointOptics[lightIndex],
                   sizeof(fc.pointLightOptics[lightIndex]));
        }
        memcpy(fc.projection, mProjMatrix, sizeof(fc.projection));
    memcpy(mConstantBufferMapped + (frameIndex % 3) * kSurfaceFrameStride, &fc, sizeof(fc));
    }

    const bool counterSample = mPrimaryGateCountersEnabled &&
        (diagnosticView == 5 || diagnosticView == 6 || diagnosticView == 7 || diagnosticView == 8);
    if (counterSample && mPrimaryGateReadbackPending) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][READBACK_FAILED] reason=previous_sample_still_pending mode=" +
                           std::string(diagnosticView == 5 ? "NORMAL" : diagnosticView == 6 ? "BYPASS_PRIMARY_RASTER_BLEND_ONLY" : diagnosticView == 7 ? "BYPASS_PRIMARY_RASTER_BLEND_PLUS_VERTEX_ALPHA" : "CANDIDATE_ALPHA_POLICY"));
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> cmdList4;
    if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

    D3D12_RESOURCE_BARRIER pre[5] = {};
    pre[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[0].Transition.pResource = depthBuffer;
    pre[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    pre[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    pre[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[1].Transition.pResource = mReflectionMask.Get();
    pre[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    pre[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[2].Transition.pResource = mEmissiveMask.Get();
    pre[2].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    pre[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[3].Transition.pResource = mIndirectMask.Get();
    pre[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    pre[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pre[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[4].Transition.pResource = mReflectionColor.Get();
    pre[4].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    pre[4].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(5, pre);
    mMaskTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mReflectionColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mEmissiveTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mIndirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { mDescriptorHeap.Get() };
    cmdList4->SetDescriptorHeaps(1, heaps);
    cmdList4->SetComputeRootSignature(mGlobalRootSignature.Get());
    cmdList4->SetComputeRootShaderResourceView(0, tlasGpuVA);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList4->SetComputeRootDescriptorTable(1, gpu);
    D3D12_GPU_DESCRIPTOR_HANDLE outputGpu = gpu;
    outputGpu.ptr += mDescriptorSize;
    cmdList4->SetComputeRootDescriptorTable(2, outputGpu);
    cmdList4->SetComputeRootShaderResourceView(3, surfaceData.vertexData->GetGPUVirtualAddress());
    cmdList4->SetComputeRootShaderResourceView(4, surfaceData.indexData->GetGPUVirtualAddress());
    cmdList4->SetComputeRootShaderResourceView(5, surfaceData.instanceData->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE materialTextureGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    materialTextureGpu.ptr += kSurfaceTextureDescriptorBase * mDescriptorSize;
    cmdList4->SetComputeRootDescriptorTable(6, materialTextureGpu);
    cmdList4->SetComputeRootConstantBufferView(7, mConstantBuffer->GetGPUVirtualAddress() +
                                                     (frameIndex % 3) * kSurfaceFrameStride);
    D3D12_CPU_DESCRIPTOR_HANDLE environmentSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    environmentSrvCpu.ptr += kSurfaceEnvironmentDescriptorBase * mDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC environmentSrv = {};
    environmentSrv.Format = renderTarget->GetDesc().Format;
    environmentSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    environmentSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    environmentSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(renderTarget, &environmentSrv, environmentSrvCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE softShadowSrvCpu = environmentSrvCpu;
    softShadowSrvCpu.ptr += mDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC softShadowSrv = {};
    softShadowSrv.Format = DXGI_FORMAT_R8_UNORM;
    softShadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    softShadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    softShadowSrv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(rasterSoftShadowEnabled ? rasterSoftShadow : nullptr,
                                     &softShadowSrv, softShadowSrvCpu);
    D3D12_GPU_DESCRIPTOR_HANDLE environmentSrvGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    environmentSrvGpu.ptr += kSurfaceEnvironmentDescriptorBase * mDescriptorSize;
    cmdList4->SetComputeRootDescriptorTable(8, environmentSrvGpu);
    static int lastRasterSoftShadowState = -1;
    const int rasterSoftShadowStateForLog = rasterSoftShadowEnabled ? 1 : 0;
    if (rasterSoftShadowStateForLog != lastRasterSoftShadowState) {
        lastRasterSoftShadowState = rasterSoftShadowStateForLog;
        if (rasterSoftShadowEnabled) {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE] primary path lighting uses soft-shadow mask=" +
                               std::to_string(rasterSoftShadowWidth) + "x" +
                               std::to_string(rasterSoftShadowHeight) +
                               (temporalSoftShadow == rasterSoftShadow
                                    ? " (temporally reprojected)" : " (current DXR sample)"));
        } else {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE] soft-shadow mask unavailable; primary visibility uses fallback rays");
        }
    }
    cmdList4->SetPipelineState1(mStateObject.Get());

    D3D12_DISPATCH_RAYS_DESC dispatch = {};
    dispatch.RayGenerationShaderRecord.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mRayGenRecordOffset;
    dispatch.RayGenerationShaderRecord.SizeInBytes = mShaderRecordSize;
    dispatch.MissShaderTable.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mMissRecordOffset;
    dispatch.MissShaderTable.SizeInBytes = mShaderRecordSize;
    dispatch.MissShaderTable.StrideInBytes = mShaderRecordSize;
    dispatch.HitGroupTable.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mHitGroupRecordOffset;
    dispatch.HitGroupTable.SizeInBytes = mShaderRecordSize;
    dispatch.HitGroupTable.StrideInBytes = mShaderRecordSize;
    dispatch.Width = launchWidth;
    dispatch.Height = launchHeight;
    dispatch.Depth = 1;
    D3D12_RESOURCE_BARRIER rayInputsToRead[2] = {};
    rayInputsToRead[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    rayInputsToRead[0].Transition.pResource = renderTarget;
    rayInputsToRead[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    rayInputsToRead[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rayInputsToRead[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    UINT rayInputBarrierCount = 1;
    if (rasterSoftShadowEnabled) {
        rayInputsToRead[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rayInputsToRead[1].Transition.pResource = rasterSoftShadow;
        rayInputsToRead[1].Transition.StateBefore = rasterSoftShadowState;
        rayInputsToRead[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        rayInputsToRead[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rayInputBarrierCount = 2;
    }
    cmdList->ResourceBarrier(rayInputBarrierCount, rayInputsToRead);
    if (counterSample) {
        const UINT clearValues[4] = { 0, 0, 0, 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE counterGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        counterGpu.ptr += kPrimaryGateCounterDescriptor * mDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE counterCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        counterCpu.ptr += kPrimaryGateCounterDescriptor * mDescriptorSize;
        cmdList->ClearUnorderedAccessViewUint(counterGpu, counterCpu, mPrimaryGateCounters.Get(), clearValues, 0, nullptr);
        D3D12_RESOURCE_BARRIER clearToRay = {};
        clearToRay.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearToRay.UAV.pResource = mPrimaryGateCounters.Get();
        cmdList->ResourceBarrier(1, &clearToRay);
        LogCouchDxrSurface("[COUCH-DXR-GATE] COUNTERS_CLEARED mode=" +
                           std::string(diagnosticView == 5 ? "NORMAL" : diagnosticView == 6 ? "BYPASS_PRIMARY_RASTER_BLEND_ONLY" : diagnosticView == 7 ? "BYPASS_PRIMARY_RASTER_BLEND_PLUS_VERTEX_ALPHA" : "CANDIDATE_ALPHA_POLICY") +
                           " frame=" + std::to_string(diagnosticFrame));
    }
    cmdList4->DispatchRays(&dispatch);
    if (counterSample) {
        D3D12_RESOURCE_BARRIER counterUavBarrier = {};
        counterUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        counterUavBarrier.UAV.pResource = mPrimaryGateCounters.Get();
        cmdList->ResourceBarrier(1, &counterUavBarrier);
        D3D12_RESOURCE_BARRIER counterToCopy = {};
        counterToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        counterToCopy.Transition.pResource = mPrimaryGateCounters.Get();
        counterToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        counterToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        counterToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &counterToCopy);
        cmdList->CopyBufferRegion(mPrimaryGateReadback.Get(), 0, mPrimaryGateCounters.Get(), 0,
                                  kPrimaryGateCounterBytes);
        counterToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        counterToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmdList->ResourceBarrier(1, &counterToCopy);
        mPrimaryGateReadbackPending = true;
        mPrimaryGatePendingFrame = diagnosticFrame;
        mPrimaryGatePendingScene = sceneId;
        mPrimaryGatePendingRoom = roomId;
        mPrimaryGatePendingTlasInstances = tlasInstanceCount;
        mPrimaryGatePendingWidth = launchWidth;
        mPrimaryGatePendingHeight = launchHeight;
        mPrimaryGatePendingViewHash = SurfaceMatrixHash(mViewMatrix);
        mPrimaryGatePendingProjectionHash = SurfaceMatrixHash(mProjMatrix);
        mPrimaryGatePendingDepthIdentity = reinterpret_cast<uintptr_t>(depthBuffer);
        mPrimaryGatePendingBypassBlend = diagnosticView == 6 || diagnosticView == 7;
        mPrimaryGatePendingBypassVertexAlpha = diagnosticView == 7;
        mPrimaryGatePendingCandidatePolicy = diagnosticView == 8;
        mPrimaryGatePendingMaterialRecords = surfaceData.materialRecords;
        LogCouchDxrSurface("[COUCH-DXR-GATE] COUNTER_READBACK_QUEUED mode=" +
            std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)) +
            " frame=" + std::to_string(diagnosticFrame) + " scene=" +
            std::to_string(sceneId) + " room=" + std::to_string(roomId) +
            " dispatch=" + std::to_string(launchWidth) + "x" + std::to_string(launchHeight) +
            " tlasInstances=" + std::to_string(tlasInstanceCount) +
            " materialRecords=" + std::to_string(surfaceData.instanceCount) +
            " vertexCount=" + std::to_string(surfaceData.vertexCount) +
            " indexCount=" + std::to_string(surfaceData.indexCount) +
            " viewHash=" + std::to_string(mPrimaryGatePendingViewHash) +
            " projectionHash=" + std::to_string(mPrimaryGatePendingProjectionHash) +
            " depthResource=" + std::to_string(mPrimaryGatePendingDepthIdentity));
    }
    D3D12_RESOURCE_BARRIER rayInputsRestore[2] = {};
    rayInputsRestore[0] = rayInputsToRead[0];
    rayInputsRestore[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    rayInputsRestore[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (rasterSoftShadowEnabled) {
        rayInputsRestore[1] = rayInputsToRead[1];
        rayInputsRestore[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        rayInputsRestore[1].Transition.StateAfter = rasterSoftShadowState;
    }
    cmdList->ResourceBarrier(rayInputBarrierCount, rayInputsRestore);
    mStats.dispatchRaysCalls++;
    // Each launch performs a material probe, reflection work and indirect
    // samples; every indirect sample now includes its bounded second bounce.
    const uint64_t raysPerPixel = 3u + 2u * mIndirectSamples;
    mStats.approxRaysDispatched += static_cast<uint64_t>(launchWidth) * launchHeight * raysPerPixel;
    mStats.tlasBound = true;
    mStats.depthSrvBound = true;
    mStats.emissiveMaskBound = true;
    mStats.indirectMaskBound = true;
    mStats.reflectionColorBound = true;
    mStats.dispatchActive = true;

    D3D12_RESOURCE_BARRIER post[9] = {};
    post[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[0].UAV.pResource = mReflectionMask.Get();
    post[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[1].UAV.pResource = mEmissiveMask.Get();
    post[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[2].UAV.pResource = mIndirectMask.Get();
    post[3].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    post[3].UAV.pResource = mReflectionColor.Get();
    post[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[4].Transition.pResource = mReflectionMask.Get();
    post[4].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[4].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[5].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[5].Transition.pResource = mEmissiveMask.Get();
    post[5].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[5].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[5].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[6].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[6].Transition.pResource = mIndirectMask.Get();
    post[6].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[6].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[6].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[7].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    post[7].Transition.pResource = mReflectionColor.Get();
    post[7].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    post[7].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    post[7].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    post[8] = pre[0];
    post[8].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    post[8].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cmdList->ResourceBarrier(9, post);
    ++mStats.signalOutputsProduced;
    mMaskTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mReflectionColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mEmissiveTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mIndirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Preserve the scene before the water/reflection composite. Reading the
    // swapchain resource while writing it would be an illegal D3D12 alias.
    D3D12_RESOURCE_BARRIER copyPre[2] = {};
    UINT copyPreCount = 0;
    copyPre[copyPreCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyPre[copyPreCount].Transition.pResource = renderTarget;
    copyPre[copyPreCount].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    copyPre[copyPreCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyPre[copyPreCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ++copyPreCount;
    if (mSceneColorTrackedState != static_cast<unsigned>(D3D12_RESOURCE_STATE_COPY_DEST)) {
        copyPre[copyPreCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyPre[copyPreCount].Transition.pResource = mSceneColorCopy.Get();
        copyPre[copyPreCount].Transition.StateBefore =
            static_cast<D3D12_RESOURCE_STATES>(mSceneColorTrackedState);
        copyPre[copyPreCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        copyPre[copyPreCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++copyPreCount;
    }
    cmdList->ResourceBarrier(copyPreCount, copyPre);
    cmdList->CopyResource(mSceneColorCopy.Get(), renderTarget);

    D3D12_RESOURCE_BARRIER copyPost[2] = {};
    copyPost[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyPost[0].Transition.pResource = renderTarget;
    copyPost[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyPost[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    copyPost[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    copyPost[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyPost[1].Transition.pResource = mSceneColorCopy.Get();
    copyPost[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    copyPost[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyPost[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(2, copyPost);
    mSceneColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // The DXR surface probes remain active for diagnostics and material
    // validation, but the fullscreen water/reflection composite is opt-in.
    // A bad surface signal must never be allowed to resample the complete
    // Fast3D scene and turn ordinary walls/floors into stretched bands.  The
    // depth resource is already restored by `post[8]`, and the render target
    // is back in RENDER_TARGET state, so returning here leaves the raster
    // image intact while preserving DXR shadows/TLAS and surface dispatch.
    // OCZ-RTX-REPAIR REPAIR-02: the fullscreen composite is part of the DXR
    // profiles, not an opt-in overlay. HYBRID_RT and PATH_TRACED_EXPERIMENTAL
    // (both covered by IsHybridRtProfile) composite by default so valid
    // shadow/reflection/indirect signals reach the presented framebuffer.
    // An explicit COUCH_DXR_SURFACE_COMPOSITE=0 keeps the user's opt-out;
    // an explicit =1 can never enable the composite on Classic/Enhanced
    // raster profiles (it only confirms the default on DXR profiles).
    // The probes above already dispatched, so returning here still leaves the
    // raster image intact while preserving DXR shadows/TLAS and dispatches.
    const char* compositeEnv = getenv("COUCH_DXR_SURFACE_COMPOSITE");
    const bool compositeExplicitOn = compositeEnv && strcmp(compositeEnv, "1") == 0;
    const bool compositeExplicitOff = compositeEnv && strcmp(compositeEnv, "0") == 0;
    const bool dxrCompositeProfile = MaterialRegistry::IsHybridRtProfile();
    const bool compositeEnabled = dxrCompositeProfile && !compositeExplicitOff;
    if (!compositeEnabled) {
        mPathHistoryValid = false;
        static bool loggedCompositeGate = false;
        if (!loggedCompositeGate) {
            LogCouchDxrSurface(
                "[COUCH-DXR-SURFACE] composite=OFF profile='" +
                std::string(MaterialRegistry::Instance().GetProfileName()) + "' env=" +
                std::string(compositeEnv ? compositeEnv : "<unset>") +
                "; DXR profiles composite by default, COUCH_DXR_SURFACE_COMPOSITE=0 opts out");
            loggedCompositeGate = true;
        }
        if (compositeExplicitOn && !dxrCompositeProfile) {
            static bool loggedCompositeConflict = false;
            if (!loggedCompositeConflict) {
                LogCouchDxrSurface(
                    "[COUCH-DXR-SURFACE] COUCH_DXR_SURFACE_COMPOSITE=1 ignored on raster profile '" +
                    std::string(MaterialRegistry::Instance().GetProfileName()) +
                    "'; select Hybrid RT to composite DXR signals");
                loggedCompositeConflict = true;
            }
        }
        return;
    }
    // OCZ-RTX-REPAIR REPAIR-02: frame coherence. The composite resamples the
    // current frame's render target, depth, and DXR masks. A dimension or
    // resource mismatch must skip the fullscreen draw (raster stays intact)
    // instead of stretching stale descriptors across the scene.
    {
        const auto targetDesc = renderTarget ? renderTarget->GetDesc() : D3D12_RESOURCE_DESC{};
        const auto depthDesc = depthBuffer ? depthBuffer->GetDesc() : D3D12_RESOURCE_DESC{};
        const bool dimsMatch = renderTarget && depthBuffer &&
            static_cast<uint32_t>(targetDesc.Width) == width &&
            targetDesc.Height == height &&
            static_cast<uint32_t>(depthDesc.Width) == width &&
            depthDesc.Height == height;
        if (!dimsMatch) {
            mPathHistoryValid = false;
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][FRAME_MISMATCH] composite skipped: target=" +
                               std::to_string(renderTarget ? targetDesc.Width : 0) + "x" +
                               std::to_string(renderTarget ? targetDesc.Height : 0) + " depth=" +
                               std::to_string(depthBuffer ? depthDesc.Width : 0) + "x" +
                               std::to_string(depthBuffer ? depthDesc.Height : 0) + " expected=" +
                               std::to_string(width) + "x" + std::to_string(height));
            return;
        }
    }

    if (diagnosticView != 0) mPathHistoryValid = false;
    const bool pathTemporalApplied = diagnosticView == 0 && DispatchPathTemporal(
        cmdList, depthBuffer, depthSrvCpuHandle, launchWidth, launchHeight,
        width, height, frameIndex);
    if (!pathTemporalApplied) {
        // The descriptor ring is per in-flight frame; always point this
        // composite slot at the raw signal when temporal path tracing is off.
        SelectDxrDescriptorHeap(device, mDescriptorHeap, mDescriptorTemplate,
                                mDescriptorFrames, frameIndex % 3);
        D3D12_SHADER_RESOURCE_VIEW_DESC rawIndirectSrv = {};
        rawIndirectSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rawIndirectSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        rawIndirectSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rawIndirectSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE rawIndirectCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        rawIndirectCpu.ptr += (kSurfaceSrvDescriptorBase + 3) * mDescriptorSize;
        device->CreateShaderResourceView(mIndirectMask.Get(), &rawIndirectSrv, rawIndirectCpu);
        D3D12_CPU_DESCRIPTOR_HANDLE rawReflectionCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        rawReflectionCpu.ptr += (kSurfaceSrvDescriptorBase + 4) * mDescriptorSize;
        device->CreateShaderResourceView(mReflectionColor.Get(), &rawIndirectSrv, rawReflectionCpu);
        ID3D12DescriptorHeap* rawSurfaceHeap[] = { mDescriptorHeap.Get() };
        cmdList->SetDescriptorHeaps(1, rawSurfaceHeap);
    }

    // The composite samples depth in the pixel shader for a conservative
    // screen-space absorption estimate. Restore DEPTH_WRITE immediately after
    // the fullscreen draw so the next Fast3D frame sees the normal state.
    D3D12_RESOURCE_BARRIER depthRead = {};
    depthRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    depthRead.Transition.pResource = depthBuffer;
    depthRead.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthRead.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    depthRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &depthRead);

    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    D3D12_VIEWPORT viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->SetGraphicsRootSignature(mCompositeRootSig.Get());
    // OCZ-RTX-REPAIR REPAIR-02: the compute dispatches above (path temporal,
    // ray passes) leave an unrelated heap bound. The fullscreen tables below
    // address mDescriptorHeap slots, so bind it on every path -- not only
    // when the temporal pass is off -- or the composite samples stale
    // descriptors from the previous heap.
    ID3D12DescriptorHeap* compositeHeaps[] = { mDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, compositeHeaps);
    D3D12_GPU_DESCRIPTOR_HANDLE descriptorBase = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu = descriptorBase;
    sceneSrvGpu.ptr += (kSurfaceSrvDescriptorBase + 1) * mDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpu = descriptorBase;
    D3D12_GPU_DESCRIPTOR_HANDLE surfaceSrvGpu = descriptorBase;
    surfaceSrvGpu.ptr += kSurfaceSrvDescriptorBase * mDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE emissiveSrvGpu = descriptorBase;
    emissiveSrvGpu.ptr += (kSurfaceSrvDescriptorBase + 2) * mDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE indirectSrvGpu = descriptorBase;
    indirectSrvGpu.ptr += (kSurfaceSrvDescriptorBase + 3) * mDescriptorSize;
    D3D12_GPU_DESCRIPTOR_HANDLE reflectionColorSrvGpu = descriptorBase;
    reflectionColorSrvGpu.ptr += (kSurfaceSrvDescriptorBase + 4) * mDescriptorSize;
    cmdList->SetGraphicsRootDescriptorTable(0, sceneSrvGpu);
    cmdList->SetGraphicsRootDescriptorTable(1, depthSrvGpu);
    cmdList->SetGraphicsRootDescriptorTable(2, surfaceSrvGpu);
    cmdList->SetGraphicsRootDescriptorTable(3, emissiveSrvGpu);
    cmdList->SetGraphicsRootDescriptorTable(4, indirectSrvGpu);
    cmdList->SetGraphicsRootDescriptorTable(5, reflectionColorSrvGpu);
    float reflectionScale = 1.0f;
    float transmissionScale = 0.65f;
    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        reflectionScale = std::clamp(cvars->GetFloat(
            "gEnhancements.Graphics.DXR.SurfaceReflection", 1.0f), 0.0f, 1.0f);
        transmissionScale = std::clamp(cvars->GetFloat(
            "gEnhancements.Graphics.DXR.SurfaceTransmission", 0.65f), 0.0f, 1.0f);
    }
    if (!std::isfinite(reflectionScale)) reflectionScale = 1.0f;
    if (!std::isfinite(transmissionScale)) transmissionScale = 0.65f;
    const bool hdrScene = sceneFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const char* gateDiagnosticEnv = getenv("COUCH_DXR_GATE_DIAGNOSTIC");
    const char* forceCoverageEnv = getenv("COUCH_DXR_FORCE_ANY_HIT_COVERAGE");
    const int environmentDiagnosticView = forceCoverageEnv && strcmp(forceCoverageEnv, "1") == 0
        ? 6 : gateDiagnosticEnv && strcmp(gateDiagnosticEnv, "1") == 0 ? 5 : 0;
    const int requestedView = std::max(diagnosticView, environmentDiagnosticView);
    // Truth views (10..15) pass through unclamped; legacy views keep 0..8.
    const int boundedDiagnosticView = IsDxrTruthView(requestedView)
        ? requestedView : std::clamp(requestedView, 0, 8);
    static int lastDiagnosticView = -1;
    if (boundedDiagnosticView != lastDiagnosticView) {
        // V2 A/B evidence: same scene/room/camera across views is checked via
        // viewHash/projectionHash + instance counts on every view transition.
        const SceneLightingSnapshot viewLight = MaterialRegistry::Instance().GetSceneLighting();
        LogCouchDxrSurface("[COUCH-DXR-SURFACE][FORENSIC] diagnosticView=" +
                           std::to_string(boundedDiagnosticView) +
                           (IsDxrTruthView(boundedDiagnosticView)
                                ? std::string(" ") + DxrTruthViewName(boundedDiagnosticView)
                                : " (0=normal, 1=coverage, 2=reflection, 3=indirect, 4=red-if-surface-mask, 5=gate-chain, 6=forced-any-hit, 8=candidate-alpha-policy)") +
                           " scene=" + std::to_string(sceneId) + " room=" + std::to_string(roomId) +
                           " frame=" + std::to_string(diagnosticFrame) +
                           " viewHash=" + std::to_string(SurfaceMatrixHash(mViewMatrix)) +
                           " projectionHash=" + std::to_string(SurfaceMatrixHash(mProjMatrix)) +
                           " instances=" + std::to_string(surfaceData.instanceCount) +
                           " tlasInstances=" + std::to_string(tlasInstanceCount) +
                           " gameDir=(" + std::to_string(viewLight.worldDirection[0]) + "," +
                           std::to_string(viewLight.worldDirection[1]) + "," +
                           std::to_string(viewLight.worldDirection[2]) + ") gameValid=" +
                           std::to_string(viewLight.capturedFromScene && viewLight.directionalLightCount > 0 ? 1 : 0));
        lastDiagnosticView = boundedDiagnosticView;
    }
    // Match the same captured transform used by the productive ray lighting.
    // Fast3D may keep the camera in model-view or fold it into projection.
    // In either case this light must share the decoded TLAS normal's space.
    const SceneLightingSnapshot compositeLight = MaterialRegistry::Instance().GetSceneLighting();
    const float* nativeWorldLight = compositeLight.worldDirection;
    DxrSurfaceCompositeConstants compositeParams = {
        { reflectionScale, static_cast<float>(width), static_cast<float>(height), transmissionScale },
        hdrScene ? 1.0f : 0.0f, static_cast<float>(boundedDiagnosticView), 0.0f, 0.0f,
        { nativeWorldLight[0] * mViewMatrix[0][0] + nativeWorldLight[1] * mViewMatrix[1][0] + nativeWorldLight[2] * mViewMatrix[2][0],
          nativeWorldLight[0] * mViewMatrix[0][1] + nativeWorldLight[1] * mViewMatrix[1][1] + nativeWorldLight[2] * mViewMatrix[2][1],
          nativeWorldLight[0] * mViewMatrix[0][2] + nativeWorldLight[1] * mViewMatrix[1][2] + nativeWorldLight[2] * mViewMatrix[2][2] },
        0.0f
    };
    const auto& light = compositeParams.gameLightDirection;
    const float lightLengthSquared = light[0] * light[0] + light[1] * light[1] + light[2] * light[2];
    compositeParams.gameLightValid = compositeLight.capturedFromScene && compositeLight.directionalLightCount > 0 &&
        std::isfinite(lightLengthSquared) && lightLengthSquared > 1.0e-12f ? 1.0f : 0.0f;
    cmdList->SetGraphicsRoot32BitConstants(6, kSurfaceCompositeRootDwords, &compositeParams, 0);
    cmdList->SetPipelineState(mCompositePso[hdrScene ? 1u : 0u].Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    mVisualAblationFrame = -1;
    const char* ablationEnv = std::getenv("COUCH_DXR_VISUAL_ABLATION");
    const char* harnessOutput = std::getenv("COUCH_DXR_OUTPUT");
    const char* harnessActive = std::getenv("COUCH_DXR_HARNESS");
    if (!mVisualAblationCaptured && boundedDiagnosticView == 0 && !MaterialRegistry::IsPathTracedExperimentalProfile() &&
        ablationEnv && std::strcmp(ablationEnv, "1") == 0 &&
        harnessActive && std::strcmp(harnessActive, "1") == 0 && harnessOutput &&
        std::filesystem::exists(std::filesystem::path(harnessOutput) / "capture.request.json")) {
        bool ready = true;
        if (!mVisualAblationRtvs) {
            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = 3;
            ready = SUCCEEDED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mVisualAblationRtvs)));
        }
        if (ready) {
            auto desc = renderTarget->GetDesc();
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            auto handle = mVisualAblationRtvs->GetCPUDescriptorHandleForHeapStart();
            const UINT stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            for (unsigned i = 0; i < 3; ++i) {
                if (!mVisualAblationTargets[i]) {
                    ready = SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&mVisualAblationTargets[i])));
                    if (!ready) break;
                    device->CreateRenderTargetView(mVisualAblationTargets[i].Get(), nullptr, handle);
                }
                compositeParams.ablationMode = static_cast<float>(i + 1);
                cmdList->OMSetRenderTargets(1, &handle, FALSE, nullptr);
                cmdList->SetGraphicsRoot32BitConstants(6, kSurfaceCompositeRootDwords, &compositeParams, 0);
                cmdList->DrawInstanced(3, 1, 0, 0);
                handle.ptr += stride;
            }
        }
        if (ready) {
            mVisualAblationFrame = diagnosticFrame;
            mVisualAblationCaptured = true;
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][ABLATION] frame=" + std::to_string(diagnosticFrame) +
                " sameScene=1 sameSignals=1 sameCommandList=1 controls=SURFACE_OPTICS_OFF,INDIRECT_OFF,RELIGHT_OFF");
        } else {
            LogCouchDxrSurface("[COUCH-DXR-SURFACE][ABLATION_FAILED] frame=" + std::to_string(diagnosticFrame));
        }
        compositeParams.ablationMode = 0.0f;
        cmdList->SetGraphicsRoot32BitConstants(6, kSurfaceCompositeRootDwords, &compositeParams, 0);
        cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    }

    D3D12_RESOURCE_BARRIER depthWrite = {};
    depthWrite.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    depthWrite.Transition.pResource = depthBuffer;
    depthWrite.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    depthWrite.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthWrite.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &depthWrite);

    if (mStats.dispatchRaysCalls == 1) {
        LogCouchDxrSurface("[COUCH-DXR-SURFACE] First reflection DispatchRays SUCCESS | resolution=" +
                           std::to_string(launchWidth) + "x" + std::to_string(launchHeight) +
                           " profile=" +
                           std::string(MaterialRegistry::Instance().GetProfileName()) +
                           " emissiveMask=" + (mEmissiveFormat == DXGI_FORMAT_R16_FLOAT ? "R16F" : "R32F") +
                           " bloom=7x7 indirectSamples=" + std::to_string(mIndirectSamples));
    }
    // OCZ-RTX-REPAIR REPAIR-02/04: machine-readable proof that the fullscreen
    // composite was recorded into the current framebuffer this frame. The
    // scene harness (GATE-B) matches this marker to confirm the DXR signal
    // path reached the presented image instead of stalling in probe buffers.
    ++mStats.compositeExecutions;
    if ((mStats.compositeExecutions % 60) == 1) {
        LogCouchDxrSurface("[COUCH-DXR-SURFACE] COMPOSITE_EXECUTED frame=" +
                           std::to_string(diagnosticFrame) + " target=" +
                           std::to_string(width) + "x" + std::to_string(height) + " hdr=" +
                           std::string(hdrScene ? "1" : "0") + " diagnosticView=" +
                           std::to_string(boundedDiagnosticView) + " executions=" +
                           std::to_string(mStats.compositeExecutions));
    }
}

void DxrSurfaceRenderer::ResolvePrimaryGateCounters(ID3D12CommandQueue* queue) {
    if (!mPrimaryGateReadbackPending) return;
    auto fail = [&](const std::string& reason) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][READBACK_FAILED] mode=" +
            std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)) +
            " frame=" + std::to_string(mPrimaryGatePendingFrame) + " reason=" + reason);
        mPrimaryGateReadbackPending = false;
    };
    if (!queue || !mPrimaryGateFence || !mPrimaryGateFenceEvent || !mPrimaryGateReadback) {
        fail("missing_queue_or_readback_resource");
        return;
    }
    const uint64_t fenceValue = ++mPrimaryGateFenceValue;
    HRESULT hr = queue->Signal(mPrimaryGateFence.Get(), fenceValue);
    if (FAILED(hr)) {
        fail("queue_signal_" + SurfaceHr(hr));
        return;
    }
    LogCouchDxrSurface("[COUCH-DXR-GATE] COUNTER_READBACK_FRAME=" +
        std::to_string(mPrimaryGatePendingFrame) + " COUNTER_READBACK_MODE=" +
        std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)) +
        " COUNTER_READBACK_FENCE=" + std::to_string(fenceValue));
    if (mPrimaryGateFence->GetCompletedValue() < fenceValue) {
        hr = mPrimaryGateFence->SetEventOnCompletion(fenceValue, mPrimaryGateFenceEvent);
        if (FAILED(hr)) {
            fail("set_event_" + SurfaceHr(hr));
            return;
        }
        const DWORD wait = WaitForSingleObject(mPrimaryGateFenceEvent, 30000);
        if (wait != WAIT_OBJECT_0) {
            fail(wait == WAIT_TIMEOUT ? "fence_wait_timeout" : "fence_wait_failed");
            return;
        }
    }

    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(kPrimaryGateCounterBytes) };
    void* mappedData = nullptr;
    hr = mPrimaryGateReadback->Map(0, &readRange, &mappedData);
    if (FAILED(hr) || !mappedData) {
        fail("map_" + SurfaceHr(hr));
        return;
    }
    const auto* mapped = static_cast<const uint32_t*>(mappedData);
    uint64_t totals[kPrimaryGateCounterCount] = {};
    for (uint32_t shard = 0; shard < kPrimaryGateCounterShards; ++shard) {
        for (uint32_t counter = 0; counter < kPrimaryGateCounterCount; ++counter) {
            totals[counter] += mapped[shard * kPrimaryGateCounterCount + counter];
        }
    }
    for (uint32_t sample = 0; sample < kPrimaryAlphaSampleCount; ++sample) {
        const uint32_t address = kPrimaryAlphaSampleBase + sample * kPrimaryAlphaSampleStride;
        const uint32_t* values = mapped + address;
        const uint32_t expectedRecord = sample == 0 ? 0u : sample == 1 ? 1u : 8u;
        if (values[0] != 0u) {
            float bary0 = 0.0f, bary1 = 0.0f, interpolated = 0.0f;
            float textureAlpha = 0.0f, recordAlpha = 0.0f, combinedAlpha = 0.0f, effectiveVertexAlpha = 0.0f;
            memcpy(&bary0, &values[3], sizeof(bary0));
            memcpy(&bary1, &values[4], sizeof(bary1));
            memcpy(&interpolated, &values[11], sizeof(interpolated));
            memcpy(&textureAlpha, &values[12], sizeof(textureAlpha));
            memcpy(&recordAlpha, &values[13], sizeof(recordAlpha));
            memcpy(&combinedAlpha, &values[14], sizeof(combinedAlpha));
            memcpy(&effectiveVertexAlpha, &values[15], sizeof(effectiveVertexAlpha));
            const uint32_t sourceAlphaA = values[16], sourceAlphaB = values[17], sourceAlphaC = values[18];
            const uint32_t recordLoadedAlphaA = values[19], recordLoadedAlphaB = values[20], recordLoadedAlphaC = values[21];
            const uint32_t capturedAlphaA = values[22], capturedAlphaB = values[23], capturedAlphaC = values[24];
            const uint32_t fogModeA = values[25], fogModeB = values[26], fogModeC = values[27];
            const uint32_t localAlphaA = values[28], localAlphaB = values[29], localAlphaC = values[30];
            LogCouchDxrSurface("[COUCH-DXR-ALPHA-FORENSIC][GPU_TRIANGLE_SAMPLE] frame=" +
                std::to_string(mPrimaryGatePendingFrame) + " mode=" +
                PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend,
                                       mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy) +
                " record=" + std::to_string(expectedRecord) + " instance=" +
                std::to_string(values[1]) + " primitive=" + std::to_string(values[2]) +
                " triangle=" + std::to_string(values[5]) + "," + std::to_string(values[6]) + "," +
                std::to_string(values[7]) + " bary=" + std::to_string(bary0) + "," +
                std::to_string(bary1) + " gpuUploadedAlphaABC=" + std::to_string(values[8]) + "," +
                std::to_string(values[9]) + "," + std::to_string(values[10]) +
                " hlslDecodedAlphaABC=" + std::to_string(values[8] / 255.0f) + "," +
                std::to_string(values[9] / 255.0f) + "," + std::to_string(values[10] / 255.0f) +
                " interpolated=" + std::to_string(interpolated) + " effectiveVertexAlpha=" +
                std::to_string(effectiveVertexAlpha) + " textureAlpha=" + std::to_string(textureAlpha) +
                " recordAlpha=" + std::to_string(recordAlpha) + " combinedAlpha=" +
                std::to_string(combinedAlpha) + " rspSourceAlphaABC=" +
                std::to_string(sourceAlphaA) + "," + std::to_string(sourceAlphaB) + "," +
                std::to_string(sourceAlphaC) + " preservedSourceAlphaABC=" +
                std::to_string(sourceAlphaA) + "," + std::to_string(sourceAlphaB) + "," +
                std::to_string(sourceAlphaC) + " fogFactorABC=" +
                std::to_string(recordLoadedAlphaA) + "," + std::to_string(recordLoadedAlphaB) + "," +
                std::to_string(recordLoadedAlphaC) + " postInterpreterRasterAlphaABC=" +
                std::to_string(recordLoadedAlphaA) + "," + std::to_string(recordLoadedAlphaB) + "," +
                std::to_string(recordLoadedAlphaC) + " capturedAlphaABC=" +
                std::to_string(capturedAlphaA) + "," + std::to_string(capturedAlphaB) + "," +
                std::to_string(capturedAlphaC) + " localAlphaABC=" + std::to_string(localAlphaA) + "," +
                std::to_string(localAlphaB) + "," + std::to_string(localAlphaC) + " G_FOG_ABC=" +
                std::to_string(fogModeA) + "," + std::to_string(fogModeB) + "," + std::to_string(fogModeC));
        } else {
            LogCouchDxrSurface("[COUCH-DXR-ALPHA-FORENSIC][GPU_TRIANGLE_SAMPLE] frame=" +
                std::to_string(mPrimaryGatePendingFrame) + " mode=" +
                PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend,
                                       mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy) +
                " record=" + std::to_string(expectedRecord) + " captured=0");
        }
    }
    D3D12_RANGE noWrite = { 0, 0 };
    mPrimaryGateReadback->Unmap(0, &noWrite);
    mPrimaryGateReadbackPending = false;

    const uint64_t total = totals[0], miss = totals[1], hit = totals[2];
    const uint64_t ids = totals[3], material = totals[4], eligible = totals[5];
    const uint64_t coverage = totals[6], surfaceWritten = totals[7];
    const uint64_t invalid = totals[8], nan = totals[9];
    static constexpr const char* validationErrorNames[] = {
        "instance_id_record_count", "geometry_index", "primitive_index", "material_record_fields",
        "index_buffer_bounds", "vertex_buffer_or_triangle_indices", "texture_descriptor",
        "non_finite_record_or_triangle", "material_record_count_metadata_mismatch",
    };
    uint64_t validationErrorTotal = totals[kPrimaryRasterDepthMismatchCounter];
    for (uint32_t i = 0; i < 9; ++i) validationErrorTotal += totals[10 + i];
    bool invariants = (miss + hit == total);
    invariants = invariants && (validationErrorTotal == invalid);
    invariants = invariants && surfaceWritten <= coverage && coverage <= eligible &&
        eligible + totals[23] == material && totals[19] == material &&
        material <= ids && ids <= hit;
    if (!invariants) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][COUNTER_INVARIANT_FAILURE] mode=" +
            std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)) +
            " total=" + std::to_string(total) + " miss=" + std::to_string(miss) +
            " hit=" + std::to_string(hit) + " idsValid=" + std::to_string(ids) +
            " materialValid=" + std::to_string(material) + " eligible=" +
            std::to_string(eligible) + " coverage=" + std::to_string(coverage) +
            " surfaceWritten=" + std::to_string(surfaceWritten));
    }
    LogCouchDxrSurface("[COUCH-DXR-GATE] PRIMARY_GATE_COUNTERS mode=" +
        std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)) +
        " total=" + std::to_string(total) + " miss=" + std::to_string(miss) +
        " hit=" + std::to_string(hit) + " idsValid=" + std::to_string(ids) +
        " materialValid=" + std::to_string(material) + " eligible=" +
        std::to_string(eligible) +
        " coverage=" + std::to_string(coverage) + " surfaceWritten=" +
        std::to_string(surfaceWritten) + " invalid=" + std::to_string(invalid) +
        " nan=" + std::to_string(nan) + " invariants=" + (invariants ? "PASS" : "FAIL"));

    const auto asCount = [](uint64_t value) {
        return value > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(value);
    };
    LogPrimaryGatePercent("miss", asCount(miss), asCount(total), asCount(hit));
    LogPrimaryGatePercent("hit", asCount(hit), asCount(total), asCount(hit));
    LogPrimaryGatePercent("idsValid", asCount(ids), asCount(total), asCount(hit));
    LogPrimaryGatePercent("materialValid", asCount(material), asCount(total), asCount(hit));
    LogPrimaryGatePercent("rasterDepthMismatch", asCount(totals[kPrimaryRasterDepthMismatchCounter]),
                          asCount(total), asCount(hit));
    LogPrimaryGatePercent("eligible", asCount(eligible), asCount(total), asCount(hit));
    LogPrimaryGatePercent("coverage", asCount(coverage), asCount(total), asCount(hit));
    LogPrimaryGatePercent("surfaceWritten", asCount(surfaceWritten), asCount(total), asCount(hit));
    LogPrimaryGatePercent("invalid", asCount(invalid), asCount(total), asCount(hit));
    LogPrimaryGatePercent("nan", asCount(nan), asCount(total), asCount(hit));
    for (uint32_t i = 0; i < 9; ++i) {
        LogCouchDxrSurface("[COUCH-DXR-GATE][VALIDATION_ERROR] code=" +
            std::to_string(i + 2) + " check=" + validationErrorNames[i] +
            " count=" + std::to_string(totals[10 + i]) + " hitPct=" +
            (hit == 0 ? std::string("N/A") : std::to_string(
                static_cast<double>(totals[10 + i]) * 100.0 / static_cast<double>(hit))));
    }

    auto logHistogram = [&](const char* name, uint32_t base, uint32_t bins) {
        std::string values;
        for (uint32_t bin = 0; bin < bins; ++bin) {
            if (totals[base + bin] == 0) continue;
            if (!values.empty()) values += ",";
            values += std::to_string(bin) + ":" + std::to_string(totals[base + bin]);
        }
        LogCouchDxrSurface(std::string("[COUCH-DXR-GATE][HISTOGRAM] ") + name + " bins=" +
            (values.empty() ? "none" : values));
    };
    const uint64_t validHitDenominator = material;
    const uint64_t eligibleAndRejected = totals[5] + totals[23];
    LogCouchDxrSurface("[COUCH-DXR-GATE][ELIGIBILITY_ACCOUNTING] valid=" +
        std::to_string(validHitDenominator) + " eligible=" + std::to_string(totals[5]) +
        " rejectBlend=" + std::to_string(totals[23]) + " sum=" +
        std::to_string(eligibleAndRejected) + " invariant=" +
        (eligibleAndRejected == validHitDenominator ? "PASS" : "ELIGIBILITY_ACCOUNTING_INVARIANT_FAIL"));
    LogCouchDxrSurface("[COUCH-DXR-GATE][ELIGIBILITY_STAGE] stage=MATERIAL_RECORD_VALID entering=" +
        std::to_string(material) + " passing=" + std::to_string(material) + " rejected=0");
    LogCouchDxrSurface("[COUCH-DXR-GATE][ELIGIBILITY_STAGE] stage=PRIMARY_RASTER_BLEND_POLICY entering=" +
        std::to_string(material) + " passing=" + std::to_string(totals[5]) + " rejected=" +
        std::to_string(totals[23]) + " passPct=" + (material == 0 ? "N/A" : std::to_string(
            static_cast<double>(totals[5]) * 100.0 / static_cast<double>(material))));
    LogCouchDxrSurface("[COUCH-DXR-GATE][ELIGIBILITY_STAGE] stage=FINAL_ELIGIBLE entering=" +
        std::to_string(material) + " passing=" + std::to_string(totals[5]) + " rejected=" +
        std::to_string(totals[23]));
    LogCouchDxrSurface("[COUCH-DXR-GATE][RASTER_DEPTH] geometryPixels=" +
        std::to_string(totals[25]) + " backgroundPixels=" + std::to_string(totals[26]) +
        " geometryPrimaryHit=" + std::to_string(totals[27]) + " geometryPrimaryMiss=" +
        std::to_string(totals[28]));
    logHistogram("recordIndex", 29, 257);
    logHistogram("alphaMode", 286, 5);
    logHistogram("materialType", 291, 22);
    logHistogram("flags", 313, 17);
    logHistogram("textureState_0=noTexture_1=flagValid_2=flagInvalid_3=indexWithoutFlag", 330, 4);
    logHistogram("surfaceOpticsW_0=front_1=back_2=blend_3=other", 334, 5);
    logHistogram("materialCoverage_16Bins", 339, 16);
    logHistogram("roughness_16Bins", 355, 16);
    logHistogram("metallic_16Bins", 371, 16);
    logHistogram("reflectivity_16Bins", 387, 16);
    logHistogram("specular_16Bins", 403, 16);
    logHistogram("recordBaseColorAlpha_16Bins", 419, 16);
    logHistogram("vertexAlpha_16Bins", 435, 16);
    logHistogram("sampledBaseColorAlpha_16Bins", 451, 16);
    LogCouchDxrSurface("[COUCH-DXR-GATE][ALPHA_ZERO_COUNTS] vertexAlphaZero=" +
        std::to_string(totals[467]) + " textureAlphaZero=" + std::to_string(totals[468]) +
        " recordAlphaZero=" + std::to_string(totals[469]) + " combinedAlphaZero=" +
        std::to_string(totals[470]) + " coverageNonZero=" + std::to_string(totals[471]));
    logHistogram("vertexAlpha_4Bins", 472, 4);
    logHistogram("sampledTextureAlpha_4Bins", 476, 4);
    logHistogram("recordBaseAlpha_4Bins", 480, 4);
    logHistogram("combinedAlpha_4Bins", 484, 4);
    logHistogram("perRecordTextureAlpha_3Bins_0zero_1fractional_2one", 512, 257 * 6);
    const char* materialTypeNames[] = { "CLASSIC", "STONE", "WOOD", "METAL", "WATER", "GLASS",
        "FABRIC", "EARTH", "GRASS", "FOLIAGE", "SAND", "LAVA", "ICE", "MAGIC", "EMISSIVE",
        "SKY", "CHARACTER", "CERAMIC", "GENERIC_MATTE", "PROTECTED_2D", "UNKNOWN" };
    for (size_t index = 0; index < mPrimaryGatePendingMaterialRecords.size(); ++index) {
        const auto& record = mPrimaryGatePendingMaterialRecords[index];
        const uint64_t recordHits = totals[29 + std::min<size_t>(index, 256)];
        const uint32_t typeIndex = std::min<uint32_t>(record.materialType, 20);
        LogCouchDxrSurface("[COUCH-DXR-GATE][MATERIAL_RECORD] index=" + std::to_string(index) +
            " hits=" + std::to_string(recordHits) + " type=" + materialTypeNames[typeIndex] +
            " typeId=" + std::to_string(record.materialType) + " alphaMode=" +
            std::to_string(record.alphaMode) + " textureIndex=" + std::to_string(record.textureIndex) +
            " candidateAlphaMode=" + std::to_string((record.flags >> 2u) & 3u) +
            " flags=" + std::to_string(record.flags) + " roughness=" + std::to_string(record.roughness) +
            " metallic=" + std::to_string(record.metallic) + " reflectivity=" +
            std::to_string(record.reflectivity) + " specular=" + std::to_string(record.specular) +
            " baseColorAlpha=" + std::to_string(record.baseColor[3]));
    }
    LogCouchDxrSurface("[COUCH-DXR-GATE][REJECTION_GATES] alphaBlendCandidate=" +
        std::to_string(totals[20]) + " transmissiveException=" + std::to_string(totals[21]) +
        " rawPrimaryRasterBlend=" + std::to_string(totals[22]) + " rejectBlend=" +
        std::to_string(totals[23]) + " eligibleCoverageZero=" + std::to_string(totals[24]));

    if (!mPrimaryGatePendingBypassBlend && !mPrimaryGatePendingCandidatePolicy) {
        for (uint32_t i = 0; i < kPrimaryGateCounterCount; ++i) mPrimaryGateNormalCounts[i] = totals[i];
        mPrimaryGateNormalSampleReady = true;
        mPrimaryGateNormalInvariantsPass = invariants;
    } else if (mPrimaryGatePendingCandidatePolicy && mPrimaryGateNormalSampleReady) {
        for (uint32_t i = 0; i < kPrimaryGateCounterCount; ++i) mPrimaryGateCandidateCounts[i] = totals[i];
        uint64_t classificationMatrix[4][3] = {};
        uint64_t recordTextureAlpha[3] = {};
        uint64_t recordCombinedAlpha[3] = {};
        for (size_t index = 0; index < mPrimaryGatePendingMaterialRecords.size(); ++index) {
            const auto& record = mPrimaryGatePendingMaterialRecords[index];
            const uint64_t recordHits = totals[29 + std::min<size_t>(index, 256)];
            const uint32_t oldMode = std::min<uint32_t>(record.alphaMode, 3u);
            const uint32_t candidateMode = std::min<uint32_t>((record.flags >> 2u) & 3u, 2u);
            classificationMatrix[oldMode][candidateMode] += recordHits;
            const uint32_t alphaBase = 512u + static_cast<uint32_t>(std::min<size_t>(index, 256)) * 6u;
            for (uint32_t bin = 0; bin < 3; ++bin) {
                recordTextureAlpha[bin] += totals[alphaBase + bin];
                recordCombinedAlpha[bin] += totals[alphaBase + 3u + bin];
            }
        }
        for (uint32_t oldMode = 0; oldMode < 4; ++oldMode) {
            for (uint32_t candidateMode = 0; candidateMode < 3; ++candidateMode) {
                LogCouchDxrSurface("[COUCH-DXR-ALPHA-POLICY][CLASSIFICATION_MATRIX] currentAlphaMode=" +
                    std::to_string(oldMode) + " candidateAlphaMode=" + std::to_string(candidateMode) +
                    " hits=" + std::to_string(classificationMatrix[oldMode][candidateMode]));
            }
        }
        const uint64_t normalEligible = mPrimaryGateNormalCounts[5];
        const uint64_t candidateEligible = totals[5];
        LogCouchDxrSurface("[COUCH-DXR-ALPHA-POLICY][AB_COMPARE] normalFrame=" +
            std::to_string(mPrimaryGatePendingFrame - 1) + " candidateFrame=" +
            std::to_string(mPrimaryGatePendingFrame) + " normalMaterialValid=" +
            std::to_string(mPrimaryGateNormalCounts[4]) + " candidateMaterialValid=" +
            std::to_string(totals[4]) + " normalEligible=" + std::to_string(normalEligible) +
            " candidateEligible=" + std::to_string(candidateEligible) + " normalCoverage=" +
            std::to_string(mPrimaryGateNormalCounts[6]) + " candidateCoverage=" +
            std::to_string(totals[6]) + " candidateRejectedBlend=" + std::to_string(totals[23]) +
            " candidateBypassBlend=0 candidateBypassVertexAlpha=0 normalInvariant=" +
            std::string(mPrimaryGateNormalInvariantsPass ? "PASS" : "FAIL") + " candidateInvariant=" +
            (invariants ? "PASS" : "FAIL") + " textureAlphaZeroFractionOne=" +
            std::to_string(recordTextureAlpha[0]) + "," + std::to_string(recordTextureAlpha[1]) + "," +
            std::to_string(recordTextureAlpha[2]) + " combinedAlphaZeroFractionOne=" +
            std::to_string(recordCombinedAlpha[0]) + "," + std::to_string(recordCombinedAlpha[1]) + "," +
            std::to_string(recordCombinedAlpha[2]));
        for (size_t index = 0; index < mPrimaryGatePendingMaterialRecords.size(); ++index) {
            const uint32_t alphaBase = 512u + static_cast<uint32_t>(std::min<size_t>(index, 256)) * 6u;
            const auto& record = mPrimaryGatePendingMaterialRecords[index];
            LogCouchDxrSurface("[COUCH-DXR-ALPHA-POLICY][PER_RECORD] record=" + std::to_string(index) +
                " hits=" + std::to_string(totals[29 + std::min<size_t>(index, 256)]) +
                " currentAlphaMode=" + std::to_string(record.alphaMode) +
                " candidateAlphaMode=" + std::to_string((record.flags >> 2u) & 3u) +
                " textureAlphaZeroFractionOne=" + std::to_string(totals[alphaBase]) + "," +
                std::to_string(totals[alphaBase + 1u]) + "," + std::to_string(totals[alphaBase + 2u]) +
                " combinedAlphaZeroFractionOne=" + std::to_string(totals[alphaBase + 3u]) + "," +
                std::to_string(totals[alphaBase + 4u]) + "," + std::to_string(totals[alphaBase + 5u]));
        }
        LogCouchDxrSurface("[COUCH-DXR-ALPHA-POLICY][CANDIDATE_CONTROL] zMode=XLU_or_forceBlendSourceOverOrAdditive alphaCompare=threshold_or_dither_or_CVG_X_ALPHA=Cutout opaqueFallback=Opaque FORCE_BL_alone=insufficient");
    } else if (mPrimaryGatePendingBypassBlend && mPrimaryGateNormalSampleReady) {
        std::string rootClass = "UNRESOLVED";
        std::string firstDrop = "NONE";
        std::string recommendation = "review the measured per-record blend policy before proposing a product change";
        if (!mPrimaryGateNormalInvariantsPass || !invariants) {
            rootClass = "UNCLASSIFIED_COUNTER_INVARIANT_FAILURE";
            recommendation = "fix diagnostic counter accounting before interpreting material eligibility";
        } else {
            const uint64_t normalValid = mPrimaryGateNormalCounts[4];
            const uint64_t normalRejected = mPrimaryGateNormalCounts[23];
            if (normalValid > 0 && normalRejected == normalValid && totals[5] > 0 &&
                totals[23] == 0 && mPrimaryGateNormalCounts[22] == normalRejected) {
                rootClass = "ELIGIBILITY_BLEND_POLICY";
                firstDrop = "primaryRasterBlend rejects every normal valid hit; bypass yields eligible=" +
                    std::to_string(totals[5]);
            } else {
                rootClass = "ELIGIBILITY_RESULT_INCONCLUSIVE";
                firstDrop = "normalRejected=" + std::to_string(normalRejected) +
                    " bypassEligible=" + std::to_string(totals[5]);
            }
        }
        LogCouchDxrSurface("[COUCH-DXR-GATE] FIRST_MAJOR_DROP=" + firstDrop);
        LogCouchDxrSurface("[COUCH-DXR-GATE] ROOT_CLASSIFICATION=" + rootClass);
        LogCouchDxrSurface("[COUCH-DXR-GATE] NEXT_MINIMAL_INVESTIGATION=" + recommendation);
    }

    LogCouchDxrSurface("[COUCH-DXR-GATE] SAMPLE_CONTEXT scene=" +
        std::to_string(mPrimaryGatePendingScene) + " room=" +
        std::to_string(mPrimaryGatePendingRoom) + " frame=" +
        std::to_string(mPrimaryGatePendingFrame) + " dispatch=" +
        std::to_string(mPrimaryGatePendingWidth) + "x" +
        std::to_string(mPrimaryGatePendingHeight) + " tlasInstances=" +
        std::to_string(mPrimaryGatePendingTlasInstances) + " viewHash=" +
        std::to_string(mPrimaryGatePendingViewHash) + " projectionHash=" +
        std::to_string(mPrimaryGatePendingProjectionHash) + " depthResource=" +
        std::to_string(mPrimaryGatePendingDepthIdentity) + " diagnosticMode=" +
        std::string(PrimaryGatePendingMode(mPrimaryGatePendingBypassBlend, mPrimaryGatePendingBypassVertexAlpha, mPrimaryGatePendingCandidatePolicy)));
}

void DxrSurfaceRenderer::OnResize(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    mReflectionMask.Reset();
    mReflectionColor.Reset();
    mEmissiveMask.Reset();
    mIndirectMask.Reset();
    mSceneColorCopy.Reset();
    for (auto& target : mVisualAblationTargets) target.Reset();
    mVisualAblationRtvs.Reset();
    mVisualAblationFrame = -1;
    mVisualAblationCaptured = false;
    mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
    mMaskWidth = 0;
    mMaskHeight = 0;
    mSceneWidth = 0;
    mSceneHeight = 0;
    mSceneFormat = DXGI_FORMAT_UNKNOWN;
    mSceneColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_COPY_DEST);
    mReflectionColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mEmissiveTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mIndirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mPathHistoryValid = false;
    mPathProfileWasActive = false;
}

} // namespace Fast

#endif

