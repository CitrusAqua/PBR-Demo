#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(0), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 1) )"

#define V_THRESHOLD 0.0f

Texture2D<float4> inImage : register(t0);
RWTexture2D<float4> outImage : register(u0);

[RootSignature(g_RootSignature)]
[numthreads(8, 8, 1)]
void CSMain(int2 dt : SV_DispatchThreadID)
{
    // Skip for now
    float brightness = float(dot(inImage[dt].rgb, float3(0.299f, 0.587f, 0.114f)));
    if (brightness > V_THRESHOLD)
    {
        outImage[dt] = inImage[dt];
    }
    else
    {
        outImage[dt] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
