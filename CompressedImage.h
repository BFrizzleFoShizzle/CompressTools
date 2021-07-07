#pragma once
#include <vector>
#include <memory>
#include <map>

#include "RansEncode.h"
#include "CompressedImageBlock.h"
#include "Serialize.h"

struct CompressedImageHeader
{
    static const uint16_t CURR_VERSION = 0x0002;
    CompressedImageHeader()
    {

    }
    CompressedImageHeader(uint32_t width, uint32_t height)
        : width(width), height(height)
    {}
    bool IsCorrect()
    {
        return MAGIC == 0xFEDF && version == CURR_VERSION;
    }
    // Header header
    uint16_t MAGIC = 0xFEDF;
    uint16_t version = CURR_VERSION;

    // required metadata for wavelet encoder
    uint32_t width;
    uint32_t height;
    uint32_t blockSize;
    size_t blockBodyStart;
};

class CompressedImage
{
public:
    // TODO remove
    CompressedImage() {};
    // open + full decode
    CompressedImage(const std::vector<uint16_t>& values, size_t width, size_t height, size_t blockSize);
    // loads whole file
    static std::shared_ptr<CompressedImage> Deserialize(ByteIterator& bytes);
    // Opens for streaming
    static std::shared_ptr<CompressedImage> OpenStream(std::string filename);
    std::vector<uint8_t> Serialize();
    std::vector<uint16_t> GetBottomLevelPixels();

    // returns the level each block is decoded at
    std::vector<uint8_t> GetBlockLevels();

    uint16_t GetPixel(size_t x, size_t y);

    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWidthInBlocks() const;
    uint32_t GetHeightInBlocks() const;

    uint32_t GetTopLOD() const;

    size_t GetMemoryUsage() const;

private:
    // generate header info from stream
    static std::shared_ptr<CompressedImage> GenerateFromStream(ByteIterator& bytes);

    CompressedImageHeader header;
    // Wavelet image containing parent vals
    std::vector<std::shared_ptr<CompressedImageBlock>> compressedImageBlocks;

    // used for streamed decode
    std::vector<CompressedImageBlockHeader> blockHeaders;
    std::shared_ptr<RansTable> globalSymbolTable;
    std::basic_ifstream<uint8_t> fileStream;
    size_t blockBodiesStart;
    
    // used for caching
    // total approx. RAM usage of image stream
    size_t currentCacheSize;
    // min. RAM usage of image stream 
    size_t memoryOverhead;

    SymbolCountDict globalSymbolCounts;
};