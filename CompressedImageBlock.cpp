#include "CompressedImageBlock.h"
#include "WaveletLayer.h"
#include "Serialize.h"

#include <iostream>
#include <assert.h>

// makes serialization easy lmao
struct CompressedImageBlockHeader::BlockHeaderHeader
{
    // TODO possibly not needded?
    uint32_t width;
    uint32_t height;

    // position of block in stream
    size_t blockPos;

    // rANS
    uint64_t finalRansState;
};

CompressedImageBlock::CompressedImageBlock(std::vector<uint16_t> pixelVals, uint32_t width, uint32_t height)
{
    waveletPyramidBottom = std::make_shared<WaveletLayer>(pixelVals, width, height);
    // get top layer
    std::shared_ptr<WaveletLayer> topLayer = waveletPyramidBottom;
    while (topLayer->GetParentLayer() != nullptr)
        topLayer = topLayer->GetParentLayer();
    header = CompressedImageBlockHeader(topLayer->GetParentVals(), width, height);
}

struct BlockBodyHeader
{
    size_t hash;
};

CompressedImageBlock::CompressedImageBlock(CompressedImageBlockHeader header, size_t bodiesStart, const std::vector<uint8_t>& bytes, RansTable symbolTable)
    : header(header)
{
    size_t readPos = bodiesStart + header.GetBlockPos();
    BlockBodyHeader bodyHeader = ReadValue<BlockBodyHeader>(bytes, readPos);

    std::vector<uint8_t> rANSBytes = ReadVector<uint8_t>(bytes, readPos);

    if (header.finalRansState == 0)
    {
        std::cout << "INVALID FINAL rANS STATE!" << std::endl;
        assert(false);
        return;
    }

    //std::cout << rANSBytes.size() << " Bytes read" << std::endl;
    RansState ransState = RansState(rANSBytes, header.finalRansState, 24, symbolTable, 8);
    std::vector<uint16_t> wavelets;
    while (ransState.HasData())
        wavelets.emplace_back(ransState.ReadSymbol());

    size_t waveletsHash = HashVec(wavelets);
    if (waveletsHash != bodyHeader.hash)
    {
        std::cout << "Block hash mismatch!" << std::endl;
    }

    //std::reverse(wavelets.begin(), wavelets.end());

    WaveletLayerSize size = WaveletLayerSize(header.width, header.height);

    std::vector<WaveletLayerSize> waveletLayerSizes;
    waveletLayerSizes.push_back(size);
    while (!size.IsRoot())
    {
        size = WaveletLayerSize(size.GetParentWidth(), size.GetParentHeight());
        waveletLayerSizes.push_back(size);
    }

    // root first
    std::reverse(waveletLayerSizes.begin(), waveletLayerSizes.end());

    // root layer - special logic
    std::vector<uint16_t> rootWavelets;
    rootWavelets.resize(waveletLayerSizes[0].GetWidth() * waveletLayerSizes[0].GetHeight());
    memcpy(&rootWavelets[0], &wavelets[0], rootWavelets.size() * sizeof(uint16_t));
    std::shared_ptr<WaveletLayer> currLayer = std::make_shared<WaveletLayer>(rootWavelets, header.parentVals, waveletLayerSizes[0].GetWidth(), waveletLayerSizes[0].GetHeight());
    // remove root from sizes
    waveletLayerSizes.erase(waveletLayerSizes.begin());

    // All layers below root
    size_t waveletIndex = rootWavelets.size();
    for (auto layerSize : waveletLayerSizes)
    {
        // get layer wavelets
        std::vector<uint16_t> layerWavelets;
        layerWavelets.resize(layerSize.GetWidth() * layerSize.GetHeight());
        memcpy(&layerWavelets[0], &wavelets[waveletIndex], layerWavelets.size() * sizeof(uint16_t));
        waveletIndex += layerWavelets.size();

        // create layer object
        currLayer = std::make_shared<WaveletLayer>(currLayer, layerWavelets, layerSize.GetWidth(), layerSize.GetHeight());
    }

    waveletPyramidBottom = currLayer;
}

std::vector<uint16_t> CompressedImageBlock::GetWaveletValues()
{
    // Get wavelet layers
    std::shared_ptr<WaveletLayer> topLayer = waveletPyramidBottom;
    std::vector<std::shared_ptr<WaveletLayer>> waveletLayers;
    waveletLayers.push_back(topLayer);
    while (topLayer->GetParentLayer() != nullptr)
    {
        topLayer = topLayer->GetParentLayer();
        waveletLayers.push_back(topLayer);
    }

    // Top-layer first
    std::reverse(waveletLayers.begin(), waveletLayers.end());

    // Combine wavelet vector
    std::vector<uint16_t> blockWavelets;
    for (auto waveletLayer : waveletLayers)
    {
        const std::vector<uint16_t>& layerWavelets = waveletLayer->GetWavelets();
        blockWavelets.insert(blockWavelets.end(), layerWavelets.begin(), layerWavelets.end());
    }

    return blockWavelets;
}

// Writes body of block - everything needed to decode layers below root
void CompressedImageBlock::WriteBody(std::vector<uint8_t>& outputBytes, const RansTable & globalSymbolTable)
{
    // add header
    size_t headerPos = outputBytes.size();
    outputBytes.resize(outputBytes.size() + sizeof(BlockBodyHeader));

    // Get wavelets
    std::vector<uint16_t> blockWavelets = GetWaveletValues();

    // TODO error-checking
    size_t waveletsHash = HashVec(blockWavelets);
    // rANS encode - 24-bit probability, 8-bit blocks
    RansState waveletRansState(24, globalSymbolTable, 8);
    // rANS decodes backwards
    std::reverse(blockWavelets.begin(), blockWavelets.end());
    //std::cout << "Starting rANS encode..." << std::endl;
    for (auto value : blockWavelets)
        waveletRansState.AddSymbol(value);
    header.finalRansState = waveletRansState.GetRansState();
    //std::cout << "finsihed rANS encode..." << std::endl;

    // write rANS encoded wavelets
    const std::vector<uint8_t>& ransEncondedWavelets = waveletRansState.GetCompressedBlocks();
    WriteVector(outputBytes, ransEncondedWavelets);

    // write header
    BlockBodyHeader header;
    header.hash = waveletsHash;
    memcpy(&outputBytes[headerPos], &header, sizeof(header));
}

std::vector<uint16_t> CompressedImageBlock::GetBottomLevelPixels()
{
    return waveletPyramidBottom->DecodeLayer();
}

void CompressedImageBlockHeader::Write(std::vector<uint8_t>& outputBytes)
{
    // struct data that can easily be serialized
    BlockHeaderHeader headerTop;
    headerTop.width = width;
    headerTop.height = height;
    headerTop.blockPos = blockPos;
    headerTop.finalRansState = finalRansState;

    WriteValue(outputBytes, headerTop);
    WriteVector(outputBytes, parentVals);
}

CompressedImageBlockHeader CompressedImageBlockHeader::Read(const std::vector<uint8_t>& bytes, size_t &readPos)
{
    BlockHeaderHeader headerTop = ReadValue<BlockHeaderHeader>(bytes, readPos);
    std::vector<uint16_t> parentVals = ReadVector<uint16_t>(bytes, readPos);
    return CompressedImageBlockHeader(headerTop, parentVals);
}

CompressedImageBlockHeader CompressedImageBlock::GetHeader()
{
    return header;
}

CompressedImageBlockHeader::CompressedImageBlockHeader(BlockHeaderHeader header, std::vector<uint16_t> parentVals)
    : width(header.width), height(header.height), blockPos(header.blockPos), finalRansState(header.finalRansState), parentVals(parentVals)
{

}

CompressedImageBlockHeader::CompressedImageBlockHeader(std::vector<uint16_t> parentVals, uint32_t width, uint32_t height)
    : parentVals(parentVals), width(width), height(height), blockPos(-1), finalRansState(0)
{
    
}
CompressedImageBlockHeader::CompressedImageBlockHeader()
    : width(-1), height(-1), blockPos(-1), finalRansState(0)
{

}

CompressedImageBlockHeader::CompressedImageBlockHeader(CompressedImageBlockHeader header, size_t blockPos)
    : parentVals(header.parentVals), width(header.width), height(header.height), finalRansState(header.finalRansState), blockPos(blockPos)
{

}

size_t CompressedImageBlockHeader::GetBlockPos()
{
    return blockPos;
}
