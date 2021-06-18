#include "CompressToolsLib.h"
#include "CompressedImage.h"

#include <fstream>
#include <vector>
#include <Windows.h>
#include <sstream>

using namespace CompressToolsLib;

struct CompressToolsLib::CompressedImageFile
{
	std::string filename;
	std::shared_ptr<CompressedImage> image;
};

__declspec(dllexport) CompressedImageFileHdl CompressToolsLib::OpenImage(const char* filename)
{
	MessageBoxA(0, "Loading heightmap...", "Debug", MB_OK);
	std::ifstream compressedFile(filename, std::ios::binary | std::ios::ate);
	if (!compressedFile.is_open())
		MessageBoxA(0, "Error opening heightmap!", "Debug", MB_OK);

	// get size of file
	size_t numBytes = compressedFile.tellg();
	compressedFile.seekg(0, std::ios::beg);

	// read bytes
	MessageBoxA(0, "Reading...", "Debug", MB_OK);
	std::vector<uint8_t> fileBytes;
	fileBytes.resize(numBytes);
	compressedFile.read(reinterpret_cast<char*>(&fileBytes[0]), numBytes);
	compressedFile.close();

	// decode
	MessageBoxA(0, "Decoding...", "Debug", MB_OK);
	CompressedImageFileHdl imageHdl = new CompressedImageFile();
	imageHdl->filename = filename;
	imageHdl->image = CompressedImage::Deserialize(fileBytes);

	MessageBoxA(0, "Heightmap loaded!", "Debug", MB_OK);
	return imageHdl;
}

__declspec(dllexport) uint16_t CompressToolsLib::ReadHeightValue(CompressedImageFileHdl image, uint32_t x, uint32_t y)
{
	if (x >= image->image->GetWidth()
		|| y >= image->image->GetHeight())
	{
		//MessageBoxA(0, "Out-of-bounds pixel read!", "Debug", MB_OK);
		//std::stringstream msg;
		//msg << "Index: " << pixelIndex << " Size: " << image->pixels.size() << std::endl;
		//MessageBoxA(0, msg.str().c_str(), "Debug", MB_OK);
		return 0;
	}
	return image->image->GetPixel(x, y);
}

__declspec(dllexport) void CompressToolsLib::CloseImage(CompressedImageFileHdl image)
{
	delete image;
}


__declspec(dllexport) uint32_t CompressToolsLib::GetImageWidthInBlocks(CompressedImageFileHdl image)
{
	return image->image->GetWidthInBlocks();
}

__declspec(dllexport) uint32_t CompressToolsLib::GetImageHeightInBlocks(CompressedImageFileHdl image)
{
	return image->image->GetHeightInBlocks();
}

// outputs w
__declspec(dllexport) void CompressToolsLib::GetBlockLODs(CompressedImageFileHdl image, uint8_t* output)
{
	std::vector<uint8_t> blockLevels = image->image->GetBlockLevels();
	memcpy(output, &blockLevels[0], sizeof(uint8_t) * blockLevels.size());
}