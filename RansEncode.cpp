#include <vector>
#include <unordered_map>
#include "Release_Assert.h"
#include <iostream>
#include <algorithm>

#include "RansEncode.h"

const uint32_t COMPRESSED_LOOKUP_BINS = 4096;
const uint32_t ROOT_BIN_SIZE = 3;
// Ported from my old Python implementation

// helper for deterministic entropy sorting
bool EntropyLess(const SymbolPDF& count1, const SymbolPDF& count2)
{
	if (count1.pdf != count2.pdf)
		return count1.pdf > count2.pdf;
	else
		return count1.symbol < count2.symbol;
}

std::vector<SymbolPDF> EntropySortSymbols(const SymbolCountDict &dict)
{
	// Convert to vector so we can sort by entropy, resulting in consistant ordering
	std::vector<SymbolPDF> countsVec;
	for (auto symbolCount : dict)
	{
		countsVec.emplace_back(symbolCount.first, symbolCount.second);
	}

	std::sort(countsVec.begin(), countsVec.end(), EntropyLess);

	return std::move(countsVec);
}

CDFTable::CDFTable(const SymbolCountDict& counts, uint32_t probabilityRes)
{
	std::vector<SymbolPDF> sortedPDFs = EntropySortSymbols(counts);
	symbols.reserve(sortedPDFs.size());
	uint32_t groupStart = 0;
	uint32_t currentCDF = 0;
	uint32_t groupPDF = sortedPDFs[0].pdf;

	// for all values before this, groupStart == groupNum
	pivot = -1;

	for (auto symbolPDF : sortedPDFs)
	{
		if (symbolPDF.pdf != groupPDF)
		{
			// push last group
			groupCDFs.push_back(currentCDF);
			if (pivot == -1 && symbols.size() - groupStart > 1)
				pivot = groupStart;
			if (pivot != -1)
				groupStarts.push_back(groupStart);
			// start new group
			groupStart = symbols.size();
			groupPDF = symbolPDF.pdf;
		}
		symbols.push_back(symbolPDF.symbol);
		currentCDF += symbolPDF.pdf;
	}
	// push last group
	groupCDFs.push_back(currentCDF);
	groupStarts.push_back(groupStart);

	std::cout << symbols.size() << " symbols" << std::endl;
	std::cout << groupCDFs.size() << " groups" << std::endl;
}

RansGroup CDFTable::GetSymbolGroup(uint32_t symbolCDF)
{
	// find matching group
	const uint32_t* __restrict groupCDFp = &groupCDFs.front();
	const uint32_t* groupCDFEnd = &groupCDFs.back();
	for (; groupCDFp <= groupCDFEnd; ++groupCDFp)
	{
		if (*groupCDFp > symbolCDF)
			break;
	}

	// calculate PDF
	uint32_t groupStartCDF = 0;
	if (groupCDFp != &groupCDFs.front())
		groupStartCDF = groupCDFp[-1];
	const uint32_t groupPDF = (*groupCDFp) - groupStartCDF;

	// get index from pointers
	uint16_t groupIdx = groupCDFp - &groupCDFs.front();

	// fast path for values before the pivot
	if (groupIdx < pivot)
		return RansGroup(0, symbols[groupIdx], groupPDF, groupStartCDF);

	// calculate num. symbols
	uint32_t nextGroupStart = symbols.size();
	// convert to group start index
	groupIdx -= pivot;
	if (groupCDFp != groupCDFEnd)
		nextGroupStart = groupStarts[groupIdx + 1ul];
	const uint32_t groupCount = nextGroupStart - groupStarts[groupIdx];

	return RansGroup(groupStarts[groupIdx], groupCount, groupPDF, groupStartCDF);
}

uint16_t CDFTable::GetSymbol(RansGroup group, uint32_t symbolIndex)
{
	//if (group.start + symbolIndex >= symbols.size())
	//	std::cout << "BAD INDEX " << group.start + symbolIndex << " " << symbols.size() << std::endl;
	return symbols[group.start + symbolIndex];
}

uint16_t CDFTable::GetSymbolIdxInGroup(RansGroup group, uint16_t symbol)
{
	assert(group.start != 0);
	for (int i = 0; i < group.count; ++i)
		if (symbols[group.start + i] == symbol)
			return i;
	std::cout << "error finding symbol " << symbol << std::endl;
	return -1;
}

RansTable::RansTable(SymbolCountDict PDFs, uint32_t probabilityRes)
{
	// TODO symbols get sorted twice (1st time in cdfTable constructor)
	std::vector<SymbolPDF> sortedPDFs = EntropySortSymbols(PDFs);
	cdfTable = CDFTable(PDFs, probabilityRes);

	// for encoding
	uint32_t currCDF = 0;
	for (auto symbolPDF : sortedPDFs)
	{
		RansEntry ransEntry = RansEntry(symbolPDF.symbol, currCDF);
		symbolTable.emplace(symbolPDF.symbol, ransEntry);
		currCDF += symbolPDF.pdf;
	}
	// debug code
	/*
	for (auto symbolPDF : symbolTable)
	{
		RansGroup group = cdfTable.GetSymbolGroup(symbolPDF.second.cdf);
		// fast path
		uint16_t entry = group.count;
		// slow path
		if (group.start != 0)
		{
			uint32_t idx = cdfTable.GetSymbolIdxInGroup(group, symbolPDF.first);
			entry = cdfTable.GetSymbol(group, idx);
		}
		if (entry != symbolPDF.second.symbol)
		{

			std::cout << "cdfTable test fail " << symbolPDF.first << " " << symbolPDF.second.cdf << " " << std::endl;
			assert(false);
			break;
		}
	}
	*/
}

RansEntry RansTable::GetSymbolEntry(uint16_t symbol)
{
	return symbolTable[symbol];
}

uint16_t RansTable::GetSymbolIdxInGroup(const RansGroup group, const uint16_t symbol)
{
	return cdfTable.GetSymbolIdxInGroup(group, symbol);
}

// Cumulative probability to rANS group
RansGroup RansTable::GetSymbolGroupFromFreq(const uint32_t prob)
{
	return cdfTable.GetSymbolGroup(prob);
}

uint16_t RansTable::GetSymbolEntryFromGroup(const RansGroup group, const uint16_t subIndex)
{
	return cdfTable.GetSymbol(group, subIndex);
}

size_t RansTable::GetMemoryFootprint() const
{
	// this isn't that accurate
	size_t mapSize = std::max(symbolTable.bucket_count(), symbolTable.size()) * sizeof(std::unordered_map< uint16_t, RansEntry>::value_type);
	// TODO UPDATE
	size_t vectorSize = 0;// cdfTable.capacity() * sizeof(RansEntry);

	return sizeof(RansTable) + mapSize + vectorSize;
}

SymbolCountDict GenerateQuantizedPDFs(SymbolCountDict unquantizedCounts, size_t probabilityRange)
{
	uint64_t countsSum = 0;
	for (const auto symbolCount : unquantizedCounts)
	{
		countsSum += symbolCount.second;
	}

	// already quantized
	if (countsSum == probabilityRange)
		return unquantizedCounts;

	// TODO move elsewhere?
	std::vector<SymbolPDF> sortedSymbolCounts = EntropySortSymbols(unquantizedCounts);
	//std::cout << "Counting symbols..." << std::endl;
	// Step 1: convert to quantized probabilities

	//std::cout << "Generating quantized symbols..." << std::endl;
	uint64_t quantizedPDFsSum = 0;
	// TODO this will probably fail if quantized probabilites overflow 32-bits
	SymbolCountDict quantizedPDFs;
	for (auto symbolCount : sortedSymbolCounts)
	{
		// TODO check for integer overflow
		uint64_t newCount = (symbolCount.pdf * probabilityRange) / countsSum;
		newCount = std::max(1ull, newCount);
		quantizedPDFs.emplace(symbolCount.symbol, newCount);
		quantizedPDFsSum += newCount;
	}

	if (quantizedPDFsSum > probabilityRange)
		std::cout << "Fixing probability underflow..." << std::endl;
	// SUPER ineffient way of dealing with overflowing probabilities
	while (quantizedPDFsSum > probabilityRange)
	{
		double smallestError = probabilityRange;
		uint32_t smallestErrorSymbol = -1;
		for (auto quantizedPDF : quantizedPDFs)
		{
			const uint16_t symbol = quantizedPDF.first;
			const uint32_t quantizedSymbolPDF = quantizedPDF.second;
			const uint32_t symbolCount = unquantizedCounts.at(quantizedPDF.first);
			// can't reduce a probability to zero
			if (quantizedPDF.second == 1)
				continue;
			// Entropy cost of reducing probability
			// (TODO This can probably be simplified to something cheap, but D-Day is in 2 days...)
			double oldProbability = quantizedPDF.second;
			oldProbability /= quantizedPDFsSum;
			double oldEntropy = symbolCount * -log2(oldProbability);
			double newProbability = quantizedPDF.second - 1;
			// TODO Should we have a -1 here? Seems sketch...
			newProbability /= quantizedPDFsSum;
			double newEntropy = symbolCount * -log2(newProbability);
			// newEntropy will be larger
			double entropyError = newEntropy - oldEntropy;
			if (entropyError < smallestError)
			{
				smallestError = entropyError;
				smallestErrorSymbol = symbol;
			}
		}
		// we now have the symbol that gives the smallest-entropy error
		// when subtracting one from it's quantized count/probability
		// TODO ERROR
		assert(smallestErrorSymbol != -1);

		// subtract one from probability
		quantizedPDFs[smallestErrorSymbol] -= 1;
		quantizedPDFsSum -= 1;
	}

	if (quantizedPDFsSum < probabilityRange)
		std::cout << "Fixing probability overflow..." << std::endl;
	// SUPER ineffient way of dealing with overflowing probabilities
	while (quantizedPDFsSum < probabilityRange)
	{
		double biggestGain = 0;
		uint32_t biggestGainSymbol = -1;
		for (auto quantizedPDF : quantizedPDFs)
		{
			// TODO this is a bad metric...
			if (quantizedPDF.second > biggestGain)
			{
				biggestGain = quantizedPDF.second;
				biggestGainSymbol = quantizedPDF.first;
			}
		}
		assert(biggestGainSymbol != -1);

		// Add one to probability
		quantizedPDFs[biggestGainSymbol] += 1;
		quantizedPDFsSum += 1;
	}

	return quantizedPDFs;
}

RansState::RansState()
	: probabilityRange(0), blockSize(0), stateMax(0), stateMin(0), ransState(0)
{

}

// rANS state - size of state = size of probability + size of output block
RansState::RansState(uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize)
	: ransTable()
{
	compressedBlocks = std::shared_ptr<VectorStream<uint8_t>>(new VectorVectorStream<uint8_t>());

	//std::cout << "STARTING" << std::endl;
	// Initialize rANS encoding parameters
	probabilityRange = 1 << probabilityRes;
	blockSize = 1 << outputBlockSize;
	stateMin = probabilityRange;
	stateMax = (stateMin * blockSize) - 1;
	
	SymbolCountDict quantizedPDFs = GenerateQuantizedPDFs(counts, probabilityRange);

	//std::cout << "generating rANS table..." << std::endl;
	// Our quantized probabilites now sum to exactly probabilityRange,
	// which is require for rANS to work
	ransTable = std::make_shared<RansTable>(quantizedPDFs, probabilityRes);
	// You can technically set initial rANS state to anything, but I choose the min. val
	ransState = stateMin;
}

// fast constructor
RansState::RansState(uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize)
{
	compressedBlocks = std::shared_ptr<VectorStream<uint8_t>>(new VectorVectorStream<uint8_t>());

	// Initialize rANS encoding parameters
	probabilityRange = 1 << probabilityRes;
	blockSize = 1 << outputBlockSize;
	stateMin = probabilityRange;
	stateMax = (stateMin * blockSize) - 1;

	ransTable = symbolTable;
	// You can technically set initial rANS state to anything, but I choose the min. val
	ransState = stateMin;
}

// initialize for decoding
RansState::RansState(std::shared_ptr<VectorStream<uint8_t>> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize)
	: RansState(probabilityRes, counts, outputBlockSize)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}

RansState::RansState(std::shared_ptr<VectorStream<uint8_t>>  compressedBlocks, uint64_t ransState, uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize)
	: RansState(probabilityRes, symbolTable, outputBlockSize)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}

// Encode symbol
void RansState::AddSymbol(uint16_t symbol)
{
	// Write sub-index
	RansEntry entry = ransTable->GetSymbolEntry(symbol);

	RansGroup group = ransTable->GetSymbolGroupFromFreq(entry.cdf);
	if (group.cdf> entry.cdf || entry.cdf > group.cdf + group.pdf)
	{
		std::cout << group.cdf << " " << group.pdf << " " << entry.cdf << std::endl;
		std::cout << "Bad group range" << std::endl;
		return;
	}

	if (group.start != 0 && group.count > 1)
	{
		// find symbol idx in group
		uint16_t symbolIdx = ransTable->GetSymbolIdxInGroup(group, symbol);

		// double-check
		if (ransTable->GetSymbolEntryFromGroup(group, symbolIdx) != symbol)
		{
			std::cout << group.cdf << " " << group.pdf << " " << entry.cdf << std::endl;
			std::cout << "Bad symbol encode " << symbol << " " << symbolIdx << std::endl;
			return;
		}

		// add symbol to rANS state
		uint64_t newState = ransState * group.count;
		newState += symbolIdx;

		// renormalize if necessary
		while (newState > stateMax)
		{
			compressedBlocks->push_back(ransState % blockSize);
			ransState /= blockSize;
			newState = ransState * group.count;
			newState += symbolIdx;
		}

		ransState = newState;
	}

	// write group (also handles fast path)
	// add symbol to rANS state
	uint64_t newState = ransState / group.pdf;
	newState *= probabilityRange;
	newState += group.cdf;
	newState += ransState % group.pdf;

	// renormalize if necessary
	while (newState > stateMax)
	{
		compressedBlocks->push_back(ransState % blockSize);
		ransState /= blockSize;
		newState = ransState / group.pdf;
		newState *= probabilityRange;
		newState += group.cdf;
		newState += ransState % group.pdf;
	}

	ransState = newState;
}

// Decode symbol
uint16_t RansState::ReadSymbol()
{
	// feed data into state as needed
	while (ransState < stateMin)
	{
		ransState *= blockSize;
		ransState += compressedBlocks->back();
		compressedBlocks->pop_back();
	}

	// read group
	// TODO this can be a bitwise AND
	uint64_t cumulativeProb = ransState % probabilityRange;
	RansGroup group = ransTable->GetSymbolGroupFromFreq(cumulativeProb);
	// TODO can use bit shift
	uint64_t newState = ransState / probabilityRange;
	newState = newState * group.pdf;
	// TODO can use bitwise AND
	newState += ransState % probabilityRange;
	newState -= group.cdf;

	ransState = newState;

	// fast path for values before the pivot
	if (group.start == 0)
		// symbol is smuggled in count to skip a layer of indirection
		return group.count;

	// if there's only 1 item in the group, no need to read
	if (group.count == 1)
		return ransTable->GetSymbolEntryFromGroup(group, 0);

	// multiple symbols in group - need to get sub-index
	// feed data into state as needed
	while (ransState < stateMin)
	{
		ransState *= blockSize;
		ransState += compressedBlocks->back();
		compressedBlocks->pop_back();
	}

	// read sub-index
	// get sub index (all symbols have PDF=1/group.count)
	cumulativeProb = ransState % group.count;
	uint16_t symbol = ransTable->GetSymbolEntryFromGroup(group, cumulativeProb);
	newState = ransState / group.count;
	// PDF is 1
	//newState = newState * entry.pdf;
	newState += ransState % group.count;
	newState -= cumulativeProb;

	ransState = newState;

	return symbol;
}

const std::vector<uint8_t> RansState::GetCompressedBlocks()
{
	return compressedBlocks->get_vec();
}


uint64_t RansState::GetRansState()
{
	return ransState;
}


bool RansState::HasData()
{
#ifdef DEBUG
	// this is now slower
	if (compressedBlocks->size() > 0)
		return true;
#endif
	if (ransState != stateMin)
		return true;
	return false;
}


bool RansState::IsValid()
{
	bool valid = true;

	// check min/max are different
	valid = valid && stateMin < stateMax;

	// check state is between min/max values
	valid = valid && (stateMin <= ransState || compressedBlocks->size() > 0) && stateMax >= ransState;

	// check rANS state is large enough
	valid = valid && std::numeric_limits<uint64_t>::max() / probabilityRange > blockSize;

	// TODO table checks?

	return valid;
}