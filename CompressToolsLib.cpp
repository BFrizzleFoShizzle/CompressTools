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
	// TODO REMOVE AFTER TESTING used if preloading
	std::vector<uint16_t> decodedPixels;
};

__declspec(dllexport) CompressedImageFileHdl CompressToolsLib::OpenImage(const char* filename, ImageMode mode)
{
	// decode
	CompressedImageFileHdl imageHdl = new CompressedImageFile();
	imageHdl->filename = filename;
	imageHdl->image = CompressedImage::OpenStream(filename);
	if (mode == ImageMode::Preload)
		imageHdl->decodedPixels = imageHdl->image->GetBottomLevelPixels();
	// TODO temporary - delete this later
	float memoryUsage = (imageHdl->image->GetMemoryUsage() / 1024) / 1024.0f;
	std::cout << "Heightmap memory usage: " << memoryUsage << "MB" << std::endl;
	// free up memory
	imageHdl->image->ClearBlockCache();
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
	uint16_t val;
	// HACK if preloading use preloaded cache
	if(image->decodedPixels.size() > 0)
		val = image->decodedPixels[y * image->image->GetWidth() + x];
	else
		val = image->image->GetPixel(x, y);
	return val;
}

__declspec(dllexport) void CompressToolsLib::CloseImage(CompressedImageFileHdl image)
{
	delete image;
}

__declspec(dllexport) uint32_t CompressToolsLib::GetImageWidthInBlocks(CompressedImageFileHdl image)
{
	uint32_t val = image->image->GetWidthInBlocks();
	return val;
}

__declspec(dllexport) uint32_t CompressToolsLib::GetImageHeightInBlocks(CompressedImageFileHdl image)
{
	uint32_t val = image->image->GetHeightInBlocks();
	return val;
}

// outputs w
__declspec(dllexport) void CompressToolsLib::GetBlockLODs(CompressedImageFileHdl image, uint8_t* output)
{
	std::vector<uint8_t> blockLevels = image->image->GetBlockLevels();
	memcpy(output, &blockLevels[0], sizeof(uint8_t) * blockLevels.size());
}

__declspec(dllexport) uint32_t CompressToolsLib::GetMaxLOD(CompressedImageFileHdl image)
{
	uint32_t val = image->image->GetTopLOD();
	return val;
}

__declspec(dllexport) size_t CompressToolsLib::GetMemoryUsage(CompressedImageFileHdl image)
{
	size_t val = image->image->GetMemoryUsage();
	return val;
}

__declspec(dllexport) void CompressToolsLib::GetBottomPixels(CompressedImageFileHdl image, uint16_t* values)
{
	std::vector<uint16_t> vals = image->image->GetBottomLevelPixels();
	memcpy(values, &vals[0], sizeof(uint16_t) * vals.size());
	return;
}

