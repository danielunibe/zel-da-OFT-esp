#if defined(_WIN32)

#include "fast/backends/DxrShadowRenderer.h"
#include "fast/backends/DxrDescriptorRing.h"
#include "fast/couch_diagnostics.h"
#include "fast/backends/DxrSurfaceRenderer.h"
#include "fast/backends/dxr_shadow_cso.h"
#include "fast/backends/TemporalManager.h"
#include "fast/backends/RtxQuality.h"
#include "fast/backends/TemporalTrace.h"
#include "fast/MaterialRegistry.h"
#include "fast/interpreter.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "spdlog/spdlog.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace Fast {

// Per-frame file logging is useful while diagnosing a GPU regression, but it
// must never be part of the normal RTX frame budget. Set COUCH_DXR_VERBOSE=1
// when a forensic run really needs these messages.
static bool CouchDxrVerbose() {
    static const bool enabled = [] {
        const char* value = std::getenv("COUCH_DXR_VERBOSE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

static void LogCouchDxrShadow(const std::string& line) {
    SPDLOG_INFO(line);
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) {
        ofs << line << "\n";
    }
    const char* harness = std::getenv("COUCH_DXR_HARNESS");
    const char* outputRoot = std::getenv("COUCH_DXR_OUTPUT");
    if (harness && std::strcmp(harness, "1") == 0 && outputRoot && *outputRoot) {
        std::ofstream runLog(std::string(outputRoot) + "\\harness-dxr.log", std::ios::app);
        if (runLog) runLog << line << '\n';
    }
}

// Inversión analítica 4x4 mediante cofactores para desproyección NDC -> World
static bool InvertMatrix4x4(const float m[4][4], float inv[4][4]) {
    float invTmp[16];
    const float* src = &m[0][0];

    invTmp[0] = src[5]  * src[10] * src[15] - 
                src[5]  * src[11] * src[14] - 
                src[9]  * src[6]  * src[15] + 
                src[9]  * src[7]  * src[14] +
                src[13] * src[6]  * src[11] - 
                src[13] * src[7]  * src[10];

    invTmp[4] = -src[4]  * src[10] * src[15] + 
                 src[4]  * src[11] * src[14] + 
                 src[8]  * src[6]  * src[15] - 
                 src[8]  * src[7]  * src[14] - 
                 src[12] * src[6]  * src[11] + 
                 src[12] * src[7]  * src[10];

    invTmp[8] = src[4]  * src[9] * src[15] - 
                src[4]  * src[11] * src[13] - 
                src[8]  * src[5] * src[15] + 
                src[8]  * src[7] * src[13] + 
                src[12] * src[5] * src[11] - 
                src[12] * src[7] * src[9];

    invTmp[12] = -src[4]  * src[9] * src[14] + 
                  src[4]  * src[10] * src[13] +
                  src[8]  * src[5] * src[14] - 
                  src[8]  * src[6] * src[13] - 
                  src[12] * src[5] * src[10] + 
                  src[12] * src[6] * src[9];

    invTmp[1] = -src[1]  * src[10] * src[15] + 
                 src[1]  * src[11] * src[14] + 
                 src[9]  * src[2] * src[15] - 
                 src[9]  * src[3] * src[14] - 
                 src[13] * src[2] * src[11] + 
                 src[13] * src[3] * src[10];

    invTmp[5] = src[0]  * src[10] * src[15] - 
                src[0]  * src[11] * src[14] - 
                src[8]  * src[2] * src[15] + 
                src[8]  * src[3] * src[14] + 
                src[12] * src[2] * src[11] - 
                src[12] * src[3] * src[10];

    invTmp[9] = -src[0]  * src[9] * src[15] + 
                 src[0]  * src[11] * src[13] + 
                 src[8]  * src[1] * src[15] - 
                 src[8]  * src[3] * src[13] - 
                 src[12] * src[1] * src[11] + 
                 src[12] * src[3] * src[9];

    invTmp[13] = src[0]  * src[9] * src[14] - 
                 src[0]  * src[10] * src[13] - 
                 src[8]  * src[1] * src[14] + 
                 src[8]  * src[2] * src[13] + 
                 src[12] * src[1] * src[10] - 
                 src[12] * src[2] * src[9];

    invTmp[2] = src[1]  * src[6] * src[15] - 
                src[1]  * src[7] * src[14] - 
                src[5]  * src[2] * src[15] + 
                src[5]  * src[3] * src[14] + 
                src[13] * src[2] * src[7] - 
                src[13] * src[3] * src[6];

    invTmp[6] = -src[0]  * src[6] * src[15] + 
                 src[0]  * src[7] * src[14] + 
                 src[4]  * src[2] * src[15] - 
                 src[4]  * src[3] * src[14] - 
                 src[12] * src[2] * src[7] + 
                 src[12] * src[3] * src[6];

    invTmp[10] = src[0]  * src[5] * src[15] - 
                 src[0]  * src[7] * src[13] - 
                 src[4]  * src[1] * src[15] + 
                 src[4]  * src[3] * src[13] + 
                 src[12] * src[1] * src[7] - 
                 src[12] * src[3] * src[5];

    invTmp[14] = -src[0]  * src[5] * src[14] + 
                  src[0]  * src[6] * src[13] + 
                  src[4]  * src[1] * src[14] - 
                  src[4]  * src[2] * src[13] - 
                  src[12] * src[1] * src[6] + 
                  src[12] * src[2] * src[5];

    invTmp[3] = -src[1] * src[6] * src[11] + 
                 src[1] * src[7] * src[10] + 
                 src[5] * src[2] * src[11] - 
                 src[5] * src[3] * src[10] - 
                 src[9] * src[2] * src[7] + 
                 src[9] * src[3] * src[6];

    invTmp[7] = src[0] * src[6] * src[11] - 
                src[0] * src[7] * src[10] - 
                src[4] * src[2] * src[11] + 
                src[4] * src[3] * src[10] + 
                src[8] * src[2] * src[7] - 
                src[8] * src[3] * src[6];

    invTmp[11] = -src[0] * src[5] * src[11] + 
                  src[0] * src[7] * src[9] + 
                  src[4] * src[1] * src[11] - 
                  src[4] * src[3] * src[9] - 
                  src[8] * src[1] * src[7] + 
                  src[8] * src[3] * src[5];

    invTmp[15] = src[0] * src[5] * src[10] - 
                 src[0] * src[6] * src[9] - 
                 src[4] * src[1] * src[10] + 
                 src[4] * src[2] * src[9] + 
                 src[8] * src[1] * src[6] - 
                 src[8] * src[2] * src[5];

    float det = src[0] * invTmp[0] + src[1] * invTmp[4] + src[2] * invTmp[8] + src[3] * invTmp[12];
    if (fabsf(det) < 1e-8f) return false;

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; i++) {
        (&inv[0][0])[i] = invTmp[i] * invDet;
    }
    return true;
}

typedef HRESULT(WINAPI* PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static HRESULT DynamicD3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
                                                   D3D_ROOT_SIGNATURE_VERSION Version,
                                                   ID3DBlob** ppBlob,
                                                   ID3DBlob** ppErrorBlob) {
    static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE sFunc = nullptr;
    if (!sFunc) {
        HMODULE hD3D12 = GetModuleHandleA("d3d12.dll");
        if (!hD3D12) hD3D12 = LoadLibraryA("d3d12.dll");
        if (hD3D12) {
            sFunc = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(hD3D12, "D3D12SerializeRootSignature");
        }
    }
    if (sFunc) {
        return sFunc(pRootSignature, Version, ppBlob, ppErrorBlob);
    }
    return E_FAIL;
}

DxrShadowRenderer& DxrShadowRenderer::Instance() {
    static DxrShadowRenderer sInstance;
    return sInstance;
}

DxrShadowRenderer::DxrShadowRenderer()
    : mInitialized(false), mSupported(false), mEnabled(false),
      mShaderRecordSize(64), mRayGenRecordOffset(0), mMissRecordOffset(256), mHitGroupRecordOffset(512),
      mConstantBufferMapped(nullptr), mDescriptorSize(0), mMaskWidth(0), mMaskHeight(0),
      mMatricesValid(false) {
    memset(mViewMatrix, 0, sizeof(mViewMatrix));
    memset(mProjMatrix, 0, sizeof(mProjMatrix));
    memset(mInvProjectionMatrix, 0, sizeof(mInvProjectionMatrix));
    mViewMatrix[0][0] = mViewMatrix[1][1] = mViewMatrix[2][2] = mViewMatrix[3][3] = 1.0f;
    mProjMatrix[0][0] = mProjMatrix[1][1] = mProjMatrix[2][2] = mProjMatrix[3][3] = 1.0f;
    mInvProjectionMatrix[0][0] = mInvProjectionMatrix[1][1] = mInvProjectionMatrix[2][2] = mInvProjectionMatrix[3][3] = 1.0f;
}

DxrShadowRenderer::~DxrShadowRenderer() {
    if (mConstantBuffer && mConstantBufferMapped) {
        mConstantBuffer->Unmap(0, nullptr);
        mConstantBufferMapped = nullptr;
    }
}

bool DxrShadowRenderer::Init(ID3D12Device* device) {
    if (mInitialized) return true;
    if (!device) return false;

    // Hard Capability Gate: Consultar soporte DXR y obtener ID3D12Device5
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) ||
        options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] DXR Raytracing not supported on this adapter.");
        mSupported = false;
        return false;
    }

    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&mDevice5)))) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Failed to obtain ID3D12Device5.");
        mSupported = false;
        return false;
    }

    mSupported = true;

    // Crear Global Root Signature
    if (!CreateRootSignature(device)) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Failed to create DXR Global Root Signature.");
        return false;
    }

    // Crear State Object (RTPSO) SM 6.3
    if (!CreateRaytracingPipelineState(mDevice5.Get())) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Failed to create DXR State Object.");
        return false;
    }

    // Crear Shader Binding Table (SBT)
    if (!CreateShaderTable(device)) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Failed to create Shader Binding Table.");
        return false;
    }

    // Crear Constant Buffer para FrameConstants (256 bytes)
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = 256 * 3;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&mConstantBuffer)))) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Failed to create FrameConstants buffer.");
        return false;
    }

    mConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mConstantBufferMapped));

    // The ray pass is not usable without its final composite. Treat the
    // composite PSOs as required resources so a partial initialization can
    // never leave DXR recording draws through null pipeline state objects.
    if (!CreateCompositePipeline(device)) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW] Composite pipeline creation failed; disabling DXR shadows.");
        mSupported = false;
        mEnabled = false;
        return false;
    }

    const char* envShadow = getenv("COUCH_DXR_SHADOW");
    const char* envDxr = getenv("COUCH_DXR_AS");
    const bool explicitlyDisabled = envShadow && strcmp(envShadow, "0") == 0;
    const bool explicitlyRequested = envShadow && strcmp(envShadow, "1") == 0;
    const bool useHardwareDefault = !envShadow && !envDxr;
    if (!explicitlyDisabled && (explicitlyRequested || useHardwareDefault)) {
        mEnabled = true;
        LogCouchDxrShadow(std::string("[COUCH-DXR-SHADOW] Enabled via ") +
                          (useHardwareDefault ? "supported-hardware default" : "environment"));
    }

    // Diagnostic switches (Phase 2): both default to true when DXR_SHADOW is enabled
    mDispatchEnabled = true;
    mCompositeEnabled = true;
    const char* envDispatch = getenv("COUCH_DXR_DISPATCH_ENABLE");
    if (envDispatch && strcmp(envDispatch, "0") == 0) {
        mDispatchEnabled = false;
    }
    const char* envComposite = getenv("COUCH_DXR_COMPOSITE_ENABLE");
    if (envComposite && strcmp(envComposite, "0") == 0) {
        mCompositeEnabled = false;
    }

    // Phase 3: White composite diagnostic test
    mWhiteCompositeTest = false;
    const char* envWhite = getenv("COUCH_WHITE_COMPOSITE_TEST");
    if (envWhite && strcmp(envWhite, "1") == 0) {
        mWhiteCompositeTest = true;
    }

    mInitialized = true;
    if (auto cvars = Ship::Context::GetInstance()->GetConsoleVariables()) {
        cvars->RegisterInteger("gEnhancements.Graphics.DXR.Shadows", 2);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.ShadowBias", 0.15f);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.ShadowMaxDist", 10000.0f);
        cvars->RegisterFloat("gEnhancements.Graphics.DXR.ShadowIntensity", 0.5f);
    }
    LogCouchDxrShadow("[COUCH-DXR-SHADOW] DxrShadowRenderer initialized successfully.");
    LogCouchDxrShadow("[COUCH-DXR-SHADOW] DIAGNOSTIC: dispatch=" + std::string(mDispatchEnabled ? "ON" : "OFF") +
                      " composite=" + std::string(mCompositeEnabled ? "ON" : "OFF") +
                      " whiteTest=" + std::string(mWhiteCompositeTest ? "ON" : "OFF"));
    LogCouchDxrShadow("[COUCH-DXR] StateObject created");
    LogCouchDxrShadow("[COUCH-DXR] RayGen loaded");
    LogCouchDxrShadow("[COUCH-DXR] Miss loaded");
    LogCouchDxrShadow("[COUCH-DXR] HitGroup loaded");
    LogCouchDxrShadow("[COUCH-DXR] ShaderTable ready");

    return true;
}

bool DxrShadowRenderer::CreateRootSignature(ID3D12Device* device) {
    // 8 Root Parameters:
    // 0: SRV t0 (TLAS RaytracingAccelerationStructure)
    // 1: Descriptor Table SRV t1 (Depth Texture)
    // 2: Descriptor Table UAV u0 (Shadow Mask Output)
    // 3: CBV b0 (FrameConstants)
    // 4..6: Root SRVs t2..t4 (hit vertices, indices, per-instance material)
    // 7: Descriptor Table SRV t5 (material textures)

    D3D12_ROOT_PARAMETER rootParams[8] = {};

    // t0: TLAS SRV
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // t1: Depth Texture SRV Table
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 1;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // u0: Shadow Mask UAV Table
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b0: FrameConstants CBV
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 0;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for (UINT i = 0; i < 3; ++i) {
        rootParams[4 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParams[4 + i].Descriptor.ShaderRegister = 2 + i;
        rootParams[4 + i].Descriptor.RegisterSpace = 0;
        rootParams[4 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kDxrSurfaceTextureDescriptorCount;
    textureRange.BaseShaderRegister = 5;
    textureRange.RegisterSpace = 0;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[7].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[7].DescriptorTable.pDescriptorRanges = &textureRange;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC materialSampler = {};
    materialSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    materialSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    materialSampler.MaxAnisotropy = 1;
    materialSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    materialSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    materialSampler.MinLOD = 0.0f;
    materialSampler.MaxLOD = D3D12_FLOAT32_MAX;
    materialSampler.ShaderRegister = 0;
    materialSampler.RegisterSpace = 0;
    materialSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 8;
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &materialSampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;


    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;
    if (FAILED(DynamicD3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob))) {
        if (errorBlob) {
            SPDLOG_ERROR("[COUCH-DXR-SHADOW] D3D12SerializeRootSignature failed: {}", (char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mGlobalRootSignature)))) {
        return false;
    }

    return true;
}

bool DxrShadowRenderer::CreateRaytracingPipelineState(ID3D12Device5* device5) {
    // 5 Subobjetos DXR:
    // 0: DXIL Library
    // 1: HitGroup
    // 2: Shader Config
    // 3: Global Root Signature
    // 4: Pipeline Config

    D3D12_STATE_SUBOBJECT subobjects[5] = {};

    // 0: DXIL Library
    D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
    dxilLibDesc.DXILLibrary.pShaderBytecode = g_pDxrShadowBytecode;
    dxilLibDesc.DXILLibrary.BytecodeLength = sizeof(g_pDxrShadowBytecode);

    D3D12_EXPORT_DESC exports[4] = {};
    exports[0].Name = L"RayGenShadow";
    exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    exports[1].Name = L"MissShadow";
    exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    exports[2].Name = L"ClosestHitShadow";
    exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    exports[3].Name = L"AnyHitShadow";
    exports[3].Flags = D3D12_EXPORT_FLAG_NONE;

    dxilLibDesc.NumExports = 4;
    dxilLibDesc.pExports = exports;

    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &dxilLibDesc;

    // 1: HitGroup
    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.HitGroupExport = L"HitGroupShadow";
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHitShadow";
    hitGroupDesc.AnyHitShaderImport = L"AnyHitShadow";

    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hitGroupDesc;

    // 2: Shader Config
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    // This state object also carries the surface reflection payload: float4
    // radiance plus four uint fields. The shared shader config must cover the
    // largest TraceRay payload, not only the scalar shadow visibility value.
    shaderConfig.MaxPayloadSizeInBytes = 32;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2; // 8 bytes barycentrics

    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[2].pDesc = &shaderConfig;

    // 3: Global Root Signature
    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig = {};
    globalRootSig.pGlobalRootSignature = mGlobalRootSignature.Get();

    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[3].pDesc = &globalRootSig;

    // 4: Pipeline Config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[4].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC stateDesc = {};
    stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateDesc.NumSubobjects = 5;
    stateDesc.pSubobjects = subobjects;

    if (FAILED(device5->CreateStateObject(&stateDesc, IID_PPV_ARGS(&mStateObject)))) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] CreateStateObject failed.");
        return false;
    }

    if (FAILED(mStateObject.As(&mStateObjectProperties))) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to obtain ID3D12StateObjectProperties.");
        return false;
    }

    mStats.stateObjectCreated = true;
    mStats.rayGenLoaded = (mStateObjectProperties->GetShaderIdentifier(L"RayGenShadow") != nullptr);
    mStats.missLoaded = (mStateObjectProperties->GetShaderIdentifier(L"MissShadow") != nullptr);
    mStats.hitGroupLoaded = (mStateObjectProperties->GetShaderIdentifier(L"HitGroupShadow") != nullptr);
    mStats.anyHitLoaded = (mStateObjectProperties->GetShaderIdentifier(L"AnyHitShadow") != nullptr);

    return true;
}

bool DxrShadowRenderer::CreateShaderTable(ID3D12Device* device) {
    if (!mStateObjectProperties) return false;

    void* rayGenId = mStateObjectProperties->GetShaderIdentifier(L"RayGenShadow");
    void* missId = mStateObjectProperties->GetShaderIdentifier(L"MissShadow");
    void* hitGroupId = mStateObjectProperties->GetShaderIdentifier(L"HitGroupShadow");

    if (!rayGenId || !missId || !hitGroupId) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Could not retrieve shader identifiers for SBT.");
        return false;
    }

    const uint32_t idSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32 bytes
    const uint32_t recordAlignment = 64;
    const uint32_t tableAlignment = 256;

    mShaderRecordSize = recordAlignment;
    mRayGenRecordOffset = 0;
    mMissRecordOffset = tableAlignment;
    mHitGroupRecordOffset = tableAlignment * 2;

    const size_t totalTableSize = tableAlignment * 3; // 768 bytes

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalTableSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&mShaderTableBuffer)))) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create Shader Table buffer.");
        return false;
    }

    uint8_t* pData = nullptr;
    if (SUCCEEDED(mShaderTableBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData)))) {
        memset(pData, 0, totalTableSize);
        memcpy(pData + mRayGenRecordOffset, rayGenId, idSize);
        memcpy(pData + mMissRecordOffset, missId, idSize);
        memcpy(pData + mHitGroupRecordOffset, hitGroupId, idSize);
        mShaderTableBuffer->Unmap(0, nullptr);
    } else {
        return false;
    }

    mStats.shaderTableReady = true;
    return true;
}

bool DxrShadowRenderer::EnsureShadowMaskResources(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (mShadowMaskTexture && mMaskWidth == width && mMaskHeight == height) {
        return true;
    }

    mShadowMaskTexture.Reset();
    mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
    mBoundTextureSrvIndices.clear();
    mBoundRasterSrvHeap = nullptr;

    mMaskWidth = width;
    mMaskHeight = height;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                             IID_PPV_ARGS(&mShadowMaskTexture)))) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create Shadow Mask texture.");
        return false;
    }

    // Slots 0..3 are depth SRV, mask UAV, mask SRV, and a null texture SRV.
    // Slots 4..4099 form the t5 material-texture table.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 4 + kDxrSurfaceTextureDescriptorCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)))) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create DXR Descriptor Heap.");
        return false;
    }

    InitializeDxrDescriptorTemplate(device,mDescriptorHeap.Get());
        mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Slot 1: UAV para mShadowMaskTexture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    uavCpuHandle.ptr += 1 * mDescriptorSize; // Slot 1
    device->CreateUnorderedAccessView(mShadowMaskTexture.Get(), nullptr, &uavDesc, uavCpuHandle);

    // Slot 2: SRV para mShadowMaskTexture (para composite)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpuHandle.ptr += 2 * mDescriptorSize; // Slot 2
    device->CreateShaderResourceView(mShadowMaskTexture.Get(), &srvDesc, srvCpuHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureDesc = {};
    nullTextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTextureDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTextureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTextureDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE nullTextureCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    nullTextureCpu.ptr += 3 * mDescriptorSize;
    device->CreateShaderResourceView(nullptr, &nullTextureDesc, nullTextureCpu);

    D3D12_CPU_DESCRIPTOR_HANDLE textureTableCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    textureTableCpu.ptr += 4 * mDescriptorSize;
    for (uint32_t i = 0; i < kDxrSurfaceTextureDescriptorCount; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst = textureTableCpu;
        dst.ptr += static_cast<SIZE_T>(i) * mDescriptorSize;
        device->CopyDescriptorsSimple(1, dst, nullTextureCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    mStats.shadowMaskUavBound = true;
    mStats.shadowWidth = width;
    mStats.shadowHeight = height;

    return true;
}

bool DxrShadowRenderer::CreateCompositePipeline(ID3D12Device* device) {
    // Root Signature para Composite Pass:
    // Slot 0: Descriptor Table SRV (Mask Texture)
    // Slot 1: 32-bit Constants (intensity, debugMode)
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 4; // float intensity, float debugMode, 2 unused
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static Point / Linear Sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
    sigDesc.NumParameters = 2;
    sigDesc.pParameters = params;
    sigDesc.NumStaticSamplers = 1;
    sigDesc.pStaticSamplers = &sampler;
    sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> err;
    if (FAILED(DynamicD3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mCompositeRootSig)))) {
        return false;
    }

    // Shaders HLSL mínimos para Fullscreen Triangle Pass
    const char* vsSrc = 
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSOut VSMain(uint id : SV_VertexID) {\n"
        "    VSOut o;\n"
        "    o.uv = float2((id << 1) & 2, id & 2);\n"
        "    o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
        "    return o;\n"
        "}\n";

    const char* psCompositeSrc =
        "Texture2D<float> gMask : register(t0);\n"
        "SamplerState gSampler : register(s0);\n"
        "cbuffer Params : register(b0) { float gIntensity; float gDebugMode; float2 pad; };\n"
        "float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "    float mask = gMask.Sample(gSampler, uv);\n"
        "    float shadowFactor = 1.0f - gIntensity * (1.0f - mask);\n"
        "    return float4(shadowFactor, shadowFactor, shadowFactor, 1.0f);\n"
        "}\n";

    const char* psDebugSrc =
        "Texture2D<float> gMask : register(t0);\n"
        "SamplerState gSampler : register(s0);\n"
        "cbuffer Params : register(b0) { float gIntensity; float gDebugMode; float2 pad; };\n"
        "float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "    float mask = gMask.Sample(gSampler, uv);\n"
        "    if (isnan(mask) || isinf(mask)) return float4(1.0f, 0.0f, 1.0f, 1.0f);\n"
        "    return float4(mask, mask, mask, 1.0f);\n"
        "}\n";

    ComPtr<ID3DBlob> vsBlob, psCompBlob, psDebugBlob;
    D3DCompile(vsSrc, strlen(vsSrc), "VS", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(psCompositeSrc, strlen(psCompositeSrc), "PSComp", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psCompBlob, nullptr);
    D3DCompile(psDebugSrc, strlen(psDebugSrc), "PSDebug", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psDebugBlob, nullptr);

    if (!vsBlob || !psCompBlob || !psDebugBlob) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to compile fullscreen composite shaders.");
        return false;
    }

    // PSO para Composite con Blend Multiplicativo (DEST_COLOR * SRC_COLOR)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = mCompositeRootSig.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psCompBlob->GetBufferPointer(), psCompBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    const DXGI_FORMAT compositeFormats[2] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT };
    psoDesc.RTVFormats[0] = compositeFormats[0];
    psoDesc.SampleDesc.Count = 1;

    // Blending Multiplicativo: FinalColor = DestColor * SrcColor
    D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = psoDesc.BlendState.RenderTarget[0];
    rtBlend.BlendEnable = TRUE;
    rtBlend.SrcBlend = D3D12_BLEND_DEST_COLOR;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    for (size_t i = 0; i < 2; ++i) {
        psoDesc.RTVFormats[0] = compositeFormats[i];
        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mCompositePso[i]))) || !mCompositePso[i]) {
            SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create hybrid composite PSO.");
            return false;
        }
    }

    // PSO para Debug Mask (Reemplazo Directo: FinalColor = SrcColor)
    psoDesc.PS = { psDebugBlob->GetBufferPointer(), psDebugBlob->GetBufferSize() };
    rtBlend.BlendEnable = FALSE; // Opaque overwrite
    for (size_t i = 0; i < 2; ++i) {
        psoDesc.RTVFormats[0] = compositeFormats[i];
        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mDebugMaskPso[i]))) || !mDebugMaskPso[i]) {
            SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create debug-mask PSO.");
            return false;
        }
    }

    // Phase 3: White Composite Diagnostic PSO
    // Output float4(1,1,1,1) with same blend state as composite (DestColor * SrcColor)
    const char* psWhiteSrc =
        "Texture2D<float> gMask : register(t0);\n"
        "SamplerState gSampler : register(s0);\n"
        "cbuffer Params : register(b0) { float gIntensity; float gDebugMode; float2 pad; };\n"
        "float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "    return float4(1.0f, 1.0f, 1.0f, 1.0f);\n"
        "}\n";
    ComPtr<ID3DBlob> psWhiteBlob;
    D3DCompile(psWhiteSrc, strlen(psWhiteSrc), "PSWhite", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psWhiteBlob, nullptr);
    if (!psWhiteBlob) {
        SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to compile diagnostic composite shader.");
        return false;
    }
    if (psWhiteBlob) {
        psoDesc.PS = { psWhiteBlob->GetBufferPointer(), psWhiteBlob->GetBufferSize() };
        // Keep exact same blend state as composite: DestColor * SrcColor
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_DEST_COLOR;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        for (size_t i = 0; i < 2; ++i) {
            psoDesc.RTVFormats[0] = compositeFormats[i];
            if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mWhiteCompositePso[i]))) ||
                !mWhiteCompositePso[i]) {
                SPDLOG_ERROR("[COUCH-DXR-SHADOW] Failed to create diagnostic composite PSO.");
                return false;
            }
        }
    }

    return true;
}

void DxrShadowRenderer::InvalidateCameraMatrices() {
    mMatricesValid = false;
    DxrSurfaceRenderer::Instance().InvalidateCameraMatrices();
}

void DxrShadowRenderer::UpdateCameraMatrices(const float view[4][4], const float proj[4][4], float clipXScale) {
    // FORENSIC (phase-2 instrumentation fix): this is called ~258x/frame by the game
    // loop; per-call trace lines blew the trace log up to 3.3GB. Throttle to the
    // first 5 calls and every 600th call.
    static int sUcmTraceCount = 0;
    const bool ucmTrace = (sUcmTraceCount < 5) || (sUcmTraceCount % 600 == 0);
    ++sUcmTraceCount;
    if (ucmTrace) {
        TEMPORAL_TRACE_ISENABLED("DxrShadowRenderer::UpdateCameraMatrices:IsEnabled", TemporalManager::Instance().IsEnabled());
    }
    memcpy(mViewMatrix, view, sizeof(mViewMatrix));
    memcpy(mProjMatrix, proj, sizeof(mProjMatrix));
    mClipXScale = (std::isfinite(clipXScale) && clipXScale > 1.0e-4f) ? clipXScale : 1.0f;

    mMatricesValid = false;
    if (InvertMatrix4x4(mProjMatrix, mInvProjectionMatrix)) {
        mMatricesValid = true;
        mStats.inverseProjectionValid = true;
    }

    // RT4-A: Also update temporal manager with current camera matrices
    if (ucmTrace) {
        TEMPORAL_TRACE_ENTER("UpdateCameraMatrices");
    }
    float temporalProjection[4][4];
    memcpy(temporalProjection,proj,sizeof(temporalProjection));
    for (int r=0;r<4;++r) temporalProjection[r][0]*=mClipXScale;
    TemporalManager::Instance().UpdateCameraMatrices(view, temporalProjection);
    DxrSurfaceRenderer::Instance().UpdateCameraMatrices(view, proj, mClipXScale);
    if (ucmTrace) {
        TEMPORAL_TRACE_EXIT("UpdateCameraMatrices");
    }
}

void DxrShadowRenderer::RenderShadows(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                      ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                                      ID3D12Resource* depthBuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                                      D3D12_GPU_VIRTUAL_ADDRESS tlasGpuVA,
                                      uint32_t width, uint32_t height,
                                      uint32_t frameIndex, uint64_t fenceValue,
                                      const DxrSurfaceFrameBuffers& surfaceData,
                                      ID3D12DescriptorHeap* rasterSrvHeap,
                                      uint32_t rasterSrvDescriptorSize,
                                      bool forceDebugMask,
                                      bool forceRawSignal) {
    if (!mInitialized || !mSupported || !mEnabled) return;
    if (!device || !cmdList || !renderTarget || !depthBuffer || tlasGpuVA == 0) return;
    if (width == 0 || height == 0 || !mMatricesValid) return;
    if (!surfaceData.vertexData || !surfaceData.indexData || !surfaceData.instanceData ||
        !rasterSrvHeap || rasterSrvDescriptorSize == 0) return;

    mStats.renderWidth = width;
    mStats.renderHeight = height;

    // Obtener configuración de CVars o defaults
    int shadowMode = 2; // Default: Hybrid Composite
    float shadowBias = 0.15f;
    float shadowDist = 10000.0f;
    float shadowStrength = 0.85f;
    float lightDir[4] = { 0.22f, 0.94f, 0.26f, 0.0f }; // Luz solar cenital (+Y dominante) para sombras naturales bajo estructuras

    auto cvars = Ship::Context::GetInstance()->GetConsoleVariables();
    if (cvars) {
        shadowMode = cvars->GetInteger("gEnhancements.Graphics.DXR.Shadows", 2);
        shadowBias = cvars->GetFloat("gEnhancements.Graphics.DXR.ShadowBias", 0.15f);
        shadowDist = cvars->GetFloat("gEnhancements.Graphics.DXR.ShadowMaxDist", 10000.0f);
        shadowStrength = cvars->GetFloat("gEnhancements.Graphics.DXR.ShadowIntensity", 0.85f);
    }
    const SceneLightingSnapshot sceneLighting = MaterialRegistry::Instance().GetSceneLighting();
    if (sceneLighting.capturedFromScene) {
        if (sceneLighting.directionalLightCount > 0) {
            for (int axis = 0; axis < 3; ++axis) lightDir[axis] = sceneLighting.worldDirection[axis];
        } else {
            // Do not cast a synthetic sun shadow in an ambient-only scene.
            shadowMode = 0;
        }
    }
    if (forceDebugMask) {
        shadowMode = 1; // Temporary sequence stage A: visualize the raw DXR visibility mask.
    } else if (const char* debugEnv = getenv("COUCH_DXR_SHADOW_DEBUG")) {
        if (strcmp(debugEnv, "1") == 0) shadowMode = 1; // Visualize the raw DXR visibility mask.
    }

    if (shadowMode <= 0) {
        // Desactivado por CVar
        return;
    }
    shadowMode = std::clamp(shadowMode, 1, 2);

    const uint32_t maskWidth = RtxScaledExtent(width, mQualityScale);
    const uint32_t maskHeight = RtxScaledExtent(height, mQualityScale);
    if (!EnsureShadowMaskResources(device, maskWidth, maskHeight)) return;

    // Copiar descriptor SRV de profundidad a Slot 0 del descriptor heap de DXR
    SelectDxrDescriptorHeap(device, mDescriptorHeap, mDescriptorTemplate, mDescriptorFrames, frameIndex % 3);
    D3D12_CPU_DESCRIPTOR_HANDLE slot0CpuHandle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    device->CopyDescriptorsSimple(1, slot0CpuHandle, depthSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mStats.depthSrvBound = true;

    const size_t textureCount = std::min<size_t>(surfaceData.textureSrvIndices.size(),
                                                  kDxrSurfaceTextureDescriptorCount - 1);
    const bool textureTableChanged = true; // populate the selected frame-local heap;
    if (textureTableChanged && textureCount != 0) {
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> destinationStarts(textureCount);
        std::vector<UINT> destinationSizes(textureCount, 1);
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sourceStarts(textureCount);
        std::vector<UINT> sourceSizes(textureCount, 1);
        D3D12_CPU_DESCRIPTOR_HANDLE destination = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE source = rasterSrvHeap->GetCPUDescriptorHandleForHeapStart();
        for (size_t i = 0; i < textureCount; ++i) {
            destinationStarts[i] = destination;
            destinationStarts[i].ptr += static_cast<SIZE_T>(5 + i) * mDescriptorSize;
            sourceStarts[i] = source;
            sourceStarts[i].ptr += static_cast<SIZE_T>(surfaceData.textureSrvIndices[i]) * rasterSrvDescriptorSize;
        }
        device->CopyDescriptors(static_cast<UINT>(textureCount), destinationStarts.data(), destinationSizes.data(),
                                static_cast<UINT>(textureCount), sourceStarts.data(), sourceSizes.data(),
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (textureTableChanged) {
        mBoundTextureSrvIndices.assign(surfaceData.textureSrvIndices.begin(),
                                       surfaceData.textureSrvIndices.begin() + textureCount);
        mBoundRasterSrvHeap = rasterSrvHeap;
    }

    // Actualizar ConstantBuffer FrameConstants
    if (mConstantBufferMapped) {
        DxrShadowFrameConstants fc = {};
        memcpy(fc.invProjection, mInvProjectionMatrix, sizeof(fc.invProjection));
        // Captured TLAS instances include the Fast3D view transform. Express
        // the fixed world-space key light in that same view space so its
        // direction remains anchored as the camera moves.
        const float viewLight[3] = {
            lightDir[0] * mViewMatrix[0][0] + lightDir[1] * mViewMatrix[1][0] + lightDir[2] * mViewMatrix[2][0],
            lightDir[0] * mViewMatrix[0][1] + lightDir[1] * mViewMatrix[1][1] + lightDir[2] * mViewMatrix[2][1],
            lightDir[0] * mViewMatrix[0][2] + lightDir[1] * mViewMatrix[1][2] + lightDir[2] * mViewMatrix[2][2],
        };
        const float viewLightLength = sqrtf(viewLight[0] * viewLight[0] + viewLight[1] * viewLight[1] +
                                            viewLight[2] * viewLight[2]);
        if (viewLightLength > 1.0e-6f) {
            fc.lightDir[0] = viewLight[0] / viewLightLength;
            fc.lightDir[1] = viewLight[1] / viewLightLength;
            fc.lightDir[2] = viewLight[2] / viewLightLength;
            fc.lightDir[3] = 0.0f;
        } else {
            memcpy(fc.lightDir, lightDir, sizeof(fc.lightDir));
        }
        fc.screenSize[0] = static_cast<float>(width);
        fc.screenSize[1] = static_cast<float>(height);
        fc.screenSize[2] = 1.0f / static_cast<float>(width);
        fc.screenSize[3] = 1.0f / static_cast<float>(height);
        fc.params[0] = shadowBias;
        fc.params[1] = shadowDist;
        fc.params[2] = mClipXScale;
        fc.params[3] = shadowStrength;
        fc.quality[0] = static_cast<float>(mShadowSamples);

        memcpy(mConstantBufferMapped + (frameIndex % 3) * 256, &fc, sizeof(fc));
    }

    // Obtener ID3D12GraphicsCommandList4 para DispatchRays
    ComPtr<ID3D12GraphicsCommandList4> cmdList4;
    if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
        return;
    }

    // Diagnostic switch (Phase 2): COUCH_DXR_DISPATCH_ENABLE=0 skips DispatchRays
    // but we still do all barriers so resources are in correct state for composite
    bool doDispatch = mDispatchEnabled;

    // Barreras:
    // DepthBuffer: DEPTH_WRITE -> NON_PIXEL_SHADER_RESOURCE (para lectura en RayGen)
    // ShadowMask: UNORDERED_ACCESS (ya está o se asegura)
    D3D12_RESOURCE_BARRIER preBarriers[2] = {};
    preBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preBarriers[0].Transition.pResource = depthBuffer;
    preBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    preBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    preBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    preBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preBarriers[1].Transition.pResource = mShadowMaskTexture.Get();
    preBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    preBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    preBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    cmdList->ResourceBarrier(2, preBarriers);
    mMaskTrackedState = (unsigned)D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // FORENSIC: DXR_SHADOW heap/replacement state before SetDescriptorHeaps.
    // This is intentionally opt-in because it runs once per frame.
    if (CouchDxrVerbose()) {
        char fbuf[256];
        snprintf(fbuf, sizeof(fbuf), "[COUCH-FORENSIC][DXR_HEAP_PRE] cmdList heapBefore=0x%p -> dxrHeap=0x%p", (void*)cmdList, (void*)mDescriptorHeap.Get());
        LogCouchDxrShadow(fbuf);
    }

    // Configurar DXR Pipeline y Descriptor Heaps
    ID3D12DescriptorHeap* heaps[] = { mDescriptorHeap.Get() };
    cmdList4->SetDescriptorHeaps(1, heaps);

    cmdList4->SetComputeRootSignature(mGlobalRootSignature.Get());

    // Root Param 0: TLAS GPU Virtual Address (SRV t0)
    cmdList4->SetComputeRootShaderResourceView(0, tlasGpuVA);
    mStats.tlasBound = true;

    // Root Param 1: Descriptor Table Depth SRV (t1) -> Slot 0
    D3D12_GPU_DESCRIPTOR_HANDLE depthGpuHandle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList4->SetComputeRootDescriptorTable(1, depthGpuHandle);

    // Root Param 2: Descriptor Table Mask UAV (u0) -> Slot 1
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle = depthGpuHandle;
    uavGpuHandle.ptr += 1 * mDescriptorSize;
    cmdList4->SetComputeRootDescriptorTable(2, uavGpuHandle);

    // Root Param 3: Constant Buffer (b0)
    cmdList4->SetComputeRootConstantBufferView(3, mConstantBuffer->GetGPUVirtualAddress() + (frameIndex % 3) * 256);

    cmdList4->SetComputeRootShaderResourceView(4, surfaceData.vertexData->GetGPUVirtualAddress());
    cmdList4->SetComputeRootShaderResourceView(5, surfaceData.indexData->GetGPUVirtualAddress());
    cmdList4->SetComputeRootShaderResourceView(6, surfaceData.instanceData->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE textureTableGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    textureTableGpu.ptr += 4 * mDescriptorSize;
    cmdList4->SetComputeRootDescriptorTable(7, textureTableGpu);

    // Configurar DXR State Object
    cmdList4->SetPipelineState1(mStateObject.Get());

    // Configurar D3D12_DISPATCH_RAYS_DESC
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mRayGenRecordOffset;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = mShaderRecordSize;

    dispatchDesc.MissShaderTable.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mMissRecordOffset;
    dispatchDesc.MissShaderTable.SizeInBytes = mShaderRecordSize;
    dispatchDesc.MissShaderTable.StrideInBytes = mShaderRecordSize;

    dispatchDesc.HitGroupTable.StartAddress = mShaderTableBuffer->GetGPUVirtualAddress() + mHitGroupRecordOffset;
    dispatchDesc.HitGroupTable.SizeInBytes = mShaderRecordSize;
    dispatchDesc.HitGroupTable.StrideInBytes = mShaderRecordSize;

    dispatchDesc.Width = maskWidth;
    dispatchDesc.Height = maskHeight;
    dispatchDesc.Depth = 1;

    if (doDispatch) {
        cmdList4->DispatchRays(&dispatchDesc);
    } else {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW][DIAG] DispatchRays SKIPPED (DXR_DISPATCH_ENABLE=0)");
    }

    mStats.dispatchRaysCalls += doDispatch ? 1 : 0;
    // DispatchRays runs over the quality-scaled mask, which may be smaller
    // than the scene buffer. Keep telemetry aligned with actual raygen work.
    mStats.rayGenInvocations += doDispatch ? (static_cast<uint64_t>(maskWidth) * maskHeight) : 0;
    mStats.rayDispatchIssued = doDispatch;

    // Barreras posteriores a DispatchRays:
    // UAV Barrier en mShadowMaskTexture para garantizar finalización de escrituras
    // ShadowMask: UNORDERED_ACCESS -> PIXEL_SHADER_RESOURCE (para composite pass)
    // DepthBuffer: NON_PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
    D3D12_RESOURCE_BARRIER midBarriers[3] = {};
    midBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    midBarriers[0].UAV.pResource = mShadowMaskTexture.Get();

    midBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    midBarriers[1].Transition.pResource = mShadowMaskTexture.Get();
    midBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    midBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    midBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    midBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    midBarriers[2].Transition.pResource = depthBuffer;
    midBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    midBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    midBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    cmdList->ResourceBarrier(3, midBarriers);
    mMaskTrackedState = (unsigned)D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    mStats.signalOutputsProduced += doDispatch ? 1 : 0;

    // Temporal denoising must consume this frame's freshly traced mask. The
    // old wiring ran reprojection before DispatchRays, which meant it only
    // reprojected the previous frame and its output was never composited.
    // Keep the operation here, between the DXR dispatch and the graphics
    // composite, so the final modulation uses the committed temporal mask.
    const bool temporalComposite = !forceRawSignal && TemporalManager::Instance().IsEnabled();
    if (temporalComposite) {
        D3D12_CPU_DESCRIPTOR_HANDLE maskSrvCpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        maskSrvCpu.ptr += 2 * mDescriptorSize;
        TemporalManager::Instance().DispatchReprojection(
            cmdList, mShadowMaskTexture.Get(), maskSrvCpu, width, height, mShadowSamples);
    }

    // ============================================================
    // COMPOSITE / DEBUG MASK PASS (Phase 2 diagnostic switch)
    // ============================================================
    if (!mCompositeEnabled) {
        LogCouchDxrShadow("[COUCH-DXR-SHADOW][DIAG] Composite SKIPPED (DXR_COMPOSITE_ENABLE=0)");
        return;
    }

    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetGraphicsRootSignature(mCompositeRootSig.Get());

    if (temporalComposite && TemporalManager::Instance().GetTemporalOutputBufferForTrace()) {
        ID3D12DescriptorHeap* temporalHeap = TemporalManager::Instance().GetSrvHeapForTrace();
        if (temporalHeap) {
            cmdList->SetDescriptorHeaps(1, &temporalHeap);
        }
    } else {
        ID3D12DescriptorHeap* dxrHeap = mDescriptorHeap.Get();
        cmdList->SetDescriptorHeaps(1, &dxrHeap);
    }

    // Enlazar SRV de la máscara (Slot 2 de mDescriptorHeap)
    D3D12_GPU_DESCRIPTOR_HANDLE maskSrvGpu = {};
    if (temporalComposite && TemporalManager::Instance().GetTemporalOutputBufferForTrace()) {
        maskSrvGpu = TemporalManager::Instance().GetTemporalOutputSrvGpuHandle();
    } else {
        maskSrvGpu = depthGpuHandle;
        maskSrvGpu.ptr += 2 * mDescriptorSize;
    }
    cmdList->SetGraphicsRootDescriptorTable(0, maskSrvGpu);

    float constants[4] = { shadowStrength, (shadowMode == 1) ? 1.0f : 0.0f, 0.0f, 0.0f };
    cmdList->SetGraphicsRoot32BitConstants(1, 4, constants, 0);

    const size_t compositeIndex = renderTarget->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1u : 0u;
    if (mWhiteCompositeTest && mWhiteCompositePso[compositeIndex]) {
        // Phase 3: White composite diagnostic — output float4(1,1,1,1) with same blend
        cmdList->SetPipelineState(mWhiteCompositePso[compositeIndex].Get());
        LogCouchDxrShadow("[COUCH-DXR-SHADOW][DIAG] WHITE_COMPOSITE_TEST PSO active");
    } else if (shadowMode == 1) {
        // Debug Mask: Blanco/Negro directo
        cmdList->SetPipelineState(mDebugMaskPso[compositeIndex].Get());
        mStats.debugMaskPassRecorded = true;
    } else {
        // Hybrid Composite: Modula color de raster
        cmdList->SetPipelineState(mCompositePso[compositeIndex].Get());
    }

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0); // Fullscreen Triangle
    ++mStats.compositePasses;

    if (mStats.dispatchRaysCalls == 1) {
        LogCouchDxrShadow("[COUCH-DXR] First DispatchRays SUCCESS! Resolution=" + std::to_string(width) + "x" + std::to_string(height));
    }
}

void DxrShadowRenderer::OnResize(uint32_t width, uint32_t height) {
    mShadowMaskTexture.Reset();
    mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
    mBoundTextureSrvIndices.clear();
    mBoundRasterSrvHeap = nullptr;
    mMaskWidth = 0;
    mMaskHeight = 0;
}

void DxrShadowRenderer::Reset() {
    mStats = DxrShadowStats();
}

void DxrShadowRenderer::DumpReport() const {
    LogCouchDxrShadow("[COUCH-DXR] ================= RT3 TELEMETRY =================");
    LogCouchDxrShadow("[COUCH-DXR] StateObject = " + std::string(mStats.stateObjectCreated ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] RayGen = " + std::string(mStats.rayGenLoaded ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] Miss = " + std::string(mStats.missLoaded ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] HitGroup = " + std::string(mStats.hitGroupLoaded ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] AnyHit = " + std::string(mStats.anyHitLoaded ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] ShaderTable = " + std::string(mStats.shaderTableReady ? "OK" : "FAIL"));
    LogCouchDxrShadow("[COUCH-DXR] Total DispatchRays = " + std::to_string(mStats.dispatchRaysCalls));
    LogCouchDxrShadow("[COUCH-DXR] RayGen invocations = " + std::to_string(mStats.rayGenInvocations));
    LogCouchDxrShadow("[COUCH-DXR] Resolution = " + std::to_string(mStats.shadowWidth) + "x" + std::to_string(mStats.shadowHeight));
    LogCouchDxrShadow("[COUCH-DXR] DIAGNOSTIC: dispatch=" + std::string(mDispatchEnabled ? "ON" : "OFF") +
                      " composite=" + std::string(mCompositeEnabled ? "ON" : "OFF") +
                      " whiteTest=" + std::string(mWhiteCompositeTest ? "ON" : "OFF"));
    LogCouchDxrShadow("[COUCH-DXR] Root bindings: t0(TLAS), t1(depth), t2(vertices), t3(indices), t4(materials), t5(textures), u0(mask), b0(frame), s0(linear-wrap).");
    LogCouchDxrShadow("[COUCH-DXR] =================================================");
}

} // namespace Fast

#endif
