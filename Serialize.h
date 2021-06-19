#pragma once

#include <vector>

// std::hash is garbage
size_t HashVec(std::vector<uint16_t> const& vec);

template<typename T>
void WriteValue(std::vector<uint8_t>& outputBytes, T value)
{
    size_t writePos = outputBytes.size();
    outputBytes.resize(outputBytes.size() + sizeof(value));
    memcpy(&outputBytes[writePos], &value, sizeof(value));
}

template<typename T>
T ReadValue(const std::vector<uint8_t>& inputBytes, uint64_t& readPos)
{
    T value;
    memcpy(&value, &inputBytes[readPos], sizeof(value));
    readPos += sizeof(value);
    return value;
}

template<typename T>
struct VectorHeader
{
    VectorHeader()
        : count(-1)
    {

    }
    VectorHeader(const std::vector<T>& values)
    {
        count = values.size();
    }
    uint32_t count;
};

// helper function
template<typename T>
void WriteVector(std::vector<uint8_t>& outputBytes, const std::vector<T>& vector)
{
    uint64_t writePos = outputBytes.size();
    // write header
    VectorHeader<T> vectorHeader = VectorHeader<T>(vector);
    WriteValue(outputBytes, vectorHeader);

    // write vector values
    writePos = outputBytes.size();
    uint64_t vectorSize = vector.size() * sizeof(T);
    outputBytes.resize(outputBytes.size() + vectorSize);
    memcpy(&outputBytes[writePos], &vector[0], vectorSize);
}

// helper function
template<typename T>
std::vector<T> ReadVector(const std::vector<uint8_t>& inputBytes, uint64_t& readPos)
{
    // read header
    VectorHeader<T> vectorHeader = ReadValue<VectorHeader<T>>(inputBytes, readPos);

    // read vector values
    std::vector<T> outputVector;
    outputVector.resize(vectorHeader.count);
    uint64_t vectorSize = outputVector.size() * sizeof(T);
    memcpy(&outputVector[0], &inputBytes[readPos], vectorSize);
    readPos += vectorSize;

    return std::move(outputVector);
}