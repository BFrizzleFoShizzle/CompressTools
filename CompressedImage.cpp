#include "CompressedImage.h"

#include "Serialize.h"

#include <iostream>
#include <assert.h>

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

CompressedImage::CompressedImage(const std::vector<uint16_t>& values, size_t width, size_t height, size_t blockSize)
{
    // set up header
    header.width = width;
    header.height = height;
    header.blockSize = blockSize;


    // generate blocks
    // TODO morton order?
    std::vector<uint16_t> waveletValues;
    std::cout << "Generating image blocks..." << std::endl;
    for (size_t blockStartY = 0; blockStartY < height; blockStartY += blockSize)
    {
        for (size_t blockStartX = 0; blockStartX < width; blockStartX += blockSize)
        {
            size_t blockX = blockStartX / blockSize;
            size_t blockY = blockStartY / blockSize;
            size_t blockW = std::min(width - blockStartX, blockSize);
            size_t blockH = std::min(height - blockStartY, blockSize);
            // Copy block values
            std::vector<uint16_t> blockValues;
            blockValues.resize(blockW * blockH);
            for (size_t pixY = 0; pixY < blockH; ++pixY)
            {
                for (size_t pixX = 0; pixX < blockW; ++pixX)
                {
                    blockValues[pixY * blockW  + pixX] = values[(blockStartY + pixY) * width + (blockStartX + pixX)];
                }
            }
            if (blockX == 50 && blockY == 50)
                std::cout << "Chosen block hash: " << HashVec(blockValues) << std::endl;
            // Create compressed block
            std::shared_ptr<CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(blockValues, blockW, blockH);
            std::vector<uint16_t> layerWavelets = block->GetWaveletValues();
            waveletValues.insert(waveletValues.end(), layerWavelets.begin(), layerWavelets.end());
            compressedImageBlocks.emplace(std::make_pair(blockY, blockX), block);
        }
    }
    // generate symbol table
    std::cout << "Generating symbol counts..." << std::endl;
    globalSymbolCounts = GenerateSymbolCountDictionary(waveletValues);
    std::cout << compressedImageBlocks.size() << " blocks created." << std::endl;
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

    // write global symbol table
    std::cout << "Unique symbols: " << globalSymbolCounts.size() << std::endl;
    // write symbol table
    std::cout << "Writing symbol table..." << std::endl;
    WriteSymbolTable(byteStream, globalSymbolCounts);
    // generate rANS symbol table (currently costly)
    size_t probabilityRes = 24;
    size_t probabilityRange = 1 << probabilityRes;
    SymbolCountDict quantizedCounts = GenerateQuantizedCounts(globalSymbolCounts, probabilityRange);
    std::shared_ptr<RansTable> globalSymbolTable = std::make_shared<RansTable>(quantizedCounts);

    // get parent values
    std::vector<uint16_t> parentValues;
    for (auto block : compressedImageBlocks)
    {
        std::vector<uint16_t> blockParentVals = block.second->GetParentVals();
        // TODO this will need to change for interpolated wavelets
        WaveletLayerSize rootSize = block.second->GetSize().GetRoot();
        if (blockParentVals.size() != rootSize.GetPixelCount())
            std::cout << "Invalid number of parent vals! " << blockParentVals.size() << std::endl;

        parentValues.insert(parentValues.end(), blockParentVals.begin(), blockParentVals.end());
    }

    SymbolCountDict parentValsSymbolCounts = GenerateSymbolCountDictionary(parentValues);

    // reverse for rANS
    std::reverse(parentValues.begin(), parentValues.end());

    // rANS encode block parent values
    RansState parentValsState = RansState(24, GenerateSymbolCountDictionary(parentValues), 8);
    for (auto value : parentValues)
        parentValsState.AddSymbol(value);

    // Write to stream
    WriteSymbolTable(byteStream, parentValsSymbolCounts);
    WriteValue(byteStream, parentValsState.GetRansState());
    WriteVector(byteStream, parentValsState.GetCompressedBlocks());
    
    // Write block bodies + generate headers
    size_t blockHeaderPos = byteStream.size();
    std::vector<CompressedImageBlockHeader> blockHeaders;
    std::vector<uint8_t> bodyBytes;
    std::cout << "Generating block bodies and headers..." << std::endl;
    for (auto block : compressedImageBlocks)
    {
        //std::cout << "New block... "  << block.first.first << " " << block.first.second << std::endl;
        size_t bodyWritePos = bodyBytes.size();
        // this secretly updates the header
        block.second->WriteBody(bodyBytes, globalSymbolTable);
        // generate header with block byte position
        CompressedImageBlockHeader header = CompressedImageBlockHeader(block.second->GetHeader(), bodyWritePos);
        //std::cout << "rANS state: " << header.GetFinalRANSState() << std::endl;
        blockHeaders.push_back(header);
    }
    std::cout << "Block bodies size: " << bodyBytes.size() << std::endl;

    // Write generated headers
    std::cout << "Writing block headers..." << std::endl;
    for (auto header : blockHeaders)
    {
        header.Write(byteStream);
    }

    // Write encoded block bodies
    std::cout << "Position at: " << byteStream.size() << std::endl;
    std::cout << "Writing block bodies..." << std::endl;
    header.blockBodyStart = byteStream.size();
    byteStream.insert(byteStream.end(), bodyBytes.begin(), bodyBytes.end());

    // write header
    memcpy(&byteStream[0], &header, sizeof(header));
    std::cout << "Final size: " << byteStream.size() << std::endl;

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

    // read symbol counts
    SymbolCountDict waveletSymbolCounts = ReadSymbolTable(bytes, readPos);
    // generate rANS symbol table (currently costly)
    size_t probabilityRes = 24;
    size_t probabilityRange = 1 << probabilityRes;
    SymbolCountDict quantizedCounts = GenerateQuantizedCounts(waveletSymbolCounts, probabilityRange);
    std::shared_ptr<RansTable> globalSymbolTable = std::make_shared<RansTable>(quantizedCounts);

    // read parent values
    SymbolCountDict parentValsCounts = ReadSymbolTable(bytes, readPos);
    size_t parentValsFinalRansState = ReadValue<size_t>(bytes, readPos);
    std::vector<uint8_t> parentValsRansBytes = ReadVector<uint8_t>(bytes, readPos);
    RansState parentValsState = RansState(parentValsRansBytes, parentValsFinalRansState, 24, parentValsCounts, 8);

    std::vector<uint16_t> parentVals;
    while (parentValsState.HasData())
        parentVals.push_back(parentValsState.ReadSymbol());

    // read block headers
    std::vector<CompressedImageBlockHeader> headers;
    auto parentVal = parentVals.begin();
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            size_t blockW = std::min(header.width - blockStartX, header.blockSize);
            size_t blockH = std::min(header.height - blockStartY, header.blockSize);
            WaveletLayerSize rootSize = WaveletLayerSize(blockW, blockH).GetRoot();
            std::vector<uint16_t> blockParents;
            blockParents.insert(blockParents.end(), parentVal, parentVal + rootSize.GetPixelCount());
            parentVal += rootSize.GetPixelCount();
            CompressedImageBlockHeader header = CompressedImageBlockHeader::Read(bytes, blockParents, readPos, blockW, blockH);
            headers.push_back(header);
        }
    }

    std::cout << headers.size() << " block headers read." << std::endl;
    std::cout << "Position at: " << readPos << std::endl;

    // read block bodies
    size_t bodiesStart = readPos;
    std::map<std::pair<uint32_t, uint32_t>, std::shared_ptr<CompressedImageBlock>> blocks;
    auto blockHeader = headers.begin();
    std::cout << "Decoding block bodies..." << std::endl;
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            uint32_t blockX = blockStartX / header.blockSize;
            uint32_t blockY = blockStartY / header.blockSize;
            uint32_t blockW = std::min(header.width - blockStartX, header.blockSize);
            uint32_t blockH = std::min(header.height - blockStartY, header.blockSize);
            
            std::shared_ptr <CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(*blockHeader, bodiesStart, bytes, globalSymbolTable);
            if (blockX == 50 && blockY == 50)
                std::cout << "Chosen block hash: " << HashVec(block->GetBottomLevelPixels()) << std::endl;

            blocks.emplace(std::make_pair(blockY, blockX), block);

            ++blockHeader;
        }
    }

    std::shared_ptr<CompressedImage> image = std::make_shared<CompressedImage>();
    image->compressedImageBlocks = std::move(blocks);
    image->header = header;
    image->globalSymbolCounts = std::move(waveletSymbolCounts);

    return std::move(image);
}

std::vector<uint16_t> CompressedImage::GetBottomLevelPixels()
{
    std::vector<uint16_t> pixels;
    pixels.resize(header.width * header.height);

    // recreate image from blocks
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            uint32_t blockX = blockStartX / header.blockSize;
            uint32_t blockY = blockStartY / header.blockSize;
            uint32_t blockW = std::min(header.width - blockStartX, header.blockSize);
            uint32_t blockH = std::min(header.height - blockStartY, header.blockSize);

            std::vector<uint16_t> blockPixels = compressedImageBlocks.at(std::make_pair(blockY, blockX))->GetBottomLevelPixels();

            for (uint32_t pixY = 0; pixY < blockH; ++pixY)
            {
                for (uint32_t pixX = 0; pixX < blockW; ++pixX)
                {
                    pixels[(blockStartY + pixY) * header.width + (blockStartX + pixX)] = blockPixels[pixY * blockW + pixX];
                }
            }
        }
    }

    return pixels;
}


std::vector<uint8_t> CompressedImage::GetBlockLevels()
{
    std::vector<uint8_t> blockLevels;
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            uint32_t blockX = blockStartX / header.blockSize;
            uint32_t blockY = blockStartY / header.blockSize;

            std::shared_ptr<CompressedImageBlock> block = compressedImageBlocks.at(std::make_pair(blockY, blockX));

            blockLevels.push_back(block->GetLevel());
        }
    }
    return blockLevels;
}

uint16_t CompressedImage::GetPixel(size_t x, size_t y)
{
    uint32_t blockX = x / header.blockSize;
    uint32_t blockY = y / header.blockSize;
    uint32_t subBlockX = x % header.blockSize;
    uint32_t subBlockY = y % header.blockSize;

    std::shared_ptr<CompressedImageBlock> block = compressedImageBlocks.at(std::make_pair(blockY, blockX));

    return block->GetPixel(subBlockX, subBlockY);
}

uint32_t CompressedImage::GetWidth() const
{
    return header.width;
}

uint32_t CompressedImage::GetHeight() const
{
    return header.height;
}

uint32_t CompressedImage::GetWidthInBlocks() const
{
    uint32_t roundedWidth = header.width / header.blockSize;
    if (header.width % header.blockSize != 0)
        roundedWidth += 1;
    return roundedWidth;
}

uint32_t CompressedImage::GetHeightInBlocks() const
{
    uint32_t roundedHeight = header.height / header.blockSize;
    if (header.height % header.blockSize != 0)
        roundedHeight += 1;
    return roundedHeight;
}