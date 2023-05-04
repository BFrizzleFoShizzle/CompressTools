
#include "Release_Assert.h"
#include <iostream>
#include <algorithm>

#include "RansEncode.h"

// Ported from my old Python implementation

constexpr uint16_t PIVOT_INVALID = 0xFFFF;

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

// TODO clean up this massive, hulking, leviathan of a function
CDFTable::CDFTable(const SymbolCountDict& unquantizedCounts, uint16_t probabilityRes,
	std::unordered_map<symbol_t, RansGroup>& symbolGroupsOut, std::unordered_map<symbol_t, symidx_t>& symbolSubIdxOut)
{
	assert(RansGroup(1, 2, 3, 4).Pack() == RansGroup(RansGroup(1, 2, 3, 4).Pack()).Pack());
	assert(RansGroup(1, 2, 3, 4).Pack() == 0x0002000100040003ull);
	size_t probabilityRange = 1 << probabilityRes;

	uint64_t countsSum = 0;
	for (const auto symbolCount : unquantizedCounts)
	{
		countsSum += symbolCount.second;
	}

	// already quantized
	if (countsSum == probabilityRange || countsSum == 65535)
	{
		// TODO
		assert(false);
	}

	std::vector<SymbolPDF> sortedSymbolCounts = EntropySortSymbols(unquantizedCounts);

	// Step 1: group equal counts
	std::vector<std::vector<SymbolPDF>> initialGroups;
	//SymbolCountDict initialGroupCounts;
	std::vector<SymbolPDF> initialGroupCounts;
	std::vector<symidx_t> initialStarts;

	symidx_t groupStart = 0;
	// confusingly, this is the "count" shared by symbols in the current group, not the count of the group itself
	count_t groupCount = sortedSymbolCounts[0].pdf;
	// this is the sum of counts of all symbols in the group
	count_t currentCount = 0;
	symidx_t symbolIndex = 0;
	initialGroups.emplace_back();
	initialStarts.push_back(0);

	for (auto symbolCount : sortedSymbolCounts)
	{
		if (symbolCount.pdf != groupCount)
		{
			// push last group
			initialGroupCounts.emplace_back(initialGroups.size() - 1, currentCount);
			initialGroups.emplace_back();
			// start new group
			groupStart = symbolIndex;
			initialStarts.push_back(groupStart);
			groupCount = symbolCount.pdf;
			currentCount = 0;
		}
		++symbolIndex;
		initialGroups.back().push_back(symbolCount);
		currentCount += symbolCount.pdf;
	}
	// push end group
	initialGroupCounts.emplace_back(initialGroups.size() - 1, currentCount);

	double initialEntropy = 0;
	for (auto groupCount : initialGroupCounts)
	{
		double groupProbability = groupCount.pdf;
		groupProbability /= countsSum;
		// entropy of encoding group index
		double groupEncodeEntropy = groupCount.pdf * -log2(groupProbability);
		// probability encoding sub-index
		double groupSymbolProbability = 1;
		groupSymbolProbability /= initialGroups[groupCount.symbol].size();
		// entropy of encoding sub-index
		double groupSymbolEntropy = groupCount.pdf * -log2(groupSymbolProbability);
		initialEntropy += groupEncodeEntropy;
		initialEntropy += groupSymbolEntropy;
	}

	// Step 2: separate out lowest-count symbols for special coding
	double rawValuesEntropy = 0.0;
	double rawValuesCrossEntropy = 0.0;
	size_t rawEntryCount = 0;
	size_t rawSymbolCount = 0;
	std::vector<SymbolPDF> culledGroups;
	// this currently matches the code...
	// TODO if we write the raw to rANS state, we can reduce this
	assert(sizeof(block_t) >= 2);
	size_t sizeOfRawInBits = 8 * sizeof(block_t);
	for (int i = initialGroupCounts.size() - 1; i > 1; --i)
	{
		uint32_t groupIdx = initialGroupCounts[i].symbol;
		double groupEntropy = initialGroupCounts[i].pdf * -log2(double(initialGroupCounts[i].pdf) / countsSum);
		// new entropy is the cost of encoding with no compression + the cost of selecting the "no compression" group
		double groupNewEntropy = initialGroupCounts[i].pdf * sizeOfRawInBits;
		size_t newCount = rawSymbolCount + initialGroupCounts[i].pdf;
		// this is an estimation
		double newGroupSelectionEntropy = newCount * -log2(double(newCount) / countsSum);
		double groupCrossEntropy = groupNewEntropy - groupEntropy;
		double groupSelectionCheckEntropy = initialGroupCounts[i].pdf * -log2(double(newCount) / countsSum);
		assert(groupSelectionCheckEntropy + groupCrossEntropy > 0.0);
		uint32_t nextStart = sortedSymbolCounts.size() - 1;
		if (groupIdx < initialGroupCounts.size() - 1)
			nextStart = initialStarts[groupIdx + 1];
		// don't increase file size by more than 1%
		if ((newGroupSelectionEntropy + groupCrossEntropy) + rawValuesCrossEntropy > initialEntropy * 0.01)
			break;
		rawValuesCrossEntropy += groupCrossEntropy;
		rawValuesEntropy += groupEntropy;
		rawSymbolCount += initialGroupCounts[i].pdf;
		rawEntryCount += nextStart - initialStarts[groupIdx];
		culledGroups.push_back(initialGroupCounts[i]);
	}

	initialGroupCounts.resize(initialGroupCounts.size() - culledGroups.size());

	// TODO this isn't the "ideal" value, but it's close enough, and equal to the equation used in the preceding heuristic
	// this number isn't allowed to change
	prob_t noCompressPDF = (rawSymbolCount * probabilityRange) / countsSum;

	// this is needed for GetSymbolGroup optimizations to work
	assert(noCompressPDF > 0);

	for (auto culledGroup : culledGroups)
		for (auto symbol : initialGroups[culledGroup.symbol])
			// TODO full noCompressPDF
			symbolGroupsOut[symbol.symbol] = RansGroup(-1, -1, noCompressPDF - 1, probabilityRange - noCompressPDF);

	// calculate group's final entropy
	if(rawSymbolCount > 0)
		rawValuesEntropy += rawSymbolCount * -log2(double(noCompressPDF) / probabilityRange);

	// Step 3: quantize groups
	count_t quantizedGroupPDFsSum = 0;
	SymbolCountDict quantizedGroupPDFs;
	for (auto symbolCount : initialGroupCounts)
	{
		uint64_t newCount = (symbolCount.pdf * probabilityRange) / countsSum;
		assert(newCount <= probabilityRange);
		newCount = std::max(1ull, newCount);
		quantizedGroupPDFs.emplace(symbolCount.symbol, newCount);
		quantizedGroupPDFsSum += newCount;
	}

	quantizedGroupPDFsSum += noCompressPDF;

	// Step 4: fix probability overflow/underflow
	if (quantizedGroupPDFsSum > probabilityRange)
		std::cout << "Fixing probability underflow..." << std::endl;

	// SUPER ineffient way of dealing with overflowing probabilities
	// TODO https://cbloomrants.blogspot.com/2014/02/02-11-14-understanding-ans-10.html
	while (quantizedGroupPDFsSum > probabilityRange)
	{
		double smallestError = probabilityRange;
		group_t smallestErrorGroup = -1;
		for (auto quantizedPDF : quantizedGroupPDFs)
		{
			const group_t group = quantizedPDF.first;
			const count_t quantizedSymbolPDF = quantizedPDF.second;
			const count_t symbolCount = initialGroupCounts.at(group).pdf;
			// can't reduce a probability to zero
			if (quantizedPDF.second == 1)
				continue;
			// Entropy cost of reducing probability
			double oldProbability = quantizedPDF.second;
			oldProbability /= quantizedGroupPDFsSum;
			double oldEntropy = symbolCount * -log2(oldProbability);
			double newProbability = quantizedPDF.second - 1;
			newProbability /= quantizedGroupPDFsSum;
			double newEntropy = symbolCount * -log2(newProbability);
			// newEntropy will be larger
			double entropyError = newEntropy - oldEntropy;
			if (entropyError < smallestError)
			{
				smallestError = entropyError;
				smallestErrorGroup = group;
			}
		}
		// we now have the symbol that gives the smallest-entropy error
		// when subtracting one from it's quantized count/probability
		// TODO ERROR
		assert(smallestErrorGroup != -1);

		// subtract one from probability
		quantizedGroupPDFs[smallestErrorGroup] -= 1;
		quantizedGroupPDFsSum -= 1;
	}

	if (quantizedGroupPDFsSum < probabilityRange)
		std::cout << "Fixing probability overflow..." << std::endl;

	// SUPER ineffient way of dealing with overflowing probabilities
	while (quantizedGroupPDFsSum < probabilityRange)
	{
		double biggestGain = 0;
		group_t biggestGainGroup = -1;
		for (auto quantizedPDF : quantizedGroupPDFs)
		{
			// TODO this is a bad metric...
			if (quantizedPDF.second > biggestGain)
			{
				biggestGain = quantizedPDF.second;
				biggestGainGroup = quantizedPDF.first;
			}
		}
		assert(biggestGainGroup != -1);

		// Add one to probability
		quantizedGroupPDFs[biggestGainGroup] += 1;
		quantizedGroupPDFsSum += 1;
	}

	double quantizedEntropy = 0;
	for (auto groupCount : initialGroupCounts)
	{
		double groupProbability = quantizedGroupPDFs[groupCount.symbol];
		groupProbability /= probabilityRange;
		// entropy of encoding group index
		double groupEncodeEntropy = groupCount.pdf * -log2(groupProbability);
		// probability encoding sub-index
		double groupSymbolProbability = 1;
		groupSymbolProbability /= initialGroups[groupCount.symbol].size();
		// entropy of encoding sub-index
		double groupSymbolEntropy = groupCount.pdf * -log2(groupSymbolProbability);
		quantizedEntropy += groupEncodeEntropy;
		quantizedEntropy += groupSymbolEntropy;
	}
	quantizedEntropy += rawValuesEntropy;


	// Step 5: merge quantized groups (we don't need to re-sort)
	// TODO the ideal way would be to quantize while merging to better preserve probabilities of groups with low PDF after quantizing
	// sorted, merged, quantized, merged - this is more or less the highest-resolution most-compact representation at a given quantization level
	SymbolCountDict finalGroupPDFs;
	// number of unique symbols in the group
	std::vector<symidx_t> finalGroupEntryCounts;
	// unquantized count of symbols in stream for each quantized group
	std::vector<count_t> finalGroupSymbolCounts;
	std::vector<symidx_t> finalGroupStarts;

	// confusingly, this is the "count" shared by symbols in the current group, not the count of the group itself
	prob_t groupPDF = quantizedGroupPDFs[initialGroupCounts[0].symbol];
	// this is the sum of counts of all symbols in the group
	prob_t currentQuantizedPDF = 0;
	finalGroupEntryCounts.push_back(0);
	finalGroupSymbolCounts.push_back(0);
	finalGroupStarts.push_back(initialStarts[0]);

	// combine groups with equal quantized PDF
	for (auto symbolGroupCount : initialGroupCounts)
	{
		prob_t quantizedPDF = quantizedGroupPDFs[symbolGroupCount.symbol];
		count_t unquantizedCount = symbolGroupCount.pdf;
		if (quantizedPDF != groupPDF)
		{
			finalGroupPDFs.emplace(finalGroupEntryCounts.size() - 1, currentQuantizedPDF);
			finalGroupEntryCounts.push_back(0);
			finalGroupSymbolCounts.push_back(0);
			finalGroupStarts.push_back(initialStarts[symbolGroupCount.symbol]);
			// start new group
			groupPDF = quantizedPDF;
			currentQuantizedPDF = 0;
		}
		finalGroupEntryCounts.back() += initialGroups[symbolGroupCount.symbol].size();
		finalGroupSymbolCounts.back() += unquantizedCount;
		currentQuantizedPDF += quantizedPDF;
	}
	// push end group
	finalGroupPDFs.emplace(finalGroupSymbolCounts.size() - 1, currentQuantizedPDF);

	// Step 6: re-sort after merging
	// fast path comes first
	std::vector<SymbolPDF> finalSymbolGroupsPreSplit = EntropySortSymbols(finalGroupPDFs);
	std::vector<SymbolPDF> fastPath;
	std::vector<SymbolPDF> slowPath;
	for (SymbolPDF groupPDF : finalSymbolGroupsPreSplit)
	{
		if (finalGroupEntryCounts[groupPDF.symbol] == 1)
			fastPath.push_back(groupPDF);
		else
			slowPath.push_back(groupPDF);
	}
	// merge back into final ordered group list, all fast path come first
	std::vector<SymbolPDF> finalSymbolGroupPDFs;
	for (SymbolPDF groupPDF : fastPath)
		finalSymbolGroupPDFs.push_back(groupPDF);
	for (SymbolPDF groupPDF : slowPath)
		finalSymbolGroupPDFs.push_back(groupPDF);

	// Step 7: generate final internal state
	double finalEntropy = 0;
	count_t finalCount = 0;
	symidx_t finalSymbols = 0;
	// this will overflow if we use prob_t
	count_t finalCDF = 0;
	// rearrange symbols to follow the sorted qantized group order
	pivotIdx = PIVOT_INVALID;
	for (auto groupPDF : finalSymbolGroupPDFs)
	{
		// check start
		symidx_t groupEntryCount = finalGroupEntryCounts[groupPDF.symbol];
		// pivot is at CDF start of first block with >1 children
		if (pivotIdx == PIVOT_INVALID && groupEntryCount > 1)
		{
			pivotIdx = groupCDFs.size();
			pivotCDF = finalCDF;
		}
		count_t groupSymbolCount = finalGroupSymbolCounts[groupPDF.symbol];
		finalSymbols += groupEntryCount;
		finalCount += groupSymbolCount;
		double groupProbability = groupPDF.pdf;
		groupProbability /= probabilityRange;
		// entropy of encoding group index
		double groupEncodeEntropy = groupSymbolCount * -log2(groupProbability);
		double groupSymbolProbability = 1;
		groupSymbolProbability /= groupEntryCount;
		// entropy of encoding sub-index
		double groupSymbolEntropy = groupSymbolCount * -log2(groupSymbolProbability);
		finalEntropy += groupEncodeEntropy;
		finalEntropy += groupSymbolEntropy;
		symidx_t oldGroupStart = finalGroupStarts[groupPDF.symbol];
		symidx_t newGroupStart = symbols.size();
		// TODO move this check earlier
		assert(oldGroupStart < 65536);
		assert(newGroupStart < 65536);

		// Final check for if all symbols are in the correct group
		count_t checkedCount = 0;
		for (int i = 0; i < groupEntryCount; ++i)
		{
			symbol_t symbol = sortedSymbolCounts[oldGroupStart + i].symbol;
			if (symbol == 0)
				std::cout << "zero " << std::endl;
			checkedCount += sortedSymbolCounts[oldGroupStart + i].pdf;
			symbols.push_back(symbol);
		}
		finalCDF += groupPDF.pdf;
		groupCDFs.push_back(finalCDF);
		if (groupSymbolCount != checkedCount)
		{
			std::cout << "BAD VALUE " << groupPDF.symbol << " " << newGroupStart << " " << (newGroupStart + groupEntryCount) << " " << oldGroupStart << " " << (oldGroupStart + groupEntryCount) << " " << checkedCount << " " << groupSymbolCount << std::endl;
			return;
		}
		if (pivotIdx != PIVOT_INVALID)
			groupStarts.push_back(newGroupStart);
		assert(groupSymbolCount == checkedCount);
	}
	finalCount += rawSymbolCount;
	rawCDF = finalCDF;
	finalCDF += noCompressPDF;
	finalEntropy += rawValuesEntropy;
	// fix the last CDF entry (needed if probabilityRange == 1 << sizeof(probability))
	groupCDFs.push_back(probabilityRange - 1);

	std::cout << initialGroupCounts.size() << " initial groups" << std::endl;
	std::cout << finalGroupSymbolCounts.size() << " final groups" << std::endl;
	std::cout << countsSum << " initial count" << std::endl;
	std::cout << finalCount << " final count" << std::endl;
	std::cout << sortedSymbolCounts.size() << " initial symbol entries" << std::endl;
	std::cout << finalSymbols << " final symbol entries" << std::endl;
	std::cout << probabilityRange << " target CDF" << std::endl;
	std::cout << finalCDF << " final CDF" << std::endl;
	std::cout << pivotIdx << " pivot idx" << std::endl;
	std::cout << pivotCDF << " pivot CDF" << std::endl;
	std::cout << rawCDF << " raw CDF" << std::endl;
	std::cout << fastPath.size() << " fast-path entires" << std::endl;
	std::cout << slowPath.size() << " slow-path entires" << std::endl;
	std::cout << int(initialEntropy / 8) << " initial entropy (bytes)" << std::endl;
	std::cout << int(quantizedEntropy / 8) << " quantized entropy (bytes)" << std::endl;
	std::cout << int(finalEntropy / 8) << " final entropy (bytes)" << std::endl;
	std::cout << symbols.size() << " symbols" << std::endl;
	std::cout << groupCDFs.size() << " groups" << std::endl;

	assert(countsSum == finalCount);
	assert(probabilityRange == finalCDF);

	// generate encoding tables
	prob_t lastCDF = 0;
	for (int i = 0; i < groupCDFs.size() - 1;++i)
	{
		prob_t groupCDF = groupCDFs[i];
		prob_t groupPDF = groupCDF - lastCDF;

		assert(lastCDF != rawCDF);

		// fast path
		symidx_t groupEntryCount = 1;
		symidx_t groupStart = i;

		// slow path
		if (i >= pivotIdx)
		{
			// convert to group start index
			groupStart -= pivotIdx;
			symidx_t nextGroupStart = symbols.size();
			if (groupStart < groupStarts.size() - 1)
				nextGroupStart = groupStarts[groupStart + 1];
			// get actual start
			groupStart = groupStarts[groupStart];
			assert(nextGroupStart > groupStart);
			groupEntryCount = nextGroupStart - groupStart;
		}

		for (int j = 0; j < groupEntryCount; ++j)
		{
			symbol_t symbol = symbols[groupStart + j];
			symbolGroupsOut[symbol] = RansGroup(groupStart, groupEntryCount, groupPDF, lastCDF);
			symbolSubIdxOut[symbol] = j;
		}

		lastCDF = groupCDF;
	}

	// remaining CDF should be raw
	assert(lastCDF == rawCDF);

	// check all symbols encode/decode correctly
	for (auto symbolCount : unquantizedCounts)
	{
		symbol_t symbol = symbolCount.first;
		RansGroup group = symbolGroupsOut[symbol];

		// raw values
		if (group.start == (symidx_t)-1)
			continue;

		assert(group.cdf < rawCDF);

		// check group CDF lookup
		RansGroup test1 = GetSymbolGroup(group.cdf);
		assert(test1.pdf == group.pdf);
		assert(test1.cdf == group.cdf);
		// test output
		if (test1.start == 0)
		{
			assert(group.count == 1);
			assert(test1.count == symbol);
		}
		else
		{
			assert(test1.start == group.start);
			assert(test1.count == group.count);
			symidx_t subIdx = symbolSubIdxOut[symbol];
			assert(symbol == GetSymbol(test1.Pack(), subIdx));
		}
		RansGroup test2 = GetSymbolGroup(group.cdf + group.pdf - 1);
		assert(test2.pdf == group.pdf);
		assert(test2.cdf == group.cdf);
		// test output
		if (test2.start == 0)
		{
			assert(group.count == 1);
			assert(test2.count == symbol);
		}
		else
		{
			assert(test2.start == group.start);
			assert(test2.count == group.count);
			symidx_t subIdx = symbolSubIdxOut[symbol];
			assert(symbol == GetSymbol(test2.Pack(), subIdx));
		}
	}

	// if we reach this point, all symbol CDF values decode to themselves
}

CDFTable::CDFTable(const TableGroupList& groupList, uint32_t probabilityRes)
{
	pivotIdx = PIVOT_INVALID;
	pivotCDF = 0;
	groupCDFs.reserve(groupList.size());
	for (int group = 0; group < groupList.size() - 1; ++group)
	{
		prob_t groupCDF = groupList[group].first;
		symidx_t groupCount = groupList[group].second.size();

		if (pivotIdx == PIVOT_INVALID && groupCount > 1)
		{
			pivotIdx = groupCDFs.size();
			if(group > 0)
				pivotCDF = groupList[group - 1].first;
		}

		if (pivotIdx != PIVOT_INVALID)
		{
			// create group start
			symidx_t groupStart = symbols.size();
			groupStarts.push_back(groupStart);
			//std::cout << group << " " << groupStart << " " << groupCount << " " << groupCDF << std::endl;
		}
		else
		{
			//std::cout << group << " " << groupCount << " " << groupCDF << std::endl;
		}

		// load symbols
		symbols.insert(symbols.end(), groupList[group].second.begin(), groupList[group].second.end());

		// create group CDF
		groupCDFs.push_back(groupCDF);
	}

	rawCDF = groupCDFs.back();

	assert(groupList.back().second.size() == 0);
	groupCDFs.push_back(groupList.back().first);

	// this is needed for GetSymbolGroup optimizations to work
	assert(groupCDFs.back() - rawCDF > 0);

	assert(groupCDFs.back() == 1 << probabilityRes || groupCDFs.back() == 65535);
}

group_packed_t CDFTable::GetSymbolGroup(const prob_t symbolCDF)
{
	if (symbolCDF >= rawCDF)
		return 0xFFFFFFFF00000000ull | (((uint32_t)rawCDF) << 16) | (groupCDFs.back() - rawCDF);

	// find matching group
	const prob_t* __restrict groupCDFp = &groupCDFs.front();

	/*
	// poor-mans SIMD - check within bounds of next two entries
	// this ended up being the same speed on average...
	const uint32_t* __restrict groupCDF2p = (const uint32_t *)groupCDFp;
	uint32_t search2 = ((uint32_t)symbolCDF) << (8 * sizeof(prob_t));

	while (*groupCDF2p < search2)
		++groupCDF2p;

	// fine-grain search
	groupCDFp = (const prob_t *)groupCDF2p;

	while (*groupCDFp <= symbolCDF)
		++groupCDFp;
	*/

	if (symbolCDF >= pivotCDF)
		groupCDFp += pivotIdx;

	/* 
	for (; groupCDFp < groupCDFEnd; ++groupCDFp)
	{
		if (*groupCDFp > symbolCDF)
			break;
	}
	*/

	// this is only safe if rawPDF > 0 since the short-circuit at the top of this stops 
	// the last groupCDF value from being used - if the last value is valid here, the
	// code will likely yeet past the end of the array and crash
	while (*groupCDFp <= symbolCDF)
		++groupCDFp;

	//assert(*groupCDFp <= rawCDF);

	// calculate PDF
	prob_t groupStartCDF = 0;
	if (groupCDFp != &groupCDFs.front())
		groupStartCDF = groupCDFp[-1];
	const prob_t groupPDF = (*groupCDFp) - groupStartCDF;

	// get index from pointers
	group_t groupIdx = groupCDFp - &groupCDFs.front();

	// fast path for values before the pivot
	if (groupStartCDF < pivotCDF)
		return (((uint64_t)symbols[groupIdx]) << 48) | (((uint32_t)groupStartCDF) << 16) | groupPDF;

	// calculate num. symbols
	symidx_t nextGroupStart = symbols.size();
	const prob_t* groupCDFEnd = (&groupCDFs.back()) - 1;
	// convert to group start index
	groupIdx -= pivotIdx;
	if (groupCDFp != groupCDFEnd)
		nextGroupStart = groupStarts[groupIdx + 1u];
	const symidx_t groupCount = nextGroupStart - groupStarts[groupIdx];

	return (((uint64_t)groupCount) << 48) | (((uint64_t)groupStarts[groupIdx]) << 32) | (((uint32_t)groupStartCDF) << 16) | groupPDF;
}

symbol_t CDFTable::GetSymbol(group_packed_t group, symidx_t symbolIndex)
{
	//if ((group >> 32) & 0xFFFF) + symbolIndex >= symbols.size())
	//	std::cout << "BAD INDEX " << (group >> 32) & 0xFFFF) + symbolIndex << " " << symbols.size() << std::endl;
	return symbols[((group >> 32) & 0xFFFF) + symbolIndex];
}

TableGroupList CDFTable::GenerateGroupCDFs()
{
	TableGroupList groupList;

	// fast-path groups
	for (group_t groupIdx = 0; groupIdx < pivotIdx; ++groupIdx)
	{
		std::vector<symbol_t> groupSymbols;
		groupSymbols.push_back(symbols[groupIdx]);
		groupList.emplace_back(groupCDFs[groupIdx], std::move(groupSymbols));
		//std::cout << groupIdx << " " << 1 << " " << groupCDFs[groupIdx] << std::endl;
	}
	// slow-path groups
	for (group_t groupIdx = pivotIdx; groupIdx < pivotIdx + groupStarts.size(); ++groupIdx)
	{
		group_t startIndex = groupIdx - pivotIdx;
		std::vector<symbol_t> groupSymbols;
		symidx_t groupStart = groupStarts[startIndex];
		symidx_t nextStart = symbols.size();
		if(startIndex < groupStarts.size() - 1)
			nextStart = groupStarts[startIndex + 1];
		symidx_t numChildren = nextStart - groupStart;
		for (int i = 0; i < numChildren; ++i)
			groupSymbols.push_back(symbols[groupStart + i]);

		groupList.emplace_back(groupCDFs[groupIdx], std::move(groupSymbols));
		//std::cout << groupIdx << " " << groupStart << " " << numChildren << " " << groupCDFs[groupIdx] << std::endl;
	}
	assert(groupList.back().first == rawCDF);
	// rawCDF
	groupList.emplace_back(groupCDFs.back(), std::vector<symbol_t>());

	return groupList;
}

RansTable::RansTable(SymbolCountDict unquantizedCounts, uint32_t probabilityRes)
{
	cdfTable = CDFTable(unquantizedCounts, probabilityRes, symbolGroups, symbolSubIdx);
}

RansTable::RansTable(const TableGroupList& groupList, uint32_t probabilityRes)
{
	cdfTable = CDFTable(groupList, probabilityRes);
}

TableGroupList RansTable::GenerateGroupCDFs()
{
	return cdfTable.GenerateGroupCDFs();
}

RansGroup RansTable::GetSymbolGroup(symbol_t symbol)
{
	return symbolGroups[symbol];
}

symidx_t RansTable::GetSymbolSubIdx(const symbol_t symbol)
{
	return symbolSubIdx[symbol];
}

// Cumulative probability to rANS group
group_packed_t RansTable::GetSymbolGroupFromFreq(const prob_t prob)
{
	return cdfTable.GetSymbolGroup(prob);
}

symbol_t RansTable::GetSymbolFromGroup(const group_packed_t group, const symidx_t subIndex)
{
	return cdfTable.GetSymbol(group, subIndex);
}

size_t RansTable::GetMemoryFootprint() const
{
	// this isn't that accurate
	// TODO UPDATE
	size_t mapSize = 0;// std::max(symbolTable.bucket_count(), symbolTable.size()) * sizeof(std::unordered_map<uint16_t, RansEntry>::value_type);
	// TODO UPDATE
	size_t vectorSize = 0;// cdfTable.capacity() * sizeof(RansEntry);

	return sizeof(RansTable) + mapSize + vectorSize;
}

RansState::RansState()
	: ransState(0)
{

}

// rANS state - size of state = size of probability + size of output block
RansState::RansState(SymbolCountDict counts)
	: ransTable()
{
	compressedBlocks = std::shared_ptr<VectorStream<block_t>>(new VectorVectorStream<block_t>());
	
	ransTable = std::make_shared<RansTable>(counts, PROBABILITY_RES);
	// You can technically set initial rANS state to anything, but I choose the min. val
	ransState = STATE_MIN;
}

// fast constructor
RansState::RansState(std::shared_ptr<RansTable> symbolTable)
{
	compressedBlocks = std::shared_ptr<VectorStream<block_t>>(new VectorVectorStream<block_t>());

	ransTable = symbolTable;
	// You can technically set initial rANS state to anything, but I choose the min. val
	ransState = STATE_MIN;
}

// initialize for decoding
RansState::RansState(std::shared_ptr<VectorStream<block_t>> compressedBlocks, state_t ransState, SymbolCountDict counts)
	: RansState(counts)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}

RansState::RansState(std::shared_ptr<VectorStream<block_t>>  compressedBlocks, state_t ransState, std::shared_ptr<RansTable> symbolTable)
	: RansState(symbolTable)
{
	this->compressedBlocks = compressedBlocks;
	this->ransState = ransState;
}


void RansState::AddGroup(RansGroup group)
{
	//std::cout << "State1: " << ransState << std::endl;
	/*
	while (ransState >= BLOCK_SIZE * group.pdf)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		ransState /= BLOCK_SIZE;
	}

	std::cout << "State2: " << ransState << std::endl;
	*/

	// renormalize if necessary

	assert(group.cdf < PROBABILITY_RANGE);

	// write group (also handles fast path)
	// add symbol to rANS state
	state_t newState = ransState / group.pdf;
	newState *= PROBABILITY_RANGE;
	newState += group.cdf;
	newState += ransState % group.pdf;

	int count = 0;
	while (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		//std::cout << "push " << int(compressedBlocks->back()) << std::endl;
		ransState /= BLOCK_SIZE;
		newState = ransState / group.pdf;
		newState *= PROBABILITY_RANGE;
		newState += group.cdf;
		newState += ransState % group.pdf;
		++count;
		if (count > 100)
		{
			std::cout << "RANS STUCK IN LOOP?" << std::endl;
			continue;
		}
	}

	assert(count != 3);

	/*
	// renormalize if necessary
	if (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		std::cout << "push " << int(compressedBlocks->back()) << std::endl;
		ransState /= BLOCK_SIZE;
		AddGroup(group);
		return;
	}
	std::cout << "State1.5: " << ransState << std::endl;
	*/
	/*
	// renormalize if necessary
	while (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		ransState /= BLOCK_SIZE;
		newState = ransState / group.pdf;
		newState *= probabilityRange;
		newState += group.cdf;
		newState += ransState % group.pdf;
	}
	*/
	ransState = newState;
	//std::cout << "State3: " << ransState << std::endl;

	// current state is the state our value will be decoded at
	assert(ransState >= STATE_MIN);
	assert(ransState <= STATE_MAX);
}

void RansState::AddSubIdx(symbol_t symbol, RansGroup group)
{
	// find symbol idx in group
	symidx_t symbolIdx = ransTable->GetSymbolSubIdx(symbol);
	//std::cout << "adding symbol idx " << symbol << " " << symbolIdx << " " << group.count << std::endl;

	// double-check
	if (ransTable->GetSymbolFromGroup(group.Pack(), symbolIdx) != symbol)
	{
		std::cout << group.cdf << " " << group.pdf << std::endl;
		std::cout << "Bad symbol encode " << symbol << " " << symbolIdx << std::endl;
		return;
	}

	assert(symbolIdx < group.count);
	assert(group.count > 1);
	assert(group.count < PROBABILITY_RANGE);

	//std::cout << "State1: " << ransState2 << std::endl;
	// renormalize if necessary
	int count = 0;

	// TODO FIX THIS
	//while (ransState2 >= BLOCK_SIZE * (STATE_MIN / probabilityRange))
	/*
	while ((ransState2 * group.count) + symbolIdx > STATE_MAX
		// TODO dumb hack to work around rounding issues
		&& ((ransState2 / BLOCK_SIZE) * group.count) + symbolIdx >= STATE_MIN)
		*/
	// if adding this value stops underflow from triggering, we need to trigger underflow
	//while (((ransState2 * group.count) + symbolIdx) / BLOCK_SIZE >= STATE_MIN)
	// floating min
	/*
	while ((ransState2 * group.count) + symbolIdx > (BLOCK_SIZE * group.count) - 1)
	{
		std::cout << "Renorm " << ((ransState2 * group.count) + symbolIdx) << std::endl;
		compressedBlocks->push_back(ransState2 % BLOCK_SIZE);
		ransState2 /= BLOCK_SIZE;
		++count;
		if (count > 100)
		{
			std::cout << "RANS STUCK IN LOOP?" << std::endl;
			continue;
		}
	}

	// add symbol to rANS state
	uint64_t newState = ransState2 * group.count;
	newState += symbolIdx;

	std::cout << "State2: " << ransState2 << std::endl;
	*/
	
	prob_t pdf = (PROBABILITY_RANGE - 1) / group.count;
	assert(pdf > 0);
	prob_t cdf = pdf * symbolIdx;
	assert(cdf < PROBABILITY_RANGE);
	// renormalize if necessary
	// add symbol to rANS state
	state_t newState = ransState / pdf;
	newState *= PROBABILITY_RANGE;
	newState += cdf;
	newState += ransState % pdf;
	// push the bytes the decoder will need to read after decoding our value
	while (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		//std::cout << "push " << int(compressedBlocks->back()) << std::endl;
		ransState /= BLOCK_SIZE;
		newState = ransState / pdf;
		newState *= PROBABILITY_RANGE;
		newState += cdf;
		newState += ransState % pdf;
		++count;
		if (count > 100)
		{
			std::cout << "RANS STUCK IN LOOP?" << std::endl;
			continue;
		}
	}

	assert(count != 3);
	
	/*
	// renormalize if necessary
	if (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState2 % BLOCK_SIZE);
		std::cout << "push " << int(compressedBlocks->back()) << std::endl;
		ransState2 /= BLOCK_SIZE;
		AddSubIdx(symbol, group);
		return;
	}

	std::cout << "State2: " << ransState2 << std::endl;
	*/
	ransState = newState;
	//std::cout << "State3: " << newState << " " << STATE_MIN  << " " << STATE_MAX << std::endl;

	// current state is the state our value will be decoded at
	//assert(ransState2 >= group.count);
	assert(ransState >= STATE_MIN);
	assert(ransState <= STATE_MAX);
}

// Encode symbol
void RansState::AddSymbol(symbol_t symbol)
{
	// Write sub-index
	RansGroup group = ransTable->GetSymbolGroup(symbol);

	// raw symbol
	// encoder: symbol, renorm, group
	// decoder: group, renorm, symbol
	if (group.start == (symidx_t)-1)
	{
		// TODO this isn't ideal - we should pack into rANS state if symbol_t != block_t
		// the reason I'm not setting that up right now is raw groups make up <1% of the data
		// and the renormalization for this needs thinking through, since it can use a different
		// modulo to symbol reads
		compressedBlocks->push_back(symbol);
	}
	else if (group.start != 0 && group.count > 1)
	{
		AddSubIdx(symbol, group);
		// TODO fix
		/*
		// find symbol idx in group
		symidx_t symbolIdx = ransTable->GetSymbolSubIdx(symbol);
		std::cout << "adding symbol idx " << symbol << " " << symbolIdx << std::endl;

		// double-check
		if (ransTable->GetSymbolEntryFromGroup(group, symbolIdx) != symbol)
		{
			std::cout << group.cdf << " " << group.pdf << std::endl;
			std::cout << "Bad symbol encode " << symbol << " " << symbolIdx << std::endl;
			return;
		}

		// add symbol to rANS state
		uint64_t newState = ransState * group.count;
		newState += symbolIdx;

		std::cout << "State1: " << newState << std::endl;

		// renormalize if necessary
		while (newState > STATE_MAX)
		{
			compressedBlocks->push_back(ransState % BLOCK_SIZE);
			ransState /= BLOCK_SIZE;
			newState = ransState * group.count;
			newState += symbolIdx;
		}

		ransState = newState;
		std::cout << "State2: " << newState << std::endl;
		*/
	}

	//std::cout << "adding group " << symbol << " " << group.count << " " << group.start << " " << group.pdf << " " << group.cdf << " " << probabilityRange << std::endl;
	AddGroup(group);
	/*
	// write group (also handles fast path)
	// add symbol to rANS state
	uint64_t newState = ransState / group.pdf;
	newState *= probabilityRange;
	newState += group.cdf;
	newState += ransState % group.pdf;

	std::cout << "State3: " << newState << std::endl;	

	// renormalize if necessary
	while (newState > STATE_MAX)
	{
		compressedBlocks->push_back(ransState % BLOCK_SIZE);
		ransState /= BLOCK_SIZE;
		newState = ransState / group.pdf;
		newState *= probabilityRange;
		newState += group.cdf;
		newState += ransState % group.pdf;
	}

	std::cout << "State4: " << newState << std::endl;
	ransState = newState;
	*/
}

// Decode symbol
symbol_t RansState::ReadSymbol()
{
	// read group
	// TODO this can be a bitwise AND
	prob_t cumulativeProb = ransState % PROBABILITY_RANGE;
	const group_packed_t group = ransTable->GetSymbolGroupFromFreq(cumulativeProb);
	group_packed_t groupShifted = group;
	// TODO can use bit shift
	state_t newState = ransState / PROBABILITY_RANGE;
	// TODO generate mask from prob_t
	// groupShifted & 0xFFFF = PDF
	newState = newState * (prob_t)groupShifted;
	// TODO can use bitwise AND
	newState += ransState % PROBABILITY_RANGE;
	groupShifted = groupShifted >> 16;
	// groupShifted & 0xFFFF = CDF
	newState -= (prob_t)groupShifted;
	groupShifted = groupShifted >> 16;

	ransState = newState;

	//std::cout << "State2: " << ransState << std::endl;

	//std::cout << "read group " << group.count << " " << group.start << " " << " " << group.pdf << " " << group.cdf << " " << probabilityRange << std::endl;

	// feed data into state as needed
	while (ransState < STATE_MIN)
	{
		//std::cout << "pop " << int(compressedBlocks->back()) << std::endl;
		ransState *= BLOCK_SIZE;
		ransState += compressedBlocks->back();
		compressedBlocks->pop_back();
	}

	// raw symbols
	// groupShifted & 0xFFFF = start
	if ((group_t)groupShifted == (group_t)-1)
	{
		symbol_t symbol = compressedBlocks->back();
		compressedBlocks->pop_back();
		return symbol;
	}

	// fast path for values before the pivot
	// groupShifted & 0xFFFF = start
	if ((group_t)groupShifted == 0)
		// symbol is smuggled in count to skip a layer of indirection
		// groupShifted & 0xFFFF = count
		return (groupShifted >> 16);

	// if there's only 1 item in the group, no need to read
	// this shouldn't trigger anymore
	/*
	if (group.count == 1)
		assert(false);
		//return ransTable->GetSymbolFromGroup(group, 0);
		*/

	// read index
	// (groupShifted >> 16) & 0xFFFF = count
	//assert(((groupShifted >> 16) & 0xFFFF) > 0);
	prob_t pdf = (PROBABILITY_RANGE - 1) / (groupShifted >> 16);
	assert(pdf > 0);
	// TODO this can be a bitwise AND
	prob_t readCDF = ransState % PROBABILITY_RANGE;
	symidx_t index = readCDF / pdf;
	prob_t cdf = pdf * index;
	assert(cdf < PROBABILITY_RANGE);
	// TODO can use bit shift
	newState = ransState / PROBABILITY_RANGE;
	newState = newState * pdf;
	// TODO can use bitwise AND
	newState += ransState % PROBABILITY_RANGE;
	newState -= cdf;
	symbol_t symbol = ransTable->GetSymbolFromGroup(group, index);

	ransState = newState;

	/*
	// feed data into state as needed
	while (ransState2 < group.count)
	{
		std::cout << "pop " << int(compressedBlocks->back()) << std::endl;
		ransState2 *= BLOCK_SIZE;
		ransState2 += compressedBlocks->back();
		compressedBlocks->pop_back();
	}

	//std::cout << "State3: " << ransState << std::endl;

	// read sub-index
	// get sub index (all symbols have PDF=1/group.count)
	cumulativeProb = ransState2 % group.count;
	symbol_t symbol = ransTable->GetSymbolEntryFromGroup(group, cumulativeProb);
	newState = ransState2 / group.count;
	// PDF is 1
	//newState = newState * entry.pdf;
	// these values are identical because PDF is 1
	//newState += ransState % group.count;
	//newState -= cumulativeProb;
	*/

	//std::cout << "read sub-group " << symbol << " " << cumulativeProb << std::endl;

	ransState = newState;

	// feed data into state as needed
	while (ransState < STATE_MIN)
	{
		//std::cout << "pop " << int(compressedBlocks->back()) << std::endl;
		ransState *= BLOCK_SIZE;
		ransState += compressedBlocks->back();
		compressedBlocks->pop_back();
	}
	//std::cout << "State5: " << ransState2 << std::endl;

	return symbol;
}

const std::vector<block_t> RansState::GetCompressedBlocks()
{
	return compressedBlocks->get_vec();
}


uint64_t RansState::GetRansState()
{
	return ransState;// +(ransState2 << 32);
}


bool RansState::HasData()
{
	if (ransState != STATE_MIN)
		return true;
	// this is now slower
	if (compressedBlocks->size() > 0)
		return true;
	return false;
}


bool RansState::IsValid()
{
	bool valid = true;

	// check state is between min/max values
	valid = valid && (STATE_MIN <= ransState || compressedBlocks->size() > 0) && STATE_MAX >= ransState;

	// check rANS state is large enough
	valid = valid && std::numeric_limits<uint64_t>::max() / PROBABILITY_RANGE > BLOCK_SIZE;

	// TODO table checks?

	return valid;
}