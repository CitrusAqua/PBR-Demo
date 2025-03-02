#pragma once

#include "HelperFunctions.h"
#include "DescHeapWrapper.h"

#include <string>
#include <list>

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
		m_GPUHandle = other.m_GPUHandle;
		assert(m_textures.size() == 0);  // Copy is allowed only when no textures are loaded
		m_textureResources = other.m_textureResources;
		m_uploadHeaps = other.m_uploadHeaps;
		m_textureFilenames = other.m_textureFilenames;
	}

	// Copy Assignment operator
	STexture& operator=(const STexture& other) {
		if (this != &other) {  // Pervent self-assignment
			ReleaseUploadHeaps();
			m_textureResources.clear();
			m_uploadHeaps.clear();
			m_GPUHandle = other.m_GPUHandle;
			assert(m_textures.size() == 0);  // Copy is allowed only when no textures are loaded
			m_textureResources = other.m_textureResources;
			m_uploadHeaps = other.m_uploadHeaps;
			m_textureFilenames = other.m_textureFilenames;
		}
		return *this;
	}

	// Move Constructor
	STexture(STexture&& other) noexcept {
		m_GPUHandle = other.m_GPUHandle;
		m_textures = std::move(other.m_textures);
		m_textureResources = std::move(other.m_textureResources);
		m_uploadHeaps = std::move(other.m_uploadHeaps);
		m_textureFilenames = std::move(other.m_textureFilenames);
		other.m_GPUHandle.ptr = 0;
	}

	// Move Assignment operator
	STexture& operator=(STexture&& other) noexcept {
		ReleaseUploadHeaps();
		ReleaseCPUData();
		m_textureResources.clear();
		m_uploadHeaps.clear();
		m_GPUHandle = other.m_GPUHandle;
		m_textures = std::move(other.m_textures);
		m_textureResources = std::move(other.m_textureResources);
		m_uploadHeaps = std::move(other.m_uploadHeaps);
		m_textureFilenames = std::move(other.m_textureFilenames);
		other.m_GPUHandle.ptr = 0;
		return *this;
	}
private:
	D3D12_GPU_DESCRIPTOR_HANDLE m_GPUHandle;
	std::list<TextureData> m_textures;
	std::list<std::string> m_textureFilenames;
	std::vector<ComPtr<ID3D12Resource>> m_textureResources;
	std::vector<ComPtr<ID3D12Resource>> m_uploadHeaps;

public:
	inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() { return m_GPUHandle; }

private:
	void _LoadEXR(const char* filename);
	void _LoadStbi(const char* filename);
	
public:
	void AddTexture(std::string filename);
	void LoadTextures();
	void CopyToUploadHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, DescHeapWrapper& hh);
	void ReleaseUploadHeaps();
	void ReleaseCPUData();
	void ReleaseGPUData();

public:
	inline size_t size() { return m_textures.size(); }
};

