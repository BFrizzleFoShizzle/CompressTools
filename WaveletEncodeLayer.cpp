
#include "WaveletEncodeLayer.h"

#include <iostream>
#include <assert.h>

WaveletEncodeLayer::WaveletEncodeLayer(std::vector<uint16_t> values, uint32_t width, uint32_t height)
    : size(width, height)
{
    assert(values.size() == width * height);
    //  Initialize + prealloc memory
    wavelets.resize(size.GetWaveletCount());
    uint32_t parentReserveCount = size.GetPixelCount();
    parentVals.resize(parentReserveCount);

    auto currWavelet = wavelets.begin();

    for (uint32_t y = 0; y < height; y += 2)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            // set parent value
            uint32_t parentX = x / 2;
            uint32_t parentY = y / 2;
            uint16_t parent = values[y * width + x];
            uint16_t predicted = values[y * width + x];
            parentVals[parentY * size.GetParentWidth() + parentX] = parent;

            if (x + 1 < width)
            {
                *currWavelet = values[y * width + x + 1] - predicted;
                ++currWavelet;
            }

            if (y + 1 < height)
            {
                *currWavelet = values[(y + 1) * width + x] - predicted;
                ++currWavelet;
            }

            if (x + 1 < width && y + 1 < height)
            {
                *currWavelet = values[(y + 1) * width + x + 1] - predicted;
                ++currWavelet;
            }
        }
    }

    //GetSymbolEntropy(layer->wavelets);

    // process parent layer
    assert(parentReserveCount == parentVals.size());
    if (!IsRoot())
    {
        parent = std::make_shared<WaveletEncodeLayer>(parentVals, size.GetParentWidth(), size.GetParentHeight());
    }
}


WaveletEncodeLayer::WaveletEncodeLayer(const std::vector<uint16_t>& pyramidWavelets, std::vector<uint16_t> rootParentVals, uint32_t width, uint32_t height)
    : wavelets(), size(width, height)
{
    // reconstruct parent tree
    if (!IsRoot())
    {
        std::cout << "Processing parent..." << std::endl;
        parent = std::make_shared<WaveletEncodeLayer>(pyramidWavelets, rootParentVals, size.GetParentWidth(), size.GetParentHeight());
    }
    else
    {
        // We're the root, parent vals are for us
        parentVals = rootParentVals;
    }

    // sum of wavelets used by parent layers
    // (used to get index of child wavelets)
    uint64_t parentWaveletCounts = 0;
    std::shared_ptr<WaveletEncodeLayer> parentLayer = parent;
    while (parentLayer)
    {
        parentWaveletCounts += parentLayer->GetWaveletCount();
        parentLayer = parentLayer->GetParentLayer();
    }

    // copy layer wavelets
    uint64_t waveletsCount = GetWaveletCount();
    uint64_t readIdx = pyramidWavelets.size() - (parentWaveletCounts + waveletsCount);
    wavelets.insert(wavelets.end(), pyramidWavelets.begin() + readIdx, pyramidWavelets.begin() + readIdx + waveletsCount);

    if (!IsRoot())
    {
        std::cout << "Decoding parent... " << std::endl;
        parentVals = parent->DecodeLayer();
    }
}

uint32_t WaveletEncodeLayer::GetWidth() const
{
    return size.GetWidth();
}

uint32_t WaveletEncodeLayer::GetHeight() const
{
    return size.GetHeight();
}

uint32_t WaveletEncodeLayer::GetWaveletCount() const
{
    return size.GetWaveletCount();
}

std::shared_ptr<WaveletEncodeLayer> WaveletEncodeLayer::GetParentLayer() const
{
    return parent;
}

const std::vector<uint16_t> WaveletEncodeLayer::GetParentVals() const
{
    return parentVals;
}

const std::vector<uint16_t> WaveletEncodeLayer::GetWavelets() const
{
    return wavelets;
}

std::vector<uint16_t> WaveletEncodeLayer::DecodeLayer() const
{
    std::vector<uint16_t> output;
    output.resize(size.GetWidth() * size.GetHeight());

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
            output[y * size.GetWidth() + x] = predicted;

            // add wavelet to get final value
            if (x + 1 < size.GetWidth())
            {
                output[y * size.GetWidth() + x + 1] = predicted + *currWavelet;
                ++currWavelet;
            }

            if (y + 1 < size.GetHeight())
            {
                output[(y + 1) * size.GetWidth() + x] = predicted + *currWavelet;
                ++currWavelet;
            }

            if (x + 1 < size.GetWidth() && y + 1 < size.GetHeight())
            {
                output[(y + 1) * size.GetWidth() + x + 1] = predicted + *currWavelet;
                ++currWavelet;
            }
        }
    }
    return output;
}

bool WaveletEncodeLayer::IsRoot() const
{
    return size.IsRoot();
}
