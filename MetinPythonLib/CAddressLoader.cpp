#include "stdafx.h"
#include "CAddressLoader.h"
#include "../common/Offsets.h"
#include "defines.h"

CAddressLoader::CAddressLoader()
{
}

CAddressLoader::~CAddressLoader()
{
}

bool CAddressLoader::setAddress(HMODULE hDll)
{
	Patterns* patternFinder = 0;
	Pattern global("GLOBAL_PATTERN", GLOBAL_PATTERN_OFFSET, GLOBAL_PATTERN, GLOBAL_PATTERN_MASK);
	try {
		patternFinder = new Patterns(hDll, &global);
	}
	catch (std::exception& e) {
		LOG_ERROR("Pattern scanner failed to initialize: %s -- no addresses will resolve", e.what());
		return false;
	}

	// A signature that no longer matches is stored as address 0 on purpose: the call* wrappers in
	// Memory.h check both their function pointer and the singleton they pass as `this`, so a dead
	// signature degrades that one feature to a no-op instead of jumping to 0 or faulting deep inside
	// the client. The counts below are the only warning a client patch gives us.
	int resolved = 0;
	for (auto& pattern : memPatterns) {
		DWORD addr = (DWORD)patternFinder->GetPatternAddress(&pattern.second);
		if (addr)
			++resolved;
		memoryAddress.insert({ pattern.first, addr });
	}
	int total = (int)memPatterns.size();
	if (resolved == total)
		LOG_INFO("%d/%d builtin patterns resolved", resolved, total);
	else
		LOG_ERROR("%d/%d builtin patterns resolved -- %d dead, re-derive", resolved, total, total - resolved);

	delete patternFinder;
	return true;
}

void* CAddressLoader::GetAddress(int id)
{
	auto it = memoryAddress.find(id);
	if (it == memoryAddress.end()) {
		LOG_DEBUG("No address resolved for id %d", id);
		return 0;
	}
	return (void*)it->second;
}
