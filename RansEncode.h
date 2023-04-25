#pragma once

#include <vector>
#include <unordered_map>

#include "Serialize.h"

// Ported from my old Python implementation

// TODO can we replace this with sorted symbol count vector?
typedef std::unordered_map<uint16_t, uint32_t> SymbolCountDict;

// helper struct
struct SymbolPDF
{
	SymbolPDF()
		: symbol(-1), pdf(-1)
	{
	}
	SymbolPDF(uint16_t symbol, uint32_t pdf)
		: symbol(symbol), pdf(pdf)
	{

	}

	uint16_t symbol;
	uint32_t pdf;
};

std::vector<SymbolPDF> EntropySortSymbols(const SymbolCountDict &dict);

// 16Kx16K shouldn't overflow 32-bit counts
class RansEntry
{
public:
	RansEntry()
	{};
	RansEntry(uint16_t symbol, uint32_t cdf)
		: symbol(symbol), cdf(cdf)
	{

	}
	uint16_t symbol;
	// used for probabilities
	uint32_t cdf;
};

// represents a group of symbols with the same count
class RansGroup
{
public:
	RansGroup()
	{};
	RansGroup(uint16_t start, uint16_t count, uint32_t pdf, uint32_t cdf)
		: start(start), count(count), pdf(pdf), cdf(cdf)
	{

	}
	uint16_t start;
	uint16_t count;
	// used for probabilities
	uint32_t pdf;
	uint32_t cdf;
};

class CDFTable
{
public:
	CDFTable() {};
	CDFTable(const SymbolCountDict &counts, uint32_t probabilityRes);

	uint16_t GetSymbol(RansGroup group, uint32_t symbolIndex);
	uint16_t GetSymbolIdxInGroup(RansGroup group, uint16_t symbol);
	RansGroup GetSymbolGroup(uint32_t symbolCDF);

private:
	// set to group idx of lowest group with >1 count
	// 98% of wavelets occur before this, we can use a fast-path for them since group_idx == symbol_idx
	uint32_t pivot;
	std::vector<uint16_t> symbols;
	// TODO SIMD?
	// TODO we could likely drop at least 8 bits off group CDFs with negligible size increase
	std::vector<uint32_t> groupCDFs;
	std::vector<uint16_t> groupStarts;
};

class RansTable
{
public:
	RansTable() {};
	RansTable(SymbolCountDict unquantizedCounts, uint32_t probabilityRes);

	inline RansEntry GetSymbolEntry(const uint16_t symbol);
	inline uint16_t GetSymbolIdxInGroup(const RansGroup group, const uint16_t symbol);
	inline RansGroup GetSymbolGroupFromFreq(const uint32_t prob);
	inline uint16_t GetSymbolEntryFromGroup(const RansGroup group, const uint16_t subIndex);

	// Get RAM usage
	size_t GetMemoryFootprint() const;

private:
	// TODO do we need both of these?
	std::unordered_map<uint16_t, RansEntry> symbolTable;
	CDFTable cdfTable; // used for CDF probability lookups when decoding
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
	RansState(std::shared_ptr<VectorStream<uint8_t>> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize);
	// fast constructor if rANS table is already generated
	RansState(std::shared_ptr<VectorStream<uint8_t>> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize);

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
	std::shared_ptr<VectorStream<uint8_t>> compressedBlocks;
	// so we can reuse one hunk of memory
	std::shared_ptr<RansTable> ransTable;
};