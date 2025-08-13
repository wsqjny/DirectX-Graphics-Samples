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


#define NETWORK_VERSION 3

#include "ThirdParty/libntc/include/libntc/shaders/InferenceConstants.h"
//#include "ThirdParty/libntc/include/libntc/shaders/Inference.hlsli"


//#define USE_COOPVEC
//#define USE_FP8 1

#include "ThirdParty/libntc/include/libntc/shaders/InferenceCoopVec.hlsli"

typedef NtcNetworkParams<NETWORK_VERSION> NtcParams;

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D g_texture : register(t0);

ByteAddressBuffer t_InputFile : register(t1);
ByteAddressBuffer t_WeightBuffer : register(t2);
StructuredBuffer<NtcTextureSetConstants> t_ConstantBuffer : register(t3);


SamplerState g_sampler : register(s0);


PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
    PSInput result;

    result.position = position;
    result.uv = uv;

    return result;
}

float3 SampleNTC(NtcTextureSetConstants g_NtcMaterial, ByteAddressBuffer t_InputFile, ByteAddressBuffer t_WeightBuffer, float2 uv)
{
    int mipLevel = 5;

    const int2 textureSize = NtcGetTextureDimensions(g_NtcMaterial, mipLevel);
    int2 texel = int2(floor(uv* textureSize));
    

    const bool linearizeColorsOnSample = false;

    // Decompress the texel and get all the channels.
    float channels[NtcParams::OUTPUT_CHANNELS];
#ifdef USE_COOPVEC
#if USE_FP8
    NtcSampleTextureSet_CoopVec_FP8<NETWORK_VERSION>(g_NtcMaterial, t_InputFile, 0,
        t_WeightBuffer, 0, texel, mipLevel, linearizeColorsOnSample, channels);
#else
    NtcSampleTextureSet_CoopVec_Int8<NETWORK_VERSION>(g_NtcMaterial, t_InputFile, 0,
        t_WeightBuffer, 0, texel, mipLevel, linearizeColorsOnSample, channels);
#endif
#else
    NtcSampleTextureSet<NETWORK_VERSION>(g_NtcMaterial, t_InputFile, 0,
        t_WeightBuffer, 0, texel, mipLevel, linearizeColorsOnSample, channels);
#endif

    float3 baseOrDiffuse = 1;
    baseOrDiffuse = float3(channels[0], channels[1], channels[2]);

    return baseOrDiffuse;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    //vector<int, 16> intArray;

    //return g_texture.Sample(g_sampler, input.uv);

    float3 sample_result = SampleNTC(t_ConstantBuffer[0], t_InputFile, t_WeightBuffer, input.uv);
    return float4(sample_result, 1);
}
