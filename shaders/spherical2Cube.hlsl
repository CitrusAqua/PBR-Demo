#include "../ShaderStructs.h"
#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
	"DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
	"StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_BORDER, " \
		"addressV = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<CameraConstants> g_camera : register(b0);
Texture2D g_tex_spherical : register(t0);
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
    float4 pos = float4(input.obj_position, 1.0f);
    pos = mul(g_camera.view, pos);
    pos = mul(g_camera.projection, pos);
    result.clip_position = pos;
    result.obj_position = input.obj_position;
    return result;
}

[RootSignature(g_RootSignature)]
float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = SampleSphericalMap(normalize(input.obj_position));
    return g_tex_spherical.SampleLevel(g_sampler, uv, 0);
}
