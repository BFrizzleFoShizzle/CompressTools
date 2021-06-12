#pragma once
#include <string>

// C-style interface to get around compiler differences...
// Hopefully this can be removed in the future...

namespace CompressToolsLib {
	struct CompressedImageFile;
	typedef CompressedImageFile* CompressedImageFileHdl;

	// need to use C strings...
	__declspec(dllexport) CompressedImageFileHdl OpenImage(const char* filename);
	__declspec(dllexport) uint16_t ReadHeightValue(CompressedImageFileHdl image, uint32_t x, uint32_t y);
	__declspec(dllexport) void CloseImage(CompressedImageFileHdl image);
}
