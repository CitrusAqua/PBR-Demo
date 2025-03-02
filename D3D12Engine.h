#pragma once

#include "DXSample.h"

#include <dxcapi.h>
#include <vector>
#include <Windows.h>
#include <memory>
#include "StepTimer.h"

#include "MatricesAndMeshes.h"
#include "DescHeapWrapper.h"
#include "ShaderStructs.h"
#include "HelperFunctions.h"
#include "SMesh.h"
#include "STexture.h"

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
using Microsoft::WRL::ComPtr;

namespace
{
	constexpr UINT FRAME_COUNT = 2;  // Use double buffering

	// SSAA configurations
	constexpr UINT SSAA_MULTIPLIER = 2;

	// MSAA configurations
	constexpr UINT MSAA_COUNT = 8;
	constexpr UINT MSAA_QUALITY = 0;
	constexpr DXGI_FORMAT MSAA_DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;

	// HDR rendering configurations
	constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

	// Camera parameters
	constexpr float CAMERA_SENSITIVITY = 0.05f;   // Mouse movement sensitivity
	constexpr float CAMERA_SPEED = 2.0f;      // Keyboard movement speed (units per second)

	enum  // Root Signature and PSO indexing
	{
		PSO_Render = 0,
		PSO_Present8bit,
		PSO_Spherical2Cube,
		PSO_SampleEnvMap,
		PSO_CreateIrradianceMap,
		PSO_PrefilterEnvMap,
		PSO_CreateBRDFMap,
		PSO_Count
	};

	enum  // Tone mapping options
	{
		ToneMappingMode_sRGB = 0,   // The display expects an sRGB signal.
		//  ToneMappingMode_ST2084, // The display expects an HDR10 signal.
		ToneMappingMode_None        // The display expects a linear signal.
	};

	// Screen clear color
	constexpr float CLEAR_COLOR[] = { 112.0f / 255.0f, 151.0f / 255.0f, 232.0f / 255.0f, 1.0f };
}

class D3D12Engine : public DXSample
{
public:
	D3D12Engine(UINT width, UINT height, std::wstring name);
	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

	// Perspective Camera Control
	void OnKeyDown(UINT8 /*key*/);
	void OnKeyUp(UINT8 /*key*/);
	void OnButtonDown(UINT32 lParam);
	void OnMouseMove(UINT8 wParam, UINT32 lParam);

private:
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[FRAME_COUNT];
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	// Heap Helper
	DescHeapWrapper m_HH;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void CreateContext();
	void CreateFrameResources();
	void LoadAssets();
	void WaitForPreviousFrame();

	// =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
	// Root Signature and PSO
	//
	ComPtr<ID3D12RootSignature> m_rootSignatures[PSO_Count];
	ComPtr<ID3D12PipelineState> m_pipelineStates[PSO_Count];
	void AddGraphicsPipeline(UINT32, D3D12_GRAPHICS_PIPELINE_STATE_DESC&, const char*, const char*);
	void AddComputePipeline(UINT32 PSOIndex, const char* CSName);
	void CreatePipelines();

	// =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
	// Camera related members
	//
	DirectX::XMFLOAT3 cameraPosition; // Camera position
	DirectX::XMFLOAT3 cameraForward;  // Initial forward vector
	DirectX::XMFLOAT3 cameraRight;    // Initial right vector
	float cameraYaw;    // Horizontal rotation
	float cameraPitch;  // Vertical rotation

	StepTimer m_timer;
	bool keyStates[256] = { false };

	void InitCamera();
	void UpdateCameraBuffer(float);
	void RotateObject(float);	

	// Shader constant buffer data.
	// Any modification to the CPU data will be autimatically mapped to GPU.
	// Raw pointers point to a location in Heap which is managed by the DescHeapWrapper,
	// so we do not use smart pointer here.
	CameraConstants* m_cameraConstants;
	PixelShaderConstants* m_pixelConstants;
	D3D12_GPU_VIRTUAL_ADDRESS m_cameraConstants_GPUAddr;
	D3D12_GPU_VIRTUAL_ADDRESS m_pixelConstants_GPUAddr;

	void CreateConstantBufferViews();

	// =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
	// HDR Rendering & Tone Mapping
	//
	ComPtr<ID3D12Resource> m_msaaRenderTarget;
	ComPtr<ID3D12Resource> m_depthStencilBuffer;
	ComPtr<ID3D12Resource> m_hdrResolveTarget;
	ComPtr<ID3D12Resource> m_renderTargets[FRAME_COUNT];

	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_frameBuffers[FRAME_COUNT];
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_msaaRenderTarget;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DSV_depthStencilBuffer;
	//D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_hdrResolveTarget;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_hdrResolveTarget;
	
	// =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
	// Meshes & textures
	//
	SMesh m_presentTriangle;
	SMesh m_cube;
	SMesh m_cubeInsideFacing;
	std::vector<SMesh> m_meshes;

	// Object textures
	std::vector<STexture> m_textures;

	// =*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=
	// Environment Map
	//
	STexture m_sphericalTexture;
	ComPtr<ID3D12Resource> m_envMap;
	ComPtr<ID3D12Resource> m_irradianceMap;
	ComPtr<ID3D12Resource> m_prefilteredEnvMap;
	ComPtr<ID3D12Resource> m_BRDFMap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_envMap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_irradianceMap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_prefilteredEnvMap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_BRDFMap;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRV_IBL;

	void LoadIBL(const char* filename);
};
