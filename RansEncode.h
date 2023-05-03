#pragma once

#include <vector>
#include <unordered_map>
#include "Precision.h"
#include "Serialize.h"

// Ported from my old Python implementation

typedef std::unordered_map<symbol_t, count_t> SymbolCountDict;

// helper struct
struct SymbolPDF
{
	SymbolPDF()
		: symbol(-1), pdf(-1)
	{
	}

	SymbolPDF(symbol_t symbol, count_t pdf)
		: symbol(symbol), pdf(pdf)
	{

	}

	symbol_t symbol;
	count_t pdf;
};

std::vector<SymbolPDF> EntropySortSymbols(const SymbolCountDict &dict);

// 16Kx16K shouldn't overflow 32-bit counts
class RansEntry
{
public:
	RansEntry()
	{};
	RansEntry(symbol_t symbol, uint32_t cdf)
		: symbol(symbol), cdf(cdf)
	{

	}
	symbol_t symbol;
	// used for probabilities
	uint32_t cdf;
};

// represents a group of symbols with the same count
class RansGroup
{
public:
	RansGroup()
	{};
	RansGroup(symidx_t start, symidx_t count, uint32_t pdf, uint32_t cdf)
		: start(start), count(count), pdf(pdf), cdf(cdf)
	{

	}
	symidx_t start;
	symidx_t count;
	// used for probabilities
	uint32_t pdf;
	uint32_t cdf;
};

typedef std::vector<std::pair<prob_t, std::vector<symbol_t>>> TableGroupList;

class CDFTable
{
public:
	CDFTable() {};
	// encoding
	CDFTable(const SymbolCountDict& unquantizedCounts, uint16_t probabilityRes,
		std::unordered_map<symbol_t, RansGroup>& symbolGroupsOut, std::unordered_map<symbol_t, symidx_t>& symbolSubIdxOut);
	// decoding
	CDFTable(const TableGroupList& groupList, uint32_t probabilityRes);

	inline symbol_t GetSymbol(RansGroup group, symidx_t symbolIndex);
	symidx_t GetSymbolSubIdx(RansGroup group, symbol_t symbol);
	inline void GetSymbolGroup(prob_t symbolCDF, RansGroup& out);
	// used for writing to disk
	// [group](PDF, symbols[])
	TableGroupList GenerateGroupCDFs();

private:
	// set to group idx of lowest group with >1 count
	// 98% of wavelets occur before this, we can use a fast-path for them since group_idx == symbol_idx
	group_t pivotIdx;
	prob_t pivotCDF;
	// start CDF of "raw" values that are stored uncompressed
	// 95% of symbol entries are handled by this, which keeps symbols[] small
	//use of rawCDF increases file size by up to 1% but increases decode speed of both raw and non-raw symbols
	prob_t rawCDF;
	std::vector<symbol_t> symbols;
	// TODO SIMD?
	std::vector<prob_t> groupCDFs;
	std::vector<symidx_t> groupStarts;
};

// TODO extend CDFTable?
class RansTable
{
public:
	RansTable() {};
	// encoding
	RansTable(SymbolCountDict unquantizedCounts, uint32_t probabilityRes);
	// decoding
	RansTable(const TableGroupList &unquantizedCounts, uint32_t probabilityRes);

	inline RansGroup GetSymbolGroup(const symbol_t symbol);
	inline symidx_t GetSymbolSubIdx(const symbol_t symbol);
	inline void GetSymbolGroupFromFreq(const prob_t prob, RansGroup& out);
	inline symbol_t GetSymbolFromGroup(const RansGroup group, const symidx_t subIndex);

	// used for writing to disk
	// [group](PDF, symbols[])
	TableGroupList GenerateGroupCDFs();

	// Get RAM usage
	size_t GetMemoryFootprint() const;

private:
	CDFTable cdfTable; // used for CDF probability lookups when decoding
	// TODO do we need both of these? used for encoding
	std::unordered_map<symbol_t, RansGroup> symbolGroups;
	std::unordered_map<symbol_t, symidx_t> symbolSubIdx;
};

class RansState
{
public:
	RansState();
	// rANS state - size of state = size of probability + size of output block
	RansState(uint32_t probabilityRes, SymbolCountDict counts);
	// fast constructor if rANS table is already generated
	RansState(uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable);
	// TODO serialize whole thing?
	RansState(std::shared_ptr<VectorStream<block_t>> compressedBlocks, state_t ransState, uint32_t probabilityRes, SymbolCountDict counts);
	// fast constructor if rANS table is already generated
	RansState(std::shared_ptr<VectorStream<block_t>> compressedBlocks, state_t ransState, uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable);

	// Encode symbol
	void AddSymbol(symbol_t symbol);
	// Decode symbol
	symbol_t ReadSymbol();

	// this isn't really const...
	const std::vector<block_t> GetCompressedBlocks();
	state_t GetRansState();
	bool HasData();

	// true if initialized properly
	bool IsValid();

private:
	// TODO used to work around bad math
	void AddGroup(RansGroup group);
	void AddSubIdx(symbol_t symbol, RansGroup group);
	count_t probabilityRange;
	state_t stateMin;
	state_t stateMax;
	state_t ransState;
	std::shared_ptr<VectorStream<block_t>> compressedBlocks;
	// so we can reuse one hunk of memory
	std::shared_ptr<RansTable> ransTable;
};