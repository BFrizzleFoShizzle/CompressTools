#include "CompressedImageBlock.h"

#include <iostream>
#include "Release_Assert.h"

// makes serialization easy lmao
struct CompressedImageBlockHeader::BlockHeaderHeader
{
    // position of block in stream
    uint32_t blockPos;

    // rANS
    uint64_t finalRansState;
};

CompressedImageBlock::CompressedImageBlock(std::vector<symbol_t> pixelVals, uint32_t width, uint32_t height)
{
    encodeWaveletPyramidBottom = std::make_shared<WaveletEncodeLayer>(pixelVals, width, height);
    // get top layer
    std::shared_ptr<WaveletEncodeLayer> topLayer = encodeWaveletPyramidBottom;
    while (topLayer->GetParentLayer() != nullptr)
        topLayer = topLayer->GetParentLayer();
    header = CompressedImageBlockHeader(topLayer->GetParentVals(), width, height);
}
/*
struct BlockBodyHeader
{
    uint32_t hash;
};
*/
CompressedImageBlock::CompressedImageBlock(CompressedImageBlockHeader header, Iterator<block_t> &blocks, std::shared_ptr<RansTable> symbolTable)
    : header(header)
{
    //BlockBodyHeader bodyHeader = ReadValue<BlockBodyHeader>(bytes);

    std::shared_ptr<VectorStream<block_t>> ransByteStream = ReverseStreamVector<block_t>(blocks, false);

    if (header.finalRansState == 0)
    {
        std::cout << "INVALID FINAL rANS STATE!" << std::endl;
        assert_release(false);
        return;
    }

    //std::cout << rANSBytes.size() << " Bytes read" << std::endl;
    ransState = RansState(ransByteStream, header.finalRansState, symbolTable);
}

uint32_t CompressedImageBlock::DecodeToLevel(uint32_t targetLevel)
{
    // Check what level we're on
    // -1 = no layer decoded
    uint32_t decodedLevel = -1;
    if (currDecodeLayer)
    {
        ++decodedLevel;

        WaveletLayerSize size = WaveletLayerSize(header.width, header.height);
        while (!size.IsRoot())
        {
            if (size.GetHeight() == currDecodeLayer->GetHeight()
                && size.GetWidth() == currDecodeLayer->GetWidth())
                break;
            ++decodedLevel;
            size = size.GetParentSize();
        }
    }

    WaveletLayerSize rootSize = WaveletLayerSize(header.width, header.height);
    uint32_t topLayer = 1;
    while (!rootSize.IsRoot())
    {
        rootSize = rootSize.GetParentSize();
        ++topLayer;
    }
    
    // Special case - root layer
    // TODO try and refactor this out
    if (decodedLevel == -1)
    {
        size_t waveletsCount = rootSize.GetWaveletCount();

        // read wavelets
        std::vector<symbol_t> rootWavelets;
        while (ransState.HasData() && rootWavelets.size() < waveletsCount)
            rootWavelets.emplace_back(ransState.ReadSymbol());
        
        // create root layer
        currDecodeLayer = std::make_shared<WaveletDecodeLayer>(rootWavelets, header.parentVals, rootSize.GetWidth(), rootSize.GetHeight());

        decodedLevel = topLayer - 1;
    }

    // if we haven't decoded the level yet, decode
    while (decodedLevel > targetLevel)
    {
        uint32_t newLevel = decodedLevel - 1;

        // error-checking
        if (!ransState.IsValid() && ransState.HasData())
        {
            std::cout << "Decode requested, but rANS is unhappy!" << std::endl;
            return -1;
        }

        // find layer size
        WaveletLayerSize newLayerSize = WaveletLayerSize(header.width, header.height);
        uint32_t layer = 0;
        while (layer < newLevel)
        {
            newLayerSize = newLayerSize.GetParentSize();
            ++layer;
        }

        size_t waveletsCount = newLayerSize.GetWaveletCount();
        
        // read wavelets
        std::vector<symbol_t> wavelets;
        wavelets.reserve(waveletsCount);
        while (ransState.HasData() && wavelets.size() < waveletsCount)
            wavelets.emplace_back(ransState.ReadSymbol());

        if (wavelets.size() != waveletsCount)
        {
            std::cout << "Wrong number of wavelets decoded! Decoded " << wavelets.size() << " but expected " << waveletsCount << std::endl;
            assert_release(false);
            return -1;
        }

        // create layer object
        currDecodeLayer = std::make_shared<WaveletDecodeLayer>(wavelets, currDecodeLayer->GetPixels(), newLayerSize.GetWidth(), newLayerSize.GetHeight());
        
        decodedLevel = newLevel;
    }

    return decodedLevel;
}

std::vector<symbol_t> CompressedImageBlock::GetLevelPixels(uint32_t level)
{
    uint32_t currLevel = DecodeToLevel(level);
    
    // error-handling
    assert_release(currLevel != -1);

    if (currLevel == level)
    {
        return currDecodeLayer->GetPixels();
    }
    else
    {
        return currDecodeLayer->GetParentLevelPixels(level - currLevel);
    }
}

std::vector<symbol_t> CompressedImageBlock::GetWaveletValues()
{
    // Get wavelet layers
    std::shared_ptr<WaveletEncodeLayer> topLayer = encodeWaveletPyramidBottom;
    std::vector<std::shared_ptr<WaveletEncodeLayer>> waveletLayers;
    waveletLayers.push_back(topLayer);
    while (topLayer->GetParentLayer() != nullptr)
    {
        topLayer = topLayer->GetParentLayer();
        waveletLayers.push_back(topLayer);
    }

    // Top-layer first
    std::reverse(waveletLayers.begin(), waveletLayers.end());

    // Combine wavelet vector
    std::vector<symbol_t> blockWavelets;
    for (auto waveletLayer : waveletLayers)
    {
        const std::vector<symbol_t>& layerWavelets = waveletLayer->GetWavelets();
        blockWavelets.insert(blockWavelets.end(), layerWavelets.begin(), layerWavelets.end());
    }

    return blockWavelets;
}

// Writes body of block - everything needed to decode layers below root
void CompressedImageBlock::WriteBody(std::vector<uint8_t>& outputBytes, const std::shared_ptr<RansTable> & globalSymbolTable)
{
    // add header
    //size_t headerPos = outputBytes.size();
    //outputBytes.resize(outputBytes.size() + sizeof(BlockBodyHeader));
    //outputBytes.resize(outputBytes.size());

    // Get wavelets
    std::vector<symbol_t> blockWavelets = GetWaveletValues();

    // TODO error-checking
    size_t waveletsHash = HashVec(blockWavelets);
    // rANS encode
    std::shared_ptr<RansState> waveletRansState = std::make_shared<RansState>(globalSymbolTable);
    // rANS decodes backwards
    std::reverse(blockWavelets.begin(), blockWavelets.end());

    //std::cout << "Starting rANS encode..." << std::endl;
    for (auto value : blockWavelets)
        waveletRansState->AddSymbol(value);

    size_t finalRansState = waveletRansState->GetRansState();

    //if (finalRansState > std::numeric_limits<uint32_t>::max())
    //    std::cerr << "Final rANS state is above max!" << std::endl;

    header.finalRansState = waveletRansState->GetRansState();
    //std::cout << "finsihed rANS encode..." << std::endl;

    // write rANS encoded wavelets
    std::vector<block_t> ransEncondedWavelets = waveletRansState->GetCompressedBlocks();
    // reverse
    std::reverse(ransEncondedWavelets.begin(), ransEncondedWavelets.end());
    WriteVector(outputBytes, ransEncondedWavelets);

    // write header
    //BlockBodyHeader header;
    //header.hash = waveletsHash;
    //memcpy(&outputBytes[headerPos], &header, sizeof(header));
}

std::vector<symbol_t> CompressedImageBlock::GetBottomLevelPixels()
{
    return GetLevelPixels(0);
}

symbol_t CompressedImageBlock::GetPixel(uint32_t x, uint32_t y)
{
    // level of parent values
    WaveletLayerSize rootSize = WaveletLayerSize(header.width, header.height);
    uint32_t rootLevel = 1;
    while (!rootSize.IsRoot())
    {
        rootSize = rootSize.GetParentSize();
        ++rootLevel;
    }

    // Check what level we're on
    // -1 = no layer decoded
    uint32_t decodedLevel = -1;
    if (currDecodeLayer)
    {
        ++decodedLevel;

        WaveletLayerSize size = WaveletLayerSize(header.width, header.height);
        while (!size.IsRoot())
        {
            if (size.GetHeight() == currDecodeLayer->GetHeight()
                && size.GetWidth() == currDecodeLayer->GetWidth())
                break;
            ++decodedLevel;
            size = size.GetParentSize();
        }
    }
    else
    {
        // not decoded, only have root values
        decodedLevel = rootLevel;
    }

    // shuffle coordinate up to our level, or as high as possible
    uint32_t positionLevel = 0;
    uint32_t shiftedX = x;
    uint32_t shiftedY = y;
    while (positionLevel != decodedLevel && (shiftedX & 1) == 0 && (shiftedY & 1) == 0)
    {
        shiftedX = shiftedX >> 1;
        shiftedY = shiftedY >> 1;
        ++positionLevel;
    }

    // special case - value is root parent value
    if (positionLevel == rootLevel)
    {
        uint32_t parentValIdx = shiftedY * rootSize.GetParentWidth() + shiftedX;
        return header.parentVals[parentValIdx];
    }

    if (positionLevel == decodedLevel)
    {
        return currDecodeLayer->GetPixelAt(shiftedX, shiftedY);
    }
    else
    {
        // have to decode...
        decodedLevel = DecodeToLevel(positionLevel);

        if (decodedLevel != positionLevel)
        {
            std::cout << "Bad read: " << x << " " << y << " " << shiftedX << " " << shiftedY << std::endl;
            std::cout << header.width << " " << header.height << std::endl;
            std::cout << rootLevel << " " << positionLevel << " " << decodedLevel << std::endl;
        }

        assert_release(decodedLevel == positionLevel);
        return currDecodeLayer->GetPixelAt(shiftedX, shiftedY);
    }
}

uint32_t CompressedImageBlock::GetLevel()
{
    // Generate layer sizes
    WaveletLayerSize size = WaveletLayerSize(header.width, header.height);

    std::vector<WaveletLayerSize> waveletLayerSizes;
    waveletLayerSizes.push_back(size);
    while (!size.IsRoot())
    {
        size = WaveletLayerSize(size.GetParentWidth(), size.GetParentHeight());
        waveletLayerSizes.push_back(size);
    }

    // level of parent values
    uint32_t rootLevel = waveletLayerSizes.size();

    // Check what level we're on
    // -1 = no layer decoded
    uint32_t decodedLevel = -1;
    if (currDecodeLayer)
    {
        ++decodedLevel;
        for (WaveletLayerSize size : waveletLayerSizes)
        {
            if (size.GetHeight() == currDecodeLayer->GetHeight()
                && size.GetWidth() == currDecodeLayer->GetWidth())
                break;
            ++decodedLevel;
        }
    }
    else
    {
        // not decoded, only have root values
        decodedLevel = rootLevel;
    }

    return decodedLevel;
}

std::vector<symbol_t> CompressedImageBlock::GetParentVals()
{
    return header.GetParentVals();
}

WaveletLayerSize CompressedImageBlock::GetSize() const
{
    return WaveletLayerSize(header.width, header.height);
}


size_t CompressedImageBlock::GetMemoryFootprint() const
{
    size_t memoryUsage = 0;
    // TODO this could be a pointer to reduce overhead
    memoryUsage += header.GetMemoryFootprint();
    // ~90% correct
    memoryUsage += sizeof(ransState);
    if(currDecodeLayer)
        memoryUsage += currDecodeLayer->GetMemoryFootprint();

    return memoryUsage;
}

void CompressedImageBlockHeader::Write(std::vector<uint8_t>& outputBytes)
{
    // struct data that can easily be serialized
    BlockHeaderHeader headerTop;
    headerTop.blockPos = blockPos;
    headerTop.finalRansState = finalRansState;

    WriteValue(outputBytes, headerTop);
}

CompressedImageBlockHeader CompressedImageBlockHeader::Read(ByteIterator &bytes, std::vector<symbol_t> parentVals, uint32_t width, uint32_t height)
{
    BlockHeaderHeader headerTop = ReadValue<BlockHeaderHeader>(bytes);
    return CompressedImageBlockHeader(headerTop, width, height, parentVals);
}

CompressedImageBlockHeader CompressedImageBlock::GetHeader()
{
    return header;
}

CompressedImageBlockHeader::CompressedImageBlockHeader(BlockHeaderHeader header, uint32_t width, uint32_t height, std::vector<symbol_t> parentVals)
    : width(width), height(height), blockPos(header.blockPos), finalRansState(header.finalRansState), parentVals(parentVals)
{

}

CompressedImageBlockHeader::CompressedImageBlockHeader(std::vector<symbol_t> parentVals, uint32_t width, uint32_t height)
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

size_t CompressedImageBlockHeader::GetMemoryFootprint() const
{
    // allocated bytes
    size_t vectorMemoryUsage = parentVals.capacity() * sizeof(parentVals[0]);
    return sizeof(CompressedImageBlockHeader) + vectorMemoryUsage;
}

const std::vector<symbol_t>& CompressedImageBlockHeader::GetParentVals()
{
    return parentVals;
}

uint32_t CompressedImageBlockHeader::getWidth()
{
    return width;
}