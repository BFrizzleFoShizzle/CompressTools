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

CDFTable::CDFTable(SymbolCountDict counts, uint32_t probabilityRes)
{
	uint32_t cumulativeCount = 0;
	std::vector<SymbolCount> sortedCounts = EntropySortSymbols(counts);
	symbols.reserve(sortedCounts.size());
	CDFVals.reserve(sortedCounts.size());
	symbolCounts.reserve(sortedCounts.size());
	for (auto symbolCount : sortedCounts)
	{
		symbols.push_back(symbolCount.symbol);
		CDFVals.push_back(cumulativeCount);
		symbolCounts.push_back(symbolCount.count);
		cumulativeCount += symbolCount.count;
	}
}

RansEntry CDFTable::GetSymbol(uint32_t symbolCDF)
{
	uint32_t symbolIdx = 0;
	for (; symbolIdx <CDFVals.size() - 1; ++symbolIdx)
	{
		if (/*CDFVals[symbolIdx] <= symbolCDF && */CDFVals[symbolIdx + 1] > symbolCDF)
			break;
	}
	RansEntry ransEntry = RansEntry(symbols[symbolIdx], symbolCounts[symbolIdx], CDFVals[symbolIdx]);
	//if (!(ransEntry.cumulativeCount <= symbolCDF && symbolCDF < ransEntry.cumulativeCount + ransEntry.count))
	//	std::cout << "CDF lookup doesn't match CDF!" << std::endl;
	return ransEntry;
}

RansTable::RansTable(SymbolCountDict counts, uint32_t probabilityRes)
	: cdfTable(counts, probabilityRes)
{
	// TODO symbols get sorted twice (1st time in cdfTable constructor)
	std::vector<SymbolCount> sortedCounts = EntropySortSymbols(counts);

	// for encoding
	uint32_t cumulativeCount = 0;
	for (auto symbolCount : sortedCounts)
	{
		RansEntry ransEntry = RansEntry(symbolCount.symbol, symbolCount.count, cumulativeCount);
		symbolTable.emplace(symbolCount.symbol, ransEntry);
		cumulativeCount += symbolCount.count;
	}

	/*
	uint32_t cumulativeCount = 0;
	std::vector<SymbolCount> sortedCounts = EntropySortSymbols(counts);
	// here's the thought: at some point the average number of items in a bucket becomes greater than the number
	// of rANS entries from the start of the probability table
	// That point is the "pivot" where binning starts increasing performance
	// So, we treat all symbols with CDF below pivot as being in same bin starting at zero
	// For all other blocks (with CDF >= pivot), bin is found using lookup table
	binLookupTable.reserve(COMPRESSED_LOOKUP_BINS + 1);
	size_t currBinMaxCDF = 0;
	binningPivot = -1;
	for (auto symbolCount : sortedCounts)
	{
		// bootleg - first N items are in "block zero"
		if (symbolTable.size() == ROOT_BIN_SIZE)
		{
			float cdf = ((float)cumulativeCount) / (1 << probabilityRes);
			binningPivot = cumulativeCount;
			uint32_t remainingCDF = (1 << probabilityRes) - cumulativeCount;
			cdfBinDivisor = remainingCDF / COMPRESSED_LOOKUP_BINS;
			std::cout << cdf << std::endl;
		}
		/*
		if (binPivot == -1)
		{
			// num. symbols before current in root block
			uint32_t currBlockItems = cdfTable.size();
			uint32_t remainingItems = sortedCounts.size() - currBlockItems;
			uint32_t avgSymbolsPerBlock = remainingItems / COMPRESSED_LOOKUP_BINS;
			// if the root block has as many symbols as the rest of the file will, on average, emit root block + update pivot
			// curr symbol is now in binLookupTable[0]
			if (currBlockItems > avgSymbolsPerBlock)
			{
				float cdf = ((float)cumulativeCount) / (1 << probabilityRes);
				float pdf = ((float)symbolCount.count) / (1 << probabilityRes);
				// 
				float bpdf = ((float)symbolCount.count) / ((1 << probabilityRes) - cumulativeCount);
				std::cout << currBlockItems << " " << cdf << " " << pdf << " " << bpdf << std::endl;
				binPivot = cumulativeCount;
				uint32_t remainingCDF = (1 << probabilityRes) - cumulativeCount;
				cdfBinDivisor = remainingCDF / COMPRESSED_LOOKUP_BINS;
			}
			
		}
		*/
		/*
		if (binningPivot != -1)
		{
			// if the total CDF is outside the range of the current bin, need a new bin pointed at new symbol
			// e.g. size 8 CDF at 0 goes from 0-7, hence the -1
			uint32_t scaledMaxCDF = ((cumulativeCount + symbolCount.count) - binningPivot) - 1;
			while (currBinMaxCDF <= scaledMaxCDF)
			{
				binLookupTable.push_back(symbolTable.size());
				currBinMaxCDF += cdfBinDivisor;
				if (binLookupTable.size() < 10)
					std::cout << symbolTable.size() << std::endl;
				if ((COMPRESSED_LOOKUP_BINS - binLookupTable.size()) < 10)
					std::cout << symbolTable.size() << std::endl;
			}
		}
		
		RansEntry ransEntry = RansEntry(symbolCount.symbol, symbolCount.count, cumulativeCount);
		symbolTable.emplace(symbolCount.symbol, ransEntry);
		cdfTable.push_back(ransEntry);
		cumulativeCount += symbolCount.count;
	}
	if (binLookupTable.size() != COMPRESSED_LOOKUP_BINS)
		std::cerr << "WRONG NUMBER OF rANS LOOKUP TABLE BINS!" << std::endl;
		*/
}

RansEntry RansTable::GetSymbolEntry(uint16_t symbol)
{
	return symbolTable[symbol];
}

// Cumulative probability to rANS entry
RansEntry RansTable::GetSymbolEntryFromFreq(uint32_t prob)
{
	return cdfTable.GetSymbol(prob);
	/*
	auto entry = cdfTable.begin();
	if (prob >= binningPivot)
	{
		const uint32_t lookupBin = (prob - binningPivot) / cdfBinDivisor;
		if (lookupBin >= binLookupTable.size())
			std::cout << "OUT-OF-BOUNDS CDF LOOKUP1! " << lookupBin << std::endl;
		uint32_t startPos = binLookupTable[lookupBin];

		if (startPos >= cdfTable.size())
			std::cout << "OUT-OF-BOUNDS CDF LOOKUP2!" << std::endl;

		entry += startPos;

		if (entry->cumulativeCount > prob)
			std::cout << "CDF ENTRY SKIPPED!" << std::endl;
	}

	while (entry != cdfTable.end())
	{
		if (entry->cumulativeCount <= prob && prob < entry->cumulativeCount + entry->count)
			return *entry;
		++entry;
	}
	*/
	// TODO ERROR
	assert(false);
}


size_t RansTable::GetMemoryFootprint() const
{
	// this isn't that accurate
	size_t mapSize = std::max(symbolTable.bucket_count(), symbolTable.size()) * sizeof(std::unordered_map< uint16_t, RansEntry>::value_type);
	// TODO UPDATE
	size_t vectorSize = 0;// cdfTable.capacity() * sizeof(RansEntry);

	return sizeof(RansTable) + mapSize + vectorSize;
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
	compressedBlocks = std::shared_ptr<VectorStream<uint8_t>>(new VectorVectorStream<uint8_t>());

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
	ransTable = std::make_shared<RansTable>(quantizedCounts, probabilityRes);
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
	RansEntry entry = ransTable->GetSymbolEntry(symbol);
	if (entry.count == 0 || entry.count >= probabilityRange)
	{
		std::cout << "Bad rANS value" << std::endl;
		return;
	}


	// renormalize if necessary
	int count = 0;
	while (ransState >= blockSize * (stateMin / probabilityRange) * entry.count)
	{
		compressedBlocks->push_back(ransState % blockSize);
		ransState /= blockSize;
		++count;
		if (count > 100)
		{
			std::cout << "RANS STUCK IN LOOP?" << std::endl;
			continue;
		}
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
	while (/*compressedBlocks->size() > 0 && */ newState < stateMin)
	{
		newState *= blockSize;
		newState += compressedBlocks->back();
		compressedBlocks->pop_back();
	}

	if (newState < stateMin)
		std::cerr << "rANS underflow!" << std::endl;

	// Done!
	ransState = newState;
	return entry.symbol;
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
	valid = valid && stateMin <= ransState && stateMax >= ransState;

	// check rANS state is large enough
	valid = valid && std::numeric_limits<uint64_t>::max() / probabilityRange > blockSize;

	// TODO table checks?

	return valid;
}