#pragma once

static constexpr size_t PROBABILITY_RES = 16;
static constexpr size_t BLOCK_SIZE = 16;

#include <vector>
#include "WaveletEncodeLayer.h"
#include "WaveletDecodeLayer.h"
#include "RansEncode.h"
#include "Precision.h"

class CompressedImageBlock;

//
class CompressedImageBlockHeader
{
    friend CompressedImageBlock;
public:
    struct BlockHeaderHeader;
    CompressedImageBlockHeader();
    CompressedImageBlockHeader(BlockHeaderHeader header, uint32_t width, uint32_t height, std::vector<symbol_t> parentVals);
    CompressedImageBlockHeader(CompressedImageBlockHeader header, size_t blockPos);
    CompressedImageBlockHeader(std::vector<symbol_t> parentVals, uint32_t width, uint32_t height);
    void Write(std::vector<uint8_t>& outputBytes);
    static CompressedImageBlockHeader Read(ByteIterator& bytes, std::vector<symbol_t> parentVals, uint32_t width, uint32_t height);
    size_t GetBlockPos();
    // returns RAM usage
    size_t GetMemoryFootprint() const;
    const std::vector<symbol_t> &GetParentVals();
    uint32_t getWidth();
private:
    // TODO possibly not needded?
    uint32_t width;
    uint32_t height;

    // position of block in stream
    size_t blockPos;
    // rANS/wavelet vals
    std::vector<symbol_t> parentVals;
    state_t finalRansState;
};

class CompressedImageBlock
{
public:
    // TODO remove
    CompressedImageBlock() {};
    CompressedImageBlock(std::vector<symbol_t> pixelVals, uint32_t width, uint32_t height);
    CompressedImageBlock(CompressedImageBlockHeader header, Iterator<block_t> &blocks, std::shared_ptr<RansTable> symbolTable);
    std::vector<symbol_t> GetWaveletValues();
    void WriteBody(std::vector<uint8_t>& outputBytes, const std::shared_ptr<RansTable>& globalSymbolTable);

    std::vector<symbol_t> GetLevelPixels(uint32_t level);
    symbol_t GetPixel(uint32_t x, uint32_t y);
    std::vector<symbol_t> GetBottomLevelPixels();

    uint32_t GetLevel();

    // TODO ptr?
    CompressedImageBlockHeader GetHeader();
    std::vector<symbol_t> GetParentVals();

    WaveletLayerSize GetSize() const;

    size_t GetMemoryFootprint() const;

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