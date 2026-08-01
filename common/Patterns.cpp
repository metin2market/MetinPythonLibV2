//#include "stdafx.h"
#include "Patterns.h"
#include "utils.h"


Patterns::Patterns(HMODULE hMod, Pattern* modulePattern) : hMod(hMod){
	if(!Init(modulePattern))
		throw std::exception("Fail to Initialize Patterns Class");
}

Patterns::~Patterns(){
}
bool Patterns::Init(Pattern* modulePattern) {

	if (!modulePattern) {
		if (!setModuleInfo())
			throw std::runtime_error("Error setting module");
	}
	else {
		// LAUNCH-CRASH FIX (client 26.1.11): the old path set SizeOfImage = 0x7FFFFFFF and scanned the ENTIRE
		// 0..2GB address space for GLOBAL_PATTERN just to LOCATE the host module. That whole-address-space walk
		// reads across arbitrary region boundaries and faults (0xC0000005 at ~0x15270000) on the new client's
		// rearranged layout. The host module is simply the main executable: GetModuleHandle(NULL) gives its base
		// and the PE OptionalHeader.SizeOfImage bounds the scan exactly to metin2client.exe -- no giant scan, and
		// every real signature lives inside this range anyway. modulePattern's CONTENTS are dead, but its
		// NULL-ness still selects the branch: CAddressLoader passes &global to reach this path, while the
		// pattern scanner relies on the default 0 to reach setModuleInfo(). Do not drop the argument.
		(void)modulePattern;
		HMODULE hExe = GetModuleHandle(NULL);
		if (!hExe)
			throw std::runtime_error("GetModuleHandle(NULL) failed locating host module");
		IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hExe;
		IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)hExe + dos->e_lfanew);
		mInfo.EntryPoint = 0;
		mInfo.lpBaseOfDll = (LPVOID)hExe;
		mInfo.SizeOfImage = nt->OptionalHeader.SizeOfImage;
	}

	LOG_INFO("Module base=%x size=%x", mInfo.lpBaseOfDll, mInfo.SizeOfImage);

	return true;
}

DWORD Patterns::FindPattern(const char *pattern, const char *mask)
{
	/*int len = strlen(mask);
	printf("Pattern: ");
	for (int i = 0; i < len; i++) {
		printf("%#x ", (BYTE)pattern[i]);
	}
	printf("\n\n");*/

	DWORD patternLength = (DWORD)strlen(mask);
	MEMORY_BASIC_INFORMATION PermInfo;


		DWORD startModule = (DWORD)mInfo.lpBaseOfDll;
		DWORD endModule = startModule + (DWORD)mInfo.SizeOfImage;
		int pageEndAddr = startModule;

		for (DWORD indexAddr = startModule; indexAddr + patternLength <= endModule; indexAddr++)
		{
			if (pageEndAddr <= indexAddr)
			{
				if (VirtualQuery((LPCVOID*)(indexAddr), &PermInfo, sizeof(PermInfo)))
				{
					//printf("Permission info %#x\n", PermInfo.Protect);
					//printf("State info %#x\n", PermInfo.State);
					//system("pause");
					if ((PermInfo.State != MEM_COMMIT) || PermInfo.Protect & PAGE_NOACCESS || PermInfo.Protect & PAGE_GUARD) {
						// BUGFIX (launch crash on client 26.1.11): the region END is BaseAddress + RegionSize, NOT a
						// running "pageEndAddr += RegionSize". When VirtualQuery's BaseAddress < indexAddr the running
						// total overshoots -> pageEndAddr lands past the mapped range -> the boundary check at line ~90
						// passes -> the compare loop reads *(indexAddr+j) into an unmapped page -> 0xC0000005. The
						// larger/rearranged 26.1.11 layout (plus the now-unmatchable sigs scanning the whole module)
						// makes an unreadable region follow a committed one at exactly the offending boundary.
						pageEndAddr = (DWORD)PermInfo.BaseAddress + PermInfo.RegionSize;
						indexAddr = pageEndAddr;
						continue;
					}
					else {
						indexAddr = (DWORD)PermInfo.BaseAddress;
						pageEndAddr = indexAddr + PermInfo.RegionSize;
					}
				}
				else {
					// This is a byte-granular loop: continuing without advancing re-queries the same
					// address, so one failing region would re-fail and re-log forever, hanging the game
					// thread while it fills the disk. VirtualQuery told us no region size, so skip a
					// page and log only the first failure.
					static bool loggedQueryFail = false;
					if (!loggedQueryFail) {
						loggedQueryFail = true;
						LOG_ERROR("Error Querying memory at %#x", indexAddr);
					}
					pageEndAddr = (indexAddr & 0xFFFFF000) + 0x1000;
					indexAddr = pageEndAddr;
					continue;
				}
			}

			if (indexAddr + patternLength > pageEndAddr) {
				indexAddr = pageEndAddr;
				continue;
			}
			bool found = true;
			for (DWORD j = 0; j < patternLength && found; j++)
			{
				//if we have a ? in our mask then we have true by default, 
				//or if the bytes match then we keep searching until finding it or not
				found &= mask[j] == '?' || pattern[j] == *(char*)(indexAddr + j);
			}

			//found = true, our entire pattern was found
			//return the memory addy so we can write to it
			if (found) {
				return indexAddr;
			}
		}
	return NULL;
}




DWORD* Patterns::GetPatternAddress(Pattern* pat) {
	
	DWORD* addr = (DWORD*)FindPattern(pat->pattern, pat->mask);
	if (addr) { 
		
		DWORD* result = (DWORD*)((int)addr + pat->offset);
		LOG_DEBUG("Pattern %s with address -> %#x", pat->name, result);
		return result;

	}else {
		LOG_ERROR("ERROR FINDING PATTERN -> %s", pat->name);
	}

	return addr;
}

bool Patterns::setModuleInfo()
{
	HANDLE psHandle = GetCurrentProcess();

	TCHAR buffer[MAX_PATH];
	int path_size = GetProcessImageFileName(psHandle, buffer, MAX_PATH);


	if (!path_size) {
		LOG_ERROR("Fail to Get Module FileName");
		return false;
	}


	PathStripPath(buffer);
	HMODULE hModule = GetModuleHandle(buffer);
	if (hModule == 0)
		return false;

	LOG_INFO("Scanning module: %s", buffer);
	GetModuleInformation(psHandle, hModule, &mInfo, sizeof(MODULEINFO));
	mInfo.SizeOfImage = getModuleSize(mInfo.lpBaseOfDll);
	return true;
}

int Patterns::getModuleSize(void * baseAddress)
{
	PIMAGE_DOS_HEADER     pDosH = (PIMAGE_DOS_HEADER)baseAddress;
	if (pDosH->e_magic == (WORD)0x5A4D) {
		PIMAGE_NT_HEADERS     pNtH = (PIMAGE_NT_HEADERS)((int)baseAddress + pDosH->e_lfanew);
		if (pNtH->Signature == (DWORD)0x4550) {
			if (pNtH->OptionalHeader.Magic == (WORD)0x010B) {
				return pNtH->OptionalHeader.SizeOfImage;
				
			}
		}

	}
	return 0;
}

void Patterns::printModules()
{
	HANDLE psHandle = GetCurrentProcess();
	HMODULE modules[1000] = { 0 };
	DWORD numModules = 0;

	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
	if (Snapshot == INVALID_HANDLE_VALUE) {
		LOG_ERROR("Error CreatingToolhelp32, code: %#x", GetLastError());
		return;
	}

	MODULEENTRY32 module;
	module.dwSize = sizeof(MODULEENTRY32);

	if (!Module32First(Snapshot, &module)) {
		LOG_ERROR("Error on Module32First, code: %#x", GetLastError());
		return;
	}

	do
	{
		// One record per line: the log is grepped, and a multi-line entry loses the timestamp and
		// level prefix on every line but the first.
		LOG_DEBUG("module %s base=0x%08X size=0x%X refs=%u/%u pid=0x%08X exe=%s",
			module.szModule, (DWORD)module.modBaseAddr, module.modBaseSize,
			module.GlblcntUsage, module.ProccntUsage, module.th32ProcessID, module.szExePath);

	} while (Module32Next(Snapshot, &module));
}


void* Patterns::GetStartModuleAddress() {
	return mInfo.lpBaseOfDll;
}