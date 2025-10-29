/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NTC_INFERENCE_HLSLI
#define NTC_INFERENCE_HLSLI

#include "InferenceConstants.h"
#include "ColorSpaces.hlsli"

// Define this macro before including the header to set the DP4a support flag for compatibility with older GPUs
#ifndef NTC_USE_DP4A
    #define NTC_USE_DP4A 1
#endif

// Define this macro before including the header to set the FP16 support flag for compatibility with older GPUs
#ifndef NTC_USE_FLOAT16
    #define NTC_USE_FLOAT16 1
#endif

// Helper macros used to declare templated functions with different t-parameter counts in Slang and HLSL.
#if __SLANG__
#define NTC_TEMPLATE_FN_1(ReturnType, FnName, ArgType1, ArgName1) \
    ReturnType FnName <let ArgName1: ArgType1>
#define NTC_TEMPLATE_FN_2(ReturnType, FnName, ArgType1, ArgName1, ArgType2, ArgName2) \
    ReturnType FnName <let ArgName1: ArgType1, let ArgName2: ArgType2>
#define NTC_TEMPLATE_FN_3(ReturnType, FnName, ArgType1, ArgName1, ArgType2, ArgName2, ArgType3, ArgName3) \
    ReturnType FnName <let ArgName1: ArgType1, let ArgName2: ArgType2, let ArgName3: ArgType3>
#else
#define NTC_TEMPLATE_FN_1(ReturnType, FnName, ArgType1, ArgName1) \
    template<ArgType1 ArgName1> ReturnType FnName
#define NTC_TEMPLATE_FN_2(ReturnType, FnName, ArgType1, ArgName1, ArgType2, ArgName2) \
    template<ArgType1 ArgName1, ArgType2 ArgName2> ReturnType FnName
#define NTC_TEMPLATE_FN_3(ReturnType, FnName, ArgType1, ArgName1, ArgType2, ArgName2, ArgType3, ArgName3) \
    template<ArgType1 ArgName1, ArgType2 ArgName2, ArgType3 ArgName3> ReturnType FnName
#endif

// The NtcNetworkParams structure is used to derive the MLP geometry from network version
#if __SLANG__
struct NtcNetworkParams<let _NETWORK_VERSION: int>
#else
template<int _NETWORK_VERSION> struct NtcNetworkParams
#endif
{
    static const int INPUT_CHANNELS = 
        (_NETWORK_VERSION == NTC_NETWORK_SMALL) ? NTC_MLP_INPUT_CHANNELS_SMALL :
        (_NETWORK_VERSION == NTC_NETWORK_MEDIUM) ? NTC_MLP_INPUT_CHANNELS_MEDIUM :
        (_NETWORK_VERSION == NTC_NETWORK_LARGE) ? NTC_MLP_INPUT_CHANNELS_LARGE :
        (_NETWORK_VERSION == NTC_NETWORK_XLARGE) ? NTC_MLP_INPUT_CHANNELS_XLARGE :
        0; // Unsupported value

    static const int FEATURES = 
        (_NETWORK_VERSION == NTC_NETWORK_SMALL) ? NTC_MLP_FEATURES_SMALL :
        (_NETWORK_VERSION == NTC_NETWORK_MEDIUM) ? NTC_MLP_FEATURES_MEDIUM :
        (_NETWORK_VERSION == NTC_NETWORK_LARGE) ? NTC_MLP_FEATURES_LARGE :
        (_NETWORK_VERSION == NTC_NETWORK_XLARGE) ? NTC_MLP_FEATURES_XLARGE :
        0; // Unsupported value

    static const int HIDDEN_LAYER_CHANNELS = NTC_MLP_HIDDEN_CHANNELS;

    static const int OUTPUT_CHANNELS = NTC_MLP_OUTPUT_CHANNELS;
};

// The pack_clamp_s8 intrinsic should map well to an I2IP instruction on NV GPUs, but using it causes major slowdowns on Intel.
#define USE_PACKING_INTRINSICS 0

float16_t2 NtcUintToHalf2(uint u)
{
    return asfloat16(uint16_t2(uint16_t(u), uint16_t(u >> 16)));
}

uint NtcHalf2ToUint(float16_t2 h)
{
    uint16_t2 u = asuint16(h);
    return uint(u.x) | (uint(u.y) << 16);
}

uint NtcFloatToInt8(float h, float scale)
{
    return uint(int(clamp(h * scale, -128.f, 127.f)) & 0xff);
}

uint NtcPackFloat4(float4 h, float scale)
{
    return NtcFloatToInt8(h.x, scale)
        | (NtcFloatToInt8(h.y, scale) << 8)
        | (NtcFloatToInt8(h.z, scale) << 16)
        | (NtcFloatToInt8(h.w, scale) << 24);
}

uint NtcPackInt8x4(int4 vec)
{    
    #if USE_PACKING_INTRINSICS
    {
        return pack_s8(vec);
    }
    #else
    {
        return uint(vec.x & 0xff) 
            | (uint(vec.y & 0xff) << 8) 
            | (uint(vec.z & 0xff) << 16) 
            | (uint(vec.w) << 24);
    }
    #endif
}

int4 NtcUnpackInt8x4(uint packed)
{
    #if USE_PACKING_INTRINSICS
    {
        return unpack_s8s32(packed);
    }
    #else
    {
        int4 result;
        result.x = (int(packed) << 24) >> 24;
        result.y = (int(packed) << 16) >> 24;
        result.z = (int(packed) << 8) >> 24;
        result.w = int(packed) >> 24;
        return result;
    }
    #endif
}

// Software emulation of the dot4add_i8packed intrinsic
int NtcDotProductInt8x4(uint32_t a, uint32_t b)
{
    int ia = a;
    int ib = b;

    return (ia >> 24) * (ib >> 24)
        + ((ia << 8) >> 24) * ((ib << 8) >> 24)
        + ((ia << 16) >> 24) * ((ib << 16) >> 24)
        + ((ia << 24) >> 24) * ((ib << 24) >> 24);
}

// Converts the int4 packed version of ColorMipConstants into a struct
NtcColorMipConstants NtcUnpackColorMipConstants(int4 i)
{
    NtcColorMipConstants result;
    result.neuralMip = i.x;
    result.positionLod = asfloat(i.y);
    result.positionScale = asfloat(i.z);
    result.pad = i.w;
    return result;
}

// TODO[BC1L]: Verify that this function is actually inlined and there is no dynamic array indexing in the shader
NTC_TEMPLATE_FN_1(void, NtcInsertUintAtByteOffset, int, ARRAY_SIZE)
    (inout uint array[ARRAY_SIZE],
    uint value,
    uint byteOffset)
{
    const int arrayIndex = byteOffset >> 2;
    switch(byteOffset & 3)
    {
        case 0:
            array[arrayIndex] = value;
            break;
        case 1:
            array[arrayIndex] |= value << 8;
            break;
        case 2:
            array[arrayIndex] |= value << 16;
            array[arrayIndex + 1] = value >> 16;
            break;
        case 3:
            array[arrayIndex] |= value << 24;
            array[arrayIndex + 1] = value >> 8;
            break;
    }
    
}

static const float c_InputScale = 127.5f; // Inputs are in the [-1, 1] range, scale matches tin::InputQuant

NTC_TEMPLATE_FN_2(bool, NtcSampleLatentGrid, int, NUM_FEATURES, int, OUTPUT_SIZE)
    (Texture2DArray latentTexture,
    SamplerState latentSampler,
    float2 uv,
    int neuralLod,
    int featureOffset,
    inout uint outputArray[OUTPUT_SIZE])
{
    int width, height, arraySize;
    latentTexture.GetDimensions(width, height, arraySize);

    width = max(width >> neuralLod, 1);
    height = max(height >> neuralLod, 1);
    const float2 invSize = float2(1.0f / width, 1.0f / height);

#if __SLANG__
    [ForceUnroll]
#else
    [unroll]
#endif
    for (int layerIndex = 0; layerIndex < NUM_FEATURES / 3; ++layerIndex)
    {
        if (layerIndex >= arraySize)
            break;
        
        float3 sampledValue = latentTexture.SampleLevel(latentSampler, float3(uv, layerIndex), neuralLod).xyz;
        sampledValue = sampledValue * (2.f * c_InputScale) - c_InputScale;

        const uint packedValues = NtcPackFloat4(float4(sampledValue.xyz, 0), 1);

        NtcInsertUintAtByteOffset(outputArray, packedValues, featureOffset + layerIndex * 3);

        // Offset the sampling UV by one pixel on each array layer.
        // This should be possible with integer sampling offsets, but DXC fails to generate valid SPIR-V for that.
        uv += invSize;
    }

    return true;
}

float4 NtcEvaluatePositionalEncoding(float2 posf, float iscale)
{
    float4 result;

    result.x = frac(posf.x * iscale) * 2 - 1;
    result.y = frac(posf.y * iscale) * 2 - 1;
    result.z = frac(posf.x * iscale + 0.25f) * 2 - 1;
    result.w = frac(posf.y * iscale + 0.25f) * 2 - 1;

    return result;
}

NTC_TEMPLATE_FN_1(void, NtcEncodeSamplePosition, int, OUTPUT_SIZE)
    (float2 posf, float lod, int featureOffset, inout uint outputArray[OUTPUT_SIZE])
{
    int idx = featureOffset;
    int scale = NTC_MLP_POS_ENC_SCALE;
    float iscale = 1.f / scale;
    
    [unroll]
    for (; scale > 1; scale >>= 1)
    {
        float4 enc = NtcEvaluatePositionalEncoding(posf, iscale);
        uint packedPositionalEncoding = NtcPackFloat4(enc, c_InputScale);
        NtcInsertUintAtByteOffset(outputArray, packedPositionalEncoding, idx);
        idx += 4;
        iscale *= 2;
    }

    uint packedLod = NtcPackFloat4(float4(lod.xx, 0, 0), c_InputScale);
    NtcInsertUintAtByteOffset(outputArray, packedLod, idx);
}

struct NtcHGELUParams
{
    float maxval;
    float invStep;
    float bias;
};

NtcHGELUParams NtcGetHGELUParams()
{
    const float minval = -3.0 / 16.0;
    const float maxval = 3.0;

    const int bins = 256;
    const float step = (maxval - minval) / float(bins - 1);
    const float invStep = 1.0 / step;
    const int qmax = int(maxval / step);
    const int qmin = qmax - bins + 1;
    const int bias = -(bins / 2) - qmin;

    NtcHGELUParams params;
    params.maxval = maxval;
    params.invStep = invStep;
    params.bias = bias;
    return params;
}

// HGELU activation function with clamping, forward evaluation
float16_t4 NtcHGELUClamp_ForwardHalf(float16_t4 x)
{
    const NtcHGELUParams params = NtcGetHGELUParams();
    return min(x, float16_t(params.maxval)) * clamp(float16_t(1/3.f) * x + 0.5h, 0.h, 1.h);
}

float4 NtcHGELUClamp_ForwardFloat(float4 x)
{
    const NtcHGELUParams params = NtcGetHGELUParams();
    return min(x, params.maxval) * clamp((1/3.f) * x + 0.5f, 0.f, 1.f);
}

int4 NtcHGELUClamp_QuantizeHalf(float16_t4 x)
{
    const NtcHGELUParams params = NtcGetHGELUParams();
    return int4(round(x * float16_t(params.invStep) + float16_t(params.bias)));
}

int4 NtcHGELUClamp_QuantizeFloat(float4 x)
{
    const NtcHGELUParams params = NtcGetHGELUParams();
    return int4(round(x * params.invStep + params.bias));
}

NTC_TEMPLATE_FN_3(void, NtcEvaluateLayerINT8, int, IN, int, OUT, bool, OUTPUT_LAYER)
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    inout int scaleBiasOffset,
    int totalChannels,
    bool activation,
    uint inputArray[IN / 4],
#if NTC_USE_FLOAT16
    out uint outputArray[OUTPUT_LAYER ? OUT / 2 : OUT / 4]
#else
    out uint outputArray[OUTPUT_LAYER ? OUT : OUT / 4]
#endif
)
{
    // See the comment block in the beginning of TextureSet.cpp for the weight layouts

    // Note: not unrolling the outer loop.
    // If we do, DXC/SPIR-V crashes.
    // DXC/DXIL compiles the unrolled loop successfully, but then creating a pipeline with it takes seconds,
    // and the resulting code works slower than a regular loop.
    for (uint c = 0; c < OUT; c += 4)
    {
        int4 biases = weightBuffer.Load<int4>(scaleBiasOffset + (totalChannels + c) * 4);
        int acc0 = biases.x;
        int acc1 = biases.y;
        int acc2 = biases.z;
        int acc3 = biases.w;
        
        [unroll]
        for (uint k = 0; k < IN / 4; k++)
        {
            const uint weights0 = weightBuffer.Load(weightOffset + (c + 0) * IN + k * 4);
            const uint weights1 = weightBuffer.Load(weightOffset + (c + 1) * IN + k * 4);
            const uint weights2 = weightBuffer.Load(weightOffset + (c + 2) * IN + k * 4);
            const uint weights3 = weightBuffer.Load(weightOffset + (c + 3) * IN + k * 4);
            
#if NTC_USE_DP4A
            acc0 = dot4add_i8packed(inputArray[k], weights0, acc0);
            acc1 = dot4add_i8packed(inputArray[k], weights1, acc1);
            acc2 = dot4add_i8packed(inputArray[k], weights2, acc2);
            acc3 = dot4add_i8packed(inputArray[k], weights3, acc3);
#else
            acc0 += DotProductInt8x4(inputArray[k], weights0);
            acc1 += DotProductInt8x4(inputArray[k], weights1);
            acc2 += DotProductInt8x4(inputArray[k], weights2);
            acc3 += DotProductInt8x4(inputArray[k], weights3);
#endif
        }
        
        float4 results = float4(acc0, acc1, acc2, acc3);
        float4 scales = weightBuffer.Load<float4>(scaleBiasOffset + c * 4);

#if NTC_USE_FLOAT16
        float16_t4 hresults = float16_t4(results * scales);
        
        if (activation)
        {
            hresults = NtcHGELUClamp_ForwardHalf(hresults);
        }

        if (OUTPUT_LAYER)
        {
            outputArray[c / 2 + 0] = NtcHalf2ToUint(hresults.xy);
            outputArray[c / 2 + 1] = NtcHalf2ToUint(hresults.zw);
        }
        else
        {
            int4 iresults = NtcHGELUClamp_QuantizeHalf(hresults);

            outputArray[c / 4] = NtcPackInt8x4(iresults);
        }
#else
        float4 hresults = results * scales;
        
        if (activation)
        {
            hresults = NtcHGELUClamp_ForwardFloat(hresults);
        }

        if (OUTPUT_LAYER)
        {
            outputArray[c + 0] = asuint(hresults.x);
            outputArray[c + 1] = asuint(hresults.y);
            outputArray[c + 2] = asuint(hresults.z);
            outputArray[c + 3] = asuint(hresults.w);
        }
        else
        {
            int4 iresults = NtcHGELUClamp_QuantizeFloat(hresults);

            outputArray[c / 4] = PackInt8x4(iresults);
        }
#endif
    }

    // Advance the input offsets to point at the next layer.
    scaleBiasOffset += OUT * sizeof(float);
}

int2 NtcGetTextureDimensions(NtcTextureSetConstants desc, int mipLevel)
{
    return max(int2(desc.imageWidth, desc.imageHeight) >> mipLevel, 1);
}

int NtcGetTextureMipLevels(NtcTextureSetConstants desc)
{
    return desc.imageMips;
}

uint NtcGetChannelMask(int firstChannel, int numChannels = 1)
{
    return ((1u << numChannels) - 1u) << firstChannel;
}

// Returns the bit mask of channels in the texture set that have some texture data.
// If a channel's bit in this mask is 0, then its contents are undefined.
// Use GetChannelMask(first, num) to get the expected mask for a given set of channels.
uint NtcGetValidChannelMask(NtcTextureSetConstants desc)
{
    return desc.validChannelMask;
}

bool NtcTextureSetHasChannels(NtcTextureSetConstants desc, int firstChannel, int numChannels = 1)
{
    uint mask = NtcGetChannelMask(firstChannel, numChannels);
    return (NtcGetValidChannelMask(desc) & mask) == mask;
}

NTC_TEMPLATE_FN_1(bool, NtcPrepareNetworkInputsInternal, int, VERSION)
    (Texture2DArray latentTexture,
    SamplerState latentSampler,
    int2 texel,
    float2 uv,
    const NtcColorMipConstants colorMip,
    out uint networkInputs[NtcNetworkParams<VERSION>::INPUT_CHANNELS / 4])
{
    typedef NtcNetworkParams<VERSION> Params;

    // Zero init the array
    [unroll]
    for (int i = 0; i < Params::INPUT_CHANNELS / 4; ++i)
        networkInputs[i] = 0;

    if (colorMip.neuralMip < 0)
        return false;

    // Sample the latent grids
    if (!NtcSampleLatentGrid<Params::FEATURES, Params::INPUT_CHANNELS / 4>(latentTexture, latentSampler,
        uv, colorMip.neuralMip, 0, networkInputs))
        return false;

    if (!NtcSampleLatentGrid<Params::FEATURES, Params::INPUT_CHANNELS / 4>(latentTexture, latentSampler,
        uv, colorMip.neuralMip + 1, Params::FEATURES, networkInputs))
        return false;

    // Encode the sample position
    NtcEncodeSamplePosition<Params::INPUT_CHANNELS / 4>(float2(texel) * colorMip.positionScale,
        colorMip.positionLod, Params::FEATURES * 2, networkInputs);

    return true;
}

NTC_TEMPLATE_FN_1(bool, NtcPrepareNetworkInputs, int, VERSION)
    (NtcTextureSetConstants desc,
    Texture2DArray latentTexture,
    SamplerState latentSampler,
    int2 texel,
    int mipLevel,
    out uint networkInputs[NtcNetworkParams<VERSION>::INPUT_CHANNELS / 4])
{
    typedef NtcNetworkParams<VERSION> Params;

    const int2 imageSize = NtcGetTextureDimensions(desc, mipLevel);
    const float2 uv = (float2(texel) + 0.5) / imageSize;

    // Zero init the array - in some cases, OUTPUT_SIZE is rounded up from the actual used size.
    [unroll]
    for (int i = 0; i < Params::INPUT_CHANNELS / 4; ++i)
        networkInputs[i] = 0;

    const NtcColorMipConstants colorMip = NtcUnpackColorMipConstants(desc.colorMips[mipLevel]);

    return NtcPrepareNetworkInputsInternal<VERSION>(latentTexture, latentSampler, texel, uv, colorMip, networkInputs);
}

float NtcConvertChannelToLinearColorSpace(NtcTextureSetConstants desc, int channel, float storedValue)
{
    int colorSpace = (desc.channelColorSpaces >> (channel * 2)) & 3;
    
    switch (colorSpace)
    {
        case NtcColorSpace_sRGB:
            return NtcSrgbColorSpace::Decode(storedValue);
        case NtcColorSpace_HLG:
            return NtcHybridLogGammaColorSpace::Decode(storedValue);
        default:
            return storedValue;
    }
}

// NtcSampleTextureSet - this is the main NTC function for applications.
// Use like NtcSampleTextureSet<NETWORK_VERSION>(Constants, LatentsBuffer, ...)
// Returns true if the mip level is valid; out-of-bounds texel positions are clamped.
NTC_TEMPLATE_FN_1(bool, NtcSampleTextureSet, int, VERSION)
    (NtcTextureSetConstants desc,
    Texture2DArray latentTexture,
    SamplerState latentSampler,
    ByteAddressBuffer weightsBuffer,
    uint weightsOffset, // Offset of the weight chunk in weightsBuffer if packing multiple textures together
    int2 texel,
    int mipLevel,
    bool convertToLinearColorSpace,
    inout float outputs[NtcNetworkParams<VERSION>::OUTPUT_CHANNELS])
{
    typedef NtcNetworkParams<VERSION> Params;

    uint networkInputs[Params::INPUT_CHANNELS / 4];
    if (!NtcPrepareNetworkInputs<VERSION>(desc, latentTexture, latentSampler, texel, mipLevel, networkInputs))
        return false;

    int scaleBiasOffset = weightsOffset + desc.networkScaleBiasOffset;

    // Evaluate the MLP layers:
    const int totalChannels = Params::HIDDEN_LAYER_CHANNELS * 3 + Params::OUTPUT_CHANNELS;

    // Input layer
    uint hiddenOutput1[Params::HIDDEN_LAYER_CHANNELS / 4];
    NtcEvaluateLayerINT8<Params::INPUT_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, false>
        (weightsBuffer, weightsOffset + desc.networkWeightOffsets.x, scaleBiasOffset,
        totalChannels, true, networkInputs, hiddenOutput1);

    // Hidden layer 1
    uint hiddenOutput2[Params::HIDDEN_LAYER_CHANNELS / 4];
    NtcEvaluateLayerINT8<Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, false>
        (weightsBuffer, weightsOffset + desc.networkWeightOffsets.y, scaleBiasOffset,
        totalChannels, true, hiddenOutput1, hiddenOutput2);

    // Hidden layer 2
    NtcEvaluateLayerINT8<Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, false>
        (weightsBuffer, weightsOffset + desc.networkWeightOffsets.z, scaleBiasOffset,
        totalChannels, true, hiddenOutput2, hiddenOutput1);

    // Output layer
#if NTC_USE_FLOAT16
    uint networkOutputs[Params::OUTPUT_CHANNELS / 2];
#else
    uint networkOutputs[Params::OUTPUT_CHANNELS];
#endif
    NtcEvaluateLayerINT8<Params::HIDDEN_LAYER_CHANNELS, Params::OUTPUT_CHANNELS, true>
        (weightsBuffer, weightsOffset + desc.networkWeightOffsets.w, scaleBiasOffset,
        totalChannels, false, hiddenOutput1, networkOutputs);

#if NTC_USE_FLOAT16
    [unroll]
    for (int ch = 0; ch < Params::OUTPUT_CHANNELS/2; ++ch)
    {
        uint twoCh = networkOutputs[ch];
        int ch0 = ch * 2 + 0;
        int ch1 = ch * 2 + 1;
        outputs[ch0] = asfloat16(uint16_t(twoCh));
        outputs[ch1] = asfloat16(uint16_t(twoCh >> 16));

        if (convertToLinearColorSpace)
        {
            outputs[ch0] = NtcConvertChannelToLinearColorSpace(desc, ch0, outputs[ch0]);
            outputs[ch1] = NtcConvertChannelToLinearColorSpace(desc, ch1, outputs[ch1]);
        }
    }
#else
    [unroll]
    for (int ch = 0; ch < Params::OUTPUT_CHANNELS; ++ch)
    {
        outputs[ch] = asfloat(networkOutputs[ch]);

        if (convertToLinearColorSpace)
        {
            outputs[ch] = NtcConvertChannelToLinearColorSpace(desc, ch, outputs[ch]);
        }
    }
#endif
    
    return true;
}

#endif