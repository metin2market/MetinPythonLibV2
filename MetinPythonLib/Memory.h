#pragma once
#include "stdafx.h"
#include "DetoursHook.h"
#include "../common/Patterns.h"
#include "ReturnHook.h"
#include "defines.h"
#include "Singleton.h"
#include "MemoryPatch.h"



class CMemory : public CSingleton<CMemory>
{
public:
	CMemory();
	~CMemory();

	bool setupPatterns(HMODULE hDll);
	bool setupHooks();

	bool setupProcessHook();

	inline ClassPointer getPythonNetwork() { return *pythonNetwork; };
	inline ClassPointer getPythonChrMgr() { return *pythonChrMgr; };
	inline ClassPointer getPythonPlayer() { return *pythonPlayer; };
	inline ClassPointer getNetworkStream() { return networkStream; };
	inline void setNetworkStream(ClassPointer val) {networkStream=val; };

	//Hooked original functions  (walker build: NULL-guarded so dead-sig features no-op instead of calling NULL)
	inline bool callBackgroundCheckAdv(ClassPointer p,void* instanceBase) { if(!backgroundCheckAdvHook->originalFunction) return true; return backgroundCheckAdvHook->originalFunction(p, instanceBase);}
	inline bool callIntsanceBaseCheckAdv(ClassPointer p) { if(!instanceBaseCheckAdvHook->originalFunction) return true; return instanceBaseCheckAdvHook->originalFunction(p); }
	inline bool callSendSequence() { if(!sendSequenceHook->originalFunction) return false; return sendSequenceHook->originalFunction(getNetworkStream()); }
	inline bool callSendPacket(int size, void* buffer, ClassPointer classPointer = 0) { if (sendHook->originalFunction && getNetworkStream() && classPointer == 0) return sendHook->originalFunction(getNetworkStream(), size, buffer); return false; }//If network stream is not set, the function will not be called
	inline bool callSendStatePacket(fPoint& pos, float rot, BYTE eFunc, BYTE uArg) { if(!sendStateHook->originalFunction) return false; return sendStateHook->originalFunction(getPythonNetwork(),pos,rot,eFunc,uArg); }
	inline bool callMoveToDestPosition(ClassPointer p, fPoint& pos) { if(!setMoveToDestPositionHook->originalFunction) return false; return setMoveToDestPositionHook->originalFunction(p, pos); }
	inline bool callMoveToDirection(ClassPointer p, float rot) { if(!setMoveToDirectionHook->originalFunction) return false; return setMoveToDirectionHook->originalFunction(p, rot); }
	inline bool callProcess(ClassPointer p) { if(!processHook->originalFunction) return false; return processHook->originalFunction(p); }
	inline bool callGet(ClassPointer cp, CMappedFile& file, const char* fileName, void** buffer) { if(!getEtherPacketHook->originalFunction) return false; return getEtherPacketHook->originalFunction(cp, file, fileName, (LPCVOID*)buffer); }
	inline bool callCheckPacket(BYTE * header) { if(!checkPacketHook->originalFunction || !getNetworkStream()) return false; return checkPacketHook->originalFunction(getNetworkStream(),header); }
	inline bool callRecvPacket(int size, void* buffer) { if(!recvHook->originalFunction) return false; return recvHook->originalFunction(getNetworkStream(), size, buffer); }

	//Client functions
	inline bool callSendAttackPacket(BYTE type, DWORD vid) { if(!sendAttackPacketHook->originalFunction) return false; return sendAttackPacketHook->originalFunction(getPythonNetwork(), type, vid); }
	inline bool callGlobalToLocalPosition(long& lx, long& ly){ if(!globalToLocalFunc) return false; return globalToLocalFunc(getPythonNetwork(),lx,ly);}
	inline bool callLocalToGlobalPosition(long& lx, long& ly) { if(!localToGlobalFunc) return false; return localToGlobalFunc(getPythonNetwork(), lx, ly); }
	inline void* callGetInstancePointer(DWORD vid) { if(!getInstanceFunc) return 0; return getInstanceFunc(getInstanceClassPtr,vid); }
	inline void callSendUseSkillBySlot(DWORD dwSkillSlotIndex, DWORD dwTargetVID) { if(!sendUseSkillBySlotFunc) return; return sendUseSkillBySlotFunc(getPythonPlayer(), dwSkillSlotIndex, dwTargetVID); }
	inline bool callPeek(int len, void*buffer) { if(!peekFunc) return false; return peekFunc(getNetworkStream(),len,buffer); }

	void setSkipRenderer();
	void unsetSkipRenderer();

	DetoursHook<tTracef>* traceFHook;
	DetoursHook<tTracef>* tracenFHook;

private:
	inline ClassPointer* SetClassPointer(DWORD** pointer) { if(pointer)return (ClassPointer*)*pointer; };

	

private:

	//Hooks
	DetoursHook<tRecvPacket>* recvHook;

	DetoursHook<tGet> * getEtherPacketHook;
	DetoursHook<tBackground_CheckAdvancing>* backgroundCheckAdvHook;
	DetoursHook<tInstanceBase_CheckAdvancing>* instanceBaseCheckAdvHook;
	DetoursHook<tSendSequencePacket>* sendSequenceHook;
	DetoursHook<tSendPacket>* sendHook;
	DetoursHook<tSendStatePacket>* sendStateHook;
	DetoursHook<tMoveToDestPosition>* setMoveToDestPositionHook;
	DetoursHook<tMoveToDirection>* setMoveToDirectionHook;
	DetoursHook<tProcess>* processHook;
	DetoursHook<tCheckPacket>* checkPacketHook;
	DetoursHook<tSendAttackPacket>* sendAttackPacketHook;

	//Memory patches
	CMemoryPatch* graphicPatch;

	//Pointers
	void* recvAddr;
	void* sendAddr;
	void* sendSequenceAddr;
	void* getEtherPackAddr;
	void* statePacketAddr;
	void** netClassPointer;
	void** pythonPlayerPointer;
	void** chrMgrClassPointer;
	void* moveToDestAddr;
	void* backgroundCheckAdvancingAddr;
	void* instanceCheckAdvancingAddr;
	void* traceFFuncAddr;
	void* tracenFFuncAddr;
	void* moveToDirectionAddr;
	void* processAddr;
	void* checkPacketAddr;
	void* sendAttackPacketAddr;
	void* skipGraphicsAddr;

	tLocalToGlobalPosition	localToGlobalFunc;
	tGlobalToLocalPosition	globalToLocalFunc;
	//tSendAttackPacket		sendAttackPacketFunc;
	tGetInstancePointer		getInstanceFunc;
	tSendUseSkillBySlot		sendUseSkillBySlotFunc;
	tPeek					peekFunc;

	ClassPointer* pythonNetwork;
	ClassPointer* pythonChrMgr;
	ClassPointer* pythonPlayer;
	ClassPointer networkStream; //Used by Send
	ClassPointer getInstanceClassPtr;


	HMODULE hDll;
};

