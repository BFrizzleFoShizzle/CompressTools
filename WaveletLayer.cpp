
#include "WaveletLayer.h"
#include <iostream>
#include <assert.h>

WaveletLayer::WaveletLayer(std::vector<uint16_t> values, uint32_t width, uint32_t height)
    : parent(nullptr), width(width), height(height)
{
    assert(values.size() == width * height);
    //  Initialize + prealloc memory
    wavelets.reserve(width * height);
    uint32_t parentReserveCount = GetParentHeight() * GetParentWidth();
    parentVals.reserve(parentReserveCount);

    std::cout << "Generating wavelets from layer..." << std::endl;

    for (uint32_t y = 0; y < height; y += 2)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            // get prediction/parent value
            uint32_t sum = values[y * width + x];
            uint32_t numVals = 1;
            if (x + 1 < width)
            {
                sum += values[y * width + x + 1];
                numVals += 1;
            }
            if (y + 1 < height)
            {
                sum += values[(y + 1) * width + x];
                numVals += 1;
            }
            if (x + 1 < width && y + 1 < height)
            {
                sum += values[(y + 1) * width + x + 1];
                numVals += 1;
            }
            // fix rounding
            // TODO test + improve this
            sum += numVals / 2;
            uint16_t average = (sum / numVals);
            parentVals.push_back(average);

            // get wavelet values
            wavelets.push_back(values[y * width + x] - average);
            if (x + 1 < width)
                wavelets.push_back(values[y * width + x + 1] - average);

            if (y + 1 < height)
                wavelets.push_back(values[(y + 1) * width + x] - average);

            if (x + 1 < width && y + 1 < height)
                wavelets.push_back(values[(y + 1) * width + x + 1] - average);
        }
    }

    std::cout << "Level wavelets generated." << std::endl;
    //GetSymbolEntropy(layer->wavelets);

    // process parent layer
    assert(parentReserveCount == parentVals.size());
    if (GetParentWidth() > 1 && GetParentHeight() > 1)
    {
        std::cout << "Processing parent..." << std::endl;

        // TODO shared_ptr/memory management?
        parent = new WaveletLayer(parentVals, GetParentWidth(), GetParentHeight());
    }
}

uint32_t WaveletLayer::GetParentWidth() const
{
    return (width + 1) / 2;
}

uint32_t WaveletLayer::GetParentHeight() const
{
    return (height + 1) / 2;
}

uint16_t WaveletLayer::DecodeAt(uint32_t x, uint32_t y) const
{
    uint32_t parentX = x / 2;
    uint32_t parentY = y / 2;
    uint16_t predicted = parentVals[parentY * GetParentWidth() + parentX];
    uint16_t wavelet = wavelets[y * width + x];
    uint16_t decoded = predicted + wavelet;
    return decoded;
}

WaveletLayer* WaveletLayer::GetParent() const
{
    return parent;
}


std::vector<uint16_t> WaveletLayer::DecodeLayer() const
{
    // TODO
    return std::vector<uint16_t>();
}