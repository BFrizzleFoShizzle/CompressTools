
#include "WaveletDecodeLayer.h"
#include <iostream>
#include <assert.h>

WaveletDecodeLayer::WaveletDecodeLayer(const std::vector<uint16_t>& wavelets, const std::vector<uint16_t>& parentVals, uint32_t width, uint32_t height)
    : size(width, height) 
{
    pixelVals.resize(size.GetWidth() * size.GetHeight());

    auto currWavelet = wavelets.begin();

    // TODO this can be rearranged to run faster
    for (uint32_t y = 0; y < size.GetHeight(); y += 2)
    {
        for (uint32_t x = 0; x < size.GetWidth(); x += 2)
        {
            uint32_t parentX = x / 2;
            uint32_t parentY = y / 2;
            // get prediction/parent value
            uint16_t parent = parentVals[parentY * size.GetParentWidth() + parentX];
            uint16_t predicted = parent;

            // Parent transform is TL, so no wavelet needed
            pixelVals[y * size.GetWidth() + x] = predicted;

            // add wavelet to get final value
            if (x + 1 < size.GetWidth())
            {
                pixelVals[y * size.GetWidth() + x + 1] = predicted + *currWavelet;
                ++currWavelet;
            }

            if (y + 1 < size.GetHeight())
            {
                pixelVals[(y + 1) * size.GetWidth() + x] = predicted + *currWavelet;
                ++currWavelet;
            }

            if (x + 1 < size.GetWidth() && y + 1 < size.GetHeight())
            {
                pixelVals[(y + 1) * size.GetWidth() + x + 1] = predicted + *currWavelet;
                ++currWavelet;
            }
        }
    }
}


std::vector<uint16_t> WaveletDecodeLayer::GetPixels() const
{
    return pixelVals;
}

// does inverse parent transform to get parent values
std::vector<uint16_t> WaveletDecodeLayer::GetParentLevelPixels(uint32_t level) const
{
    WaveletLayerSize targetSize = size;
    for (int i = 0; i < level; ++i)
        targetSize = targetSize.GetParentSize();

    std::vector<uint16_t> parentVals;
    parentVals.resize(targetSize.GetPixelCount());
    size_t shift = 1 << level;
    for (uint32_t y = 0; y < targetSize.GetHeight(); ++y)
    {
        for (uint32_t x = 0; x < targetSize.GetWidth(); ++x)
        {
            uint32_t parentX = x << level;
            uint32_t parentY = y << level;
            parentVals[y * targetSize.GetWidth() + x] = pixelVals[parentY * GetWidth() + parentX];
        }
    }

    return parentVals;
}

uint32_t WaveletDecodeLayer::GetWidth() const
{
    return size.GetWidth();
}

uint32_t WaveletDecodeLayer::GetHeight() const
{
    return size.GetHeight();
}

uint16_t WaveletDecodeLayer::GetPixelAt(uint32_t x, uint32_t y) const
{
    return pixelVals[y * GetWidth() + x];
}

bool WaveletDecodeLayer::IsRoot() const
{
    return size.IsRoot();
}
