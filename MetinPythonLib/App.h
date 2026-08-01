#pragma once
#include "stdafx.h"
#include "Singleton.h"
#include "defines.h"

#define METIN_GF


extern HMODULE hDll;

// eXLib deliberately probes client memory that may have gone stale -- pinGameWindow's exReadDwordSafe
// and CMemory::GetDialogAnswerCount both fault by design and handle it in their own __except. The crash
// logger in main.cpp is a first-chance VECTORED handler, so it runs before that __except and would
// rewrite exlib_crash.txt on every frame. Bracket such a read to tell it the fault is expected.
extern volatile LONG g_exExpectedFault;
inline void exExpectedFaultEnter() { InterlockedIncrement(&g_exExpectedFault); }
inline void exExpectedFaultLeave() { InterlockedDecrement(&g_exExpectedFault); }



class CApp : public CSingleton<CApp>{
public:
	CApp();
	~CApp();

	void init();
	void exit();
	void setSkipRenderer();
	void unsetSkipRenderer();

	bool __AppProcess(ClassPointer p);

private:
	void initMainThread();
	void initPythonModules();
	void SetupConsole();

private:

	bool mainScriptExec;
	bool passed;
	bool wasInGame;          // previous frame's in-game state -- the edge that catches a channel switch
	std::string lastMap;     // last map name -- a change means a teleport/map warp
};
