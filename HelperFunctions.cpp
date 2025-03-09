#include "stdafx.h"
#include "HelperFunctions.h"

#include <stdexcept>
#include <vector>
using std::vector;

vector<uint8_t> LoadBytecodeFromFile(const char* name)
{
	FILE* File = fopen(name, "rb");
	//assert(File);
	fseek(File, 0, SEEK_END);
	long Size = ftell(File);
	if (Size <= 0)
	{
		//assert(0);
		return vector<uint8_t>();
	}
	vector<uint8_t> Content(Size);
	fseek(File, 0, SEEK_SET);
	fread(Content.data(), 1, Content.size(), File);
	fclose(File);
	return Content;
}

DXGI_FORMAT GetCompatableFormat(DXGI_FORMAT f, int& sRGB)
{
	switch (f)
	{
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	//case DXGI_FORMAT_R16G16B16A16_UNORM:
	//case DXGI_FORMAT_R16G16B16A16_UINT:
	//case DXGI_FORMAT_R16G16B16A16_SNORM:
	//case DXGI_FORMAT_R16G16B16A16_SINT:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;

	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	//case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		sRGB = 1;
		[[fallthrough]];
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	//case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	//case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	return DXGI_FORMAT_UNKNOWN;
}
