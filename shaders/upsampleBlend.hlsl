#include "../ShaderSharedStructs.h"
#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 6), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( SRV(t1, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 1) )," \
    "StaticSampler(s0," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

ConstantBuffer<BlendParams> g_params : register(b0);
Texture2D<float4> g_blendSrcHires : register(t0);
Texture2D<float4> g_blendSrcLowres : register(t1);
RWTexture2D<float4> g_blendTarget : register(u0);
SamplerState g_sampler : register(s0);

[RootSignature(g_RootSignature)]
[numthreads(8, 8, 1)]
void CSMain(int2 dt : SV_DispatchThreadID)
{
    // Out-of-bounds check.
    if (dt.x >= g_params.targetWidth || dt.y >= g_params.targetHeight)
    {
        return;
    }
    
    //
    // Convert dt to uv coords.
    //
    // We want:
    //      uv = ( (dt + 0.5) / targetDimension) * (1.0f / 2^(targetMipLevel + 1))
    //                       ^                                  ^
    //                  range (0, 1)                   the top-left fraction
    //                                              containing the low res input
    //
    // Compute from CPU side:
    //      uvScale = 1.0f / ( targetDimension << (targetMipLevel + 1) )
    //
    float2 uv = ((float2) dt + 0.5f) * g_params.uvScale;
    
    // Blend
    g_blendTarget[dt] = lerp(g_blendSrcHires.Load(int3(dt, g_params.mipLevel)), g_blendSrcLowres.SampleLevel(g_sampler, uv, 0), g_params.blendFactor);
}
