#pragma once
#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>
namespace Fast {
inline void InitializeDxrDescriptorTemplate(ID3D12Device* device,ID3D12DescriptorHeap* heap) {
    D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};
    nullView.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    nullView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    nullView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullView.Texture2D.MipLevels=1;
    auto handle=heap->GetCPUDescriptorHandleForHeapStart();
    const auto stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i=0;i<heap->GetDesc().NumDescriptors;++i,handle.ptr+=stride)
        device->CreateShaderResourceView(nullptr,&nullView,handle);
}
// Static descriptors are authored in a CPU-only heap. Each in-flight frame
// gets a shader-visible copy so later CPU writes cannot alter queued work.
template<size_t N>
void SelectDxrDescriptorHeap(ID3D12Device* device,
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& active,
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& cpuTemplate,
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>,N>& frames,size_t slot) {
    if (!cpuTemplate) cpuTemplate=active;
    slot%=N;
    if (!frames[slot]) {
        auto desc=cpuTemplate->GetDesc(); desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&frames[slot]))))
            throw std::runtime_error("DXR descriptor ring allocation failed");
        device->CopyDescriptorsSimple(desc.NumDescriptors,frames[slot]->GetCPUDescriptorHandleForHeapStart(),
            cpuTemplate->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    active=frames[slot];
}
}
#endif
