#include "stdafx.h"
#include "HelperFunctions.h"

#include <tga.h>
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