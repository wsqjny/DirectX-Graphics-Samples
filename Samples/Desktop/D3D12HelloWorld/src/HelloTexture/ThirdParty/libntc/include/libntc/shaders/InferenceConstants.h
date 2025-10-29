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

#ifndef NTC_INFERENCE_CONSTANTS_H
#define NTC_INFERENCE_CONSTANTS_H

#ifdef __cplusplus
#define NTC_UINT unsigned int
#else
#define NTC_UINT uint
#endif

#define NTC_MAX_MIPS        16
#define NTC_MAX_CHANNELS    16
#define NTC_MAX_NEURAL_MIPS 8

#define NTC_NETWORK_UNKNOWN 0
#define NTC_NETWORK_SMALL   1
#define NTC_NETWORK_MEDIUM  2
#define NTC_NETWORK_LARGE   3
#define NTC_NETWORK_XLARGE  4
#define NTC_NETWORK_COUNT   (NTC_NETWORK_XLARGE)

#define NTC_MLP_LAYERS                4
#define NTC_MLP_FEATURES_SMALL        9  // Feature counts are multiples of 3 because latents are stored in BC1 textures
#define NTC_MLP_FEATURES_MEDIUM       15 // with no alpha, so each texture can hold 3 features.
#define NTC_MLP_FEATURES_LARGE        24
#define NTC_MLP_FEATURES_XLARGE       33
#define NTC_MLP_POS_ENC_SCALE         8
#define NTC_MLP_SUPPLEMENTAL_INPUTS   14 // Positional encoding (12 inputs) and the mip level (twice)
#define NTC_MLP_INPUT_CHANNELS_SMALL  32 // roundup(NTC_MLP_SUPPLEMENTAL_INPUTS + NTC_MLP_FEATURES_SMALL  * 2, 16)
#define NTC_MLP_INPUT_CHANNELS_MEDIUM 48 // roundup(NTC_MLP_SUPPLEMENTAL_INPUTS + NTC_MLP_FEATURES_MEDIUM * 2, 16)
#define NTC_MLP_INPUT_CHANNELS_LARGE  64 // roundup(NTC_MLP_SUPPLEMENTAL_INPUTS + NTC_MLP_FEATURES_LARGE  * 2, 16)
#define NTC_MLP_INPUT_CHANNELS_XLARGE 80 // roundup(NTC_MLP_SUPPLEMENTAL_INPUTS + NTC_MLP_FEATURES_XLARGE * 2, 16)
#define NTC_MLP_HIDDEN_CHANNELS       64
#define NTC_MLP_OUTPUT_CHANNELS       16

struct NtcColorMipConstants
{
    int neuralMip;
    float positionLod;
    float positionScale;
    int pad;
};

struct NtcTextureSetConstants
{
#ifdef __cplusplus
    NtcColorMipConstants colorMips[NTC_MAX_MIPS];
    int networkWeightOffsets[NTC_MLP_LAYERS];
#else
    // These structures are packed as int4 for compatibility with engines
    // that don't support structs in constant buffers.
    int4 colorMips[NTC_MAX_MIPS];
    
    // This maps to the int[] array on the host side
    // but doesn't require 16-byte alignment for each element.
    int4 networkWeightOffsets;
#endif

    int imageWidth;
    int imageHeight;
    int imageMips;    
    int networkScaleBiasOffset;

    NTC_UINT validChannelMask;
    NTC_UINT channelColorSpaces; // Packed with 2 bits for each channel
    int pad1;
    int pad2;
};

#endif
