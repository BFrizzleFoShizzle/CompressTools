#pragma once
#include <vector>
#include <memory>

#include "RansEncode.h"
#include "WaveletLayer.h"

struct CompressedImageHeader
{
    CompressedImageHeader()
    {

    }
    CompressedImageHeader(uint32_t width, uint32_t height, uint64_t finalRansState)
        : width(width), height(height), finalRansState(finalRansState)
    {}
    bool IsCorrect()
    {
        return MAGIC == 0xFEDF && version == 0x001;
    }
    // Header header
    uint16_t MAGIC = 0xFEDF;
    uint16_t version = 0x0001;

    // required metadata for wavelet encoder
    uint32_t width;
    uint32_t height;

    // rANS metadata
    uint64_t finalRansState;
};


class CompressedImage
{
public:
    CompressedImage(std::shared_ptr<WaveletLayer> waveletPyramidBottom);
    static std::shared_ptr<CompressedImage> Deserialize(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> Serialize();
    std::vector<uint16_t> GetBottomLevelPixels();

private:
    CompressedImageHeader header;
    std::shared_ptr<WaveletLayer> waveletPyramidBottom;
};