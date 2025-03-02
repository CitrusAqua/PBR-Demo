#pragma once

#ifdef __cplusplus
#include <DirectXMath.h>
typedef DirectX::XMFLOAT4X4 float4x4;
typedef DirectX::XMFLOAT4X3 float4x3;
typedef DirectX::XMFLOAT2 float2;
typedef DirectX::XMFLOAT3 float3;
typedef DirectX::XMFLOAT4 float4;
typedef uint32_t uint;
#endif

#ifdef __cplusplus
#define SALIGN alignas(256)
#else
#define SALIGN
#endif

struct SALIGN CameraConstants
{
	float4x4 view;
	float4x4 projection;
};

struct SALIGN ModelConstants
{
	float4x4 model;
};

struct SALIGN PixelShaderConstants
{
	float3 eyePosition;
	uint ToneMappingMode;
};

struct SALIGN PrefilterConstants
{
	float roughness;
};

#ifdef __cplusplus
#undef SALIGN
#endif
