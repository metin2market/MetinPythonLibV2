#include "stdafx.h"
#include "App.h"
#include "PythonModule.h"
#include "Memory.h"
#include "InstanceManager.h"
#include "Player.h"
#include "Background.h"
#include "NetworkStream.h"
#include "Communication.h"
#include <set>

HMODULE hDll = 0;

// -------------------------------------------------------------------------------------------------
// MODULE PINNING -- fixes the world-reload use-after-free crash.
// Crash evidence (exlib_crash.txt): the client faults in python27 getattr on a FREED python object
// (refcnt=0) whose memory was reused by the module-path string "OpenBot.Modules.Actions" -- i.e. a
// module object was over-decref'd to zero and freed while the client still resolves that dotted
// import on a world reload. Modules are meant to be immortal (they live in sys.modules for the whole
// process), so a stray decref freeing one is the bug. We give every module ONE permanent extra ref:
// a stray over-decref then can't drop it to zero, so the dangling-module-pointer getattr can't happen.
// Idempotent (a std::set skips already-pinned modules), so it's cheap to call on every world re-entry
// to also cover modules imported lazily after first load. Must run under the GIL (callers hold it).
static std::set<PyObject*> g_pinnedModules;
static void pinAllModules()
{
	PyObject* modules = PyImport_GetModuleDict();   // borrowed reference to sys.modules
	if (!modules || !PyDict_Check(modules))
		return;
	PyObject *key, *value;
	Py_ssize_t pos = 0;
	while (PyDict_Next(modules, &pos, &key, &value)) {   // increfing values does not mutate the dict
		if (value && PyModule_Check(value) && g_pinnedModules.find(value) == g_pinnedModules.end()) {
			Py_INCREF(value);                            // immortalize -- a stray decref can't free it
			g_pinnedModules.insert(value);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// GAME-WINDOW PIN -- the ROOT fix for the world-reload UAF (backstopped by exVehHandler in main.cpp).
// CPythonNetworkStream::SetPhaseWindow stores the game window BORROWED (no Py_INCREF); the client then
// calls methods on m_apoPhaseWnd[PHASE_WINDOW_GAME] every frame. uBot's object churn drops the window's
// last python reference while that borrowed pointer is still live -> use-after-free. We hold ONE ref to
// the current game window so it can never reach refcount 0 while active; when the client installs a new
// window we release the old pin (it frees normally) and pin the new one. Verified live via CE (26.1.11):
//   singleton = CMemory::getNetworkStream() (live __CheckPacket 'this'). The old hardcoded
//   exeBase+0x02EACCD4 global went STALE on GF 26.1.11 -> reads 0 -> pin no-oped -> keys died; see below.
//   m_apoPhaseWnd at singleton + 0x39C ; PHASE_WINDOW_GAME = 5 (slot's tp_name = "GameWindow")
//   -> game window = *(singleton + 0x39C + 5*4) = *(singleton + 0x3B0)
// Every read is range-checked + SEH-guarded, so a wrong offset on a future client just no-ops. The VEH is
// log-only and is NOT a safety net here -- the __except below is. Runs under the GIL from the process loop.
static bool exReadDwordSafe(DWORD addr, DWORD* out)
{
	// A fault here is the expected outcome for a stale offset and the __except below is its handler,
	// so keep the first-chance crash logger out of it (see g_exExpectedFault in App.h).
	exExpectedFaultEnter();
	bool ok;
	__try { *out = *(volatile DWORD*)addr; ok = true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
	exExpectedFaultLeave();
	return ok;
}
static PyObject* g_pinnedGameWnd = 0;
static void pinGameWindow()
{
	// Source the CPythonNetworkStream from eXLib's OWN live pointer -- the same 'this' the working packet
	// senders use: getNetworkStream() is captured by the __CheckPacket hook ("proven correct"), and once
	// in-game packets flow every frame so it's populated. The old hardcoded (GetModuleHandle(NULL)+0x02EACCD4)
	// global is STALE on GF 26.1.11 -- verified LIVE it reads 0 with the correct exe base (0x00320000), so
	// pinGameWindow silently no-oped, the game window was NEVER held alive, and the VEH recovery had to NULL
	// that freed window's getattrs every frame -> that ALSO killed I/V/C key handling (mouse buttons live in
	// separate windows, hence they survived). m_apoPhaseWnd is a CPythonNetworkStream member at +0x39C;
	// PHASE_WINDOW_GAME=5 -> game window = *(stream + 0x3B0) (offset verified live: a real GameWindow).
	DWORD singleton = (DWORD)CMemory::Instance().getNetworkStream();
	if (singleton < 0x00100000 || singleton >= 0x7F000000)
		return;                                          // stream not captured yet (no packet) -> safe no-op
	DWORD cur = 0;
	if (!exReadDwordSafe(singleton + 0x3B0, &cur))       // m_apoPhaseWnd[PHASE_WINDOW_GAME]
		return;
	if (cur == (DWORD)g_pinnedGameWnd)
		return;                                          // unchanged -> nothing to do
	if (cur == 0)
		return;                                          // slot cleared (transient during reload) -> KEEP the
	                                                     // current pin until a real new window is installed,
	                                                     // so cached copies of the old window can't dangle
	if (cur < 0x00100000 || cur >= 0x7F000000)           // validate it's a live PyObject before Py_INCREF
		return;
	DWORD refcnt = 0, obtype = 0;
	if (!exReadDwordSafe(cur, &refcnt) || refcnt == 0 || refcnt > 0x10000000)
		return;                                          // refcnt 0/absurd -> freed or not an object
	if (!exReadDwordSafe(cur + 4, &obtype) || obtype < 0x00100000 || obtype >= 0x7F000000)
		return;                                          // ob_type unreadable -> not a live object
	Py_XINCREF((PyObject*)cur);                          // pin the new game window (can't hit 0 while active)
	Py_XDECREF(g_pinnedGameWnd);                         // release the previous window's pin (frees if unused)
	g_pinnedGameWnd = (PyObject*)cur;
}


// Call a no-arg method on OpenBot.Modules.AutoHunting.instance. The client drops the python
// ScriptWindow.OnUpdate pump on world reload (teleport), so we drive the bot's Frame from this
// process-loop hook instead (it always runs). Read the module straight from sys.modules (borrowed) --
// NOT PyImport_ImportModule, which can run import machinery / re-execute during the phase transition.
static void callAutoHunting(const char* method) {
	PyObject* modules = PyImport_GetModuleDict();   // borrowed
	if (!modules) return;
	PyObject* mod = PyDict_GetItemString(modules, "OpenBot.Modules.AutoHunting");   // borrowed
	if (!mod) return;
	PyObject* inst = PyObject_GetAttrString(mod, "instance");   // new ref
	if (!inst) { PyErr_Clear(); return; }
	CallMethod(inst, method, "");   // self-contained, no manual refcount (see PythonUtils.h)
	Py_DECREF(inst);
	if (PyErr_Occurred()) PyErr_Clear();
}


bool CApp::__AppProcess(ClassPointer p) {
	CMemory& memory = CMemory::Instance();

	if (!passed) {
		passed = true;
		initMainThread();
		LOG_INFO("Process hook fired, python modules imported");
	}

	if (!Py_IsInitialized()) {
		static bool loggedGate = false;
		if (!loggedGate) {
			loggedGate = true;
			LOG_ERROR("Python is not initialized -- script.py will never run");
		}
		// This IS the client's own process hook, so the client's frame and the comms pump still have
		// to run when we can do nothing else: returning without callProcess freezes the game.
		CCommunication& c0 = CCommunication::Instance();
		c0.Process();
		return memory.callProcess(p);
	}

	// GIL GUARD -- fixes the world-reload heap/GC corruption crash. Everything below touches the Python
	// C-API, and this is the client's PROCESS-LOOP hook: it runs on the game thread, which does NOT hold
	// the GIL, while uBot runs background Python threads (OpenThreads: thread.start_new_thread x3).
	// Unguarded C-API calls from here race those threads on refcounts -> an object gets freed while still
	// linked in a GC generation -> corrupted gc_next -> the crash in update_refs (python27+0x31B16) on the
	// next collection, which a world reload reliably triggers. The packet path (CNetworkStream::
	// __CheckPacket) is guarded the same way. PyGILState_Ensure is recursive-safe, so this is fine even if
	// we are already called with the GIL held.
	PyGILState_STATE __gil = PyGILState_Ensure();

	pinGameWindow();   // ROOT fix: keep the borrowed m_apoPhaseWnd[GAME] window alive (see note above)

	CBackground& bck = CBackground::Instance();
	std::string curMap = bck.currentMapName();   // "" when not in the game world
	bool inGame = !curMap.empty();

	// The packet hooks are off, so the client's phase machinery never reaches uBot. The client's own map
	// name is the substitute edge: empty during a teleport/map load, non-empty again once the new map is
	// ready. We use it to run script.py once on first entry, and on EVERY subsequent world re-entry to
	// reload collision for the new map and replay the game-phase event to python -- otherwise
	// CURRENT_PHASE stays stale, the bots' Frame pump never resumes, and the walker keeps the previous
	// map's collision. This process-loop hook runs every frame regardless of UI state, which is what
	// makes it the only place that can recover any of it.
	if (!mainScriptExec) {
		if (inGame) {
			CNetworkStream& net = CNetworkStream::Instance();
			net.forceGamePhase();
			mainScriptExec = true;
			// script.py, NOT init.py: init.py is the bootstrap (sys.path + module aliases), already
			// run at module-init from PythonModule.cpp. script.py builds the hackbar and imports the
			// bot -- running init.py here loads nothing and reports no error.
			LOG_INFO("In-game on map '%s', running script.py", curMap.c_str());
			if (!executePythonFile("script.py"))
				LOG_ERROR("script.py failed (see syserr)");
			pinAllModules();                // immortalize every loaded module (see note above)
		}
	}
	else if (mainScriptExec && inGame && (!wasInGame || curMap != lastMap)) {
		// World re-entry: a portal warp changes the map name WITHOUT an empty gap (so the in-game
		// edge alone misses it -> we also fire on any map-name change), while a channel switch drops
		// out of game first (caught by the !wasInGame edge). Replaying PHASE_GAME refreshes
		// Hooks.CURRENT_PHASE and re-fires the callbacks, which is what resumes the bots' Frame pump.
		CNetworkStream& net = CNetworkStream::Instance();
		net.reloadGamePhase();
		PyObject* hooks = PyImport_ImportModule("Hooks");
		if (hooks) {
			CallMethod(hooks, "replayGamePhase", "");   // self-contained (see PythonUtils.h)
			Py_DECREF(hooks);
		}
		callAutoHunting("onReenter");   // AutoHunting lives in the package Hooks the replay can't reach
		pinAllModules();                // pin any modules imported lazily since first load (idempotent)
	}
	if (inGame)
		lastMap = curMap;
	wasInGame = inGame;

	// Drive AutoHunting's Frame once script.py is loaded (the client's OnUpdate pump is dropped for
	// ~1min after a world reload). NOT gated on inGame here -- the client map name can lag on landing,
	// and OnUpdate self-throttles, gates on State(STOPPED), and Frame's own _inGame() guard bails when
	// not in the world. Harmless double-drive when the client pump is also alive (throttle dedups).
	if (mainScriptExec)
		callAutoHunting("OnUpdate");

	PyGILState_Release(__gil);   // end GIL-guarded region (see note above)

	CCommunication& c = CCommunication::Instance();
	c.Process();

	return memory.callProcess(p);
}

void CApp::init() {
#ifdef _DEBUG
	SetupConsole();
#endif
	static CBackground bck = CBackground();
	static CCommunication coms; //Weird compile error with constructor
	static CInstanceManager mgr = CInstanceManager();
	static CPlayer pl = CPlayer();
	static CNetworkStream ns = CNetworkStream();
	static CMemory memory = CMemory();

	memory.setupPatterns(hDll);   // CAddressLoader logs the resolved/total count and every dead sig

	if (memory.setupProcessHook())
		LOG_INFO("Process hook installed, waiting for the first frame");
	else
		LOG_ERROR("Failed to install the process hook -- nothing further will run");
}

void CApp::initMainThread() {
	CMemory& memory = CMemory::Instance();
	initModule();
	// Ensure the Python GIL machinery exists before any hook that touches the Python
	// C-API from the packet path. eXLib reenters Python from CheckPacket while uBot
	// runs background Python threads; PyGILState_Ensure (used to guard those calls) is
	// only safe once the GIL has been created. Idempotent / no-op if already inited.
	PyEval_InitThreads();
	initPythonModules();
	memory.setupHooks();
}

void CApp::SetupConsole()
{
	AllocConsole();
	freopen("CONOUT$", "wb", stdout);
	freopen("CONOUT$", "wb", stderr);
	freopen("CONIN$", "rb", stdin);
	SetConsoleTitle("Debug Console");

}

void CApp::initPythonModules() {
	CBackground& bck = CBackground::Instance();
	CInstanceManager& mgr = CInstanceManager::Instance();
	CPlayer& pl = CPlayer::Instance();
	CNetworkStream& ns = CNetworkStream::Instance();
	
	bck.importPython();
	mgr.importPython();
	pl.importPython();
	ns.importPython();
}

void CApp::exit() {
	LOG_INFO("Shutting down");
	// Only tear down the console WE allocated -- in Release there is none, and closing the client's
	// own stdin/stdout/stderr breaks it. exit() is reachable mid-run from initModule's error path,
	// not just at teardown, so this has to be safe to hit while the client keeps running.
#ifdef _DEBUG
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
	FreeConsole();
#endif
}

void CApp::setSkipRenderer()
{
	CMemory& mem = CMemory::Instance();
	mem.setSkipRenderer();
}

void CApp::unsetSkipRenderer()
{
	CMemory& mem = CMemory::Instance();
	mem.unsetSkipRenderer();
}

CApp::CApp()
{

	mainScriptExec = false;
	passed = false;
	wasInGame = false;

}

CApp::~CApp()
{
	exit();
}
