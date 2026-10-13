#if defined(_WIN32)
#define NOMINMAX
#include "fast/backends/gfx_direct3d12.h"
#include "fast/backends/gfx_dxgi.h"
#include "fast/backends/GpuProfilerD3D12.h"
#include "fast/backends/DxrShadowRenderer.h"
#include "fast/backends/DxrSurfaceRenderer.h"
#include "fast/backends/TemporalManager.h"
#include "fast/MaterialRegistry.h"
#include "fast/couch_diagnostics.h"
#include "ship/Context.h"
#include "ship/config/ConsoleVariable.h"
#include <imgui_impl_dx12.h>
#include <fstream>
#include <stdexcept>

namespace Fast {
void GfxRenderingAPIDX12::InitGuiRenderer() {
    if (mGuiRendererInitialized) return;
    ImGui_ImplDX12_InitInfo info;
    info.Device = mDevice.Get(); info.CommandQueue = mCommandQueue.Get();
    info.NumFramesInFlight = kFrameBufferCount; info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    info.SrvDescriptorHeap = mSrvHeap.Get(); info.UserData = this;
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto self = static_cast<GfxRenderingAPIDX12*>(i->UserData);
        const uint32_t slot = self->AllocateSrvDescriptor();
        *cpu = self->mSrvHeap->GetCPUDescriptorHandleForHeapStart();
        *gpu = self->mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        cpu->ptr += size_t(slot) * self->mSrvDescriptorSize; gpu->ptr += size_t(slot) * self->mSrvDescriptorSize;
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto self = static_cast<GfxRenderingAPIDX12*>(i->UserData);
        const auto base = self->mSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
        self->mFrameContexts[self->mFrameIndex].retiredSrvIndices.push_back(uint32_t((cpu.ptr-base)/self->mSrvDescriptorSize));
    };
    mGuiRendererInitialized = ImGui_ImplDX12_Init(&info);
    if (!mGuiRendererInitialized) throw std::runtime_error("ImGui DX12 initialization failed");
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
}
void GfxRenderingAPIDX12::NewGuiFrame() {
    if (mGuiRendererInitialized) ImGui_ImplDX12_NewFrame();
}
void GfxRenderingAPIDX12::ShutdownGuiRenderer() {
    if (!mGuiRendererInitialized) return;
    WaitForGpu(); ImGui_ImplDX12_Shutdown(); mGuiRendererInitialized = false;
}
void GfxRenderingAPIDX12::RenderGui(ImDrawData* data) {
    if (!mGuiRendererInitialized || !mCommandListOpen) return;
    StartDrawToFramebuffer(0,1);
    ID3D12DescriptorHeap* heap = mSrvHeap.Get();
    mCommandList->SetDescriptorHeaps(1,&heap);
    ImGui_ImplDX12_RenderDrawData(data,mCommandList.Get());
    RestoreRasterState();
}
void GfxRenderingAPIDX12::UpdateAutomaticQuality() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now-mQualityTime).count();
    mQualityTime = now;
    const HWND window = static_cast<HWND>(mWindowBackend->GetWindowHandle());
    const HMONITOR monitor = MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
    if (monitor != mQualityMonitor) {
        mQualityMonitor = monitor; mQualityController.Reset();
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        if (GetMonitorInfoW(monitor,&info) && EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode)) {
            mQualityTargetHz = mode.dmDisplayFrequency > 1 ? std::min(60.0,double(mode.dmDisplayFrequency)) : 60.0;
            std::ofstream log(couch_diagnostics_path(),std::ios::app);
            log << "[COUCH-RTX-MONITOR] native=" << mode.dmPelsWidth << "x" << mode.dmPelsHeight
                << " refresh=" << mode.dmDisplayFrequency << " target=" << mQualityTargetHz
                << " adapter=" << mAdapterName << " vramMiB=" << mDedicatedVideoMemory/(1024*1024) << "\n";
        }
    }
    const auto cvars=Ship::Context::GetInstance()->GetConsoleVariables();
    const char* envQuality = getenv("COUCH_RTX_QUALITY");
    bool automatic = (envQuality && *envQuality) ? false : (cvars->GetInteger("gEnhancements.Graphics.DXR.AutoQuality", 1) != 0);
    const auto& sample = GpuProfilerD3D12::Instance().GetLatestResult();
    if (sample.valid && sample.frameNumber >= mLastMemorySample + 300 && mQualityAdapter) {
        mLastMemorySample = sample.frameNumber;
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(mQualityAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
            std::ofstream log(couch_diagnostics_path(), std::ios::app);
            log << "[COUCH-RTX-MEMORY] frame=" << sample.frameNumber << " usageMiB=" << memory.CurrentUsage / (1024 * 1024)
                << " budgetMiB=" << memory.Budget / (1024 * 1024) << " completedFence=" << mFence->GetCompletedValue() << "\n";
        }
    }
    const char* captures = getenv("COUCH_FORENSIC_CAPTURES");
    const bool suspended = IsIconic(window) || !mLastFrameHadDxr || cvars->GetInteger("gEnhancements.Graphics.DXR.CaptureFrame", 0) != 0 || (captures && strcmp(captures, "1") == 0);
    bool changed = false;
    if (envQuality && *envQuality) {
        int manual = std::clamp(atoi(envQuality), 0, 3);
        changed = manual != mQualityController.level;
        mQualityController.level = manual;
        mQualityController.Reset();
    } else if (automatic && sample.valid && sample.frameNumber != mLastQualitySample) {
        mLastQualitySample = sample.frameNumber;
        changed = mQualityController.Update(sample.frameGpuMs, sample.cpuFrameMs, dt, mQualityTargetHz, suspended);
    } else if (!automatic) {
        int manual = std::clamp(cvars->GetInteger("gEnhancements.Graphics.DXR.Quality", 3), 0, 3);
        changed = manual != mQualityController.level;
        mQualityController.level = manual;
        mQualityController.Reset();
    } else if (suspended) mQualityController.Reset();
    cvars->SetInteger("gEnhancements.Graphics.DXR.EffectiveQuality",mQualityController.level);
    const auto quality=GetRtxQualitySettings(mQualityController.level);
    // Path lighting doubles the preset sample budget except at the automatic
    // minimum. There, use the preset's actual Low budget so the controller can
    // reduce DXR cost enough to recover frame time without disabling DXR.
    const bool pathProfile = MaterialRegistry::IsPathTracedExperimentalProfile();
    const bool autoLowDxr = automatic && MaterialRegistry::IsEnhancedVisualProfile() &&
                            mQualityController.level == 0;
    const unsigned pathSampleMultiplier = pathProfile && !autoLowDxr ? 2u : 1u;
    const unsigned indirectSamples = std::min(8u, quality.indirectSamples * pathSampleMultiplier);
    const unsigned shadowSamples = quality.shadowSamples * pathSampleMultiplier;
    const float dynamicScale = autoLowDxr ? mQualityController.lowTierResolutionScale : 1.0f;
    const float shadowScale = (autoLowDxr ? std::min(quality.shadowScale, 0.50f) : quality.shadowScale) * dynamicScale;
    const float surfaceScale = (autoLowDxr ? std::min(quality.surfaceScale, 0.375f) : quality.surfaceScale) * dynamicScale;
    DxrShadowRenderer::Instance().SetQualityScale(shadowScale);
    DxrShadowRenderer::Instance().SetQualitySamples(shadowSamples);
    DxrSurfaceRenderer::Instance().SetQuality(surfaceScale,indirectSamples);
    if (changed) {
        // Existing masks/resources are retained until the queue finishes before
        // changing their dimensions; this only blocks on rare tier changes.
        WaitForGpu();
        DxrShadowRenderer::Instance().OnResize(mRenderWidth,mRenderHeight);
        DxrSurfaceRenderer::Instance().OnResize(mRenderWidth,mRenderHeight);
        TemporalManager::Instance().OnResize(mRenderWidth,mRenderHeight);
        std::ofstream log(couch_diagnostics_path(),std::ios::app);
        log << "[COUCH-RTX-QUALITY] level=" << mQualityController.level << " automatic=" << automatic
            << " shadow=" << shadowScale << " surface=" << surfaceScale
            << " shadowExtent=" << RtxScaledExtent(mRenderWidth, shadowScale) << "x"
            << RtxScaledExtent(mRenderHeight, shadowScale)
            << " surfaceExtent=" << RtxScaledExtent(mRenderWidth, surfaceScale) << "x"
            << RtxScaledExtent(mRenderHeight, surfaceScale)
            << " shadowSamples=" << shadowSamples << " indirectSamples=" << indirectSamples
            << " autoLowDxr=" << autoLowDxr << " dynamicScale=" << dynamicScale
            << " frameGpuMs=" << sample.frameGpuMs
            << " targetMs=" << (mQualityTargetHz > 0.0 ? 1000.0 / mQualityTargetHz : 0.0) << "\n";
    }
}
} // namespace Fast
#endif
