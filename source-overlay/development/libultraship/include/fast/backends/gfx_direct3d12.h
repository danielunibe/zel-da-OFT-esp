#pragma once

#if defined(_WIN32)

#include "gfx_rendering_api.h"
#include "fast/interpreter.h"
#include <imgui.h>
#include "RtxQuality.h"
#include "fast/backends/DxrPipelinePolicy.h"
#include <chrono>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <deque>
#include <tuple>

namespace Fast {

class GfxWindowBackendDXGI;

struct ShaderProgramD3D12 {
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> hdrPsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> physicalVsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> physicalPsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> physicalHdrPsBlob;
    uint64_t shaderId0;
    uint32_t shaderId1;
    uint8_t numInputs;
    uint8_t numFloats;
    bool usedTextures[2];
    bool physicalShaderValid = false;
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    std::vector<D3D12_INPUT_ELEMENT_DESC> physicalInputElements;
};

struct TextureDataD3D12 {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint32_t width;
    uint32_t height;
    bool linearFiltering;
    uint32_t srvIndex;
    uint32_t samplerIndex;
    bool uploaded;
};

struct SamplerKeyD3D12 {
    bool linearFilter;
    uint32_t cms;
    uint32_t cmt;

    bool operator<(const SamplerKeyD3D12& other) const {
        return std::tie(linearFilter, cms, cmt) < std::tie(other.linearFilter, other.cms, other.cmt);
    }
};

struct FramebufferD3D12 {
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    uint32_t textureId;
    bool hasDepthBuffer;
    uint32_t msaaLevel;
    uint32_t width;
    uint32_t height;
};

class GfxRenderingAPIDX12 final : public GfxRenderingAPI {
public:
    GfxRenderingAPIDX12(GfxWindowBackendDXGI* backend);
    ~GfxRenderingAPIDX12() override;

    const char* GetName() override { return "Direct3D 12"; }
    bool SupportsRenderToFramebuffer() override { return true; }

    // Forensic phase 2: composite capture hook (invoked via TemporalManager callback)
    void ForensicCompositeCapture(int frame);
    int GetMaxTextureSize() override { return 16384; }
    GfxClipParameters GetClipParameters() override;

    void UnloadShader(struct ShaderProgram* oldPrg) override;
    void LoadShader(struct ShaderProgram* newPrg) override;
    struct ShaderProgram* CreateAndLoadNewShader(uint64_t shaderId0, uint32_t shaderId1) override;
    struct ShaderProgram* LookupShader(uint64_t shaderId0, uint32_t shaderId1) override;
    void ShaderGetInfo(struct ShaderProgram* prg, uint8_t* numInputs, bool usedTextures[2]) override;

    uint32_t NewTexture() override;
    void SelectTexture(int tile, uint32_t textureId) override;
    void SetTextureResourceIdentity(uint64_t resourceIdentity) override { mCurrentTextureResourceIdentity = resourceIdentity; }
    void UploadTexture(const uint8_t* rgba32Buf, uint32_t width, uint32_t height) override;
    void SetSamplerParameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) override;
    void DeleteTexture(uint32_t texId) override;
    void SetTextureFilter(FilteringMode mode) override;
    FilteringMode GetTextureFilter() override;
    void SetSrgbMode() override;
    ImTextureID GetTextureById(int id) override;

    void SetDepthTestAndMask(bool depth_test, bool z_upd) override;
    void SetZmodeDecal(bool decal) override;
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    void SetUseAlpha(bool useAlpha) override;

    void DrawTriangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) override;
    bool SupportsPhysicalDrawData() const override { return true; }
    const DxrFrameEvidence& GetDxrFrameEvidence() const { return mDxrFrameEvidence; }
    void DrawTrianglesPhysical(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris,
                               const PhysicalVertexData* physicalVertices, size_t physicalVertexCount,
                               const PhysicalDrawData& physicalDraw) override;

    void Init() override;
    void OnResize() override;
    void StartFrame() override;
    void BeginUiOverlayPass() override;
    void EndFrame() override;
    void FinishRender() override;

    int CreateFramebuffer() override;
    void UpdateFramebufferParameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                     bool opengl_invertY, bool render_target, bool has_depth_buffer,
                                     bool can_extract_depth) override;
    void StartDrawToFramebuffer(int fbId, float noiseScale) override;
    void CopyFramebuffer(int fbDstId, int fbSrcId, int srcX0, int srcY0, int srcX1, int srcY1,
                         int dstX0, int dstY0, int dstX1, int dstY1) override;
    void ClearFramebuffer(bool color, bool depth) override;
    void ReadFramebufferToCPU(int fbId, uint32_t width, uint32_t height, uint16_t* rgba16Buf) override;
    void ResolveMSAAColorBuffer(int fbIdTarget, int fbIdSrc) override;
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int fb_id, const std::set<std::pair<float, float>>& coordinates) override;
    void* GetFramebufferTextureId(int fbId) override;
    void SelectTextureFb(int fbId) override;

    void RunTonemappingPass(float fogR = 0.45f, float fogG = 0.55f, float fogB = 0.70f, float fogStrength = 0.0f,
                            float fogStart = 0.3f, float fogRange = 0.7f) override;
    void RecordFogDiagnosticSource(bool sceneFogEnabled, uint64_t fogBlendDraws, uint64_t geometryFogDraws,
                                   float gameFogMul, float gameFogOffset, float gameFogR, float gameFogG,
                                   float gameFogB) override;

    void PrepareInitFallback();
    bool IsInitialized() const { return mInitialized; }
    const std::string& GetAdapterName() const { return mAdapterName; }
    uint32_t GetVendorId() const { return mVendorId; }
    uint32_t GetDeviceId() const { return mDeviceId; }
    uint64_t GetDedicatedVideoMemory() const { return mDedicatedVideoMemory; }
    D3D_FEATURE_LEVEL GetFeatureLevel() const { return mFeatureLevel; }
    const char* GetDxrTierString() const { return mDxrTierString.c_str(); }
    uint64_t GetPresentedFrames() const { return mPresentedFrames; }
    uint64_t GetDrawCount() const { return mDrawCount; }
    uint64_t GetVertexCount() const { return mVertexCount; }
    uint64_t GetTriangleCount() const { return mTriangleCount; }
    uint64_t GetInvalidDrawCount() const { return mInvalidDrawCount; }
    bool WasDeviceRemoved() const { return mDeviceRemoved; }
    void InitGuiRenderer();
    void RenderGui(ImDrawData* data);
    void NewGuiFrame();
    void ShutdownGuiRenderer();
    ID3D12DescriptorHeap* GetTextureSrvHeap() const { return mSrvHeap.Get(); }
    uint32_t GetTextureSrvDescriptorSize() const { return mSrvDescriptorSize; }

private:
    static const uint32_t kFrameBufferCount = 3;
    static const uint32_t kSrvHeapSize = 16384;
    static const uint32_t kNullSrvIndex = 0;
    static const size_t kUploadBufferSize = 96 * 1024 * 1024;
    static const size_t kRingBufferSliceSize = 32 * 1024 * 1024;

    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
        uint64_t fenceValue;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> stagingBuffers;
        // Descriptors replaced while recording this frame cannot be reused
        // until the frame fence has completed: recorded descriptor tables
        // keep the GPU handle, not a copy of the descriptor contents.
        std::vector<uint32_t> retiredSrvIndices;
        size_t ringBufferBase;
        size_t ringBufferUsed;
    };

    struct PsoKey {
        uint64_t shaderId0;
        uint32_t shaderId1;
        bool depthTest;
        bool depthMask;
        bool decal;
        bool useAlpha;
        bool physicalInputs;
        bool hdrOutput;

        bool operator<(const PsoKey& o) const {
            if (shaderId0 != o.shaderId0) return shaderId0 < o.shaderId0;
            if (shaderId1 != o.shaderId1) return shaderId1 < o.shaderId1;
            if (depthTest != o.depthTest) return depthTest < o.depthTest;
            if (depthMask != o.depthMask) return depthMask < o.depthMask;
            if (decal != o.decal) return decal < o.decal;
            if (useAlpha != o.useAlpha) return useAlpha < o.useAlpha;
            if (physicalInputs != o.physicalInputs) return physicalInputs < o.physicalInputs;
            return hdrOutput < o.hdrOutput;
        }
    };

    ID3D12Resource* FramebufferColor(int id);
    ID3D12Resource* FramebufferDepth(int id);
    D3D12_RESOURCE_STATES& FramebufferColorState(int id);
    D3D12_CPU_DESCRIPTOR_HANDLE FramebufferRtv(int id);
    D3D12_CPU_DESCRIPTOR_HANDLE FramebufferDsv(int id);
    D3D12_CPU_DESCRIPTOR_HANDLE FramebufferDepthSrv(int id);
    uint32_t FramebufferWidth(int id) const;
    uint32_t FramebufferHeight(int id) const;
    void RestoreRasterState();
    void UpdateAutomaticQuality();
    bool CreateFramebufferCopyPipeline(DXGI_FORMAT outputFormat);
    bool ReadTexturePixels(ID3D12Resource* resource, D3D12_RESOURCE_STATES state,
                           std::vector<uint8_t>& bytes, uint32_t& rowPitch);
    void CaptureFogDepthDiagnostics(int target, uint64_t frame);
    void WaitForGpu();
    void WaitForFrame(uint32_t frameIndex);
    void MoveToNextFrame();
    void TransitionResource(ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetOrCreatePipelineState(const PsoKey& key, ShaderProgramD3D12* prog);
    bool CreateRootSignature();
    void CreateDescriptorHeaps();
    void CreateRenderTargetsAndDepth();
    void CleanUpRenderTargetsAndDepth();
    uint32_t AllocateSrvDescriptor();
    void ReleaseRetiredSrvDescriptors(uint32_t frameIndex);
    bool ValidateDraw(const char* stage);
    void ReleaseFrameStagingBuffers(uint32_t frameIndex);
    void DumpDebugMessages();

    GfxWindowBackendDXGI* mWindowBackend;
    bool mInitialized;

    // Metadatos de hardware
    std::string mAdapterName;
    uint32_t mVendorId;
    uint32_t mDeviceId;
    uint64_t mDedicatedVideoMemory;
    D3D_FEATURE_LEVEL mFeatureLevel;
    std::string mDxrTierString;

    // Objetos centrales de D3D12
    Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;

    // Sincronización y Frames in Flight
    FrameContext mFrameContexts[kFrameBufferCount];
    uint32_t mFrameIndex;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent;
    uint64_t mCurrentFenceValue;

    // Render Targets y Depth
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    uint32_t mRtvDescriptorSize;
    Microsoft::WRL::ComPtr<ID3D12Resource> mRenderTargets[kFrameBufferCount];
    D3D12_RESOURCE_STATES mBackbufferState[kFrameBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> mHdrSceneTargets[kFrameBufferCount];
    D3D12_RESOURCE_STATES mHdrSceneState[kFrameBufferCount]{};
    bool mHdrSceneActive = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    uint32_t mDsvDescriptorSize;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDepthSrvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mDepthSrvCpuHandle;
    uint32_t mRenderWidth;
    uint32_t mRenderHeight;
    bool mGuiRendererInitialized = false;
    int mPostprocessFramebuffer = -1;
    bool mFramebufferTonemap = false;
    float mFramebufferFog[6] = {};
    int mFramebufferFogDepthId = 0;
    bool mFogSourceSceneFogEnabled = false;
    uint64_t mFogSourceBlendDraws = 0;
    uint64_t mFogSourceGeometryDraws = 0;
    float mFogSourceGameMul = 0.0f;
    float mFogSourceGameOffset = 0.0f;
    float mFogSourceGameColor[3] = {};
    D3D12_RESOURCE_STATES mBackbufferDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mFramebufferCopyRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mFramebufferCopyPso[2];
    Microsoft::WRL::ComPtr<IDXGIAdapter3> mQualityAdapter;
    uint64_t mLastMemorySample=0;
    RtxQualityController mQualityController;
    uint64_t mLastQualitySample = 0;
    std::chrono::steady_clock::time_point mQualityTime = std::chrono::steady_clock::now();
    HMONITOR mQualityMonitor = nullptr;
    double mQualityTargetHz = 60;

    // Descriptores de recursos (SRV) y samplers
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvCpuHeap;
    uint32_t mLastDxrWidth=0, mLastDxrHeight=0;
    uint32_t mSrvDescriptorSize;
    uint32_t mNextSrvIndex;
    std::vector<uint32_t> mFreeSrvIndices;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
    uint32_t mSamplerDescriptorSize;
    uint32_t mNextSamplerIndex;
    std::map<SamplerKeyD3D12, uint32_t> mSamplerCache;
    uint32_t mCurrentSamplerIndices[SHADER_MAX_TEXTURES];
    // Dynamic Upload Ring Buffer para geometría
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    uint8_t* mUploadBufferMapped;
    const PhysicalVertexData* mPendingPhysicalVertices = nullptr;
    size_t mPendingPhysicalVertexCount = 0;
    PhysicalDrawData mPendingPhysicalDraw{};

    // Colecciones de recursos
    std::vector<TextureDataD3D12> mTextures;
    std::vector<FramebufferD3D12> mFrameBuffers;
    int mCurrentFramebuffer;
    int mCurrentTile;
    uint32_t mCurrentTextureIds[SHADER_MAX_TEXTURES];
    uint64_t mCurrentTextureResourceIdentity = 0;
    FilteringMode mCurrentFilterMode;

    // Cache de Shaders y Pipelines
    std::map<std::pair<uint64_t, uint32_t>, ShaderProgramD3D12> mShaderProgramPool;
    ShaderProgramD3D12* mCurrentShaderProgram;
    std::map<PsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPipelineStates;

    // Estados dinámicos de viewport y scissor
    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
    bool mViewportDirty;
    bool mScissorDirty;

    // Flags de dibujo
    bool mCurrentDepthTest;
    bool mCurrentDepthMask;
    bool mCurrentZmodeDecal;
    bool mCurrentUseAlpha;

    // Módulo de compilación
    HMODULE mCompilerModule;
    pD3DCompile mD3dCompile;

    // Métricas y diagnósticos
    uint64_t mPresentedFrames;
    uint64_t mDrawCount;
    uint64_t mVertexCount;
    uint64_t mTriangleCount;
    uint64_t mInvalidDrawCount;
    bool mCommandListOpen;
    bool mDxrPassesRunThisFrame = false;
    bool mDxrWorkAttemptedThisFrame = false;
    DxrFrameEvidence mDxrFrameEvidence{};
    bool mLastFrameHadDxr = false;
    bool mDeviceRemoved;

    // Diagnóstico debug layer
    bool mDebugLayerEnabled;

    // R4 — Pixel Output Isolation diagnostics
    int mPixelIsoMode; // 0=off, 1=P0, 2=P1, 3=P2, 4=P3, 5=P4, 20=C0, 21=C1, 22=C2, 23=C3
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDiagnosticPSO_P0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDiagnosticPSO_P1;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDiagnosticPSO_P2;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mDiagnosticPSO_P4;
    Microsoft::WRL::ComPtr<ID3DBlob> mSavedVSBlob;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mSavedInputElements;
    bool mDiagnosticPSOsCreated;
    uint64_t mDiagnosticDrawCount;
    void CreateDiagnosticPSOs();

    // R4-B — Valid-Vertex Canonical Draw protocol
    struct R4bCanonical {
        bool captured = false;          // canonical draw locked
        bool dumped = false;            // vertex/audit dump emitted
        bool settled = false;           // we observed a settled frame (>= r4bMinFrame)
        bool presentCheckArmed = false; // post-raster readback armed for VISIBLE/BLACK
        bool presentChecked = false;    // VISIBLE/BLACK already resolved
        bool presentVisible = false;    // readback result
        int frame = -1;                 // CANONICAL_FRAME
        int drawIndex = -1;             // CANONICAL_DRAW_INDEX
        uint64_t shaderId0 = 0;
        uint32_t shaderId1 = 0;
        uint32_t vertexCount = 0;
        uint32_t strideBytes = 0;
        double triangleArea = 0.0;      // approx screen-space area in NDC (x*w, y*w)
        int insideClip = 0;             // vertices inside clip volume
        int totalVerts = 0;             // vertices dumped for clip analysis
        bool depthTest = false, depthMask = false, decal = false, useAlpha = false;
        uint32_t tex0W = 0, tex0H = 0;
        // audits
        bool layoutStrideMatch = false;
        bool topologyMatch = false;
        bool drawArgsValid = false;
        bool colorWriteValid = false;
        float dumpV[6][8];             // up to 6 vertices, first 8 floats
        int dumpFloats = 0;
        R4bCanonical() { memset(dumpV, 0, sizeof(dumpV)); }
    };
    R4bCanonical mR4b;
    Microsoft::WRL::ComPtr<ID3DBlob> mR4bKnownVSBlob;          // C4: SV_VertexID triangle VS
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mR4bPSO_C0;    // solid PS, original states
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mR4bPSO_C1;    // depth off
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mR4bPSO_C2;    // cull off
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mR4bPSO_C3;    // full viewport/scissor
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mR4bPSO_C4;    // known VS
    Microsoft::WRL::ComPtr<ID3D12Resource> mR4bStatsReadback;  // pipeline statistics readback
    Microsoft::WRL::ComPtr<ID3D12Resource> mR4bPresentReadback; // backbuffer readback for VISIBLE/BLACK
    D3D12_QUERY_HEAP_DESC mR4bStatsHeapDesc = {};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> mR4bStatsHeap;
    uint64_t mR4bStatsQueryStartFrame = 0;
    bool mR4bStatsPendingRead = false;
    bool mR7c5A5Enabled = false;
    bool mR7c5A5QueryArmed = false;
    bool mR7c5A5QueryDone = false;
    uint64_t mR7c5A5DrawTrianglesEntered = 0;
    uint64_t mR7c5A5DrawTrianglesPhysicalEntered = 0;
    bool mR7c5A5CanonicalLogged = false;
    void CreateR4bDiagnosticPSOs();
    void BeginR4bFrameStats();
    void R4bFinalizeStatsQuery();
    void R4bArmPresentCheck();
    void R4bFrameFlushAndReport();
};

} // namespace Fast

#endif
