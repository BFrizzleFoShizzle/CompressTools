#pragma once

#include <vector>
#include <memory>

class WaveletLayer
{
public:
    WaveletLayer(std::vector<uint16_t> data, uint32_t width, uint32_t height);
    WaveletLayer(const std::vector<uint16_t>& pyramidWavelets, std::vector<uint16_t> rootParentVals, uint32_t width, uint32_t height);

    uint16_t DecodeAt(uint32_t x, uint32_t y) const;
    std::vector<uint16_t> DecodeLayer() const;
    // TODO is this really const?
    const std::vector<uint16_t> GetWavelets() const;
    const std::vector<uint16_t> GetParentVals() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    std::shared_ptr<WaveletLayer> GetParentLayer() const;

private:
    // TODO check
    uint32_t GetParentWidth() const;
    uint32_t GetParentHeight() const;
    bool IsRoot() const;
    // TODO these can be 16-bit
    uint32_t width;
    uint32_t height;
    std::vector<uint16_t> wavelets;
    std::vector<uint16_t> parentVals;
    std::shared_ptr<WaveletLayer> parent;
};