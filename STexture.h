#pragma once

#include "HelperFunctions.h"
#include "DescHeapWrapper.h"

#include <string>

#include <dxgi1_6.h>
using Microsoft::WRL::ComPtr;

class TextureData
{
private:
	// A better way is to use std::vector to store texture data.
	// But for the sake of learning, we use raw pointer here.
    char* _data = nullptr;
    uint64_t _size = 0;
    bool _allocated = false;

public:
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t pixelSize = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

	// For debugging purpose
	std::string name = "";

public:
	TextureData() = default;
	~TextureData() { release(); }

	// Copying is not allowed
	TextureData(const TextureData& other) = delete;
	TextureData& operator=(const TextureData& other) = delete;

	// Move Constructor
	TextureData(TextureData&& other) noexcept {
		move_from(std::move(other));
	}

	// Move Assignment operator
	TextureData& operator=(TextureData&& other) noexcept {
		if (this != &other) {  // Pervent self-move
			release();
			move_from(std::move(other));
		}
		return *this;
	}

public:
	inline void reserve(uint64_t size)
	{
		assert(!_allocated);  // Memory must not be allocated.
		_data = new char[size];
		_allocated = true;
		_size = size;
	}

	inline void release()
	{
		if (_allocated)
		{
			delete[] _data;
			_data = nullptr;
			_allocated = false;
			_size = 0;
		}
	}

	inline uint64_t size()
	{
		assert(_allocated);  // Calling size() before reserve(int) is not allowed.
		return _size;
	}

	inline char* data()
	{
		assert(_allocated);  // Calling data() before reserve(int) is not allowed.
		return _data;
	}

	inline bool loaded()
	{
		return _allocated;
	}

private:
	void copy_from(const TextureData& other) {
		if (other._allocated) {
			reserve(other._size);
			memcpy(_data, other._data, _size);
		}
		width = other.width;
		height = other.height;
		pixelSize = other.pixelSize;
		format = other.format;
	}

	void move_from(TextureData&& other) noexcept {
		_data = other._data;
		_size = other._size;
		_allocated = other._allocated;
		width = other.width;
		height = other.height;
		pixelSize = other.pixelSize;
		format = other.format;

		// Set source point to nullptr to prevent double releasing
		other._data = nullptr;
		other._size = 0;
		other._allocated = false;
		other.width = 0;
		other.height = 0;
		other.pixelSize = 0;
		other.format = DXGI_FORMAT_UNKNOWN;
	}
};

class STexture
{
public:
	STexture() = default;
	~STexture()
	{
		ReleaseUploadHeaps();
		ReleaseCPUData();
		ReleaseGPUData();
	}

	// Copy Constructor
	STexture(const STexture& other) {
		assert(m_textures.size() == 0);  // Copy is allowed only when no textures are loaded
		m_textureResources = other.m_textureResources;
		m_uploadHeaps = other.m_uploadHeaps;
		m_SRVCombined = other.m_SRVCombined;
		m_SRVsSeparated = other.m_SRVsSeparated;
		m_textureFilenames = other.m_textureFilenames;
	}

	// Copy Assignment operator
	STexture& operator=(const STexture& other) {
		if (this != &other) {  // Pervent self-assignment
			ReleaseUploadHeaps();
			m_textureResources.clear();
			m_uploadHeaps.clear();
			assert(m_textures.size() == 0);  // Copy is allowed only when no textures are loaded
			m_textureResources = other.m_textureResources;
			m_uploadHeaps = other.m_uploadHeaps;
			m_SRVCombined = other.m_SRVCombined;
			m_SRVsSeparated = other.m_SRVsSeparated;
			m_textureFilenames = other.m_textureFilenames;
		}
		return *this;
	}

	// Move Constructor
	STexture(STexture&& other) noexcept {
		m_textures = std::move(other.m_textures);
		m_textureResources = std::move(other.m_textureResources);
		m_uploadHeaps = std::move(other.m_uploadHeaps);
		m_SRVCombined = std::move(other.m_SRVCombined);
		m_SRVsSeparated = std::move(other.m_SRVsSeparated);
		m_textureFilenames = std::move(other.m_textureFilenames);
	}

	// Move Assignment operator
	STexture& operator=(STexture&& other) noexcept {
		if (this != &other) {  // Pervent self-move
			ReleaseUploadHeaps();
			ReleaseCPUData();
			m_textureResources.clear();
			m_uploadHeaps.clear();
			m_textures = std::move(other.m_textures);
			m_textureResources = std::move(other.m_textureResources);
			m_uploadHeaps = std::move(other.m_uploadHeaps);
			m_SRVCombined = std::move(other.m_SRVCombined);
			m_SRVsSeparated = std::move(other.m_SRVsSeparated);
			m_textureFilenames = std::move(other.m_textureFilenames);
		}
		return *this;
	}
private:
	std::vector<TextureData> m_textures;
	std::vector<ComPtr<ID3D12Resource>> m_textureResources;
	std::vector<ComPtr<ID3D12Resource>> m_uploadHeaps;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRVCombined;
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_SRVsSeparated;

	// Pending lists that stores textures to be loaded later
	// Note textures are not loaded until LoadTextures() is called
	std::vector<std::string> m_textureFilenames;

public:
	inline ComPtr<ID3D12Resource>& GetTextureResource(uint32_t index) { return m_textureResources[index]; }
	inline D3D12_GPU_DESCRIPTOR_HANDLE GetCombinedSRV() { return m_SRVCombined; }
	inline D3D12_GPU_DESCRIPTOR_HANDLE GetSRV(uint32_t index) { return m_SRVsSeparated[index]; }
	inline size_t size() { return m_textureFilenames.size(); }
	

private:
	void _LoadEXR(const char* filename);
	void _LoadStbi(const char* filename);
	
public:
	// Push a texture filename to the pending list
	// Note textures are not loaded until LoadTextures() is called
	void AddTexture(std::string filename);

	// Load any added texture files into CPU memory
	// Pending list will be cleared after loading
	void LoadTextures();

	// Copy texture data to upload heap
	// This function expects that the command list is in recording state
	void CopyToUploadHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, DescHeapWrapper& hh);

	void ReleaseUploadHeaps();
	void ReleaseCPUData();
	void ReleaseGPUData();

};

