#include "stdafx.h"
#include "SMesh.h"

#include "DXSampleHelper.h"
#include "HelperFunctions.h"
//#include "OBJ_Loader.h"

#include <iostream>

#define TINYOBJLOADER_IMPLEMENTATION 
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

using namespace DirectX;
using std::vector;

SMesh::~SMesh()
{
	ReleaseUploadHeaps();
	ReleaseCPUData();

	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
}

void SMesh::_LoadArray(const vector<SVertex>& vertices, const vector<UINT32>& indices)
{
	SMeshSection sms;
	sms.baseVertexLocation = (UINT32)m_vertices.size();
	sms.startIndexLocation = (UINT32)m_indices.size();
	sms.indexCount = (UINT32)indices.size();
	m_meshSections.push_back(sms);
	m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
	m_indices.insert(m_indices.end(), indices.begin(), indices.end());
}

void SMesh::_LoadGLTF(const char* filename)
{
	cgltf_options Options = {};
	cgltf_data* Data = nullptr;
	{
		cgltf_result R = cgltf_parse_file(&Options, filename, &Data);
		assert(R == cgltf_result_success);
		R = cgltf_load_buffers(&Options, Data, filename);
		assert(R == cgltf_result_success);
	}

	cgltf_mesh* Mesh = &Data->meshes[0];
	//assert(Mesh->primitives_count <= MESH_MAX_NUM_SECTIONS);

	auto sectionOffset = m_meshSections.size();

	UINT32 TotalNumVertices = 0;
	UINT32 TotalNumIndices = 0;

	for (UINT32 SectionIdx = 0; SectionIdx < Mesh->primitives_count; ++SectionIdx)
	{
		assert(Data->meshes[0].primitives[SectionIdx].indices);
		assert(Data->meshes[0].primitives[SectionIdx].attributes);

		m_meshSections.push_back(SMeshSection());

		TotalNumIndices += (UINT32)Data->meshes[0].primitives[SectionIdx].indices->count;
		TotalNumVertices += (UINT32)Data->meshes[0].primitives[SectionIdx].attributes[0].data->count;
	}

	//InOutVertices.reserve(InOutVertices.size() + TotalNumVertices);
	//InOutIndices.reserve(InOutIndices.size() + TotalNumIndices);

	vector<XMFLOAT3> positions;
	vector<XMFLOAT2> texcoords;
	vector<XMFLOAT3> normals;
	//vector<XMFLOAT3> tangents;
	positions.reserve(TotalNumVertices);
	texcoords.reserve(TotalNumVertices);
	normals.reserve(TotalNumVertices);
	//tangents.reserve(TotalNumVertices);
	//memset(positions.data(), 0, positions.size() * sizeof(XMFLOAT3));
	//memset(texcoords.data(), 0, texcoords.size() * sizeof(XMFLOAT2));
	//memset(normals.data(), 0, normals.size() * sizeof(XMFLOAT3));
	//memset(tangents.data(), 0, tangents.size() * sizeof(XMFLOAT3));

	for (UINT32 SectionIdx = 0; SectionIdx < Mesh->primitives_count; ++SectionIdx)
	{
		// Indices.
		{
			const cgltf_accessor* Accessor = Data->meshes[0].primitives[SectionIdx].indices;

			assert(Accessor->buffer_view);
			assert(Accessor->stride == Accessor->buffer_view->stride || Accessor->buffer_view->stride == 0);
			assert((Accessor->stride * Accessor->count) == Accessor->buffer_view->size);

			const auto DataAddr = (const UINT8*)Accessor->buffer_view->buffer->data + Accessor->offset + Accessor->buffer_view->offset;

			m_meshSections[sectionOffset + SectionIdx].startIndexLocation = (UINT32)m_indices.size();
			m_meshSections[sectionOffset + SectionIdx].indexCount = (UINT32)Accessor->count;

			if (Accessor->stride == 1)
			{
				const UINT8* DataU8 = (const UINT8*)DataAddr;
				for (UINT32 Idx = 0; Idx < Accessor->count; ++Idx)
				{
					m_indices.push_back((UINT32)*DataU8++);
				}
			}
			else if (Accessor->stride == 2)
			{
				const UINT16* DataU16 = (const UINT16*)DataAddr;
				for (UINT32 Idx = 0; Idx < Accessor->count; ++Idx)
				{
					m_indices.push_back((UINT32)*DataU16++);
				}
			}
			else if (Accessor->stride == 4)
			{
				m_indices.resize(m_indices.size() + Accessor->count);
				memcpy(&m_indices[m_indices.size() - Accessor->count], DataAddr, Accessor->count * Accessor->stride);
			}
			else
			{
				assert(0);
			}
		}

		// Attributes.
		{
			const UINT32 NumAttribs = (UINT32)Data->meshes[0].primitives[SectionIdx].attributes_count;

			for (UINT32 AttribIdx = 0; AttribIdx < NumAttribs; ++AttribIdx)
			{
				const cgltf_attribute* Attrib = &Data->meshes[0].primitives[SectionIdx].attributes[AttribIdx];
				const cgltf_accessor* Accessor = Attrib->data;

				assert(Accessor->buffer_view);
				assert(Accessor->stride == Accessor->buffer_view->stride || Accessor->buffer_view->stride == 0);
				assert((Accessor->stride * Accessor->count) == Accessor->buffer_view->size);

				const auto DataAddr = (const UINT8*)Accessor->buffer_view->buffer->data + Accessor->offset + Accessor->buffer_view->offset;

				if (Attrib->type == cgltf_attribute_type_position)
				{
					assert(Accessor->type == cgltf_type_vec3);
					positions.resize(Accessor->count);
					memcpy(positions.data(), DataAddr, Accessor->count * Accessor->stride);
				}
				else if (Attrib->type == cgltf_attribute_type_texcoord)
				{
					assert(Accessor->type == cgltf_type_vec2);
					texcoords.resize(Accessor->count);
					memcpy(texcoords.data(), DataAddr, Accessor->count * Accessor->stride);
				}
				else if (Attrib->type == cgltf_attribute_type_normal)
				{
					assert(Accessor->type == cgltf_type_vec3);
					normals.resize(Accessor->count);
					memcpy(normals.data(), DataAddr, Accessor->count * Accessor->stride);
				}
				//else if (Attrib->type == cgltf_attribute_type_tangent)
				//{
				//	assert(Accessor->type == cgltf_type_vec3);
				//	tangents.resize(Accessor->count);
				//	memcpy(tangents.data(), DataAddr, Accessor->count * Accessor->stride);
				//}
			}

			assert(positions.size() > 0 && positions.size() == normals.size());

			m_meshSections[sectionOffset + SectionIdx].baseVertexLocation = (UINT32)m_vertices.size();

			for (UINT32 Idx = 0; Idx < positions.size(); ++Idx)
			{
				SVertex Vertex;
				Vertex.position = positions[Idx];
				Vertex.uv = texcoords[Idx];
				Vertex.normal = normals[Idx];
				//Vertex.tangent = tangents[Idx];
				m_vertices.push_back(Vertex);
			}

			positions.clear();
			texcoords.clear();
			normals.clear();
			//tangents.clear();
		}
	}

	cgltf_free(Data);
}

void SMesh::_LoadOBJ(const char* filename)
{
	tinyobj::ObjReaderConfig reader_config;
	reader_config.mtl_search_path = "./"; // Path to material files

	tinyobj::ObjReader reader;

	if (!reader.ParseFromFile(filename, reader_config)) {
		if (!reader.Error().empty()) {
			std::cerr << "TinyObjReader: " << reader.Error();
		}
		exit(1);
	}

	if (!reader.Warning().empty()) {
		std::cout << "TinyObjReader: " << reader.Warning();
	}

	auto& attrib = reader.GetAttrib();
	auto& shapes = reader.GetShapes();
	auto& materials = reader.GetMaterials();

	// Loop over shapes
	for (size_t s = 0; s < shapes.size(); s++) {

		vector<SVertex> vertices;
		m_meshSections.push_back(SMeshSection());
		m_meshSections.back().baseVertexLocation = (UINT32)m_vertices.size();
		m_meshSections.back().startIndexLocation = (UINT32)m_indices.size();
		m_meshSections.back().indexCount = (UINT32)shapes[s].mesh.indices.size();
		m_indices.reserve(m_indices.size() + shapes[s].mesh.indices.size());

		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

			// Loop over vertices in the face.
			size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
			assert(fv == 3);  // Only triangle faces are supported

			for (size_t v = 0; v < fv; v++) {

				// Re-order vertices to fix the winding order to match DirectX 12
				size_t reordered_v = v;
				if (v == 1) reordered_v = 2;
				if (v == 2) reordered_v = 1;
				m_indices.push_back(index_offset + reordered_v);

				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
				
				SVertex vertex;
				vertex.position = XMFLOAT3(
					attrib.vertices[3 * size_t(idx.vertex_index) + 0],
					attrib.vertices[3 * size_t(idx.vertex_index) + 1],
					attrib.vertices[3 * size_t(idx.vertex_index) + 2]);

				// Check if `normal_index` is zero or positive. negative = no normal data
				if (idx.normal_index >= 0) {
					vertex.normal = XMFLOAT3(
						attrib.normals[3 * size_t(idx.normal_index) + 0],
						attrib.normals[3 * size_t(idx.normal_index) + 1],
						attrib.normals[3 * size_t(idx.normal_index) + 2]);
				}

				// Check if `texcoord_index` is zero or positive. negative = no texcoord data
				if (idx.texcoord_index >= 0) {
					vertex.uv = XMFLOAT2(
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
						attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]);
				}

				m_vertices.push_back(vertex);
			}
			index_offset += fv;
		}
	}
}

void SMesh::Load(const vector<SVertex>& vertices, const vector<UINT32>& indices)
{
	_LoadArray(vertices, indices);
}

void SMesh::Load(const char* filename)
{
	if (ends_with(filename, ".obj"))
	{
		_LoadOBJ(filename);
	}
	else if (ends_with(filename, ".gltf"))
	{
		_LoadGLTF(filename);
	}
	else
	{
		throw std::runtime_error("Unsupported file extension.");
	}
}

void SMesh::GenerateNormals()
{
	for (UINT32 mesh_idx = 0; mesh_idx < m_meshSections.size(); ++mesh_idx)
	{
		SMeshSection& meshSection = m_meshSections[mesh_idx];
		UINT32 offset = meshSection.baseVertexLocation;
		for (UINT32 index_idx = meshSection.startIndexLocation; index_idx < meshSection.startIndexLocation + meshSection.indexCount; index_idx += 3)
		{
			XMFLOAT3 v0 = m_vertices[offset + m_indices[index_idx + 0]].position;
			XMFLOAT3 v1 = m_vertices[offset + m_indices[index_idx + 1]].position;
			XMFLOAT3 v2 = m_vertices[offset + m_indices[index_idx + 2]].position;
			XMFLOAT3 e0 = XMFLOAT3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
			XMFLOAT3 e1 = XMFLOAT3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
			XMFLOAT3 normal = XMFLOAT3(
				e0.y * e1.z - e0.z * e1.y,
				e0.z * e1.x - e0.x * e1.z,
				e0.x * e1.y - e0.y * e1.x
			);
			for (UINT32 i = 0; i < 3; ++i)
			{
				m_vertices[offset + m_indices[index_idx + i]].normal = normal;
			}
		}
	}
}

void SMesh::GenerateTangents()
{
	for (UINT32 mesh_idx = 0; mesh_idx < m_meshSections.size(); ++mesh_idx)
	{
		SMeshSection& meshSection = m_meshSections[mesh_idx];
		UINT32 offset = meshSection.baseVertexLocation;
		for (UINT32 index_idx = meshSection.startIndexLocation; index_idx < meshSection.startIndexLocation + meshSection.indexCount; index_idx += 3)
		{
			XMFLOAT3 v0 = m_vertices[offset + m_indices[index_idx + 0]].position;
			XMFLOAT3 v1 = m_vertices[offset + m_indices[index_idx + 1]].position;
			XMFLOAT3 v2 = m_vertices[offset + m_indices[index_idx + 2]].position;
			XMFLOAT2 uv0 = m_vertices[offset + m_indices[index_idx + 0]].uv;
			XMFLOAT2 uv1 = m_vertices[offset + m_indices[index_idx + 1]].uv;
			XMFLOAT2 uv2 = m_vertices[offset + m_indices[index_idx + 2]].uv;
			XMFLOAT3 e0 = XMFLOAT3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
			XMFLOAT3 e1 = XMFLOAT3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
			XMFLOAT2 duv0 = XMFLOAT2(uv1.x - uv0.x, uv1.y - uv0.y);
			XMFLOAT2 duv1 = XMFLOAT2(uv2.x - uv0.x, uv2.y - uv0.y);
			float f = 1.0f / (duv0.x * duv1.y - duv1.x * duv0.y);
			XMFLOAT3 tangent = XMFLOAT3(
				f * (duv1.y * e0.x - duv0.y * e1.x),
				f * (duv1.y * e0.y - duv0.y * e1.y),
				f * (duv1.y * e0.z - duv0.y * e1.z)
			);
			XMFLOAT3 bitangent = XMFLOAT3(
				f * (duv0.x * e1.x - duv1.x * e0.x),
				f * (duv0.x * e1.y - duv1.x * e0.y),
				f * (duv0.x * e1.z - duv1.x * e0.z)
			);
			for (UINT32 i = 0; i < 3; ++i)
			{
				m_vertices[offset + m_indices[index_idx + i]].tangent = tangent;
				m_vertices[offset + m_indices[index_idx + i]].bitangent = bitangent;
			}
		}
	}
}

void SMesh::MoveTo(const XMFLOAT3& position)
{
	m_translation = XMMatrixTranslation(position.x, position.y, position.z);
	_UpdateModelMatrix();
}

void SMesh::RotateBy(const DirectX::XMFLOAT3& rotation)
{
	m_rotation *= XMMatrixRotationX(rotation.x);
	m_rotation *= XMMatrixRotationY(rotation.y);
	m_rotation *= XMMatrixRotationZ(rotation.z);
	_UpdateModelMatrix();
}

void SMesh::SetScale(const float factor)
{
	m_scaling = XMMatrixScaling(factor, factor, factor);
	_UpdateModelMatrix();
}

void SMesh::_UpdateModelMatrix()
{
	XMMATRIX model = m_scaling * m_rotation * m_translation;
	memcpy(&m_constants->model, &model, sizeof(XMMATRIX));
}

void SMesh::CreateConstants(DescHeapWrapper& hh)
{
	UINT32 vertexCBSize = sizeof(ModelConstants);
	m_constants = (ModelConstants*)hh.AllocateGPUMemory(vertexCBSize, m_constants_GPUAddr);
	m_scaling = XMMatrixIdentity();
	m_rotation = XMMatrixIdentity();
	m_translation = XMMatrixIdentity();
	_UpdateModelMatrix();
}

void SMesh::CopyToUploadHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	// Vertices
	auto verticsDataSize = m_vertices.size() * sizeof(SVertex);
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(verticsDataSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_stagingVertexBuffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(verticsDataSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_vertexBuffer)));

	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.StrideInBytes = sizeof(SVertex);
	m_vertexBufferView.SizeInBytes = (UINT)verticsDataSize;

	void* tempPtr_vertex = nullptr;
	ThrowIfFailed(m_stagingVertexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &tempPtr_vertex));
	memcpy(tempPtr_vertex, m_vertices.data(), verticsDataSize);
	m_stagingVertexBuffer->Unmap(0, nullptr);

	cmdList->CopyResource(m_vertexBuffer.Get(), m_stagingVertexBuffer.Get());
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// Indices
	auto indicesDataSize = m_indices.size() * sizeof(UINT32);
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indicesDataSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_stagingIndexBuffer)));

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indicesDataSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_indexBuffer)));

	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	m_indexBufferView.SizeInBytes = (UINT)indicesDataSize;

	void* tempPtr_index = nullptr;
	ThrowIfFailed(m_stagingIndexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &tempPtr_index));
	memcpy(tempPtr_index, m_indices.data(), indicesDataSize);
	m_stagingIndexBuffer->Unmap(0, nullptr);

	cmdList->CopyResource(m_indexBuffer.Get(), m_stagingIndexBuffer.Get());
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
}

void SMesh::ScheduleDraw(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	cmdList->IASetIndexBuffer(&m_indexBufferView);

	for (SMeshSection sms : m_meshSections)
	{
		cmdList->DrawIndexedInstanced(sms.indexCount, 1, sms.startIndexLocation, sms.baseVertexLocation, 0);
	}
}

void SMesh::ReleaseUploadHeaps()
{
	m_stagingVertexBuffer = nullptr;
	m_stagingIndexBuffer = nullptr;
}

void SMesh::ReleaseCPUData()
{
	m_vertices.clear();
	m_indices.clear();
}
