
#include "WaveletLayer.h"
#include <iostream>
#include <assert.h>

WaveletLayer::WaveletLayer(std::vector<uint16_t> values, uint32_t width, uint32_t height)
    : width(width), height(height)
{
    assert(values.size() == width * height);
    //  Initialize + prealloc memory
    wavelets.resize(width * height);
    uint32_t parentReserveCount = GetParentHeight() * GetParentWidth();
    parentVals.resize(parentReserveCount);

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

            // set parent value
            uint32_t parentX = x / 2;
            uint32_t parentY = y / 2;
            parentVals[parentY * GetParentWidth() + parentX] = average;

            // get wavelet values
            wavelets[y * width + x] = values[y * width + x] - average;
            if (x + 1 < width)
                wavelets[y * width + x + 1] = values[y * width + x + 1] - average;

            if (y + 1 < height)
                wavelets[(y + 1) * width + x] = values[(y + 1) * width + x] - average;

            if (x + 1 < width && y + 1 < height)
                wavelets[(y + 1) * width + x + 1] = values[(y + 1) * width + x + 1] - average;
        }
    }

    std::cout << "Level wavelets generated." << std::endl;
    //GetSymbolEntropy(layer->wavelets);

    // process parent layer
    assert(parentReserveCount == parentVals.size());
    if (GetParentWidth() > 1 && GetParentHeight() > 1)
    {
        std::cout << "Processing parent..." << std::endl;
        parent = std::make_shared<WaveletLayer>(parentVals, GetParentWidth(), GetParentHeight());
    }
}

WaveletLayer::WaveletLayer(std::vector<uint16_t> wavelets, std::vector<uint16_t> parentVals, uint32_t width, uint32_t height)
    : wavelets(wavelets), parentVals(parentVals), width(width), height(height), parent(nullptr)
{
    assert(wavelets.size() == width * height);
    assert(parentVals.size() == GetParentWidth() * GetParentHeight());
}

uint32_t WaveletLayer::GetParentWidth() const
{
    return (width + 1) / 2;
}

uint32_t WaveletLayer::GetParentHeight() const
{
    return (height + 1) / 2;
}


uint32_t WaveletLayer::GetWidth() const
{
    return width;
}

uint32_t WaveletLayer::GetHeight() const
{
    return height;
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

std::shared_ptr<WaveletLayer> WaveletLayer::GetParentLayer() const
{
    return parent;
}

const std::vector<uint16_t> WaveletLayer::GetParentVals() const
{
    return parentVals;
}

const std::vector<uint16_t> WaveletLayer::GetWavelets() const
{
    return wavelets;
}

std::vector<uint16_t> WaveletLayer::DecodeLayer() const
{
    std::vector<uint16_t> output;
    output.resize(width * height);

    auto currWavelet = wavelets.begin();

    // TODO this can be rearranged to run faster
    for (uint32_t y = 0; y < height; y += 2)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            uint32_t parentX = x / 2;
            uint32_t parentY = y / 2;
            // get prediction/parent value
            uint16_t predicted = parentVals[parentY * GetParentWidth() + parentX];

            // add wavelet to get final value
            output[y * width + x] = predicted + currWavelet[y * width + x];

            if (x + 1 < width)
                output[y * width + x + 1] = predicted + wavelets[y * width + x + 1];

            if (y + 1 < height)
                output[(y + 1) * width + x] = predicted + wavelets[(y + 1) * width + x];

            if (x + 1 < width && y + 1 < height)
                output[(y + 1) * width + x + 1] = predicted + wavelets[(y + 1) * width + x + 1];
        }
    }
    return output;
}