#include "stdafx.h"
#include "DescriptorHandleAllocator.h"

DescriptorHandleAllocator::DescriptorHandleAllocator(ID3D12Device* device, ComPtr<ID3D12DescriptorHeap> heap)
    : m_heap(heap)
{
    m_incrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_offset = 0;
}

    // Allocate and return next available CPU handle
DescriptorHandle DescriptorHandleAllocator::Allocate()
{
	DescriptorHandle descriptorHandle = {};

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = m_heap->GetCPUDescriptorHandleForHeapStart();
    descriptorHandle.cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuBase, m_offset, m_incrementSize);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = m_heap->GetGPUDescriptorHandleForHeapStart();
    descriptorHandle.gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuBase, m_offset, m_incrementSize);

    ++m_offset;
    return descriptorHandle;
}

// Reset offset to zero (optional, if you want to reuse heap)
void DescriptorHandleAllocator::Reset()
{
    m_offset = 0;
}

// Get current offset
UINT DescriptorHandleAllocator::GetOffset() const
{
    return m_offset;
}
