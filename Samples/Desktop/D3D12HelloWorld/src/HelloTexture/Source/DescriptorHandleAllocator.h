// DescriptorHandleAllocator: Simple allocator for CBV/SRV/UAV descriptor heap
#pragma once
#include "../DXSampleHelper.h"

using namespace DirectX;

struct DescriptorHandle
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};

class DescriptorHandleAllocator
{
public:
    DescriptorHandleAllocator(ID3D12Device* device, ComPtr<ID3D12DescriptorHeap> heap);

    // Allocate and return next available CPU handle
    DescriptorHandle Allocate();

    // Reset offset to zero (optional, if you want to reuse heap)
    void Reset();

    // Get current offset
    UINT GetOffset() const;

private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT m_incrementSize;
    UINT m_offset;
};