#pragma once

#include <vector>
#include <memory>
#include "WaveletEncodeLayer.h"
#include "WaveletDecodeLayer.h"
#include "RansEncode.h"
#include "Serialize.h"

class CompressedImageBlock;

//
class CompressedImageBlockHeader
{
    friend CompressedImageBlock;
public:
    struct BlockHeaderHeader;
    CompressedImageBlockHeader();
    CompressedImageBlockHeader(BlockHeaderHeader header, uint32_t width, uint32_t height, std::vector<uint16_t> parentVals);
    CompressedImageBlockHeader(CompressedImageBlockHeader header, size_t blockPos);
    CompressedImageBlockHeader(std::vector<uint16_t> parentVals, uint32_t width, uint32_t height);
    void Write(std::vector<uint8_t>& outputBytes);
    static CompressedImageBlockHeader Read(ByteIterator& bytes, std::vector<uint16_t> parentVals, uint32_t width, uint32_t height);
    size_t GetBlockPos();
    // returns RAM usage
    size_t GetMemoryFootprint() const;
    std::vector<uint16_t> GetParentVals();
private:
    // TODO possibly not needded?
    uint32_t width;
    uint32_t height;

    // position of block in stream
    size_t blockPos;
    // rANS/wavelet vals
    std::vector<uint16_t> parentVals;
    uint64_t finalRansState;
};

class CompressedImageBlock
{
public:
    // TODO remove
    CompressedImageBlock() {};
    CompressedImageBlock(std::vector<uint16_t> pixelVals, uint32_t width, uint32_t height);
    CompressedImageBlock(CompressedImageBlockHeader header, ByteIterator &bytes, std::shared_ptr<RansTable> symbolTable);
    std::vector<uint16_t> GetWaveletValues();
    void WriteBody(std::vector<uint8_t>& outputBytes, const std::shared_ptr<RansTable>& globalSymbolTable);

    std::vector<uint16_t> GetLevelPixels(uint32_t level);
    uint16_t GetPixel(uint32_t x, uint32_t y);
    std::vector<uint16_t> GetBottomLevelPixels();

    uint32_t GetLevel();

    // TODO ptr?
    CompressedImageBlockHeader GetHeader();
    std::vector<uint16_t> GetParentVals();

    WaveletLayerSize GetSize() const;

private:
    // decodes down to layer, does nothing if already at/below layer
    // returns current level after decode
    uint32_t DecodeToLevel(uint32_t targetLevel);

    CompressedImageBlockHeader header;
    // TODO remove this and stream in data
    RansState ransState;
    std::shared_ptr<WaveletEncodeLayer> encodeWaveletPyramidBottom;
    std::shared_ptr<WaveletDecodeLayer> currDecodeLayer;
};