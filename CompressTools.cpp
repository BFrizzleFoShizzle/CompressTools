// CompressTools.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define FREEIMAGE_LIB

#include <FreeImage.h>

#include <iostream>
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <assert.h>

#include "RansEncode.h"
#include "WaveletLayer.h"

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
        WaveletLayer bottomLayer = WaveletLayer(values, width, height);
        WaveletLayer* waveletLayer = &bottomLayer;

        std::vector<WaveletLayer*> waveletLayers;
        waveletLayers.push_back(waveletLayer);
        // Queue up wavelet layers
        while (waveletLayer->GetParentLayer() != nullptr)
        {
            waveletLayer = waveletLayer->GetParentLayer();
            waveletLayers.push_back(waveletLayer);
        }
        // Decode pyramid
        std::vector<uint16_t> parentVals = waveletLayer->GetParentVals();
        while (waveletLayers.size() > 0)
        {
            std::cout << "Decompressing layer..." << std::endl;
            WaveletLayer* currLayer = waveletLayers.back();
            // TODO does this do a copy?
            const std::vector<uint16_t> wavelets = currLayer->GetWavelets();
            // TODO garbage collection/memory leak
            WaveletLayer* reconstructedLayer = new WaveletLayer(wavelets, parentVals, currLayer->GetWidth(), currLayer->GetHeight());

            std::vector<uint16_t> decodedValues = reconstructedLayer->DecodeLayer();

            // pass decoded vals down to next layer
            parentVals = std::move(decodedValues);
            waveletLayers.pop_back();
        }
        std::cout << values.size() << " " << parentVals.size();
        // this is now the child values
        std::cout << "Checking decoded values..." << std::endl;
        // check wavelet decode is correct
        for (int i = 0; i < values.size(); ++i)
        {
            assert(values[i] == parentVals[i]);
            if (values[i] != parentVals[i])
            {
                std::cout << "Decoded wavelet values at " << i << " did not match." << std::endl;
            }
        }
        std::cout << "Decoded wavelets checked." << std::endl;

    }
    
    FreeImage_Unload(bitmap);
    FreeImage_DeInitialise();
}
