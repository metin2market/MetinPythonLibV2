#include "utils.h"
#include <map>
#include <iomanip>

static std::map<tTimePoint, tTimerFunction> timer_functions;
using namespace std::chrono;


char DLLPATH[256] = { 0 };
// Sized so the longest DLLPATH plus the suffix always fits, so setDllPath's buildPath cannot fail.
char MAP_PATH[sizeof(DLLPATH) + sizeof(SUBPATH_MAPS)] = { 0 };

bool getCurrentPathFromModule(HMODULE hMod, char* dllPath, int size)
{
	int len = GetModuleFileNameA(hMod, dllPath, size);
	if (!len) {
		return false;
	}
	stripFileFromPath(dllPath, len);

	return true;
}

void stripFileFromPath(char* dllPath, int size)
{
	for (int i = size; i >= 0; --i) {
		if (dllPath[i] == '\\')
			return;
		dllPath[i] = 0;
	}
	return;
}

const char* getDllPath()
{
	return DLLPATH;
}

const char* getMapsPath()
{
	return MAP_PATH;
}

void setDllPath(char* file)
{
	// strncpy bounds the READ, not just the write: `file` is the injector's DLLArgs::path[256], which
	// is only NUL-terminated if the injector bothered.
	strncpy(DLLPATH, file, sizeof(DLLPATH) - 1);
	DLLPATH[sizeof(DLLPATH) - 1] = 0;
	stripFileFromPath(DLLPATH, sizeof(DLLPATH) - 1);
	// Return discarded: MAP_PATH is sized so it cannot truncate, and this runs from DllMain, where
	// logging a failure would open the log file under the loader lock.
	buildPath(MAP_PATH, sizeof(MAP_PATH), SUBPATH_MAPS);
}

bool buildPath(char* out, size_t n, const char* suffix)
{
	if (!out || n == 0)
		return false;
	int written = _snprintf(out, n, "%s%s", getDllPath(), suffix);
	out[n - 1] = 0;   // _snprintf leaves the buffer unterminated on truncation and on an exact fit
	return written >= 0 && (size_t)written < n;
}

void setTimerFunction(tTimerFunction func,float sec)
{
	auto timer = std::chrono::system_clock::now();
	timer += std::chrono::milliseconds((int)(sec*1000));
	timer_functions.insert(std::pair<tTimePoint,tTimerFunction>(timer,func));
}



//Smoehow deleting directly inside the loop is giving segmentation fault
void executeTimerFunctions()
{
	if (timer_functions.empty())
		return;

	//LOG_TRACE("Going Trough");
	auto start = std::chrono::system_clock::now();
	auto it = timer_functions.begin();

	for (auto it = timer_functions.begin(); it != timer_functions.end();)
	{
		auto end = it->first;
		auto delta = end - start;
		if (delta.count() > 0) {
			return;
		}
		else {
			auto func = it->second;
			LOG_TRACE("Executing Timer Function");
			func();
			it = timer_functions.erase(it); //WHY ERROR?
			return;
		}
		++it;
	}
}

