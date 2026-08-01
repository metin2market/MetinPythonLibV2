#pragma once
#include <Windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <sstream> 
#include <chrono>
#include <ctime>
#include <math.h>
#include <stdarg.h>


#include "Log.h"

// Collision-map cache dir, relative to the dir holding eXLib.mix. Must stay inside a dir the
// deploy ships as payload -- anything else is on its legacy-cleanup list and gets wiped, which
// would strip the shipped .dat and force a re-derive from the client on every run. The leading
// underscore matches _logs\/_reports\: it marks the dir as ours at a glance in a game dir full
// of the client's own folders.
#define SUBPATH_MAPS "_resources\\Maps\\"

typedef void (__stdcall *tTimerFunction)();
typedef std::chrono::time_point<std::chrono::system_clock> tTimePoint;

struct TPixelPosition {
	float x, y, z;
};

bool getCurrentPathFromModule(HMODULE hMod, char* dllPath, int size);
void stripFileFromPath(char* dllPath, int size);
const char* getDllPath();
const char* getMapsPath();
void setDllPath(char* file);

// getDllPath() + suffix into out, always NUL-terminated. getDllPath() already ends in a backslash,
// so suffix starts with the name, not a separator. False means it didn't fit and out holds a
// truncated path -- callers that only log it can ignore that, callers that open it must not.
bool buildPath(char* out, size_t n, const char* suffix);

//There are bugs here that migh crash the process
void setTimerFunction(tTimerFunction func,float sec);
void executeTimerFunctions();


inline void* getRelativeCallAddress(void* startCallAddr) {
	if (!startCallAddr) return 0; // walker build: dead/missing sigs resolve NULL; don't deref
	DWORD addr = (DWORD)startCallAddr;
	DWORD* offset = (DWORD*)(addr + 1);
	void* _final = (void*)(addr + *offset + 5);
	return  _final;
}


inline float distance(float x1, float y1, float x2, float y2) {
	return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}


struct Point {
	Point(int x=0, int y=0) : x(x), y(y){}
	int x, y;
};

struct fPoint {
	fPoint(float x, float y) : x(x), y(y) {}
	float x, y;
};

struct fPoint3D {
	float x, y, z;
};

inline fPoint getPointAtDistanceTimes(float x1, float y1, float x2, float y2, float multiplier) {
	fPoint vector(x2 - x1, y2 - y1);
	fPoint result(x1 + vector.x * multiplier, y1 + vector.y * multiplier);
	return result;
}

#pragma pack(push, 1)
template<class T>
int fillPacket(void* data, int size, T* _struct) {
	ZeroMemory(_struct, sizeof(T));
	int curr_size = std::min<int>(size, sizeof(T));
	memcpy(_struct, data, curr_size);
	return curr_size;
}
#pragma pack(pop)


inline bool checkPointBetween(float xStart, float yStart, float xCheckPoint, float yCheckPoint, float xEnd, float yEnd ) {
	fPoint vector(xEnd - xStart, yEnd - yStart);
	float kx = 0;
	float ky = 0;
	if (distance(xStart, yStart, xEnd, yEnd) < 1) {
		return false;
	}
	if (vector.x != 0){
		kx = (xCheckPoint - xStart) / vector.x;
	}
	if (vector.y != 0) {
		ky = (yCheckPoint - yStart) / vector.y;
	}

	if (abs(kx - ky) < 2 && kx<1) {
		return true;
	}

	return false;
}
