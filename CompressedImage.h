#pragma once
#include <vector>
#include <memory>
#include <map>

#include "RansEncode.h"
#include "CompressedImageBlock.h"

struct CompressedImageHeader
{
    static const uint16_t CURR_VERSION = 0x0002;
    CompressedImageHeader()
    {

    }
    CompressedImageHeader(uint32_t width, uint32_t height)
        : width(width), height(height)
    {}
    bool IsCorrect()
    {
        return MAGIC == 0xFEDF && version == CURR_VERSION;
    }
    // Header header
    uint16_t MAGIC = 0xFEDF;
    uint16_t version = CURR_VERSION;

    // required metadata for wavelet encoder
    uint32_t width;
    uint32_t height;
    uint32_t blockSize;
    size_t blockBodyStart;
};

class CompressedImage
{
public:
    // TODO remove
    CompressedImage() {};
    CompressedImage(const std::vector<uint16_t>& values, size_t width, size_t height, size_t blockSize);
    static std::shared_ptr<CompressedImage> Deserialize(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> Serialize();
    std::vector<uint16_t> GetBottomLevelPixels();

    uint16_t GetPixel(size_t x, size_t y);

private:
    CompressedImageHeader header;
    std::map<std::pair<uint32_t, uint32_t>, std::shared_ptr<CompressedImageBlock>> compressedImageBlocks;
    SymbolCountDict globalSymbolCounts;
};