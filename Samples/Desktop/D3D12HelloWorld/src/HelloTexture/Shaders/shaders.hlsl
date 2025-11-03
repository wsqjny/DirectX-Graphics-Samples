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


#include "../ThirdParty/libntc/include/libntc/shaders/InferenceConstants.h"
#include "../ThirdParty/libntc/include/libntc/shaders/Inference.hlsli"


//#define USE_COOPVEC
//#define USE_FP8 1

//#include "ThirdParty/libntc/include/libntc/shaders/InferenceCoopVec.hlsli"


struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D g_texture : register(t0);

Texture2DArray t_Latents : register(t1);
ByteAddressBuffer t_WeightBuffer : register(t2);
StructuredBuffer<NtcTextureSetConstants> t_ConstantBuffer : register(t3);

Texture2D<float4> SourceTexture_BaseColor: register(t4);
Texture2D<float4> SourceTexture_Normal: register(t5);
Texture2D<float4> SourceTexture_ARM: register(t6);

SamplerState g_sampler : register(s0);


PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
    PSInput result;

    result.position = position;
    result.uv = uv.xy;

    return result;
}

float3 SampleNTC(NtcTextureSetConstants g_NtcMaterial, Texture2DArray t_Latents, ByteAddressBuffer t_WeightBuffer, SamplerState s_LatentSampler, float2 uv)
{
    int mipLevel = 0;

    const int2 textureSize = NtcGetTextureDimensions(g_NtcMaterial, mipLevel);
    int2 texel = int2(floor(uv* textureSize));
    

    const bool linearizeColorsOnSample = false;

    // Decompress the texel and get all the channels.
    float channels[NTC_MLP_OUTPUT_CHANNELS];
    NtcSampleTextureSet(g_NtcMaterial, t_Latents, s_LatentSampler,
        t_WeightBuffer, 0, texel, mipLevel, linearizeColorsOnSample, channels);

    float3 baseOrDiffuse = 1;
    baseOrDiffuse = float3(channels[0], channels[1], channels[2]);

    return baseOrDiffuse;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    //vector<int, 16> intArray;

    //return g_texture.Sample(g_sampler, input.uv);

    
    //{
   //     float4 color = SourceTexture_BaseColor.Sample(g_sampler, input.uv);
   //     color.rgb = pow(color.rgb, 0.4545);

  //      return color;    

  //  }

    {
        float3 sample_result = SampleNTC(t_ConstantBuffer[0], t_Latents, t_WeightBuffer, g_sampler, input.uv);
        return float4(sample_result, 1);
    }
    
}
