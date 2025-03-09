#include "../ShaderSharedStructs.h"
#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b1, visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, " \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_CLAMP, " \
		"addressV = TEXTURE_ADDRESS_CLAMP, " \
		"addressW = TEXTURE_ADDRESS_CLAMP)"

ConstantBuffer<CameraConstants> g_camera : register(b0);
ConstantBuffer<PrefilterConstants> g_prefilter : register(b1);
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
    
    float roughness = g_prefilter.roughness;
    // Right-handed coordinate system 
    float3 N = normalize(input.obj_position);
    float3 R = N;
    float3 V = R;
    
    float3 prefiltered = float3(0.0f, 0.0f, 0.0f);
    float weight_sum = 0.0f;
    
    // We need to set very large NUM_SAMPLES to get a good result
    // because sun is not seperatly processed rught now.
    // Fix to do: Add a directional light to the scene.
    uint NUM_SAMPLES = 65536u;
    
    for (uint i = 0; i < NUM_SAMPLES; ++i)
    {
        // Importance sample for GGX (Trowbridge-Reitz) distribution
        float2 Xi = Hammersley(i, NUM_SAMPLES);
        float3 H = ImportanceSampleGGX(Xi, roughness, N);
        //float3 L = normalize(2.0f * dot(V, H) * H - V);
        float3 L = -1.0f * reflect(V, H);
        float NoL = saturate(dot(N, L));
        if (NoL > 0.0f)
        {
            // Compute Lod using inverse solid angle and pdf.
            // From Chapter 20.4 Mipmap filtered samples in GPU Gems 3.
            // https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
            float pdf = NoL * INV_PI;
            float solidAngleTexel = 4 * PI / (6 * cubeWidth * cubeWidth);
            float solidAngleSample = 1.0 / (NUM_SAMPLES * pdf);
            float lod = 0.5 * log2((float) (solidAngleSample / solidAngleTexel));
            
            // Incoming lighe intensity is attenuated by factor NoL ( cosing theta L )
            prefiltered += g_cubemap.SampleLevel(g_sampler, L, lod).rgb * NoL;
            weight_sum += NoL;
        }
    }
    
    prefiltered = prefiltered / weight_sum;
    return float4(prefiltered, 1.0f);
}
