#pragma once

#include "stdafx.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

std::vector<uint8_t> LoadBytecodeFromFile(const char* name);

// String endswith function
inline bool ends_with(std::string const& value, std::string const& ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

// Aligns a value to the nearest multiple of 256 (for constant buffers)
inline UINT AlignAs256(UINT size) {
	const UINT alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256 bytes
	return (size + alignment - 1) & ~(alignment - 1);
}

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
    if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args ...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}