#pragma once

#if defined(_WIN32)

#include <windows.h>
#include <d3d12.h>
#include <array>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include "fast/backends/DxrAccelerationStructureManager.h"

namespace Fast {

// DXR TRUTH MODE (V1) — single authoritative contract for diagnostic views.
// diagnosticView 0 is OFF (exact normal production path). Values 1..4 are the
// pre-existing composite signal views, 5..8 the pre-existing primary-gate
// chain. Truth views live at 10..15 so they can never alias gate views.
// View 15 (V2, GAME_LIGHT_LAMBERT) reuses the V1 14 Lambert machinery with
// L = real game probe; productive lighting is untouched.
// Truth views reuse the raygen material-debug encodings (gLightDir.w) and add
// composite-only decoding branches; they never touch traversal, AnyHit
// filtering, blend policy, counters, root signature, or DXIL.
enum class DxrTruthView : int {
    Off = 0,
    PrimaryHitMiss = 10,
    WorldNormal = 11,
    HitDistance = 12,
    InstanceIds = 13,
    SyntheticLambert = 14,
    GameLightLambert = 15, // DXR TRUTH (V2): same Lambert as 14, L = real game probe
};

constexpr bool IsDxrTruthView(int view) {
    return view >= static_cast<int>(DxrTruthView::PrimaryHitMiss) &&
           view <= static_cast<int>(DxrTruthView::GameLightLambert);
}

// Raygen material-debug encoding (MaterialDebugColor mode) forced while a
// Truth view is active. Deliberately avoids mode 1: mode 1 fans the primary
// ray into per-band origin variants, while every mode used here keeps the
// exact OFF ray (variant 0), so the trace is bit-identical to OFF.
constexpr int DxrTruthMaterialDebugMode(int view) {
    return view == static_cast<int>(DxrTruthView::PrimaryHitMiss) ? 4 :   // roughness: grayscale, never magenta
           view == static_cast<int>(DxrTruthView::WorldNormal) ? 3 :      // normalWS * 0.5 + 0.5
           view == static_cast<int>(DxrTruthView::HitDistance) ? 7 :      // saturate(RayTCurrent / 256)
           view == static_cast<int>(DxrTruthView::InstanceIds) ? 9 :      // deterministic instance hash
            view == static_cast<int>(DxrTruthView::SyntheticLambert) ? 3 : // normalWS, lambert done in composite
            view == static_cast<int>(DxrTruthView::GameLightLambert) ? 3 : // normalWS, game lambert in composite
            -1;
}

constexpr const char* DxrTruthViewName(int view) {
    return view == static_cast<int>(DxrTruthView::PrimaryHitMiss) ? "TRUTH_PRIMARY_HIT_MISS" :
           view == static_cast<int>(DxrTruthView::WorldNormal) ? "TRUTH_WORLD_NORMAL" :
           view == static_cast<int>(DxrTruthView::HitDistance) ? "TRUTH_HIT_DISTANCE" :
           view == static_cast<int>(DxrTruthView::InstanceIds) ? "TRUTH_IDS" :
            view == static_cast<int>(DxrTruthView::SyntheticLambert) ? "TRUTH_SYNTHETIC_LAMBERT" :
            view == static_cast<int>(DxrTruthView::GameLightLambert) ? "TRUTH_GAME_LIGHT_LAMBERT" :
            view == 0 ? "TRUTH_OFF" : "UNKNOWN";
}

// Distance normalization for TRUTH_HIT_DISTANCE. Must match fc.params[1]
// (gParams.y = 256) consumed by MaterialDebugColor mode 7.
constexpr float kDxrTruthDistanceScale = 256.0f;

struct DxrSurfaceStats {
    bool stateObjectCreated = false;
    bool shaderTableReady = false;
    bool tlasBound = false;
    bool depthSrvBound = false;
    bool emissiveMaskBound = false;
    bool indirectMaskBound = false;
    bool reflectionColorBound = false;
    bool dispatchActive = false;
    uint64_t dispatchRaysCalls = 0;
    uint64_t approxRaysDispatched = 0;
    uint64_t signalOutputsProduced = 0;
    // OCZ-RTX-REPAIR REPAIR-02: fullscreen composite executions this session.
    // The scene harness matches COMPOSITE_EXECUTED log markers (GATE-B) and
    // this counter proves the DXR signal reached the framebuffer.
    uint64_t compositeExecutions = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// A separate surface pass keeps reflection, water, and emissive output out of
// the original Fast3D color/depth payload. It consumes the same TLAS and depth
// buffer as DXR shadows and composites material-selective signals over a copy
// of the already-rendered scene.
class DxrSurfaceRenderer {
public:
    static DxrSurfaceRenderer& Instance();

    bool Init(ID3D12Device* device);
    bool IsSupported() const { return mSupported; }
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled && mInitialized && mSupported; }

    void InvalidateCameraMatrices() { mMatricesValid = false; }
    void SetQuality(float scale, unsigned samples) { mQualityScale = scale; mIndirectSamples = samples; }
    void UpdateCameraMatrices(const float view[4][4], const float proj[4][4], float clipXScale = 1.0f,
                              const float viewport[4] = nullptr);

    void RenderReflections(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                           ID3D12Resource* renderTarget, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                           ID3D12Resource* depthBuffer, D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                           D3D12_GPU_VIRTUAL_ADDRESS tlasGpuVA,
                           uint32_t width, uint32_t height,
                           uint32_t frameIndex, uint64_t fenceValue,
                           const DxrSurfaceFrameBuffers& surfaceData,
                           ID3D12DescriptorHeap* rasterTextureHeap,
                           uint32_t rasterTextureDescriptorSize,
                           int diagnosticView = 0,
                           int diagnosticFrame = -1,
                           int sceneId = -1,
                           int roomId = -1,
                           uint32_t tlasInstanceCount = 0);

    // Resolve a single queued gate-counter sample after the command list that
    // dispatched it has been submitted. This waits only on the dedicated
    // counter fence and is a no-op on ordinary frames.
    void ResolvePrimaryGateCounters(ID3D12CommandQueue* queue);

    void OnResize(uint32_t width, uint32_t height);
    const DxrSurfaceStats& GetStats() const { return mStats; }
    ID3D12Resource* GetReflectionMask() const { return mReflectionMask.Get(); }
    ID3D12Resource* GetReflectionColorForCapture() const { return mReflectionColor.Get(); }
    ID3D12Resource* GetEmissiveMaskForCapture() const { return mEmissiveMask.Get(); }
    ID3D12Resource* GetIndirectMaskForCapture() const { return mIndirectMask.Get(); }
    ID3D12Resource* GetNativePointDirectForCapture() const { return mNativePointDirect.Get(); }
    bool IsNaviPilotDispatch() const { return mNaviPilotDispatch; }
    ID3D12Resource* GetRasterSceneColorCopyForCapture() const { return mSceneColorCopy.Get(); }
    ID3D12Resource* GetVisualAblationForCapture(unsigned index, int frame) const {
        return index < 4 && frame == mVisualAblationFrame ? mVisualAblationTargets[index].Get() : nullptr;
    }
    unsigned GetReflectionMaskState() const { return mMaskTrackedState; }
    unsigned GetReflectionColorState() const { return mReflectionColorTrackedState; }
    unsigned GetEmissiveMaskState() const { return mEmissiveTrackedState; }
    unsigned GetIndirectMaskState() const { return mIndirectTrackedState; }
    unsigned GetNativePointDirectState() const { return mNativePointDirectTrackedState; }
    unsigned GetSceneColorCopyState() const { return mSceneColorTrackedState; }

private:
    DxrSurfaceRenderer();
    ~DxrSurfaceRenderer();

    bool CreateRootSignature(ID3D12Device* device);
    bool CreateRaytracingPipelineState(ID3D12Device5* device5);
    bool CreateShaderTable(ID3D12Device* device);
    bool CreatePathTemporalPipeline(ID3D12Device* device);
    bool EnsureResources(ID3D12Device* device, uint32_t maskWidth, uint32_t maskHeight,
                         uint32_t sceneWidth, uint32_t sceneHeight, DXGI_FORMAT sceneFormat);
    bool EnsurePrimaryGateCounterResources(ID3D12Device* device);
    bool EnsurePathTemporalResources(ID3D12Device* device, uint32_t width, uint32_t height);
    bool DispatchPathTemporal(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depthBuffer,
                              D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                              uint32_t width, uint32_t height, uint32_t screenWidth,
                              uint32_t screenHeight, uint32_t frameIndex);
    bool CreateCompositePipeline(ID3D12Device* device, DXGI_FORMAT outputFormat);

    bool mInitialized = false;
    bool mSupported = false;
    bool mEnabled = false;

    Microsoft::WRL::ComPtr<ID3D12Device5> mDevice5;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGlobalRootSignature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> mStateObject;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> mStateObjectProperties;

    Microsoft::WRL::ComPtr<ID3D12Resource> mShaderTableBuffer;
    uint32_t mShaderRecordSize = 64;
    uint32_t mRayGenRecordOffset = 0;
    uint32_t mMissRecordOffset = 256;
    uint32_t mHitGroupRecordOffset = 512;

    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantBuffer;
    uint8_t* mConstantBufferMapped = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> mReflectionMask;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrimaryGateCounters;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrimaryGateReadback;
    Microsoft::WRL::ComPtr<ID3D12Fence> mPrimaryGateFence;
    HANDLE mPrimaryGateFenceEvent = nullptr;
    uint64_t mPrimaryGateFenceValue = 0;
    bool mPrimaryGateReadbackPending = false;
    bool mNaviPilotDispatch = false;
    bool mNaviPilotCaptured = false;
    bool mNaviPilotMetadataCaptured = false;
    bool mNaviPilotPending = false;
    std::vector<DxrSurfaceIdentity> mNaviPendingIdentities;
    int mPrimaryGatePendingFrame = -1;
    int mPrimaryGatePendingScene = -1;
    int mPrimaryGatePendingRoom = -1;
    uint32_t mPrimaryGatePendingTlasInstances = 0;
    uint32_t mPrimaryGatePendingWidth = 0;
    uint32_t mPrimaryGatePendingHeight = 0;
    uint64_t mPrimaryGatePendingViewHash = 0;
    uint64_t mPrimaryGatePendingProjectionHash = 0;
    uintptr_t mPrimaryGatePendingDepthIdentity = 0;
    std::vector<DxrInstanceMaterialData> mPrimaryGatePendingMaterialRecords;
    bool mPrimaryGatePendingBypassBlend = false;
    bool mPrimaryGatePendingBypassVertexAlpha = false;
    bool mPrimaryGateCountersEnabled = false;
    std::array<uint64_t, 2112> mPrimaryGateNormalCounts = {};
    std::array<uint64_t, 2112> mPrimaryGateCandidateCounts = {};
    bool mPrimaryGatePendingCandidatePolicy = false;
    bool mPrimaryGateNormalSampleReady = false;
    bool mPrimaryGateNormalInvariantsPass = false;
    // Full DXR radiance output. The scalar mask remains separate so the
    // composite can control reflection strength without losing the actual
    // hit/miss color returned by the ray.
    Microsoft::WRL::ComPtr<ID3D12Resource> mReflectionColor;
    // Separate material-aware emission signal. Bloom never has to reinterpret
    // water normals or refraction data.
    Microsoft::WRL::ComPtr<ID3D12Resource> mEmissiveMask;
    // One-bounce indirect-lighting signal traced from the same visible
    // surface. It is kept scalar and low bandwidth so it cannot replace the
    // original texture or direct-light response.
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndirectMask;
    // Direct Navi light for static receivers whose captured Fast3D draw proves
    // G_LIGHTING did not already put that source into Shade.
    Microsoft::WRL::ComPtr<ID3D12Resource> mNativePointDirect;
    // Copy of the already-rendered scene used by the fullscreen surface
    // composite. Keeping this separate avoids read/write aliasing when the
    // water refraction path samples the scene while writing the backbuffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> mSceneColorCopy;
    // Harness-only controls share the exact same scene, signals and command
    // list as production. They are never bound to the displayed framebuffer.
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> mVisualAblationTargets;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mVisualAblationRtvs;
    int mVisualAblationFrame = -1;
    bool mVisualAblationCaptured = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPathHistory[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mPathTemporalOutput;
    Microsoft::WRL::ComPtr<ID3D12Resource> mReflectionHistory[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mReflectionTemporalOutput;
    D3D12_RESOURCE_STATES mPathHistoryStates[2] = {
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    D3D12_RESOURCE_STATES mPathTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES mReflectionHistoryStates[2] = {
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    D3D12_RESOURCE_STATES mReflectionTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    uint32_t mPathHistoryIndex = 0;
    uint32_t mPathHistoryWidth = 0;
    uint32_t mPathHistoryHeight = 0;
    bool mPathHistoryValid = false;
    bool mPathProfileWasActive = false;
    uint64_t mLastTemporalCameraCutCount = 0;
    uint64_t mLastTemporalSceneChangeCount = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPathTemporalConstants;
    uint8_t* mPathTemporalConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mPathTemporalRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPathTemporalPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mPathTemporalDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mPathTemporalDescriptorTemplate;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 3> mPathTemporalDescriptorFrames;
    uint32_t mPathTemporalDescriptorSize = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorTemplate;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 3> mDescriptorFrames;
    uint32_t mDescriptorSize = 0;
    uint32_t mMaskWidth = 0;
    uint32_t mMaskHeight = 0;
    uint32_t mSceneWidth = 0;
    uint32_t mSceneHeight = 0;
    DXGI_FORMAT mSceneFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT mEmissiveFormat = DXGI_FORMAT_R16_FLOAT;
    unsigned mMaskTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    unsigned mReflectionColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    unsigned mEmissiveTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    unsigned mIndirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    unsigned mNativePointDirectTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    unsigned mSceneColorTrackedState = static_cast<unsigned>(D3D12_RESOURCE_STATE_COPY_DEST);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mCompositeRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mCompositePso[2];

    float mViewMatrix[4][4] = {};
    float mProjMatrix[4][4] = {};
    float mInvProjectionMatrix[4][4] = {};
    float mInvViewMatrix[4][4] = {};
    float mCameraPosition[4] = { 0, 0, 0, 1 };
    float mClipXScale = 1.0f;
    float mRasterViewport[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    bool mMatricesValid = false;
    float mQualityScale = 0.5f;
    unsigned mIndirectSamples = 1;
    uint32_t mSampleFrameIndex = 0;

    DxrSurfaceStats mStats;
};

} // namespace Fast

#endif
