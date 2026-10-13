#if defined(_WIN32)

#include "fast/backends/TemporalManager.h"
#include "fast/backends/DxrDescriptorRing.h"
#include "fast/couch_diagnostics.h"
#include "fast/backends/DxrShadowRenderer.h"
#include "fast/PersistentScene.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include "spdlog/spdlog.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace Fast {

static bool CouchDxrVerbose() {
    static const bool enabled = [] {
        const char* value = std::getenv("COUCH_DXR_VERBOSE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

static void LogTemporal(const std::string& line) {
    SPDLOG_INFO(line);
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) {
        ofs << line << "\n";
    }
}

typedef HRESULT(WINAPI* PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_TEMP)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static HRESULT DynamicSerializeRootSig(const D3D12_ROOT_SIGNATURE_DESC* pDesc,
                                        D3D_ROOT_SIGNATURE_VERSION version,
                                        ID3DBlob** ppBlob, ID3DBlob** ppError) {
    static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_TEMP sFunc = nullptr;
    if (!sFunc) {
        HMODULE h = GetModuleHandleA("d3d12.dll");
        if (!h) h = LoadLibraryA("d3d12.dll");
        if (h) sFunc = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_TEMP)GetProcAddress(h, "D3D12SerializeRootSignature");
    }
    if (sFunc) return sFunc(pDesc, version, ppBlob, ppError);
    return E_FAIL;
}

// ============================================================
// MATRIX UTILITIES
// ============================================================

void TemporalManager::MultiplyMatrices(float out[4][4], const float a[4][4], const float b[4][4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
}

bool TemporalManager::InvertMatrix4x4(const float m[4][4], float inv[4][4]) {
    float invTmp[16];
    const float* s = &m[0][0];

    invTmp[0]  =  s[5]*s[10]*s[15] - s[5]*s[11]*s[14] - s[9]*s[6]*s[15] + s[9]*s[7]*s[14] + s[13]*s[6]*s[11] - s[13]*s[7]*s[10];
    invTmp[4]  = -s[4]*s[10]*s[15] + s[4]*s[11]*s[14] + s[8]*s[6]*s[15] - s[8]*s[7]*s[14] - s[12]*s[6]*s[11] + s[12]*s[7]*s[10];
    invTmp[8]  =  s[4]*s[9]*s[15]  - s[4]*s[11]*s[13] - s[8]*s[5]*s[15] + s[8]*s[7]*s[13] + s[12]*s[5]*s[11] - s[12]*s[7]*s[9];
    invTmp[12] = -s[4]*s[9]*s[14]  + s[4]*s[10]*s[13] + s[8]*s[5]*s[14] - s[8]*s[6]*s[13] - s[12]*s[5]*s[10] + s[12]*s[6]*s[9];
    invTmp[1]  = -s[1]*s[10]*s[15] + s[1]*s[11]*s[14] + s[9]*s[2]*s[15] - s[9]*s[3]*s[14] - s[13]*s[2]*s[11] + s[13]*s[3]*s[10];
    invTmp[5]  =  s[0]*s[10]*s[15] - s[0]*s[11]*s[14] - s[8]*s[2]*s[15] + s[8]*s[3]*s[14] + s[12]*s[2]*s[11] - s[12]*s[3]*s[10];
    invTmp[9]  = -s[0]*s[9]*s[15]  + s[0]*s[11]*s[13] + s[8]*s[1]*s[15] - s[8]*s[3]*s[13] - s[12]*s[1]*s[11] + s[12]*s[3]*s[9];
    invTmp[13] =  s[0]*s[9]*s[14]  - s[0]*s[10]*s[13] - s[8]*s[1]*s[14] + s[8]*s[2]*s[13] + s[12]*s[1]*s[10] - s[12]*s[2]*s[9];
    invTmp[2]  =  s[1]*s[6]*s[15]  - s[1]*s[7]*s[14]  - s[5]*s[2]*s[15] + s[5]*s[3]*s[14] + s[13]*s[2]*s[7]  - s[13]*s[3]*s[6];
    invTmp[6]  = -s[0]*s[6]*s[15]  + s[0]*s[7]*s[14]  + s[4]*s[2]*s[15] - s[4]*s[3]*s[14] - s[12]*s[2]*s[7]  + s[12]*s[3]*s[6];
    invTmp[10] =  s[0]*s[5]*s[15]  - s[0]*s[7]*s[13]  - s[4]*s[1]*s[15] + s[4]*s[3]*s[13] + s[12]*s[1]*s[7]  - s[12]*s[3]*s[5];
    invTmp[14] = -s[0]*s[5]*s[14]  + s[0]*s[6]*s[13]  + s[4]*s[1]*s[14] - s[4]*s[2]*s[13] - s[12]*s[1]*s[6]  + s[12]*s[2]*s[5];
    invTmp[3]  = -s[1]*s[6]*s[11]  + s[1]*s[7]*s[10]  + s[5]*s[2]*s[11] - s[5]*s[3]*s[10] - s[9]*s[2]*s[7]   + s[9]*s[3]*s[6];
    invTmp[7]  =  s[0]*s[6]*s[11]  - s[0]*s[7]*s[10]  - s[4]*s[2]*s[11] + s[4]*s[3]*s[10] + s[8]*s[2]*s[7]   - s[8]*s[3]*s[6];
    invTmp[11] = -s[0]*s[5]*s[11]  + s[0]*s[7]*s[9]   + s[4]*s[1]*s[11] - s[4]*s[3]*s[9]  - s[8]*s[1]*s[7]   + s[8]*s[3]*s[5];
    invTmp[15] =  s[0]*s[5]*s[10]  - s[0]*s[6]*s[9]   - s[4]*s[1]*s[10] + s[4]*s[2]*s[9]  + s[8]*s[1]*s[6]   - s[8]*s[2]*s[5];

    float det = s[0]*invTmp[0] + s[1]*invTmp[4] + s[2]*invTmp[8] + s[3]*invTmp[12];
    if (fabsf(det) < 1e-8f) return false;

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; i++) {
        (&inv[0][0])[i] = invTmp[i] * invDet;
    }
    return true;
}

float TemporalManager::ComputePositionDelta(const float curr[4][4], const float prev[4][4]) {
    float dx = curr[3][0] - prev[3][0];
    float dy = curr[3][1] - prev[3][1];
    float dz = curr[3][2] - prev[3][2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

float TemporalManager::ComputeRotationDelta(const float curr[4][4], const float prev[4][4]) {
    float maxDiff = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float d = fabsf(curr[i][j] - prev[i][j]);
            if (d > maxDiff) maxDiff = d;
        }
    }
    return maxDiff;
}

// ============================================================
// SINGLETON & LIFECYCLE
// ============================================================

TemporalManager& TemporalManager::Instance() {
    static TemporalManager sInstance;
    return sInstance;
}

D3D12_GPU_DESCRIPTOR_HANDLE TemporalManager::GetTemporalOutputSrvGpuHandle() const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
    if (!mDescriptorHeap || mDescriptorSize == 0) return handle;
    handle = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(7) * mDescriptorSize;
    return handle;
}

TemporalManager::TemporalManager()
    : mEnabled(false), mInitialized(false), mDevice(nullptr), mCommandQueue(nullptr),
      mTemporalHistoryValid(false), mDebugMode(0),
      mCameraCutThreshold(5.0f),
      mMVWidth(0), mMVHeight(0),
      mCurrentHistoryIndex(0), mHistoryWidth(0), mHistoryHeight(0),
      mDescriptorSize(0), mDescriptorCount(0),
      mConstantBufferMapped(nullptr), mFrameCounter(0),
      mTrace(&TemporalTrace::Instance()) {
    memset(&mCurrentState, 0, sizeof(mCurrentState));
    memset(&mPreviousState, 0, sizeof(mPreviousState));
    memset(&mStats, 0, sizeof(mStats));
    memset(mLastPosition, 0, sizeof(mLastPosition));
    memset(mLastRotation, 0, sizeof(mLastRotation));
}

TemporalManager::~TemporalManager() {
    Shutdown();
}

bool TemporalManager::Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {
    TEMPORAL_TRACE_ENTER("Init");
    TEMPORAL_TRACE_MENABLED_READ("Init:start");
    
    if (mInitialized) {
        TEMPORAL_TRACE_EXIT("Init");
        return true;
    }
    if (!device || !commandQueue) {
        TEMPORAL_TRACE_EXIT("Init");
        return false;
    }

    mDevice = device;
    mCommandQueue = commandQueue;

    const char* envTemporal = getenv("COUCH_TEMPORAL");
    const bool explicitTemporalOn = envTemporal && strcmp(envTemporal, "1") == 0;
    const bool explicitTemporalOff = envTemporal && strcmp(envTemporal, "0") == 0;
    const bool hardwareDefault = !envTemporal && DxrShadowRenderer::Instance().IsEnabled();
    if (!explicitTemporalOff && (explicitTemporalOn || hardwareDefault)) {
        mEnabled = true;
        TEMPORAL_TRACE_MENABLED_WRITE(hardwareDefault ? "Init:hardware_default" : "Init:env", true);
        LogTemporal(std::string("[COUCH-TEMPORAL] Enabled via ") +
                    (hardwareDefault ? "supported-hardware default" : "environment"));
    }
    TEMPORAL_TRACE_ISENABLED("Init:after_env_check", mEnabled);

    // Crear Constant Buffer (256 bytes aligned)
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = kTemporalConstantStride * 6;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&mConstantBuffer)))) {
        LogTemporal("[COUCH-TEMPORAL] Failed to create constant buffer");
        TEMPORAL_TRACE_EXIT("Init");
        return false;
    }
    mConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mConstantBufferMapped));

    // Crear pipelines de compute
    if (!CreateRootSignature()) {
        LogTemporal("[COUCH-TEMPORAL] Failed to create root signature");
        TEMPORAL_TRACE_EXIT("Init");
        return false;
    }
    if (!CreateMotionVectorPipeline()) {
        LogTemporal("[COUCH-TEMPORAL] Failed to create motion vector pipeline");
        TEMPORAL_TRACE_EXIT("Init");
        return false;
    }
    if (!CreateReprojectionPipeline()) {
        LogTemporal("[COUCH-TEMPORAL] Failed to create reprojection pipeline");
        TEMPORAL_TRACE_EXIT("Init");
        return false;
    }

    mInitialized = true;
    mTemporalHistoryValid = false;

    LogTemporal("[COUCH-TEMPORAL] TemporalManager INITIALIZED OK");
    LogTemporal("[COUCH-TEMPORAL] Motion Vector Format: R16G16_FLOAT");
    LogTemporal("[COUCH-TEMPORAL] History Buffer Format: R8_UNORM (double-buffered)");
    LogTemporal("[COUCH-TEMPORAL] Camera Cut Threshold Position: " + std::to_string(mCameraCutThreshold));
    LogTemporal("[COUCH-TEMPORAL][TRACE] Init complete: mEnabled=" + std::to_string(mEnabled ? 1 : 0) +
                " cb=0x" + std::to_string(reinterpret_cast<uintptr_t>(mConstantBuffer.Get())) +
                " rootSig=0x" + std::to_string(reinterpret_cast<uintptr_t>(mMotionVectorRootSig.Get())) +
                " mvPso=0x" + std::to_string(reinterpret_cast<uintptr_t>(mMotionVectorPso.Get())) +
                " rpPso=0x" + std::to_string(reinterpret_cast<uintptr_t>(mReprojectionPso.Get())));

    TEMPORAL_TRACE_EXIT("Init");
    return true;
}

void TemporalManager::Shutdown() {
    if (mConstantBuffer && mConstantBufferMapped) {
        mConstantBuffer->Unmap(0, nullptr);
        mConstantBufferMapped = nullptr;
    }
    mMotionVectorBuffer.Reset();
    mMotionVectorReadback.Reset();
    mMotionValidityBuffer.Reset();
    for (uint32_t i = 0; i < kHistoryBufferCount; ++i) {
        mHistoryBuffers[i].Reset();
    }
    mTemporalOutputBuffer.Reset();
    mTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    mHistoryResourceStates[0] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mHistoryResourceStates[1] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
    mMotionVectorRootSig.Reset();
    mMotionVectorPso.Reset();
    mReprojectionRootSig.Reset();
    mReprojectionPso.Reset();
    mConstantBuffer.Reset();
    mDevice = nullptr;
    mCommandQueue = nullptr;
    mInitialized = false;
}

// ============================================================
// ROOT SIGNATURE (Shared for compute pipelines)
// ============================================================

bool TemporalManager::CreateRootSignature() {
    // Compute Root Signature:
    // [0] CBV b0 (TemporalConstants)
    // [1] Descriptor Table SRV t0 (Depth / CurrentShadow)
    // [2] Descriptor Table SRV t1 (History / MotionVector)
    // [3] Descriptor Table UAV u0 (Output: MotionVector / TemporalOutput)

    D3D12_ROOT_PARAMETER rootParams[4] = {};

    // b0: TemporalConstants
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // t0: SRV descriptor table
    D3D12_DESCRIPTOR_RANGE srvRange0 = {};
    srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0.NumDescriptors = 1;
    srvRange0.BaseShaderRegister = 0;
    srvRange0.RegisterSpace = 0;
    srvRange0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // t1/t2: motion-vector + history SRVs. Keeping them in one contiguous
    // table makes the same root signature usable by both compute passes;
    // the motion-vector pass simply leaves the unused t1/t2 entries alone.
    D3D12_DESCRIPTOR_RANGE srvRange1 = {};
    srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors = 2;
    srvRange1.BaseShaderRegister = 1;
    srvRange1.RegisterSpace = 0;
    srvRange1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange1;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // u0: UAV descriptor table
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 4;
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, err;
    if (FAILED(DynamicSerializeRootSig(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        if (err) {
            LogTemporal("[COUCH-TEMPORAL] Root sig error: " + std::string((char*)err->GetBufferPointer()));
        }
        return false;
    }

    return SUCCEEDED(mDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                   IID_PPV_ARGS(&mMotionVectorRootSig)));
}

// ============================================================
// MOTION VECTOR COMPUTE PIPELINE
// ============================================================

bool TemporalManager::CreateMotionVectorPipeline() {
    // HLSL Compute Shader: Generate camera motion vectors from depth buffer
    const char* csSrc =
        "cbuffer TemporalConstants : register(b0) {\n"
        "    row_major float4x4 gCurrentView;\n"
        "    row_major float4x4 gCurrentProj;\n"
        "    row_major float4x4 gCurrentViewProj;\n"
        "    row_major float4x4 gCurrentInvViewProj;\n"
        "    row_major float4x4 gPreviousView;\n"
        "    row_major float4x4 gPreviousProj;\n"
        "    row_major float4x4 gPreviousViewProj;\n"
        "    row_major float4x4 gPreviousInvViewProj;\n"
        "    float4 gScreenSize;\n"
        "    float4 gTemporalParams;\n"
        "    float4 gCameraCutParams;\n"
        "};\n"
        "\n"
        "Texture2D<float> gDepth : register(t0);\n"
        "RWTexture2D<float2> gMotionVectors : register(u0);\n"
        "\n"
        "[numthreads(8, 8, 1)]\n"
        "void CSMain(uint3 id : SV_DispatchThreadID) {\n"
        "    float2 uv = (float2(id.xy) + 0.5) * gScreenSize.zw;\n"
        "    \n"
        "    if (id.x >= (uint)gScreenSize.x || id.y >= (uint)gScreenSize.y) return;\n"
        "    \n"
        "    float depth = gDepth[id.xy];\n"
        "    \n"
        "    if (depth >= 1.0f) {\n"
        "        gMotionVectors[id.xy] = float2(0, 0);\n"
        "        return;\n"
        "    }\n"
        "    \n"
        "    // Reconstruct current world position from depth\n"
        "    float4 ndc = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth * 2.0f - 1.0f, 1.0f);\n"
        "    float4 worldPos = mul(ndc, gCurrentInvViewProj);\n"
        "    worldPos /= worldPos.w;\n"
        "    \n"
        "    // Project to previous frame screen space\n"
        "    float4 prevClip = mul(worldPos, gPreviousViewProj);\n"
        "    float2 prevUV = float2(prevClip.x / prevClip.w * 0.5f + 0.5f, 0.5f - prevClip.y / prevClip.w * 0.5f);\n"
        "    \n"
        "    // Motion vector = current - previous (displacement to find previous position)\n"
        "    float2 motion = uv - prevUV;\n"
        "    \n"
        "    // Clamp large motions (camera cut protection)\n"
        "    float motionLen = length(motion);\n"
        "    if (motionLen > 0.5f || gCameraCutParams.x > 0.5f) {\n"
        "        motion = float2(0, 0);\n"
        "    }\n"
        "    \n"
        "    gMotionVectors[id.xy] = motion;\n"
        "}\n";

    ComPtr<ID3DBlob> csBlob, errBlob;
    if (FAILED(D3DCompile(csSrc, strlen(csSrc), "MotionVectorCS", nullptr, nullptr,
                           "CSMain", "cs_5_0", 0, 0, &csBlob, &errBlob))) {
        if (errBlob) {
            LogTemporal("[COUCH-TEMPORAL] Motion Vector CS compile error: " +
                        std::string((char*)errBlob->GetBufferPointer()));
        }
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = mMotionVectorRootSig.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

    return SUCCEEDED(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mMotionVectorPso)));
}

// ============================================================
// REPROJECTION COMPUTE PIPELINE
// ============================================================

bool TemporalManager::CreateReprojectionPipeline() {
    // Both pipelines share the same root signature layout
    mReprojectionRootSig = mMotionVectorRootSig;

    const char* csSrc =
        "cbuffer TemporalConstants : register(b0) {\n"
        "    row_major float4x4 gCurrentView;\n"
        "    row_major float4x4 gCurrentProj;\n"
        "    row_major float4x4 gCurrentViewProj;\n"
        "    row_major float4x4 gCurrentInvViewProj;\n"
        "    row_major float4x4 gPreviousView;\n"
        "    row_major float4x4 gPreviousProj;\n"
        "    row_major float4x4 gPreviousViewProj;\n"
        "    row_major float4x4 gPreviousInvViewProj;\n"
        "    float4 gScreenSize;\n"
        "    float4 gTemporalParams;\n"
        "    float4 gCameraCutParams;\n"
        "};\n"
        "\n"
        "Texture2D<float> gCurrentShadow : register(t0);\n"
        "Texture2D<float2> gMotionVectors : register(t1);\n"
        "Texture2D<float> gPreviousHistory : register(t2);\n"
        "RWTexture2D<float> gTemporalOutput : register(u0);\n"
        "\n"
        "[numthreads(8, 8, 1)]\n"
        "void CSMain(uint3 id : SV_DispatchThreadID) {\n"
        "    if (id.x >= (uint)gScreenSize.x || id.y >= (uint)gScreenSize.y) return;\n"
        "    \n"
        "    float2 uv = (float2(id.xy) + 0.5) * gScreenSize.zw;\n"
        "    uint shadowW, shadowH; gCurrentShadow.GetDimensions(shadowW, shadowH);\n"
        "    uint2 shadowPixel = min(uint2(uv * float2(shadowW, shadowH)), uint2(shadowW, shadowH) - 1u);\n"
        "    float current = gCurrentShadow[shadowPixel];\n"
        "    float2 motion = gMotionVectors[id.xy];\n"
        "    \n"
        "    // Compute previous UV\n"
        "    float2 prevUV = uv - motion;\n"
        "    \n"
        "    // Bounds check: if previous UV is outside screen, reject history\n"
        "    if (prevUV.x < 0.0f || prevUV.x > 1.0f || prevUV.y < 0.0f || prevUV.y > 1.0f) {\n"
        "        gTemporalOutput[id.xy] = current;\n"
        "        return;\n"
        "    }\n"
        "    \n"
        "    // Reproject the actual previous denoised mask. A Load is used\n"
        "    // deliberately: the mask is a data signal, not a color texture,\n"
        "    // and this avoids requiring a sampler in the DXR/compute heap.\n"
        "    int2 previousPixel = int2(clamp(prevUV * gScreenSize.xy, 0.0f, gScreenSize.xy - 1.0f));\n"
        "    float history = gPreviousHistory.Load(int3(previousPixel, 0));\n"
        "    float historyBlend = gTemporalParams.x; // 0.0 = no history, 1.0 = full history\n"
        "    if (abs(current - history) > 0.35f) historyBlend = 0.0f;\n"
        "    \n"
        "    float result = lerp(current, history, historyBlend);\n"
        "    \n"
        "    gTemporalOutput[id.xy] = result;\n"
        "}\n";

    ComPtr<ID3DBlob> csBlob, errBlob;
    if (FAILED(D3DCompile(csSrc, strlen(csSrc), "ReprojectionCS", nullptr, nullptr,
                           "CSMain", "cs_5_0", 0, 0, &csBlob, &errBlob))) {
        if (errBlob) {
            LogTemporal("[COUCH-TEMPORAL] Reprojection CS compile error: " +
                        std::string((char*)errBlob->GetBufferPointer()));
        }
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = mReprojectionRootSig.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

    return SUCCEEDED(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mReprojectionPso)));
}

// ============================================================
// RESOURCE CREATION
// ============================================================

bool TemporalManager::EnsureResources(ID3D12Device* device, uint32_t width, uint32_t height) {
    if (!device) return false;

    bool needsCreate = (mMVWidth != width || mMVHeight != height);
    if (!needsCreate && mMotionVectorBuffer) return true;

    mMVWidth = width;
    mMVHeight = height;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Motion Vector Buffer: R16G16_FLOAT (UV-space velocity)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        mMotionVectorBuffer.Reset();
        HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                      IID_PPV_ARGS(&mMotionVectorBuffer));
        if (FAILED(hr)) {
            LogTemporal("[COUCH-TEMPORAL] Failed to create Motion Vector Buffer");
            return false;
        }
    }

    // Motion Validity Mask: R8_UINT (0=valid, 1=disoccluded, 2=camera-only, 3=invalid)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UINT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        mMotionValidityBuffer.Reset();
        device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                         IID_PPV_ARGS(&mMotionValidityBuffer));
    }

    // History Buffers: R8_UNORM (shadow mask history, double-buffered)
    if (!CreateHistoryBuffers(device, width, height)) {
        return false;
    }

    // Temporal Output: R8_UNORM
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        mTemporalOutputBuffer.Reset();
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                         IID_PPV_ARGS(&mTemporalOutputBuffer)))) {
            LogTemporal("[COUCH-TEMPORAL] Failed to create temporal output buffer");
            return false;
        }
        mTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Descriptor Heap: 8 slots
    // 0: Depth SRV (t0 for motion vectors)
    // 1: Current Shadow SRV (t0 for reprojection)
    // 2: Motion Vector SRV (t1 for reprojection)
    // 3: Motion Vector UAV (u0 for motion vectors)
    // 4: History A SRV
    // 5: History B SRV
    // 6: Temporal Output UAV (u0 for reprojection)
    // 7: Temporal Output SRV
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        mDescriptorHeap.Reset();
    mDescriptorTemplate.Reset();
    for (auto& heap : mDescriptorFrames) heap.Reset();
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)))) {
            return false;
        }
        InitializeDxrDescriptorTemplate(device,mDescriptorHeap.Get());
        mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        mDescriptorCount = 8;
    }

    mStats.motionVectorWidth = width;
    mStats.motionVectorHeight = height;

    LogTemporal("[COUCH-TEMPORAL] Resources created: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool TemporalManager::CreateHistoryBuffers(ID3D12Device* device, uint32_t width, uint32_t height) {
    mHistoryWidth = width;
    mHistoryHeight = height;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (uint32_t i = 0; i < kHistoryBufferCount; ++i) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        mHistoryBuffers[i].Reset();
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&mHistoryBuffers[i])))) {
            LogTemporal("[COUCH-TEMPORAL] Failed to create history buffer " + std::to_string(i));
            return false;
        }
    }

    mCurrentHistoryIndex = 0;
    mHistoryResourceStates[0] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mHistoryResourceStates[1] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    LogTemporal("[COUCH-TEMPORAL] History buffers created (double-buffered): " +
                std::to_string(width) + "x" + std::to_string(height));
    return true;
}

// ============================================================
// CAMERA STATE
// ============================================================

void TemporalManager::UpdateCameraMatrices(const float view[4][4], const float proj[4][4]) {
    // FORENSIC (phase-2 instrumentation fix): called ~258x/frame via DxrShadowRenderer;
    // per-call trace lines blew the trace log up to 3.3GB. Throttle to first 5 + every 600th.
    static int sUcmTraceCount = 0;
    const bool ucmTrace = (sUcmTraceCount < 5) || (sUcmTraceCount % 600 == 0);
    ++sUcmTraceCount;
    if (ucmTrace) {
        TEMPORAL_TRACE_ENTER("UpdateCameraMatrices");
        TEMPORAL_TRACE_MENABLED_READ("UpdateCameraMatrices:check");
    }
    
    memcpy(mPreviousState.viewMatrix, mCurrentState.viewMatrix, sizeof(mCurrentState.viewMatrix));
    memcpy(mPreviousState.projectionMatrix, mCurrentState.projectionMatrix, sizeof(mCurrentState.projectionMatrix));
    memcpy(mPreviousState.viewProjectionMatrix, mCurrentState.viewProjectionMatrix, sizeof(mCurrentState.viewProjectionMatrix));
    memcpy(mPreviousState.inverseViewProjectionMatrix, mCurrentState.inverseViewProjectionMatrix, sizeof(mCurrentState.inverseViewProjectionMatrix));
    mPreviousState.valid = mCurrentState.valid;

    memcpy(mCurrentState.viewMatrix, view, sizeof(float) * 16);
    if (mCurrentState.valid) {
        for (int r=0;r<4;++r) for (int c=0;c<4;++c)
            if (std::abs(mCurrentState.projectionMatrix[r][c]-proj[r][c])>0.01f) mTemporalHistoryValid=false;
    }
    memcpy(mCurrentState.projectionMatrix, proj, sizeof(float) * 16);

    float vp[4][4];
    MultiplyMatrices(vp, view, proj);
    memcpy(mCurrentState.viewProjectionMatrix, vp, sizeof(vp));

    float invVP[4][4];
    if (InvertMatrix4x4(vp, invVP)) {
        memcpy(mCurrentState.inverseViewProjectionMatrix, invVP, sizeof(invVP));
        mCurrentState.valid = true;
    } else {
        mCurrentState.valid = false;
    }

    // Camera cut detection
    if (mPreviousState.valid) {
        float posDelta = ComputePositionDelta(mCurrentState.viewMatrix, mPreviousState.viewMatrix);
        float rotDelta = ComputeRotationDelta(mCurrentState.viewMatrix, mPreviousState.viewMatrix);

        bool cameraCut = (posDelta > mCameraCutThreshold) || (rotDelta > 0.8f);

        if (cameraCut) {
            mTemporalHistoryValid = false;
            mStats.cameraCutsDetected++;
            if (mStats.cameraCutsDetected <= 5 || (mStats.cameraCutsDetected % 60) == 0) {
                LogTemporal("[COUCH-TEMPORAL] CAMERA CUT DETECTED | posDelta=" + std::to_string(posDelta) +
                            " rotDelta=" + std::to_string(rotDelta));
            }
            TEMPORAL_TRACE_DIVERGENCE("CAMERA_CUT");
        }
    }

    memcpy(mLastPosition, &mCurrentState.viewMatrix[3][0], sizeof(mLastPosition));
    memcpy(mLastRotation, mCurrentState.viewMatrix[0], sizeof(mLastRotation));
    if (CouchDxrVerbose() && mStats.cameraCutsDetected <= 3) {
        LogTemporal("[COUCH-TEMPORAL][TRACE] UpdateCameraMatrices frame=" + std::to_string(mFrameCounter) +
                    " mEnabled=" + std::to_string(mEnabled ? 1 : 0) +
                    " valid=" + std::to_string(mCurrentState.valid ? 1 : 0) +
                    " cutCount=" + std::to_string(mStats.cameraCutsDetected));
    }

    if (ucmTrace) {
        TEMPORAL_TRACE_EXIT("UpdateCameraMatrices");
    }
}

void TemporalManager::OnResize(uint32_t width, uint32_t height) {
    mTemporalHistoryValid = false;
    mStats.temporalHistoryValid = false;
    mStats.resizeEventsDetected++;
    LogTemporal("[COUCH-TEMPORAL] Resize detected: " + std::to_string(width) + "x" + std::to_string(height) +
                " | History INVALIDATED");
}

void TemporalManager::OnSceneChange() {
    mTemporalHistoryValid = false;
    mStats.temporalHistoryValid = false;
    mStats.sceneChangesDetected++;

    // Reset history buffers
    mCurrentHistoryIndex = 0;

    LogTemporal("[COUCH-TEMPORAL] SCENE CHANGE: History buffers reset");
}

// ============================================================
// FRAME LIFECYCLE
// ============================================================

void TemporalManager::BeginFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex) {
    mCurrentFrameIndex = frameIndex % 3;
    TEMPORAL_TRACE_ENTER("BeginFrame");
    TEMPORAL_TRACE_MENABLED_READ("BeginFrame:check");
    
    if (!mEnabled || !mInitialized) {
        TEMPORAL_TRACE_EXIT("BeginFrame");
        return;
    }

    mFrameCounter++;
    mStats.framesProcessed = mFrameCounter;

    if (mFrameCounter <= 5 || mFrameCounter % 300 == 0) {
        LogTemporal("[COUCH-TEMPORAL][TRACE] BeginFrame frame=" + std::to_string(mFrameCounter) +
                    " cmdList=0x" + std::to_string(reinterpret_cast<uintptr_t>(cmdList)) +
                    " historyValid=" + std::to_string(mTemporalHistoryValid ? 1 : 0) +
                    " historyIdx=" + std::to_string(mCurrentHistoryIndex));
    }
    
    TEMPORAL_TRACE_EXIT("BeginFrame");
}

void TemporalManager::EndFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex) {
    TEMPORAL_TRACE_ENTER("EndFrame");
    TEMPORAL_TRACE_MENABLED_READ("EndFrame:check");
    
    if (!mEnabled || !mInitialized) {
        TEMPORAL_TRACE_EXIT("EndFrame");
        return;
    }

    bool wasValid = mTemporalHistoryValid;

    // Swap history buffers al final del frame
    if (mTemporalHistoryValid) {
        SwapHistoryBuffers();
    }

    if (mFrameCounter <= 5 || mFrameCounter % 300 == 0) {
        LogTemporal("[COUCH-TEMPORAL][TRACE] EndFrame frame=" + std::to_string(mFrameCounter) +
                    " historyValid=" + std::to_string(wasValid ? 1 : 0) +
                    " historyIdx=" + std::to_string(mCurrentHistoryIndex) +
                    " cmdList=0x" + std::to_string(reinterpret_cast<uintptr_t>(cmdList)));
    }
    
    TEMPORAL_TRACE_EXIT("EndFrame");
}

void TemporalManager::SwapHistoryBuffers() {
    mCurrentHistoryIndex = 1 - mCurrentHistoryIndex;
}

// ============================================================
// MOTION VECTOR DISPATCH
// ============================================================

void TemporalManager::DispatchMotionVectors(ID3D12GraphicsCommandList* cmdList,
                                             ID3D12Resource* depthBuffer,
                                             D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                                             uint32_t width, uint32_t height) {
    TEMPORAL_TRACE_ENTER("DispatchMotionVectors");
    TEMPORAL_TRACE_MENABLED_READ("DispatchMotionVectors:check");
    TEMPORAL_TRACE_GPU_BEFORE("DispatchMotionVectors:start", cmdList, mMotionVectorPso.Get(), mMotionVectorRootSig.Get(), mDescriptorHeap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mMotionVectorBuffer.Get());
    
    mDescriptorPhase = 0;
    if (!mEnabled || !mInitialized || !cmdList) {
        TEMPORAL_TRACE_EXIT("DispatchMotionVectors");
        return;
    }
    if (!mMotionVectorPso || !mMotionVectorRootSig) {
        TEMPORAL_TRACE_EXIT("DispatchMotionVectors");
        return;
    }

    if (!EnsureResources(mDevice, width, height)) {
        TEMPORAL_TRACE_EXIT("DispatchMotionVectors");
        return;
    }
    if (!mMotionVectorBuffer) {
        TEMPORAL_TRACE_EXIT("DispatchMotionVectors");
        return;
    }

    // Update constant buffer
    if (mConstantBufferMapped) {
        TemporalConstants tc = {};
        memcpy(tc.currentView, mCurrentState.viewMatrix, sizeof(tc.currentView));
        memcpy(tc.currentProjection, mCurrentState.projectionMatrix, sizeof(tc.currentProjection));
        memcpy(tc.currentViewProjection, mCurrentState.viewProjectionMatrix, sizeof(tc.currentViewProjection));
        memcpy(tc.currentInverseViewProjection, mCurrentState.inverseViewProjectionMatrix, sizeof(tc.currentInverseViewProjection));
        memcpy(tc.previousView, mPreviousState.viewMatrix, sizeof(tc.previousView));
        memcpy(tc.previousProjection, mPreviousState.projectionMatrix, sizeof(tc.previousProjection));
        memcpy(tc.previousViewProjection, mPreviousState.viewProjectionMatrix, sizeof(tc.previousViewProjection));
        memcpy(tc.previousInverseViewProjection, mPreviousState.inverseViewProjectionMatrix, sizeof(tc.previousInverseViewProjection));
        tc.screenSize[0] = static_cast<float>(width);
        tc.screenSize[1] = static_cast<float>(height);
        tc.screenSize[2] = 1.0f / static_cast<float>(width);
        tc.screenSize[3] = 1.0f / static_cast<float>(height);
        // The first frame (or a camera cut) is current-only. Once a valid
        // previous mask exists, retain most of it and let the reprojection
        // shader reject large signal changes as disocclusions.
        tc.temporalParams[0] = mTemporalHistoryValid ? 0.85f : 0.0f;
        tc.temporalParams[1] = 0.1f; // rejectionThreshold
        tc.temporalParams[2] = static_cast<float>(mDebugMode);
        tc.temporalParams[3] = static_cast<float>(mFrameCounter);
        tc.cameraCutParams[0] = mTemporalHistoryValid ? 0.0f : 1.0f;

        memcpy(mConstantBufferMapped + (mCurrentFrameIndex * 2 + mDescriptorPhase) * kTemporalConstantStride, &tc, sizeof(tc));
    }

    // Transition depth buffer for reading
    D3D12_RESOURCE_BARRIER preBarrier = {};
    preBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    preBarrier.Transition.pResource = depthBuffer;
    preBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    preBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    preBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &preBarrier);

    // Setup descriptor heap
    SelectDxrDescriptorHeap(mDevice, mDescriptorHeap, mDescriptorTemplate, mDescriptorFrames, mCurrentFrameIndex * 2 + mDescriptorPhase);
    ID3D12DescriptorHeap* heaps[] = { mDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(mMotionVectorRootSig.Get());
    cmdList->SetPipelineState(mMotionVectorPso.Get());

    // b0: Constant Buffer
    cmdList->SetComputeRootConstantBufferView(0, mConstantBuffer->GetGPUVirtualAddress() + (mCurrentFrameIndex * 2 + mDescriptorPhase) * kTemporalConstantStride);

    // t0: Depth SRV -> Slot 0
    D3D12_CPU_DESCRIPTOR_HANDLE slot0Cpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CopyDescriptorsSimple(1, slot0Cpu, depthSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetComputeRootDescriptorTable(1, srvGpu);

    // u0: Motion Vector UAV -> Slot 3
    D3D12_UNORDERED_ACCESS_VIEW_DESC mvUavDesc = {};
    mvUavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    mvUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE mvUavCpu = slot0Cpu;
    mvUavCpu.ptr += 3 * mDescriptorSize;
    mDevice->CreateUnorderedAccessView(mMotionVectorBuffer.Get(), nullptr, &mvUavDesc, mvUavCpu);

    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = srvGpu;
    uavGpu.ptr += 3 * mDescriptorSize;
    cmdList->SetComputeRootDescriptorTable(3, uavGpu);

    // Dispatch
    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Barrier: motion vectors ready
    D3D12_RESOURCE_BARRIER mvBarrier = {};
    mvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    mvBarrier.UAV.pResource = mMotionVectorBuffer.Get();
    cmdList->ResourceBarrier(1, &mvBarrier);

    // Restore depth buffer
    D3D12_RESOURCE_BARRIER postBarrier = preBarrier;
    postBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    postBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cmdList->ResourceBarrier(1, &postBarrier);

    TEMPORAL_TRACE_GPU_AFTER("DispatchMotionVectors:end", cmdList, mMotionVectorPso.Get(), mMotionVectorRootSig.Get(), mDescriptorHeap.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mMotionVectorBuffer.Get());
    TEMPORAL_TRACE_EXIT("DispatchMotionVectors");
}

// ============================================================
// REPROJECTION DISPATCH
// ============================================================

void TemporalManager::DispatchReprojection(ID3D12GraphicsCommandList* cmdList,
                                            ID3D12Resource* currentShadow,
                            			    D3D12_CPU_DESCRIPTOR_HANDLE currentShadowSrvCpuHandle,
                                            uint32_t width, uint32_t height,
                                            uint32_t currentSampleCount) {
    TEMPORAL_TRACE_ENTER("DispatchReprojection");
    TEMPORAL_TRACE_MENABLED_READ("DispatchReprojection:check");
    mDescriptorPhase = 1;
    float historyBlend = 0.0f;
    if (mConstantBufferMapped) {
        auto* reprojectionConstants = reinterpret_cast<TemporalConstants*>(
            mConstantBufferMapped + (mCurrentFrameIndex * 2 + 1) * kTemporalConstantStride);
        memcpy(reprojectionConstants,
               mConstantBufferMapped + (mCurrentFrameIndex * 2) * kTemporalConstantStride,
               sizeof(TemporalConstants));
        const float sampleCount = static_cast<float>(std::max(1u, currentSampleCount));
        historyBlend = mTemporalHistoryValid ? 0.85f / sqrtf(sampleCount) : 0.0f;
        reprojectionConstants->temporalParams[0] = historyBlend;
    }
    if (CouchDxrVerbose()) {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "[COUCH-FORENSIC][REPROJ_BIND] resource=0x%p srvHandle=0x%llX", (void*)currentShadow, (unsigned long long)currentShadowSrvCpuHandle.ptr);
        LogTemporal(dbg);
    }
    if (!mEnabled || !mInitialized || !cmdList) { TEMPORAL_TRACE_EXIT("DispatchReprojection"); return; }
    if (!mReprojectionPso || !mReprojectionRootSig) { TEMPORAL_TRACE_EXIT("DispatchReprojection"); return; }

    if (!currentShadow || !mMotionVectorBuffer || !mTemporalOutputBuffer ||
        !mHistoryBuffers[0] || !mHistoryBuffers[1]) {
        TEMPORAL_TRACE_EXIT("DispatchReprojection");
        return;
    }

    // The DXR renderer leaves the current mask as a pixel-readable resource.
    // Reprojection is a compute pass, so make that transition explicit and
    // restore it before the graphics composite.
    D3D12_RESOURCE_BARRIER shadowReadBarrier = {};
    shadowReadBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    shadowReadBarrier.Transition.pResource = currentShadow;
    shadowReadBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    shadowReadBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    shadowReadBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &shadowReadBarrier);

    // Motion vectors are produced as a UAV immediately before reprojection.
    // Transition them explicitly while this compute pass samples them, then
    // restore UAV state for the next frame's motion-vector dispatch.
    D3D12_RESOURCE_BARRIER motionReadBarrier = {};
    motionReadBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    motionReadBarrier.Transition.pResource = mMotionVectorBuffer.Get();
    motionReadBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    motionReadBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    motionReadBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &motionReadBarrier);

    if (mTemporalOutputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER outputToUav = {};
        outputToUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        outputToUav.Transition.pResource = mTemporalOutputBuffer.Get();
        outputToUav.Transition.StateBefore = mTemporalOutputState;
        outputToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        outputToUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &outputToUav);
        mTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Setup descriptor heap
    SelectDxrDescriptorHeap(mDevice, mDescriptorHeap, mDescriptorTemplate, mDescriptorFrames, mCurrentFrameIndex * 2 + mDescriptorPhase);
    ID3D12DescriptorHeap* heaps[] = { mDescriptorHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(mReprojectionRootSig.Get());
    cmdList->SetPipelineState(mReprojectionPso.Get());

    // b0: Constant Buffer
    cmdList->SetComputeRootConstantBufferView(0, mConstantBuffer->GetGPUVirtualAddress() + (mCurrentFrameIndex * 2 + mDescriptorPhase) * kTemporalConstantStride);

    // t0: Current Shadow SRV -> Slot 1
    D3D12_CPU_DESCRIPTOR_HANDLE slot0Cpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE shadowSrvSlot = slot0Cpu;
    shadowSrvSlot.ptr += 1 * mDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowView{};
    shadowView.Format=DXGI_FORMAT_R8_UNORM; shadowView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; shadowView.Texture2D.MipLevels=1;
    mDevice->CreateShaderResourceView(currentShadow,&shadowView,shadowSrvSlot);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE t0Gpu = srvGpu;
    t0Gpu.ptr += 1 * mDescriptorSize;
    cmdList->SetComputeRootDescriptorTable(1, t0Gpu);

    // t1: Motion Vector SRV -> Slot 2
    D3D12_SHADER_RESOURCE_VIEW_DESC mvSrvDesc = {};
    mvSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    mvSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    mvSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    mvSrvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE mvSrvSlot = slot0Cpu;
    mvSrvSlot.ptr += 2 * mDescriptorSize;
    mDevice->CreateShaderResourceView(mMotionVectorBuffer.Get(), &mvSrvDesc, mvSrvSlot);

    D3D12_GPU_DESCRIPTOR_HANDLE t1Gpu = srvGpu;
    t1Gpu.ptr += 2 * mDescriptorSize;
    cmdList->SetComputeRootDescriptorTable(2, t1Gpu);

    // t2: previous denoised mask -> Slot 3
    D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc = {};
    historySrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    historySrvDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE historySrvSlot = slot0Cpu;
    historySrvSlot.ptr += 3 * mDescriptorSize;
    ID3D12Resource* previousHistory = mHistoryBuffers[mCurrentHistoryIndex].Get();
    mDevice->CreateShaderResourceView(previousHistory, &historySrvDesc, historySrvSlot);

    // u0: Temporal Output UAV -> Slot 6
    D3D12_UNORDERED_ACCESS_VIEW_DESC outUavDesc = {};
    outUavDesc.Format = DXGI_FORMAT_R8_UNORM;
    outUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE outUavSlot = slot0Cpu;
    outUavSlot.ptr += 6 * mDescriptorSize;
    mDevice->CreateUnorderedAccessView(mTemporalOutputBuffer.Get(), nullptr, &outUavDesc, outUavSlot);

    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = srvGpu;
    uavGpu.ptr += 6 * mDescriptorSize;
    cmdList->SetComputeRootDescriptorTable(3, uavGpu);

    // Dispatch
    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Barrier after the reprojection write.
    D3D12_RESOURCE_BARRIER outBarrier = {};
    outBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    outBarrier.UAV.pResource = mTemporalOutputBuffer.Get();
    cmdList->ResourceBarrier(1, &outBarrier);

    // Persist the denoised mask into the other history buffer. The next
    // frame swaps indices in EndFrame(), so the shader always reads a fully
    // completed previous frame and never aliases its write target.
    const uint32_t writeHistoryIndex = 1u - mCurrentHistoryIndex;
    ID3D12Resource* writeHistory = mHistoryBuffers[writeHistoryIndex].Get();

    D3D12_RESOURCE_BARRIER outputToCopy = {};
    outputToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    outputToCopy.Transition.pResource = mTemporalOutputBuffer.Get();
    outputToCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    outputToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    outputToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    D3D12_RESOURCE_BARRIER historyToCopy = {};
    historyToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    historyToCopy.Transition.pResource = writeHistory;
    historyToCopy.Transition.StateBefore = mHistoryResourceStates[writeHistoryIndex];
    historyToCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    historyToCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &outputToCopy);
    cmdList->ResourceBarrier(1, &historyToCopy);
    cmdList->CopyResource(writeHistory, mTemporalOutputBuffer.Get());

    D3D12_RESOURCE_BARRIER historyFromCopy = historyToCopy;
    historyFromCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    historyFromCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &historyFromCopy);
    mHistoryResourceStates[writeHistoryIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    D3D12_RESOURCE_BARRIER outputToSrv = outputToCopy;
    outputToSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    outputToSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &outputToSrv);
    mTemporalOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Slot 7 is the SRV consumed by DxrShadowRenderer's composite pass.
    D3D12_CPU_DESCRIPTOR_HANDLE outputSrvSlot = slot0Cpu;
    outputSrvSlot.ptr += 7 * mDescriptorSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC outputSrvDesc = {};
    outputSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    outputSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    outputSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    outputSrvDesc.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mTemporalOutputBuffer.Get(), &outputSrvDesc, outputSrvSlot);

    const bool historyWasValid = mTemporalHistoryValid;
    mTemporalHistoryValid = true;
    mStats.temporalHistoryValid = true;
    if (CouchDxrVerbose()) {
        LogTemporal("[COUCH-TEMPORAL] Reprojection committed frame=" + std::to_string(mFrameCounter) +
                    " historyIn=" + std::to_string(mCurrentHistoryIndex) +
                    " historyOut=" + std::to_string(writeHistoryIndex) +
                    " blend=" + std::to_string(historyWasValid ? historyBlend : 0.0f) +
                    " samples=" + std::to_string(currentSampleCount));
    }

    // Restore the DXR mask to its graphics-readable state before the caller
    // binds the composite PSO.
    D3D12_RESOURCE_BARRIER shadowRestoreBarrier = shadowReadBarrier;
    shadowRestoreBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    shadowRestoreBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &shadowRestoreBarrier);

    D3D12_RESOURCE_BARRIER motionRestoreBarrier = motionReadBarrier;
    motionRestoreBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    motionRestoreBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdList->ResourceBarrier(1, &motionRestoreBarrier);

    // Forensic phase 2: composite capture inside the reprojection interval
    InvokeCompositeCapture((int)mFrameCounter);

    TEMPORAL_TRACE_EXIT("DispatchReprojection");
}

} // namespace Fast

#endif
