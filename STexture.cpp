#include "stdafx.h"
#include "STexture.h"

#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImfEnvmap.h>
#include <ImfInputFile.h>
#include <ImfHeader.h>
#include <ImfStringAttribute.h>
#include <ImfIntAttribute.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfNamespace.h>
#include <ImathBox.h>

#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void STexture::_LoadEXR(const char* filename)
{
	TextureData res;
	res.name = filename;

	int inputChannelCount, overrideChannelCount, channelSize;

	// Read EXR file header
	Imf::InputFile file(filename);
	const Imf::Header& header = file.header();

	Imath::Box2i dw = header.dataWindow();
	res.width = dw.max.x - dw.min.x + 1;
	res.height = dw.max.y - dw.min.y + 1;

	const Imf::ChannelList& channels = header.channels();
	Imf::PixelType pixelType = channels.begin().channel().type;

	inputChannelCount = 0;
	auto pChannel = channels.begin();
	while (pChannel != channels.end())
	{
		if (pChannel.channel().type != pixelType)
			throw std::runtime_error("EXR CHANNELS HAVE DIFFERENT PIXEL TYPES");
		++inputChannelCount;
		++pChannel;
	}

	std::string ICCProfile;
	bool sRGBColorSpace = false;
	const Imf::IntAttribute* pColorSpace = header.findTypedAttribute<Imf::IntAttribute>("Exif:ColorSpace");
	if (pColorSpace)
	{
		sRGBColorSpace = pColorSpace->value() == 1 ? true : false;
	}

	if (sRGBColorSpace)
	{
		throw std::runtime_error("SRGB EXR image has not been implemented");
	}

	if (pixelType == Imf::UINT)  // 32-bit fix point
	{
		channelSize = 4;
		switch (inputChannelCount)
		{
		case 1:
			res.format = DXGI_FORMAT_R32_UINT;
			overrideChannelCount = 1;
			break;
		case 2:
			res.format = DXGI_FORMAT_R32G32_UINT;
			overrideChannelCount = 2;
			break;
		case 3:
			res.format = DXGI_FORMAT_R32G32B32_UINT;
			overrideChannelCount = 3;
			break;
		case 4:
			res.format = DXGI_FORMAT_R32G32B32A32_UINT;
			overrideChannelCount = 4;
			break;
		default:
			throw std::runtime_error("Unsupported number of channels in EXR file.");
			break;
		}
	}
	else if (pixelType == Imf::HALF)  // 16-bit float point
	{
		channelSize = 2;
		switch (inputChannelCount)
		{
		case 1:
			res.format = DXGI_FORMAT_R16_FLOAT;
			overrideChannelCount = 1;
			break;
		case 2:
			res.format = DXGI_FORMAT_R16G16_FLOAT;
			overrideChannelCount = 2;
			break;
		case 3:
			res.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			overrideChannelCount = 4;
			break;
		case 4:
			res.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			overrideChannelCount = 4;
			break;
		default:
			throw std::runtime_error("Unsupported number of channels in EXR file.");
			break;
		}
	}
	else if (pixelType == Imf::FLOAT)  // 32-bit float point
	{
		channelSize = 4;
		switch (inputChannelCount)
		{
		case 1:
			res.format = DXGI_FORMAT_R32_FLOAT;
			overrideChannelCount = 1;
			break;
		case 2:
			res.format = DXGI_FORMAT_R32G32_FLOAT;
			overrideChannelCount = 2;
			break;
		case 3:
			res.format = DXGI_FORMAT_R32G32B32_FLOAT;
			overrideChannelCount = 3;
			break;
		case 4:
			res.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			overrideChannelCount = 4;
			break;
		default:
			throw std::runtime_error("Unsupported number of channels in EXR file.");
			break;
		}
	}
	else
	{
		throw std::runtime_error("UNSUPPORTED PIXEL TYPE");
	}

	res.pixelSize = overrideChannelCount * channelSize;
	res.reserve(res.width * res.height * res.pixelSize);

	char* basePtr = static_cast<char*>(res.data())
		- dw.min.x * res.pixelSize
		- dw.min.y * res.width * res.pixelSize;

	Imf::FrameBuffer frameBuffer;

	int _offset = 0;

	// R Channel
	if (channels.findChannel("R"))
	{
		frameBuffer.insert("R", Imf::Slice(pixelType,
			basePtr + _offset * channelSize,    // Offset
			res.pixelSize,          // Stride
			res.width * res.pixelSize   // Row stride
		));
		++_offset;
	}

	// G Channel
	if (channels.findChannel("G"))
	{
		frameBuffer.insert("G", Imf::Slice(pixelType,
			basePtr + _offset * channelSize,
			res.pixelSize,
			res.width * res.pixelSize
		));
		++_offset;
	}

	// B Channel
	if (channels.findChannel("B"))
	{
		frameBuffer.insert("B", Imf::Slice(pixelType,
			basePtr + _offset * channelSize,
			res.pixelSize,
			res.width * res.pixelSize
		));
		++_offset;
	}

	// A Channel
	if (channels.findChannel("A"))
	{
		frameBuffer.insert("A", Imf::Slice(pixelType,
			basePtr + _offset * channelSize,
			res.pixelSize,
			res.width * res.pixelSize
		));
		++_offset;
	}

	// Y Channel
	if (channels.findChannel("Y"))
	{
		frameBuffer.insert("Y", Imf::Slice(pixelType,
			basePtr + _offset * channelSize,
			res.pixelSize,
			res.width * res.pixelSize
		));
		++_offset;
	}

	file.setFrameBuffer(frameBuffer);
	file.readPixels(dw.min.y, dw.max.y);

	m_textures.push_back(std::move(res));
}

void STexture::_LoadStbi(const char* filename)
{
	int width, height, pixelSize;
	uint8_t* rgb_image = stbi_load(filename, &width, &height, &pixelSize, 0);

	// Assume 8 bit sRGB RGB-A image
	TextureData res;
	res.width = width;
	res.height = height;
	res.pixelSize = 4;
	res.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	res.name = filename;
	res.reserve(width * height * 4);
	char* basePtr = res.data();
	memset(basePtr, 255, res.size());
	for (uint32_t i = 0; i < res.width * res.height; ++i)
		memcpy_s(&basePtr[4 * i], pixelSize, &rgb_image[pixelSize * i], pixelSize);
	stbi_image_free(rgb_image);

	m_textures.push_back(std::move(res));
}

void STexture::AddTexture(std::string filename)
{
	m_textureFilenames.push_back(filename);
}

void STexture::LoadTextures()
{
	for (std::string filename : m_textureFilenames)
	{
		if (ends_with(filename, ".exr"))
		{
			_LoadEXR(filename.c_str());
		}
		else
		{
			_LoadStbi(filename.c_str());
		}
	}
}

void STexture::CopyToUploadHeap(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, DescHeapWrapper& hh)
{
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> tex_SRVCPUHandles;

	// Create upload heaps and data heaps
	// Schedule copy
	for (TextureData& tex : m_textures)
	{
		ComPtr<ID3D12Resource> uploadHeap;
		ComPtr<ID3D12Resource> dataHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE SRVCPUHandle;

		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.MipLevels = 0;  // 0: Auto calculate mipmaps level
		textureDesc.Format = tex.format;
		textureDesc.Width = tex.width;
		textureDesc.Height = tex.height;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&dataHeap)));
		dataHeap->SetName((LPCWSTR)tex.name.c_str());

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(dataHeap.Get(), 0, 1);
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadHeap)));

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = tex.data();
		textureData.RowPitch = tex.width * tex.pixelSize;
		textureData.SlicePitch = textureData.RowPitch * tex.height;

		SRVCPUHandle = hh.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = dataHeap->GetDesc().MipLevels;
		device->CreateShaderResourceView(dataHeap.Get(), &srvDesc, SRVCPUHandle);
		m_SRVsSeparated.push_back(hh.CopyDescriptorsToGPUHeap(1, SRVCPUHandle));

		UpdateSubresources(cmdList, dataHeap.Get(), uploadHeap.Get(), 0, 0, 1, &textureData);
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dataHeap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		m_textureResources.push_back(std::move(dataHeap));
		m_uploadHeaps.push_back(std::move(uploadHeap));
		tex_SRVCPUHandles.push_back(SRVCPUHandle);
	}

	// Upload descriptors
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle;
	hh.AllocateGPUDescriptors(tex_SRVCPUHandles.size(), CPUHandle, m_SRVCombined);
	for (auto& handle : tex_SRVCPUHandles)
	{
		device->CopyDescriptorsSimple(1, CPUHandle, handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		CPUHandle.Offset(1, hh.GetDescriptorSizeCBV_SRV_UAV());
	}
}

void STexture::ReleaseUploadHeaps()
{
	m_uploadHeaps.clear();
}

void STexture::ReleaseCPUData()
{
	m_textures.clear();
}

void STexture::ReleaseGPUData()
{
	m_SRVCombined.ptr = 0;
	m_SRVsSeparated.clear();
	m_textureResources.clear();
}
