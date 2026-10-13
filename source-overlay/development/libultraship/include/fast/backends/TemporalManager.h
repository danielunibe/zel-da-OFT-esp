#pragma once

#if defined(_WIN32)

#include <d3d12.h>
#include <array>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include "TemporalTrace.h"

namespace Fast {

// ============================================================
// MOTION VECTOR CONVENTION
// ============================================================
// Motion vectors represent displacement in SCREEN/UV space.
// Convention: motionVector = currentScreenUV - previousScreenUV
// Where:
//   currentScreenUV  = pixel position in current frame [0,1]
//   previousScreenUV = where that world point appeared in previous frame [0,1]
//
// Encoding: R16G16_FLOAT
//   R channel = horizontal displacement (u_current - u_previous)
//   G channel = vertical displacement   (v_current - v_previous)
//
// For static geometry with no motion: motion = (0, 0)
// ============================================================

enum class MotionClassification : uint8_t {
    MOTION_VALID_CAMERA = 0,       // Motion computed from camera movement
    MOTION_VALID_RIGID = 1,        // Motion from rigid body transform change
    MOTION_DEFORMING_UNSUPPORTED = 2, // Deforming geometry, no valid velocity
    MOTION_INVALID = 3             // No motion data available
};

struct TemporalFrameState {
    float viewMatrix[4][4];
    float projectionMatrix[4][4];
    float viewProjectionMatrix[4][4];
    float inverseViewProjectionMatrix[4][4];
    bool valid;
};

struct TemporalStats {
    uint64_t framesProcessed;
    uint64_t historyAcceptedPixels;
    uint64_t historyRejectedPixels;
    uint64_t cameraCutsDetected;
    uint64_t sceneChangesDetected;
    uint64_t resizeEventsDetected;
    uint64_t deformingInstancesSkipped;
    uint32_t motionVectorWidth;
    uint32_t motionVectorHeight;
    bool temporalHistoryValid;
};

struct TemporalConstants {
    float currentView[4][4];
    float currentProjection[4][4];
    float currentViewProjection[4][4];
    float currentInverseViewProjection[4][4];
    float previousView[4][4];
    float previousProjection[4][4];
    float previousViewProjection[4][4];
    float previousInverseViewProjection[4][4];
    float screenSize[4];         // x: width, y: height, z: 1/width, w: 1/height
    float temporalParams[4];     // x: historyBlend, y: rejectionThreshold, z: debugMode, w: frameIndex
    float cameraCutParams[4];    // x: cutDetected(1/0), y: positionDelta, z: rotationDelta, w: unused
};

inline constexpr size_t kTemporalConstantStride = (sizeof(TemporalConstants)+255u)&~size_t(255u);
static_assert(kTemporalConstantStride >= sizeof(TemporalConstants));

class TemporalManager {
public:
    static TemporalManager& Instance();

    bool Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    void Shutdown();

    // Frame lifecycle
    void BeginFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void EndFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);

    // Camera state
    void UpdateCameraMatrices(const float view[4][4], const float proj[4][4]);
    void OnResize(uint32_t width, uint32_t height);
    void OnSceneChange();

    // GPU resources
    bool EnsureResources(ID3D12Device* device, uint32_t width, uint32_t height);
    void DispatchMotionVectors(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depthBuffer,
                                D3D12_CPU_DESCRIPTOR_HANDLE depthSrvCpuHandle,
                                uint32_t width, uint32_t height);
    void DispatchReprojection(ID3D12GraphicsCommandList* cmdList,
                                ID3D12Resource* currentShadow,
                                D3D12_CPU_DESCRIPTOR_HANDLE currentShadowSrvCpuHandle,
                                uint32_t width, uint32_t height,
                                uint32_t currentSampleCount = 1);

    // Accessors
    bool IsEnabled() const { return mEnabled; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }
    const TemporalStats& GetStats() const { return mStats; }
    const TemporalFrameState& GetCurrentState() const { return mCurrentState; }
    const TemporalFrameState& GetPreviousState() const { return mPreviousState; }

    ID3D12Resource* GetMotionVectorBuffer() const { return mMotionVectorBuffer.Get(); }
    ID3D12Resource* GetCurrentHistoryBuffer() const { return mHistoryBuffers[mCurrentHistoryIndex].Get(); }
    ID3D12Resource* GetPreviousHistoryBuffer() const { return mHistoryBuffers[1 - mCurrentHistoryIndex].Get(); }

    // Forensic phase 2: read-only trace accessors
    ID3D12Resource* GetHistoryResourceForTrace(uint32_t idx) const {
        return (idx < kHistoryBufferCount) ? mHistoryBuffers[idx].Get() : nullptr;
    }
    unsigned GetHistoryResourceStateForTrace(uint32_t idx) const {
        return idx < kHistoryBufferCount ? static_cast<unsigned>(mHistoryResourceStates[idx]) : 0x4000;
    }
    ID3D12Resource* GetTemporalOutputBufferForTrace() const { return mTemporalOutputBuffer.Get(); }
    unsigned GetTemporalOutputBufferStateForTrace() const { return static_cast<unsigned>(mTemporalOutputState); }
    ID3D12DescriptorHeap* GetSrvHeapForTrace() const { return mDescriptorHeap.Get(); }
    ID3D12PipelineState* GetMotionVectorPsoForTrace() const { return mMotionVectorPso.Get(); }

    // SRV used by the post-DXR composite. The descriptor is populated after
    // reprojection and lives in the temporal shader-visible heap.
    D3D12_GPU_DESCRIPTOR_HANDLE GetTemporalOutputSrvGpuHandle() const;

    // Forensic capture hook: invoked inside DispatchReprojection interval at frame boundaries
    void RegisterCompositeCaptureCallback(void (*cb)(int frame)) { mCompositeCaptureCb = cb; }
    void InvokeCompositeCapture(int frame) { if (mCompositeCaptureCb) mCompositeCaptureCb(frame); }

    // Debug
    void SetDebugMode(uint32_t mode) { mDebugMode = mode; }
    uint32_t GetDebugMode() const { return mDebugMode; }

    // Validation
    bool AreMatricesValid() const { return mCurrentState.valid && mPreviousState.valid; }
    void InvalidateHistory() { mTemporalHistoryValid = false; mCurrentState.valid = mPreviousState.valid = false; }
    bool IsTemporalHistoryValid() const { return mTemporalHistoryValid; }
    uint64_t GetFrameCounter() const { return mFrameCounter; }
    bool IsInitialized() const { return mInitialized; }
    uint32_t GetCurrentHistoryIndex() const { return mCurrentHistoryIndex; }

    // Trace access
    TemporalTrace* GetTrace() { return mTrace; }
    const TemporalTrace* GetTrace() const { return mTrace; }
    TemporalTrace& GetTraceRef() { return *mTrace; }
    const TemporalTrace& GetTraceRef() const { return *mTrace; }
    bool HasTrace() const { return mTrace != nullptr; }

private:
    friend class TemporalTrace;
    TemporalManager();
    ~TemporalManager();

    bool CreateRootSignature();
    bool CreateMotionVectorPipeline();
    bool CreateReprojectionPipeline();
    bool CreateHistoryBuffers(ID3D12Device* device, uint32_t width, uint32_t height);

    void SwapHistoryBuffers();

    // Matrix utilities (reuse from DxrShadowRenderer pattern)
    static void MultiplyMatrices(float out[4][4], const float a[4][4], const float b[4][4]);
    static bool InvertMatrix4x4(const float m[4][4], float inv[4][4]);
    static float ComputePositionDelta(const float curr[4][4], const float prev[4][4]);
    static float ComputeRotationDelta(const float curr[4][4], const float prev[4][4]);

    bool mEnabled;
    bool mInitialized;
    ID3D12Device* mDevice;
    ID3D12CommandQueue* mCommandQueue;

    // Frame state
    TemporalFrameState mCurrentState;
    TemporalFrameState mPreviousState;
    bool mTemporalHistoryValid;
    uint32_t mDebugMode;

    // Camera cut detection
    float mLastPosition[3];
    float mLastRotation[3];
    float mCameraCutThreshold;

    // Motion Vector Buffer: R16G16_FLOAT
    Microsoft::WRL::ComPtr<ID3D12Resource> mMotionVectorBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mMotionVectorReadback;
    uint32_t mMVWidth;
    uint32_t mMVHeight;

    // Motion Validity Mask: R8_UINT
    Microsoft::WRL::ComPtr<ID3D12Resource> mMotionValidityBuffer;

    // History Buffers (Double Buffer): R8_UNORM (shadow mask history)
    static const uint32_t kHistoryBufferCount = 2;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHistoryBuffers[kHistoryBufferCount];
    D3D12_RESOURCE_STATES mHistoryResourceStates[kHistoryBufferCount] = {
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    uint32_t mCurrentHistoryIndex;
    uint32_t mHistoryWidth;
    uint32_t mHistoryHeight;

    // Temporal Output Buffer: R8_UNORM
    Microsoft::WRL::ComPtr<ID3D12Resource> mTemporalOutputBuffer;
    D3D12_RESOURCE_STATES mTemporalOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Descriptor Heaps
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorTemplate;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 6> mDescriptorFrames;
    uint32_t mDescriptorSize;
    uint32_t mDescriptorCount;

    // Root Signatures & Pipelines
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mMotionVectorRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mMotionVectorPso;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mReprojectionRootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mReprojectionPso;

    // Constant Buffer
    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantBuffer;
    uint8_t* mConstantBufferMapped;
    uint32_t mCurrentFrameIndex = 0;
    uint32_t mDescriptorPhase = 0;

    TemporalStats mStats;
    uint64_t mFrameCounter;

    // Trace
    TemporalTrace* mTrace;

    // Forensic phase 2: composite capture callback
    void (*mCompositeCaptureCb)(int frame) = nullptr;
};

} // namespace Fast

#endif
