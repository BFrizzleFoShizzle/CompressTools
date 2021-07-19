
#include "WaveletEncodeLayer.h"

#include <iostream>
#include <assert.h>

WaveletEncodeLayer::WaveletEncodeLayer(std::vector<uint16_t> values, uint32_t width, uint32_t height)
    : size(width, height)
{
    assert(values.size() == width * height);
    //  Initialize + prealloc memory
    wavelets.resize(size.GetWaveletCount());
    uint32_t parentReserveCount = size.GetParentSize().GetPixelCount();
    parentVals.resize(parentReserveCount);

    auto currWavelet = wavelets.begin();

    for (int32_t y = 0; y < height; y += 2)
    {
        for (int32_t x = 0; x < width; x += 2)
        {
            // Bilinear wavelet
            // Parent vals:
            // X O X O
            // O O O O
            // X O X O
            // O O O O
            // Step 1: Encode diagonals by averaging parents (X pattern, usually 4 vals)
            // X O X O
            // O D O D
            // X O X O
            // O D O D
            // Step 2: Encode vertical/horizontal using parents + diag (+ pattern, usually 4 vals)
            // X D X D
            // D X D X
            // X D X D
            // D X D X
            
            int32_t parentX = x / 2;
            int32_t parentY = y / 2;

            // Top left is guaranteed
            uint16_t TL = values[y * width + x];

            // Parent transform is TL, so no wavelet needed
            parentVals[parentY * size.GetParentWidth() + parentX] = TL;

            // Diagonal gets decoded first
            // X-shaped averaging
            if (x + 1 < width && y + 1 < height)
            {
                // TL
                uint32_t prediction = TL;
                uint32_t predictionCount = 1;

                // TR
                if (x + 2 < width)
                {
                    prediction += values[y * width + x + 2];
                    ++predictionCount;
                }

                // BL
                if (y + 2 < height)
                {
                    prediction += values[(y + 2) * width + x];
                    ++predictionCount;
                }

                // BR
                if (x + 2 < width && y + 2 < height)
                {
                    prediction += values[(y + 2) * width + x + 2];
                    ++predictionCount;
                }

                // fix rounding
                // 1 = +0
                // 2-3 = +1
                // 4 = +2
                prediction += predictionCount / 2;

                // average
                prediction = prediction / predictionCount;

                *currWavelet = values[(y + 1) * width + x + 1] - prediction;
                ++currWavelet;
            }

            // simplified from decoder since we have all the leaf data
            // +-shaped averaging
            if (x + 1 < width)
            {
                // Left
                uint32_t prediction = TL;
                uint32_t predictionCount = 1;

                // Right
                if (x + 2 < width)
                {
                    prediction += values[y * width + x + 2];
                    ++predictionCount;
                }

                // Top
                if (y - 1 > 0)
                {
                    prediction += values[(y - 1) * width + x + 1];
                    ++predictionCount;
                }

                // Bottom
                if (y + 1 < height)
                {
                    prediction += values[(y + 1) * width + x + 1];
                    ++predictionCount;
                }

                // fix rounding
                prediction += predictionCount / 2;

                // average
                prediction = prediction / predictionCount;

                *currWavelet = values[y * width + x + 1] - prediction;
                ++currWavelet;
            }

            // simplified from decoder since we have all the leaf data
            // +-shaped averaging
            if (y + 1 < height)
            {
                // Top
                uint32_t prediction = TL;
                uint32_t predictionCount = 1;

                // Bottom
                if (y + 2 < height)
                {
                    prediction += values[(y + 2) * width + x];
                    ++predictionCount;
                }

                // Left
                if (x - 1 > 0)
                {
                    prediction += values[(y + 1) * width + (x - 1)];
                    ++predictionCount;
                }

                // right
                if (x + 1 < width)
                {
                    prediction += values[(y + 1) * width + x + 1];
                    ++predictionCount;
                }

                // fix rounding
                prediction += predictionCount / 2;

                // average
                prediction = prediction / predictionCount;

                *currWavelet = values[(y + 1) * width + x] - prediction;
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
