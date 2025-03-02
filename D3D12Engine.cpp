#include "stdafx.h"
#include "D3D12Engine.h"

#include <algorithm>
#include <windowsx.h>

using namespace DirectX;
using std::vector;

D3D12Engine::D3D12Engine(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_cameraConstants(nullptr),
	m_pixelConstants(nullptr)
{
}

void D3D12Engine::OnInit()
{
	CreateContext();
	CreateFrameResources();
	CreateConstantBufferViews();
	CreatePipelines();

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

	// Load any assets here.
	LoadAssets();
	LoadIBL("resources/hdris/brown_photostudio_02_8k.exr");

	// Importance sampling is required for correctly rendering this scene.
	//LoadIBL("resources/hdris/je_gray_02_4k.exr");

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists2[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists2), ppCommandLists2);
	WaitForPreviousFrame();

	auto r = m_device->GetDeviceRemovedReason();

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
			//m_CPUHandle_frameBuffers[n].ptr += m_HH.GetDescriptorSizeRTV();
			//m_GPUHandle_frameBuffers[n] = m_HH.CopyDescriptorsToGPUHeap(1, m_CPUHandle_frameBuffers[n]);
		}
	}

	// Create MSAA render target.
	{
		// Create an MSAA render target.
		D3D12_RESOURCE_DESC msaaRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			HDR_FORMAT,
			m_width * SSAA_MULTIPLIER,
			m_height * SSAA_MULTIPLIER,
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
			m_width * SSAA_MULTIPLIER,
			m_height * SSAA_MULTIPLIER,
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
			m_width * SSAA_MULTIPLIER,
			m_height * SSAA_MULTIPLIER,
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

	// Create a command allocator for each frame.
	for (UINT n = 0; n < FRAME_COUNT; n++)
	{
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
	}
}

void D3D12Engine::CreateConstantBufferViews()
{
	// Camera Constants
	{
		UINT32 vertexCBSize = sizeof(CameraConstants);
		m_cameraConstants = (CameraConstants*)m_HH.AllocateGPUMemory(vertexCBSize, m_cameraConstants_GPUAddr);
	}

	// Pixel Shader Constants
	{
		UINT32 pixelCBSize = sizeof(PixelShaderConstants);
		m_pixelConstants = (PixelShaderConstants*)m_HH.AllocateGPUMemory(pixelCBSize, m_pixelConstants_GPUAddr);
		m_pixelConstants->ToneMappingMode = ToneMappingMode_sRGB;
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

	const int NUM_SPHERES = 5;
	for (uint32_t i = 0; i < NUM_SPHERES; ++i)
	{
		SMesh sphere;
		sphere.Load("resources/meshes/Sphere.obj");
		sphere.GenerateTangents();
		sphere.CreateConstants(m_HH);
		sphere.MoveTo(XMFLOAT3(-1.0f * NUM_SPHERES / 2.0f + i * 1.0f, 0.0f, 0.0f));
		sphere.CopyToUploadHeap(m_device.Get(), m_commandList.Get());
		sphere.ReleaseCPUData();
		m_meshes.push_back(sphere);
	}

	// /*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*//*-+-*/
	// Textures

	STexture coast_sand_rocks_02;
	coast_sand_rocks_02.AddTexture("resources/coast_sand_rocks_02/textures/coast_sand_rocks_02_diff_1k.jpg");
	coast_sand_rocks_02.AddTexture("resources/coast_sand_rocks_02/textures/coast_sand_rocks_02_nor_dx_1k.exr");
	coast_sand_rocks_02.AddTexture("resources/coast_sand_rocks_02/textures/coast_sand_rocks_02_arm_1k.exr");
	m_textures.push_back(coast_sand_rocks_02);

	STexture tex_cliff_side;
	tex_cliff_side.AddTexture("resources/cliff_side/textures/cliff_side_diff_1k.jpg");
	tex_cliff_side.AddTexture("resources/cliff_side/textures/cliff_side_nor_dx_1k.exr");
	tex_cliff_side.AddTexture("resources/cliff_side/textures/cliff_side_arm_1k.exr");
	m_textures.push_back(tex_cliff_side);

	STexture red_brick;
	red_brick.AddTexture("resources/red_brick/textures/red_brick_diff_1k.jpg");
	red_brick.AddTexture("resources/red_brick/textures/red_brick_nor_dx_1k.exr");
	red_brick.AddTexture("resources/red_brick/textures/red_brick_arm_1k.exr");
	m_textures.push_back(red_brick);

	//STexture plastered_wall_04;
	//plastered_wall_04.AddTexture("resources/plastered_wall_04/textures/plastered_wall_04_diff_1k.jpg");
	//plastered_wall_04.AddTexture("resources/plastered_wall_04/textures/plastered_wall_04_nor_dx_1k.exr");
	//plastered_wall_04.AddTexture("resources/plastered_wall_04/textures/plastered_wall_04_arm_1k.exr");
	//m_textures.push_back(plastered_wall_04);

	STexture square_tiles_03;
	square_tiles_03.AddTexture("resources/square_tiles_03/textures/square_tiles_03_diff_1k.jpg");
	square_tiles_03.AddTexture("resources/square_tiles_03/textures/square_tiles_03_nor_dx_1k.exr");
	square_tiles_03.AddTexture("resources/square_tiles_03/textures/square_tiles_03_arm_1k.exr");
	m_textures.push_back(square_tiles_03);

	STexture wood_table_001;
	wood_table_001.AddTexture("resources/wood_table_001/textures/wood_table_001_diff_1k.jpg");
	wood_table_001.AddTexture("resources/wood_table_001/textures/wood_table_001_nor_dx_1k.exr");
	wood_table_001.AddTexture("resources/wood_table_001/textures/wood_table_001_arm_1k.exr");
	m_textures.push_back(wood_table_001);

	for (STexture& t : m_textures)
	{
		t.LoadTextures();
		t.CopyToUploadHeap(m_device.Get(), m_commandList.Get(), m_HH);
		t.ReleaseCPUData();
	}
}

void D3D12Engine::LoadIBL(const char* filename)
{
	m_sphericalTexture.AddTexture(filename);
	m_sphericalTexture.LoadTextures();
	m_sphericalTexture.CopyToUploadHeap(m_device.Get(), m_commandList.Get(), m_HH);
	m_sphericalTexture.ReleaseCPUData();

	const uint32_t resolution_envMap = 1024;
	const uint32_t resolution_irradianceMap = 256;
	const uint32_t resolution_prefilteredEnvMap = 256;
	const uint32_t n_mipLevels = 6; // 256, 128, 64, 32, 16, 8
	const uint32_t resolution_BRDFMap = 256;

	D3D12_CPU_DESCRIPTOR_HANDLE SRV_envMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_irradianceMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_prefilteredEnvMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	D3D12_CPU_DESCRIPTOR_HANDLE SRV_BRDFMap = m_HH.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

	// \=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/ +
	// Environment map
	// /=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\=/=\ +
	{
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			resolution_envMap,
			resolution_envMap,
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
			IID_PPV_ARGS(&m_envMap)));

		// As shader resource:
		// SRV for m_envMap

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels = (uint32_t)-1;
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
			m_commandList->SetGraphicsRootDescriptorTable(1, m_sphericalTexture.GetGPUHandle());
			m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

			currentRTV.ptr += m_HH.GetDescriptorSizeRTV();
			constMatrices_GPUAddr += sizeof(CameraConstants);
			constMatrices_CPUAddr++;
		}

		// Transition the cube map to a shader resource
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_envMap.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
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
		auto Desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			resolution_prefilteredEnvMap,
			resolution_prefilteredEnvMap,
			6,  // ArraySize (Cubemap has 6 faces)
			n_mipLevels,  // MipLevels
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

	CD3DX12_VIEWPORT viewportSSAA(0.0f, 0.0f, static_cast<float>(m_width * SSAA_MULTIPLIER), static_cast<float>(m_height * SSAA_MULTIPLIER));
	CD3DX12_RECT scissorRectSSAA(0, 0, static_cast<LONG>(m_width * SSAA_MULTIPLIER), static_cast<LONG>(m_height * SSAA_MULTIPLIER));
	m_commandList->RSSetViewports(1, &viewportSSAA);
	m_commandList->RSSetScissorRects(1, &scissorRectSSAA);

	// Set MSAA render target.
	m_commandList->OMSetRenderTargets(1, &m_RTV_msaaRenderTarget, FALSE, &m_DSV_depthStencilBuffer);
	m_commandList->ClearRenderTargetView(m_RTV_msaaRenderTarget, CLEAR_COLOR, 0, nullptr);
	m_commandList->ClearDepthStencilView(m_DSV_depthStencilBuffer, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// Render environment map
	// sampleEnvMap.hlsl
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	m_commandList->SetPipelineState(m_pipelineStates[PSO_SampleEnvMap].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_SampleEnvMap].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraConstants_GPUAddr);
	m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_envMap);
	m_cubeInsideFacing.ScheduleDraw(m_commandList.Get());

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// Render objects
	// render.hlsl
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	m_commandList->SetPipelineState(m_pipelineStates[PSO_Render].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_Render].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_cameraConstants_GPUAddr);
	m_commandList->SetGraphicsRootConstantBufferView(2, m_pixelConstants_GPUAddr);
	m_commandList->SetGraphicsRootDescriptorTable(3, m_SRV_IBL);

	for (uint32_t i = 0; i < m_meshes.size(); ++i)
	{
		m_commandList->SetGraphicsRootConstantBufferView(1, m_meshes[i].GetGPUAddr());
		m_commandList->SetGraphicsRootDescriptorTable(4, m_textures[i].GetGPUHandle());
		m_meshes[i].ScheduleDraw(m_commandList.Get());
	}

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// Resolve MSAA buffer to intermediate HDR texture
	// MSAA Render Target:  RENDER_TARGET -> RESOLVE_SOURCE
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	D3D12_RESOURCE_BARRIER resolve_barriers[] = {
		// transition msaa texture from target to resolve source
		CD3DX12_RESOURCE_BARRIER::Transition(m_msaaRenderTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
	};
	m_commandList->ResourceBarrier(_countof(resolve_barriers), resolve_barriers);
	m_commandList->ResolveSubresource(m_hdrResolveTarget.Get(), 0, m_msaaRenderTarget.Get(), 0, HDR_FORMAT);

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// Render intermediate texture to back buffer
	// HDR Resolve Target:  RESOLVE_DEST -> PIXEL_SHADER_RESOURCE
	// Back Buffer:			PRESENT -> RENDER_TARGET
	// present.hlsl
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	D3D12_RESOURCE_BARRIER present_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_hdrResolveTarget.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
	};
	m_commandList->ResourceBarrier(_countof(present_barriers), present_barriers);

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	m_commandList->OMSetRenderTargets(1, &m_RTV_frameBuffers[m_frameIndex], FALSE, nullptr);
	m_commandList->ClearRenderTargetView(m_RTV_frameBuffers[m_frameIndex], CLEAR_COLOR, 0, nullptr);

	// Process the intermediate and draw into the swap chain render target.
	m_commandList->SetPipelineState(m_pipelineStates[PSO_Present8bit].Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignatures[PSO_Present8bit].Get());
	m_HH.BindDescriptorHeaps(m_commandList.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_pixelConstants_GPUAddr);
	m_commandList->SetGraphicsRootDescriptorTable(1, m_SRV_hdrResolveTarget);
	m_presentTriangle.ScheduleDraw(m_commandList.Get());

	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	// Present the frame, and
	// Reset buffer states for next frame
	// MSAA Render Target:  RESOLVE_SOURCE -> RENDER_TARGET
	// HDR Resolve Target:  PIXEL_SHADER_RESOURCE -> RESOLVE_DEST
	// Back Buffer:			RENDER_TARGET -> PRESENT
	// ==--==--==--==--==--==--==--==--==--==--==--==--==--==--==--==
	D3D12_RESOURCE_BARRIER cleanup_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_msaaRenderTarget.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(m_hdrResolveTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST),
		CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
	};
	m_commandList->ResourceBarrier(_countof(cleanup_barriers), cleanup_barriers);

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
	memcpy(&m_pixelConstants->eyePosition, &cameraPosition, sizeof(XMFLOAT3));
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
