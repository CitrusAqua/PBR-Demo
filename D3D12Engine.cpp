#include "stdafx.h"
#include "D3D12Engine.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <windowsx.h>

using namespace DirectX;
using std::vector;

D3D12Engine::D3D12Engine(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_cameraConstants(nullptr),
	m_pbrConstants(nullptr),
	m_blurKernel(nullptr)
{
}

void D3D12Engine::OnInit()
{
	m_widthSSAA = m_width * SSAA_MULTIPLIER;
	m_heightSSAA = m_height * SSAA_MULTIPLIER;

	CreateContext();
	CreateFrameResources();
	CreateMipmapResources();
	CreateConstantBufferViews();
	CreatePipelines();

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

	// Load any assets here.
	LoadAssets();
	//LoadIBL("resources/hdris/veranda_4k.exr");
	//LoadIBL("resources/hdris/illovo_beach_balcony_4k.exr");
	//LoadIBL("resources/hdris/brown_photostudio_02_8k.exr");
	//LoadIBL("resources/hdris/blaubeuren_church_square_4k.exr");


	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists2[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists2), ppCommandLists2);
	WaitForPreviousFrame();

	// Release upload heaps after the command list has been executed.
	m_presentTriangle.ReleaseUploadHeaps();
	for (auto& m : m_meshes)
	{
		m.ReleaseUploadHeaps();
	}

	m_sphericalTexture.ReleaseUploadHeaps();
	for (auto& t : m_textures)
	{
		t.ReleaseUploadHeaps();
	}

	// Any other initialization logic goes here.
	InitCamera();
}

void D3D12Engine::CreateContext()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_COUNT;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();


	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	m_fenceValue = 1;

	// Create an event handle to use for frame synchronization.
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

// Load the rendering pipeline dependencies.
void D3D12Engine::CreateFrameResources()
{
	// Create Descriptor Heaps
	m_HH.Init(m_device.Get());

	// Create swap chain frame resources.
	{
		// Create a RTV for each frame.
		for (UINT n = 0; n < FRAME_COUNT; n++)
		{
			m_RTV_frameBuffers[n] = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, m_RTV_frameBuffers[n]);
		}
	}

	// Create MSAA render target.
	{
		// Create an MSAA render target.
		D3D12_RESOURCE_DESC msaaRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			HDR_FORMAT,
			m_widthSSAA,
			m_heightSSAA,
			1, // This depth stencil view has only one texture.
			1, // Use a single mipmap level.
			MSAA_COUNT,
			MSAA_QUALITY
		);
		msaaRTDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		CD3DX12_CLEAR_VALUE msaaClearValue = {};
		msaaClearValue.Format = HDR_FORMAT;
		memcpy(msaaClearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&msaaRTDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&msaaClearValue,
			IID_PPV_ARGS(&m_msaaRenderTarget)));
		m_msaaRenderTarget->SetName(L"MSAA Render Target");

		m_RTV_msaaRenderTarget = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = HDR_FORMAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
		m_device->CreateRenderTargetView(m_msaaRenderTarget.Get(), &rtvDesc, m_RTV_msaaRenderTarget);
	}

	// MSAA depth-stencil target.
	{
		D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			MSAA_DEPTH_FORMAT,
			m_widthSSAA,
			m_heightSSAA,
			1, // This render target view has only one texture.
			1, // Use a single mipmap level
			MSAA_COUNT,
			MSAA_QUALITY
		);
		depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE(MSAA_DEPTH_FORMAT, 1.0f, 0),
			IID_PPV_ARGS(&m_depthStencilBuffer)));
		m_depthStencilBuffer->SetName(L"MSAA Depth Stencil Buffer");

		m_DSV_depthStencilBuffer = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = MSAA_DEPTH_FORMAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
		m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_DSV_depthStencilBuffer);
	}

	// Create HDR MSAA resolve target (intermediate render target) resources.
	{
		D3D12_RESOURCE_DESC renderTargetDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			HDR_FORMAT,
			m_widthSSAA,
			m_heightSSAA,
			1, // This render target view has only one texture.
			1  // Use a single mipmap level
		);

		CD3DX12_CLEAR_VALUE hdrClearValue = {};
		hdrClearValue.Format = HDR_FORMAT;
		memcpy(hdrClearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&renderTargetDesc,
			D3D12_RESOURCE_STATE_RESOLVE_DEST,
			nullptr,
			IID_PPV_ARGS(&m_hdrResolveTarget)));
		m_hdrResolveTarget->SetName(L"HDR Resolve Target");

		//D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		//rtvDesc.Format = HDR_FORMAT;
		//rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		//m_RTV_hdrRenderTarget = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
		//m_device->CreateRenderTargetView(m_hdrRenderTarget.Get(), &rtvDesc, m_RTV_hdrRenderTarget);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = HDR_FORMAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle_hdrSrv = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
		m_device->CreateShaderResourceView(m_hdrResolveTarget.Get(), &srvDesc, CPUHandle_hdrSrv);
		m_SRV_hdrResolveTarget = m_HH.CopyDescriptorsToGPUHeap(1, CPUHandle_hdrSrv);
	}

	// Temp buffers for post-processing.
	{
		// Common descriptors
		D3D12_RESOURCE_DESC postprocessingDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			HDR_FORMAT,
			m_widthSSAA,
			m_heightSSAA,
			1,  // ArraySize
			0,  // MipLevels: 0 for automatically calculate the maximum mip levels supported
			1,  // SampleCount
			0,  // SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS  // Allow UAV
		);

		CD3DX12_CLEAR_VALUE hdrClearValue = {};
		hdrClearValue.Format = HDR_FORMAT;
		memcpy(hdrClearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = HDR_FORMAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = HDR_FORMAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = HDR_FORMAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

		for (uint32_t i = 0; i < NUM_PP_BUFFERS; ++i)
		{
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&postprocessingDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,  // Initial state is UAV
				nullptr, //&hdrClearValue,
				IID_PPV_ARGS(&m_ppBuffers[i])));

			std::wstring bufferName = L"Post-processing buffer " + std::to_wstring(i);
			m_ppBuffers[i]->SetName(bufferName.c_str());

			m_UAV_ppBuffers_CPU[i] = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
			m_device->CreateUnorderedAccessView(m_ppBuffers[i].Get(), nullptr, &uavDesc, m_UAV_ppBuffers_CPU[i]);
			m_UAV_ppBuffers[i] = m_HH.CopyDescriptorsToGPUHeap(1, m_UAV_ppBuffers_CPU[i]);

			D3D12_CPU_DESCRIPTOR_HANDLE SRV_CPUHandle = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
			srvDesc.Texture2D.MipLevels = m_ppBuffers[i]->GetDesc().MipLevels;
			m_device->CreateShaderResourceView(m_ppBuffers[i].Get(), &srvDesc, SRV_CPUHandle);
			m_SRV_ppBuffers[i] = m_HH.CopyDescriptorsToGPUHeap(1, SRV_CPUHandle);

			// We dont use RTV for pp buffers
			//m_RTV_ppBuffers[i] = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
			//m_device->CreateRenderTargetView(m_ppBuffers[i].Get(), &rtvDesc, m_RTV_ppBuffers[i]);
		}
	}

	// Create a command allocator for each frame.
	for (UINT n = 0; n < FRAME_COUNT; n++)
	{
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
	}
}

void D3D12Engine::CreateMipmapResources()
{
	// [NUM_MIP_FORMATS] UAVs are created for each mip format.
	// Each UAV holds [NUM_MIPS_PER_PASS] textures.
	for (uint32_t index_format = 0; index_format < NUM_MIP_FORMATS; ++index_format)
	{
		uint32_t width = PADDING_MIPMAP_MAX_WIDTH / 2;
		uint32_t height = PADDING_MIPMAP_MAX_HEIGHT / 2;

		D3D12_CPU_DESCRIPTOR_HANDLE UAV = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);
		D3D12_CPU_DESCRIPTOR_HANDLE currentUAV = UAV;

		DXGI_FORMAT format = MIP_FORMATS[index_format];
		for (uint32_t i = 0; i < NUM_MIPS_PER_PASS; ++i)
		{
			CD3DX12_RESOURCE_DESC TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
			TextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&TextureDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&m_mipTemps[index_format * NUM_MIPS_PER_PASS + i])));

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			m_device->CreateUnorderedAccessView(m_mipTemps[index_format * NUM_MIPS_PER_PASS + i].Get(), nullptr, &uavDesc, currentUAV);

			width /= 2;
			height /= 2;
			currentUAV.ptr += m_HH.GetDescriptorSizeCBV_SRV_UAV();
		}

		m_UAV_mipTemps[index_format] = m_HH.CopyDescriptorsToGPUHeap(4, UAV);
	}
}

void D3D12Engine::CreateConstantBufferViews()
{
	// Camera Constants
	{
		m_cameraConstants = (CameraConstants*)m_HH.AllocateGPUMemory(sizeof(CameraConstants), m_cameraConstants_GPUAddr);
	}

	// Pixel Shader Constants
	{
		m_pbrConstants = (PBRConstants*)m_HH.AllocateGPUMemory(sizeof(PBRConstants), m_pbrConstants_GPUAddr);
	}

	// Blur Kernel
	{
		m_blurKernel = (BlurKernel*)m_HH.AllocateGPUMemory(sizeof(BlurKernel), m_blurKernel_GPUAddr);
	}
}

void D3D12Engine::CreatePipelines()
{

	const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// PSO_Render
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.MultisampleEnable = MSAA_COUNT > 1 ? TRUE : FALSE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		//psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		//psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = HDR_FORMAT;
		psoDesc.SampleMask = UINT32_MAX;
		psoDesc.SampleDesc.Count = MSAA_COUNT;
		psoDesc.SampleDesc.Quality = MSAA_QUALITY;
		//assert(OutPipelines.size() == PSO_Test);

		AddGraphicsPipeline(PSO_Render, psoDesc, "render.hlsl.vs.cso", "render.hlsl.ps.cso");
	}

	// PSO_Present8bit
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		//PSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleMask = UINT32_MAX;
		psoDesc.SampleDesc.Count = 1;
		//assert(OutPipelines.size() == PSO_Test);
		AddGraphicsPipeline(PSO_Present8bit, psoDesc, "present.hlsl.vs.cso", "present.hlsl.ps.cso");
	}

	// PSO_Spherical2Cube
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		//PSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleMask = UINT32_MAX;
		psoDesc.SampleDesc.Count = 1;
		//assert(OutPipelines.size() == PSO_Test);
		AddGraphicsPipeline(PSO_Spherical2Cube, psoDesc, "spherical2Cube.hlsl.vs.cso", "spherical2Cube.hlsl.ps.cso");
	}

	// PSO_SampleEnvMap
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.MultisampleEnable = MSAA_COUNT > 1 ? TRUE : FALSE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		//psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		//psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = HDR_FORMAT;
		psoDesc.SampleMask = UINT32_MAX;
		//psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Count = MSAA_COUNT;
		psoDesc.SampleDesc.Quality = MSAA_QUALITY;
		//EA_ASSERT(OutPipelines.size() == PSO_SampleEnvMap);
		AddGraphicsPipeline(PSO_SampleEnvMap, psoDesc, "sampleEnvMap.hlsl.vs.cso", "sampleEnvMap.hlsl.ps.cso");
	}

	// PSO_CreateIrradianceMap
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		//psoDesc.RasterizerState.MultisampleEnable = NumSamples > 1 ? TRUE : FALSE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		//psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleMask = UINT32_MAX;
		psoDesc.SampleDesc.Count = 1;
		//EA_ASSERT(OutPipelines.size() == PSO_SampleEnvMap);
		AddGraphicsPipeline(PSO_CreateIrradianceMap, psoDesc, "createIrradianceMap.hlsl.vs.cso", "createIrradianceMap.hlsl.ps.cso");
	}

	// PSO_PrefilterEnvMap
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		//psoDesc.RasterizerState.MultisampleEnable = NumSamples > 1 ? TRUE : FALSE;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		//psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.SampleMask = UINT32_MAX;
		psoDesc.SampleDesc.Count = 1;
		//EA_ASSERT(OutPipelines.size() == PSO_SampleEnvMap);
		AddGraphicsPipeline(PSO_PrefilterEnvMap, psoDesc, "prefilterEnvMap.hlsl.vs.cso", "prefilterEnvMap.hlsl.ps.cso");
	}

	// PSO_CreateBRDFMap
	{
		AddComputePipeline(PSO_CreateBRDFMap, "createBRDFMap.hlsl.cs.cso");
	}

	// PSO_GenerateMips
	{
		AddComputePipeline(PSO_GenerateMips, "generateMipmaps.hlsl.cs.cso");
	}

	// PSO_Thresholding
	{
		AddComputePipeline(PSO_Thresholding, "thresholding.hlsl.cs.cso");
	}

	// PSO_UpsampleBlend
	{
		AddComputePipeline(PSO_UpsampleBlend, "upsampleBlend.hlsl.cs.cso");
	}

	// PSO_Filter2DSeparable
	{
		AddComputePipeline(PSO_Filter2DSeparable, "filter2DSeparable.hlsl.cs.cso");
	}
}

// Load the sample assets.
void D3D12Engine::LoadAssets()
{
	//ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
	//ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

	// /*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*/
	// Meshes
	m_presentTriangle.Load(PresentVertices, PresentIndicies);
	m_presentTriangle.CreateConstants(m_HH);
	m_presentTriangle.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
	m_presentTriangle.ReleaseCPUData();

	m_cube.Load(CubeVertices, CubeIndicies);
	m_cube.CreateConstants(m_HH);
	m_cube.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
	m_cube.ReleaseCPUData();

	m_cubeInsideFacing.Load(CubeInVertices, CubeInIndicies);
	m_cubeInsideFacing.CreateConstants(m_HH);
	m_cubeInsideFacing.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
	m_cubeInsideFacing.ReleaseCPUData();

	//SMesh floor;
	//floor.Load(SquareVertices, SquareIndicies);
	//floor.CreateConstants(m_HH);
	//floor.MoveTo(XMFLOAT3(0.0f, -1.0f, 0.0f));
	//floor.RotateBy(XMFLOAT3(-1.0f * XM_PI / 2.0f, 0.0f, 0.0f));
	//floor.SetScale(20.0f);
	//floor.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
	//floor.ReleaseCPUData();
	//m_meshes.push_back(floor);

	// /*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*/
	// Textures

	//std::vector<std::string> textureNames = {
	//	"coast_sand_rocks_02",
	//	"plastered_wall_04",
	//	"square_tiles_03",
	//	"wood_table_001",
	//};

	std::vector<std::string> textureNames = {
		"coast_sand_rocks_02",
		"square_tiles_03",
		"metal",
		"smooth_albedo",
	};

	//std::vector<std::string> textureNames = {
	//	"glow",
	//};

	for (std::string s : textureNames)
	{
		STexture t;
		t.AddTexture("resources/" + s + "/textures/" + s + "_diff_1k.jpg");
		t.AddTexture("resources/" + s + "/textures/" + s + "_nor_dx_1k.exr");
		t.AddTexture("resources/" + s + "/textures/" + s + "_arm_1k.exr");
		t.AddTexture("resources/" + s + "/textures/" + s + "_emission_1k.jpg");
		m_textures.push_back(t);
	}

	for (uint32_t i = 0; i < m_textures.size(); ++i)
	{
		STexture& t = m_textures[i];

		// Load texture
		t.LoadTextures();
		t.CopyToUploadHeap(m_device.Get(), m_commandList.Get(), m_HH);
		t.ReleaseCPUData();
		for (uint32_t i = 0; i < t.size(); ++i)
		{
			auto texRes = t.GetTextureResource(i);
			auto texSRV = t.GetSRV(i);
			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				texRes.Get(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			GenerateMips(texRes, texSRV, (uint16_t)-1);
			m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				texRes.Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		}

		// Create sphere model for the texture
		SMesh sphere;
		sphere.Load("resources/meshes/Sphere.obj");
		sphere.GenerateTangents();
		sphere.CreateConstants(m_HH);
		sphere.MoveTo(XMFLOAT3(-1.0f * m_textures.size() / 2.0f + i * 1.0f, 0.0f, 0.0f));
		sphere.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
		sphere.ReleaseCPUData();
		m_meshes.push_back(sphere);
	}
}

void D3D12Engine::LoadIBL(const char* filename)
{
	m_sphericalTexture.AddTexture(filename);
	m_sphericalTexture.LoadTextures();
	m_sphericalTexture.CopyToUploadHeap(m_device.Get(), m_commandList.Get(), m_HH);
	m_sphericalTexture.ReleaseCPUData();

	const uint32_t resolution_envMap = 2048;
	const uint32_t resolution_irradianceMap = 256;
	const uint32_t resolution_prefilteredEnvMap = 256;
	const uint32_t resolution_BRDFMap = 256;

	D3D12_CPU_DESCRIPTOR_HANDLE SRV_envMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_irradianceMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_prefilteredEnvMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_BRDFMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

	// \=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/ +
	// Environment map
	// /=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\ +
	{
		uint32_t mipLevels = 9;
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			resolution_envMap,
			resolution_envMap,
			6,  // ArraySize (Cubemap has 6 faces)
			mipLevels,  // 1024, 512, 256, 128, 64, 32, 16, 8
			1,  // SampleCount
			0,  // SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		CD3DX12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		memcpy(clearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue,
			IID_PPV_ARGS(&m_envMap)));
		m_envMap->SetName(L"Environment Map");

		// As shader resource:
		// SRV for m_envMap
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = mipLevels;
		m_device->CreateShaderResourceView(m_envMap.Get(), &SRVDesc, SRV_envMap);
		m_SRV_envMap = m_HH.CopyDescriptorsToGPUHeap(1, SRV_envMap);

		// As render target:
		// RTV for six faces
		D3D12_CPU_DESCRIPTOR_HANDLE RTVs = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6);
		D3D12_CPU_DESCRIPTOR_HANDLE currentRTV = RTVs;
		for (uint32_t i = 0; i < 6; ++i)
		{
			D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
			RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			RTVDesc.Texture2DArray.MipSlice = 0;  // render to mip level 0
			RTVDesc.Texture2DArray.ArraySize = 1;
			RTVDesc.Texture2DArray.FirstArraySlice = i;
			m_device->CreateRenderTargetView(m_envMap.Get(), &RTVDesc, currentRTV);
			currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
		}

		D3D12_GPU_VIRTUAL_ADDRESS constMatrices_GPUAddr;
		auto* constMatrices_CPUAddr = (CameraConstants*)m_HH.AllocateGPUMemory(6 * sizeof(CameraConstants), constMatrices_GPUAddr);

		// Render to the cube map
		m_commandList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(resolution_envMap), static_cast<float>(resolution_envMap)));
		m_commandList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, static_cast<LONG>(resolution_envMap), static_cast<LONG>(resolution_envMap)));
		m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_Spherical2Cube].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_Spherical2Cube].Get());
		m_HH.BindDescriptorHeaps(m_commandList.Get());

		// Render six times
		currentRTV = RTVs;
		for (uint32_t i = 0; i < 6; ++i)
		{
			m_commandList->OMSetRenderTargets(1, &currentRTV, TRUE, nullptr);
			m_commandList->ClearRenderTargetView(currentRTV, CLEAR_COLOR, 0, nullptr);

			XMStoreFloat4x4(&constMatrices_CPUAddr->view, CubeViewTransforms[i]);
			XMStoreFloat4x4(&constMatrices_CPUAddr->projection, CubeProjectionTransform);

			// spherical2Cube.hlsl
			m_commandList->SetGraphicsRootConstantBufferView(0, constMatrices_GPUAddr);
			m_commandList->SetGraphicsRootDescriptorTable(1, m_sphericalTexture.GetCombinedSRV());
			m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

			currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
			constMatrices_GPUAddr += sizeof(CameraConstants);
			constMatrices_CPUAddr++;
		}

		// Generate mipmaps for environment map
		// Transition envmap to NON_PIXEL_SHADER_RESOURCE to enable mipmap generation
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_envMap.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

		// We need to create a SRV that treats the cube map as an array of 2D textures
		D3D12_SHADER_RESOURCE_VIEW_DESC arraySRVDesc = {};
		arraySRVDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		arraySRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		arraySRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		arraySRVDesc.Texture2DArray.ArraySize = 6;
		arraySRVDesc.Texture2DArray.MipLevels = mipLevels;

		D3D12_CPU_DESCRIPTOR_HANDLE arraySRVCPU = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
		m_device->CreateShaderResourceView(m_envMap.Get(), &arraySRVDesc, arraySRVCPU);
		D3D12_GPU_DESCRIPTOR_HANDLE arraySRVGPU = m_HH.CopyDescriptorsToGPUHeap(1, arraySRVCPU);

		// It is inaccurate to generate mipmaps from a baked cubemap.
		// We expect mipmaps are generated by averaging on the entire
		// environment map. But the mipmap generation is performed on
		// each face separately.
		GenerateMips(m_envMap, arraySRVGPU, (uint16_t)-1);

		// Transition the cube map to a shader resource for further processing
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_envMap.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// \=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/ +
	// Irradiance Map
	// /=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\ +
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			resolution_irradianceMap,
			resolution_irradianceMap,
			6,  // ArraySize (Cubemap has 6 faces)
			1,  // MipLevels
			1,  // SampleCount
			0,  // SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET  // Allow render target
		);

		CD3DX12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		memcpy(clearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue,
			IID_PPV_ARGS(&m_irradianceMap)));
		m_irradianceMap->SetName(L"Irradiance Map");

		// Shader resource
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = (uint32_t)-1;
		m_device->CreateShaderResourceView(m_irradianceMap.Get(), &SRVDesc, SRV_irradianceMap);
		m_SRV_irradianceMap = m_HH.CopyDescriptorsToGPUHeap(1, SRV_irradianceMap);

		// RTV for six faces
		const D3D12_CPU_DESCRIPTOR_HANDLE RTVs = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6);
		D3D12_CPU_DESCRIPTOR_HANDLE currentRTV = RTVs;
		for (uint32_t i = 0; i < 6; ++i)
		{
			D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
			RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			RTVDesc.Texture2DArray.ArraySize = 1;
			RTVDesc.Texture2DArray.FirstArraySlice = i;
			m_device->CreateRenderTargetView(m_irradianceMap.Get(), &RTVDesc, currentRTV);
			currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
		}

		D3D12_GPU_VIRTUAL_ADDRESS constMatrices_GPUAddr;
		auto* constMatrices_CPUAddr = (CameraConstants*)m_HH.AllocateGPUMemory(6 * sizeof(CameraConstants), constMatrices_GPUAddr);

		m_commandList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(resolution_irradianceMap), static_cast<float>(resolution_irradianceMap)));
		m_commandList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, static_cast<LONG>(resolution_irradianceMap), static_cast<LONG>(resolution_irradianceMap)));
		m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_CreateIrradianceMap].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_CreateIrradianceMap].Get());
		m_HH.BindDescriptorHeaps(m_commandList.Get());

		// Render six times
		currentRTV = RTVs;
		for (uint32_t i = 0; i < 6; ++i)
		{
			m_commandList->OMSetRenderTargets(1, &currentRTV, TRUE, nullptr);
			m_commandList->ClearRenderTargetView(currentRTV, CLEAR_COLOR, 0, nullptr);

			XMStoreFloat4x4(&constMatrices_CPUAddr->view, CubeViewTransforms[i]);
			XMStoreFloat4x4(&constMatrices_CPUAddr->projection, CubeProjectionTransform);

			// createIrradianceMap.hlsl
			m_commandList->SetGraphicsRootConstantBufferView(0, constMatrices_GPUAddr);
			m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_envMap);
			m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

			currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
			constMatrices_GPUAddr += sizeof(CameraConstants);
			constMatrices_CPUAddr++;
		}

		// Transition the cube map to a shader resource
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_irradianceMap.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// \=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/ +
	// Pre-filtered Environment Map
	// /=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\ +
	{
		uint32_t n_mipLevels = 6;  // 256, 128, 64, 32, 16, 8
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			resolution_prefilteredEnvMap,
			resolution_prefilteredEnvMap,
			6,  // ArraySize (Cubemap has 6 faces)
			n_mipLevels,
			1,  // SampleCount
			0,  // SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET  // Allow render target
		);

		CD3DX12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		memcpy(clearValue.Color, CLEAR_COLOR, sizeof(CLEAR_COLOR));

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue,
			IID_PPV_ARGS(&m_prefilteredEnvMap)));
		m_prefilteredEnvMap->SetName(L"Pre-filtered Environment Map");

		// Shader resource
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = n_mipLevels;
		m_device->CreateShaderResourceView(m_prefilteredEnvMap.Get(), &SRVDesc, SRV_prefilteredEnvMap);
		m_SRV_prefilteredEnvMap = m_HH.CopyDescriptorsToGPUHeap(1, SRV_prefilteredEnvMap);

		// RTV for six faces
		const D3D12_CPU_DESCRIPTOR_HANDLE RTVs = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6 * n_mipLevels);
		D3D12_CPU_DESCRIPTOR_HANDLE currentRTV = RTVs;
		for (uint32_t imip = 0; imip < n_mipLevels; ++imip)  // mip level
		{
			for (uint32_t iface = 0; iface < 6; ++iface)  // face
			{
				D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
				RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				RTVDesc.Texture2DArray.ArraySize = 1;
				RTVDesc.Texture2DArray.FirstArraySlice = iface;
				RTVDesc.Texture2DArray.MipSlice = imip;
				m_device->CreateRenderTargetView(m_prefilteredEnvMap.Get(), &RTVDesc, currentRTV);
				currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
			}
		}

		D3D12_GPU_VIRTUAL_ADDRESS constMatrices_GPUAddr;
		D3D12_GPU_VIRTUAL_ADDRESS prefilterConstants_GPUAddr;
		auto* constMatrices_CPUAddr = (CameraConstants*)m_HH.AllocateGPUMemory(6 * n_mipLevels * sizeof(CameraConstants), constMatrices_GPUAddr);
		auto* prefilterConstants_CPUAddr = (PrefilterConstants*)m_HH.AllocateGPUMemory(n_mipLevels * sizeof(PrefilterConstants), prefilterConstants_GPUAddr);

		// Render
		m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_PrefilterEnvMap].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_PrefilterEnvMap].Get());
		m_HH.BindDescriptorHeaps(m_commandList.Get());

		currentRTV = RTVs;
		uint32_t currentResolution = resolution_prefilteredEnvMap;
		for (uint32_t imip = 0; imip < n_mipLevels; ++imip)  // mip level
		{
			m_commandList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(currentResolution), static_cast<float>(currentResolution)));
			m_commandList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, static_cast<LONG>(currentResolution), static_cast<LONG>(currentResolution)));
			prefilterConstants_CPUAddr->roughness = (float)imip / (n_mipLevels - 1);

			for (uint32_t iface = 0; iface < 6; ++iface)  // face
			{
				m_commandList->OMSetRenderTargets(1, &currentRTV, TRUE, nullptr);
				m_commandList->ClearRenderTargetView(currentRTV, CLEAR_COLOR, 0, nullptr);

				XMStoreFloat4x4(&constMatrices_CPUAddr->view, CubeViewTransforms[iface]);
				XMStoreFloat4x4(&constMatrices_CPUAddr->projection, CubeProjectionTransform);

				// prefilterEnvMap.hlsl
				m_commandList->SetGraphicsRootConstantBufferView(0, constMatrices_GPUAddr);
				m_commandList->SetGraphicsRootConstantBufferView(1, prefilterConstants_GPUAddr);
				m_commandList->SetGraphicsRootDescriptorTable(2, m_SRV_envMap);
				m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

				currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
				constMatrices_GPUAddr += sizeof(CameraConstants);
				constMatrices_CPUAddr++;
			}

			prefilterConstants_GPUAddr += sizeof(PrefilterConstants);
			prefilterConstants_CPUAddr++;
			currentResolution /= 2;
		}

		// Transition to shader resource
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_prefilteredEnvMap.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// \=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/ +
	// Integrated BRDF Map
	// /=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\ +
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16_FLOAT,
			resolution_BRDFMap,
			resolution_BRDFMap,
			1,  // ArraySize
			1,  // MipLevels
			1,  // SampleCount
			0,  // SampleQuality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_BRDFMap)));
		m_BRDFMap->SetName(L"BRDF Integration Map");

		// Shader resource
		m_device->CreateShaderResourceView(m_BRDFMap.Get(), nullptr, SRV_BRDFMap);
		m_SRV_BRDFMap = m_HH.CopyDescriptorsToGPUHeap(1, SRV_BRDFMap);

		const D3D12_CPU_DESCRIPTOR_HANDLE UAV = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
		m_device->CreateUnorderedAccessView(m_BRDFMap.Get(), nullptr, nullptr, UAV);
		D3D12_GPU_DESCRIPTOR_HANDLE UAV_GPU = m_HH.CopyDescriptorsToGPUHeap(1, UAV);

		// Create BRDFIntegrationMap.
		m_commandList->SetComputeRootSignature(m_rootSignatures[PSO_CreateBRDFMap].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_CreateBRDFMap].Get());
		m_commandList->SetComputeRootDescriptorTable(0, UAV_GPU);
		m_commandList->Dispatch(resolution_BRDFMap / 8, resolution_BRDFMap / 8, 1);

		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_BRDFMap.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	// IBL textures descriptor
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle;
	m_HH.AllocateGPUDescriptors(3, CPUHandle, m_SRV_IBL);
	//m_device->CopyDescriptorsSimple(1, CPUHandle, SRV_envMap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//CPUHandle.Offset(1, m_HH.GetDescriptorSizeCBV_SRV_UAV());
	m_device->CopyDescriptorsSimple(1, CPUHandle, SRV_irradianceMap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CPUHandle.Offset(1, m_HH.GetDescriptorSizeCBV_SRV_UAV());
	m_device->CopyDescriptorsSimple(1, CPUHandle, SRV_prefilteredEnvMap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CPUHandle.Offset(1, m_HH.GetDescriptorSizeCBV_SRV_UAV());
	m_device->CopyDescriptorsSimple(1, CPUHandle, SRV_BRDFMap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// This function expect the texture to be in NON_PIXEL_RESOURCE state.
void D3D12Engine::GenerateMips(ComPtr<ID3D12Resource>& texture, D3D12_GPU_DESCRIPTOR_HANDLE srv, uint16_t mipLevels)
{
	auto resourceDesc = texture->GetDesc();

	// If the texture only has a single mip level (level 0)
	// or the specified mipLevels is 0, then there is no need to generate mips.
	if (resourceDesc.MipLevels == 1 || mipLevels == 0) return;

	uint16_t nMipsToGen = std::min<uint16_t>(resourceDesc.MipLevels - 1, mipLevels);

	// Currently, only non-multi-sampled 2D textures are supported.
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		resourceDesc.SampleDesc.Count > 1)
	{
		throw std::exception("GenerateMips is only supported for non-multi-sampled 2D Textures.");
	}

	// Check for support of the requested format for generating mips
	int sRGB = 0;
	DXGI_FORMAT compatibleFormat = GetCompatableFormat(resourceDesc.Format, sRGB);
	if (compatibleFormat == DXGI_FORMAT_UNKNOWN)
	{
		throw std::exception("Resource format not supported.");
	}

	uint32_t indexFormat = 0;
	while (indexFormat < NUM_MIP_FORMATS)
	{
		if (compatibleFormat == MIP_FORMATS[indexFormat])
		{
			break;
		}
		++indexFormat;
	}
	assert(indexFormat < NUM_MIP_FORMATS);
	uint32_t tempOffset = indexFormat * NUM_MIPS_PER_PASS;

	// Set the root signature and pipeline state
	m_commandList->SetComputeRootSignature(m_rootSignatures[PSO_GenerateMips].Get());
	m_commandList->SetPipelineState(m_pipelineStates[PSO_GenerateMips].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());

	bool isTextureArray = (resourceDesc.DepthOrArraySize > 1);

	// Support for cubemap
	for (uint32_t ArraySliceIdx = 0; ArraySliceIdx < resourceDesc.DepthOrArraySize; ++ArraySliceIdx)
	{
		// When generating more than 4 mips, we should use loop:
		// 1. Generate 4 mips at a time and then increment srcMip by 4.
		// 2. The next loop generates the following 4 mips using srcMip = 4.
		// 3. If less than 4 mips are remaining then set the value constants->NumMipLevels
		//    to the number of remaining mips. Pad unused UAVs with dummy ones.
		// 4. The procedure is repeated until all mips are generated.
		// Ref: https://www.3dgep.com/learning-directx-12-4/

		// The source mip level in current loop
		uint16_t srcMip = 0;

		while (srcMip < nMipsToGen)
		{
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			// Compute [mipCount], [dstWidth], [dstHeight], etc.
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			GenerateMipsConstants constants;

			// Note the dstWidth and dstHeight indicate the size of halved src dimensions (the 1st generated mip),
			// not the size of the smallest mip in this pass.
			uint64_t srcWidth = resourceDesc.Width >> srcMip;
			uint32_t srcHeight = resourceDesc.Height >> srcMip;
			uint32_t dstWidth = static_cast<uint32_t>(srcWidth >> 1);
			uint32_t dstHeight = srcHeight >> 1;

			// 0b00(0): Both width and height are even.
			// 0b01(1): Width is odd, height is even.
			// 0b10(2): Width is even, height is odd.
			// 0b11(3): Both width and height are odd.
			constants.SrcDimension = (srcHeight & 1) << 1 | (srcWidth & 1);

			// How many mipmap levels to compute this pass (max 4 mips per pass)
			DWORD mipCount;

			// The number of times we can half the size of the texture and get
			// exactly a 50% reduction in size.
			// A 1 bit in the width or height indicates an odd dimension.
			// The case where either the width or the height is exactly 1 is handled
			// as a special case (as the dimension does not require reduction).
			_BitScanForward(&mipCount, (dstWidth == 1 ? dstHeight : dstWidth) |
				(dstHeight == 1 ? dstWidth : dstHeight));

			// Maximum number of mips to generate is 4.
			mipCount = std::min<DWORD>(NUM_MIPS_PER_PASS, mipCount + 1);
			// Clamp to total number of mips left over.
			mipCount = (srcMip + mipCount) >= resourceDesc.MipLevels ?
				resourceDesc.MipLevels - srcMip - 1 : mipCount;

			// Dimensions should not reduce to 0.
			// This can happen if the width and height are not the same.
			dstWidth = std::max<DWORD>(1, dstWidth);
			dstHeight = std::max<DWORD>(1, dstHeight);

			// We assume linear space texture
			constants.IsSRGB = sRGB;
			constants.SrcMipLevel = srcMip;
			constants.NumMipLevels = mipCount;
			constants.TexelSize.x = 1.0f / (float)dstWidth;
			constants.TexelSize.y = 1.0f / (float)dstHeight;
			constants.ArraySlice = ArraySliceIdx;
			
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			// Dispatch the compute shader
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			m_commandList->SetComputeRoot32BitConstants(0, 7, &constants, 0);
			m_commandList->SetComputeRootDescriptorTable(1, srv);
			m_commandList->SetComputeRootDescriptorTable(2, m_UAV_mipTemps[indexFormat]);

			// As we want to despatch at least 1 thread per group,
			// we use res = (a + (b - 1)) / b to round up division results.
			m_commandList->Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);

			// TODO:
			// If more than 4 mips needed to be generated,
			// use barrier here to wait for the completion of the previous dispatch.
			D3D12_RESOURCE_BARRIER uavBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::UAV(m_mipTemps[tempOffset + 0].Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(m_mipTemps[tempOffset + 1].Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(m_mipTemps[tempOffset + 2].Get()),
				CD3DX12_RESOURCE_BARRIER::UAV(m_mipTemps[tempOffset + 3].Get()),
			};
			m_commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			// Copy the generated mips to the texture
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			{
				const CD3DX12_RESOURCE_BARRIER barriers[5] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 2].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 3].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
				};
				m_commandList->ResourceBarrier(_countof(barriers), barriers);
			}

			for (uint32_t i = 0; i < mipCount; ++i)
			{
				const auto src = CD3DX12_TEXTURE_COPY_LOCATION(m_mipTemps[tempOffset + i].Get(), 0);
				const auto dest = CD3DX12_TEXTURE_COPY_LOCATION(texture.Get(), i + 1 + srcMip + ArraySliceIdx * resourceDesc.MipLevels);
				const auto box = CD3DX12_BOX(0, 0, 0, std::max<uint32_t>(dstWidth >> i, 1), std::max<uint32_t>(dstHeight >> i, 1), 1);
				m_commandList->CopyTextureRegion(&dest, 0, 0, 0, &src, &box);
			}

			{
				const CD3DX12_RESOURCE_BARRIER barriers[5] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 0].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 1].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 2].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_mipTemps[tempOffset + 3].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				m_commandList->ResourceBarrier(_countof(barriers), barriers);
			}

			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			// Set the source mip level for the next loop
			// +-----+-----+-----+-----+-----+-----+-----+-----+-----+
			srcMip += mipCount;
		}

	}

}

void D3D12Engine::BloomEffect(ComPtr<ID3D12Resource>& cascade, D3D12_GPU_DESCRIPTOR_HANDLE cascadeSRV, uint32_t blurTargetID, uint32_t blendTargetID, uint16_t mipLevels)
{
	//
	// -*- Bloom Effect -*-
	// Ref: http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
	//      https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
	// We ignore the blur pass in downsampling and adopt generate mips to do it instead.
	// 
	// We use 2 post-processing buffers to do this.
	// Workflow for a single pass:
	//		Blur -> Upsample -> Combine
	// 
	// Args:
	//		[cascade] - resource containing the mipmap of bright areas.
	//		[cascadeSRV] - SRV for cascade.
	//		[blurTargetID], [blendTargetID] - indices of 2 empty post-processing buffers.
	//		[mipLevels] - how many mips used in the cascade (because all levels are generated and we only wnat to use some of them).
	//

	// Blur kernel
	float kernelWeights[3] = { 0.25f, 0.50f, 0.25f };
	for (uint32_t i = 0; i < sizeof(kernelWeights) / sizeof(float); ++i)
	{
		m_blurKernel->w[i * 4] = kernelWeights[i];
	}

	auto resourceDesc = cascade->GetDesc();
	uint32_t nMips = std::min<uint16_t>(resourceDesc.MipLevels - 1, mipLevels);

	bool firstLoop = true;
	for (int8_t i = nMips - 1; i >= 0; --i)
	{
		uint32_t dstWidth = resourceDesc.Width >> i;
		uint32_t dstHeight = resourceDesc.Height >> i;
		uint32_t srcWidth = dstWidth >> 1;
		uint32_t srcHeight = dstHeight >> 1;

		D3D12_GPU_DESCRIPTOR_HANDLE blurSource;
		BlurConstants blurConstants;

		if (firstLoop)
		{
			blurSource = cascadeSRV;
			blurConstants.srcMipLevel = i + 1;
		}
		else
		{
			blurSource = m_SRV_ppBuffers[blendTargetID];
			blurConstants.srcMipLevel = 0;
		}

		//
		// Initial states:
		// cascade     - NON_PIXEL_SHADER_RESOURCE
		// blurTarget  - UNORDERED_ACCESS
		// blendTarget - UNORDERED_ACCESS
		//
		// States after the first loop:
		// cascade     - NON_PIXEL_SHADER_RESOURCE
		// blurTarget  - UNORDERED_ACCESS
		// blendTarget - NON_PIXEL_SHADER_RESOURCE
		//

		// Blur
		blurConstants.blurRadius = 1;
		blurConstants.srcWidth = srcWidth;
		blurConstants.srcHeight = srcHeight;
		m_commandList->SetComputeRootSignature(m_rootSignatures[PSO_Filter2DSeparable].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_Filter2DSeparable].Get());
		m_HH.BindDescriptorHeaps(m_commandList.Get());
		m_commandList->SetComputeRoot32BitConstants(0, 4, &blurConstants, 0);
		m_commandList->SetComputeRootConstantBufferView(1, m_blurKernel_GPUAddr);
		m_commandList->SetComputeRootDescriptorTable(2, blurSource);
		m_commandList->SetComputeRootDescriptorTable(3, m_UAV_ppBuffers[blurTargetID]);

		uint32_t threadGroupCountX = (srcWidth + FILTER_N_THREADS - 1) / FILTER_N_THREADS;
		uint32_t threadGroupCountY = (srcHeight + FILTER_N_THREADS - 1) / FILTER_N_THREADS;
		m_commandList->Dispatch(threadGroupCountX, threadGroupCountY, 1);


		// Wait for completion of the blur pass
		if (firstLoop)
		{
			const CD3DX12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::UAV(m_ppBuffers[blurTargetID].Get()),
				CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[blurTargetID].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			m_commandList->ResourceBarrier(_countof(barriers), barriers);
		}
		else
		{
			const CD3DX12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::UAV(m_ppBuffers[blurTargetID].Get()),
				CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[blurTargetID].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[blendTargetID].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			m_commandList->ResourceBarrier(_countof(barriers), barriers);
		}

		// Blend
		BlendParams blendParams;
		blendParams.mipLevel = i;
		blendParams.blendFactor = 0.7f;
		blendParams.targetWidth = dstWidth;
		blendParams.targetHeight = dstHeight;
		blendParams.uvScale.x = 1.0f / (float)(dstWidth << (i + 1));  // See upsampleBlend.hlsl for details.
		blendParams.uvScale.y = 1.0f / (float)(dstHeight << (i + 1));

		m_commandList->SetComputeRootSignature(m_rootSignatures[PSO_UpsampleBlend].Get());
		m_commandList->SetPipelineState(m_pipelineStates[PSO_UpsampleBlend].Get());
		m_HH.BindDescriptorHeaps(m_commandList.Get());
		m_commandList->SetComputeRoot32BitConstants(0, 6, &blendParams, 0);
		m_commandList->SetComputeRootDescriptorTable(1, cascadeSRV);
		m_commandList->SetComputeRootDescriptorTable(2, m_SRV_ppBuffers[blurTargetID]);
		m_commandList->SetComputeRootDescriptorTable(3, m_UAV_ppBuffers[blendTargetID]);
		m_commandList->Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);

		// Wait for completion of the blend pass
		{
			const CD3DX12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::UAV(m_ppBuffers[blendTargetID].Get()),
				CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[blurTargetID].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[blendTargetID].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			m_commandList->ResourceBarrier(_countof(barriers), barriers);
		}

		firstLoop = false;
	}

}

// Update frame-based values.
void D3D12Engine::OnUpdate()
{
	m_timer.Tick();
	float elapsedTime = static_cast<float>(m_timer.GetElapsedSeconds());
	// #DXR Extra: Perspective Camera
	UpdateCameraBuffer(elapsedTime);
	RotateObject(elapsedTime);
}

// Render the scene.
void D3D12Engine::OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

	CD3DX12_VIEWPORT viewportSSAA(0.0f, 0.0f, static_cast<float>(m_widthSSAA), static_cast<float>(m_heightSSAA));
	CD3DX12_RECT scissorRectSSAA(0, 0, static_cast<LONG>(m_widthSSAA), static_cast<LONG>(m_heightSSAA));
	m_commandList->RSSetViewports(1, &viewportSSAA);
	m_commandList->RSSetScissorRects(1, &scissorRectSSAA);

	// Set MSAA render target.
	m_commandList->OMSetRenderTargets(1, &m_RTV_msaaRenderTarget, FALSE, &m_DSV_depthStencilBuffer);
	m_commandList->ClearRenderTargetView(m_RTV_msaaRenderTarget, CLEAR_COLOR, 0, nullptr);
	m_commandList->ClearDepthStencilView(m_DSV_depthStencilBuffer, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Render environment map
	// 
	// Shader file(s):
	//		sampleEnvMap.hlsl
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	m_commandList->SetPipelineState(m_pipelineStates[PSO_SampleEnvMap].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_SampleEnvMap].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraConstants_GPUAddr);
	m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_envMap);
	//m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_irradianceMap);
	m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Render objects
	// 
	// Shader file(s):
	//		render.hlsl
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	m_commandList->SetPipelineState(m_pipelineStates[PSO_Render].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_Render].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraConstants_GPUAddr);
	m_commandList->SetGraphicsRootConstantBufferView(2, m_pbrConstants_GPUAddr);
	m_commandList->SetGraphicsRootDescriptorTable(3, m_SRV_IBL);

	for (uint32_t i = 0; i < m_meshes.size(); ++i)
	{
		m_commandList->SetGraphicsRootConstantBufferView(1, m_meshes[i].GetGPUAddr());
		m_commandList->SetGraphicsRootDescriptorTable(4, m_textures[i].GetCombinedSRV());
		m_meshes[i].ScheduleDraw(m_commandList.Get());
	}

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Resolve MSAA buffer to intermediate HDR texture
	// 
	// Shader file(s):
	//		None
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			// transition msaa texture from target to resolve source
			CD3DX12_RESOURCE_BARRIER::Transition(m_msaaRenderTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
		};
		m_commandList->ResourceBarrier(_countof(barriers), barriers);
	}

	m_commandList->ResolveSubresource(m_hdrResolveTarget.Get(), 0, m_msaaRenderTarget.Get(), 0, HDR_FORMAT);

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Post-processing
	// Note that tone-mapping is not processed here.
	// 
	// Shader file(s):
	//		thresholding.hlsl
	//	    bloomEffect.hlsl
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_hdrResolveTarget.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		m_commandList->ResourceBarrier(_countof(barriers), barriers);
	}

	// 1. Apply thresholding to select bright pixels
	//    Render to post-processing buffer 0
	m_commandList->SetComputeRootSignature(m_rootSignatures[PSO_Thresholding].Get());
	m_commandList->SetPipelineState(m_pipelineStates[PSO_Thresholding].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetComputeRootDescriptorTable(0, m_SRV_hdrResolveTarget);
	m_commandList->SetComputeRootDescriptorTable(1, m_UAV_ppBuffers[0]);
	m_commandList->Dispatch(m_widthSSAA / 8, m_heightSSAA / 8, 1);

	// 2. Generate mipmaps for post-processing buffer 0
	//    The function expects the texture to be in NON_PIXEL_SHADER_RESOURCE state.
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		m_commandList->ResourceBarrier(_countof(barriers), barriers);
	}

	uint32_t mipsUsed = 10;
	uint32_t indexBlurTarget = 1;
	uint32_t indexBlendTarget = 2;
	GenerateMips(m_ppBuffers[0], m_SRV_ppBuffers[0], mipsUsed);

	// 3. Blur, upsample, and blend the mipmap cascade
	//	  Post-processing buffer 0 as the cascade buffer.
	//    Buffer 1 and 2 as the blur and blend target.

	// Clear blur target because result form last frame is still there.
	float fill_value = 0.0f;
	auto Values = std::array<UINT, 4>{ *((UINT*)&fill_value),0,0,0 };
	m_commandList->ClearUnorderedAccessViewUint(
		m_UAV_ppBuffers[indexBlurTarget],
		m_UAV_ppBuffers_CPU[indexBlurTarget],
		m_ppBuffers[indexBlurTarget].Get(),
		Values.data(),
		0,
		nullptr
	);

	BloomEffect(m_ppBuffers[0], m_SRV_ppBuffers[0], indexBlurTarget, indexBlendTarget, mipsUsed);

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Tone-mapping and prensent to back buffer.
	// 
	// Shader file(s):
	//		present.hlsl
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_hdrResolveTarget.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[indexBlendTarget].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
		};
		m_commandList->ResourceBarrier(_countof(barriers), barriers);
	}

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);
	m_commandList->OMSetRenderTargets(1, &m_RTV_frameBuffers[m_frameIndex], FALSE, nullptr);
	m_commandList->ClearRenderTargetView(m_RTV_frameBuffers[m_frameIndex], CLEAR_COLOR, 0, nullptr);

	ToneMapperParams tmparams;
	tmparams.toneMappingMode = ToneMappingMode_sRGB;
	tmparams.bloomIntensity = 0.3f;

	// Process the intermediate and draw into the swap chain render target.
	m_commandList->SetPipelineState(m_pipelineStates[PSO_Present8bit].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_Present8bit].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRoot32BitConstants(0, 2, &tmparams, 0);
	m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_hdrResolveTarget);
	m_commandList->SetGraphicsRootDescriptorTable(2, m_SRV_ppBuffers[indexBlendTarget]);
	m_presentTriangle.ScheduleDraw(m_commandList.Get());

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// 
	// Present the frame and reset buffer states for next frame.
	// 
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_msaaRenderTarget.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(m_hdrResolveTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_ppBuffers[indexBlendTarget].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
		};
		m_commandList->ResourceBarrier(_countof(barriers), barriers);
	}

	ThrowIfFailed(m_commandList->Close());

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
	
	auto r = m_device->GetDeviceRemovedReason();

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(0, 0));

	WaitForPreviousFrame();
}

void D3D12Engine::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);

	// We want to manually Unmap upload heaps.
	m_HH.Release();
}

void D3D12Engine::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12Engine::InitCamera()
{
	// Initialize camera position and orientation
	cameraPosition = { 2.68f, 0.48f, -1.13f };
	cameraForward = { 0.84f, 0.25f, -0.48f };
	cameraRight = { -0.50f, 0.00f, -0.86f };
	cameraYaw = 119.85f;
	cameraPitch = -14.50f;

	UpdateCameraBuffer(0.0f);
}

void D3D12Engine::UpdateCameraBuffer(float deltaTime) {

	// Directional movement based on key states
	XMVECTOR moveDir = XMVectorZero();
	if (keyStates['W']) moveDir -= XMLoadFloat3(&cameraForward);
	if (keyStates['S']) moveDir += XMLoadFloat3(&cameraForward);
	if (keyStates['A']) moveDir -= XMLoadFloat3(&cameraRight);
	if (keyStates['D']) moveDir += XMLoadFloat3(&cameraRight);

	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // World up (Y-axis)
	if (keyStates[' ']) moveDir += up;
	if (keyStates['Z']) moveDir -= up;

	// Normalize and apply speed/delta time
	moveDir = XMVector3Normalize(moveDir);
	moveDir *= CAMERA_SPEED * deltaTime;

	// Update camera position
	XMStoreFloat3(&cameraPosition, XMLoadFloat3(&cameraPosition) + moveDir);

	// Compute new orientation from yaw and pitch
	XMMATRIX rotation = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(cameraPitch),
		XMConvertToRadians(cameraYaw),
		0.0f
	);

	// Update forward and right vectors
	XMVECTOR newForward = XMVector3TransformNormal(
		XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), // Initial forward (Z-axis)
		rotation
	);
	XMStoreFloat3(&cameraForward, newForward);

	XMVECTOR newRight = XMVector3TransformNormal(
		XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), // Initial right (X-axis)
		rotation
	);
	XMStoreFloat3(&cameraRight, newRight);

	// View Matrix
	XMVECTOR pos = XMLoadFloat3(&cameraPosition);
	XMVECTOR dir = XMLoadFloat3(&cameraForward);
	XMMATRIX view = XMMatrixLookToLH(pos, dir, up);

	float fovAngleY = 45.0f * XM_PI / 180.0f;
	XMMATRIX projection = XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f);

	// Copy the matrix contents
	memcpy(&m_cameraConstants->view, &view, sizeof(XMMATRIX));
	memcpy(&m_cameraConstants->projection, &projection, sizeof(XMMATRIX));
	memcpy(&m_pbrConstants->eyePosition, &cameraPosition, sizeof(XMFLOAT3));
}

void D3D12Engine::RotateObject(float deltaTime)
{
	// Rotate the object
	static const float rotationRate = 0.02f;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		m_meshes[i].RotateBy(XMFLOAT3(0.0f, rotationRate * deltaTime, 0.0f));
	}
}

// #DXR Extra: Perspective Camera++
void D3D12Engine::OnKeyDown(UINT8 key)
{
	keyStates[key] = true;
}

void D3D12Engine::OnKeyUp(UINT8 key)
{
	keyStates[key] = false;
}

void D3D12Engine::OnButtonDown(UINT32 lParam)
{
	
}

void D3D12Engine::OnMouseMove(UINT8 wParam, UINT32 lParam)
{
	static int prevX = 0, prevY = 0;
	int currentX = GET_X_LPARAM(lParam);
	int currentY = GET_Y_LPARAM(lParam);

	float centerX = (float)m_scissorRect.right / 2.0f;
	float centerY = (float)m_scissorRect.bottom / 2.0f;

	float deltaX = currentX - centerX;
	float deltaY = currentY - centerY;

	// Update camera angles
	cameraYaw -= deltaX * CAMERA_SENSITIVITY;
	cameraPitch -= deltaY * CAMERA_SENSITIVITY;

	// Clamp pitch to [-89бу, 89бу]
	cameraPitch = std::clamp(cameraPitch, -89.0f, 89.0f);
}

void D3D12Engine::AddGraphicsPipeline(UINT32 PSOIndex, D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc, const char* VSName, const char* PSName)
{
	char VSPath[MAX_PATH], PSPath[MAX_PATH];

	sprintf(VSPath, "shaders/%s", VSName);
	vector<uint8_t> VSBytecode = LoadBytecodeFromFile(VSPath);

	sprintf(PSPath, "shaders/%s", PSName);
	vector<uint8_t> PSBytecode = LoadBytecodeFromFile(PSPath);

	ID3D12RootSignature* RootSignature;
	ThrowIfFailed(m_device->CreateRootSignature(0, VSBytecode.data(), VSBytecode.size(), IID_PPV_ARGS(&RootSignature)));

	PSODesc.pRootSignature = RootSignature;
	PSODesc.VS = { VSBytecode.data(), VSBytecode.size() };
	PSODesc.PS = { PSBytecode.data(), PSBytecode.size() };

	ID3D12PipelineState* Pipeline;
	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&Pipeline)));

	m_rootSignatures[PSOIndex] = RootSignature;
	m_pipelineStates[PSOIndex] = Pipeline;
}

void D3D12Engine::AddComputePipeline(UINT32 PSOIndex, const char* CSName)
{
	char Path[MAX_PATH];

	sprintf(Path, "shaders/%s", CSName);
	vector<uint8_t> CSBytecode = LoadBytecodeFromFile(Path);

	ID3D12RootSignature* RootSignature;
	ThrowIfFailed(m_device->CreateRootSignature(0, CSBytecode.data(), CSBytecode.size(), IID_PPV_ARGS(&RootSignature)));

	D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.pRootSignature = RootSignature;
	PSODesc.CS = { CSBytecode.data(), CSBytecode.size() };

	ID3D12PipelineState* Pipeline;
	ThrowIfFailed(m_device->CreateComputePipelineState(&PSODesc, IID_PPV_ARGS(&Pipeline)));

	m_rootSignatures[PSOIndex] = RootSignature;
	m_pipelineStates[PSOIndex] = Pipeline;
}
