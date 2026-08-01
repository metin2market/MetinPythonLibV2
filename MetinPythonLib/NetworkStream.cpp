#include "stdafx.h"
#include "NetworkStream.h"
#include "Memory.h"
#include "Player.h"
#include "InstanceManager.h"
#include "Background.h"

CNetworkStream::CNetworkStream() : lastPoint(0,0)
{
	m_blockAttackPackets = false;
	currentPhase = 0;
	lastPointIsStored = false;
	speed_Multiplier = 1.0;

	mainCharacterVID = 0;

	blockFishingPackets = 0;
	block_next_sequence = 0;

	recvDigMotionCallback = 0;
	shopRegisterCallback = 0;
	recvStartFishCallback = 0;
	chatCallback = 0;

	filterInboundOnlyIncluded = false;
	filterOutboundOnlyIncluded = false;
	printToConsole = false;

	netMod = 0;
}

CNetworkStream::~CNetworkStream()
{
}

void CNetworkStream::importPython()
{
	netMod = PyImport_ImportModule("net");

	if (!netMod) {
		LOG_ERROR("Could not import net module! Maybe init.py was not executed");
	}
	if (netMod) {
		LOG_INFO("Net module loaded");
	}
}

bool CNetworkStream::SendBattlePacket(DWORD vid, BYTE type)
{
	LOG_TRACE("Sending AttackPacket vid=%d",vid);
	CMemory& mem = CMemory::Instance();
	return mem.callSendAttackPacket(type, vid);
}

bool CNetworkStream::SendStatePacket(fPoint& pos, float rot, BYTE eFunc, BYTE uArg)
{
	CMemory& mem = CMemory::Instance();
	LOG_TRACE("Sending StatePacket to x=%f,y=%f,rot=%f,eFunc=%d,uArg=%d", pos.x,pos.y,rot,eFunc,uArg);
	return mem.callSendStatePacket(pos, rot, eFunc, uArg);
}

bool CNetworkStream::SendPacket(int size, void* buffer)
{
	CMemory& mem = CMemory::Instance();
	return mem.callSendPacket(size, buffer);
}

bool CNetworkStream::SendSequencePacket()
{
	CMemory& mem = CMemory::Instance();
	return mem.callSendSequence();
}

bool CNetworkStream::SendAddFlyTargetingPacket(DWORD dwTargetVID, float x, float y)
{
	SSend_AddFlyTargetingPacket packet;
	packet.dwTargetVID = dwTargetVID;
	packet.lX = (long)x;
	packet.lY = (long)y;
	LocalToGlobalPosition(packet.lX, packet.lY);

	if (SendPacket(sizeof(SSend_AddFlyTargetingPacket), &packet))
		return SendSequencePacket();
	return false;
}

bool CNetworkStream::SendShootPacket(BYTE uSkill)
{
	// Wire to the game's own alive SendShootPacket leaf (SENDSHOOT_FUNCTION, clean entry @ 0x...5230).
	// The raw SEND path is psw_tnt-protected/dead on this GF build; the client leaf builds the packet,
	// sends it, and runs SendSequence itself, so we just forward the skill id.
	CMemory& mem = CMemory::Instance();
	return mem.callSendShootPacket((DWORD)uSkill);
}

bool CNetworkStream::SendStartFishing(WORD direction)
{
	SSend_StartFishingPacket packet;
	packet.direction = direction;
	if (SendPacket(sizeof(SSend_StartFishingPacket), &packet))
		return SendSequencePacket();

	return false;
}

bool CNetworkStream::SendStopFishing(BYTE type, float timeLeft)
{
	SSend_StopFishingPacket packet;
	packet.type = type;
	packet.timeLeft = timeLeft;
	if (SendPacket(sizeof(SSend_StopFishingPacket), &packet))
		return SendSequencePacket();

	return false;
}

bool CNetworkStream::SendPickupItemPacket(DWORD vid)
{
	SSend_PickupPacket packet;
	packet.vid = vid;
	LOG_TRACE("Send Pickup Item vid=%d", vid);
	if (SendPacket(sizeof(SSend_PickupPacket), &packet))
		return SendSequencePacket();

	return false;
}

bool CNetworkStream::SendUseSkillPacket(DWORD dwSkillIndex, DWORD dwTargetVID)
{
	// Wire to the game's own alive SendUseSkillPacket leaf (SENDUSESKILL_PACKET_FUNCTION, clean entry).
	// Raw SEND is psw_tnt-protected/dead on this GF build; the leaf builds TPacketCGUseSkill (header 0x4C),
	// sends it, and runs SendSequence itself. Drives Skillbot's default "Cast instant" (by-index) path.
	CMemory& mem = CMemory::Instance();
	return mem.callSendUseSkillPacket(dwSkillIndex, dwTargetVID);
}

void CNetworkStream::SendUseSkillBySlot(DWORD dwSkillSlotIndex, DWORD dwTargetVID)
{
	CMemory& mem = CMemory::Instance();
	return mem.callSendUseSkillBySlot(dwSkillSlotIndex,dwTargetVID);
}

bool CNetworkStream::SendSyncPacket(std::vector<InstanceLocalPosition>& targetPositions)
{
	SSend_SyncPosition kPacketSync;
	kPacketSync.wSize = sizeof(kPacketSync) + sizeof(SSend_SyncPositionElement) * targetPositions.size();

	LOG_DEBUG("Sending sync packet size=%d", kPacketSync.wSize);
	if (!SendPacket(sizeof(SSend_SyncPosition), &kPacketSync)) {
		LOG_DEBUG("Fail to send Sync Packet");
		return false;
	}
	for(InstanceLocalPosition & pos : targetPositions){
		SSend_SyncPositionElement kSyncPos;
		kSyncPos.dwVID = pos.vid;
		kSyncPos.lX = (long)pos.x;
		kSyncPos.lY = (long)pos.y;
		LOG_DEBUG("SendSyncPacketElement: Local vid=%d, x=%d, y=%d", kSyncPos.dwVID, kSyncPos.lX, kSyncPos.lY);
		if (!LocalToGlobalPosition(kSyncPos.lX, kSyncPos.lY)) {
			LOG_DEBUG("Fail to transform local to global on Sync Packet");
			return false;
		}
		LOG_DEBUG("SendSyncPacketElement: Global vid=%d, x=%d, y=%d", kSyncPos.dwVID, kSyncPos.lX, kSyncPos.lY);
		if (!SendPacket(sizeof(SSend_SyncPositionElement), &kSyncPos))
		{
			LOG_DEBUG("Fail to send Sync Element Packet");
			return false;
		}
	}

	return SendSequencePacket();
}


bool CNetworkStream::__RecvPacket(int size, void* buffer)
{
	CMemory& mem = CMemory::Instance();
	bool val = mem.callRecvPacket(size, buffer);
	if (size > 0 && printToConsole && val) {
		BYTE* byte_buffer = (BYTE*)buffer;
		if (byte_buffer[0] != 0) {
			printPacket(0, byte_buffer, size, INBOUND);

		}
	}

	return val;
	/*if (return_value && size>0) {
		BYTE header = getPacketHeader(buffer, size);
		if (header == HEADER_GC_FISHING) {
			printPacket(0, (BYTE*)buffer, size, INBOUND);
		}
	}
	return return_value;*/
}

bool CNetworkStream::__SendPacket(int size, void* buffer)
{
	BYTE header = getPacketHeader(buffer, size);
	LOG_TRACE("Hook SendPacket called header = %d return size=%d", header, size);
	//PacketFilter
	if (size > 0 && printToConsole) {
		BYTE* byte_buffer = (BYTE*)buffer;
		if (byte_buffer[0] != 0) {
			printPacket(0, byte_buffer, size, OUTBOUND);

		}
	}

	switch (header) {
	case HEADER_CG_FISHING: { //SEQUENCE IS NOT SENT
		/*if (blockFishingPackets) {
			LOG_DEBUG("Blocking Fishing Packet");
			block_next_sequence = 1; //Also block sequence packet
			return 1;			//Block fishing packets
		}*/
		break;
	}
	}

	CMemory& mem = CMemory::Instance();
	return mem.callSendPacket(size, buffer);
}

bool CNetworkStream::__SendStatePacket(fPoint& pos, float rot, BYTE eFunc, BYTE uArg)
{
	LOG_DEBUG("__SendStatePacket StartPos X->%f, Y->%f, EndPos X->%f, Y->%f", lastPoint.x, lastPoint.y, pos.x, pos.y);
	CMemory& mem = CMemory::Instance();

	if (eFunc == CHAR_STATE_FUNC_WALK && speed_Multiplier > 1) {
		if (pos.x < 0 || pos.y < 0) {
			return false;
		}
		if (!lastPointIsStored) {
			lastPoint = pos;
			lastPointIsStored = true;
		}
		else {
			//LOG_DEBUG("starting X->%f, Y->%f, EndPos X->%f, Y->%f", lastPoint.x, lastPoint.y, pos.x, pos.y);
			if (distance(lastPoint.x, lastPoint.y, pos.x, pos.y) > 1) {

				//LOG_DEBUG("BOSTING");
				fPoint currPosition = { 0,0 };
				CPlayer& player = CPlayer::Instance();
				if (player.getLastMovementType() == MOVE_POSITION)
					currPosition = getPointAtDistanceTimes(lastPoint.x, lastPoint.y, pos.x, pos.y, speed_Multiplier / 2);
				else
					currPosition = getPointAtDistanceTimes(lastPoint.x, lastPoint.y, pos.x, pos.y, speed_Multiplier);


				//Check if it is destination position and if it is in between the points
				if (player.getLastMovementType() == MOVE_POSITION) {

					fPoint destPos = player.getLastDestPosition();
					if (checkPointBetween(lastPoint.x, lastPoint.y, destPos.x, destPos.y, currPosition.x, currPosition.y)) {
						//LOG_DEBUG("CharacterState Point is in between");
						currPosition.x = destPos.x;
						currPosition.y = destPos.y;
					}
				}
				float dist = distance(lastPoint.x, lastPoint.y, currPosition.x, currPosition.y);
				int steps = dist / MAX_TELEPORT_DIST;


				//LOG_DEBUG("StartPos X->%f, Y->%f, EndPos X->%f, Y->%f, dist %f", lastPoint.x, lastPoint.y, currPosition.x, currPosition.y, dist);

				//Fix large movement speed
				for (int step = 0; step < steps; step++) {
					fPoint this_pos = getPointAtDistanceTimes(lastPoint.x, lastPoint.y, currPosition.x, currPosition.y, (float)(step + 1) / (float)(steps + 1));
					pos.x = this_pos.x;
					pos.y = this_pos.y;

					//LOG_DEBUG("CharStateBoostedSteps X->%f, Y->%f, eFunc %d, rot %f, uArg %d, step %d, dist %f steps %f", pos.x, pos.y, 0, rot, uArg, step, distance(lastPoint.x, lastPoint.y, pos.x, pos.y), (float)(step + 1) / (float)(steps + 1));

					mem.callSendStatePacket(pos, rot, CHAR_STATE_FUNC_STOP, 0);
				}


				pos.x = currPosition.x;
				pos.y = currPosition.y;
				//LOG_DEBUG("CharStateBoosted X->%f, Y->%f, eFunc %d, rot %f, uArg %d, dist %f", pos.x, pos.y, 0, rot, uArg, distance(lastPoint.x, lastPoint.y, pos.x, pos.y));

				mem.callSendStatePacket(pos, rot, CHAR_STATE_FUNC_STOP, 0);

				//Reload Mobs
				pos.x += 200;
				pos.y -= 200;
				mem.callSendStatePacket(pos, rot, eFunc, uArg);

				pos.x -= 200;
				pos.y += 200;
				mem.callSendStatePacket(pos, rot, eFunc, uArg);

				pos.x = currPosition.x;
				pos.y = currPosition.y;

				player.setPixelPosition(pos);
				lastPoint = pos;
			}
		}


	}
	else {
		lastPointIsStored = false;
	}
	LOG_DEBUG("End __SendStatePacket X->%f, Y->%f, eFunc %d, rot %f, uArg %d", pos.x, pos.y, eFunc, rot, uArg);
	return mem.callSendStatePacket(pos, rot, eFunc, uArg);
}

bool CNetworkStream::__CheckPacket(BYTE * header)
{
	CMemory& mem = CMemory::Instance();
	bool val = mem.callCheckPacket(header);
	if (!val || header == 0)
		return val;

	// walker build hardening: serialize + sanitize every Python C-API call the packet
	// handlers below make (InstancesList dict + shop/chat/dig callbacks). On a map warp
	// the client floods us with clear + character add/del + phase packets; doing that
	// work without holding the GIL, or leaving a stray Python error set, corrupts the
	// interpreter and crashes inside python27.dll (0xc0000005) on world reload.
	PyGILState_STATE __gil = PyGILState_Ensure();

	LOG_TRACE("Hook CheckPacket header=%d", *header);

	switch (currentPhase) {
	case PHASE_GAME:
		val = RecvGamePhase(header);
		break;

	case PHASE_LOADING:
		val = RecvLoadingPhase(header);
		break;
	default: {
		switch (*header) {
		case HEADER_GC_PHASE: {
			SRcv_ChangePhasePacket phase;
			if (peekNetworkStream(sizeof(SRcv_ChangePhasePacket), &phase)) {
				if (phase.phase != 1 && phase.phase != 2)
					setPhase(phase);
			}
			else {
				LOG_DEBUG("Could not parse phase packet!");
			}
			break;
		}
		case HEADER_GC_CHARACTER_ADD: {
			SRcv_PlayerCreatePacket instance;
			if (peekNetworkStream(sizeof(SRcv_PlayerCreatePacket), &instance)) {
				GlobalToLocalPosition(instance.x, instance.y);
				CInstanceManager& mgr = CInstanceManager::Instance();
				mgr.appendNewInstance(instance);
			}
			else {
				LOG_DEBUG("Could not parse character add packet!");
			}
			break;
		}
		}

		}
	}

	// Drop any exception a callback/dict op left pending so it can't propagate into the
	// game's own Python on the next packet, then release the GIL we took above. This is the only
	// entry point into that whole subtree, so no callback below needs its own clear.
	if (PyErr_Occurred())
		PyErr_Clear();
	PyGILState_Release(__gil);
	return val;
}

bool CNetworkStream::__SendAttackPacket(BYTE type, DWORD vid)
{
	LOG_TRACE("__SendAttackPacket called ");
	CMemory& mem = CMemory::Instance();
	if (m_blockAttackPackets)
		return true;
	else
		return mem.callSendAttackPacket(type, vid);
}

bool CNetworkStream::__SendSequencePacket()
{
	LOG_TRACE("__SendSequence called ");
	if (block_next_sequence) {
		block_next_sequence = 0;
		LOG_DEBUG("Hook SendSequence called return val=%d", 1);
		return 1;
	}
	CMemory& mem = CMemory::Instance();
	return mem.callSendSequence();
}


int CNetworkStream::GetCurrentPhase()
{
    return currentPhase;
}

// walker build: phase packets aren't parsed (packet hooks off), so force the GAME phase
// once we detect we're in-game; this also loads the collision map for pathfinding.
void CNetworkStream::forceGamePhase()
{
	if (currentPhase == PHASE_GAME)
		return;
	currentPhase = PHASE_GAME;
	lastPointIsStored = false;
	LOG_INFO("Phase forced to GAME (walker build)");
	CBackground& background = CBackground::Instance();
	background.setCurrentCollisionMap();
}

void CNetworkStream::reloadGamePhase()
{
	// Called on world re-entry (teleport/map change). The phase is already GAME (packets are off so
	// it never left), so forceGamePhase() would early-return without reloading -- do the reload here
	// unconditionally so the walker's collision map matches the NEW map.
	currentPhase = PHASE_GAME;
	lastPointIsStored = false;
	LOG_INFO("World re-entry: reloading collision map (walker build)");
	CBackground& background = CBackground::Instance();
	background.setCurrentCollisionMap();
}

DWORD CNetworkStream::GetMainCharacterVID()
{
	if (netMod == 0) {
		LOG_DEBUG("Net Module has not been loaded!");
	}
	//return mainCharacterVID;
	long ret = 0;

	if (netMod == 0) {
		return 0;
	}

	// CallMethodRetLong owns the empty-tuple args internally -> the drain that WAS the root-cause crash
	// (GetMainActorVID doesn't exist on this GF build, so this ran every call) is now structurally impossible.
	CallMethodRetLong(netMod, "GetMainActorVID", &ret, "");
	return ret;
}

bool CNetworkStream::GlobalToLocalPosition(long& lx, long& ly)
{
	CMemory& mem = CMemory::Instance();
	if (mem.callGlobalToLocalPosition(lx, ly))
		return true;
	// Every packet-path caller discards this false and keeps the untouched GLOBAL coords -- ~100x too
	// large, so getCloseItemGround never matches pickRange and pickup silently stops. Only a dead
	// NETWORKCLASS_POINTER or GLOBALTOLOCAL_FUNCTION gets here, so one line is the whole story.
	static bool logged = false;
	if (!logged) {
		logged = true;
		LOG_ERROR("GlobalToLocalPosition is unavailable -- packet coordinates stay global (pickup and positions are wrong)");
	}
	return false;
}

bool CNetworkStream::LocalToGlobalPosition(LONG& rLocalX, LONG& rLocalY)
{
	CMemory& mem = CMemory::Instance();
	return mem.callLocalToGlobalPosition(rLocalX, rLocalY);
}

void CNetworkStream::callDigMotionCallback(DWORD target_player, DWORD target_vein, DWORD n)
{
	//call python callback
	LOG_DEBUG("Mining packet recived");
	if (recvDigMotionCallback && PyCallable_Check(recvDigMotionCallback)) {
		LOG_DEBUG("Calling python DigMotionCallback");
		PyObject* val = Py_BuildValue("(iii)", target_player, target_vein, n);
		PyObject* res = PyObject_CallObject(recvDigMotionCallback, val);
		Py_XDECREF(res);
		Py_XDECREF(val);
	}
}

void CNetworkStream::callStartFishCallback()
{
	//call python callback
	LOG_DEBUG("Start Fish recived");
	if (recvStartFishCallback && PyCallable_Check(recvStartFishCallback)) {
		LOG_DEBUG("Calling python StartFishCallback");
		PyObject* val = Py_BuildValue("()");
		PyObject* res = PyObject_CallObject(recvStartFishCallback, val);
		Py_XDECREF(res);
		Py_XDECREF(val);
	}
}

//THIS FUNCTION WILL CRASH IF IT HAS SELF ARGUMENT
bool CNetworkStream::setDigMotionCallback(PyObject* func)
{
	if (PyCallable_Check(func)) {
		LOG_DEBUG("RecvDigMotionCallback function set sucessfully");
		if(recvDigMotionCallback)
			Py_DECREF(recvDigMotionCallback);
		recvDigMotionCallback = func;
		Py_XINCREF(func); // own a ref: without it Python GC frees the stored callback -> use-after-free crash on reload
		return true;
	}
	else {
		LOG_WARN("RegisterNewDigMotionCallback argument is not a function");
		recvDigMotionCallback = 0;
		return false;
	}
}

void CNetworkStream::SetSpeedMultiplier(float val)
{
	speed_Multiplier = val;
}

void CNetworkStream::SetFishingPacketsBlock(bool val)
{
	blockFishingPackets = val;
}

bool CNetworkStream::setStartFishCallback(PyObject* func)
{
	if (PyCallable_Check(func)) {
		LOG_DEBUG("StartFishCallback function set sucessfully");
		if (recvStartFishCallback)
			Py_DECREF(recvStartFishCallback);
		recvStartFishCallback = func;
		Py_XINCREF(func); // own a ref (see setDigMotionCallback)
		return true;
	}
	else {
		LOG_WARN("RegisterStartFishCallback argument is not a function");
		recvStartFishCallback = 0;
		return false;
	}
}

bool CNetworkStream::setNewShopCallback(PyObject* func)
{
	if (!PyCallable_Check(func)) {
		LOG_WARN("RegisterNewShopCallback argument is not a function");
		return false;
	}

	if (shopRegisterCallback)
		Py_DECREF(shopRegisterCallback);
	shopRegisterCallback = func;
	Py_XINCREF(func); // own a ref (see setDigMotionCallback)

	LOG_DEBUG("RegisterNewShopCallback function set sucessfully");

	return true;
}

void CNetworkStream::callNewInstanceShop(DWORD player)
{
	if (shopRegisterCallback && PyCallable_Check(shopRegisterCallback)) {
		LOG_DEBUG("Calling python RegisterShopCallback");
		PyObject* val = Py_BuildValue("(i)", player);
		PyObject* res = PyObject_CallObject(shopRegisterCallback, val);
		Py_XDECREF(res);
		Py_XDECREF(val);
	}
}

bool CNetworkStream::setChatCallback(PyObject* func)
{
	if (!PyCallable_Check(func)) {
		LOG_WARN("ChatCallback argument is not a function");
		return false;
	}

	if (chatCallback)
		Py_DECREF(chatCallback);
	chatCallback = func;
	Py_XINCREF(func); // own a ref (see setDigMotionCallback)

	LOG_DEBUG("ChatCallback function set sucessfully");

	return true;
}

void CNetworkStream::callRecvChatCallback(DWORD vid, const char* msg, BYTE type, BYTE empire, const char* locale)
{
	if (chatCallback && PyCallable_Check(chatCallback)) {
		LOG_DEBUG("Calling python chatCallback message=%s,locale=%s",msg,locale);
		PyObject* val = Py_BuildValue("iiiss", vid,type,empire,msg,locale);
		PyObject* res = PyObject_CallObject(chatCallback, val);
		Py_XDECREF(res);
		Py_XDECREF(val);
	}
}

bool CNetworkStream::setRecvAddGrndItemCallback(PyObject* func)
{
	if (!PyCallable_Check(func)) {
		LOG_WARN("RecvAddGrndItemCallback argument is not a function");
		return false;
	}

	if (recvAddGrndItemCallback)
		Py_DECREF(recvAddGrndItemCallback);
	recvAddGrndItemCallback = func;
	Py_XINCREF(func); // own a ref (see setDigMotionCallback)

	LOG_DEBUG("RecvAddGrndItemCallback function set sucessfully");

	return true;
}

bool CNetworkStream::setRecvChangeOwnershipGrndItemCallback(PyObject* func)
{
	if (!PyCallable_Check(func)) {
		LOG_WARN("RecvChangeOwnershipGrndItemCallback argument is not a function");
		return false;
	}

	if (recvChangeOwnershipGrndItemCallback)
		Py_DECREF(recvChangeOwnershipGrndItemCallback);
	recvChangeOwnershipGrndItemCallback = func;
	Py_XINCREF(func); // own a ref (see setDigMotionCallback)

	LOG_DEBUG("RecvChangeOwnershipGrndItemCallback function set sucessfully");

	return true;
}

bool CNetworkStream::setRecvDelGrndItemCallback(PyObject* func)
{
	if (!PyCallable_Check(func)) {
		LOG_WARN("RecvDelGrndItemCallback argument is not a function");
		return false;
	}

	if (recvDelGrndItemCallback)
		Py_DECREF(recvDelGrndItemCallback);
	recvDelGrndItemCallback = func;
	Py_XINCREF(func); // own a ref (see setDigMotionCallback)

	LOG_DEBUG("RecvDelGrndItemCallback function set sucessfully");

	return true;
}

// The sniffer shares CApp::SetupConsole's console rather than owning one: LaunchPacketFilter /
// ClosePacketFilter are only exported under _DEBUG, and that is exactly the build where CApp has
// already allocated the console and freopen'd stdout/stderr/stdin onto it. Calling FreeConsole here
// would tear those out from under every other logger in the process.
void CNetworkStream::openConsole()
{
	filterInboundOnlyIncluded = false;
	filterOutboundOnlyIncluded = false;
	clearPacketFilter(OUTBOUND);
	clearPacketFilter(INBOUND);
	SetConsoleTitle("PACKET SNIFFER");
}

void CNetworkStream::closeConsole()
{
	filterInboundOnlyIncluded = false;
	filterOutboundOnlyIncluded = false;
	stopFilterPacket();
	clearPacketFilter(OUTBOUND);
	clearPacketFilter(INBOUND);
}

void CNetworkStream::startFilterPacket()
{
	printToConsole = true;
}

void CNetworkStream::stopFilterPacket()
{
	printToConsole = false;
}

void CNetworkStream::removeHeaderFilter(BYTE header, PACKET_TYPE t)
{
	if (t == INBOUND)
		inbound_header_filter.erase(header);
	else
		outbound_header_filter.erase(header);
}

void CNetworkStream::addHeaderFilter(BYTE header, PACKET_TYPE t)
{
	if (t == INBOUND)
		inbound_header_filter.insert(header);
	else
		outbound_header_filter.insert(header);
}

void CNetworkStream::clearPacketFilter(PACKET_TYPE t)
{
	if (t == INBOUND)
		inbound_header_filter.clear();
	else
		outbound_header_filter.clear();
}

void CNetworkStream::setFilterMode(PACKET_TYPE t, bool val)
{
	if (t == INBOUND)
		filterInboundOnlyIncluded = val;
	else
		filterOutboundOnlyIncluded = val;
}

void CNetworkStream::printPacket(DWORD calling_function, BYTE* buffer, int size, bool type)
{
	if (size < 1) {
		return;
	}

	if (outbound_header_filter.find(buffer[0]) == outbound_header_filter.end() && filterOutboundOnlyIncluded && type == OUTBOUND) {
		return;
	}
	if (inbound_header_filter.find(buffer[0]) == inbound_header_filter.end() && filterInboundOnlyIncluded && type == INBOUND) {
		return;
	}

	if (outbound_header_filter.find(buffer[0]) != outbound_header_filter.end() && !filterOutboundOnlyIncluded && type == OUTBOUND) {
		return;
	}
	if (inbound_header_filter.find(buffer[0]) != inbound_header_filter.end() && !filterInboundOnlyIncluded && type == INBOUND) {
		return;
	}



	if (INBOUND == type) {
		printf("[INBOUND]\n");
	}
	else {
		printf("[OUTBOUND]\n");
	}
	printf("Header: %d\n", buffer[0]);
	printf("Size: %d\n", size);
	printf("Calling Address: %#x\n", calling_function);
	printf("Content-Bytes: ");
	for (int i = 1; i < size; i++) {
		printf("%#x ", buffer[i]);
	}
	printf("\n\n");
	fflush(stdout);
}

std::set<BYTE>* CNetworkStream::getPacketFilter(PACKET_TYPE t)
{
	if (t == INBOUND)
		return &inbound_header_filter;
	else
		return &outbound_header_filter;
}

void CNetworkStream::blockAttackPackets()
{
	m_blockAttackPackets = true;
}

void CNetworkStream::unblockAttackPackets()
{
	m_blockAttackPackets = false;
}

void CNetworkStream::setPhase(SRcv_ChangePhasePacket& phase) {
	if (phase.phase > 5)
		return;
	//printf("Phase Packet %d\n",phase.phase);
	//if (phase.phase == 1 || phase.phase == 10 || phase.phase == 2) //The server sends 2 strange packets values(1,10), that disrupt the fase check
	//	return;
	currentPhase = phase.phase;
	lastPointIsStored = false;
	LOG_INFO("Phased changed to: %d", currentPhase);
	CBackground& background = CBackground::Instance();
	CInstanceManager& mgr = CInstanceManager::Instance();
	//CPlayer& player = CPlayer::Instance
	
	if (phase.phase == PHASE_LOADING) {
		mgr.clearInstances();
		background.freeCurrentMap();
	}
	else if (phase.phase == PHASE_GAME) {
		background.setCurrentCollisionMap();
	}
}

void CNetworkStream::handleChatPacket(SRcv_ChatPacket& packet)
{
	if (packet.size > 1025) {
		LOG_WARN("Error chat packet recived is too large dwSize=%d",packet.size);
		return;
	}
	int startChatMsgIndex = sizeof(SRcv_ChatPacket);
	char line[1024 + 1 + sizeof(SRcv_ChatPacket)] = { 0 };
	char* locale = (char*)((int)line + sizeof(SRcv_ChatPacket) + 1);
	char* msg = (char*)((int)line + sizeof(SRcv_ChatPacket) + 4);
	if (!peekNetworkStream(packet.size, line)) {
		// line is still all zeroes, so firing the callback would report a real empty message from
		// this VID to python instead of a dropped packet.
		LOG_WARN("Could not peek chat message packet, dwSize=%d", packet.size);
		return;
	}

	callRecvChatCallback(packet.dwVID, msg, packet.type, packet.bEmpire,locale);

}


bool CNetworkStream::peekNetworkStream(int len, void* buffer)
{
	CMemory& mem = CMemory::Instance();
	return mem.callPeek(len, buffer);
}

int CNetworkStream::getFishingPacketSize(BYTE header, bool isRecive)
{
	switch (header) {
		case 0x0:
		case 0x1:
		case 0x3:
		case 0x4:
		case 0x5:
			return 8;
		case 0x2:
			return 0x14;
	}
	LOG_DEBUG("Fishing header unknown: %d", header);
	return 0;
}


BYTE CNetworkStream::getPacketHeader(void* buffer, int size)
{
	if (size >= 1) {
		return *(BYTE*)buffer;
	}
	return 0;
}

bool CNetworkStream::RecvGamePhase(BYTE* header)
{
	switch (*header) {
	case HEADER_GC_FISHING: {
		SRecvHeaderFishingPacket instancePacket;
		if (peekNetworkStream(sizeof(SRecvHeaderFishingPacket), &instancePacket)) {
			if (blockFishingPackets && instancePacket.fishingHeader !=5 ) {

				CMemory& mem = CMemory::Instance();
				BYTE tmp[0x21];
				if(!mem.callRecvPacket(getFishingPacketSize(instancePacket.fishingHeader), tmp)) {
					LOG_DEBUG("Could not block fishing packet!");
				}

				if (instancePacket.fishingHeader == 2) {
					callStartFishCallback();
				}
				return false;
			}
		}
		else {
			LOG_DEBUG("Could not parse fishing packet!");
		}
		break;
	}
	case HEADER_GC_CHAT: {
		SRcv_ChatPacket chatPacket;
		if (peekNetworkStream(sizeof(SRcv_ChatPacket), &chatPacket)) {
			handleChatPacket(chatPacket);
		}
		else {
			LOG_DEBUG("Could not parse chat packet!");
		}
		break;
	}
	case HEADER_GC_ITEM_GROUND_ADD: {
		SRcv_GroundItemAddPacket instance;
		if (peekNetworkStream(sizeof(SRcv_GroundItemAddPacket), &instance)) {
			GlobalToLocalPosition(instance.x, instance.y);
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.addItemGround(instance);
		}
		else {
			LOG_DEBUG("Could not parse item ground add packet!");
		}
		break;
	}
	case HEADER_GC_ITEM_GROUND_DEL: {
		SRcv_GroundItemDeletePacket instance;
		if (peekNetworkStream(sizeof(SRcv_GroundItemDeletePacket), &instance)) {
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.delItemGround(instance);
		}
		else {
			LOG_DEBUG("Could not parse item ground delete packet!");
		}
		break;
	}
	case HEADER_GC_CHARACTER_ADD: {
		SRcv_PlayerCreatePacket instance;
		if (peekNetworkStream(sizeof(SRcv_PlayerCreatePacket), &instance)) {
			GlobalToLocalPosition(instance.x, instance.y);
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.appendNewInstance(instance);
		}else {
			LOG_DEBUG("Could not parse character add packet!");
		}
		break;
	}
	case HEADER_GC_CHARACTER_MOVE: {

		SRcv_CharacterMovePacket instance;
		if (peekNetworkStream(sizeof(SRcv_CharacterMovePacket), &instance)) {
			GlobalToLocalPosition(instance.lX, instance.lY);
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.changeInstancePosition(instance);
		}
		else {
			LOG_DEBUG("Could not parse character move packet!");
		}
		break;
	}
	case HEADER_GC_CHARACTER_DEL: {

		SRcv_DeletePlayerPacket instance_;
		if (peekNetworkStream(sizeof(SRcv_DeletePlayerPacket), &instance_)) {
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.deleteInstance(instance_.dwVID);
		}
		else {
			LOG_DEBUG("Could not parse character delete packet!");
		}
		break;
	}
	case HEADER_GC_ITEM_OWNERSHIP: {
		SRcv_PacketOwnership instance;
		if (peekNetworkStream(sizeof(SRcv_PacketOwnership), &instance)) {
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.changeItemOwnership(instance);
		}
		else {
			LOG_DEBUG("Could not parse item ownership packet!");
		}
		break;
	}
	case HEADER_GC_PHASE: {
		SRcv_ChangePhasePacket phase;
		if (peekNetworkStream(sizeof(SRcv_ChangePhasePacket), &phase)) {
			if (phase.phase != 1 && phase.phase != 2)
				setPhase(phase);
		}
		else {
			LOG_DEBUG("Could not parse phase packet!");
		}
		break;
	}
	case HEADER_CG_DIG_MOTION: {
		SRcv_DigMotionPacket instance_;
		if (peekNetworkStream(sizeof(SRcv_DigMotionPacket), &instance_)) {
			callDigMotionCallback(instance_.vid, instance_.target_vid, instance_.count);
		}
		else {
			LOG_DEBUG("Could not parse dig motion packet!");
		}
		break;
	}

	case HEADER_GC_DEAD: {
		SRcvDeadPacket dead;
		if (peekNetworkStream(sizeof(SRcvDeadPacket), &dead)) {
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.changeInstanceIsDead(dead.vid, 1);
		}
		else {
			LOG_DEBUG("Could not parse dead packet!");
		}
		break;
	}

	// Private-shop sign (docs/shop-name.md). Store it (empty = shop closed); the scanner
	// pulls it via eXLib.GetShopSign when it reads the shop. Passive peek, no send.
	case HEADER_GC_SHOP_SIGN: {
		SRcv_ShopSignPacket sign;
		if (peekNetworkStream(sizeof(SRcv_ShopSignPacket), &sign)) {
			sign.szSign[sizeof(sign.szSign) - 1] = '\0'; // guard against a non-terminated field
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.setShopSign(sign.dwVID, sign.szSign);
		}
		else {
			LOG_DEBUG("Could not parse shop-sign packet!");
		}
		break;
	}
	}

	return true;
}


bool CNetworkStream::RecvLoadingPhase( BYTE* header)
{
	switch (*header) {
	case HEADER_GC_CHARACTER_ADD: {
		SRcv_PlayerCreatePacket instance;
		if (peekNetworkStream(sizeof(SRcv_PlayerCreatePacket), &instance)) {
			GlobalToLocalPosition(instance.x, instance.y);
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.appendNewInstance(instance);
		}
		else {
			LOG_DEBUG("Could not parse character add packet!");
		}
		break;
	}
	case HEADER_GC_MAIN_CHARACTER: {
		SRcv_MainCharacterPacket m;
		if (peekNetworkStream(sizeof(SRcv_MainCharacterPacket), &m)) {
			LOG_DEBUG("MAIN VID: %d", m.dwVID);
			mainCharacterVID = m.dwVID;
		}
		else {
			LOG_DEBUG("Could not parse main character packet!");
		}
		break;
	}
	case HEADER_GC_ITEM_OWNERSHIP: {
		SRcv_PacketOwnership instance;
		if (peekNetworkStream(sizeof(SRcv_PacketOwnership), &instance)) {
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.changeItemOwnership(instance);
		}
		else {
			LOG_DEBUG("Could not parse item ownership packet!");
		}
		break;
	}
	case HEADER_GC_ITEM_GROUND_ADD: {
		SRcv_GroundItemAddPacket instance;
		if (peekNetworkStream(sizeof(SRcv_GroundItemAddPacket), &instance)) {
			GlobalToLocalPosition(instance.x, instance.y);
			CInstanceManager& mgr = CInstanceManager::Instance();
			mgr.addItemGround(instance);
		}
		else {
			LOG_DEBUG("Could not parse item ground add packet!");
		}
		break;
	}
	case HEADER_GC_PHASE: {
		SRcv_ChangePhasePacket phase;
		if (peekNetworkStream(sizeof(SRcv_ChangePhasePacket), &phase)) {
			if (phase.phase != 1 && phase.phase != 2)
				setPhase(phase);
		}
		else {
			LOG_DEBUG("Could not parse phase packet!");
		}
		break;
	}
	}

	return true;
}
