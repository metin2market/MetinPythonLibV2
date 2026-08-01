#pragma once
#include <Windows.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <io.h>
#include "../common/Offsets.h"
#include "../common/Patterns.h"
#include "../common/Config.h"
#include "../common/Log.h"
#include <sstream>

#define PRINT(...); {{printf(__VA_ARGS__); printf("\n");fflush(stdout);}}



HANDLE threadID;
HMODULE hDll;

struct DLLArgs {
	int size;
	int reserved;
	char path[256];
};

void SetupConsole()
{
	AllocConsole();
	freopen("CONOUT$", "wb", stdout);
	freopen("CONOUT$", "wb", stderr);
	freopen("CONIN$", "rb", stdin);
	SetConsoleTitle("Debug Console");

}


DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
	// The scanner is read from this console. It also writes the game dir's _logs\ like any other
	// host, so a scan run and a live bot session land in the same folder -- tell them apart by the
	// session timestamp in the filename.
	SetupConsole();
	Patterns* patternFinder = 0;
	try {
		patternFinder = new Patterns(hDll);
	}
	catch (std::exception& e) {
		LOG_ERROR("Pattern scanner failed to initialize: %s -- nothing to dump", e.what());
		return false;
	}

	// Dumps the adresses from the current process
	std::map<int, std::pair<int,std::string>> addresses;

	for (auto & x : memPatterns) {
		int addr = (int)patternFinder->GetPatternAddress(&x.second);
		addr -= (int)patternFinder->GetStartModuleAddress();
		addresses.insert({ x.first,{addr,std::string(x.second.name)} });
	}

#ifdef DUMP_TO_FILE
	std::ofstream myfile;
	std::string path= getDllPath();
	path += ADRESS_FILE_NAME;
	myfile.open(path.c_str());
	for (auto& x : addresses) {
		myfile << std::dec << x.first << "\t0x" << std::hex << std::setfill('0') << std::setw(6) << x.second.first << "\t" << x.second.second << "\n";
	}
	PRINT("Addresses wrote to %s", path.c_str());
	myfile.close();
#endif



	delete patternFinder;
	return 1;
}


BOOLEAN WINAPI DllMain(IN HINSTANCE hDllHandle,
	IN DWORD     nReason,
	IN LPVOID    Reserved)
{


	//  Perform global initialization.
	char test[256] = { 0 };
	DLLArgs* args = (DLLArgs*)Reserved;
	switch (nReason)
	{
	case DLL_PROCESS_ATTACH:
		if (Reserved) {
			DLLArgs* args = (DLLArgs*)Reserved;
			setDllPath(args->path);
		}
		else {
			GetModuleFileNameA(hDllHandle, test, 256);
			setDllPath(test);
		}
		hDll = (HMODULE)hDllHandle;
		// Off the loader lock: AllocConsole + freopen + a full module pattern scan all deadlock or
		// stall other threads' DLL loads if run inline here. Nothing below waits on the scan.
		threadID = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
		break;

		/*case DLL_PROCESS_DETACH:
			CApp & app = CApp::Instance();
			PRINT("DETACHED CALLED");
			app.exit();
			break;*/
	}

	return true;
}

