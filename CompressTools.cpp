// CompressTools.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define FREEIMAGE_LIB

#include <FreeImage.h>

#include <iostream>
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <cmath>

#include "RansEncode.h"

// TODO class
struct WaveletLayer
{
    WaveletLayer(uint32_t width, uint32_t height)
    {
        wavelets.reserve(width * height);
        // approximate
        uint32_t parentReserveCount = ((height + 1) / 2) * ((width + 1) / 2);
        parentVals.reserve(parentReserveCount);
        this->width = width;
        this->height = height;
    }
    uint32_t width;
    uint32_t height;
    std::vector<uint16_t> wavelets;
    std::vector<uint16_t> parentVals;
    WaveletLayer *parent;
};

SymbolCountDict GenerateSymbolCountDictionary(std::vector<uint16_t> symbols)
{
    SymbolCountDict symbolCounts;
    for (auto symbol : symbols)
    {
        symbolCounts.try_emplace(symbol, 0);
        symbolCounts[symbol] += 1;
    }
    return symbolCounts;
}

void GetSymbolEntropy(std::vector<uint16_t> symbols)
{
    std::cout << "Counting symbols..." << std::endl;
    std::unordered_map<uint16_t, uint64_t> symbolCounts;
    for (auto symbol : symbols)
    {
        symbolCounts.try_emplace(symbol, 0);
        symbolCounts[symbol] += 1;
    }
    std::cout << "Getting entropy..." << std::endl;
    uint64_t numSymbols = symbols.size();
    double totalEntropy = 0.0;
    std::vector<uint16_t> symbolValues;
    std::vector<double> symbolEntropies;
    
    for (auto symbolCount : symbolCounts)
    {
        //std::cout << symbolCount.first << " " << symbolCount.second << std::endl;
        symbolValues.push_back(symbolCount.first);
        double count = (double)symbolCount.second;
        double symbolProb = count / numSymbols;
        double symbolEntropy = count * -log2(symbolProb);
        symbolEntropies.push_back(symbolEntropy);
        totalEntropy += symbolEntropy;
    }

    std::cout << "Total entropy: " << (uint64_t)totalEntropy << " bits " << std::endl;
    std::cout << "Total entropy: " << (uint64_t)(totalEntropy/8) << " bytes " << std::endl;
}

WaveletLayer* ProcessImageData(uint16_t* data, const uint32_t width, const uint32_t height)
{
    std::cout << "Generating wavelets from layer..." << std::endl;

    // row-major
    WaveletLayer *layer = new WaveletLayer(width, height);
    for (uint32_t y = 0; y < height; y += 2)
    {
        for (uint32_t x = 0; x < width; x += 2)
        {
            // get prediction/parent value
            uint32_t sum = data[y * width + x];
            uint32_t numVals = 1;
            if (x + 1 < width)
            {
                sum += data[y * width + x + 1];
                numVals += 1;
            }
            if (y + 1 < height)
            {
                sum += data[(y + 1) * width + x];
                numVals += 1;
            }
            if (x + 1 < width && y + 1 < height)
            {
                sum += data[(y + 1) * width + x + 1];
                numVals += 1;
            }
            // fix rounding
            // TODO test + improve this
            sum += numVals / 2;
            uint16_t average = (sum / numVals);
            layer->parentVals.push_back(average);

            // get wavelet values
            layer->wavelets.push_back(data[y * width + x] - average);
            if (x + 1 < width)
                layer->wavelets.push_back(data[y * width + x + 1] - average);
            
            if (y + 1 < height)
                layer->wavelets.push_back(data[(y+1) * width + x] - average);

            if (x + 1 < width && y + 1 < height)
                layer->wavelets.push_back(data[(y+1) * width + x + 1] - average);
        }
    }
    std::cout << "Level wavelets generated." << std::endl;
    GetSymbolEntropy(layer->wavelets);

    uint32_t parentHeight = (height + 1) / 2;
    uint32_t parentWidth = (width + 1) / 2;
    uint32_t parentReserveCount = parentHeight * parentWidth;
    std::cout << parentReserveCount << " " << layer->parentVals.size() << std::endl;

    if (parentWidth > 1 && parentHeight > 1)
    {
        std::cout << "Processing parent..." << std::endl;
        WaveletLayer *parent = ProcessImageData(&layer->parentVals[0], parentWidth, parentHeight);
        layer->parent = parent;
    }
    return layer;
}

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char* message) {
    printf("\n*** ");
    if (fif != FIF_UNKNOWN) {
        printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
    }
    printf(message);
    printf(" ***\n");
}

int main()
{
    std::cout << "Opening image..." << std::endl;
    FreeImage_Initialise();
    FreeImage_SetOutputMessage(FreeImageErrorHandler);
    FIBITMAP *bitmap = FreeImage_Load(FREE_IMAGE_FORMAT::FIF_TIFF, "../../../fullmap.tif", TIFF_DEFAULT);
    
    int width = FreeImage_GetWidth(bitmap);
    int height = FreeImage_GetHeight(bitmap);
    int precision = FreeImage_GetBPP(bitmap);
    if (precision == 16)
    {
        std::cout << "Uncompressed size: " << width*height*sizeof(uint16_t) << " bytes" << std::endl;
        std::cout << "Reading pixels..." << std::endl;
        uint16_t* bytes = (uint16_t*)FreeImage_GetBits(bitmap);

        std::vector<uint16_t> values;
        values.resize(width * height);
        memcpy(&values[0], bytes, width * height * sizeof(uint16_t));
        
        std::cout << "Getting uncompressed/raw symbol counts..." << std::endl;
        SymbolCountDict initialSymbolCounts = GenerateSymbolCountDictionary(values);
        std::cout << "Generating initial rANS state..." << std::endl;
        // rANS encode - 24-bit probability, 8-bit blocks
        RansState ransState(24, initialSymbolCounts, 8);
        std::cout << "rANS encoding values..." << std::endl;
        for(auto value : values)
            ransState.AddSymbol(value);
        std::cout << "Encoded bytes: " << ransState.GetCompressedBlocks().size() << std::endl;
        std::cout << "Getting initial entropy..." << std::endl;
        GetSymbolEntropy(values);
        std::cout << "Getting compressed entropy..." << std::endl;
        WaveletLayer *bottomLayer = ProcessImageData(&values[0], width, height);

    }
    
    FreeImage_Unload(bitmap);
    FreeImage_DeInitialise();
}
