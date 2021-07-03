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

ByteIteratorPtr ByteStreamFromFile(std::basic_ifstream<uint8_t>* bytes)
{
    return std::shared_ptr<Stream<uint8_t>>(new FileIOStream<uint8_t>(bytes));
}