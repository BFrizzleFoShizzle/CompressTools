#include "CompressToolsLib.h"
#include "CompressedImage.h"
#include "Logging.h"

#include <fstream>
#include <vector>
#include <Windows.h>
#include <sstream>
#include <mutex>

using namespace CompressToolsLib;

struct CompressToolsLib::CompressedImageFile
{
	std::string filename;
	std::shared_ptr<CompressedImage> image;
	// TODO REMOVE AFTER TESTING used if preloading
	std::vector<symbol_t> decodedPixels;
	std::mutex lock;
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
	image->lock.lock();
	symbol_t val;
	// HACK if preloading use preloaded cache
	if(image->decodedPixels.size() > 0)
		val = image->decodedPixels[y * image->image->GetWidth() + x];
	else
		val = image->image->GetPixel(x, y);
	image->lock.unlock();
	return val;
}

__declspec(dllexport) void CompressToolsLib::CloseImage(CompressedImageFileHdl image)
{
	delete image;
}

__declspec(dllexport) void CompressToolsLib::SetLoggers(void(*debugLogger)(const char*), void(*errorLogger)(const char*))
{
	SetDebugLogger(debugLogger);
	SetErrorLogger(errorLogger);
}

__declspec(dllexport) uint32_t CompressToolsLib::GetImageWidthInBlocks(CompressedImageFileHdl image)
{
	image->lock.lock();
	uint32_t val = image->image->GetWidthInBlocks();
	image->lock.unlock();
	return val;
}

__declspec(dllexport) uint32_t CompressToolsLib::GetImageHeightInBlocks(CompressedImageFileHdl image)
{
	image->lock.lock();
	uint32_t val = image->image->GetHeightInBlocks();
	image->lock.unlock();
	return val;
}

// outputs w
__declspec(dllexport) void CompressToolsLib::GetBlockLODs(CompressedImageFileHdl image, uint8_t* output)
{
	image->lock.lock();
	std::vector<uint8_t> blockLevels = image->image->GetBlockLevels();
	image->lock.unlock();
	memcpy(output, &blockLevels[0], sizeof(uint8_t) * blockLevels.size());
}

__declspec(dllexport) uint32_t CompressToolsLib::GetMaxLOD(CompressedImageFileHdl image)
{
	image->lock.lock();
	uint32_t val = image->image->GetTopLOD();
	image->lock.unlock();
	return val;
}

__declspec(dllexport) size_t CompressToolsLib::GetMemoryUsage(CompressedImageFileHdl image)
{
	image->lock.lock();
	size_t val = image->image->GetMemoryUsage();
	image->lock.unlock();
	return val;
}

__declspec(dllexport) void CompressToolsLib::GetBottomPixels(CompressedImageFileHdl image, uint16_t* values)
{
	image->lock.lock();
	std::vector<symbol_t> vals = image->image->GetBottomLevelPixels();
	memcpy(values, &vals[0], sizeof(symbol_t) * vals.size());
	image->lock.unlock();
	return;
}

