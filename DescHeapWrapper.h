#pragma once

#include <stdint.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <vector>

#include "DirectXMath.h"
#include "DXSample.h"

using Microsoft::WRL::ComPtr;

struct DescriptorHeapStruct
{
	ComPtr<ID3D12DescriptorHeap> Heap;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUStart;
	D3D12_GPU_DESCRIPTOR_HANDLE GPUStart;
	UINT32 Size;
	UINT32 Capacity;
};

struct UploadHeapStruct
{
	ComPtr<ID3D12Resource> Heap;
	UINT8* CPUStart;
	D3D12_GPU_VIRTUAL_ADDRESS GPUStart;
	UINT32 Size;
	UINT32 Capacity;
};


class DescHeapWrapper
{
public:
	void Init(ID3D12Device* device);
	void Release();

	UINT32 m_DescriptorSizeRTV;
	UINT32 m_DescriptorSizeSRV;
	inline UINT32 GetDescriptorSizeRTV() { return m_DescriptorSizeRTV; }
	inline UINT32 GetDescriptorSizeCBV_SRV_UAV() { return m_DescriptorSizeSRV; }

private:
	// These are intended to be read-only pointers
	ID3D12Device *ref_device;

	DescriptorHeapStruct m_RTVHeap;
	DescriptorHeapStruct m_DSVHeap;
	DescriptorHeapStruct m_CPUDescriptorHeap;
	DescriptorHeapStruct m_GPUDescriptorHeap;
	UploadHeapStruct m_UploadHeap;

public:
	void CreateHeaps();
	DescriptorHeapStruct& GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags, UINT32& OutDescriptorSize);
	

public:
	inline void BindDescriptorHeaps(ID3D12GraphicsCommandList* CmdList)
	{
		ID3D12DescriptorHeap* ppHeaps[] = { m_GPUDescriptorHeap.Heap.Get()};
		CmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE Type, UINT32 Count)
	{
		UINT32 DescriptorSize;
		DescriptorHeapStruct& Heap = GetDescriptorHeap(Type, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, DescriptorSize);
		assert((Heap.Size + Count) < Heap.Capacity);

		D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle;
		CPUHandle.ptr = Heap.CPUStart.ptr + (size_t)Heap.Size * DescriptorSize;
		Heap.Size += Count;

		return CPUHandle;
	}

	inline void AllocateGPUDescriptors(UINT32 Count, D3D12_CPU_DESCRIPTOR_HANDLE& OutCPUHandle, D3D12_GPU_DESCRIPTOR_HANDLE& OutGPUHandle)
	{
		UINT32 DescriptorSize;
		DescriptorHeapStruct& Heap = GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, DescriptorSize);
		assert((Heap.Size + Count) < Heap.Capacity);

		OutCPUHandle.ptr = Heap.CPUStart.ptr + (size_t)Heap.Size * DescriptorSize;
		OutGPUHandle.ptr = Heap.GPUStart.ptr + (size_t)Heap.Size * DescriptorSize;

		Heap.Size += Count;
	}

	inline D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptorsToGPUHeap(UINT32 Count, D3D12_CPU_DESCRIPTOR_HANDLE SrcBaseHandle)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE CPUBaseHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE GPUBaseHandle;
		AllocateGPUDescriptors(Count, CPUBaseHandle, GPUBaseHandle);
		ref_device->CopyDescriptorsSimple(Count, CPUBaseHandle, SrcBaseHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return GPUBaseHandle;
	}

	inline void* AllocateGPUMemory(UINT32 Size, D3D12_GPU_VIRTUAL_ADDRESS& OutGPUAddress)
	{
		assert(Size > 0);

		if (Size & 0xff)
		{
			// Always align to 256 bytes.
			Size = (Size + 255) & ~0xff;
		}

		UploadHeapStruct& UploadHeap = m_UploadHeap;
		assert((UploadHeap.Size + Size) < UploadHeap.Capacity);

		void* CPUAddress = UploadHeap.CPUStart + UploadHeap.Size;
		OutGPUAddress = UploadHeap.GPUStart + UploadHeap.Size;

		UploadHeap.Size += Size;
		return CPUAddress;
	}
};

