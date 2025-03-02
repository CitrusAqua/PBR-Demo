#include "../ShaderStructs.h"
#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b1, visibility = SHADER_VISIBILITY_VERTEX), " \
    "CBV(b2, visibility = SHADER_VISIBILITY_PIXEL), " \
	"DescriptorTable(SRV(t0), SRV(t1), SRV(t2), visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t3), SRV(t4), SRV(t5), visibility = SHADER_VISIBILITY_PIXEL), " \
	"StaticSampler(s0, " \
        "filter = FILTER_ANISOTROPIC, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_WRAP, " \
		"addressV = TEXTURE_ADDRESS_WRAP, " \
		"addressW = TEXTURE_ADDRESS_WRAP, " \
        "maxAnisotropy = 16), " \
    "StaticSampler(s1, " \
        "filter = FILTER_MIN_MAG_MIP_LINEAR, " \
        "visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_CLAMP, " \
		"addressV = TEXTURE_ADDRESS_CLAMP, " \
		"addressW = TEXTURE_ADDRESS_CLAMP), " \

ConstantBuffer<CameraConstants> g_camera : register(b0);
ConstantBuffer<ModelConstants> g_model : register(b1);
ConstantBuffer<PixelShaderConstants> g_pscb : register(b2);
TextureCube g_irradiance : register(t0);
TextureCube g_prefilteredEnv : register(t1);
Texture2D<float2> g_BRDF : register(t2);
Texture2D<float4> g_diffuse : register(t3);
Texture2D<float3> g_normal : register(t4);
Texture2D<float3> g_arm : register(t5);  // Ambient Occlusion, Roughness, Metalness
SamplerState g_sampler : register(s0);
SamplerState g_sampler_BRDF : register(s1);

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
    float2 uv : TEXCOORD;
    float3 world_position : WORLD_POSITION;
    float3 world_normal : WORLD_NORMAL;
    float3 world_tangent : WORLD_TANGENT;
    float3 world_bitangent : WORLD_BITANGENT;
};

[RootSignature(g_RootSignature)]
PSInput VSMain(VSInput input)
{
    PSInput result;
    
    //float4x4 model = mul(g_model.translation, g_model.rotation);
    //model = mul(model, g_model.scaling);
    
    float4 world_pos = mul(g_model.model, float4(input.obj_position, 1.0f));
    float4 clip_pos = mul(g_camera.projection, mul(g_camera.view, world_pos));
    result.clip_position = clip_pos;
    result.uv = input.uv;
    result.world_position = world_pos.xyz;
    
    // This is wrong for now
    // Need transform w.r.t. non identical scaling matrix
    result.world_normal = mul(g_model.model, float4(input.obj_normal, 0.0f)).xyz;
    result.world_tangent = mul(g_model.model, float4(input.obj_tangent, 0.0f)).xyz;
    result.world_bitangent = mul(g_model.model, float4(input.obj_bitangent, 0.0f)).xyz;
    
    //result.world_normal = input.normal;

    return result;
}

[RootSignature(g_RootSignature)]
float4 PSMain(PSInput input) : SV_TARGET
{
    //float3 n2 = normalize(input.world_normal) + 1.0f;
    //float3 n3 = n2 / 2.0f;
    //return float4(n3, 1.0f);
    
    float3 normal_color = g_normal.Sample(g_sampler, input.uv).rgb * 2.0f - 1.0f;
    float3x3 TBN = float3x3(input.world_tangent, input.world_bitangent, input.world_normal);
    
    float3 N = normalize(mul(normal_color, TBN));
    float3 V = normalize(g_pscb.eyePosition - input.world_position);
    float3 R = reflect(-V, N);
    float NoV = saturate(dot(N, V));
    
    // Diffuse
    float3 albedo = g_diffuse.Sample(g_sampler, input.uv).rgb;
    float3 diffuse = g_irradiance.Sample(g_sampler, N).rgb * albedo;
    
    // Specular
    float3 arm = g_arm.Sample(g_sampler, input.uv).rgb;
    float ao = arm.r;
    float roughness = arm.g;
    float metalness = arm.b;
    
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metalness);
    float3 F = F_Schlick(F0, roughness, NoV);
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    
    float3 prefilteredColor = g_prefilteredEnv.SampleLevel(g_sampler, R, roughness * 5.0).rgb;
    float2 envBRDF = g_BRDF.Sample(g_sampler_BRDF, float2(min(NoV, 0.999f), roughness)).rg;
    float3 specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y) * INV_PI;
    
    float3 color = (kD * diffuse + specular) * ao;
    //color = prefilteredColor;
    //return float4(envBRDF, 1.0f, 1.0f);
    //return float4(g_BRDF.Sample(g_sampler, input.uv), 0.0f, 1.0f);
    return float4(color, 1.0f);
    
    //float3 reflect = g_environment.Sample(g_sampler, R).rgb;
    //float3 irradiance = g_irradiance.Sample(g_sampler, N).rgb;
    //float3 albedo = g_diffuse.Sample(g_sampler, input.uv).rgb;
    //float3 diffuse = irradiance * albedo;
    
    
    //float3 prefiltered = g_prefilteredEnv.SampleLevel(g_sampler, R, roughness * 5.0).rgb;
    //float2 envBRDF = g_BRDF.Sample(g_sampler, float2(min(NoV, 0.999f), roughness)).rg;
    
    //diffuse = prefiltered * albedo;
    
    //float4 mix = float4((1 - roughness) * prefiltered + roughness * diffuse, 1.0f);
    
    //mix = float4(g_BRDF.Sample(g_sampler, input.uv).rg, 0.0f, 1.0f);
    
    //return mix;
}
