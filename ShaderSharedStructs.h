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

// -------------------------------------------------------
// Basic rendering, Perspective Camera,
// PBR, IBL, and Tone Mapping
// -------------------------------------------------------

struct SALIGN CameraConstants
{
	float4x4 view;
	float4x4 projection;
};

struct SALIGN ModelConstants
{
	float4x4 model;
};

struct SALIGN PBRConstants
{
	float3 eyePosition;
};

struct SALIGN ToneMapperParams
{
	uint toneMappingMode;
	float bloomIntensity;
};

struct SALIGN PrefilterConstants
{
	float roughness;
};

// -------------------------------------------------------
// Mipmap generation
// -------------------------------------------------------
#define NUM_MIPS_PER_PASS 4

struct SALIGN GenerateMipsConstants
{
	uint SrcMipLevel;   // Texture level of source mip
	uint NumMipLevels;  // Number of OutMips to write: [1-4]
	uint SrcDimension;  // Width and height of the source texture are even or odd.
	int IsSRGB;		    // Must apply gamma correction to sRGB textures.
	float2 TexelSize;   // 1.0 / OutMip1.Dimensions
	uint ArraySlice;	// For texture array
};

// -------------------------------------------------------
// Bloom effect related constants
// -------------------------------------------------------

// Blur
#define FILTER_N_THREADS 16  // Number of threads per group
#define MAX_KERNEL_RADUIS 8

struct SALIGN BlurConstants
{
	int blurRadius;
	uint srcWidth;
	uint srcHeight;
	uint srcMipLevel;
};

struct SALIGN BlurKernel
{
	// Shader requires array elements to align to 16 bytes.
	// We create padded array to avoid the need for dynamic indexing in the shader.
	float w[8 * MAX_KERNEL_RADUIS + 4];
};

// For upsample and blend
struct SALIGN BlendParams
{
	float2 uvScale; // Shaders perfer multiplication so we store 0.5f / Target.Dimensions
	uint targetWidth;
	uint targetHeight;
	uint mipLevel;
	float blendFactor;
};

#ifdef __cplusplus
#undef SALIGN
#endif
