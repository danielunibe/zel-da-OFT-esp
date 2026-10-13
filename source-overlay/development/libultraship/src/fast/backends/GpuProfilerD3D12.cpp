#if defined(_WIN32)

#include "fast/backends/GpuProfilerD3D12.h"
#include "fast/couch_diagnostics.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace Fast {

static void LogGpuProfiler(const std::string& line) {
    SPDLOG_INFO(line);
    std::ofstream ofs(couch_diagnostics_path(), std::ios::app);
    if (ofs.is_open()) {
        ofs << line << "\n";
    }
}

GpuProfilerD3D12& GpuProfilerD3D12::Instance() {
    static GpuProfilerD3D12 sInstance;
    return sInstance;
}

GpuProfilerD3D12::GpuProfilerD3D12()
    : mEnabled(false), mInitialized(false), mDevice(nullptr), mCommandQueue(nullptr),
      mCurrentFrameIndex(0), mTimestampFrequency(0), mInvTimestampFrequencyMs(0.0),
      mCpuFrameStartMs(0.0), mCpuFrameEndMs(0.0), mFrameCounter(0), mWarmupFrames(0) {
    memset(&mLatestResult, 0, sizeof(mLatestResult));
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        mFrames[i].readbackMapped = nullptr;
        mFrames[i].resolved = false;
        mFrames[i].readbackReady = false;
        mFrames[i].scopeCount = 0;
        mFrames[i].totalQueries = 0;
    }
}

GpuProfilerD3D12::~GpuProfilerD3D12() {
    Shutdown();
}

bool GpuProfilerD3D12::Init(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {
    if (mInitialized) return true;
    if (!device || !commandQueue) return false;

    mDevice = device;
    mCommandQueue = commandQueue;

    HRESULT hr = commandQueue->GetTimestampFrequency(&mTimestampFrequency);
    if (FAILED(hr) || mTimestampFrequency == 0) {
        LogGpuProfiler("[COUCH-GPU-PROFILER] Failed to get timestamp frequency, hr=0x" + std::to_string((uint32_t)hr));
        mEnabled = false;
        return false;
    }
    mInvTimestampFrequencyMs = 1000.0 / static_cast<double>(mTimestampFrequency);

    const uint32_t totalQueries = kMaxScopesPerFrame * kQueriesPerScope;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = totalQueries;
        heapDesc.NodeMask = 1;

        hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&mFrames[i].queryHeap));
        if (FAILED(hr)) {
            LogGpuProfiler("[COUCH-GPU-PROFILER] Failed to create query heap for frame " + std::to_string(i));
            mEnabled = false;
            return false;
        }

        D3D12_HEAP_PROPERTIES readbackHeap = {};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalQueries * sizeof(uint64_t);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&mFrames[i].readbackBuffer));
        if (FAILED(hr)) {
            LogGpuProfiler("[COUCH-GPU-PROFILER] Failed to create readback buffer for frame " + std::to_string(i));
            mEnabled = false;
            return false;
        }

        mFrames[i].readbackMapped = nullptr;
        mFrames[i].resolved = false;
        mFrames[i].readbackReady = false;
        mFrames[i].scopeCount = 0;
        mFrames[i].totalQueries = totalQueries;
        memset(mFrames[i].scopes, 0, sizeof(mFrames[i].scopes));
    }

    mEnabled = true;
    mInitialized = true;

    LogGpuProfiler("[COUCH-GPU-PROFILER] INITIALIZED OK");
    LogGpuProfiler("[COUCH-GPU-PROFILER] TimestampFrequency = " + std::to_string(mTimestampFrequency) + " Hz");
    LogGpuProfiler("[COUCH-GPU-PROFILER] InvTimestampFrequency = " + std::to_string(mInvTimestampFrequencyMs * 1000000.0) + " ns/tick");
    LogGpuProfiler("[COUCH-GPU-PROFILER] MaxScopesPerFrame = " + std::to_string(kMaxScopesPerFrame));
    LogGpuProfiler("[COUCH-GPU-PROFILER] FramesInFlight = " + std::to_string(kMaxFramesInFlight));

    return true;
}

void GpuProfilerD3D12::Shutdown() {
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (mFrames[i].readbackMapped && mFrames[i].readbackBuffer) {
            D3D12_RANGE writeRange = { 0, 0 };
            mFrames[i].readbackBuffer->Unmap(0, &writeRange);
            mFrames[i].readbackMapped = nullptr;
        }
        mFrames[i].queryHeap.Reset();
        mFrames[i].readbackBuffer.Reset();
    }
    mDevice = nullptr;
    mCommandQueue = nullptr;
    mEnabled = false;
    mInitialized = false;
}

void GpuProfilerD3D12::BeginFrame(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex, ID3D12Fence* fence, uint64_t signalValue) {
    if (!mEnabled || !cmdList || !fence) return;
    mFence = fence;
    // Consume the slot only after its actual submission fence has completed.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) ReadbackFrame(i);
    mCurrentFrameIndex = frameIndex % kMaxFramesInFlight;
    auto& frame = mFrames[mCurrentFrameIndex];
    frame.scopeCount = 0;
    frame.resolved = false;
    frame.readbackReady = false;
    frame.frameNumber = ++mFrameCounter;
    frame.signalValue = signalValue;
    frame.cpuStartMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.cpuEndMs = frame.cpuStartMs;
    memset(frame.scopes, 0, sizeof(frame.scopes));
    BeginScope(cmdList, "GPU_FRAME");
}

void GpuProfilerD3D12::EndFrame(ID3D12GraphicsCommandList* cmdList, uint64_t signalValue) {
    if (!mEnabled || !cmdList) return;
    auto& frame = mFrames[mCurrentFrameIndex];
    frame.signalValue = signalValue;
    frame.cpuEndMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    EndScope(cmdList, "GPU_FRAME");
    // Close remaining scopes before resolving: unwritten timestamps are invalid.
    for (uint32_t i = 0; i < frame.scopeCount; ++i) {
        if (!frame.scopes[i].ended) {
            cmdList->EndQuery(frame.queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                             frame.scopes[i].endQueryIndex);
            frame.scopes[i].ended = true;
        }
    }
    ResolveFrame(mCurrentFrameIndex, cmdList);
    if (mFrameCounter <= 10 || mFrameCounter % 300 == 0) LogFrameResult();
}

void GpuProfilerD3D12::BeginScope(ID3D12GraphicsCommandList* cmdList, const char* name) {
    if (!mEnabled || !cmdList) return;

    auto& frame = mFrames[mCurrentFrameIndex];
    if (frame.scopeCount >= kMaxScopesPerFrame) return;

    uint32_t queryIndex = frame.scopeCount * kQueriesPerScope;

    // Para timestamps: solo necesitamos EndQuery para escribir el timestamp actual
    // BeginQuery NO se usa para TIMESTAMP queries
    cmdList->EndQuery(frame.queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);

    ScopeRecord& rec = frame.scopes[frame.scopeCount];
    strncpy(rec.name, name, sizeof(rec.name) - 1);
    rec.name[sizeof(rec.name) - 1] = '\0';
    rec.beginQueryIndex = queryIndex;
    rec.ended = false;
    rec.endQueryIndex = queryIndex + 1; // Se escribirá en EndScope

    frame.scopeCount++;
}

void GpuProfilerD3D12::EndScope(ID3D12GraphicsCommandList* cmdList, const char* name) {
    if (!mEnabled || !cmdList) return;

    auto& frame = mFrames[mCurrentFrameIndex];

    // Encontrar el scope activo por nombre (último match)
    for (int32_t i = frame.scopeCount - 1; i >= 0; --i) {
        if (strcmp(frame.scopes[i].name, name) == 0 && !frame.scopes[i].ended) {
            // EndQuery para el timestamp de fin
            cmdList->EndQuery(frame.queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame.scopes[i].endQueryIndex);
            frame.scopes[i].ended = true;
            return;
        }
    }
}

void GpuProfilerD3D12::ResolveFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList) return;

    auto& frame = mFrames[frameIndex];
    if (frame.scopeCount == 0) return;

    const uint32_t totalQueries = frame.scopeCount * kQueriesPerScope;

    // ResolveQueryData: copia timestamps del query heap al readback buffer
    // Esto se hace en el command list del frame actual, antes de Close()
    cmdList->ResolveQueryData(
        frame.queryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        0,
        totalQueries,
        frame.readbackBuffer.Get(),
        0
    );

    frame.resolved = true;
}

void GpuProfilerD3D12::ReadbackFrame(uint32_t frameIndex) {
    auto& frame = mFrames[frameIndex];
    if (!frame.resolved || frame.readbackReady || !mFence ||
        mFence->GetCompletedValue() < frame.signalValue) return;
    if (!frame.readbackBuffer || frame.scopeCount == 0) return;

    // The submission fence above proves the resolve has completed.
    const uint32_t totalQueries = frame.scopeCount * kQueriesPerScope;
    const size_t readSize = totalQueries * sizeof(uint64_t);

    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(readSize) };
    void* mapped = nullptr;
    if (FAILED(frame.readbackBuffer->Map(0, &readRange, &mapped))) return;
    if (!mapped) return;

    frame.readbackMapped = static_cast<uint64_t*>(mapped);
    frame.readbackReady = true;

    // Parsear timestamps
    if (frame.frameNumber <= mLatestResult.frameNumber) {
        frame.readbackBuffer->Unmap(0, nullptr);
        frame.readbackMapped = nullptr;
        frame.resolved = false;
        return;
    }
    mLatestResult = {};
    mLatestResult.frameNumber = frame.frameNumber;
    mLatestResult.fenceValue = frame.signalValue;
    mLatestResult.valid = true;
    mLatestResult.timestampFrequency = static_cast<double>(mTimestampFrequency);
    mLatestResult.cpuFrameMs = std::max(0.0, frame.cpuEndMs - frame.cpuStartMs);

    double maxGpuMs = 0.0;
    double sumGpuMs = 0.0;
    uint32_t scopeIdx = 0;

    for (uint32_t i = 0; i < frame.scopeCount && scopeIdx < GpuProfilerFrameResult::kMaxScopes; ++i) {
        uint32_t beginIdx = frame.scopes[i].beginQueryIndex;
        uint32_t endIdx = frame.scopes[i].endQueryIndex;

        if (endIdx < totalQueries) {
            uint64_t beginTick = frame.readbackMapped[beginIdx];
            uint64_t endTick = frame.readbackMapped[endIdx];
            double elapsedMs = endTick >= beginTick ?
                static_cast<double>(endTick - beginTick) * mInvTimestampFrequencyMs : 0.0;

            if (elapsedMs < 0) elapsedMs = 0;
            if (elapsedMs > 1000) elapsedMs = 0;

            auto& entry = mLatestResult.scopes[scopeIdx];
            strncpy(entry.name, frame.scopes[i].name, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            entry.ms = elapsedMs;
            scopeIdx++;

            sumGpuMs += elapsedMs;
            if (elapsedMs > maxGpuMs) maxGpuMs = elapsedMs;
        }
    }
    mLatestResult.scopeCount = scopeIdx;

    mLatestResult.frameGpuMs = maxGpuMs;
    mLatestResult.totalGpuMs = sumGpuMs;

    // Populate named fields
    auto getScope = [&](const char* n) -> double {
        for (uint32_t i = 0; i < mLatestResult.scopeCount; ++i) {
            if (strcmp(mLatestResult.scopes[i].name, n) == 0) return mLatestResult.scopes[i].ms;
        }
        return 0.0;
    };
    mLatestResult.rasterMs = getScope("GPU_RASTER");
    mLatestResult.blasTlasMs = getScope("GPU_BLAS_TLAS");
    mLatestResult.dxrShadowMs = getScope("GPU_DXR_SHADOW");
    mLatestResult.dxrSurfaceMs = getScope("GPU_DXR_SURFACE");
    mLatestResult.temporalMs = getScope("GPU_TEMPORAL");
    mLatestResult.compositeMs = getScope("GPU_COMPOSITE");
    mLatestResult.frameGpuMs = getScope("GPU_FRAME");
    mLatestResult.totalGpuMs = mLatestResult.frameGpuMs;

    // Unmap
    D3D12_RANGE writeRange = { 0, 0 };
    frame.readbackBuffer->Unmap(0, &writeRange);
    frame.readbackMapped = nullptr;
    frame.readbackReady = false;
    frame.resolved = false;
}

double GpuProfilerD3D12::GetScopeMs(const char* name) const {
    for (uint32_t i = 0; i < mLatestResult.scopeCount; ++i) {
        if (strcmp(mLatestResult.scopes[i].name, name) == 0) {
            return mLatestResult.scopes[i].ms;
        }
    }
    return -1.0;
}

void GpuProfilerD3D12::SetCpuFrameStart(double cpuTimeMs) {
    mCpuFrameStartMs = cpuTimeMs;
}

void GpuProfilerD3D12::SetCpuFrameEnd(double cpuTimeMs) {
    mCpuFrameEndMs = cpuTimeMs;
}

void GpuProfilerD3D12::LogFrameResult() const {
    std::stringstream ss;
    ss << "[COUCH-GPU-PROFILER] Frame " << mLatestResult.frameNumber
       << " fence=" << mLatestResult.fenceValue << " completed=" << (mFence ? mFence->GetCompletedValue() : 0) << " valid=" << mLatestResult.valid
       << " | GPU_TOTAL=" << std::fixed << std::setprecision(3) << mLatestResult.totalGpuMs << " ms"
       << " | CPU_FRAME=" << std::setprecision(3) << mLatestResult.cpuFrameMs << " ms";

    if (mLatestResult.rasterMs > 0)
        ss << " | RASTER=" << std::setprecision(3) << mLatestResult.rasterMs;
    if (mLatestResult.blasTlasMs > 0)
        ss << " | BLAS_TLAS=" << std::setprecision(3) << mLatestResult.blasTlasMs;
    if (mLatestResult.dxrShadowMs > 0)
        ss << " | DXR_SHADOW=" << std::setprecision(3) << mLatestResult.dxrShadowMs;
    if (mLatestResult.dxrSurfaceMs > 0)
        ss << " | DXR_SURFACE=" << std::setprecision(3) << mLatestResult.dxrSurfaceMs;
    if (mLatestResult.temporalMs > 0)
        ss << " | TEMPORAL=" << std::setprecision(3) << mLatestResult.temporalMs;
    if (mLatestResult.compositeMs > 0)
        ss << " | COMPOSITE=" << std::setprecision(3) << mLatestResult.compositeMs;

    ss << " | FRAME_GPU=" << std::setprecision(3) << mLatestResult.frameGpuMs;

    LogGpuProfiler(ss.str());
}

} // namespace Fast

#endif
