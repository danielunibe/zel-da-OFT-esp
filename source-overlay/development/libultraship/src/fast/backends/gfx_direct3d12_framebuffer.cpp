#if defined(_WIN32)
#define NOMINMAX
#include "fast/backends/gfx_direct3d12.h"
#include "fast/backends/gfx_dxgi.h"
#include "ship/Context.h"
#include "fast/MaterialRegistry.h"
#include "ship/config/ConsoleVariable.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Fast {
using Microsoft::WRL::ComPtr;
static const char* CouchColorFormatName(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        default: return "UNKNOWN";
    }
}
static void CouchColorLog(const std::string& line) {
    const char* harness = std::getenv("COUCH_DXR_HARNESS");
    const char* output = std::getenv("COUCH_DXR_OUTPUT");
    if (!harness || std::strcmp(harness, "1") != 0 || !output || !*output) return;
    std::ofstream log(std::filesystem::path(output) / "harness-backend.log", std::ios::binary | std::ios::app);
    if (log) log << line << '\n';
}
static float CouchGrayPatchMode() {
    const char* stage = std::getenv("COUCH_DXR_GRAY_PATCH_STAGE");
    if (!stage) return 0.0f;
    if (std::strcmp(stage, "POST_TONEMAP_LINEAR") == 0) return 1.0f;
    if (std::strcmp(stage, "PRE_TONEMAP_HDR") == 0) return 2.0f;
    return 0.0f;
}
static float CouchHalfToFloat(uint16_t half) {
    const uint32_t sign = uint32_t(half & 0x8000u) << 16;
    uint32_t exponent = (half >> 10) & 0x1fu;
    uint32_t mantissa = half & 0x03ffu;
    uint32_t bits = sign;
    if (exponent == 0) {
        if (mantissa != 0) {
            int unbiasedExponent = -14;
            while ((mantissa & 0x0400u) == 0) { mantissa <<= 1; --unbiasedExponent; }
            mantissa &= 0x03ffu;
            bits |= uint32_t(unbiasedExponent + 127) << 23;
            bits |= mantissa << 13;
        }
    } else if (exponent == 0x1fu) {
        bits |= 0x7f800000u | (mantissa << 13);
    } else {
        bits |= (exponent + 112u) << 23;
        bits |= mantissa << 13;
    }
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

ID3D12Resource* GfxRenderingAPIDX12::FramebufferColor(int id) {
    if (id == 0) return mHdrSceneActive ? mHdrSceneTargets[mFrameIndex].Get() : mRenderTargets[mFrameIndex].Get();
    return id > 0 && id < int(mFrameBuffers.size()) ? mFrameBuffers[id].renderTarget.Get() : nullptr;
}
ID3D12Resource* GfxRenderingAPIDX12::FramebufferDepth(int id) {
    if (id == 0) return mDepthStencilBuffer.Get();
    return id > 0 && id < int(mFrameBuffers.size()) ? mFrameBuffers[id].depth.Get() : nullptr;
}
D3D12_RESOURCE_STATES& GfxRenderingAPIDX12::FramebufferColorState(int id) {
    return id == 0 ? (mHdrSceneActive ? mHdrSceneState[mFrameIndex] : mBackbufferState[mFrameIndex])
                   : mFrameBuffers.at(id).colorState;
}
uint32_t GfxRenderingAPIDX12::FramebufferWidth(int id) const {
    return id == 0 ? mRenderWidth : mFrameBuffers.at(id).width;
}
uint32_t GfxRenderingAPIDX12::FramebufferHeight(int id) const {
    return id == 0 ? mRenderHeight : mFrameBuffers.at(id).height;
}
D3D12_CPU_DESCRIPTOR_HANDLE GfxRenderingAPIDX12::FramebufferRtv(int id) {
    auto handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    const size_t index = id == 0 ? (mHdrSceneActive ? kFrameBufferCount + mFrameIndex : mFrameIndex)
                                 : 2 * kFrameBufferCount + id;
    handle.ptr += index * mRtvDescriptorSize;
    return handle;
}
D3D12_CPU_DESCRIPTOR_HANDLE GfxRenderingAPIDX12::FramebufferDsv(int id) {
    auto handle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += size_t(id) * mDsvDescriptorSize;
    return handle;
}
D3D12_CPU_DESCRIPTOR_HANDLE GfxRenderingAPIDX12::FramebufferDepthSrv(int id) {
    auto handle = mDepthSrvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += size_t(id) * mSrvDescriptorSize;
    return handle;
}
int GfxRenderingAPIDX12::CreateFramebuffer() {
    if (mFrameBuffers.size() >= 256) throw std::runtime_error("D3D12 framebuffer capacity exceeded");
    FramebufferD3D12 fb{};
    fb.textureId = NewTexture();
    fb.msaaLevel = 1;
    mFrameBuffers.push_back(fb);
    return int(mFrameBuffers.size() - 1);
}
void GfxRenderingAPIDX12::UpdateFramebufferParameters(int id, uint32_t width, uint32_t height,
    uint32_t msaa, bool invert, bool target, bool depth, bool extractDepth) {
    if (!mInitialized || id < 0 || id >= int(mFrameBuffers.size())) return;
    auto& fb = mFrameBuffers[id];
    if (id == 0) { fb.width = mRenderWidth; fb.height = mRenderHeight; return; }
    width = std::max(1u, width); height = std::max(1u, height);
    // DXR consumes a single-sample depth SRV. Unsupported MSAA is clamped,
    // rather than exposing a setting with no actual GPU implementation.
    if (fb.renderTarget && fb.width == width && fb.height == height && fb.hasDepthBuffer == depth) return;
    auto& texture = mTextures.at(fb.textureId);
    if (fb.renderTarget) mFrameContexts[mFrameIndex].stagingBuffers.push_back(fb.renderTarget);
    if (fb.depth) mFrameContexts[mFrameIndex].stagingBuffers.push_back(fb.depth);
    if (texture.uploaded) mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(texture.srvIndex);
    fb.width = width; fb.height = height; fb.msaaLevel = 1; fb.hasDepthBuffer = depth;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[3] = 1;
    fb.renderTarget.Reset();
    if (FAILED(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&fb.renderTarget))))
        throw std::runtime_error("D3D12 framebuffer color allocation failed");
    fb.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    mDevice->CreateRenderTargetView(fb.renderTarget.Get(), nullptr, FramebufferRtv(id));
    texture.resource = fb.renderTarget; texture.width = width; texture.height = height;
    texture.srvIndex = AllocateSrvDescriptor(); texture.uploaded = true; texture.linearFiltering = true;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    auto srvCpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += size_t(texture.srvIndex) * mSrvDescriptorSize;
    mDevice->CreateShaderResourceView(fb.renderTarget.Get(), &srv, srvCpu);
    auto mirror = mSrvCpuHeap->GetCPUDescriptorHandleForHeapStart();
    mirror.ptr += size_t(texture.srvIndex) * mSrvDescriptorSize;
    mDevice->CreateShaderResourceView(fb.renderTarget.Get(), &srv, mirror);
    fb.depth.Reset();
    if (depth) {
        desc.Format = DXGI_FORMAT_R32_TYPELESS; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear = {}; clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 1;
        if (FAILED(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&fb.depth))))
            throw std::runtime_error("D3D12 framebuffer depth allocation failed");
        fb.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        mDevice->CreateDepthStencilView(fb.depth.Get(), &dsv, FramebufferDsv(id));
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        mDevice->CreateShaderResourceView(fb.depth.Get(), &srv, FramebufferDepthSrv(id));
    }
}
void GfxRenderingAPIDX12::StartDrawToFramebuffer(int id, float noiseScale) {
    if (!mCommandListOpen || id < 0 || id >= int(mFrameBuffers.size()) || !FramebufferColor(id)) return;
    mCurrentFramebuffer = id;
    auto& state = FramebufferColorState(id);
    TransitionResource(FramebufferColor(id), state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    RestoreRasterState();
}
void GfxRenderingAPIDX12::RestoreRasterState() {
    if (!mCommandListOpen) return;
    auto rtv = FramebufferRtv(mCurrentFramebuffer);
    auto dsv = FramebufferDsv(mCurrentFramebuffer);
    mCommandList->OMSetRenderTargets(1, &rtv, FALSE, FramebufferDepth(mCurrentFramebuffer) ? &dsv : nullptr);
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get(), mSamplerHeap.Get() };
    mCommandList->SetDescriptorHeaps(2, heaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    if (mViewport.Width > 0 && mViewport.Height > 0) mCommandList->RSSetViewports(1, &mViewport);
    if (mScissorRect.right > mScissorRect.left && mScissorRect.bottom > mScissorRect.top)
        mCommandList->RSSetScissorRects(1, &mScissorRect);
    mViewportDirty = mScissorDirty = false;
}
bool GfxRenderingAPIDX12::CreateFramebufferCopyPipeline(DXGI_FORMAT outputFormat) {
    const size_t outputIndex = outputFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1u : 0u;
    if (mFramebufferCopyPso[outputIndex]) return true;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    for (auto& range : ranges) { range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1; }
    ranges[1].BaseShaderRegister = 1;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1; params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 16; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters = 3; root.pParameters = params;
    root.NumStaticSamplers = 1; root.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> serialized, error, vs, ps;
    if (FAILED(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error))) return false;
    if (FAILED(mDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                          IID_PPV_ARGS(&mFramebufferCopyRoot)))) return false;
    const char* shader =
        "struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };"
        "V VSMain(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2);"
        "v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }"
        "Texture2D t:register(t0); Texture2D d:register(t1); SamplerState s:register(s0);"
        "cbuffer C:register(b0) {float4 rect;float4 tone;float4 fogColor;float4 fogParams;}"
        "float4 PSMain(V v):SV_Target {float4 c=t.SampleLevel(s,lerp(rect.xy,rect.zw,v.uv),0);"
        // FP16 scene variants already write linear RGB; the legacy SDR path
        // needs its display-encoded values decoded before the ACES shoulder.
        "if(tone.x>0.5) {float3 x=tone.y>0.5?max(c.rgb,0.0.xxx):pow(saturate(c.rgb),2.2);"
        "if(tone.w>1.5) x=0.18.xxx;"
        "x=saturate((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14));"
        "float z=d.SampleLevel(s,lerp(rect.xy,rect.zw,v.uv),0).r;"
        "float linearDepth=fogParams.x+z*fogParams.y;"
        "float fog=saturate(1.0-exp(-linearDepth*linearDepth));fog=fog*fog*fogColor.a;"
        "x=lerp(x,pow(max(fogColor.rgb,0.0),2.2),fog);"
        "if(tone.w>0.5 && tone.w<1.5) x=0.18.xxx;"
        "c.rgb=tone.z>0.5?x:pow(x,1.0/2.2);}return c;}";
    if (FAILED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &error)) ||
        FAILED(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &error))) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = mFramebufferCopyRoot.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    auto& blend=pso.BlendState.RenderTarget[0];
    blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pso.DepthStencilState.FrontFace.StencilFunc=pso.DepthStencilState.BackFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    pso.DepthStencilState.FrontFace.StencilFailOp=pso.DepthStencilState.FrontFace.StencilDepthFailOp=pso.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;
    pso.DepthStencilState.BackFace=pso.DepthStencilState.FrontFace;
    pso.SampleMask = UINT_MAX; pso.SampleDesc.Count = 1;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1; pso.RTVFormats[0] = outputFormat;
    return SUCCEEDED(mDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mFramebufferCopyPso[outputIndex])));
}
void GfxRenderingAPIDX12::CopyFramebuffer(int dst, int src, int sx0, int sy0, int sx1, int sy1,
    int dx0, int dy0, int dx1, int dy1) {
    if (!mCommandListOpen || src < 0 || dst < 0 || src >= int(mFrameBuffers.size()) || dst >= int(mFrameBuffers.size())) return;
    auto source = FramebufferColor(src); auto destination = FramebufferColor(dst);
    if (!source || !destination || source == destination || sx1 <= sx0 || sy1 <= sy0 || dx1 <= dx0 || dy1 <= dy0) return;
    auto& sourceState = FramebufferColorState(src); auto& destinationState = FramebufferColorState(dst);
    const auto previousSource = sourceState; const auto previousDestination = destinationState;
    // Same-size copies preserve exact pixels and don't disturb raster state.
    if (!mFramebufferTonemap && sx1-sx0 == dx1-dx0 && sy1-sy0 == dy1-dy0) {
        TransitionResource(source, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionResource(destination, destinationState, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = destination; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        if (sx0 >= 0 && sy0 >= 0 && dx0 >= 0 && dy0 >= 0 && sx1 <= int(FramebufferWidth(src)) &&
            sy1 <= int(FramebufferHeight(src)) && dx1 <= int(FramebufferWidth(dst)) && dy1 <= int(FramebufferHeight(dst))) {
            D3D12_BOX box{ UINT(sx0), UINT(sy0), 0, UINT(sx1), UINT(sy1), 1 };
            mCommandList->CopyTextureRegion(&to, dx0, dy0, 0, &from, &box);
        }
        TransitionResource(source, D3D12_RESOURCE_STATE_COPY_SOURCE, previousSource);
        TransitionResource(destination, D3D12_RESOURCE_STATE_COPY_DEST, previousDestination);
        return;
    }
    const DXGI_FORMAT sourceFormat = source->GetDesc().Format;
    const DXGI_FORMAT destinationFormat = destination->GetDesc().Format;
    if (!CreateFramebufferCopyPipeline(destinationFormat)) throw std::runtime_error("D3D12 scaled framebuffer copy pipeline failed");
    TransitionResource(source, sourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(destination, destinationState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    // Backbuffer SRVs are transient descriptors, retained until this frame completes.
    uint32_t srvIndex = src == 0 ? AllocateSrvDescriptor() : mTextures[mFrameBuffers[src].textureId].srvIndex;
    if (src == 0) {
        auto cpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += size_t(srvIndex)*mSrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=sourceFormat;
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=1;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        mDevice->CreateShaderResourceView(source,&view,cpu);
        mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(srvIndex);
    }
    auto gpu=mSrvHeap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr+=size_t(srvIndex)*mSrvDescriptorSize;
    auto rtv=FramebufferRtv(dst); mCommandList->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    ID3D12DescriptorHeap* heaps[]={mSrvHeap.Get()}; mCommandList->SetDescriptorHeaps(1,heaps);
    mCommandList->SetGraphicsRootSignature(mFramebufferCopyRoot.Get());
    const size_t outputIndex = destinationFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1u : 0u;
    mCommandList->SetPipelineState(mFramebufferCopyPso[outputIndex].Get());
    mCommandList->SetGraphicsRootDescriptorTable(0,gpu);
    const int depthId = mFramebufferTonemap ? mFramebufferFogDepthId : src;
    ID3D12Resource* depth = mFramebufferTonemap ? FramebufferDepth(depthId) : nullptr;
    uint32_t depthSrvIndex = 0;
    D3D12_RESOURCE_STATES* depthState = depthId == 0 ? &mBackbufferDepthState : &mFrameBuffers.at(depthId).depthState;
    if (depth) {
        depthSrvIndex = AllocateSrvDescriptor();
        auto depthCpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart(); depthCpu.ptr += size_t(depthSrvIndex)*mSrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC depthView{}; depthView.Format=DXGI_FORMAT_R32_FLOAT;
        depthView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        depthView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; depthView.Texture2D.MipLevels=1;
        mDevice->CreateShaderResourceView(depth,&depthView,depthCpu);
        auto depthGpu=mSrvHeap->GetGPUDescriptorHandleForHeapStart(); depthGpu.ptr+=size_t(depthSrvIndex)*mSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1,depthGpu);
        TransitionResource(depth,*depthState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mFrameContexts[mFrameIndex].retiredSrvIndices.push_back(depthSrvIndex);
    } else {
        mCommandList->SetGraphicsRootDescriptorTable(1,mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    }
    const float constants[]={float(sx0)/FramebufferWidth(src),float(sy0)/FramebufferHeight(src),
                      float(sx1)/FramebufferWidth(src),float(sy1)/FramebufferHeight(src),mFramebufferTonemap?1.0f:0.0f,
                      sourceFormat==DXGI_FORMAT_R16G16B16A16_FLOAT?1.0f:0.0f,
                      destinationFormat==DXGI_FORMAT_R16G16B16A16_FLOAT?1.0f:0.0f,CouchGrayPatchMode(),
                      mFramebufferFog[0],mFramebufferFog[1],mFramebufferFog[2],mFramebufferFog[3],
                      mFramebufferFog[4],mFramebufferFog[5],0,0};
    mCommandList->SetGraphicsRoot32BitConstants(2,16,constants,0);
    D3D12_VIEWPORT viewport{float(dx0),float(dy0),float(dx1-dx0),float(dy1-dy0),0,1};
    D3D12_RECT scissor{std::max(0,dx0),std::max(0,dy0),std::min(int(FramebufferWidth(dst)),dx1),std::min(int(FramebufferHeight(dst)),dy1)};
    mCommandList->RSSetViewports(1,&viewport); mCommandList->RSSetScissorRects(1,&scissor);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); mCommandList->DrawInstanced(3,1,0,0);
    if (depth) TransitionResource(depth,*depthState,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    TransitionResource(source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,previousSource);
    TransitionResource(destination,D3D12_RESOURCE_STATE_RENDER_TARGET,previousDestination);
    RestoreRasterState();
}
void GfxRenderingAPIDX12::ResolveMSAAColorBuffer(int dst,int src) {
    CopyFramebuffer(dst,src,0,0,FramebufferWidth(src),FramebufferHeight(src),0,0,FramebufferWidth(dst),FramebufferHeight(dst));
}
void* GfxRenderingAPIDX12::GetFramebufferTextureId(int id) {
    if (id <= 0 || id >= int(mFrameBuffers.size())) return nullptr;
    auto& state=FramebufferColorState(id);
    TransitionResource(FramebufferColor(id),state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return GetTextureById(mFrameBuffers[id].textureId);
}
void GfxRenderingAPIDX12::SelectTextureFb(int id) {
    if (id <= 0 || id >= int(mFrameBuffers.size())) return;
    GetFramebufferTextureId(id);
    SelectTexture(0,mFrameBuffers[id].textureId);
}
bool GfxRenderingAPIDX12::ReadTexturePixels(ID3D12Resource* source,D3D12_RESOURCE_STATES state,
    std::vector<uint8_t>& bytes,uint32_t& rowPitch) {
    if (!source || !mCommandListOpen) return false;
    const auto desc=source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{}; UINT rows; UINT64 rowSize,total;
    mDevice->GetCopyableFootprints(&desc,0,1,0,&layout,&rows,&rowSize,&total);
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=total;
    buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> staging;
    if (FAILED(mDevice->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&staging)))) return false;
    TransitionResource(source,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource=source; from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource=staging.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=layout;
    mCommandList->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    TransitionResource(source,D3D12_RESOURCE_STATE_COPY_SOURCE,state);
    mCommandList->Close(); mCommandListOpen=false;
    ID3D12CommandList* lists[]={mCommandList.Get()}; mCommandQueue->ExecuteCommandLists(1,lists);
    WaitForGpu();
    void* mapped=nullptr; D3D12_RANGE range{0,SIZE_T(total)};
    bool ok=SUCCEEDED(staging->Map(0,&range,&mapped)) && mapped;
    if (ok) { bytes.assign(static_cast<uint8_t*>(mapped),static_cast<uint8_t*>(mapped)+total); D3D12_RANGE noWrite{0,0}; staging->Unmap(0,&noWrite); }
    rowPitch=layout.Footprint.RowPitch;
    // Reset only the list. The allocator remains alive until its frame fence.
    mCommandList->Reset(mFrameContexts[mFrameIndex].commandAllocator.Get(),nullptr); mCommandListOpen=true;
    RestoreRasterState();
    return ok;
}
void GfxRenderingAPIDX12::ReadFramebufferToCPU(int id,uint32_t width,uint32_t height,uint16_t* out) {
    if (!out || id < 0 || id >= int(mFrameBuffers.size()) || width==0 || height==0) return;
    std::vector<uint8_t> bytes; uint32_t pitch=0;
    if (!ReadTexturePixels(FramebufferColor(id),FramebufferColorState(id),bytes,pitch)) return;
    const bool hdr = FramebufferColor(id)->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const uint32_t sw=FramebufferWidth(id),sh=FramebufferHeight(id);
    for (uint32_t y=0;y<height;++y) for (uint32_t x=0;x<width;++x) {
        const size_t sx = uint64_t(x)*sw/width, sy = uint64_t(y)*sh/height;
        uint8_t rgba[4]{};
        if (hdr) {
            const auto* half = reinterpret_cast<const uint16_t*>(bytes.data()+sy*pitch+sx*8);
            for (int channel=0; channel<3; ++channel) {
                const float decoded = CouchHalfToFloat(half[channel]);
                const float linear = std::isfinite(decoded) ? std::max(decoded,0.0f) : 0.0f;
                const float mapped = std::clamp((linear*(2.51f*linear+0.03f)) /
                                                (linear*(2.43f*linear+0.59f)+0.14f),0.0f,1.0f);
                const float encoded = mapped < 0.0031308f ? mapped*12.92f
                    : 1.055f*std::pow(mapped,1.0f/2.4f)-0.055f;
                rgba[channel]=static_cast<uint8_t>(std::clamp(int(encoded*255.0f+0.5f),0,255));
            }
            rgba[3]=CouchHalfToFloat(half[3])>0.0f?255:0;
        } else {
            const auto* source=bytes.data()+sy*pitch+sx*4;
            memcpy(rgba,source,4);
        }
        // Fast3D's CPU framebuffer is packed N64 RGBA5551: one uint16 per pixel.
        out[size_t(y)*width+x]=uint16_t((((rgba[0]+4)*31/255)<<11) | (((rgba[1]+4)*31/255)<<6) | (((rgba[2]+4)*31/255)<<1) | (rgba[3]!=0));
    }
}
void GfxRenderingAPIDX12::RecordFogDiagnosticSource(bool sceneFogEnabled, uint64_t fogBlendDraws,
    uint64_t geometryFogDraws, float gameFogMul, float gameFogOffset, float gameFogR, float gameFogG,
    float gameFogB) {
    mFogSourceSceneFogEnabled = sceneFogEnabled;
    mFogSourceBlendDraws = fogBlendDraws;
    mFogSourceGeometryDraws = geometryFogDraws;
    mFogSourceGameMul = gameFogMul;
    mFogSourceGameOffset = gameFogOffset;
    mFogSourceGameColor[0] = gameFogR;
    mFogSourceGameColor[1] = gameFogG;
    mFogSourceGameColor[2] = gameFogB;
}
void GfxRenderingAPIDX12::CaptureFogDepthDiagnostics(int target, uint64_t frame) {
    const char* diagnostics = std::getenv("COUCH_DXR_FOG_DIAGNOSTICS");
    const char* harness = std::getenv("COUCH_DXR_HARNESS");
    const char* output = std::getenv("COUCH_DXR_OUTPUT");
    if (!diagnostics || std::strcmp(diagnostics, "1") != 0 || !harness || std::strcmp(harness, "1") != 0 ||
        !output || !*output || !mCommandListOpen || target < 0 || target >= int(mFrameBuffers.size())) return;
    const std::filesystem::path root(output);
    const auto requestPath = root / "capture.request.json";
    if (!std::filesystem::exists(requestPath)) return;
    static uint64_t lastCapturedFrame = static_cast<uint64_t>(-1);
    if (lastCapturedFrame == frame) return;
    lastCapturedFrame = frame;
    std::ifstream requestFile(requestPath, std::ios::binary);
    const std::string request((std::istreambuf_iterator<char>(requestFile)), std::istreambuf_iterator<char>());
    auto jsonString = [&request](const char* key) {
        const std::string marker = std::string("\"") + key + "\":\"";
        const auto begin = request.find(marker);
        if (begin == std::string::npos) return std::string("UNKNOWN");
        const auto valueBegin = begin + marker.size();
        const auto end = request.find('"', valueBegin);
        return end == std::string::npos ? std::string("UNKNOWN") : request.substr(valueBegin, end - valueBegin);
    };
    const std::string viewHash = jsonString("view_hash");
    const std::string projectionHash = jsonString("projection_hash");
    ID3D12Resource* source = FramebufferColor(target);
    ID3D12Resource* depth = FramebufferDepth(target);
    ID3D12Resource* post = FramebufferColor(mPostprocessFramebuffer);
    if (!source || !depth || !post) return;
    std::vector<uint8_t> hdrBytes, depthBytes, postBytes;
    uint32_t hdrPitch = 0, depthPitch = 0, postPitch = 0;
    D3D12_RESOURCE_STATES* depthState = target == 0 ? &mBackbufferDepthState : &mFrameBuffers.at(target).depthState;
    if (!ReadTexturePixels(source, FramebufferColorState(target), hdrBytes, hdrPitch) ||
        !ReadTexturePixels(depth, *depthState, depthBytes, depthPitch) ||
        !ReadTexturePixels(post, FramebufferColorState(mPostprocessFramebuffer), postBytes, postPitch)) return;
    const uint32_t width = FramebufferWidth(target), height = FramebufferHeight(target);
    const DXGI_FORMAT sourceFormat = source->GetDesc().Format;
    const DXGI_FORMAT depthFormat = depth->GetDesc().Format;
    const DXGI_FORMAT postFormat = post->GetDesc().Format;
    struct Sample { uint32_t x, y; float depth; };
    std::vector<Sample> validDepth;
    uint64_t nonFiniteDepth = 0;
    for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        float z = 0.0f;
        std::memcpy(&z, depthBytes.data() + size_t(y) * depthPitch + size_t(x) * sizeof(float), sizeof(z));
        if (!std::isfinite(z)) { ++nonFiniteDepth; continue; }
        if (z > 0.0f && z < 0.999999f) validDepth.push_back({x, y, z});
    }
    std::sort(validDepth.begin(), validDepth.end(), [](const Sample& a, const Sample& b) { return a.depth < b.depth; });
    const size_t quantiles[] = {validDepth.size() / 4, validDepth.size() / 2, (validDepth.size() * 3) / 4};
    const char* labels[] = {"NEAR", "MID", "FAR"};
    std::ostringstream samples;
    samples << std::setprecision(9) << "[";
    bool first = true;
    const bool halfFloat = sourceFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
    uint64_t nonFiniteHdr = 0;
    for (uint32_t y = 0; halfFloat && y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const auto* rgba = reinterpret_cast<const uint16_t*>(hdrBytes.data() + size_t(y) * hdrPitch + size_t(x) * 8);
        for (int c = 0; c < 4; ++c) if (!std::isfinite(CouchHalfToFloat(rgba[c]))) ++nonFiniteHdr;
    }
    for (size_t i = 0; i < 3 && !validDepth.empty(); ++i) {
        const Sample& sample = validDepth[std::min(quantiles[i], validDepth.size() - 1)];
        const uint8_t* out = postBytes.data() + size_t(sample.y) * postPitch + size_t(sample.x) * 4;
        float before[3] = {}, fogColorLinear[3] = {}, expected[3] = {}, actual[3] = {};
        const auto* rgba16 = halfFloat ? reinterpret_cast<const uint16_t*>(hdrBytes.data() + size_t(sample.y) * hdrPitch + size_t(sample.x) * 8) : nullptr;
        for (int c = 0; c < 3; ++c) {
            const float hdr = halfFloat ? CouchHalfToFloat(rgba16[c]) : 0.0f;
            const float linear = std::max(std::isfinite(hdr) ? hdr : 0.0f, 0.0f);
            before[c] = std::clamp((linear * (2.51f * linear + 0.03f)) /
                                   (linear * (2.43f * linear + 0.59f) + 0.14f), 0.0f, 1.0f);
            fogColorLinear[c] = std::pow(std::max(mFramebufferFog[c], 0.0f), 2.2f);
        }
        const float depthUsed = mFramebufferFog[4] + sample.depth * mFramebufferFog[5];
        const float factorRaw = std::clamp(1.0f - std::exp(-depthUsed * depthUsed), 0.0f, 1.0f);
        const float factorFinal = factorRaw * factorRaw * mFramebufferFog[3];
        for (int c = 0; c < 3; ++c) {
            expected[c] = before[c] + (fogColorLinear[c] - before[c]) * factorFinal;
            actual[c] = std::pow(float(out[c]) / 255.0f, 2.2f);
        }
        const float error = std::max({std::fabs(expected[0] - actual[0]), std::fabs(expected[1] - actual[1]),
                                      std::fabs(expected[2] - actual[2])});
        if (!first) samples << ',';
        first = false;
        samples << "{\"label\":\"" << labels[i] << "\",\"x\":" << sample.x << ",\"y\":" << sample.y
                << ",\"depth_raw\":" << sample.depth << ",\"depth_used\":" << depthUsed
                << ",\"fog_factor_raw\":" << factorRaw << ",\"fog_factor_final\":" << factorFinal
                << ",\"rgb_before_aces_fog\":[" << before[0] << ',' << before[1] << ',' << before[2]
                << "],\"rgb_fog_target_linear\":[" << fogColorLinear[0] << ',' << fogColorLinear[1] << ',' << fogColorLinear[2]
                << "],\"rgb_expected_linear\":[" << expected[0] << ',' << expected[1] << ',' << expected[2]
                << "],\"rgb_actual_post_unorm_gamma22_decoded\":[" << actual[0] << ',' << actual[1] << ',' << actual[2]
                << "],\"rgb_actual_encoded_unorm\":[" << unsigned(out[0]) << ',' << unsigned(out[1]) << ',' << unsigned(out[2])
                << "],\"max_abs_error_linear\":" << error << '}';
    }
    samples << ']';
    std::ofstream json(root / ("fog-depth-samples_frame" + std::to_string(frame) + ".json"), std::ios::binary);
    if (!json) return;
    json << std::setprecision(9) << "{\n  \"frame_id\": " << frame << ",\n  \"view_hash\": \"" << viewHash
         << "\",\n  \"projection_hash\": \"" << projectionHash << "\",\n  \"dimensions\": [" << width << ',' << height
         << "],\n  \"source_color_format\": \"" << CouchColorFormatName(sourceFormat)
         << "\",\n  \"depth_resource_format\": " << depthFormat << ",\n  \"depth_srv_format\": \"R32_FLOAT\",\n  \"postprocess_format\": \""
         << CouchColorFormatName(postFormat) << "\",\n  \"postprocess_transfer\": \"gamma22_approx_unorm; actual linear values are gamma22-decoded estimates\",\n"
         << "  \"depth_transform\": \"depth_used=fogStart+depthRaw*fogRange; factorRaw=saturate(1-exp(-depth_used^2)); factorFinal=factorRaw^2*fogStrength\",\n"
         << "  \"fog_enabled\": " << (mFramebufferFog[3] > 0.001f ? "true" : "false") << ",\n  \"fog_color_srgb_like\": ["
         << mFramebufferFog[0] << ',' << mFramebufferFog[1] << ',' << mFramebufferFog[2] << "],\n  \"fog_strength\": " << mFramebufferFog[3]
         << ",\n  \"fog_start\": " << mFramebufferFog[4] << ",\n  \"fog_range\": " << mFramebufferFog[5]
         << ",\n  \"source_diagnostic_same_backend_frame\": {\"scene_geometry_fog\": " << (mFogSourceSceneFogEnabled ? "true" : "false")
         << ", \"fog_blend_draws\": " << mFogSourceBlendDraws << ", \"geometry_fog_draws\": " << mFogSourceGeometryDraws
         << ", \"game_fog_mul\": " << mFogSourceGameMul << ", \"game_fog_offset\": " << mFogSourceGameOffset
         << ", \"rdp_fog_rgb_linearized_8bit\": [" << mFogSourceGameColor[0] << ',' << mFogSourceGameColor[1] << ',' << mFogSourceGameColor[2]
         << "]},\n  \"valid_depth_pixel_count\": " << validDepth.size() << ",\n  \"depth_nonfinite_count\": " << nonFiniteDepth
         << ",\n  \"hdr_nonfinite_channel_count\": " << nonFiniteHdr << ",\n  \"samples\": " << samples.str() << "\n}\n";
}
std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff>
GfxRenderingAPIDX12::GetPixelDepth(int id,const std::set<std::pair<float,float>>& coordinates) {
    std::unordered_map<std::pair<float,float>,uint16_t,hash_pair_ff> result;
    if (id < 0 || id >= int(mFrameBuffers.size())) return result;
    std::vector<uint8_t> bytes; uint32_t pitch=0;
    if (!ReadTexturePixels(FramebufferDepth(id),D3D12_RESOURCE_STATE_DEPTH_WRITE,bytes,pitch)) return result;
    for (const auto& point:coordinates) {
        int x=std::clamp(int(point.first),0,int(FramebufferWidth(id))-1);
        int y=std::clamp(int(FramebufferHeight(id))-1-int(point.second),0,int(FramebufferHeight(id))-1);
        float depth; memcpy(&depth,bytes.data()+size_t(y)*pitch+size_t(x)*4,sizeof(depth));
        result[point]=uint16_t(std::clamp(depth,0.0f,1.0f)*65532.0f);
    }
    return result;
}
void GfxRenderingAPIDX12::RunTonemappingPass(float fogR,float fogG,float fogB,float fogStrength,float fogStart,float fogRange) {
    if (!mCommandListOpen || !MaterialRegistry::IsEnhancedVisualProfile()) return;
    const auto consoleVariables = Ship::Context::GetInstance()->GetConsoleVariables();
    const bool tonemappingEnabled = !consoleVariables ||
        consoleVariables->GetInteger("gEnhancements.Graphics.DXR.Tonemapping",1) != 0;
    // An active FP16 scene must always be resolved to the swapchain, even if
    // the setting changes while the frame is already being recorded.
    if (!tonemappingEnabled && !mHdrSceneActive) return;
    const int target=mCurrentFramebuffer;
    const bool hdrSceneWasActive = mHdrSceneActive;
    const DXGI_FORMAT rasterSourceFormat = FramebufferColor(target)
        ? FramebufferColor(target)->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
    mFramebufferFogDepthId=target;
    mFramebufferFog[0]=fogR; mFramebufferFog[1]=fogG; mFramebufferFog[2]=fogB;
    mFramebufferFog[3]=std::clamp(fogStrength,0.0f,1.0f);
    mFramebufferFog[4]=fogStart; mFramebufferFog[5]=fogRange;
    const uint32_t width=FramebufferWidth(target),height=FramebufferHeight(target);
    if (mPostprocessFramebuffer<0) mPostprocessFramebuffer=CreateFramebuffer();
    UpdateFramebufferParameters(mPostprocessFramebuffer,width,height,1,false,true,false,false);
    if (target == 0 && mHdrSceneActive) {
        // Resolve FP16 scene radiance through ACES into an SDR intermediate,
        // then transfer that image to the swapchain before Fast3D draws HUD.
        mFramebufferTonemap=true;
        CopyFramebuffer(mPostprocessFramebuffer,target,0,0,width,height,0,0,width,height);
        CaptureFogDepthDiagnostics(target, mPresentedFrames + 1);
        mFramebufferTonemap=false;
        mHdrSceneActive=false;
        CopyFramebuffer(target,mPostprocessFramebuffer,0,0,width,height,0,0,width,height);
        TransitionResource(mRenderTargets[mFrameIndex].Get(),mBackbufferState[mFrameIndex],
                           D3D12_RESOURCE_STATE_RENDER_TARGET);
        mBackbufferState[mFrameIndex]=D3D12_RESOURCE_STATE_RENDER_TARGET;
        RestoreRasterState();
    } else {
        CopyFramebuffer(mPostprocessFramebuffer,target,0,0,width,height,0,0,width,height);
        mFramebufferTonemap=true;
        CopyFramebuffer(target,mPostprocessFramebuffer,0,0,width,height,0,0,width,height);
        mFramebufferTonemap=false;
    }
    MaterialRegistry::IncrTonemapPass();
    if (hdrSceneWasActive) {
        const char* harness = std::getenv("COUCH_DXR_HARNESS");
        const char* output = std::getenv("COUCH_DXR_OUTPUT");
        if (harness && std::strcmp(harness, "1") == 0 && output && *output) {
            const std::filesystem::path runRoot(output);
            const auto requestPath = runRoot / "capture.request.json";
            static uint64_t sLastColorFrame = static_cast<uint64_t>(-1);
            const uint64_t frame = mPresentedFrames + 1;
            if (std::filesystem::exists(requestPath) && frame != sLastColorFrame) {
                sLastColorFrame = frame;
                std::ifstream requestFile(requestPath, std::ios::binary);
                const std::string request((std::istreambuf_iterator<char>(requestFile)),
                                          std::istreambuf_iterator<char>());
                std::string scene = "UNKNOWN";
                int room = -1;
                const auto sceneKey = request.find("\"scene\":\"");
                if (sceneKey != std::string::npos) {
                    const auto begin = sceneKey + 9;
                    const auto end = request.find('"', begin);
                    if (end != std::string::npos) scene = request.substr(begin, end - begin);
                }
                const auto roomKey = request.find("\"room\":");
                if (roomKey != std::string::npos) room = std::atoi(request.c_str() + roomKey + 7);
                const DXGI_FORMAT postFormat = FramebufferColor(mPostprocessFramebuffer)
                    ? FramebufferColor(mPostprocessFramebuffer)->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
                const DXGI_FORMAT swapchainFormat = mRenderTargets[mFrameIndex]
                    ? mRenderTargets[mFrameIndex]->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
                std::ostringstream record;
                record << "[COUCH-DXR-COLOR-FRAME] frame=" << frame << " scene=" << scene << " room=" << room
                       << " rasterFormat=" << CouchColorFormatName(rasterSourceFormat)
                       << " compositeFormat=" << CouchColorFormatName(rasterSourceFormat)
                       << " postAcesFormat=" << CouchColorFormatName(postFormat)
                       << " swapchainFormat=" << CouchColorFormatName(swapchainFormat)
                       << " finalRTVFormat=" << CouchColorFormatName(swapchainFormat)
                       << " finalSRVFormat=NONE dxgiColorSpace=NOT_SET"
                       << " srgbEncodeCount=0 srgbDecodeCount=0 acesCount=1 exposureCount=0 tonemapCount=1"
                       << " manualSRGBEncode=0 srgbRTVEncode=0 hdrLinearUntilTonemap="
                       << (rasterSourceFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1 : 0)
                       << " outputTransfer=GAMMA22_APPROX";
                CouchColorLog(record.str());
            }
        }
    }
    if (fogStrength > 0.001f && FramebufferDepth(target)) MaterialRegistry::IncrAtmosphericPass();
}
} // namespace Fast
#endif
