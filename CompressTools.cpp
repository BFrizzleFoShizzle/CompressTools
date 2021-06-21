// CompressTools.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define FREEIMAGE_LIB

#include <FreeImage.h>

#include <iostream>
#include <stdint.h>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <assert.h>
#include <algorithm>
#include <fstream>

#include "RansEncode.h"
#include "CompressedImage.h"

void GetSymbolEntropy(std::vector<uint16_t> symbols)
{
    std::cout << "Counting symbols..." << std::endl;
    std::unordered_map<uint16_t, uint64_t> symbolCounts;
    for (auto symbol : symbols)
    {
        symbolCounts.try_emplace(symbol, 0);
        symbolCounts[symbol] += 1;
    }
    std::cout << "Getting entropy..." << std::endl;
    uint64_t numSymbols = symbols.size();
    double totalEntropy = 0.0;
    std::vector<uint16_t> symbolValues;
    std::vector<double> symbolEntropies;
    
    for (auto symbolCount : symbolCounts)
    {
        //std::cout << symbolCount.first << " " << symbolCount.second << std::endl;
        symbolValues.push_back(symbolCount.first);
        double count = (double)symbolCount.second;
        double symbolProb = count / numSymbols;
        double symbolEntropy = count * -log2(symbolProb);
        symbolEntropies.push_back(symbolEntropy);
        totalEntropy += symbolEntropy;
    }

    std::cout << "Total entropy: " << (uint64_t)totalEntropy << " bits " << std::endl;
    std::cout << "Total entropy: " << (uint64_t)(totalEntropy/8) << " bytes " << std::endl;
}

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char* message) {
    printf("\n*** ");
    if (fif != FIF_UNKNOWN) {
        printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
    }
    printf(message);
    printf(" ***\n");
}


int main()
{
    std::cout << "Opening image..." << std::endl;
    FreeImage_Initialise();
    FreeImage_SetOutputMessage(FreeImageErrorHandler);
    FIBITMAP *bitmap = FreeImage_Load(FREE_IMAGE_FORMAT::FIF_TIFF, "../../../fullmap.tif", TIFF_DEFAULT);
    
    int width = FreeImage_GetWidth(bitmap);
    int height = FreeImage_GetHeight(bitmap);
    int precision = FreeImage_GetBPP(bitmap);
    if (precision == 16)
    {
        std::cout << "Uncompressed size: " << width*height*sizeof(uint16_t) << " bytes" << std::endl;
        std::cout << "Reading pixels..." << std::endl;

        std::vector<uint16_t> values;
        values.resize(width * height);
        // Undo Free_Image transform
        // Actually tested it properly this time...
        for (int y = 0; y < height; ++y)
        {
            BYTE* bits = FreeImage_GetScanLine(bitmap, (height-1)-y);
            memcpy(&values[y*width], bits, width * sizeof(uint16_t));
        }

        std::cout << "Generating wavelet image..." << std::endl;
        std::shared_ptr<CompressedImage> compressedImage = std::make_shared<CompressedImage>(values, width, height, 32);
        std::cout << "Serializing..." << std::endl; 
        std::vector<uint8_t> imageBytes = compressedImage->Serialize();
        std::cout << "Final encoded bytes: " << imageBytes.size() << std::endl;
        std::shared_ptr<CompressedImage> decodedImage = CompressedImage::Deserialize(imageBytes);

        std::cout << decodedImage->GetTopLOD() << std::endl;
        
        std::vector<uint8_t> blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        decodedImage->GetPixel(0, 0);
        blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        decodedImage->GetPixel(64, 0);
        blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        decodedImage->GetPixel(32, 0);
        blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        decodedImage->GetPixel(16, 0);
        blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        decodedImage->GetPixel(8, 0);
        blockLevels = decodedImage->GetBlockLevels();
        std::cout << int(blockLevels[0]) << std::endl;
        
        // test aligned reads
        std::cout << "Testing aligned reads..." << imageBytes.size() << std::endl;
        for (int y = 1024; y < 2048; y += 4)
        {
            for (int x = 1024; x < 2048; x += 4)
            {
                uint16_t sourcePixel = values[y * width + x];
                uint16_t decodedPixel = decodedImage->GetPixel(x, y);
                assert(decodedPixel == sourcePixel);
                if (decodedPixel != sourcePixel)
                {
                    std::cout << "Decoded pixel values at (" << x << ", " << y << ") did not match." << std::endl;
                }
            }
        }


        std::cout << "Decoding bottom-level pixels..." << imageBytes.size() << std::endl;
        std::vector<uint16_t> decodedPixels = decodedImage->GetBottomLevelPixels();
        for (int i = 0; i < values.size(); ++i)
        {
            assert(values[i] == decodedPixels[i]);
            if (values[i] != decodedPixels[i])
            {
                std::cout << "Decoded wavelet values at " << i << " did not match." << std::endl;
            }
        }
        std::cout << "Values checked!" << std::endl;

        // write to disk
        std::cout << "Writing bytes..." << std::endl;
        std::ofstream compressedFile("../../../fullmap.cif", std::ios::binary);
        if (compressedFile.is_open())
        {
            compressedFile.write((const char*)&imageBytes[0], imageBytes.size());
            compressedFile.close();
        }
        else
        {
            std::cerr << "Error opening output file!";
        }
    }
    
    FreeImage_Unload(bitmap);
    FreeImage_DeInitialise();
}
