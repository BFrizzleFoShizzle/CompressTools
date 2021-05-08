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
#include <algorithm>

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
    return std::move(symbolCounts);
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

struct CompressedImageHeader
{
    CompressedImageHeader()
    {

    }
    CompressedImageHeader(uint32_t width, uint32_t height, uint64_t finalRansState)
        : width(width), height(height), finalRansState(finalRansState)
    {}
    bool IsCorrect()
    {
        return MAGIC == 0xFEDF && version == 0x001;
    }
    // Header header
    uint16_t MAGIC = 0xFEDF;
    uint16_t version = 0x0001;

    // required metadata for wavelet encoder
    uint32_t width;
    uint32_t height;

    // rANS metadata
    uint64_t finalRansState;
};

template<typename T>
struct VectorHeader
{
    VectorHeader()
        : count(-1)
    {

    }
    VectorHeader(const std::vector<T> &values)
    {
        count = values.size();
    }
    uint64_t count;
};

class CompressedImage
{
public:
    CompressedImage(std::shared_ptr<WaveletLayer> waveletPyramidBottom);
    static std::shared_ptr<CompressedImage> Deserialize(const std::vector<uint8_t> &bytes);
    std::vector<uint8_t> Serialize();
    std::vector<uint16_t> GetBottomLevelPixels();

private:
    CompressedImageHeader header;
    std::shared_ptr<WaveletLayer> waveletPyramidBottom;
};

CompressedImage::CompressedImage(std::shared_ptr<WaveletLayer> waveletPyramidBottom)
    : waveletPyramidBottom(waveletPyramidBottom)
{
    header.width = waveletPyramidBottom->GetWidth();
    header.height = waveletPyramidBottom->GetHeight();
}

// helper function
template<typename T>
void WriteVector(std::vector<uint8_t> &outputBytes, const std::vector<T>& vector)
{
    uint64_t writePos = outputBytes.size();
    // write header
    VectorHeader<T> vectorHeader = VectorHeader<T>(vector);
    outputBytes.resize(outputBytes.size() + sizeof(vectorHeader));
    memcpy(&outputBytes[writePos], &vectorHeader, sizeof(vectorHeader));

    // write vector values
    writePos = outputBytes.size();
    uint64_t vectorSize = vector.size() * sizeof(T);
    outputBytes.resize(outputBytes.size() + vectorSize);
    memcpy(&outputBytes[writePos], &vector[0], vectorSize);
}

// helper function
template<typename T>
std::vector<T> ReadVector(const std::vector<uint8_t>& inputBytes, uint64_t &readPos)
{
    // read header
    VectorHeader<T> vectorHeader;
    memcpy(&vectorHeader, &inputBytes[readPos], sizeof(vectorHeader));
    readPos += sizeof(vectorHeader);

    // read vector values
    std::vector<T> outputVector;
    outputVector.resize(vectorHeader.count);
    uint64_t vectorSize = outputVector.size() * sizeof(T);
    memcpy(&outputVector[0] , &inputBytes[readPos], vectorSize);
    readPos += vectorSize;

    return std::move(outputVector);
}


void WriteSymbolTable(std::vector<uint8_t> &outputBytes, const SymbolCountDict &symbolCounts)
{
    // Convert to vector so we can sort by entropy, resulting in consistant ordering
    std::vector<SymbolCount> countsVec = EntropySortSymbols(symbolCounts);
    
    // TODO compress this
    WriteVector(outputBytes, countsVec);
}

SymbolCountDict ReadSymbolTable(const std::vector<uint8_t>& inputBytes, uint64_t& readPos)
{
    // Convert to vector so we can sort by entropy, resulting in consistant ordering
    std::vector<SymbolCount> countsVec = ReadVector<SymbolCount>(inputBytes, readPos);

    // TODO this gets turned back into a count vector later on...
    SymbolCountDict symbolCountDict;
    for (auto symbolCount : countsVec)
        symbolCountDict.emplace(symbolCount.symbol, symbolCount.count);

    return std::move(symbolCountDict);
}

std::vector<uint8_t> CompressedImage::Serialize()
{
    std::vector<uint8_t> byteStream;

    // Header is written to position 0 later (need to rANS encode first)
    byteStream.resize(sizeof(header));

    uint64_t writePos = byteStream.size();

    // Get top wavelet layer
    std::shared_ptr<WaveletLayer> topLayer = waveletPyramidBottom;
    while (topLayer->GetParentLayer() != nullptr)
        topLayer = topLayer->GetParentLayer();
    
    // ref to avoid copy
    // parent values vector (currently size 1, could change in the future)
    const std::vector<uint16_t> & topLevelParents = topLayer->GetParentVals();
    WriteVector(byteStream, topLevelParents);

    // generate fused wavelet stream
    // TODO split into blocks + possibly layers
    std::vector<uint16_t> waveletsToEncode;
    std::shared_ptr<WaveletLayer> waveletLayer = waveletPyramidBottom;
    while (waveletLayer != nullptr)
    {
        // append
        const std::vector<uint16_t>& layerWavelets = waveletLayer->GetWavelets();
        waveletsToEncode.insert(waveletsToEncode.end(), layerWavelets.begin(), layerWavelets.end());
        waveletLayer = waveletLayer->GetParentLayer();
    }
    // all the wavelets are now in one array
    std::cout << "Total wavelets: " << waveletsToEncode.size() << std::endl;
    std::cout << "Getting wavelet symbol counts..." << std::endl;
    SymbolCountDict waveletSymbolCounts = GenerateSymbolCountDictionary(waveletsToEncode);
    std::cout << "Unique symbols: " << waveletSymbolCounts.size() << std::endl;
    // write symbol table
    std::cout << "Writing symbol table..." << std::endl;
    WriteSymbolTable(byteStream, waveletSymbolCounts);

    std::cout << "Generating initial rANS state..." << std::endl;
    // rANS encode - 24-bit probability, 8-bit blocks
    RansState waveletRansState(24, waveletSymbolCounts, 8);
    std::cout << "rANS encoding values..." << std::endl;
    for (auto value : waveletsToEncode)
        waveletRansState.AddSymbol(value);
    std::cout << "Encoded bytes: " << waveletRansState.GetCompressedBlocks().size() << std::endl;

    // write rANS encoded wavelets
    const std::vector<uint8_t> &ransEncondedWavelets = waveletRansState.GetCompressedBlocks();
    WriteVector(byteStream, ransEncondedWavelets);

    // write header
    header.finalRansState = waveletRansState.GetRansState();
    memcpy(&byteStream[0], &header, sizeof(header));

    // cbf checking if this is needed
    return std::move(byteStream);
}

std::shared_ptr<CompressedImage> CompressedImage::Deserialize(const std::vector<uint8_t> &bytes)
{
    // read header
    std::cout << "Reading CompressedImage header..." << std::endl;
    CompressedImageHeader header;
    memcpy(&header, &bytes[0], sizeof(header));
    assert(header.IsCorrect());
    if (!header.IsCorrect())
        std::cerr << "Image header validation failed in CompressedImage::Deserialize()" << std::endl;

    uint64_t readPos = sizeof(header);

    // read top-level parent values
    std::vector<uint16_t> rootParentVals = ReadVector<uint16_t>(bytes, readPos);

    // read symbol counts
    SymbolCountDict waveletSymbolCounts = ReadSymbolTable(bytes, readPos);
    
    // read rANS-encoded wavelets
    std::cout << "Reading compressed wavelets..." << std::endl;
    std::vector<uint8_t> ransWavelets = ReadVector<uint8_t>(bytes, readPos);
    std::cout << "Compressed wavelets bytes: " << ransWavelets.size() << std::endl;

    std::vector<uint16_t> wavelets;
    std::cout << "Initializing rANS decoder..." << std::endl;
    RansState ransState = RansState(ransWavelets, header.finalRansState, 24, waveletSymbolCounts, 8);
    std::cout << "Decompressing wavelets..." << std::endl;
    while (ransState.HasData())
        wavelets.emplace_back(ransState.ReadSymbol());
    // TODO rANS decompresses backwards, there are faster solutions to this
    std::reverse(wavelets.begin(), wavelets.end());

    std::cout << "Decompressed wavelets: " << wavelets.size() << std::endl;
    
    // reconstruct/decode wavelet pyramid
    std::shared_ptr<WaveletLayer> bottomLayer = std::make_shared<WaveletLayer>(wavelets, rootParentVals, header.width, header.height);
    std::shared_ptr<CompressedImage> image = std::make_shared<CompressedImage>(bottomLayer);
    return std::move(image);
}

std::vector<uint16_t> CompressedImage::GetBottomLevelPixels()
{
    return waveletPyramidBottom->DecodeLayer();
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
        std::shared_ptr<WaveletLayer> bottomLayer = std::make_shared<WaveletLayer>(values, width, height);
        /*
        std::shared_ptr<WaveletLayer> waveletLayer = bottomLayer;

        std::vector<std::shared_ptr<WaveletLayer>> waveletLayers;
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
            std::shared_ptr<WaveletLayer> currLayer = waveletLayers.back();
            // TODO does this do a copy?
            const std::vector<uint16_t> wavelets = currLayer->GetWavelets();
            std::shared_ptr<WaveletLayer> reconstructedLayer = std::make_shared<WaveletLayer>(wavelets, parentVals, currLayer->GetWidth(), currLayer->GetHeight());

            std::vector<uint16_t> decodedValues = reconstructedLayer->DecodeLayer();

            // pass decoded vals down to next layer
            parentVals = std::move(decodedValues);
            waveletLayers.pop_back();
        }
        std::cout << values.size() << " " << parentVals.size() << std::endl;
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
        // smush all the wavelets together (this is bad for compression, but D-Day is tomorrow, haha)
        std::vector<uint16_t> waveletsToEncode;
        waveletLayer = bottomLayer;
        while (waveletLayer->GetParentLayer() != nullptr)
        {
            // append
            const std::vector<uint16_t>& layerWavelets = waveletLayer->GetWavelets();
            waveletsToEncode.insert(waveletsToEncode.end(), layerWavelets.begin(), layerWavelets.end());
            waveletLayer = waveletLayer->GetParentLayer();
        }
        // finish last element
        const std::vector<uint16_t>& layerWavelets = waveletLayer->GetWavelets();
        waveletsToEncode.insert(waveletsToEncode.end(), layerWavelets.begin(), layerWavelets.end());

        // all the wavelets are now in one array
        std::cout << "Total wavelets: " << waveletsToEncode.size() << std::endl;
        std::cout << "Getting wavelet symbol counts..." << std::endl;
        SymbolCountDict waveletSymbolCounts = GenerateSymbolCountDictionary(waveletsToEncode);
        std::cout << "Generating initial rANS state..." << std::endl;
        // rANS encode - 24-bit probability, 8-bit blocks
        RansState waveletRansState(24, waveletSymbolCounts, 8);
        std::cout << "rANS encoding values..." << std::endl;
        for (auto value : waveletsToEncode)
            waveletRansState.AddSymbol(value);
        std::cout << "Encoded bytes: " << waveletRansState.GetCompressedBlocks().size() << std::endl;
        */
        std::shared_ptr<CompressedImage> compressedImage = std::make_shared<CompressedImage>(bottomLayer);
        std::vector<uint8_t> imageBytes = compressedImage->Serialize();
        std::cout << "Final encoded bytes: " << imageBytes.size() << std::endl;
        std::shared_ptr<CompressedImage> decodedImage = CompressedImage::Deserialize(imageBytes);
        std::cout << "Decoding bottom-level pixels..." << imageBytes.size() << std::endl;
        std::vector<uint16_t> decodedPixels = decodedImage->GetBottomLevelPixels();
        for (int i = 0; i < values.size(); ++i)
        {
            assert(values[i] == decodedPixels[i]);
            if (values[i] != decodedPixels[i])
            {
                std::cout << "Decoded wavelet values at " << i << " did not match." << std::endl;
            }
        }
        std::cout << "Values checked!" << std::endl;
    }
    
    FreeImage_Unload(bitmap);
    FreeImage_DeInitialise();
}
