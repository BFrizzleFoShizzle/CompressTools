#pragma once

#include "WaveletLayerCommon.h"

class WaveletEncodeLayer
{
public:
    WaveletEncodeLayer(std::vector<uint16_t> data, uint32_t width, uint32_t height);
    WaveletEncodeLayer(const std::vector<uint16_t>& pyramidWavelets, std::vector<uint16_t> rootParentVals, uint32_t width, uint32_t height);

    std::vector<uint16_t> DecodeLayer() const;
    // TODO is this really const?
    const std::vector<uint16_t> GetWavelets() const;
    const std::vector<uint16_t> GetParentVals() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWaveletCount() const;
    std::shared_ptr<WaveletEncodeLayer> GetParentLayer() const;

private:
    
    bool IsRoot() const;
    WaveletLayerSize size;
    std::vector<uint16_t> wavelets;
    std::vector<uint16_t> parentVals;
    std::shared_ptr<WaveletEncodeLayer> parent;
};
