#pragma once
// ---------------------------------------------------------------------------
// Walker+pathfinding build: server-comms STRIPPED.
// Original Communication.h pulled in libcurl + jsoncpp + websocketpp + asio,
// none of which are installed here and none of which uBot's walker/pathfinding
// features use. This stub keeps the exact public surface referenced by
// PythonModule.cpp / App.cpp / CAddressLoader.cpp so they compile unchanged,
// but every method is an inert no-op. (USE_BUILTIN_PATTERNS path never calls
// the server methods at runtime.)
// ---------------------------------------------------------------------------
#include "stdafx.h"
#include <string>
#include <map>
#include "Singleton.h"
#include "PythonUtils.h"
#include "../common/utils.h"

typedef void(*tGetCallback)(int id, std::string* buffer);

struct ComCallbackFunction {
	ComCallbackFunction(PyObject* pyFunc) : pyFunction(pyFunc) { isCFunc = false; }
	ComCallbackFunction(tGetCallback cFunc) : cFunction(cFunc) { isCFunc = true; }
	inline void ExecuteCallback(int id, std::string* buffer, bool decrefPy = true) {}
	void Cleanup() {}
	bool isCFunc;
	union {
		tGetCallback	cFunction;
		PyObject*		pyFunction;
	};
};

class CCommunication : public CSingleton<CCommunication>
{
public:
	CCommunication() {}
	~CCommunication() {}

	void Process() {}

	int GetRequest(std::string& url, ComCallbackFunction callback, int id = 0) { return -1; }
	int GetRequest(const char* url, ComCallbackFunction callback, int id = 0) { return -1; }

	int OpenWebsocket(const char* host, ComCallbackFunction callback) { return -1; }
	bool WebsocketSend(int id, const char* message) { return false; }
	bool CloseWebsocket(int id) { return false; }

	int MainServerSetAuthKey() { return 1; }
	int MainServerGetOffsets(std::map<int, DWORD>* bufferOffsets, const char* server = "GF") { return 0; }
	bool IsPremiumUser() { return true; }

	void clearMemoryCertificates() {}
};
