//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include "DXSample.h"

using namespace DirectX;

struct GPUBufferWithSRV
{
    ComPtr<ID3D12Resource> UploadBuffer;
    ComPtr<ID3D12Resource> Buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
};



struct GPUBufferWithSRV_NoUpload
{
    ComPtr<ID3D12Resource> Buffer;

    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE UavCpuHandle;

    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle;    
    D3D12_GPU_DESCRIPTOR_HANDLE UavGpuHandle;

    void SetSRVResourceHandle(uint32_t offset, UINT m_cbv_srv_uavDescriptorSize, ComPtr<ID3D12DescriptorHeap> m_srvHeap);
    void SetUAVResourceHandle(uint32_t offset, UINT m_cbv_srv_uavDescriptorSize, ComPtr<ID3D12DescriptorHeap> m_srvHeap);
    void CreateBuffer(ID3D12Device* device, UINT structureStride, UINT elementNum);
};


// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTexture : public DXSample
{
public:
    D3D12HelloTexture(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();

private:
    static const UINT FrameCount = 2;
    static const UINT TextureWidth = 256;
    static const UINT TextureHeight = 256;
    static const UINT TexturePixelSize = 4;    // The number of bytes used to represent a pixel in the texture.

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT2 uv;
    };

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;
    UINT m_cbv_srv_uavDescriptorSize;

    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_texture;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    void LoadPipeline();
    void LoadAssets();
    std::vector<UINT8> GenerateTextureData();
    void PopulateCommandList();
    void WaitForPreviousFrame();

    //-
    UINT descriptorSize;
    bool m_osSupportsCoopVec;

    GPUBufferWithSRV m_LatentBuffer;
    GPUBufferWithSRV m_WeightBuffer;
    GPUBufferWithSRV m_ConstantBuffer;
    ComPtr<ID3D12Resource> m_UploadBuffer;


    GPUBufferWithSRV_NoUpload TileAllocatorBuffer;
    GPUBufferWithSRV_NoUpload TileDataPackedStructuredBuffer;

    GPUBufferWithSRV_NoUpload RayAllocatorBuffer;
    GPUBufferWithSRV_NoUpload RayDataPackedStructuredBuffer;


    ComPtr<ID3D12RootSignature> m_rootSignature_CS_CreateTile;
    ComPtr<ID3D12PipelineState> m_pipelineState_CS_CreateTile;

    ComPtr<ID3D12RootSignature> m_rootSignature_CS_CreateRay;
    ComPtr<ID3D12PipelineState> m_pipelineState_CS_CreateRay;

    ComPtr<ID3D12RootSignature> m_rootSignature_CS_TestOutput;
    ComPtr<ID3D12PipelineState> m_pipelineState_CS_TestOutput;

    bool LoadNTCFile();


    //- Test SM69 UE
    void LoadAssets_UE_CS();
    void _Execute_CS_CreateTile();
    void _Execute_CS_CreateRay();
};
