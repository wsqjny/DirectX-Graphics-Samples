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

#ifndef NTC_INFERENCE_COOPVEC_SLANGH
#define NTC_INFERENCE_COOPVEC_SLANGH

#if !__SLANG__
#define CoopVec vector
#endif

#include "Inference.hlsli"

#if __SLANG__
void NtcCoopVecStoreHalf4<let SIZE: int>(inout CoopVec<float16_t, SIZE> vec, int offset, float16_t4 values)
#else
template<int SIZE>
void NtcCoopVecStoreHalf4(inout CoopVec<float16_t, SIZE> vec, int offset, float16_t4 values)
#endif
{
    vec[offset + 0] = values.x;
    vec[offset + 1] = values.y;
    vec[offset + 2] = values.z;
    vec[offset + 3] = values.w;
}

NTC_TEMPLATE_FN_2(bool, NtcSampleLatentGrid_FP16, int, NUM_FEATURES, int, OUTPUT_SIZE)
    (Texture2DArray latentTexture,
    SamplerState latentSampler,
    float2 uv,
    int neuralLod,
    int featureOffset,
    inout CoopVec<float16_t, OUTPUT_SIZE> outputArray)
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
        sampledValue = sampledValue * 2.f - 1.f;
        
        outputArray[featureOffset + layerIndex * 3 + 0] = float16_t(sampledValue.x);
        outputArray[featureOffset + layerIndex * 3 + 1] = float16_t(sampledValue.y);
        outputArray[featureOffset + layerIndex * 3 + 2] = float16_t(sampledValue.z);

        // Offset the sampling UV by one pixel on each array layer.
        // This should be possible with integer sampling offsets, but DXC fails to generate valid SPIR-V for that.
        uv += invSize;
    }

    return true;
}

NTC_TEMPLATE_FN_1(void, NtcEncodeSamplePosition_FP16, int, OUTPUT_SIZE)
    (float2 posf, float lod, int offset, inout CoopVec<float16_t, OUTPUT_SIZE> outputArray)
{
    int idx = offset;
    int scale = NTC_MLP_POS_ENC_SCALE;
    float iscale = 1.f / scale;
    
    [unroll]
    for (; scale > 1; scale >>= 1)
    {
        float4 enc = NtcEvaluatePositionalEncoding(posf, iscale);

        outputArray[idx + 0] = float16_t(enc.x);
        outputArray[idx + 1] = float16_t(enc.y);
        outputArray[idx + 2] = float16_t(enc.z);
        outputArray[idx + 3] = float16_t(enc.w);

        idx += 4;
        iscale *= 2;
    }
    
    outputArray[idx+0] = float16_t(lod);
    outputArray[idx+1] = float16_t(lod);
}

NTC_TEMPLATE_FN_1(bool, NtcPrepareNetworkInputsInternal_FP16, int, VERSION)
    (Texture2DArray latentTexture,
    SamplerState latentSampler,
    int2 texel,
    float2 uv,
    const NtcColorMipConstants colorMip,
    out CoopVec<float16_t, NtcNetworkParams<VERSION>::INPUT_CHANNELS> networkInputs)
{
    typedef NtcNetworkParams<VERSION> Params;

    // Zero init the vector
    [unroll]
    for (int i = 0; i < Params::INPUT_CHANNELS; ++i)
        networkInputs[i] = 0;

    if (colorMip.neuralMip < 0)
        return false;

    // Sample the latent grids
    if (!NtcSampleLatentGrid_FP16<Params::FEATURES, Params::INPUT_CHANNELS>(latentTexture, latentSampler,
        uv, colorMip.neuralMip, 0, networkInputs))
        return false;

    if (!NtcSampleLatentGrid_FP16<Params::FEATURES, Params::INPUT_CHANNELS>(latentTexture, latentSampler,
        uv, colorMip.neuralMip + 1, Params::FEATURES, networkInputs))
        return false;

    // Encode the sample position
    NtcEncodeSamplePosition_FP16<Params::INPUT_CHANNELS>(float2(texel) * colorMip.positionScale,
        colorMip.positionLod, Params::FEATURES * 2, networkInputs);

    return true;
}

NTC_TEMPLATE_FN_1(bool, NtcPrepareNetworkInputs_FP16, int, VERSION)
    (NtcTextureSetConstants desc,
    Texture2DArray latentTexture,
    SamplerState latentSampler,
    int2 texel,
    int mipLevel,
    inout CoopVec<float16_t, NtcNetworkParams<VERSION>::INPUT_CHANNELS> networkInputs)
{
    typedef NtcNetworkParams<VERSION> Params;

    const int2 imageSize = NtcGetTextureDimensions(desc, mipLevel);
    const float2 uv = (float2(texel) + 0.5) / imageSize;

    const NtcColorMipConstants colorMip = NtcUnpackColorMipConstants(desc.colorMips[mipLevel]);

    return NtcPrepareNetworkInputsInternal_FP16<VERSION>(latentTexture, latentSampler,
        texel, uv, colorMip, networkInputs);
}


#if __SLANG__
void NtcHGELUClamp_Forward_CoopVec<T: __BuiltinFloatingPointType, let SIZE: int>(inout CoopVec<T, SIZE> x, bool scaleAndBias)
#else
template<typename T, int SIZE>
void NtcHGELUClamp_Forward_CoopVec(inout CoopVec<T, SIZE> x, bool scaleAndBias)
#endif
{
    const NtcHGELUParams params = NtcGetHGELUParams();

#if __SLANG__
    let v3  = CoopVec<T, SIZE>(T(params.maxval));
    let v0  = CoopVec<T, SIZE>(T(0.f));
    let v1  = CoopVec<T, SIZE>(T(1.f));
    let vi3 = CoopVec<T, SIZE>(T(1/3.f));
    let v05 = CoopVec<T, SIZE>(T(0.5f));

    x = min(x, v3) * clamp(vi3 * x + v05, v0, v1);

    if (scaleAndBias)
    {
        let istep = CoopVec<T, SIZE>(T(params.invStep));
        let obias = CoopVec<T, SIZE>(T(params.bias));
        x = x * istep + obias;
    }
#else
    CoopVec<T, SIZE> tmp, v3;
    tmp = x * T(1.0 / 3.0) + T(0.5);
    tmp = clamp(tmp, T(0.0), T(1.0));
    x = tmp * min(x, T(params.maxval));

    if (scaleAndBias)
    {
        x = x * T(params.invStep) + T(params.bias);
    }
#endif
}

#if __SLANG__
    inline void NtcEvaluateLayerMatMulAdd_CoopVec_Int8
        <T_IN: __BuiltinArithmeticType, let T_IN_NUM: int, let IN_IS_PACKED: bool, let IN: int, let OUT: int>
#else
    template<typename T_IN, int T_IN_NUM, bool IN_IS_PACKED, int IN, int OUT>
    void NtcEvaluateLayerMatMulAdd_CoopVec_Int8
#endif
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    int biasOffset,
    in CoopVec<T_IN, T_IN_NUM> inputArray,
    out CoopVec<float, OUT> outputArray)
{
#if __SLANG__

    const CoopVecComponentType inputType = IN_IS_PACKED
        ? CoopVecComponentType::SignedInt8Packed
        : CoopVecComponentType::SignedInt8;
    const CoopVecComponentType weightType = CoopVecComponentType::SignedInt8;
    const CoopVecMatrixLayout weightLayout = CoopVecMatrixLayout::InferencingOptimal;
    const CoopVecComponentType biasType = CoopVecComponentType::SignedInt32;
    const uint stride = 0;

    CoopVec<int, OUT> accum;
    accum = coopVecMatMulAddPacked<int, OUT, T_IN_NUM, T_IN>(
        inputArray,
        inputType,
        IN,
        weightBuffer,
        weightOffset,
        weightType,
        weightBuffer,
        biasOffset,
        biasType,
        weightLayout,
        false,
        stride
    );

    outputArray = CoopVec<float, OUT>(accum);

#else

    // See https://microsoft.github.io/hlsl-specs/proposals/0031-hlsl-vector-matrix-operations.html
    // for the enum value definitions
    
    const uint inputType = IN_IS_PACKED ? 17 /* SINT8_T4_PACKED */ : 20 /* SINT8 */;
    const uint weightType = 20; // SINT8
    const uint weightLayout = 2; // MUL_OPTIMAL
    const uint biasType = 4; // SINT32
    const uint stride = 0;
    
    CoopVec<int, OUT> accum;
    __builtin_MatVecMulAdd(
        accum,
        false,
        inputArray,
        IN_IS_PACKED,
        inputType,
        weightBuffer,
        weightOffset,
        weightType,
        OUT,
        IN,
        weightLayout,
        false,
        stride,
        weightBuffer,
        biasOffset,
        biasType
    );
    
    outputArray = accum;
#endif
}

NTC_TEMPLATE_FN_2(void, NtcEvaluateLayerMatMul_CoopVec_FP8, int, IN, int, OUT)
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    in CoopVec<float16_t, IN> inputArray,
    out CoopVec<float16_t, OUT> outputArray)
{
#if __SLANG__

    const CoopVecComponentType inputType = CoopVecComponentType::FloatE4M3;
    const CoopVecComponentType weightType = CoopVecComponentType::FloatE4M3;
    const CoopVecMatrixLayout weightLayout = CoopVecMatrixLayout::InferencingOptimal;
    const uint stride = 0;

    outputArray = coopVecMatMul<float16_t, OUT, IN, float16_t>(
        inputArray,
        inputType,
        weightBuffer,
        weightOffset,
        weightType,
        weightLayout,
        false,
        stride
    );

#else

    const uint inputType = 21; // F8_E4M3
    const uint weightType = 21; // F8_E4M3
    const uint weightLayout = 2; // MATRIX_LAYOUT_MUL_OPTIMAL
    const uint stride = 0;

    __builtin_MatVecMul(
        outputArray,
        false,
        inputArray,
        false,
        inputType,
        weightBuffer,
        weightOffset,
        weightType,
        OUT,
        IN,
        weightLayout,
        false,
        stride
    );

#endif
}

NTC_TEMPLATE_FN_2(void, NtcEvaluateLayerMatMulAdd_CoopVec_FP8, int, IN, int, OUT)
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    int biasOffset,
    in CoopVec<float16_t, IN> inputArray,
    out CoopVec<float16_t, OUT> outputArray)
{
#if __SLANG__

    const CoopVecComponentType inputType = CoopVecComponentType::FloatE4M3;
    const CoopVecComponentType weightType = CoopVecComponentType::FloatE4M3;
    const CoopVecComponentType biasType = CoopVecComponentType::Float16;
    const CoopVecMatrixLayout weightLayout = CoopVecMatrixLayout::InferencingOptimal;
    const uint stride = 0;

    outputArray = coopVecMatMulAdd<float16_t, OUT, IN, float16_t>(
        inputArray,
        inputType,
        weightBuffer,
        weightOffset,
        weightType,
        weightBuffer,
        biasOffset,
        biasType,
        weightLayout,
        false,
        stride
    );

#else

    const uint inputType = 21; // F8_E4M3
    const uint weightType = 21; // F8_E4M3
    const uint weightLayout = 2; // MATRIX_LAYOUT_MUL_OPTIMAL
    const uint biasType = 8; // FLOAT16
    const uint stride = 0;

    __builtin_MatVecMulAdd(
        outputArray,
        false,
        inputArray,
        false,
        inputType,
        weightBuffer,
        weightOffset,
        weightType,
        OUT,
        IN,
        weightLayout,
        false,
        stride,
        weightBuffer,
        biasOffset,
        biasType
    );

#endif
}

#if __SLANG__
    inline void NtcEvaluateLayer_CoopVec_Int8
        <T_IN: __BuiltinArithmeticType, let T_IN_NUM: int, let IN_IS_PACKED: bool, let IN: int, let OUT: int, let ACT: bool>
#else
    template<typename T_IN, int T_IN_NUM, bool IN_IS_PACKED, int IN, int OUT, bool ACT>
    void NtcEvaluateLayer_CoopVec_Int8
#endif
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    uint scaleBiasOffset,
    int totalChannels,
    in CoopVec<T_IN, T_IN_NUM> inputArray,
    out CoopVec<float, OUT> outputArray)
{
    // See the comment block in the beginning of TextureSet.cpp for the weight layouts

    const uint biasOffset = scaleBiasOffset + totalChannels * sizeof(float);
    
    NtcEvaluateLayerMatMulAdd_CoopVec_Int8<T_IN, T_IN_NUM, IN_IS_PACKED, IN, OUT>
        (weightBuffer, weightOffset, biasOffset, inputArray, outputArray);
    
    // Enforce the scale alignment to help the compiler optimize the code
    scaleBiasOffset &= ~15;
    
#if __SLANG__
    let scale = CoopVec<float, OUT>.load(weightBuffer, scaleBiasOffset);
#else
    CoopVec<float, OUT> scale = weightBuffer.Load<CoopVec<float, OUT> >(scaleBiasOffset);
#endif
    outputArray = outputArray * scale;

    if (ACT)
    {
        NtcHGELUClamp_Forward_CoopVec<float, OUT>(outputArray, true);
    }
}

NTC_TEMPLATE_FN_3(void, NtcEvaluateLayer_CoopVec_FP8, int, IN, int, OUT, bool, ACT)
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    uint scaleBiasOffset,
    bool scaleActivation,
    in CoopVec<float16_t, IN> inputArray,
    out CoopVec<float16_t, OUT> outputArray)
{
    // See the comment block in the beginning of TextureSet.cpp for the weight layouts

    NtcEvaluateLayerMatMulAdd_CoopVec_FP8<IN, OUT>
        (weightBuffer, weightOffset, scaleBiasOffset, inputArray, outputArray);
   
    if (ACT)
    {
        NtcHGELUClamp_Forward_CoopVec<float16_t, OUT>(outputArray, scaleActivation);
    }
}

#if __SLANG__
    inline void NtcEvaluateOutputLayer_CoopVec_FP8
        <let IN: int, let OUT: int>
#else
    template<int IN, int OUT>
    void NtcEvaluateOutputLayer_CoopVec_FP8
#endif
    (ByteAddressBuffer weightBuffer,
    int weightOffset,
    uint scaleBiasOffset,
    in CoopVec<float16_t, IN> inputArray,
    out CoopVec<float, OUT> outputArray)
{
    // See the comment block in the beginning of TextureSet.cpp for the weight layouts

    // Convert the inputs from float16_t to float, necessary for correct output at this time
#if __SLANG__
    CoopVec<float, IN> inputArrayFloat = CoopVec<float, IN>(inputArray);
#else
    CoopVec<float, IN> inputArrayFloat;
    inputArrayFloat = inputArray;
#endif

    const int biasOffset = scaleBiasOffset + OUT * sizeof(float);

    NtcEvaluateLayerMatMulAdd_CoopVec_Int8<float, IN, false, IN, OUT>
        (weightBuffer, weightOffset, biasOffset, inputArrayFloat, outputArray);
    
    // Enforce the scale alignment to help the compiler optimize the code
    scaleBiasOffset &= ~15;
    
#if __SLANG__
    let scale = CoopVec<float, OUT>.load(weightBuffer, scaleBiasOffset);
#else
    CoopVec<float, OUT> scale = weightBuffer.Load<CoopVec<float, OUT> >(scaleBiasOffset);
#endif

    outputArray = outputArray * scale;
}

// NtcSampleTextureSet_CoopVec_Int8 - version of NtcSampleTextureSet that uses Cooperative Vectors with Int8 math.
// Use like SampleTextureSet_CoopVec_Int8<NETWORK_VERSION>(Constants, LatentsBuffer, ...)
// Returns true if the mip level is valid; out-of-bounds texel positions are clamped.
NTC_TEMPLATE_FN_1(bool, NtcSampleTextureSet_CoopVec_Int8, int, VERSION)
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

    CoopVec<uint32_t, Params::INPUT_CHANNELS / 4> networkInputsVec;
    [unroll]
    for (int i = 0; i < Params::INPUT_CHANNELS / 4; ++i)
    {
        networkInputsVec[i] = networkInputs[i];
    }

    // Input layer
    CoopVec<float, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput1;
    NtcEvaluateLayer_CoopVec_Int8<uint32_t, Params::INPUT_CHANNELS/4, true, Params::INPUT_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.x, scaleBiasOffset, totalChannels, networkInputsVec, hiddenOutput1);
    // Advance scaleBiasOffset to point at the next layer - it's here as a workaround for a Slang bug
    // that prevents it from compiling EvaluateLayer_CoopVec_Int8 with scaleBiasOffset as 'inout' parameter.
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float);
    
    // Hidden layer 1
    CoopVec<float, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput2;
    NtcEvaluateLayer_CoopVec_Int8<float, Params::HIDDEN_LAYER_CHANNELS, false, Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.y, scaleBiasOffset, totalChannels, hiddenOutput1, hiddenOutput2);
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float);
    
    // Hidden layer 2
    CoopVec<float, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput3;
    NtcEvaluateLayer_CoopVec_Int8<float, Params::HIDDEN_LAYER_CHANNELS, false, Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.z, scaleBiasOffset, totalChannels, hiddenOutput2, hiddenOutput3);
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float);
    
    // Output layer
    CoopVec<float, Params::OUTPUT_CHANNELS> networkOutputs;
    NtcEvaluateLayer_CoopVec_Int8<float, Params::HIDDEN_LAYER_CHANNELS, false, Params::HIDDEN_LAYER_CHANNELS, Params::OUTPUT_CHANNELS, false>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.w, scaleBiasOffset, totalChannels, hiddenOutput3, networkOutputs);

    [unroll]
    for (int ch = 0; ch < Params::OUTPUT_CHANNELS; ++ch)
    {
        outputs[ch] = networkOutputs[ch];
        
        if (convertToLinearColorSpace)
        {
            outputs[ch] = NtcConvertChannelToLinearColorSpace(desc, ch, outputs[ch]);
        }
    }

    return true;
}

// NtcSampleTextureSet_CoopVec_FP8 - version of NtcSampleTextureSet that uses Cooperative Vectors with FP8 (E4M3) math.
// Use like SampleTextureSet_CoopVec_FP8<NETWORK_VERSION>(Constants, LatentsBuffer, ...)
// Returns true if the mip level is valid; out-of-bounds texel positions are clamped.
NTC_TEMPLATE_FN_1(bool, NtcSampleTextureSet_CoopVec_FP8, int, VERSION)
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

    CoopVec<float16_t, Params::INPUT_CHANNELS> networkInputs;
    if (!NtcPrepareNetworkInputs_FP16<VERSION>(desc, latentTexture, latentSampler, texel, mipLevel, networkInputs))
        return false;

    int scaleBiasOffset = weightsOffset + desc.networkScaleBiasOffset;

    // Evaluate the MLP layers:
    const int totalChannels = Params::HIDDEN_LAYER_CHANNELS * 3 + Params::OUTPUT_CHANNELS;

    // Input layer
    CoopVec<float16_t, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput1;
    NtcEvaluateLayer_CoopVec_FP8<Params::INPUT_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.x, scaleBiasOffset, false, networkInputs, hiddenOutput1);
    // Advance scaleBiasOffset to point at the next layer - it's here as a workaround for a Slang bug
    // that prevents it from compiling EvaluateLayer_CoopVec with scaleBiasOffset as 'inout' parameter.
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float16_t);
    
    // Hidden layer 1
    CoopVec<float16_t, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput2;
    NtcEvaluateLayer_CoopVec_FP8<Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.y, scaleBiasOffset, false, hiddenOutput1, hiddenOutput2);
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float16_t);
    
    // Hidden layer 2
    CoopVec<float16_t, Params::HIDDEN_LAYER_CHANNELS> hiddenOutput3;
    NtcEvaluateLayer_CoopVec_FP8<Params::HIDDEN_LAYER_CHANNELS, Params::HIDDEN_LAYER_CHANNELS, true>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.z, scaleBiasOffset, true, hiddenOutput2, hiddenOutput3);
    scaleBiasOffset += Params::HIDDEN_LAYER_CHANNELS * sizeof(float16_t);
    
    // Output layer
    CoopVec<float, Params::OUTPUT_CHANNELS> networkOutputs;
    NtcEvaluateOutputLayer_CoopVec_FP8<Params::HIDDEN_LAYER_CHANNELS, Params::OUTPUT_CHANNELS>
    (weightsBuffer, weightsOffset + desc.networkWeightOffsets.w, scaleBiasOffset, hiddenOutput3, networkOutputs);

    [unroll]
    for (int ch = 0; ch < Params::OUTPUT_CHANNELS; ++ch)
    {
        outputs[ch] = networkOutputs[ch];
        
        if (convertToLinearColorSpace)
        {
            outputs[ch] = NtcConvertChannelToLinearColorSpace(desc, ch, outputs[ch]);
        }
    }

    return true;
}

#endif