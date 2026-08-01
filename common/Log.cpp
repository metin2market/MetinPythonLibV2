#include "Log.h"
#include "utils.h"
#include <Windows.h>
#include <stdio.h>

static FILE* g_logFile = 0;
static volatile LONG g_logOpenTried = 0;

static const char* levelName(int level)
{
	switch (level) {
	case LOG_LVL_ERROR: return "ERROR";
	case LOG_LVL_WARN:  return "WARN ";
	case LOG_LVL_INFO:  return "INFO ";
	case LOG_LVL_DEBUG: return "DEBUG";
	default:            return "TRACE";
	}
}

// One file per injection. Opened on first write and held open unbuffered: a crash still leaves
// every line on disk, without an fopen per line in the packet path.
static FILE* logFile()
{
	// The VEH crash logger and DLL_PROCESS_DETACH both log outside the single-threaded startup
	// window, so the open has to be claimed atomically or two threads race on fopen.
	if (InterlockedCompareExchange(&g_logOpenTried, 1, 0) != 0)
		return g_logFile;

	char dir[300];
	if (!buildPath(dir, sizeof(dir), "_logs"))
		return 0;
	CreateDirectoryA(dir, NULL);

	SYSTEMTIME t;
	GetLocalTime(&t);
	char suffix[64];
	_snprintf(suffix, sizeof(suffix), "_logs\\exlib-%04d-%02d-%02d_%02d-%02d-%02d.txt",
		t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	suffix[sizeof(suffix) - 1] = 0;

	char path[380];
	if (!buildPath(path, sizeof(path), suffix))
		return 0;

	g_logFile = fopen(path, "w");
	if (g_logFile)
		setvbuf(g_logFile, NULL, _IONBF, 0);
	return g_logFile;
}

void logWrite(int level, const char* fmt, ...)
{
	// Build the whole line first and write it once: the game thread, the packet hook and uBot's
	// python threads all log, and a single fwrite can't interleave mid-message.
	char line[1024];
	SYSTEMTIME t;
	GetLocalTime(&t);
	// The prefix is a fixed 21 chars, which is what keeps sizeof(line) - n - 2 below from
	// underflowing; the reserved 2 are the appended '\n' and the terminator.
	int n = _snprintf(line, sizeof(line) - 2, "[%02d:%02d:%02d.%03d] %s ",
		t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, levelName(level));
	if (n < 0)
		return;

	va_list args;
	va_start(args, fmt);
	int m = _vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
	va_end(args);
	n += (m < 0) ? (int)(sizeof(line) - n - 2) : m;   // _vsnprintf returns -1 when it truncates
	// Swapping _vsnprintf for C99's vsnprintf is a one-character edit that still compiles, but C99
	// returns the would-be length: without this clamp the two writes below run off the end.
	if (n > (int)sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n';
	line[n] = 0;

	FILE* f = logFile();
	if (f)
		fwrite(line, 1, n, f);
	else
		// A read-only dir or an AV lock. Nothing else catches this, so reach for DebugView --
		// only on this path, since it is a kernel transition and TRACE runs per packet.
		OutputDebugStringA(line);

	// Unconditional: hosts that allocate their own console (the pattern scanner, the packet
	// sniffer) are read from that console, not from the file. In an injected session with no
	// console this goes nowhere and costs a failed write.
	fwrite(line, 1, n, stdout);
	fflush(stdout);
}
