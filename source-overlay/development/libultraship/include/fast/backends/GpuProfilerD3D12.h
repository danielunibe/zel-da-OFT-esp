#pragma once

#if defined(_WIN32)

#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>

namespace Fast {

struct GpuProfilerFrameResult {
    static const uint32_t kMaxScopes = 8;
    struct ScopeEntry {
        char name[64];
        double ms;
    };
    uint64_t frameNumber;
    uint64_t fenceValue;
    bool valid;
    double timestampFrequency;
    ScopeEntry scopes[kMaxScopes];
    uint32_t scopeCount;
    double totalGpuMs;
    double cpuFrameMs;
    double rasterMs;
    double blasTlasMs;
    double dxrShadowMs;
    double dxrSurfaceMs;
    double temporalMs;
    double compositeMs;
    double frameGpuMs;
};

class GpuProfilerD3D12 {
public:
    static GpuProfilerD3D12& Instance();

    bool Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    void Shutdown();

    void BeginFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex, ID3D12Fence* fence, uint64_t signalValue);
    void EndFrame(ID3D12GraphicsCommandList* cmdList, uint64_t signalValue);

    void BeginScope(ID3D12GraphicsCommandList* cmdList, const char* name);
    void EndScope(ID3D12GraphicsCommandList* cmdList, const char* name);

    void SetCpuFrameStart(double cpuTimeMs);
    void SetCpuFrameEnd(double cpuTimeMs);

    bool IsEnabled() const { return mEnabled; }
    const GpuProfilerFrameResult& GetLatestResult() const { return mLatestResult; }
    double GetScopeMs(const char* name) const;

    void LogFrameResult() const;

private:
    GpuProfilerD3D12();
    ~GpuProfilerD3D12();

    static const uint32_t kMaxScopesPerFrame = 32;
    static const uint32_t kMaxFramesInFlight = 3;
    static const uint32_t kQueriesPerScope = 2; // begin + end timestamps

    struct ScopeRecord {
        char name[64];
        uint32_t beginQueryIndex;
        uint32_t endQueryIndex;
        bool ended;
    };

    struct FrameQueries {
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
        uint64_t* readbackMapped;
        bool resolved;
        bool readbackReady;
        uint32_t scopeCount;
        uint32_t totalQueries;
        uint64_t frameNumber = 0;
        uint64_t signalValue = 0;
        double cpuStartMs = 0, cpuEndMs = 0;
        ScopeRecord scopes[kMaxScopesPerFrame];
    };

    void ResolveFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* cmdList);
    void ReadbackFrame(uint32_t frameIndex);

    bool mEnabled;
    bool mInitialized;
    ID3D12Device* mDevice;
    ID3D12Fence* mFence = nullptr;
    ID3D12CommandQueue* mCommandQueue;

    FrameQueries mFrames[kMaxFramesInFlight];
    uint32_t mCurrentFrameIndex;

    uint64_t mTimestampFrequency;
    double mInvTimestampFrequencyMs;

    GpuProfilerFrameResult mLatestResult;
    double mCpuFrameStartMs;
    double mCpuFrameEndMs;

    uint64_t mFrameCounter;
    uint32_t mWarmupFrames;
};

} // namespace Fast

#endif
