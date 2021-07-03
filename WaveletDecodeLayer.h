#pragma once

#include "WaveletLayerCommon.h"

class WaveletDecodeLayer
{
public:
    WaveletDecodeLayer(const std::vector<uint16_t>& wavelets, const std::vector<uint16_t> &parentVals, uint32_t width, uint32_t height);

    uint16_t GetPixelAt(uint32_t x, uint32_t y) const;
    // TODO is this actually const?
    std::vector<uint16_t> GetPixels() const;
    std::vector<uint16_t> GetParentLevelPixels(uint32_t level) const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    size_t GetMemoryFootprint() const;

private:

    bool IsRoot() const;
    WaveletLayerSize size;
    std::vector<uint16_t> pixelVals;
};