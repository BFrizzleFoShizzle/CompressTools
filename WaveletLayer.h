#pragma once

#include <vector>

class WaveletLayer
{
public:
    WaveletLayer(std::vector<uint16_t> data, uint32_t width, uint32_t height);

    // TODO static?
    // TODO move to constructor?
    //void ProcessImageData(uint16_t* data, const uint32_t width, const uint32_t height);

    uint16_t DecodeAt(uint32_t x, uint32_t y) const;
    std::vector<uint16_t> DecodeLayer() const;
    WaveletLayer* GetParent() const;

private:
    // TODO check
    uint32_t GetParentWidth() const;
    uint32_t GetParentHeight() const;
    // TODO these can be 16-bit
    uint32_t width;
    uint32_t height;
    std::vector<uint16_t> wavelets;
    std::vector<uint16_t> parentVals;
    WaveletLayer* parent;
};