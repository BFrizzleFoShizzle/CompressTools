#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include "Release_Assert.h"
#include "Precision.h"

// std::hash is garbage
size_t HashVec(std::vector<uint16_t> const& vec);

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

// tellg() + seekg() have TERRIBLE performance on windows, so we roll our own
class FastFileStream
{
public:
    // TODO remove
    FastFileStream();
    FastFileStream(std::string filename);
    void Seek(size_t newPosition);
    size_t GetPosition() const;
    void Read(void* dest, size_t length);
    void Close();
private:
    size_t position;
    std::basic_ifstream<uint8_t> fileStream;
};

template<typename T>
class Stream
{
public:
    virtual T operator*() = 0;
    virtual void operator++() = 0;
    virtual void operator--() = 0;
    virtual void operator+=(size_t count) = 0;
    virtual void operator-=(size_t count) = 0;
    // clone stream at position
    virtual std::shared_ptr<Stream<T>> clone() = 0;
    virtual Stream<symbol_t>* castToBlocks() = 0;
};

template<typename T>
class VectorIOStream : public Stream<T>
{
public:
    VectorIOStream(const std::vector<uint8_t>* input)
        : input(input), position(0)
    {

    }
    VectorIOStream(const std::vector<uint8_t>* input, size_t position)
        : input(input), position(position)
    {

    }
    T operator*() override
    {
        size_t positionCopy = position;
        return ReadValue<T>(*input, positionCopy);
    }
    void operator++() override
    {
        position += sizeof(T);
    }
    void operator--() override
    {
        position -= sizeof(T);
    }
    void operator+=(size_t count) override
    {
        position += count * sizeof(T);
    }
    void operator-=(size_t count) override
    {
        position -= count * sizeof(T);
    }
    std::shared_ptr<Stream<T>> clone() override
    {
        return std::shared_ptr<Stream<T>>(new VectorIOStream<T>(input, position));
    }
    // clone stream at position
    Stream<symbol_t>* castToBlocks()
    {
        return new VectorIOStream<symbol_t>(input, position);
    }
private:
    const std::vector<uint8_t>* input;
    size_t position;
};

template<typename T>
class FileIOStream : public Stream<T>
{
public:
    FileIOStream(FastFileStream* bytes)
        : bytes(bytes), position(bytes->GetPosition())
    {

    }
    FileIOStream(FastFileStream* bytes, size_t position)
        : bytes(bytes), position(position)
    {

    }
    T operator*() override
    {
        // seek before read in case something else has moved the underlying filestream in the meantime
        bytes->Seek(position);
        T value;
        bytes->Read(&value, sizeof(value));
        return value;
    }
    void operator++()
    {
        position += sizeof(T);
    }
    void operator--()
    {
        position -= sizeof(T);
    }
    void operator+=(size_t count) override
    {
        position += count * sizeof(T);
    }
    void operator-=(size_t count) override
    {
        position -= count * sizeof(T);
    }
    std::shared_ptr<Stream<T>> clone() override
    {
        return std::shared_ptr<Stream<T>>(new FileIOStream<T>(bytes, position));
    }
    // clone stream at position
    Stream<symbol_t>* castToBlocks()
    {
        return new FileIOStream<symbol_t>(bytes, position);
    }
private:
    FastFileStream* bytes;
    size_t position;
};


template <typename T>
using Iterator = Stream<T>;
template <typename T>
using IteratorPtr = std::shared_ptr<Stream<T>>;
typedef Iterator<uint8_t> ByteIterator;
typedef IteratorPtr<uint8_t> ByteIteratorPtr;

template <typename T>
IteratorPtr<T> StreamFromVector(const std::vector<uint8_t>* input)
{
    return IteratorPtr<T>(new VectorIOStream<T>(input));
}

template <typename T>
IteratorPtr<T> StreamFromFile(FastFileStream* bytes)
{
    return IteratorPtr<T>(new FileIOStream<T>(bytes));
}

template <typename T>
IteratorPtr<T> StreamFromFile(FastFileStream* bytes, size_t position)
{
    return IteratorPtr<T>(new FileIOStream<T>(bytes, position));
}

template<typename T, typename BT>
T ReadValue(Iterator<BT> &input)
{
    assert(sizeof(T) % sizeof(BT) == 0);
    T value;
    BT* valueBlocks = reinterpret_cast<BT*>(&value);

    // basically memcpy
    for (size_t blockPos = 0; blockPos < (sizeof(value) / sizeof(BT)); ++blockPos)
    {
        *valueBlocks = *input;
        ++valueBlocks;
        ++input;
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
std::vector<T> ReadVector(ByteIterator &bytes)
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

template<typename T>
class VectorStream
{
public:
    virtual void push_back(T val) = 0;
    virtual void pop_back() = 0;
    virtual size_t size() = 0;
    virtual T back() = 0;
    virtual std::vector<T> get_vec() = 0;
    virtual std::shared_ptr<Stream<T>> get_stream() = 0;
};

// TODO this name is terrible
// "vector stream from a vector"
template<typename T>
class VectorVectorStream : public VectorStream<T>
{
public:
    VectorVectorStream(std::vector<T>& vector)
        : vector(vector) {};
    VectorVectorStream() {};
    void push_back(T val) override
    {
        vector.push_back(val);
    }
    void pop_back() override
    {
        vector.pop_back();
    }
    size_t size() override
    {
        return vector.size();
    }
    T back() override
    {
        return vector.back();
    }
    std::vector<T> get_vec() override
    {
        return vector;
    }
    std::shared_ptr<Stream<T>> get_stream() override
    {
        assert(false);
        return std::shared_ptr<Stream<T>>();
    }
private:
    std::vector<T> vector;
};

template<typename T>
class ByteVectorStream : public VectorStream<T>
{
public:
    ByteVectorStream(ByteIterator& bytes, size_t count)
        : count(count)
    {
        basePtr = bytes.clone();
        backPtr = basePtr->clone();
        *backPtr += count - 1;
    }
    void push_back(T val) override
    {
        // TODO
        assert(false);
    }
    void pop_back() override
    {
        --(*backPtr);
        --count;
    }
    size_t size() override
    {
        return count;
    }
    T back() override
    {
        return **backPtr;
    }
    std::vector<T> get_vec() override
    {
        // TODO
        assert(false);
        return std::vector<T>();
    }
    std::shared_ptr<Stream<T>> get_stream() override
    {
        return basePtr->clone();
    }
    ByteIteratorPtr basePtr;
    ByteIteratorPtr backPtr;
    size_t count;
};

// swap back and front
// has *EXTREME* late binding fo dat mad perf gainz
template<typename T>
class ReverseVectorStream : public VectorStream<T>
{
public:
    ReverseVectorStream(Iterator<T>& bytes, size_t count)
        : count(count)
    {
        basePtr = bytes.clone();
    }
    ReverseVectorStream(Iterator<T>& bytes)
        : count(-1)
    {
        basePtr = bytes.clone();
    }
    void push_back(T val) override
    {
        // TODO
        assert(false);
    }
    void pop_back() override
    {
        init();
        ++(*basePtr);
        --count;
    }
    size_t size() override
    {
        init();
        return count;
    }
    T back() override
    {
        init();
        return **basePtr;
    }
    std::vector<T> get_vec() override
    {
        // TODO
        assert(false);
        return std::vector<T>();
    }
    std::shared_ptr<Stream<T>> get_stream() override
    {
        init();
        return basePtr->clone();
    }
private:
    void init()
    {
        if (count == -1)
        {
            // read header
            VectorHeader<T> vectorHeader = ReadValue<VectorHeader<T>>(*basePtr);
            count = vectorHeader.count;
        }
    }
    IteratorPtr<T> basePtr;
    size_t count;
    bool initialized;
};

template<typename T>
std::shared_ptr<VectorStream<T>> StreamVector(Iterator<T>& bytes)
{
    // read header
    VectorHeader<T> vectorHeader = ReadValue<VectorHeader<T>>(bytes);
    
    // create stream
    std::shared_ptr<VectorStream<T>> out = std::shared_ptr<VectorStream<T>>(new ByteVectorStream<T>(bytes, vectorHeader.count));
    
    // seek to end of stream
    bytes += vectorHeader.count * sizeof(T);

    return out;
}

// WARNING: if consumeImmediate = true, does NO READS and DOES NOT move ptr forward!
template<typename T>
std::shared_ptr<VectorStream<T>> ReverseStreamVector(Iterator<T>& values, bool consumeImmediate = false)
{
    std::shared_ptr<VectorStream<T>> out;

    if (consumeImmediate)
    {
        // read header
        VectorHeader<T> vectorHeader = ReadValue<VectorHeader<T>>(values);

        // create stream
        out = std::shared_ptr<VectorStream<T>>(new ReverseVectorStream<T>(values, vectorHeader.count));

        // seek to end of stream
        values += vectorHeader.count * sizeof(T);
    }
    else
    {
        // create stream (it reads it's own header)
        out = std::shared_ptr<VectorStream<T>>(new ReverseVectorStream<T>(values));
    }

    return out;
}
