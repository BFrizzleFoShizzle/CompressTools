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