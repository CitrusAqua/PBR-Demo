// present.hlsl
// Handles the final tone mapping and display of the scene.
// Bloom effect is also blended in here.

#include "../ShaderSharedStructs.h"
#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "RootConstants(b0, num32BitConstants = 2, visibility = SHADER_VISIBILITY_PIXEL), " \
	"DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL), " \
	"StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_MIP_LINEAR, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_BORDER, " \
		"addressV = TEXTURE_ADDRESS_BORDER, " \
		"addressW = TEXTURE_ADDRESS_BORDER," \
        "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK)"

#define DISPLAY_CURVE_SRGB      0
#define ACES_FILMIC             1
#define DISPLAY_CURVE_LINEAR    2

//ConstantBuffer<VertexShaderConstants> g_vscb : register(b0);
ConstantBuffer<ToneMapperParams> g_tmp : register(b0);
Texture2D g_hdrscene : register(t0);
Texture2D g_hdrbloom : register(t1);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

[RootSignature(g_RootSignature)]
PSInput VSMain(VSInput input)
{
    PSInput result;
    result.position = float4(input.position, 1.0f);
    result.uv = input.uv;
    return result;
}

[RootSignature(g_RootSignature)]
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 scene = g_hdrscene.SampleLevel(g_sampler, input.uv, 0).rgb;
    float3 bloom = g_hdrbloom.SampleLevel(g_sampler, input.uv, 0).rgb;
    float3 result = lerp(scene, bloom, g_tmp.bloomIntensity);
    
    if (g_tmp.toneMappingMode == DISPLAY_CURVE_SRGB)
    {
        result = LinearToSRGB(result);
    }
    else if (g_tmp.toneMappingMode == ACES_FILMIC)
    {
        result = ACESFilm(result);
    }
    else // ToneMappingMode == DISPLAY_CURVE_LINEAR
    {
        // Just pass through
    }
    
    return float4(result, 1.0f);
}