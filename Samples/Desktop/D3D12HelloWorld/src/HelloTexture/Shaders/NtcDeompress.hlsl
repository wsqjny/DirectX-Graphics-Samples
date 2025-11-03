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

#include "../ThirdParty/libntc/include/libntc/shaders/DecompressConstants.h"

#define DX12_DEMO 1

Texture2DArray t_Latents : register(t0);
ByteAddressBuffer t_WeightBuffer : register(t1);
StructuredBuffer<NtcDecompressConstants> t_Constants : register(t2);

SamplerState s_LatentSampler : register(s0);

RWTexture2D<float4> u_OutputBuffers_0 : register(u0); 
RWTexture2D<float4> u_OutputBuffers_1 : register(u1);
RWTexture2D<float4> u_OutputBuffers_2 : register(u2);


#include "../ThirdParty/libntc/src/shaders/DecompressINT8.hlsl"

[numthreads(DECOMPRESS_CS_BLOCK_WIDTH, DECOMPRESS_CS_BLOCK_HEIGHT, 1)]
void UEMain(uint2 globalIndex : SV_DispatchThreadID, uint2 threadIndex : SV_GroupThreadID)
{
	NtcDecompressConstants kConst = t_Constants[0];

	//DecompressPixel(globalThreadIdx.xy + uint2(kConst.gridLeft, kConst.gridTop), groupThreadIdx.xy, kConst);

	DecompressPixel(globalIndex, threadIndex, kConst);
	//DecompressPixel(globalThreadIdx.xy);
}