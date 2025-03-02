#include "helperFunctions.hlsli"

#define g_RootSignature \
    "RootFlags(0), " \
	"DescriptorTable(UAV(u0))"

RWTexture2D<float4> g_BRDFMap : register(u0);

// *************************************************************************************************
// Unreal Engine 4 style environment BRDF pre-computation.
// Ref: https://cdn2-unrealengine-1251447533.file.myqcloud.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
//
// We make 
float2 IntegrateBRDF(float Roughness, float NoV)
{
    float3 V;
    V.x = 0.0f;
    V.y = NoV; // cos
    V.z = sqrt(1.0f - NoV * NoV); // sin

    float3 N = float3(0.0f, 1.0f, 0.0f);

    // 
    float A = 0.0f;
    float B = 0.0f;
    uint NUM_SAMPLES = 4096u;
    for (uint i = 0; i < NUM_SAMPLES; ++i)
    {
        float2 Xi = Hammersley(i, NUM_SAMPLES);
        float3 H = ImportanceSampleGGX(Xi, Roughness, N);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        //float3 L = -1.0f * reflect(V, H);
        
        // This code is an example of call of previous functions
        //float NdotV = abs(dot(N, V)) + 1e-5f; // avoid artifact
        //float LdotH = saturate(dot(L, H));
        //float NdotH = saturate(dot(N, H));
        //float NdotL = saturate(dot(N, L));
        
        float NdotV = abs(dot(N, V)) + 1e-5f; // avoid artifact
        //float LdotH = saturate(dot(L, H));
        float NdotL = saturate(dot(N, L));
        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0f)
        {
            //float k = Roughness * Roughness * Roughness * Roughness / 2.0f;
            //float G_Vis = V_SmithGGXCorrelated(NdotV, NdotL, Roughness);
            //float G_Vis = G * VoH / (NoH * NoV);
            //float G_Vis = G / (4.0f * NdotL * NdotV);
            //float Fc = pow(1.0f - VoH, 5.0f);
            
            //A += (1.0f - Fc) * G_Vis;
            //B += Fc * G_Vis;
            
            float k = Roughness * Roughness / 2;
            float G = SpecularG_UE4(NdotV, NdotL, k);
            float G_Vis = G * VdotH / (NdotH * NdotV);
            float Fc = pow(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    
    return float2(A, B) / NUM_SAMPLES;
}

[RootSignature(g_RootSignature)]
[numthreads(8, 8, 1)]
void CSMain(uint3 ThreadID : SV_DispatchThreadID)
{
    float Width, Height;
    g_BRDFMap.GetDimensions(Width, Height);

    float Roughness = (ThreadID.y + 1) / Height;
    float NoV = (ThreadID.x + 1) / Width;

    float2 Result = IntegrateBRDF(Roughness, NoV);

    g_BRDFMap[ThreadID.xy] = float4(Result, 0.0f, 1.0f);
    //g_BRDFMap[ThreadID.xy] = float4((float) ThreadID.x / Height, (float) ThreadID.y / Width, 0.0f, 1.0f);
}
