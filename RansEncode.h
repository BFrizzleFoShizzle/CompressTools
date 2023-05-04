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

// GetSymbolGroup doesn't get inlined, this hack at least makes it return in a register
// group_packed_t & 0xFFFF = pdf
// (group_packed_t >> 16) & 0xFFFF = cdf
// (group_packed_t >> 32) & 0xFFFF = start
// (group_packed_t >> 48) & 0xFFFF = count
// ^ the above matches read order
typedef uint64_t group_packed_t;
static_assert(sizeof(group_packed_t) <= (sizeof(symidx_t) + sizeof(prob_t)) * 2, "rANS group doesn't fit in group_packed_t");
// TODO fix this
static_assert((sizeof(symidx_t) + sizeof(prob_t)) * 2 == 8, "group_packed_t needs updating");

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
	RansGroup(group_packed_t group)
		: count((group >> 48) & 0xFFFF), start((group >> 32) & 0xFFFF), cdf((group >> 16) & 0xFFFF), pdf(group & 0xFFFF)
	{

	}
	group_packed_t Pack() const
	{
		return pdf | (((uint32_t)cdf) << 16) | (((uint64_t)start) << 32) | (((uint64_t)count) << 48);
	}
	symidx_t start;
	symidx_t count;
	prob_t pdf;
	prob_t cdf;
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

	inline symbol_t GetSymbol(group_packed_t group, symidx_t symbolIndex);
	symidx_t GetSymbolSubIdx(RansGroup group, symbol_t symbol);
	inline group_packed_t GetSymbolGroup(const prob_t symbolCDF);
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
	// this doesn't get inlined - we return an int so it at least returns in a register
	inline group_packed_t GetSymbolGroupFromFreq(const prob_t prob);
	inline symbol_t GetSymbolFromGroup(const group_packed_t group, const symidx_t subIndex);

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