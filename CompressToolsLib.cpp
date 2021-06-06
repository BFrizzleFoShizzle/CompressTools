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
	std::vector<uint16_t> pixels;
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
	std::shared_ptr<CompressedImage> decodedImage = CompressedImage::Deserialize(fileBytes);
	std::vector<uint16_t> decodedPixels = decodedImage->GetBottomLevelPixels();

	// move data to struct
	MessageBoxA(0, "Generating struct...", "Debug", MB_OK);
	CompressedImageFileHdl imageHdl = new CompressedImageFile();
	imageHdl->filename = filename;
	imageHdl->pixels = std::move(decodedPixels);
	MessageBoxA(0, "Heightmap loaded!", "Debug", MB_OK);
	return imageHdl;
}

__declspec(dllexport) uint16_t CompressToolsLib::ReadHeightValue(CompressedImageFileHdl image, uint32_t pixelIndex)
{
	if (pixelIndex >= image->pixels.size())
	{
		//MessageBoxA(0, "Out-of-bounds pixel read!", "Debug", MB_OK);
		//std::stringstream msg;
		//msg << "Index: " << pixelIndex << " Size: " << image->pixels.size() << std::endl;
		//MessageBoxA(0, msg.str().c_str(), "Debug", MB_OK);
		return 0;
	}
	return image->pixels[pixelIndex];
}

__declspec(dllexport) void CompressToolsLib::CloseImage(CompressedImageFileHdl image)
{
	image->pixels.clear();
	delete image;
}
