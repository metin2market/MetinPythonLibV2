#pragma once
#include <stdarg.h>

// Always compiled in, Release included: a silent lib makes "never injected" and "injected and
// failed" look identical from the outside. Output goes to <gamedir>\_logs\exlib-<session>.txt,
// alongside the bot's own python logs.
//
// Every level is always written -- there is no filter and no runtime switch. Severity is a tag you
// grep on after the fact, not a gate:
//   ERROR - a feature is now dead (a signature didn't resolve, a hook didn't install)
//   WARN  - degraded, or the caller misused the API but the run continues
//   INFO  - startup milestones and state changes
//   DEBUG - per-action detail
//   TRACE - per-packet / per-frame
enum LogLevel {
	LOG_LVL_ERROR = 0,
	LOG_LVL_WARN,
	LOG_LVL_INFO,
	LOG_LVL_DEBUG,
	LOG_LVL_TRACE,
};

void logWrite(int level, const char* fmt, ...);

#define LOG_ERROR(...) logWrite(LOG_LVL_ERROR, __VA_ARGS__)
#define LOG_WARN(...)  logWrite(LOG_LVL_WARN,  __VA_ARGS__)
#define LOG_INFO(...)  logWrite(LOG_LVL_INFO,  __VA_ARGS__)
#define LOG_DEBUG(...) logWrite(LOG_LVL_DEBUG, __VA_ARGS__)
#define LOG_TRACE(...) logWrite(LOG_LVL_TRACE, __VA_ARGS__)
