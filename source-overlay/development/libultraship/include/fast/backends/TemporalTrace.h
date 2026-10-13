#pragma once

#if defined(_WIN32)

#include <d3d12.h>
#include <string>
#include <map>
#include <fstream>
#include <mutex>
#include <chrono>
#include <cstdio>

namespace Fast {

// Forward declaration
class TemporalTrace {
public:
    static TemporalTrace& Instance();

    void Initialize(const char* configName); // "A_corrected" or "C_extra_corrected"
    void Shutdown();

    // Function trace: ENTER/EXIT with state
    void LogFunctionEnter(const char* function, uint64_t frame,
                          bool mEnabled, bool mInitialized,
                          uint32_t historyIndex, bool historyValid,
                          bool cameraCutState);
    void LogFunctionExit(const char* function, uint64_t frame,
                         bool mEnabled, bool mInitialized,
                         uint32_t historyIndex, bool historyValid,
                         bool cameraCutState);

    // GPU state snapshot before/after GPU operations
    void LogGpuStateBefore(const char* location, uint64_t frame,
                           ID3D12PipelineState* currentPso,
                           ID3D12RootSignature* currentRootSig,
                           ID3D12DescriptorHeap* currentDescriptorHeap,
                           D3D12_RESOURCE_STATES backbufferState,
                           ID3D12Resource* rtvResource,
                           ID3D12GraphicsCommandList* cmdList);
    void LogGpuStateAfter(const char* location, uint64_t frame,
                          ID3D12PipelineState* currentPso,
                          ID3D12RootSignature* currentRootSig,
                          ID3D12DescriptorHeap* currentDescriptorHeap,
                          D3D12_RESOURCE_STATES backbufferState,
                          ID3D12Resource* rtvResource,
                          ID3D12GraphicsCommandList* cmdList);

    // Special: IsEnabled/mEnabled access tracking
    void LogIsEnabledAccess(const char* caller, uint64_t frame, bool value);
    void LogMEnabledAccess(const char* caller, uint64_t frame, bool value, bool isWrite);

    // Raster spike correlation
    void LogRasterScopeBegin(uint64_t frame);
    void LogRasterScopeEnd(uint64_t frame, double gpuMs);

    // Divergence marker
    void LogDivergence(const char* message, uint64_t frame);

    // Stage boundary trace (forensic phase 2): tracks full frame pipeline
    void LogStage(const char* stage, uint64_t frame,
                  ID3D12Resource* backbuffer, unsigned backbufferState,
                  ID3D12Resource* depthBuffer, unsigned depthState,
                  ID3D12Resource* shadowMask, unsigned shadowMaskState,
                  ID3D12Resource* motionVectors, unsigned motionVectorsState,
                  ID3D12Resource* history0, unsigned history0State,
                  ID3D12Resource* history1, unsigned history1State,
                  ID3D12Resource* temporalOutput, unsigned temporalOutputState,
                  ID3D12DescriptorHeap* mainSrvHeap,
                  ID3D12DescriptorHeap* activeSrvHeap,
                  ID3D12RootSignature* rootSig,
                  ID3D12PipelineState* pso,
                  ID3D12Resource* rtvResource,
                  void* dsvHandlePtr,
                  ID3D12GraphicsCommandList* cmdList, bool cmdListOpen);

    bool IsEnabled() const { return mTraceEnabled; }
    const char* GetConfigName() const { return mConfigName; }

private:
    TemporalTrace();
    ~TemporalTrace();

    void WriteLine(const char* fmt, ...);
    std::string GetTimestamp();
    std::string GetGpuObjectName(void* ptr, const char* type);

    std::ofstream mLogFile;
    std::mutex mMutex;
    bool mTraceEnabled = false;
    const char* mConfigName = "UNKNOWN";
    uint64_t mGlobalFrame = 0;

    // Forensic phase 2: interval bookkeeping + resource identity
    std::map<std::string, uint32_t> mStageBegin;
    std::map<std::string, uint32_t> mStageEnd;
    char mStageResName[64] = {0};
    char mIntervalNote[96] = {0};
};

// Helper macros for easy instrumentation
#define TEMPORAL_TRACE_ENTER(func) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogFunctionEnter( \
                func, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                Fast::TemporalManager::Instance().IsEnabled(), \
                Fast::TemporalManager::Instance().IsInitialized(), \
                Fast::TemporalManager::Instance().GetCurrentHistoryIndex(), \
                Fast::TemporalManager::Instance().IsTemporalHistoryValid(), \
                Fast::TemporalManager::Instance().GetStats().cameraCutsDetected > 0 \
            ); \
        } \
    } while(0)

#define TEMPORAL_TRACE_EXIT(func) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogFunctionExit( \
                func, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                Fast::TemporalManager::Instance().IsEnabled(), \
                Fast::TemporalManager::Instance().IsInitialized(), \
                Fast::TemporalManager::Instance().GetCurrentHistoryIndex(), \
                Fast::TemporalManager::Instance().IsTemporalHistoryValid(), \
                Fast::TemporalManager::Instance().GetStats().cameraCutsDetected > 0 \
            ); \
        } \
    } while(0)

#define TEMPORAL_TRACE_GPU_BEFORE(loc, cmdList, pso, rootSig, descHeap, bbState, rtvRes) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogGpuStateBefore( \
                loc, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                pso, rootSig, descHeap, bbState, rtvRes, cmdList \
            ); \
        } \
    } while(0)

#define TEMPORAL_TRACE_GPU_AFTER(loc, cmdList, pso, rootSig, descHeap, bbState, rtvRes) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogGpuStateAfter( \
                loc, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                pso, rootSig, descHeap, bbState, rtvRes, cmdList \
            ); \
        } \
    } while(0)

#define TEMPORAL_TRACE_ISENABLED(caller, value) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogIsEnabledAccess(caller, \
                Fast::TemporalManager::Instance().GetFrameCounter(), value); \
        } \
    } while(0)

#define TEMPORAL_TRACE_MENABLED_READ(caller) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogMEnabledAccess(caller, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                Fast::TemporalManager::Instance().IsEnabled(), false); \
        } \
    } while(0)

#define TEMPORAL_TRACE_MENABLED_WRITE(caller, newValue) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogMEnabledAccess(caller, \
                Fast::TemporalManager::Instance().GetFrameCounter(), newValue, true); \
        } \
    } while(0)

#define TEMPORAL_TRACE_RASTER_BEGIN() \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogRasterScopeBegin( \
                Fast::TemporalManager::Instance().GetFrameCounter()); \
        } \
    } while(0)

#define TEMPORAL_TRACE_RASTER_END(gpuMs) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogRasterScopeEnd( \
                Fast::TemporalManager::Instance().GetFrameCounter(), gpuMs); \
        } \
    } while(0)

#define TEMPORAL_TRACE_DIVERGENCE(msg) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogDivergence(msg, \
                Fast::TemporalManager::Instance().GetFrameCounter()); \
        } \
    } while(0)

// Full stage-boundary snapshot: all resources, heaps, sigs, PSO, RTV/DSV, command list
#define TEMPORAL_TRACE_STAGE(stage, bb, bbState, depth, depthState, mask, maskState, mv, mvState, h0, h0State, h1, h1State, tout, toutState, mainHeap, actHeap, rs, pso, rtvRes, dsvPtr, cmdList, openFlag) \
    do { \
        if (Fast::TemporalTrace::Instance().IsEnabled()) { \
            Fast::TemporalTrace::Instance().LogStage(stage, \
                Fast::TemporalManager::Instance().GetFrameCounter(), \
                bb, bbState, depth, depthState, mask, maskState, \
                mv, mvState, h0, h0State, h1, h1State, tout, toutState, \
                mainHeap, actHeap, rs, pso, rtvRes, dsvPtr, cmdList, openFlag); \
        } \
    } while(0)

} // namespace Fast

#endif // _WIN32
