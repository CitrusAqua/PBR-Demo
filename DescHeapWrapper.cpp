#include "stdafx.h"
#include "DescHeapWrapper.h"
#include "DXSampleHelper.h"

void DescHeapWrapper::Init(ID3D12Device* device)
{
	ref_device = device;
	CreateHeaps();

	m_DescriptorSizeRTV = ref_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DescriptorSizeSRV = ref_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DescHeapWrapper::CreateHeaps()
{
	const UINT RTVHeapCapacity = 1024;
	const UINT DSVHeapCapacity = 1024;
	const UINT CPUDescriptorHeapCapacity = 16 * 1024;
	const UINT GPUDescriptorHeapCapacity = 16 * 1024;
	const UINT GPUUploadHeapCapacity = 8 * 1024 * 1024;

	// Render target descriptor heap (RTV).
	{
		m_RTVHeap.Capacity = RTVHeapCapacity;
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = RTVHeapCapacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(ref_device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_RTVHeap.Heap)));
		m_RTVHeap.CPUStart = m_RTVHeap.Heap->GetCPUDescriptorHandleForHeapStart();
		m_RTVHeap.Heap->SetName(L"RTV Heap");
	}
	// Depth-stencil descriptor heap (DSV).
	{
		m_DSVHeap.Capacity = DSVHeapCapacity;
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = DSVHeapCapacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(ref_device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_DSVHeap.Heap)));
		m_DSVHeap.CPUStart = m_DSVHeap.Heap->GetCPUDescriptorHandleForHeapStart();
		m_DSVHeap.Heap->SetName(L"DSV Heap");
	}
	// Non-shader visible descriptor heap (CBV, SRV, UAV).
	{
		m_CPUDescriptorHeap.Capacity = CPUDescriptorHeapCapacity;
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = CPUDescriptorHeapCapacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(ref_device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_CPUDescriptorHeap.Heap)));
		m_CPUDescriptorHeap.CPUStart = m_CPUDescriptorHeap.Heap->GetCPUDescriptorHandleForHeapStart();
		m_CPUDescriptorHeap.Heap->SetName(L"Non-shader visible descriptor heap");
	}
	// Shader visible descriptor heap (CBV, SRV, UAV).
	{
		m_GPUDescriptorHeap.Capacity = GPUDescriptorHeapCapacity;
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.NumDescriptors = GPUDescriptorHeapCapacity;
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(ref_device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_GPUDescriptorHeap.Heap)));
		m_GPUDescriptorHeap.CPUStart = m_GPUDescriptorHeap.Heap->GetCPUDescriptorHandleForHeapStart();
		m_GPUDescriptorHeap.GPUStart = m_GPUDescriptorHeap.Heap->GetGPUDescriptorHandleForHeapStart();
		m_GPUDescriptorHeap.Heap->SetName(L"Shader visible descriptor heap");
	}
	// Upload Memory Heap.
	{
		m_UploadHeap.Size = 0;
		m_UploadHeap.Capacity = GPUUploadHeapCapacity;
		m_UploadHeap.CPUStart = nullptr;
		m_UploadHeap.GPUStart = 0;

		ThrowIfFailed(ref_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(GPUUploadHeapCapacity),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_UploadHeap.Heap)));

		ThrowIfFailed(m_UploadHeap.Heap->Map(0, &CD3DX12_RANGE(0, 0), reinterpret_cast<void**>(&m_UploadHeap.CPUStart)));
		m_UploadHeap.GPUStart = m_UploadHeap.Heap->GetGPUVirtualAddress();
		m_UploadHeap.Heap->SetName(L"Upload Heap");
	}
}

DescriptorHeapStruct& DescHeapWrapper::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags, UINT32& OutDescriptorSize)
{
	if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
	{
		OutDescriptorSize = ref_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		return m_RTVHeap;
	}
	else if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
	{
		OutDescriptorSize = ref_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		return m_DSVHeap;
	}
	else if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		OutDescriptorSize = ref_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		if (Flags == D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
		{
			return m_CPUDescriptorHeap;
		}
		else if (Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		{
			return m_GPUDescriptorHeap;
		}
	}
	assert(false);
	OutDescriptorSize = 0;
	return m_CPUDescriptorHeap;
}

void DescHeapWrapper::Release()
{
	m_UploadHeap.Heap->Unmap(0, &CD3DX12_RANGE(0, 0));
	m_RTVHeap.Heap.Reset();
	m_DSVHeap.Heap.Reset();
	m_CPUDescriptorHeap.Heap.Reset();
	m_GPUDescriptorHeap.Heap.Reset();
	m_UploadHeap.Heap.Reset();
}

