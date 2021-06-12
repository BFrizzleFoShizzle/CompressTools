#pragma once

#include <vector>
#include <unordered_map>

// Ported from my old Python implementation

// TODO can we replace this with sorted symbol count vector?
typedef std::unordered_map<uint16_t, uint32_t> SymbolCountDict;

SymbolCountDict GenerateQuantizedCounts(SymbolCountDict unquantizedCounts, size_t probabilityRange);

// helper struct
struct SymbolCount
{
	SymbolCount()
		: symbol(-1), count(-1)
	{
	}
	SymbolCount(uint16_t symbol, uint32_t count)
		: symbol(symbol), count(count)
	{

	}

	uint16_t symbol;
	uint32_t count;

};

std::vector<SymbolCount> EntropySortSymbols(SymbolCountDict dict);

// 16Kx16K shouldn't overflow 32-bit counts
class RansEntry
{
public:
	RansEntry()
	{};
	RansEntry(uint16_t symbol, uint32_t count, uint32_t cumulativeCount)
		: symbol(symbol), count(count), cumulativeCount(cumulativeCount)
	{

	}
	uint16_t symbol;
	// used for probabilities
	uint32_t count;
	uint32_t cumulativeCount;
};


class RansTable
{
public:
	RansTable() {};
	RansTable(SymbolCountDict counts);

	RansEntry GetSymbolEntry(uint16_t symbol);
	RansEntry GetSymbolEntryFromFreq(uint32_t prob);

private:
	// TODO do we need both of these?
	std::unordered_map<uint16_t, RansEntry> symbolTable;
	std::vector<RansEntry> cdfTable; // used for CDF probability lookups when decoding

};

class RansState
{
public:
	RansState();
	// rANS state - size of state = size of probability + size of output block
	RansState(uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize);
	// fast constructor if rANS table is already generated
	RansState(uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize);
	// TODO serialize whole thing?
	RansState(std::vector<uint8_t> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize);
	// fast constructor if rANS table is already generated
	RansState(std::vector<uint8_t> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize);

	// Encode symbol
	void AddSymbol(uint16_t symbol);
	// Decode symbol
	uint16_t ReadSymbol();

	// this isn't really const...
	const std::vector<uint8_t> GetCompressedBlocks();
	uint64_t GetRansState();
	bool HasData();

	// true if initialized properly
	bool IsValid();

private:
	uint64_t probabilityRange;
	uint64_t blockSize;
	uint64_t stateMin;
	uint64_t stateMax;
	uint64_t ransState;
	std::vector<uint8_t> compressedBlocks;
	// so we can reuse one hunk of memory
	std::shared_ptr<RansTable> ransTable;
};