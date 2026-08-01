#pragma once
#include "../common/Patterns.h"
#include <map>
class CAddressLoader
{
public:
	CAddressLoader();
	~CAddressLoader();

	// False only when the pattern scanner itself failed to construct, so nothing resolved and nothing
	// will. True says nothing about how many signatures matched -- setAddress logs that count itself.
	bool setAddress(HMODULE hDll);
	void* GetAddress(int id);

private:
	std::map<int, DWORD> memoryAddress;
};

