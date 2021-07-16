#pragma once
#include <string>
#include <stdint.h>

// C-style interface to get around compiler differences...
// Hopefully this can be removed in the future...

namespace CompressToolsLib {
	enum ImageMode
	{
		Streaming,
		Preload
	};

	struct CompressedImageFile;
	typedef CompressedImageFile* CompressedImageFileHdl;

	// need to use C strings...
	__declspec(dllexport) CompressedImageFileHdl OpenImage(const char* filename, ImageMode mode = Streaming);
	__declspec(dllexport) uint16_t ReadHeightValue(CompressedImageFileHdl image, uint32_t x, uint32_t y);
	__declspec(dllexport) void CloseImage(CompressedImageFileHdl image);
	// for debugging
	__declspec(dllexport) void GetBlockLODs(CompressedImageFileHdl image, uint8_t* output);
	__declspec(dllexport) uint32_t GetImageWidthInBlocks(CompressedImageFileHdl image);
	__declspec(dllexport) uint32_t GetImageHeightInBlocks(CompressedImageFileHdl image);
	__declspec(dllexport) uint32_t GetMaxLOD(CompressedImageFileHdl image);
	__declspec(dllexport) size_t GetMemoryUsage(CompressedImageFileHdl image);
	// TOOD remove after testing?
	__declspec(dllexport) void GetBottomPixels(CompressedImageFileHdl image, uint16_t *values);
	__declspec(dllexport) bool IsHeightmapBusy(CompressedImageFileHdl image);
}
