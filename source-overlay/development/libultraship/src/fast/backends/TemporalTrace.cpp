#include "fast/backends/TemporalTrace.h"
#include "fast/backends/TemporalManager.h"
#include <d3d12.h>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sstream>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace Fast {

TemporalTrace& TemporalTrace::Instance() {
    static TemporalTrace sInstance;
    return sInstance;
}

TemporalTrace::TemporalTrace() {
    mIntervalNote[0] = '\0';
}

TemporalTrace::~TemporalTrace() {
    Shutdown();
}

void TemporalTrace::Initialize(const char* configName) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (mTraceEnabled) {
        return;
    }

    // The full trace writes and flushes a line for every temporal call. It is
    // an audit tool, not a runtime feature, so keep it disabled unless a
    // forensic run explicitly requests it.
    const char* traceEnv = std::getenv("COUCH_TEMPORAL_TRACE");
    if (!traceEnv || std::strcmp(traceEnv, "1") != 0) {
        return;
    }
    
    mConfigName = configName ? configName : "UNKNOWN";
    
    // Create logs directory if it doesn't exist
    CreateDirectoryA("logs", nullptr);
    
    std::string filename = "logs/" + std::string(mConfigName) + "_TRACE.log";
    mLogFile.open(filename, std::ios::out | std::ios::trunc);
    
    if (mLogFile.is_open()) {
        mTraceEnabled = true;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&time_t));
        
        WriteLine("=== TEMPORAL TRACE STARTED ===");
        WriteLine("Config: %s", mConfigName);
        WriteLine("Timestamp: %s", timebuf);
        WriteLine("Format: FRAME | FUNCTION | EVENT | mEnabled | mInitialized | historyIdx | historyValid | cameraCut");
        WriteLine("---");
    }
}

void TemporalTrace::Shutdown() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    if (mLogFile.is_open()) {
        WriteLine("---");
        WriteLine("=== TEMPORAL TRACE ENDED ===");
        mLogFile.close();
    }
    mTraceEnabled = false;
}

void TemporalTrace::WriteLine(const char* fmt, ...) {
    if (!mLogFile.is_open()) return;
    
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    mLogFile << buffer << "\n";
    mLogFile.flush();
}

std::string TemporalTrace::GetTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)ns);
    return std::string(buf);
}

std::string TemporalTrace::GetGpuObjectName(void* ptr, const char* type) {
    if (!ptr) return std::string(type) + "=NULL";
    char buf[128];
    snprintf(buf, sizeof(buf), "%s=0x%p", type, ptr);
    return std::string(buf);
}

void TemporalTrace::LogFunctionEnter(const char* function, uint64_t frame,
                                     bool mEnabled, bool mInitialized,
                                     uint32_t historyIndex, bool historyValid,
                                     bool cameraCutState) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | %s | ENTER | %d | %d | %u | %d | %d",
              (unsigned long long)frame, function,
              mEnabled ? 1 : 0, mInitialized ? 1 : 0,
              historyIndex, historyValid ? 1 : 0, cameraCutState ? 1 : 0);
}

void TemporalTrace::LogFunctionExit(const char* function, uint64_t frame,
                                    bool mEnabled, bool mInitialized,
                                    uint32_t historyIndex, bool historyValid,
                                    bool cameraCutState) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | %s | EXIT  | %d | %d | %u | %d | %d",
              (unsigned long long)frame, function,
              mEnabled ? 1 : 0, mInitialized ? 1 : 0,
              historyIndex, historyValid ? 1 : 0, cameraCutState ? 1 : 0);
}

void TemporalTrace::LogGpuStateBefore(const char* location, uint64_t frame,
                                      ID3D12PipelineState* currentPso,
                                      ID3D12RootSignature* currentRootSig,
                                      ID3D12DescriptorHeap* currentDescriptorHeap,
                                      D3D12_RESOURCE_STATES backbufferState,
                                      ID3D12Resource* rtvResource,
                                      ID3D12GraphicsCommandList* cmdList) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | GPU_STATE_BEFORE:%s | PSO=%s | RS=%s | Heap=%s | BB_State=0x%X | RTV=%s | CmdList=%s",
              (unsigned long long)frame, location,
              GetGpuObjectName(currentPso, "PSO").c_str(),
              GetGpuObjectName(currentRootSig, "RS").c_str(),
              GetGpuObjectName(currentDescriptorHeap, "Heap").c_str(),
              (unsigned int)backbufferState,
              GetGpuObjectName(rtvResource, "RTV").c_str(),
              GetGpuObjectName(cmdList, "CmdList").c_str());
}

void TemporalTrace::LogGpuStateAfter(const char* location, uint64_t frame,
                                     ID3D12PipelineState* currentPso,
                                     ID3D12RootSignature* currentRootSig,
                                     ID3D12DescriptorHeap* currentDescriptorHeap,
                                     D3D12_RESOURCE_STATES backbufferState,
                                     ID3D12Resource* rtvResource,
                                     ID3D12GraphicsCommandList* cmdList) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | GPU_STATE_AFTER:%s  | PSO=%s | RS=%s | Heap=%s | BB_State=0x%X | RTV=%s | CmdList=%s",
              (unsigned long long)frame, location,
              GetGpuObjectName(currentPso, "PSO").c_str(),
              GetGpuObjectName(currentRootSig, "RS").c_str(),
              GetGpuObjectName(currentDescriptorHeap, "Heap").c_str(),
              (unsigned int)backbufferState,
              GetGpuObjectName(rtvResource, "RTV").c_str(),
              GetGpuObjectName(cmdList, "CmdList").c_str());
}

void TemporalTrace::LogIsEnabledAccess(const char* caller, uint64_t frame, bool value) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | ISENABLED_ACCESS:%s | value=%d", 
              (unsigned long long)frame, caller, value ? 1 : 0);
}

void TemporalTrace::LogMEnabledAccess(const char* caller, uint64_t frame, bool value, bool isWrite) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | MENABLED_%s:%s | value=%d",
              (unsigned long long)frame, isWrite ? "WRITE" : "READ", caller, value ? 1 : 0);
}

void TemporalTrace::LogRasterScopeBegin(uint64_t frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | RASTER_SCOPE_BEGIN", (unsigned long long)frame);
}

void TemporalTrace::LogRasterScopeEnd(uint64_t frame, double gpuMs) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | RASTER_SCOPE_END | gpuMs=%.6f", (unsigned long long)frame, gpuMs);
}

void TemporalTrace::LogDivergence(const char* message, uint64_t frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;
    
    WriteLine("%llu | >>> DIVERGENCE: %s <<<", (unsigned long long)frame, message);
}

void TemporalTrace::LogStage(const char* stage, uint64_t frame,
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
                             ID3D12GraphicsCommandList* cmdList, bool cmdListOpen) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mLogFile.is_open()) return;

    // Resource identity: first non-null tracked resource names the frame
    const char* resName = "none";
    void* res = backbuffer ? backbuffer : (depthBuffer ? depthBuffer : shadowMask);
    if (mStageResName[0] == '\0') {
        if (res) {
            snprintf(mStageResName, sizeof(mStageResName), "res@%p", res);
        }
    }
    resName = mStageResName[0] ? mStageResName : "none";

    auto mark = [&](const char* kind, std::map<std::string, uint32_t>& table) {
        std::string key = stage;
        if (strncmp(stage, "FRAME_", 6) == 0 || strstr(stage, "_TRANSITION") != nullptr) {
            key = "FRAME";
        } else {
            const char* us = strstr(stage, "_BEGIN");
            if (!us) us = strstr(stage, "_END");
            if (us) key = std::string(stage, us);
        }
        if (kind[0] == 'B') table[key] = (uint32_t)frame;
        else {
            uint32_t b = table.count(key) ? table[key] : (uint32_t)frame;
            mStageEnd[key] = (uint32_t)frame;
            char ibuf[80];
            snprintf(ibuf, sizeof(ibuf), "| interval=%s[%u..%u)", key.c_str(), b, (uint32_t)frame);
            snprintf(mIntervalNote, sizeof(mIntervalNote), "%s", ibuf);
        }
    };
    mIntervalNote[0] = '\0';
    const bool isBegin = strstr(stage, "_BEGIN") != nullptr || strncmp(stage, "FRAME_", 6) == 0;
    if (isBegin) {
        mark("B", mStageBegin);
    } else {
        mark("E", mStageBegin);
    }

    WriteLine("%llu | %s | bb=0x%p/%s bbSt=0x%X depth=0x%p depthSt=0x%X mask=0x%p maskSt=0x%X mv=0x%p mvSt=0x%X h0=0x%p h0St=0x%X h1=0x%p h1St=0x%X tout=0x%p toutSt=0x%X | mainSRV=0x%p activeSRV=0x%p RS=0x%p PSO=0x%p RTV=0x%p DSV=0x%p cmdList=0x%p/%s %s",
              (unsigned long long)frame, stage,
              (void*)backbuffer, resName, backbufferState,
              (void*)depthBuffer, depthState,
              (void*)shadowMask, shadowMaskState,
              (void*)motionVectors, motionVectorsState,
              (void*)history0, history0State,
              (void*)history1, history1State,
              (void*)temporalOutput, temporalOutputState,
              (void*)mainSrvHeap, (void*)activeSrvHeap,
              (void*)rootSig, (void*)pso,
              (void*)rtvResource, dsvHandlePtr,
              (void*)cmdList, cmdListOpen ? "open" : "closed",
              mIntervalNote);
}

} // namespace Fast
