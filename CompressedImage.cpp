#include "CompressedImage.h"

#include <iostream>
#include "Release_Assert.h"

SymbolCountDict GenerateSymbolCountDictionary(std::vector<symbol_t> symbols)
{
    SymbolCountDict symbolCounts;
    for (auto symbol : symbols)
    {
        symbolCounts.try_emplace(symbol, 0);
        symbolCounts[symbol] += 1;
    }
    return std::move(symbolCounts);
}

CompressedImage::CompressedImage(const std::vector<symbol_t>& values, size_t width, size_t height, size_t blockSize)
{
    // set up header
    header.width = width;
    header.height = height;
    header.blockSize = blockSize;

    // generate blocks
    std::vector<symbol_t> waveletValues;
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
            std::vector<symbol_t> blockValues;
            blockValues.resize(blockW * blockH);
            for (size_t pixY = 0; pixY < blockH; ++pixY)
            {
                for (size_t pixX = 0; pixX < blockW; ++pixX)
                {
                    blockValues[pixY * blockW  + pixX] = values[(blockStartY + pixY) * width + (blockStartX + pixX)];
                }
            }
            // Create compressed block
            std::shared_ptr<CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(blockValues, blockW, blockH);
            std::vector<symbol_t> layerWavelets = block->GetWaveletValues();
            if (blockX == 50 && blockY == 50)
                std::cout << "Chosen block hash: " << HashVec(blockValues) << " Num. wavelets: " << layerWavelets.size() << std::endl;
            waveletValues.insert(waveletValues.end(), layerWavelets.begin(), layerWavelets.end());
            compressedImageBlocks.emplace_back(block);
        }
    }
    // generate symbol table
    std::cout << waveletValues.size() << " wavelet values..." << std::endl;
    std::cout << "Generating symbol counts..." << std::endl;
    globalSymbolCounts = GenerateSymbolCountDictionary(waveletValues);
    std::cout << compressedImageBlocks.size() << " blocks created." << std::endl;
}

void WriteSymbolTable(std::vector<uint8_t>& outputBytes, const TableGroupList& groupList)
{
    WriteValue(outputBytes, (group_t)groupList.size());
    for (int group = 0; group < groupList.size(); ++group)
    {
        WriteValue(outputBytes, (prob_t)groupList[group].first);
        WriteVector(outputBytes, groupList[group].second);
    }
}

TableGroupList ReadSymbolTable(ByteIterator &bytes)
{
    TableGroupList groupList;
    groupList.resize(ReadValue<group_t>(bytes));
    for (int group = 0; group < groupList.size(); ++group)
    {
        groupList[group].first = ReadValue<prob_t>(bytes);
        groupList[group].second = ReadVector<symbol_t>(bytes);
    }

    return groupList;
}

std::vector<uint8_t> CompressedImage::Serialize()
{
    std::vector<uint8_t> byteStream;

    // Header is written to position 0 later (need to rANS encode first)
    byteStream.resize(sizeof(header));

    // write global symbol table
    std::cout << "Unique symbols: " << globalSymbolCounts.size() << std::endl;
    
    // generate rANS symbol table (currently costly)
    globalSymbolTable = std::make_shared<RansTable>(globalSymbolCounts, PROBABILITY_RES);

    // write symbol table
    std::cout << "Writing symbol table..." << std::endl;
    WriteSymbolTable(byteStream, globalSymbolTable->GenerateGroupCDFs());

    // Generate wavelet image for parent vals
   // parent block parents, wavelet counts, header, body
    size_t parentValsWidth = header.width / (header.blockSize / 2);
    if (header.width % (header.blockSize) != 0)
        parentValsWidth += 1;
    size_t parentValsHeight = header.height / (header.blockSize / 2);
    if (header.height % (header.blockSize) != 0)
        parentValsHeight += 1;

    // get parent values
    std::vector<symbol_t> parentValues;
    parentValues.resize(parentValsWidth * parentValsHeight);
    size_t parentValCount = 0;
    for (size_t blockIdx  = 0; blockIdx < compressedImageBlocks.size(); ++blockIdx)
    {
        std::shared_ptr<CompressedImageBlock> block = compressedImageBlocks[blockIdx];
        std::vector<symbol_t> blockParentVals = block->GetParentVals();
        // TODO this will need to change for interpolated wavelets
        WaveletLayerSize rootParentSize = block->GetSize().GetRoot().GetParentSize();
        size_t blockY = blockIdx / GetWidthInBlocks();
        size_t blockX = blockIdx % GetWidthInBlocks();

        if (blockParentVals.size() != rootParentSize.GetPixelCount())
            std::cout << "Invalid number of parent vals! " << blockParentVals.size() << " " << rootParentSize.GetPixelCount() << std::endl;

        // write to parent val image (de-swizzle)
        for (int y = 0; y < rootParentSize.GetHeight(); ++y)
            for (int x = 0; x < rootParentSize.GetWidth(); ++x)
                parentValues[(blockY * 2 + y) * parentValsWidth + (blockX * 2 + x)] = blockParentVals[y * rootParentSize.GetWidth() + x];

        //parentValues.insert(parentValues.end(), blockParentVals.begin(), blockParentVals.end());
        parentValCount += blockParentVals.size();
    }

    if (parentValues.size() != parentValCount)
        std::cout << "Wrong number of parent values! " << parentValues.size() << " " <<  parentValCount << " " << parentValsWidth << " " << parentValsHeight << std::endl;
    
    // create parent vals image
    std::shared_ptr<CompressedImageBlock> parentValsImage = std::make_shared< CompressedImageBlock>(parentValues, parentValsWidth, parentValsHeight);
    size_t parentImageStart = byteStream.size();

    // write parent val block parents
    WriteVector(byteStream, parentValsImage->GetParentVals());

    // generate rANS symbol table (currently costly)
    std::shared_ptr<RansTable> parentSymbolTable = std::make_shared<RansTable>(GenerateSymbolCountDictionary(parentValsImage->GetWaveletValues()), PROBABILITY_RES);

    std::cout << "Writing parent vals symbol table..." << std::endl;
    WriteSymbolTable(byteStream, parentSymbolTable->GenerateGroupCDFs());

    // Prepare parent val block body + fill in header
    std::vector<uint8_t> parentValsBodyBytes;
    parentValsImage->WriteBody(parentValsBodyBytes, parentSymbolTable);
    CompressedImageBlockHeader parentsBlockHeader = parentValsImage->GetHeader();
    // set body position to 0
    parentsBlockHeader = CompressedImageBlockHeader(parentsBlockHeader, 0);

    // write out header
    parentsBlockHeader.Write(byteStream);

    // write out body bytes
    byteStream.insert(byteStream.end(), parentValsBodyBytes.begin(), parentValsBodyBytes.end());

    std::cout << "Parent block size:" << (byteStream.size() - parentImageStart) << std::endl;

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
        block->WriteBody(bodyBytes, globalSymbolTable);
        // generate header with block byte position
        CompressedImageBlockHeader header = CompressedImageBlockHeader(block->GetHeader(), bodyWritePos);
        //std::cout << "rANS state: " << header.GetFinalRANSState() << std::endl;
        blockHeaders.push_back(header);
    }
    std::cout << "Block bodies size: " << bodyBytes.size() << std::endl;

    // Write generated headers
    std::cout << "Writing block headers..." << std::endl;
    size_t headersStart = byteStream.size();
    for (auto blockHeader : blockHeaders)
    {
        blockHeader.Write(byteStream);
    }
    std::cout << "Headers size: " << (byteStream.size() - headersStart) << std::endl;

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

// shared code
std::shared_ptr<CompressedImage> CompressedImage::GenerateFromStream(ByteIterator& bytes)
{
    // read header
    std::cout << "Reading CompressedImage header..." << std::endl;
    CompressedImageHeader header = ReadValue<CompressedImageHeader>(bytes);
    if (!header.IsCorrect())
        std::cerr << "Image header validation failed in CompressedImage::Deserialize()" << std::endl;

    assert_release(header.IsCorrect());

    size_t parentValsWidth = header.width / (header.blockSize / 2);
    if (header.width % (header.blockSize) != 0)
        parentValsWidth += 1;
    size_t parentValsHeight = header.height / (header.blockSize / 2);
    if (header.height % (header.blockSize) != 0)
        parentValsHeight += 1;

    // global block symbol counts
    TableGroupList waveletSymbolGroups = ReadSymbolTable(bytes);
    // generate rANS symbol table (currently costly)
    std::shared_ptr<RansTable> globalSymbolTable = std::make_shared<RansTable>(waveletSymbolGroups, PROBABILITY_RES);

    // read parent val block parents
    std::vector<symbol_t> parentValImageParents = ReadVector<symbol_t>(bytes);

    // read parent val block wavelet counts
    TableGroupList parentValImageWaveletGroups = ReadSymbolTable(bytes);
    std::shared_ptr<RansTable> parentBlockSymbolTable = std::make_shared<RansTable>(parentValImageWaveletGroups, PROBABILITY_RES);

    // read parent val block header
    CompressedImageBlockHeader parentValImageHeader = CompressedImageBlockHeader::Read(bytes, parentValImageParents, parentValsWidth, parentValsHeight);

    // read parent val image
    IteratorPtr<block_t> bodyStream = IteratorPtr<block_t>(bytes.castToBlocks());
    // TODO this is dumb - move bytes by same amount
    SkipVector<block_t>(bytes);

    std::shared_ptr <CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(parentValImageHeader, *bodyStream, parentBlockSymbolTable);

    // Decode parent values
    std::vector<symbol_t> rawParentVals = block->GetBottomLevelPixels();

    // Re-swizzle
    // TODO there are nicer ways of doing this
    std::vector<symbol_t> parentVals;
    for (int y = 0; y < parentValsHeight; y += 2)
    {
        for (int x = 0; x < parentValsWidth; x += 2)
        {
            parentVals.push_back(rawParentVals[y * parentValsWidth + x]);
            if (x + 1 < parentValsWidth)
                parentVals.push_back(rawParentVals[y * parentValsWidth + x + 1]);
            if (y + 1 < parentValsHeight)
                parentVals.push_back(rawParentVals[(y + 1) * parentValsWidth + x]);
            if (x + 1 < parentValsWidth && y + 1 < parentValsHeight)
                parentVals.push_back(rawParentVals[(y + 1) * parentValsWidth + x + 1]);
        }
    }

    if (rawParentVals.size() != parentVals.size())
        std::cerr << "De-swizzle changed num. parent vals!" << std::endl;

    // read block headers
    std::vector<CompressedImageBlockHeader> headers;
    auto parentVal = parentVals.begin();
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            size_t blockW = std::min(header.width - blockStartX, header.blockSize);
            size_t blockH = std::min(header.height - blockStartY, header.blockSize);
            WaveletLayerSize rootParentSize = WaveletLayerSize(blockW, blockH).GetRoot().GetParentSize();
            std::vector<symbol_t> blockParents;
            blockParents.insert(blockParents.end(), parentVal, parentVal + rootParentSize.GetPixelCount());
            parentVal += rootParentSize.GetPixelCount();
            CompressedImageBlockHeader header = CompressedImageBlockHeader::Read(bytes, blockParents, blockW, blockH);
            headers.push_back(header);
        }
    }

    std::cout << headers.size() << " block headers read." << std::endl;

    // get theoretical RAM usage
    size_t memoryOverhead = 0;
    for (auto header : headers)
        memoryOverhead += header.GetMemoryFootprint();
    memoryOverhead += globalSymbolTable->GetMemoryFootprint();
    std::cout << "Header memory overhead: " << memoryOverhead << " bytes." << std::endl;

    std::shared_ptr<CompressedImage> image = std::make_shared<CompressedImage>();
    image->header = header;
    image->globalSymbolTable = globalSymbolTable;
    image->blockHeaders = std::move(headers);
    image->currentCacheSize = memoryOverhead;
    image->memoryOverhead = memoryOverhead;
    image->compressedImageBlocks.resize(image->GetWidthInBlocks()*image->GetHeightInBlocks());

    return std::move(image);
}

std::shared_ptr<CompressedImage> CompressedImage::Deserialize(ByteIterator &bytes)
{
    // read headers
    std::shared_ptr<CompressedImage> image = GenerateFromStream(bytes);

    CompressedImageHeader& header = image->header;

    size_t widthInBlocks = header.width / header.blockSize;
    if (header.width % header.blockSize != 0)
        widthInBlocks += 1;

    // read block bodies
    std::vector<std::shared_ptr<CompressedImageBlock>>& blocks = image->compressedImageBlocks;
    auto blockHeader = image->blockHeaders.begin();
    std::cout << "Decoding block bodies..." << std::endl;
    // hack to deal with late evaluation of block body
    size_t lastBlockStart = blockHeader->GetBlockPos();
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            uint32_t blockX = blockStartX / header.blockSize;
            uint32_t blockY = blockStartY / header.blockSize;
            uint32_t blockW = std::min(header.width - blockStartX, header.blockSize);
            uint32_t blockH = std::min(header.height - blockStartY, header.blockSize);

            // seek to block start
            bytes += blockHeader->GetBlockPos() - lastBlockStart;
            lastBlockStart = blockHeader->GetBlockPos();

            // read parent val image
            IteratorPtr<block_t> bodyStream = IteratorPtr<block_t>(bytes.castToBlocks());

            std::shared_ptr <CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(*blockHeader, *bodyStream, image->globalSymbolTable);
            if (blockX == 50 && blockY == 50)
                std::cout << "Chosen block hash: " << HashVec(block->GetBottomLevelPixels()) << std::endl;

            size_t blockIdx = blockY * widthInBlocks + blockX;
            blocks[blockIdx] = block;


            ++blockHeader;
        }
    }

    return std::move(image);
}

// Opens file for streaming
std::shared_ptr<CompressedImage> CompressedImage::OpenStream(std::string filename)
{
    // Open file 
    FastFileStream compressedFile(filename);

    // error
    if (compressedFile.Failed())
        return std::shared_ptr<CompressedImage>();

    ByteIteratorPtr bytes = StreamFromFile<uint8_t>(&compressedFile);

    // read headers
    std::shared_ptr<CompressedImage> image = GenerateFromStream(*bytes);
    std::cout << "Stream pos: " << compressedFile.GetPosition() << std::endl;
    image->blockBodiesStart = compressedFile.GetPosition();
    // close + reopen file (std::move gives buggy behaviour)
    compressedFile.Close();
    image->fileStream = FastFileStream(filename);

    return image;
}

// Gets/creates the block for the given index
std::shared_ptr<CompressedImageBlock> CompressedImage::GetBlock(size_t index)
{
    std::shared_ptr<CompressedImageBlock> foundBlock = compressedImageBlocks[index];

    // create block if needed
    if (!foundBlock)
    {
        CompressedImageBlockHeader& header = blockHeaders[index];

        // Create new byte iterator at block body start
        IteratorPtr<block_t> blocks = StreamFromFile<block_t>(&fileStream, blockBodiesStart + header.GetBlockPos());

        std::shared_ptr <CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(header, *blocks, globalSymbolTable);

        compressedImageBlocks[index] = block;

        // add memory overhead of block
        currentCacheSize += block->GetMemoryFootprint();

        return block;
    }
    else
    {
        return foundBlock;
    }
}

std::vector<symbol_t> CompressedImage::GetBottomLevelPixels()
{
    std::vector<symbol_t> pixels;
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
            size_t blockIdx = blockY * GetWidthInBlocks() + blockX;

            // creates if necessary
            std::shared_ptr<CompressedImageBlock> block = GetBlock(blockIdx);

            currentCacheSize -= block->GetMemoryFootprint();
            std::vector<symbol_t> blockPixels = block->GetBottomLevelPixels();
            currentCacheSize += block->GetMemoryFootprint();

            // copy pixels
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
    uint32_t topLevel = GetTopLOD();
    for (uint32_t blockStartY = 0; blockStartY < header.height; blockStartY += header.blockSize)
    {
        for (uint32_t blockStartX = 0; blockStartX < header.width; blockStartX += header.blockSize)
        {
            uint32_t blockX = blockStartX / header.blockSize;
            uint32_t blockY = blockStartY / header.blockSize;
            size_t blockIdx = blockY * GetWidthInBlocks() + blockX;

            // DON'T use GetBlock, since that would create a block if none exists
            std::shared_ptr<CompressedImageBlock> block = compressedImageBlocks[blockIdx];

            if (!block)
                blockLevels.push_back(topLevel);
            else
                blockLevels.push_back(block->GetLevel());
        }
    }
    return blockLevels;
}

symbol_t CompressedImage::GetPixel(size_t x, size_t y)
{
    uint32_t blockX = x / header.blockSize;
    uint32_t blockY = y / header.blockSize;
    uint32_t subBlockX = x % header.blockSize;
    uint32_t subBlockY = y % header.blockSize;
    size_t blockIdx = blockY * GetWidthInBlocks() + blockX;

    std::shared_ptr<CompressedImageBlock> foundBlock = compressedImageBlocks[blockIdx];

    // handle nonexistant block
    if (!foundBlock)
    {
        CompressedImageBlockHeader& blockHeader = blockHeaders[blockIdx];
        uint32_t rootStride = (header.blockSize / 2);

        // If root value is being read, skip block creation
        if (subBlockX % rootStride == 0
            && subBlockY % rootStride == 0)
        {
            uint32_t rootWidth = (blockHeader.getWidth() + (rootStride - 1)) / rootStride;
            uint32_t rootX = subBlockX / rootStride;
            uint32_t rootY = subBlockY / rootStride;
            // Read directly from root vals
            return blockHeader.GetParentVals()[rootY * rootWidth + rootX];
        }
        // Else, need to create block so it can be decoded
        else
        {
            // Create new byte iterator at block body start
            IteratorPtr<block_t> blocks = StreamFromFile<block_t>(&fileStream, blockBodiesStart + blockHeader.GetBlockPos());

            std::shared_ptr <CompressedImageBlock> block = std::make_shared<CompressedImageBlock>(blockHeader, *blocks, globalSymbolTable);

            compressedImageBlocks[blockIdx] = block;

            // add memory overhead of block
            currentCacheSize += block->GetMemoryFootprint();

            foundBlock = block;
        }
    }

    currentCacheSize -= foundBlock->GetMemoryFootprint();
    symbol_t value = foundBlock->GetPixel(subBlockX, subBlockY);
    currentCacheSize += foundBlock->GetMemoryFootprint();
    
    return value;
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

uint32_t CompressedImage::GetTopLOD() const
{
    uint32_t LOD = 0;
    WaveletLayerSize size = WaveletLayerSize(header.blockSize, header.blockSize);
    while (!size.IsRoot())
    {
        ++LOD;
        size = size.GetParentSize();
    }
    // add 1 for "parent vals" LOD
    return LOD + 1;
}

size_t CompressedImage::GetMemoryUsage() const
{
    size_t totalMemUsage = currentCacheSize;
    // this isn't even slightly accurate, but better than nothing
    size_t mapSize = compressedImageBlocks.capacity() * sizeof(std::shared_ptr<CompressedImageBlock>);
    return currentCacheSize + mapSize;
}

void CompressedImage::ClearBlockCache()
{
    for (int i = 0; i < compressedImageBlocks.size(); ++i)
    {
        // replace with null ptr
        if (compressedImageBlocks[i])
            compressedImageBlocks[i] = std::shared_ptr<CompressedImageBlock>();
    }
}