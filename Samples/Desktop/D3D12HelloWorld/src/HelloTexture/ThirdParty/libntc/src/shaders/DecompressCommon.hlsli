/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma once

#include "libntc/shaders/ColorSpaces.hlsli"
#include "libntc/shaders/DecompressConstants.h"
#include "libntc/shaders/Inference.hlsli"
#include "HashBasedRNG.hlsli"
#include "Vulkan.hlsli"

#ifdef __cplusplus
static const NtcDecompressConstants g_Const;
#else
VK_BINDING(0, 0) ConstantBuffer<NtcDecompressConstants> g_Const : register(b0);
#endif
VK_BINDING(1, 0) Texture2DArray t_Latents : register(t1);
VK_BINDING(2, 0) ByteAddressBuffer t_WeightBuffer : register(t2);
VK_BINDING(3, 0) SamplerState s_LatentSampler : register(s3);
VK_BINDING(0, 1) RWTexture2D<float4> u_Outputs[] : register(u0);

typedef NtcNetworkParams<NETWORK_VERSION> Params;

static const int MAX_INPUT_SIZE = Params::INPUT_CHANNELS > Params::HIDDEN_LAYER_CHANNELS ? Params::INPUT_CHANNELS : Params::HIDDEN_LAYER_CHANNELS;
static const int MAX_OUTPUT_SIZE = Params::HIDDEN_LAYER_CHANNELS > Params::OUTPUT_CHANNELS ? Params::HIDDEN_LAYER_CHANNELS : Params::OUTPUT_CHANNELS;
