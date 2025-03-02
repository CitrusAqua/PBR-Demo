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
    // Right-handed coordinate system 
    float3 N = normalize(input.obj_position);
    //float3 Up = float3(0.0f, 1.0f, 0.0f);
    float3 Up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f); // fix: when normal is pointing up
    float3 Tangent = normalize(cross(Up, N));
    float3 Bitangent = cross(N, Tangent);
    
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    int n_samples = 0;
    float deltaPhi = TWO_PI / 180.0f;
    float deltaTheta = HALF_PI / 90.0f;
    for (float phi = 0.0f; phi < TWO_PI; phi += deltaPhi)
    {
        for (float theta = 0.0f; theta < HALF_PI; theta += deltaTheta)
        {
            float3 H = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 dir = Tangent * H.x + Bitangent * H.y + N * H.z;
            irradiance += g_cubemap.Sample(g_sampler, dir).rgb * cos(theta) * sin(theta);
            n_samples++;
        }
    }    
    irradiance = PI * irradiance * (1.0f / n_samples);
    return float4(irradiance, 1.0f);
}
