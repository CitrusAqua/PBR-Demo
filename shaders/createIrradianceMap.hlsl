#include "../ShaderSharedStructs.h"
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
    uint cubeWidth, cubeHeight;
    g_cubemap.GetDimensions(cubeWidth, cubeHeight);
    
    float3 N = normalize(input.obj_position);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    
    //int n_samples = 0;
    //float deltaPhi = TWO_PI / 180.0f;
    //float deltaTheta = HALF_PI / 90.0f;    
    //for (float phi = 0.0f; phi < TWO_PI; phi += deltaPhi)
    //{
    //    for (float theta = 0.0f; theta < HALF_PI; theta += deltaTheta)
    //    {
    //        float3 H = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
    //        float3 L = Tangent * H.x + Bitangent * H.y + N * H.z;
    //        irradiance += g_cubemap.SampleLevel(g_sampler, L, 0).rgb * cos(theta) * sin(theta);
    //        n_samples++;
    //    }
    //}
    
    // We need to set very large NUM_SAMPLES to get a good result
    // because sun is not seperatly processed rught now.
    // Fix to do: Add a directional light to the scene.
    uint NUM_SAMPLES = 65536u;
    for (uint i = 0; i < NUM_SAMPLES; i++)
    {
        float2 Xi = Hammersley(i, NUM_SAMPLES);
        float3 L = importanceSampleDiffuse(Xi, N);
        float NoL = saturate(dot(N, L));

        // Compute Lod using inverse solid angle and pdf.
        // From Chapter 20.4 Mipmap filtered samples in GPU Gems 3.
        // https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
        float pdf = NoL * INV_PI;
        float solidAngleTexel = 4 * PI / (6 * cubeWidth * cubeWidth);
        float solidAngleSample = 1.0 / (NUM_SAMPLES * pdf);
        float lod = 0.5 * log2((float) (solidAngleSample / solidAngleTexel));
            
        irradiance += g_cubemap.SampleLevel(g_sampler, L, lod).rgb;
    }
    
    irradiance = irradiance * (1.0f / NUM_SAMPLES);
    return float4(irradiance, 1.0f);
}
