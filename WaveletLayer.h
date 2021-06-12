#pragma once

#include <vector>
#include <memory>

class WaveletLayerSize
{
public:
    WaveletLayerSize(uint32_t width, uint32_t height);

    // TODO check
    uint32_t GetParentWidth() const;
    uint32_t GetParentHeight() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWaveletCount() const;
    bool IsRoot() const;

private:
    // TODO these can be 16-bit
    uint32_t width;
    uint32_t height;

};

class WaveletLayer
{
public:
    WaveletLayer(std::vector<uint16_t> data, uint32_t width, uint32_t height);
    WaveletLayer(const std::vector<uint16_t>& pyramidWavelets, std::vector<uint16_t> rootParentVals, uint32_t width, uint32_t height);
    WaveletLayer(std::shared_ptr<WaveletLayer> parent, const std::vector<uint16_t>& layerWavelets, uint32_t width, uint32_t height);

    uint16_t DecodeAt(uint32_t x, uint32_t y) const;
    std::vector<uint16_t> DecodeLayer() const;
    // TODO is this really const?
    const std::vector<uint16_t> GetWavelets() const;
    const std::vector<uint16_t> GetParentVals() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWaveletCount() const;
    std::shared_ptr<WaveletLayer> GetParentLayer() const;

private:
    bool IsRoot() const;
    WaveletLayerSize size;
    std::vector<uint16_t> wavelets;
    std::vector<uint16_t> parentVals;
    std::shared_ptr<WaveletLayer> parent;
};