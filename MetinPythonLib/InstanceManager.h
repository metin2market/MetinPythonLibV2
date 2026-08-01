#pragma once
#include "stdafx.h"
#include "Singleton.h"
#include "defines.h"
#include "../common/utils.h"
#include "PythonUtils.h"
#include <map>
#include <unordered_map>
#include <string>

class CInstanceManager : public CSingleton<CInstanceManager>
{
public:
	CInstanceManager();
	~CInstanceManager();

	void importPython();
public:

	void changeInstancePosition(SRcv_CharacterMovePacket& packet_move);
	// placeholder=true marks a record synthesised from a move packet; a later real CHARACTER_ADD
	// overwrites it. A real add for an already-real vid is still ignored.
	void appendNewInstance(SRcv_PlayerCreatePacket& player, bool placeholder = false);
	void deleteInstance(DWORD vid);
	void changeInstanceIsDead(DWORD vid, BYTE isDead);
	void addItemGround(SRcv_GroundItemAddPacket& item);
	void delItemGround(SRcv_GroundItemDeletePacket& item);
	void clearInstances();

	//Sets ownerVID of items to -1 if it belongs to another person
	void changeItemOwnership(SRcv_PacketOwnership& ownership);


	bool getCharacterPosition(DWORD vid, fPoint3D* pos);
	void* getInstancePtr(DWORD vid);
	bool isInstanceDead(DWORD vid);
	// mob race/vnum captured from the character-add packet (0 if unknown) -> uBot maps it to
	// rank/level/category via mob_proto for the full auto-hunt attack filter.
	inline DWORD getInstanceRace(DWORD vid) { auto it = instances.find(vid); return it != instances.end() ? it->second.wRaceNum : 0; }
	inline PyObject* getVIDList() { return pyVIDList; };

	// Shop-sign store (docs/shop-name.md). The HEADER_GC_SHOP_SIGN packet arrives
	// passively when a shop enters view -- long before the scanner opens it -- so the
	// sign has to be buffered here and pulled on demand (eXLib.GetShopSign). An empty
	// sign clears the entry (the server sends one on shop close). Lifecycle mirrors
	// `instances`: erased on deleteInstance, wiped on clearInstances (map warp).
	void setShopSign(DWORD vid, const char* sign);
	std::string getShopSign(DWORD vid);

	//Pickup filter
	inline void clearFilter() { pickupFilter.clear(); };
	inline void addItemFilter(DWORD index) {LOG_DEBUG("Pickup Filter Add=%d", index);pickupFilter.insert(index);};
	inline void deleteItemFilter(DWORD index) {LOG_DEBUG("Pickup Filter Remove=%d", index);pickupFilter.erase(index);};
	inline void setModeFilter(bool val) { pickOnFilter = val; };
	bool getCloseItemGround(int x, int y, SGroundItem* buffer);
	DWORD getItemGrndID(DWORD vid);
	void setPickItemFirst(bool val);
	void setPickupRange(float range);
	void setIgnoreBlockedPath(bool val){this->ignoreBlockedPath = val;}

private:
	PyObject* pyVIDList;
	PyObject* chrMod;

	bool pickOnFilter;
	bool pickItemFirst;
	bool ignoreBlockedPath;
	float pickRange;

	std::set<DWORD> pickupFilter;
	std::map<DWORD, SGroundItem> groundItems;
	std::unordered_map<DWORD, SInstance> instances;
	std::unordered_map<DWORD, std::string> shopSigns;   // vid -> private-shop sign
};

