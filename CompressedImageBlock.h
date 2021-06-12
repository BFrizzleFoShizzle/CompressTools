#pragma once

#include <vector>
#include <memory>
#include "WaveletLayer.h"
#include "RansEncode.h"

class CompressedImageBlock;

//
class CompressedImageBlockHeader
{
    friend CompressedImageBlock;
public:
    struct BlockHeaderHeader;
    CompressedImageBlockHeader();
    CompressedImageBlockHeader(BlockHeaderHeader header, std::vector<uint16_t> parentVals);
    CompressedImageBlockHeader(CompressedImageBlockHeader header, size_t blockPos);
    CompressedImageBlockHeader(std::vector<uint16_t> parentVals, uint32_t width, uint32_t height);
    void Write(std::vector<uint8_t>& outputBytes);
    static CompressedImageBlockHeader Read(const std::vector<uint8_t>& bytes, size_t &readPos);
    size_t GetBlockPos();
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
    CompressedImageBlock(std::vector<uint16_t> pixelVals, uint32_t width, uint32_t height);
    CompressedImageBlock(CompressedImageBlockHeader header, size_t bodiesStart, const std::vector<uint8_t> &bytes, std::shared_ptr<RansTable> symbolTable);
    std::vector<uint16_t> GetWaveletValues();
    void WriteBody(std::vector<uint8_t>& outputBytes, const std::shared_ptr<RansTable>& globalSymbolTable);

    std::vector<uint16_t> GetLevelPixels(uint32_t level);
    std::vector<uint16_t> GetBottomLevelPixels();

    CompressedImageBlockHeader GetHeader();

private:
    CompressedImageBlockHeader header;
    // TODO remove this and stream in data
    RansState ransState;
    std::shared_ptr<WaveletLayer> waveletPyramidBottom;
};