#include "Serialize.h"

// roughly adapted from https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector
std::size_t HashVec(std::vector<uint16_t> const& vec)
{
    std::size_t seed = vec.size();
    for (auto& i : vec) {
        seed ^= i + 0x9e37 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

ByteIteratorPtr ByteStreamFromVector(const std::vector<uint8_t>* input)
{
    return std::shared_ptr<Stream<uint8_t>>(new VectorIOStream<uint8_t>(input));
}

ByteIteratorPtr ByteStreamFromFile(FastFileStream* bytes)
{
    return std::shared_ptr<Stream<uint8_t>>(new FileIOStream<uint8_t>(bytes));
}

ByteIteratorPtr ByteStreamFromFile(FastFileStream* bytes, size_t position)
{
    return std::shared_ptr<Stream<uint8_t>>(new FileIOStream<uint8_t>(bytes, position));
}

FastFileStream::FastFileStream(std::string filename)
    : fileStream(filename, std::ios::binary), position(0)
{

}

FastFileStream::FastFileStream()
    : fileStream(), position(-1)
{

}

void FastFileStream::Seek(size_t newPosition)
{
    if (position != newPosition)
    {
        // TODO late bind?
        fileStream.seekg(newPosition);
        position = newPosition;
    }
}

size_t FastFileStream::GetPosition() const
{
    return position;
}

void FastFileStream::Read(void* dest, size_t length)
{
    fileStream.read(reinterpret_cast<uint8_t*>(dest), length);
    position += length;
}

void FastFileStream::Close()
{
    fileStream.close();
}
