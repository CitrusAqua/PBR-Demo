#pragma once

#include "ShaderSharedStructs.h"
#include "DescHeapWrapper.h"

#include <vector>
#include <dxgi1_6.h>
using Microsoft::WRL::ComPtr;

struct SVertex
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT2 uv;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT3 tangent;
	DirectX::XMFLOAT3 bitangent;
};

struct SMeshSection
{
	UINT32 indexCount;
	UINT32 startIndexLocation;
	UINT32 baseVertexLocation;
};

class SMesh
{
private:
	// [vertices] and [indices]: stores those of all meshes combined
	// [meshSection]: indexer for [vertices] and [indices]
	std::vector<SVertex> m_vertices;
	std::vector<UINT32> m_indices;
	std::vector<SMeshSection> m_meshSections;

	ComPtr<ID3D12Resource> m_stagingVertexBuffer;
	ComPtr<ID3D12Resource> m_stagingIndexBuffer;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

private:
	DirectX::XMMATRIX m_scaling;
	DirectX::XMMATRIX m_rotation;
	DirectX::XMMATRIX m_translation;

	// Raw pointers point to a location in Heap which is managed by the DescHeapWrapper,
	// so we do not use smart pointer here.
	ModelConstants* m_constants;
	D3D12_GPU_VIRTUAL_ADDRESS m_constants_GPUAddr;

public:
	SMesh() = default;
	~SMesh();

	inline D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddr() { return m_constants_GPUAddr; }

private:
	void _LoadArray(const std::vector<SVertex>& vertices, const std::vector<UINT32>& indices);
	void _LoadGLTF(const char* filename);
	void _LoadOBJ(const char* filename);

public:
	void Load(const std::vector<SVertex>& vertices, const std::vector<UINT32>& indices);
	void Load(const char* filename);

	void GenerateNormals();
	void GenerateTangents();

	void MoveTo(const DirectX::XMFLOAT3& position);
	void RotateBy(const DirectX::XMFLOAT3& rotation);
	void SetScale(const float factor);

private:
	void _UpdateModelMatrix();

public:
	void CreateConstants(DescHeapWrapper& hh);
	void CopyToUploadHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	void ScheduleDraw(ID3D12GraphicsCommandList* cmdList);
	void ReleaseUploadHeaps();
	void ReleaseCPUData();
};

