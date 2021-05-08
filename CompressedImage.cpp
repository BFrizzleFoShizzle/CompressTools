#include "CompressedImage.h"

#include "WaveletLayer.h"

#include <iostream>
#include <assert.h>


// std::hash is garbage
// roughly adapted from https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector
std::size_t HashVec(std::vector<uint16_t> const& vec)
{
    std::size_t seed = vec.size();
    for (auto& i : vec) {
        seed ^= i + 0x9e37 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

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

template<typename T>
struct VectorHeader
{
    VectorHeader()
        : count(-1)
    {

    }
    VectorHeader(const std::vector<T>& values)
    {
        count = values.size();
    }
    uint64_t count;
};

CompressedImage::CompressedImage(std::shared_ptr<WaveletLayer> waveletPyramidBottom)
    : waveletPyramidBottom(waveletPyramidBottom)
{
    header.width = waveletPyramidBottom->GetWidth();
    header.height = waveletPyramidBottom->GetHeight();
}

// helper function
template<typename T>
void WriteVector(std::vector<uint8_t>& outputBytes, const std::vector<T>& vector)
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
std::vector<T> ReadVector(const std::vector<uint8_t>& inputBytes, uint64_t& readPos)
{
    // read header
    VectorHeader<T> vectorHeader;
    memcpy(&vectorHeader, &inputBytes[readPos], sizeof(vectorHeader));
    readPos += sizeof(vectorHeader);

    // read vector values
    std::vector<T> outputVector;
    outputVector.resize(vectorHeader.count);
    uint64_t vectorSize = outputVector.size() * sizeof(T);
    memcpy(&outputVector[0], &inputBytes[readPos], vectorSize);
    readPos += vectorSize;

    return std::move(outputVector);
}


void WriteSymbolTable(std::vector<uint8_t>& outputBytes, const SymbolCountDict& symbolCounts)
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
    const std::vector<uint16_t>& topLevelParents = topLayer->GetParentVals();
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
    size_t waveletsHash = HashVec(waveletsToEncode);
    std::cout << "Wavelets hash: " << waveletsHash << std::endl;

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
    const std::vector<uint8_t>& ransEncondedWavelets = waveletRansState.GetCompressedBlocks();
    WriteVector(byteStream, ransEncondedWavelets);

    // write header
    header.finalRansState = waveletRansState.GetRansState();
    memcpy(&byteStream[0], &header, sizeof(header));

    // cbf checking if this is needed
    return std::move(byteStream);
}

std::shared_ptr<CompressedImage> CompressedImage::Deserialize(const std::vector<uint8_t>& bytes)
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
