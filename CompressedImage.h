#pragma once
#include <vector>

#include "CompressedImageBlock.h"
#include "Precision.h"

struct CompressedImageHeader
{
    static const uint16_t CURR_VERSION = 0x0003;
    CompressedImageHeader()
    {

    }
    CompressedImageHeader(uint32_t width, uint32_t height)
        : width(width), height(height)
    {}
    bool IsCorrect()
    {
        if (version != CURR_VERSION)
            std::cerr << "Old file version not supported: " << version << " expected: " << CURR_VERSION << std::endl;
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
    CompressedImage(const std::vector<symbol_t>& values, size_t width, size_t height, size_t blockSize);
    // loads whole file
    static std::shared_ptr<CompressedImage> Deserialize(ByteIterator& bytes);
    // Opens for streaming
    static std::shared_ptr<CompressedImage> OpenStream(std::string filename);
    std::vector<uint8_t> Serialize();
    std::vector<symbol_t> GetBottomLevelPixels();

    // returns the level each block is decoded at
    std::vector<uint8_t> GetBlockLevels();

    symbol_t GetPixel(size_t x, size_t y);

    uint32_t GetWidth() const;
    uint32_t GetHeight() const;
    uint32_t GetWidthInBlocks() const;
    uint32_t GetHeightInBlocks() const;

    uint32_t GetTopLOD() const;

    size_t GetMemoryUsage() const;

    void ClearBlockCache();

private:
    std::shared_ptr<CompressedImageBlock> GetBlock(size_t index);

    // generate header info from stream
    static std::shared_ptr<CompressedImage> GenerateFromStream(ByteIterator& bytes);

    CompressedImageHeader header;
    // Wavelet image containing parent vals
    std::vector<std::shared_ptr<CompressedImageBlock>> compressedImageBlocks;

    // used for streamed decode
    std::vector<CompressedImageBlockHeader> blockHeaders;
    std::shared_ptr<RansTable> globalSymbolTable;
    FastFileStream fileStream;
    size_t blockBodiesStart;
    
    // used for caching
    // total approx. RAM usage of image stream
    size_t currentCacheSize;
    // min. RAM usage of image stream 
    size_t memoryOverhead;

    SymbolCountDict globalSymbolCounts;
};