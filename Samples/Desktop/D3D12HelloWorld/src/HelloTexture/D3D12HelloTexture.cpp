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

#include "stdafx.h"
#include "D3D12HelloTexture.h"

#include "libntc/ntc.h"
#include "libntc/wrappers.h"

#include <dxcapi.h> // DXC

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 717; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

D3D12HelloTexture::D3D12HelloTexture(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0)
{
}

void D3D12HelloTexture::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloTexture::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    UUID Features[] = { D3D12ExperimentalShaderModels, D3D12CooperativeVectorExperiment };
    ThrowIfFailed(D3D12EnableExperimentalFeatures(_countof(Features), Features, nullptr, nullptr));

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
        ));
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS_EXPERIMENTAL FeatureDataTier = {};
    ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS_EXPERIMENTAL,
        &FeatureDataTier,
        sizeof(FeatureDataTier)));
    if (FeatureDataTier.CooperativeVectorTier >= D3D12_COOPERATIVE_VECTOR_TIER_1_0)
    {
        // Have Tier 1 cooperative vector support (there's also a Tier 1.1 for training operations)\

        Microsoft::WRL::ComPtr<ID3D12DevicePreview> devicePreview;
        m_device->QueryInterface(IID_PPV_ARGS(&devicePreview));

        D3D12_LINEAR_ALGEBRA_MATRIX_CONVERSION_DEST_INFO convertInfo = { 0, D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_MUL_OPTIMAL, 48, 64, 48, D3D12_LINEAR_ALGEBRA_DATATYPE_SINT8 };
        devicePreview->GetLinearAlgebraMatrixConversionDestinationInfo(&convertInfo);
        if (convertInfo.DestSize == 0)
        {
            m_osSupportsCoopVec = true;
        }
    }
    
    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a shader resource view (SRV) heap for the texture.
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 50;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_cbv_srv_uavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

// Load the sample assets.
void D3D12HelloTexture::LoadAssets()
{
    // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();

#if 0
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ID3DBlob* errorBlob0 = nullptr;
        ID3DBlob* errorBlob1 = nullptr;

        HRESULT hr = D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob0);
        D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob1);

        if (FAILED(hr)) 
        {
            if (errorBlob0)
            {
                OutputDebugStringA((char*)errorBlob0->GetBufferPointer());
                errorBlob0->Release();
            }
            ThrowIfFailed(hr);
        }

        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
#endif

#if 0
        ComPtr<IDxcCompiler3> dxcCompiler;
        ComPtr<IDxcLibrary> dxcLibrary;
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
        DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&dxcLibrary));

        ComPtr<IDxcBlobEncoding> sourceBlob;
        dxcLibrary->CreateBlobFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, &sourceBlob);

        LPCWSTR vsArgs[] = { L"-T", L"vs_6_9", L"-E", L"VSMain",  L"-HV", L"2021"};
        DxcBuffer sourceBuffer = { sourceBlob->GetBufferPointer(), sourceBlob->GetBufferSize(), DXC_CP_UTF8 };
        ComPtr<IDxcResult> vsResult;
        HRESULT hr = dxcCompiler->Compile(&sourceBuffer, vsArgs, _countof(vsArgs), nullptr, IID_PPV_ARGS(&vsResult));

        LPCWSTR psArgs[] = { L"-T", L"ps_6_9", L"-E", L"PSMain", L"-HV", L"2021"};
        ComPtr<IDxcResult> psResult;
        hr = dxcCompiler->Compile(&sourceBuffer, psArgs, _countof(psArgs), nullptr, IID_PPV_ARGS(&psResult));

        ComPtr<IDxcBlob> vertexShader;
        ComPtr<IDxcBlob> pixelShader;
        ComPtr<IDxcBlobUtf8> vsErrors;
        ComPtr<IDxcBlobUtf8> psErrors;
        vsResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&vertexShader), nullptr);
        psResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pixelShader), nullptr);
        vsResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&vsErrors), nullptr);
        psResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&psErrors), nullptr);

        if (vsErrors)
        {
            OutputDebugStringA((char*)vsErrors->GetBufferPointer());
            vsErrors->Release();
        }

        if (psErrors)
        {
            OutputDebugStringA((char*)psErrors->GetBufferPointer());
            psErrors->Release();
        }

        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };        
#endif        
        
#if 1
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

        D3DReadFileToBlob(L"compiled/VSMain.cso", &vertexShader);
        D3DReadFileToBlob(L"compiled/PSMain.cso", &pixelShader);

        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
#endif

        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Create the vertex buffer.
    {
#if 0
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 0.5f, 0.0f } },
            { { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f } }
        };
#endif

        // Draw full screen quad.
        Vertex fullscreenQuad[] =
        {
            { { -1.0f,  1.0f, 0.0f },           { 0.0f, 0.0f } },
            { {  1.0f,  1.0f, 0.0f },           { 1.0f, 0.0f } },
            { { -1.0f, -1.0f, 0.0f },           { 0.0f, 1.0f } },

            { { -1.0f, -1.0f, 0.0f },           { 0.0f, 1.0f } },
            { {  1.0f,  1.0f, 0.0f },           { 1.0f, 0.0f } },
            { {  1.0f, -1.0f, 0.0f },           { 1.0f, 1.0f } },
        };

        const UINT vertexBufferSize = sizeof(fullscreenQuad);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, fullscreenQuad, sizeof(fullscreenQuad));
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Note: ComPtr's are CPU objects but this resource needs to stay in scope until
    // the command list that references it has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resource is not
    // prematurely destroyed.
    ComPtr<ID3D12Resource> textureUploadHeap;

    // Create the texture.
    {
        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = TextureWidth;
        textureDesc.Height = TextureHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_texture)));

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

        // Create the GPU upload buffer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap)));

        // Copy data to the intermediate upload heap and then schedule a copy 
        // from the upload heap to the Texture2D.
        std::vector<UINT8> texture = GenerateTextureData();

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = &texture[0];
        textureData.RowPitch = TextureWidth * TexturePixelSize;
        textureData.SlicePitch = textureData.RowPitch * TextureHeight;

        UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    LoadNTCFile();
    LoadAssets_UE_CS();
    
    // Close the command list and execute it to begin the initial GPU setup.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.
        WaitForPreviousFrame();
    }
}

// Generate a simple black and white checkerboard texture.
std::vector<UINT8> D3D12HelloTexture::GenerateTextureData()
{
    const UINT rowPitch = TextureWidth * TexturePixelSize;
    const UINT cellPitch = rowPitch >> 3;        // The width of a cell in the checkboard texture.
    const UINT cellHeight = TextureWidth >> 3;    // The height of a cell in the checkerboard texture.
    const UINT textureSize = rowPitch * TextureHeight;

    std::vector<UINT8> data(textureSize);
    UINT8* pData = &data[0];

    for (UINT n = 0; n < textureSize; n += TexturePixelSize)
    {
        UINT x = n % rowPitch;
        UINT y = n / rowPitch;
        UINT i = x / cellPitch;
        UINT j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;        // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }

    return data;
}

// Update frame-based values.
void D3D12HelloTexture::OnUpdate()
{
}

// Render the scene.
void D3D12HelloTexture::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    WaitForPreviousFrame();
}

void D3D12HelloTexture::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloTexture::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    _Execute_CS_CreateTile();
    _Execute_CS_CreateRay();

    m_commandList->SetPipelineState(m_pipelineState.Get());

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootDescriptorTable(1, RayAllocatorBuffer.SrvGpuHandle);

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(6, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTexture::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // Wait until the previous frame is finished.
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}


inline void log_warning(const char* fmt, ...)
{
    constexpr int BufferSize = 1024;
    char buffer[BufferSize];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, BufferSize, fmt, args);
    va_end(args);

    fprintf(stderr, "[Warning] %s\n", buffer);
}

inline void log_error(const char* fmt, ...)
{
    log_warning(fmt);
}


GPUBufferWithSRV CreateStructuredOrRawSRVBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    SIZE_T dataSize,
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle,
    bool isStructuredBuffer = false,
    UINT structureStride = 0)
{
    GPUBufferWithSRV result = {};
    result.SrvCpuHandle = srvCpuHandle;

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
        dataSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&result.Buffer)));

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(dataSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&result.UploadBuffer)));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = isStructuredBuffer ? (UINT)(dataSize / structureStride) : (UINT)(dataSize / 4);
    srvDesc.Buffer.StructureByteStride = isStructuredBuffer ? structureStride : 0;
    srvDesc.Format = isStructuredBuffer ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
    srvDesc.Buffer.Flags = isStructuredBuffer ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;

    device->CreateShaderResourceView(result.Buffer.Get(), &srvDesc, srvCpuHandle);

    return result;
}


void GPUBufferWithSRV_NoUpload::SetSRVResourceHandle(uint32_t offset, UINT m_cbv_srv_uavDescriptorSize, ComPtr<ID3D12DescriptorHeap> m_srvHeap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle_cpu_start = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	SrvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(handle_cpu_start, offset, m_cbv_srv_uavDescriptorSize);

    D3D12_GPU_DESCRIPTOR_HANDLE handle_gpu_start = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    SrvGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(handle_gpu_start, offset, m_cbv_srv_uavDescriptorSize);
}

void GPUBufferWithSRV_NoUpload::SetUAVResourceHandle(uint32_t offset, UINT m_cbv_srv_uavDescriptorSize, ComPtr<ID3D12DescriptorHeap> m_srvHeap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle_cpu_start = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    UavCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(handle_cpu_start, offset, m_cbv_srv_uavDescriptorSize);

    D3D12_GPU_DESCRIPTOR_HANDLE handle_gpu_start = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    UavGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(handle_gpu_start, offset, m_cbv_srv_uavDescriptorSize);
}

void GPUBufferWithSRV_NoUpload::CreateBuffer(ID3D12Device* device, UINT structureStride, UINT elementNum)
{
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
        structureStride * elementNum,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&Buffer)));


    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = elementNum;
    srvDesc.Buffer.StructureByteStride = structureStride;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(Buffer.Get(), &srvDesc, SrvCpuHandle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = elementNum;
    uavDesc.Buffer.StructureByteStride = structureStride;    
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    device->CreateUnorderedAccessView(Buffer.Get(), nullptr, &uavDesc, UavCpuHandle);
}



void WriteBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const GPUBufferWithSRV& src,
    const void* initData,
    SIZE_T dataSize)
{
    void* mapped = nullptr;
    ThrowIfFailed(src.UploadBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, initData, dataSize);
    src.UploadBuffer->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(src.Buffer.Get(), 0, src.UploadBuffer.Get(), 0, dataSize);
}

struct FTileDataPacked
{
    uint32_t PackedData;
};

struct FRayDataPacked
{
    uint32_t PackedData;
};

bool D3D12HelloTexture::LoadNTCFile()
{    
    bool enableCoopVecInt8 = true;
    bool enableCoopVecFP8 = true;
    const char* ntcFileName = "GlassPlasticMat.ntc";
    
    m_osSupportsCoopVec = true;
    ntc::InferenceWeightType weightType = m_osSupportsCoopVec ? ntc::InferenceWeightType::CoopVecFP8 : ntc::InferenceWeightType::GenericInt8;

    //-  Init context
    ntc::ContextWrapper m_ntcContext;
    ntc::ContextParameters contextParams;
    contextParams.graphicsApi = ntc::GraphicsAPI::D3D12;
    contextParams.d3d12Device = m_device.Get();
    contextParams.graphicsDeviceSupportsDP4a = true;// IsDP4aSupported(m_device);
    contextParams.graphicsDeviceSupportsFloat16 = true;// IsFloat16Supported(m_device);
    contextParams.enableCooperativeVectorInt8 = m_osSupportsCoopVec && enableCoopVecInt8;
    contextParams.enableCooperativeVectorFP8 = m_osSupportsCoopVec && enableCoopVecFP8;

    ntc::Status ntcStatus = ntc::CreateContext(m_ntcContext.ptr(), contextParams);
    if (ntcStatus != ntc::Status::Ok && ntcStatus != ntc::Status::CudaUnavailable)
    {
        log_error("Failed to create an NTC context, code = %s: ", ntc::StatusToString(ntcStatus), ntc::GetLastErrorMessage());
        return false;
    }

    //- Load Material
    ntc::FileStreamWrapper ntcFileStream(m_ntcContext);
    ntc::MemoryStreamWrapper ntcMemoryStream(m_ntcContext);
    ntcStatus = m_ntcContext->OpenFile(ntcFileName, false, ntcFileStream.ptr());

    ntc::IStream* stream = ntcFileStream.Get();

    ntc::TextureSetMetadataWrapper textureSetMetadata(m_ntcContext);
    ntcStatus = m_ntcContext->CreateTextureSetMetadataFromStream(stream, textureSetMetadata.ptr());
    if (ntcStatus != ntc::Status::Ok)
    {
        log_warning("Cannot load metadata for '%s', error code = %s: %s", ntcFileName, ntc::StatusToString(ntcStatus), ntc::GetLastErrorMessage());
        return false;
    }

    int networkVersion = textureSetMetadata->GetNetworkVersion();

    ntc::StreamRange latentStreamRange;
    ntcStatus = textureSetMetadata->GetStreamRangeForLatents(0, textureSetMetadata->GetDesc().mips, latentStreamRange);
    if (ntcStatus != ntc::Status::Ok)
    {
        log_warning("Cannot process material, call to GetStreamRangeForLatents failed, error code = %s: %s", /*ntcMaterial->name.c_str()*/ ntc::StatusToString(ntcStatus), ntc::GetLastErrorMessage());
    }


    //- PrepareMaterialForInferenceOnSample
    ntc::InferenceData inferenceData;
    ntcStatus = m_ntcContext->MakeInferenceData(textureSetMetadata, latentStreamRange, weightType, &inferenceData);
    if (ntcStatus != ntc::Status::Ok)
    {
        log_warning("Failed to make inference data for material, error code = %s: %s",
            /*material.name.c_str(), */ntc::StatusToString(ntcStatus), ntc::GetLastErrorMessage());
        return false;
    }

    void const* weightData = nullptr;
    size_t weightSize = 0;
    size_t convertedWeightSize = 0;
    ntcStatus = textureSetMetadata->GetInferenceWeights(weightType, &weightData, &weightSize, &convertedWeightSize);
    if (ntcStatus != ntc::Status::Ok)
    {
        log_warning("Failed to get inference weights for material, error code = %s: %s",
            /*material.name.c_str(), */ntc::StatusToString(ntcStatus), ntc::GetLastErrorMessage());
        return false;
    }


    std::vector<uint8_t> latentData;
    latentData.resize(latentStreamRange.size);
    ntcFileStream->Seek(latentStreamRange.offset);
    if (!ntcFileStream->Read(latentData.data(), latentData.size()))
    {
        log_warning("Failed to read latents for materia");
        return false;
    }

   
    //- Create Buffer
    D3D12_CPU_DESCRIPTOR_HANDLE handle0                 = m_srvHeap->GetCPUDescriptorHandleForHeapStart();    
    D3D12_CPU_DESCRIPTOR_HANDLE handle_latantBuffer     = CD3DX12_CPU_DESCRIPTOR_HANDLE(handle0, 1, m_cbv_srv_uavDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE handle_weightBuffer     = CD3DX12_CPU_DESCRIPTOR_HANDLE(handle0, 2, m_cbv_srv_uavDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE handle_constantBuffer   = CD3DX12_CPU_DESCRIPTOR_HANDLE(handle0, 3, m_cbv_srv_uavDescriptorSize);   
    

    TileAllocatorBuffer.                SetSRVResourceHandle(4, m_cbv_srv_uavDescriptorSize, m_srvHeap);
	TileDataPackedStructuredBuffer.     SetSRVResourceHandle(5, m_cbv_srv_uavDescriptorSize, m_srvHeap);
    RayAllocatorBuffer.                SetSRVResourceHandle(6, m_cbv_srv_uavDescriptorSize, m_srvHeap);
    RayDataPackedStructuredBuffer.     SetSRVResourceHandle(7, m_cbv_srv_uavDescriptorSize, m_srvHeap);

    TileAllocatorBuffer.                SetUAVResourceHandle(8, m_cbv_srv_uavDescriptorSize, m_srvHeap);
    TileDataPackedStructuredBuffer.     SetUAVResourceHandle(9, m_cbv_srv_uavDescriptorSize, m_srvHeap);
    RayAllocatorBuffer.                SetUAVResourceHandle(10, m_cbv_srv_uavDescriptorSize, m_srvHeap);
    RayDataPackedStructuredBuffer.     SetUAVResourceHandle(11, m_cbv_srv_uavDescriptorSize, m_srvHeap);


    int32_t TileCount[2];
	TileCount[0] = m_viewport.Width / 8; // Assuming 8 is the tile size
    TileCount[1] = m_viewport.Height / 8; // Assuming 8 is the tile size
    
    uint32_t MaxTileCount = TileCount[0] * TileCount[1];
    TileDataPackedStructuredBuffer.CreateBuffer(m_device.Get(), sizeof(FTileDataPacked), MaxTileCount);
    TileAllocatorBuffer.CreateBuffer(m_device.Get(), sizeof(uint32_t), 1);


    UINT uav_clear_valule = 0;

    // Generate rays
    // NOTE: GroupCount for emulated indirect-dispatch of raygen shaders dictates the maximum allocation size if GroupCount > MaxTileCount
    uint32_t RayGenThreadCount = 64;// CVarLumenVisualizeHardwareRayTracingThreadCount.GetValueOnRenderThread();
    uint32_t RayGenGroupCount = 4096;// CVarLumenVisualizeHardwareRayTracingGroupCount.GetValueOnRenderThread();
    uint32_t RayCount = max(MaxTileCount, RayGenGroupCount) * 64;// FMath::Max(MaxTileCount, RayGenGroupCount)* FLumenVisualizeCreateRaysCS::GetThreadGroupSize1D();

    // Create rays within tiles
    RayAllocatorBuffer.CreateBuffer(m_device.Get(), sizeof(uint32_t), 1);
	m_commandList->ClearUnorderedAccessViewUint(RayAllocatorBuffer.UavGpuHandle, RayAllocatorBuffer.UavCpuHandle, RayAllocatorBuffer.Buffer.Get(), &uav_clear_valule, 0, nullptr);
    
    //AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RayAllocatorBuffer, PF_R32_UINT), 0);

    RayDataPackedStructuredBuffer.CreateBuffer(m_device.Get(), sizeof(FRayDataPacked), RayCount);// = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(LumenVisualize::FRayDataPacked), RayCount), TEXT("Lumen.Visualize.RayDataPacked"));
        
    m_LatentBuffer = CreateStructuredOrRawSRVBuffer(m_device.Get(), m_commandList.Get(), latentData.size(), handle_latantBuffer);
    m_WeightBuffer = CreateStructuredOrRawSRVBuffer(m_device.Get(), m_commandList.Get(), convertedWeightSize ? convertedWeightSize : weightSize, handle_weightBuffer);
    m_ConstantBuffer = CreateStructuredOrRawSRVBuffer(m_device.Get(), m_commandList.Get(), sizeof(inferenceData.constants), handle_constantBuffer, true, sizeof(inferenceData.constants));
 
    WriteBuffer(m_device.Get(), m_commandList.Get(), m_LatentBuffer, latentData.data(), latentData.size());
    WriteBuffer(m_device.Get(), m_commandList.Get(), m_ConstantBuffer, &inferenceData.constants, sizeof(inferenceData.constants));

    if (convertedWeightSize != 0)
    {

        D3D12_HEAP_PROPERTIES dhp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC drd = CD3DX12_RESOURCE_DESC::Buffer(65536);

        // 1. create upload weight buffer.
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(65536),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_UploadBuffer)));

        // 2. Fill upload buffer.
        void* mapped = nullptr;
        ThrowIfFailed(m_UploadBuffer->Map(0, nullptr, &mapped));
        memcpy(mapped, weightData, weightSize);
        m_UploadBuffer->Unmap(0, nullptr);

        //commandList->setBufferState(m_weightUploadBuffer, nvrhi::ResourceStates::ShaderResource);
        //commandList->setBufferState(material.ntcWeightsBuffer, nvrhi::ResourceStates::UnorderedAccess);
        //commandList->commitBarriers();

        void* nativeCommandList = m_commandList.Get();
        void* nativeSrcBuffer = m_UploadBuffer.Get();
        void* nativeDstBuffer = m_WeightBuffer.Buffer.Get();

        textureSetMetadata->ConvertInferenceWeights(weightType, nativeCommandList, nativeSrcBuffer, 0, nativeDstBuffer, 0);
    }
    else
    {
        WriteBuffer(m_device.Get(), m_commandList.Get(), m_WeightBuffer, weightData, weightSize);
    }
}

void D3D12HelloTexture::LoadAssets_UE_CS()
{
    // Create Tile
    {
        //- Create RootSignature
        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0]);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature_CS_CreateTile)));

        //- Load Shaders
        ComPtr<ID3DBlob> computeShader;
        D3DReadFileToBlob(L"compiled/FLumenVisualizeCreateTilesCS.dxil", &computeShader);

        //- CreatePSO
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSignature_CS_CreateTile.Get();
		psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;       
        ThrowIfFailed(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState_CS_CreateTile)));
    }

    // Create Ray
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0]);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1]);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature_CS_CreateRay)));

        //- Load Shaders
        ComPtr<ID3DBlob> computeShader;
        D3DReadFileToBlob(L"compiled/FLumenVisualizeCreateRaysCS.dxil", &computeShader);

        //- CreatePSO
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSignature_CS_CreateRay.Get();
        psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState_CS_CreateRay)));
    }
}

void D3D12HelloTexture::_Execute_CS_CreateTile()
{
    m_commandList->SetPipelineState(m_pipelineState_CS_CreateTile.Get());
    m_commandList->SetComputeRootSignature(m_rootSignature_CS_CreateTile.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    
    m_commandList->SetComputeRootDescriptorTable(0, TileAllocatorBuffer.UavGpuHandle);

    int32_t TileCount[2];
    TileCount[0] = m_viewport.Width / 8; // Assuming 8 is the tile size
    TileCount[1] = m_viewport.Height / 8; // Assuming 8 is the tile size

    m_commandList->Dispatch(TileCount[0], TileCount[1], 1);
}

void D3D12HelloTexture::_Execute_CS_CreateRay()
{
    m_commandList->SetPipelineState(m_pipelineState_CS_CreateRay.Get());
    m_commandList->SetComputeRootSignature(m_rootSignature_CS_CreateRay.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    
    m_commandList->SetComputeRootDescriptorTable(0, TileAllocatorBuffer.SrvGpuHandle);
    m_commandList->SetComputeRootDescriptorTable(1, RayAllocatorBuffer.UavGpuHandle);


    int32_t TileCount[2];
    TileCount[0] = m_viewport.Width / 8; // Assuming 8 is the tile size
    TileCount[1] = m_viewport.Height / 8; // Assuming 8 is the tile size

    uint32_t MaxTileCount = TileCount[0] * TileCount[1];
    // Generate rays
    // NOTE: GroupCount for emulated indirect-dispatch of raygen shaders dictates the maximum allocation size if GroupCount > MaxTileCount
    uint32_t RayGenThreadCount = 64;// CVarLumenVisualizeHardwareRayTracingThreadCount.GetValueOnRenderThread();
    uint32_t RayGenGroupCount = 4096;// CVarLumenVisualizeHardwareRayTracingGroupCount.GetValueOnRenderThread();
    uint32_t RayCount = max(MaxTileCount, RayGenGroupCount) * 64;// FMath::Max(MaxTileCount, RayGenGroupCount)* FLumenVisualizeCreateRaysCS::GetThreadGroupSize1D();

    const int32_t VisualizeCreateRaysDispatchSizeX = 128;
    const int32_t GroupY = RayCount / 64 / VisualizeCreateRaysDispatchSizeX;
    m_commandList->Dispatch(VisualizeCreateRaysDispatchSizeX, GroupY, 1);
}
