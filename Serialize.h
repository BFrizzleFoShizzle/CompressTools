#pragma once

#include <vector>
#include <string>
#include <streambuf>

// std::hash is garbage
size_t HashVec(std::vector<uint16_t> const& vec);

typedef std::istreambuf_iterator<uint8_t> ByteIterator;

// TODO remove vector methods and just use iterators
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
T ReadValue(ByteIterator &inputBytes)
{
    T value;
    uint8_t* valueBytes = reinterpret_cast<uint8_t*>(&value);

    // basically memcpy
    for (size_t bytePos = 0; bytePos < sizeof(value); ++bytePos)
    {
        *valueBytes = *inputBytes;
        ++valueBytes;
        ++inputBytes;
    }

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

// helper function
template<typename T>
std::vector<T> ReadVector(ByteIterator bytes)
{
    // read header
    VectorHeader<T> vectorHeader = ReadValue<VectorHeader<T>>(bytes);

    // read vector values
    std::vector<T> outputVector;
    outputVector.resize(vectorHeader.count);
    uint64_t vectorSize = outputVector.size() * sizeof(T);

    uint8_t* valuesBytes = reinterpret_cast<uint8_t*>(&outputVector[0]);
    // basically memcpy
    for (size_t bytePos = 0; bytePos < vectorSize; ++bytePos)
    {
        *valuesBytes = *bytes;
        ++valuesBytes;
        ++bytes;
    }

    return std::move(outputVector);
}

// TODO remove if possible
// helper methods to convert std::vector to istream
// adapted from https://stackoverflow.com/questions/7781898/get-an-istream-from-a-char
struct membuf : std::basic_streambuf<uint8_t>
{
    membuf(uint8_t* begin, uint8_t* end) {
        this->setg(begin, begin, end);
    }
    membuf(std::vector<uint8_t> &vector)
        : membuf(&vector[0], &vector[vector.size()])
    {
    }
};