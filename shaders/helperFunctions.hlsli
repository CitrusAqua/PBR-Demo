#define PI            3.14159265359f
#define TWO_PI        6.28318530718f
#define FOUR_PI       12.56637061436f
#define FOUR_PI2      39.47841760436f
#define INV_PI        0.31830988618f
#define INV_TWO_PI    0.15915494309f
#define INV_FOUR_PI   0.07957747155f
#define HALF_PI       1.57079632679f
#define INV_HALF_PI   0.636619772367f

#define GRS_INT_SAMPLES_CNT 2048u

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Tone mapping
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float3 LinearToSRGB(float3 color)
{
    // Approximately pow(color, 1.0 / 2.2)
    return color < 0.0031308 ? 12.92 * color : 1.055 * pow(abs(color), 1.0 / 2.4) - 0.055;
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Spherical coordinates
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float2 SampleSphericalMap(float3 coord)
{
    float theta = acos(coord.y);
    float phi = atan2(coord.x, coord.z);
    phi += (phi < 0) ? 2 * PI : 0;

    float u = phi * INV_TWO_PI;
    float v = theta * INV_PI;
    return float2(u, v);
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Low discrepancy sequence on hemisphere.
// Ref: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint Idx, uint N)
{
    return float2(Idx / (float) N, RadicalInverse_VdC(Idx));
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Specular D
// Importance sampling for GGX (Trowbridge-Reitz) distribution.
// Ref: https://cdn2-unrealengine-1251447533.file.myqcloud.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
    float a = Roughness * Roughness;
    float Phi = 2 * PI * Xi.x;
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = sqrt(1 - CosTheta * CosTheta);
    
    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;
    
    float3 UpVector = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 TangentX = normalize(cross(UpVector, N));
    float3 TangentY = cross(N, TangentX);
    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Specular G - Unreal Engine 4 version
// Ref: https://cdn2-unrealengine-1251447533.file.myqcloud.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
//
// For direct light: alpha = (Roughness + 1) / 2
//                   k = alpha^2 / 2 = (Roughness + 1)^2 / 8
//
// For IBL:          alpha = Roughness
//                   k = Roughness^2 / 2
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float SchlickGGX_UE4(float NdotV, float k)
{
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float SpecularG_UE4(float NdotV, float NdotL, float k)
{
    float NdotV_ReLU = max(NdotV, 0.0);
    float NdotL_ReLU = max(NdotL, 0.0);
    return SchlickGGX_UE4(NdotV_ReLU, k) * SchlickGGX_UE4(NdotL_ReLU, k);
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Specular F
// Fresnel equation for dielectric materials with Schlick approximation.
// Code is from Frostbite Engine
// Ref: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Param u: cosine(theta_L) (between the incoming ray and the normal)
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float3 F_Schlick(float3 f0, float f90, float u)
{
    return f0 + ( f90 - f0 ) * pow(1.0f - u , 5.0f);
}

// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// Specular G - Frostbite Engine
// Ref: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
float V_SmithGGXCorrelated(float NdotL, float NdotV, float alphaG)
{
    // Original formulation of G_SmithGGX Correlated
    // lambda_v = ( -1 + sqrt ( alphaG2 * (1 - NdotL2 ) / NdotL2 + 1)) * 0.5f;
    // lambda_l = ( -1 + sqrt ( alphaG2 * (1 - NdotV2 ) / NdotV2 + 1)) * 0.5f;
    // G_SmithGGXCorrelated = 1 / (1 + lambda_v + lambda_l );
    // V_SmithGGXCorrelated = G_SmithGGXCorrelated / (4.0 f * NdotL * NdotV );

    // This is the optimize version
    float alphaG2 = alphaG * alphaG;
    // Caution : the " NdotL *" and " NdotV *" are explicitely inversed , this is not a mistake.
    float Lambda_GGXV = NdotL * sqrt((-NdotV * alphaG2 + NdotV) * NdotV + alphaG2);
    float Lambda_GGXL = NdotV * sqrt((-NdotL * alphaG2 + NdotL) * NdotL + alphaG2);
    return 0.5f / ( Lambda_GGXV + Lambda_GGXL );
}

float D_GGX(float NdotH, float m)
{
    // Divide by PI is apply later
    float m2 = m * m;
    float f = (NdotH * m2 - NdotH) * NdotH + 1;
    return m2 / (f * f) ;
}