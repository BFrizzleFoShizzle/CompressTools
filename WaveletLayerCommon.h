#pragma once

#include <stdint.h>

class WaveletLayerSize
{
public:
    WaveletLayerSize(uint32_t width, uint32_t height);

    // TODO check
    uint32_t GetParentWidth() const;
    uint32_t GetParentHeight() const;
    WaveletLayerSize GetParentSize() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetPixelCount() const;
    uint32_t GetWaveletCount() const;
    bool IsRoot() const;
    WaveletLayerSize GetRoot() const;

private:
    // TODO these can be 16-bit
    uint32_t width;
    uint32_t height;

};
