#pragma once

#if defined(_WIN32)

#include <d3d12.h>
#include <algorithm>
#include <array>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "fast/backends/DxrAccelerationStructureManager.h"

namespace Fast {

struct DxrShadowStats {
    bool stateObjectCreated = false;
    bool rayGenLoaded = false;
    bool missLoaded = false;
    bool hitGroupLoaded = false;
    bool anyHitLoaded = false;
    bool shaderTableReady = false;
    bool tlasBound = false;
    bool depthSrvBound = false;
    bool shadowMaskUavBound = false;
    bool inverseProjectionValid = false;
    
    uint64_t dispatchRaysCalls = 0;
    uint64_t rayGenInvocations = 0;
    uint64_t signalOutputsProduced = 0;
    uint64_t compositePasses = 0;
    bool rayDispatchIssued = false;

    bool debugMaskPassRecorded = false;

    uint32_t uiShadowInteractions = 0; // HARD GATE: 0
    uint32_t dxrDebugErrors = 0;       // HARD GATE: 0

    uint32_t shadowWidth = 0;
    uint32_t shadowHeight = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    double dxrGpuTimeMs = -1.0;
    double frameGpuTimeMs = -1.0;
};

using DxrShadowFrameConstants = DxrFrameConstants;

class DxrShadowRenderer {
public:
    static DxrShadowRenderer& Instance();

    bool Init(ID3D12Device* device);
    bool IsSupported() const { return mSupported; }
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled && mInitialized && mSupported; }

    // Diagnostic switches (Phase 2): COUCH_DXR_DISPATCH_ENABLE / COUCH_DXR_COMPOSITE_ENABLE
    bool IsDispatchEnabled() const { return mDispatchEnabled; }
    bool IsCompositeEnabled() const { return mCompositeEnabled; }
    bool IsWhiteCompositeTest() const { return mWhiteCompositeTest; }
    void SetWhiteCompositeTest(bool on) { mWhiteCompositeTest = on; }

    void InvalidateCameraMatrices();
    void SetQualityScale(float scale) { mQualityScale = scale; }
    void SetQualitySamples(unsigned samples) { mShadowSamples = std::clamp(samples, 1u, 16u); }
    void UpdateCameraMatrices(const float view[4][4], const float proj[4][4], float clipXScale = 1.0f,
                              const float viewport[4] = nullptr);

    void RenderShadows(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                       ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                       ID3D12Resource* depthBuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                       D3D12_GPU_VIRTUAL_ADDRESS tlasGpuVA,
                       uint32_t width, uint32_t height,
                       uint32_t frameIndex, uint64_t fenceValue,
                       const DxrSurfaceFrameBuffers& surfaceData,
                       ID3D12DescriptorHeap* rasterSrvHeap,
                       uint32_t rasterSrvDescriptorSize,
                       bool forceDebugMask = false,
                       bool forceRawSignal = false);

    void OnResize(uint32_t width, uint32_t height);
    void Reset();

    const DxrShadowStats& GetStats() const { return mStats; }

    // Forensic phase 2: read-only trace accessors
    ID3D12Resource* GetShadowMaskForTrace() const { return mShadowMaskTexture.Get(); }
    unsigned GetShadowMaskStateForTrace() const { return mMaskTrackedState; }
    uint32_t GetMaskWidth() const { return mMaskWidth; }
    uint32_t GetMaskHeight() const { return mMaskHeight; }
    void DumpReport() const;

private:
    DxrShadowRenderer();
    ~DxrShadowRenderer();

    bool CreateRootSignature(ID3D12Device* device);
    bool CreateRaytracingPipelineState(ID3D12Device5* device5);
    bool CreateShaderTable(ID3D12Device* device);
    bool EnsureShadowMaskResources(ID3D12Device* device, uint32_t width, uint32_t height);
    bool CreateCompositePipeline(ID3D12Device* device);

    bool mInitialized;
    bool mSupported;
    bool mEnabled;

    Microsoft::WRL::ComPtr<ID3D12Device5> mDevice5;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGlobalRootSignature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> mStateObject;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> mStateObjectProperties;

    // Shader Binding Table (SBT)
    Microsoft::WRL::ComPtr<ID3D12Resource> mShaderTableBuffer;
    uint32_t mShaderRecordSize;
    uint32_t mRayGenRecordOffset;
    uint32_t mMissRecordOffset;
    uint32_t mHitGroupRecordOffset;

    // Constant Buffer para FrameConstants
    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantBuffer;
    uint8_t* mConstantBufferMapped;

    // Textura GPU de salida: Shadow Mask
    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMaskTexture;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorTemplate;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 3> mDescriptorFrames;
    uint32_t mDescriptorSize;
    uint32_t mMaskWidth;
    uint32_t mMaskHeight;
    std::vector<uint32_t> mBoundTextureSrvIndices;
    ID3D12DescriptorHeap* mBoundRasterSrvHeap = nullptr;

    // Forensic phase 2: tracked shadow mask resource state (mirror)
    unsigned mMaskTrackedState = (unsigned)D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Composite & Debug Pipeline (Raster Fullscreen)
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mCompositeRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mCompositePso[2];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDebugMaskPso[2];
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mWhiteCompositePso[2]; // Phase 3: diagnostic white output

    // Diagnostic switches (Phase 2)
    bool mDispatchEnabled = true;   // COUCH_DXR_DISPATCH_ENABLE (default: true)
    bool mCompositeEnabled = true;  // COUCH_DXR_COMPOSITE_ENABLE (default: true)
    bool mWhiteCompositeTest = false; // Phase 3: override composite to white

    // Matrices de cámara activas del frame
    float mViewMatrix[4][4];
    float mProjMatrix[4][4];
    float mInvProjectionMatrix[4][4];
    float mClipXScale = 1.0f;
    float mRasterViewport[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    bool mMatricesValid;
    float mQualityScale = 1.0f;
    unsigned mShadowSamples = 2;

    DxrShadowStats mStats;
};

} // namespace Fast

#endif
