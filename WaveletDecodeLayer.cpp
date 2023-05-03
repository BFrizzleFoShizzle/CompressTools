
#include "WaveletDecodeLayer.h"
#include "Release_Assert.h"

WaveletDecodeLayer::WaveletDecodeLayer(const std::vector<symbol_t>& wavelets, const std::vector<symbol_t>& parentVals, uint32_t width, uint32_t height)
    : size(width, height) 
{
    pixelVals.resize(size.GetWidth() * size.GetHeight());

    auto currWavelet = wavelets.begin();

    // TODO this can be rearranged to run faster
    for (int32_t y = 0; y < size.GetHeight(); y += 2)
    {
        for (int32_t x = 0; x < size.GetWidth(); x += 2)
        {
            // Bilinear wavelet
            // Parent vals:
            // X O X O
            // O O O O
            // X O X O
            // O O O O
            // Step 1: Decode diagonals by averaging parents (X pattern, usually 4 vals)
            // X O X O
            // O D O D
            // X O X O
            // O D O D
            // Step 2: Decode vertical/horizontal using parents + diag (+ pattern, usually 4 vals)
            // X D X D
            // D X D X
            // X D X D
            // D X D X

            int32_t parentX = x / 2;
            int32_t parentY = y / 2;

            // Top left is guaranteed
            symbol_t TL = parentVals[parentY * size.GetParentWidth() + parentX];

            // Parent transform is TL, so no wavelet needed
            pixelVals[y * size.GetWidth() + x] = TL;

            // for prediction values shared between outputs
            // number of values used in prediction
            uint32_t predictionCountBase = 1;
            uint32_t predictionBase = TL;

            // DIAGONAL PREDICION - average up to 4 parent values
            // X-shaped pattern of parent values
            if (x + 1 < size.GetWidth() && y + 1 < size.GetHeight())
            {
                uint32_t predictionCount = predictionCountBase;
                uint32_t predicted = predictionBase;

                // Add TR parent if possible
                if (parentX + 1 < size.GetParentWidth())
                {
                    predicted += parentVals[parentY * size.GetParentWidth() + parentX + 1];
                    ++predictionCount;
                }

                // Add BL parent if possible
                if (parentY + 1 < size.GetParentHeight())
                {
                    predicted += parentVals[(parentY + 1) * size.GetParentWidth() + parentX];
                    ++predictionCount;
                    // Add BR parent if possible
                    if (parentX + 1 < size.GetParentWidth())
                    {
                        predicted += parentVals[(parentY + 1) * size.GetParentWidth() + parentX + 1];
                        ++predictionCount;
                    }
                }

                // fix rounding
                // 1 = +0
                // 2-3 = +1
                // 4 = +2
                predicted += predictionCount / 2;

                // average
                predicted = predicted / predictionCount;

                // add wavelet to get final value
                symbol_t outputVal = predicted + *currWavelet;
                pixelVals[(y + 1) * size.GetWidth() + x + 1] = outputVal;
                ++currWavelet;

                // Diag is used as input to other output predictions
                predictionBase += outputVal;
                predictionCountBase += 1;
            }
            
            // TOP RIGHT prediction - uses output of previous block decodes + parent vals + freshly-decoded diagonal
            // predictionBase contains this nodes left + bottom, need to add top + right
            // +-shaped pattern of parent values
            if (x + 1 < size.GetWidth())
            {
                uint32_t predictionCount = predictionCountBase;
                uint32_t predicted = predictionBase;

                // Add right parent if possible
                if (parentX + 1 < size.GetParentWidth())
                {
                    predicted += parentVals[parentY * size.GetParentWidth() + parentX + 1];
                    ++predictionCount;
                }

                // Add top (diag of above block) if possible
                if (y - 1 > 0)
                {
                    predicted += pixelVals[(y - 1) * size.GetHeight() + x + 1];
                    ++predictionCount;
                }

                // fix rounding
                predicted += predictionCount / 2;

                // average
                predicted = predicted / predictionCount;

                pixelVals[y * size.GetWidth() + x + 1] = predicted + *currWavelet;
                ++currWavelet;
            }

            // BOTTOM-LEFT prediction - uses output of previous block decodes + parent vals + freshly-decoded diagonal
            // predictionBase contains this nodes top + right, need to add left + bottom
            // +-shaped pattern of parent values
            if (y + 1 < size.GetHeight())
            {
                uint32_t predictionCount = predictionCountBase;
                uint32_t predicted = predictionBase;

                // Add bottom parent if possible
                if (parentY + 1 < size.GetParentHeight())
                {
                    predicted += parentVals[(parentY + 1) * size.GetParentWidth() + parentX];
                    ++predictionCount;
                }

                // Add left (diag of previous block) if possible
                if (x - 1 > 0)
                {
                    predicted += pixelVals[(y + 1) * size.GetWidth() + (x - 1)];
                    ++predictionCount;
                }

                // fix rounding
                predicted += predictionCount / 2;

                // average
                predicted = predicted / predictionCount;

                pixelVals[(y + 1) * size.GetWidth() + x] = predicted + *currWavelet;
                ++currWavelet;
            }
        }
    }
}


std::vector<symbol_t> WaveletDecodeLayer::GetPixels() const
{
    return pixelVals;
}

// does inverse parent transform to get parent values
std::vector<symbol_t> WaveletDecodeLayer::GetParentLevelPixels(uint32_t level) const
{
    WaveletLayerSize targetSize = size;
    for (int i = 0; i < level; ++i)
        targetSize = targetSize.GetParentSize();

    std::vector<symbol_t> parentVals;
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

symbol_t WaveletDecodeLayer::GetPixelAt(uint32_t x, uint32_t y) const
{
    return pixelVals[y * GetWidth() + x];
}

bool WaveletDecodeLayer::IsRoot() const
{
    return size.IsRoot();
}

size_t WaveletDecodeLayer::GetMemoryFootprint() const
{
    return sizeof(WaveletDecodeLayer) + (pixelVals.capacity() * sizeof(pixelVals[0]));
}