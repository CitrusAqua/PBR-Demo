#include "../ShaderStructs.h"

#define g_RootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_MIP_LINEAR, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_BORDER, " \
		"addressV = TEXTURE_ADDRESS_BORDER, " \
		"addressW = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<CameraConstants> g_camera : register(b0);
TextureCube g_cubemap : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 obj_position : POSITION;
    float2 uv : TEXCOORD;
    float3 obj_normal : NORMAL;
    float3 obj_tangent : TANGENT;
    float3 obj_bitangent : BITANGENT;
};

struct PSInput
{
    float4 clip_position : SV_POSITION;
    float3 obj_position : OBJECT_POSITION;
};

[RootSignature(g_RootSignature)]
PSInput VSMain(VSInput input)
{  
    PSInput result;
    
    float3 pos = mul((float3x3) g_camera.view, input.obj_position);
    result.clip_position = mul(g_camera.projection, float4(pos, 1.0f));
    result.obj_position = input.obj_position;
    
    return result;
}

[RootSignature(g_RootSignature)]
float4 PSMain(PSInput input) : SV_TARGET
{
    //return g_cubemap.Sample(g_sampler, input.obj_position);
    return g_cubemap.SampleLevel(g_sampler, input.obj_position, 1);
}