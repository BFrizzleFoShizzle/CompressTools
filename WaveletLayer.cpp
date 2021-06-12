
#include "WaveletLayer.h"
#include <iostream>
#include <assert.h>

WaveletLayer::WaveletLayer(std::vector<uint16_t> values, uint32_t width, uint32_t height)
    : size(width, height)
{
    assert(values.size() == width * height);
    //  Initialize + prealloc memory
    wavelets.resize(size.GetWaveletCount());
    uint32_t parentReserveCount = size.GetParentHeight() * size.GetParentWidth();
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
            parentVals[parentY * size.GetParentWidth() + parentX] = average;

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
    if (!IsRoot())
    {
        std::cout << "Processing parent..." << std::endl;
        parent = std::make_shared<WaveletLayer>(parentVals, size.GetParentWidth(), size.GetParentHeight());
    }
}


WaveletLayer::WaveletLayer(const std::vector<uint16_t> &pyramidWavelets, std::vector<uint16_t> rootParentVals, uint32_t width, uint32_t height)
    : wavelets(), size(width, height)
{
    // reconstruct parent tree
    if (!IsRoot())
    {
        std::cout << "Processing parent..." << std::endl;
        parent = std::make_shared<WaveletLayer>(pyramidWavelets, rootParentVals, size.GetParentWidth(), size.GetParentHeight());
    }
    else
    {
        // We're the root, parent vals are for us
        parentVals = rootParentVals;
    }

    // sum of wavelets used by parent layers
    // (used to get index of child wavelets)
    uint64_t parentWaveletCounts = 0;
    std::shared_ptr<WaveletLayer> parentLayer = parent;
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


WaveletLayer::WaveletLayer(std::shared_ptr<WaveletLayer> parent, const std::vector<uint16_t>& layerWavelets, uint32_t width, uint32_t height)
    : parent(parent), parentVals(parent->DecodeLayer()), size(width, height)
{
    // TODO std::move?
    wavelets = layerWavelets;
}

uint32_t WaveletLayer::GetWidth() const
{
    return size.GetWidth();
}

uint32_t WaveletLayer::GetHeight() const
{
    return size.GetHeight();
}

uint32_t WaveletLayer::GetWaveletCount() const
{
    return size.GetWaveletCount();
}

uint16_t WaveletLayer::DecodeAt(uint32_t x, uint32_t y) const
{
    uint32_t parentX = x / 2;
    uint32_t parentY = y / 2;
    uint16_t predicted = parentVals[parentY * size.GetParentWidth() + parentX];
    uint16_t wavelet = wavelets[y * size.GetWidth() + x];
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
            uint16_t predicted = parentVals[parentY * size.GetParentWidth() + parentX];

            // add wavelet to get final value
            output[y * size.GetWidth() + x] = predicted + currWavelet[y * size.GetWidth() + x];

            if (x + 1 < size.GetWidth())
                output[y * size.GetWidth() + x + 1] = predicted + wavelets[y * size.GetWidth() + x + 1];

            if (y + 1 < size.GetHeight())
                output[(y + 1) * size.GetWidth() + x] = predicted + wavelets[(y + 1) * size.GetWidth() + x];

            if (x + 1 < size.GetWidth() && y + 1 < size.GetHeight())
                output[(y + 1) * size.GetWidth() + x + 1] = predicted + wavelets[(y + 1) * size.GetWidth() + x + 1];
        }
    }
    return output;
}

bool WaveletLayer::IsRoot() const
{
    return size.IsRoot();
}

WaveletLayerSize::WaveletLayerSize(uint32_t width, uint32_t height)
    : width(width), height(height)
{

}

uint32_t WaveletLayerSize::GetHeight() const
{
    return height;
}

uint32_t  WaveletLayerSize::GetWidth() const
{
    return width;
}

uint32_t WaveletLayerSize::GetWaveletCount() const
{
    return width * height;
}

uint32_t WaveletLayerSize::GetParentHeight() const
{
    return (height + 1) / 2;
}

uint32_t  WaveletLayerSize::GetParentWidth() const
{
    return (width + 1) / 2;
}

bool WaveletLayerSize::IsRoot() const
{
    return !(GetParentWidth() > 1 && GetParentHeight() > 1);
}