// ===== ===== ===== ===== ===== ===== ===== =====
// Compute shader to generate mipmaps for a given texture.
// Source: https://www.3dgep.com/learning-directx-12-4/
// Ref: https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/GenerateMipsCS.hlsli
// ===== ===== ===== ===== ===== ===== ===== =====

#include "../ShaderSharedStructs.h"
#include "helperFunctions.hlsli"

#define GenerateMips_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 7), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 4) )," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_BORDER," \
        "addressV = TEXTURE_ADDRESS_BORDER," \
        "addressW = TEXTURE_ADDRESS_BORDER," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

#define WIDTH_HEIGHT_EVEN 0     // Both the width and the height of the texture are even.
#define WIDTH_ODD_HEIGHT_EVEN 1 // The texture width is odd and the height is even.
#define WIDTH_EVEN_HEIGHT_ODD 2 // The texture width is even and the height is odd.
#define WIDTH_HEIGHT_ODD 3      // Both the width and height of the texture are odd.

ConstantBuffer<GenerateMipsConstants> g_cb : register(b0);

// Source mip map.
Texture2DArray<float4> SrcMip : register(t0);
 
// Write up to 4 mip map levels.
RWTexture2D<float4> OutMip1 : register(u0);
RWTexture2D<float4> OutMip2 : register(u1);
RWTexture2D<float4> OutMip3 : register(u2);
RWTexture2D<float4> OutMip4 : register(u3);
 
// Linear clamp sampler.
SamplerState LinearClampSampler : register(s0);

// The reason for separating channels is to reduce bank conflicts in the
// local data memory controller.  A large stride will cause more threads
// to collide on the same memory bank.
groupshared float gs_R[64];
groupshared float gs_G[64];
groupshared float gs_B[64];
groupshared float gs_A[64];

void StoreColor(uint Index, float4 Color)
{
    gs_R[Index] = Color.r;
    gs_G[Index] = Color.g;
    gs_B[Index] = Color.b;
    gs_A[Index] = Color.a;
}

float4 LoadColor(uint Index)
{
    return float4(gs_R[Index], gs_G[Index], gs_B[Index], gs_A[Index]);
}

// Convert linear color to sRGB before storing if the original source is 
// an sRGB texture.
float4 PackColor(float4 x)
{
    if (g_cb.IsSRGB == 1)
    {
        return float4(LinearToSRGB(x.rgb), x.a);
    }
    else
    {
        return x;
    }
}

[RootSignature(GenerateMips_RootSignature)]
[numthreads(8, 8, 1)]
void CSMain(uint3 ThreadID : SV_DispatchThreadID, uint GroupIndex : SV_GroupIndex)
{
    // One bilinear sample is insufficient when scaling down by more than 2x.
    // You will slightly undersample in the case where the source dimension
    // is odd.  This is why it's a really good idea to only generate mips on
    // power-of-two sized textures.  Trying to handle the undersampling case
    // will force this shader to be slower and more complicated as it will
    // have to take more source texture samples.
    float4 Src1 = { 0.0f, 0.0f, 0.0f, 0.0f };
    float2 UV = { 0.0f, 0.0f };
    float2 Off = { 0.0f, 0.0f };
    switch (g_cb.SrcDimension)
    {
        case WIDTH_HEIGHT_EVEN:
            UV = g_cb.TexelSize * (ThreadID.xy + 0.5);
            Src1 = SrcMip.SampleLevel(LinearClampSampler, float3(UV, g_cb.ArraySlice), g_cb.SrcMipLevel);
        break;
        
        case WIDTH_ODD_HEIGHT_EVEN:
            // > 2:1 in X dimension
            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // horizontally.
            UV = g_cb.TexelSize * (ThreadID.xy + float2(0.25, 0.5));
            Off = g_cb.TexelSize * float2(0.5, 0.0);
            Src1 = 0.5 * (SrcMip.SampleLevel(LinearClampSampler, float3(UV, g_cb.ArraySlice), g_cb.SrcMipLevel) +
                   SrcMip.SampleLevel(LinearClampSampler, float3(UV + Off, g_cb.ArraySlice), g_cb.SrcMipLevel));
        break;
        
        case WIDTH_EVEN_HEIGHT_ODD:

            // > 2:1 in Y dimension
            // Use 2 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // vertically.
            UV = g_cb.TexelSize * (ThreadID.xy + float2(0.5, 0.25));
            Off = g_cb.TexelSize * float2(0.0, 0.5);
            Src1 = 0.5 * (SrcMip.SampleLevel(LinearClampSampler, float3(UV, g_cb.ArraySlice), g_cb.SrcMipLevel) +
                   SrcMip.SampleLevel(LinearClampSampler, float3(UV + Off, g_cb.ArraySlice), g_cb.SrcMipLevel));
        break;
        
        case WIDTH_HEIGHT_ODD:
            // > 2:1 in in both dimensions
            // Use 4 bilinear samples to guarantee we don't undersample when downsizing by more than 2x
            // in both directions.
            UV = g_cb.TexelSize * (ThreadID.xy + float2(0.25, 0.25));
            Off = g_cb.TexelSize * 0.5;
            Src1 = SrcMip.SampleLevel(LinearClampSampler, float3(UV, g_cb.ArraySlice), g_cb.SrcMipLevel);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, float3(UV + float2(Off.x, 0.0), g_cb.ArraySlice), g_cb.SrcMipLevel);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, float3(UV + float2(0.0, Off.y), g_cb.ArraySlice), g_cb.SrcMipLevel);
            Src1 += SrcMip.SampleLevel(LinearClampSampler, float3(UV + float2(Off.x, Off.y), g_cb.ArraySlice), g_cb.SrcMipLevel);
            Src1 *= 0.25;
        break;
    }
    
    OutMip1[ThreadID.xy] = PackColor(Src1);
    
    // A scalar (constant) branch can exit all threads coherently.
    if (g_cb.NumMipLevels == 1)
        return;
 
    // Without lane swizzle operations, the only way to share data with other
    // threads is through LDS.
    StoreColor(GroupIndex, Src1);
 
    // This guarantees all LDS writes are complete and that all threads have
    // executed all instructions so far (and therefore have issued their LDS
    // write instructions.)
    GroupMemoryBarrierWithGroupSync();
    
    // With low three bits for X and high three bits for Y, this bit mask
    // (binary: 001001) checks that X and Y are even.
    if ((GroupIndex & 0x9) == 0)
    {
        float4 Src2 = LoadColor(GroupIndex + 0x01);
        float4 Src3 = LoadColor(GroupIndex + 0x08);
        float4 Src4 = LoadColor(GroupIndex + 0x09);
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
 
        OutMip2[ThreadID.xy / 2] = PackColor(Src1);
        StoreColor(GroupIndex, Src1);
    }
    
    if (g_cb.NumMipLevels == 2)
        return;
 
    GroupMemoryBarrierWithGroupSync();
    
    // This bit mask (binary: 011011) checks that X and Y are multiples of four.
    if ((GroupIndex & 0x1B) == 0)
    {
        float4 Src2 = LoadColor(GroupIndex + 0x02);
        float4 Src3 = LoadColor(GroupIndex + 0x10);
        float4 Src4 = LoadColor(GroupIndex + 0x12);
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
 
        OutMip3[ThreadID.xy / 4] = PackColor(Src1);
        StoreColor(GroupIndex, Src1);
    }
    
    if (g_cb.NumMipLevels == 3)
        return;
 
    GroupMemoryBarrierWithGroupSync();
    
    // This bit mask would be 111111 (X & Y multiples of 8), but only one
    // thread fits that criteria.
    if (GroupIndex == 0)
    {
        float4 Src2 = LoadColor(GroupIndex + 0x04);
        float4 Src3 = LoadColor(GroupIndex + 0x20);
        float4 Src4 = LoadColor(GroupIndex + 0x24);
        Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);
 
        OutMip4[ThreadID.xy / 8] = PackColor(Src1);
    }
}