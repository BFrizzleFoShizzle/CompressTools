#pragma once

#include "WaveletLayerCommon.h"
#include "Precision.h"
#include <vector>
#include <memory>

class WaveletEncodeLayer
{
public:
    WaveletEncodeLayer(std::vector<symbol_t> data, uint32_t width, uint32_t height);

    std::vector<symbol_t> DecodeLayer() const;
    // TODO is this really const?
    const std::vector<symbol_t> GetWavelets() const;
    const std::vector<symbol_t> GetParentVals() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWaveletCount() const;
    std::shared_ptr<WaveletEncodeLayer> GetParentLayer() const;

private:
    bool IsRoot() const;
    WaveletLayerSize size;
    std::vector<symbol_t> wavelets;
    std::vector<symbol_t> parentVals;
    std::shared_ptr<WaveletEncodeLayer> parent;
};
