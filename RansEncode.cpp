#include <vector>
#include <unordered_map>
#include <assert.h>
#include <iostream>
#include <algorithm>

#include "RansEncode.h"

// Ported from my old Python implementation

// helper for deterministic entropy sorting
bool EntropyLess(const SymbolCount& count1, const SymbolCount& count2)
{
	if (count1.count != count2.count)
		return count1.count > count2.count;
	else
		return count1.symbol < count2.symbol;
}

std::vector<SymbolCount> EntropySortSymbols(SymbolCountDict dict)
{
	// Convert to vector so we can sort by entropy, resulting in consistant ordering
	std::vector<SymbolCount> countsVec;
	for (auto symbolCount : dict)
	{
		countsVec.emplace_back(symbolCount.first, symbolCount.second);
	}

	std::sort(countsVec.begin(), countsVec.end(), EntropyLess);

	std::cout << countsVec[0].count << " " << countsVec.back().count << std::endl;

	return std::move(countsVec);
}

RansTable::RansTable(SymbolCountDict counts)
{
	uint32_t cumulativeCount = 0;
	std::vector<SymbolCount> sortedCounts = EntropySortSymbols(counts);
	for (auto symbolCount : sortedCounts)
	{
		RansEntry ransEntry = RansEntry(symbolCount.symbol, symbolCount.count, cumulativeCount);
		symbolTable.emplace(symbolCount.symbol, ransEntry);
		cdfTable.push_back(ransEntry);
		cumulativeCount += symbolCount.count;
	}
}

RansEntry RansTable::GetSymbolEntry(uint16_t symbol)
{
	return symbolTable[symbol];
}

// Cumulative probability to rANS entry
RansEntry RansTable::GetSymbolEntryFromFreq(uint32_t prob)
{
	for (auto entry : cdfTable)
	{
		if (entry.cumulativeCount <= prob && prob < entry.cumulativeCount + entry.count)
			return entry;
	}
	// TODO ERROR
	assert(false);
}

SymbolCountDict GenerateQuantizedCounts(SymbolCountDict unquantizedCounts, size_t probabilityRange)
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
	std::vector<SymbolCount> sortedSymbolCounts = EntropySortSymbols(unquantizedCounts);
	//std::cout << "Counting symbols..." << std::endl;
	// Step 1: convert to quantized probabilities

	//std::cout << "Generating quantized symbols..." << std::endl;
	uint64_t quantizedCountsSum = 0;
	// TODO this will probably fail if quantized probabilites overflow 32-bits
	SymbolCountDict quantizedCounts;
	for (auto symbolCount : sortedSymbolCounts)
	{
		// TODO check for integer overflow
		uint64_t newCount = (symbolCount.count * probabilityRange) / countsSum;
		newCount = std::max(1ull, newCount);
		quantizedCounts.emplace(symbolCount.symbol, newCount);
		quantizedCountsSum += newCount;
	}

	if (quantizedCountsSum > probabilityRange)
		std::cout << "Fixing probability underflow..." << std::endl;
	// SUPER ineffient way of dealing with overflowing probabilities
	while (quantizedCountsSum > probabilityRange)
	{
		double smallestError = probabilityRange;
		uint32_t smallestErrorSymbol = -1;
		for (auto quantizedCount : quantizedCounts)
		{
			const uint16_t symbol = quantizedCount.first;
			// TODO confusing naming
			const uint32_t quantizedSymbolCount = quantizedCount.second;
			const uint32_t symbolCount = unquantizedCounts.at(quantizedCount.first);
			// can't reduce a probability to zero
			if (quantizedCount.second == 1)
				continue;
			// Entropy cost of reducing probability
			// (TODO This can probably be simplified to something cheap, but D-Day is in 2 days...)
			double oldProbability = quantizedCount.second;
			oldProbability /= quantizedCountsSum;
			double oldEntropy = symbolCount * -log2(oldProbability);
			double newProbability = quantizedCount.second - 1;
			// TODO Should we have a -1 here? Seems sketch...
			newProbability /= quantizedCountsSum;
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
		quantizedCounts[smallestErrorSymbol] -= 1;
		quantizedCountsSum -= 1;
	}

	if (quantizedCountsSum < probabilityRange)
		std::cout << "Fixing probability overflow..." << std::endl;
	// SUPER ineffient way of dealing with overflowing probabilities
	while (quantizedCountsSum < probabilityRange)
	{
		double biggestGain = 0;
		uint32_t biggestGainSymbol = -1;
		for (auto quantizedCount : quantizedCounts)
		{
			// TODO this is a bad metric...
			if (quantizedCount.second > biggestGain)
			{
				biggestGain = quantizedCount.second;
				biggestGainSymbol = quantizedCount.first;
			}
		}
		assert(biggestGainSymbol != -1);

		// Add one to probability
		quantizedCounts[biggestGainSymbol] += 1;
		quantizedCountsSum += 1;
	}

	return quantizedCounts;
}

RansState::RansState()
	: probabilityRange(0), blockSize(0), stateMax(0), stateMin(0), ransState(0)
{

}

// rANS state - size of state = size of probability + size of output block
RansState::RansState(uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize)
	: ransTable()
{
	//std::cout << "STARTING" << std::endl;
	// Initialize rANS encoding parameters
	probabilityRange = 1 << probabilityRes;
	blockSize = 1 << outputBlockSize;
	stateMin = probabilityRange;
	stateMax = (stateMin * blockSize) - 1;
	
	SymbolCountDict quantizedCounts = GenerateQuantizedCounts(counts, probabilityRange);

	//std::cout << "generating rANS table..." << std::endl;
	// Our quantized probabilites now sum to exactly probabilityRange,
	// which is require for rANS to work
	ransTable = std::make_shared<RansTable>(quantizedCounts);
	// You can technically set initial rANS state to anything, but I choose the min. val
	ransState = stateMin;
}

// fast constructor
RansState::RansState(uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize)
{
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
RansState::RansState(std::vector<uint8_t> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, SymbolCountDict counts, uint32_t outputBlockSize)
	: RansState(probabilityRes, counts, outputBlockSize)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}

RansState::RansState(std::vector<uint8_t> compressedBlocks, uint64_t ransState, uint32_t probabilityRes, std::shared_ptr<RansTable> symbolTable, uint32_t outputBlockSize)
	: RansState(probabilityRes, symbolTable, outputBlockSize)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}

// Encode symbol
void RansState::AddSymbol(uint16_t symbol)
{
	RansEntry entry = ransTable->GetSymbolEntry(symbol);

	// renormalize if necessary
	while (ransState >= blockSize * (stateMin / probabilityRange) * entry.count)
	{
		compressedBlocks.push_back(ransState % blockSize);
		ransState /= blockSize;
	}

	// add symbol to rANS state
	uint64_t newState = ransState / entry.count;
	newState *= probabilityRange;
	newState += entry.cumulativeCount;
	newState += ransState % entry.count;

	ransState = newState;
}

// Decode symbol
uint16_t RansState::ReadSymbol()
{
	// TODO consistant naming
	// TODO this can be a bitwise AND
	uint64_t cumulativeProb = ransState % probabilityRange;
	RansEntry entry = ransTable->GetSymbolEntryFromFreq(cumulativeProb);
	// TODO can use bit shift
	uint64_t newState = ransState / probabilityRange;
	newState = newState * entry.count;
	// TODO can use bitwise AND
	newState += ransState % probabilityRange;
	newState -= entry.cumulativeCount;

	// feed data into state as needed
	while (compressedBlocks.size() > 0 && newState < stateMin)
	{
		newState *= blockSize;
		newState += compressedBlocks.back();
		compressedBlocks.pop_back();
	}

	if (newState < stateMin)
		std::cerr << "rANS underflow!" << std::endl;

	// Done!
	ransState = newState;
	return entry.symbol;
}

const std::vector<uint8_t> RansState::GetCompressedBlocks()
{
	return compressedBlocks;
}


uint64_t RansState::GetRansState()
{
	return ransState;
}


bool RansState::HasData()
{
	if (compressedBlocks.size() > 0)
		return true;
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
	valid = valid && stateMin <= ransState && stateMax >= ransState;

	// check rANS state is large enough
	valid = valid && std::numeric_limits<uint64_t>::max() / probabilityRange > blockSize;

	// TODO table checks?

	return valid;
}