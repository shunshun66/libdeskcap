//*****************************************************************************
// Libdeskcap: A high-performance desktop capture library
//
// Copyright (C) 2014 Lucas Murray <lmurray@undefinedfire.com>
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//*****************************************************************************

#include "../Common/datatypes.h"
#include "../Common/stlhelpers.h"
#include "../Common/boostincludes.h"
#include <windows.h>
#include <psapi.h>

//=============================================================================
// Overview
/*

The helper communicates with clients over a very basic text-based protocol. On
immediate execution the helper writes "ready <protocolVers>" to the client and
waits for a "ready" reply. After the reply has been received then we know for
certain that the client will receive our future messages. The client issues
commands/messages to the helper and the helper replies with the appropriate
response. Each message ends with a newline. If the command given expects a
response that is over multiple lines then the response is terminated with a
"<command> end" message on the last line. All responses are prefixed by the
issuing command name in order to allow log messages to be mixed in the
response. And error message immediately terminates a command.

---------------------------------------
Available commands:

`ready`
Begin processing.

`quit`
Terminate the helper process.

`ping`
Immediately repond with a "pong" so the client knows we are not frozen.

`setHookDll <filename> <entryPoint> <fullFilePath>`
Sets the short filename (E.g. "mishirahook.dll"), entry point (E.g.
"startHook") and full path (E.g. "C:\Example\MishiraHook.dll") of the hook DLL
to inject. The short filename must be entirely lowercase and not contain any
spaces. We let the client set this as Qt's string and file processing library
is much more powerful than ours.

`hook <hwnd>`
Tests if the specified window should be hooked and if so do so. Note that just
because a process has been hooked doesn't mean that the hook found anything to
actually forward to the main application.

---------------------------------------

*/
//=============================================================================
// Helpers

string getWindowExeFilename(HWND hwnd, bool fullPath = false)
{
	if(!hwnd || !IsWindow(hwnd))
		return "";

	// Get process ID
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	if(processId == GetCurrentProcessId()) {
		// The process is ourself
		// TODO: Fast way to return our own filename?
	}

	// Open the process
	HANDLE process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if(process == NULL)
		return "";

	// MSDN recommends using `GetProcessImageFileName()` or
	// `QueryFullProcessImageName()` over `GetModuleFileNameEx()`
	const int strLen = 256;
	wchar_t strBuf[strLen];
	GetProcessImageFileName(process, strBuf, strLen);

	// Close the process
	CloseHandle(process);

	// We just want the ".exe" name without the path. Note that
	// `GetProcessImageFileName()` uses device form paths instead of drive
	// letters
	string str = utf_to_utf<char, wchar_t>(strBuf);
	if(fullPath)
		return str;
	vector<string> strList;
	boost::split(strList, str, boost::is_any_of("\\"));
	return strList.back();
}

string getWindowTitle(HWND hwnd)
{
	if(!hwnd || !IsWindow(hwnd))
		return "** Unknown **";

	const int strLen = 128;
	wchar_t strBuf[strLen];

	// Get window title text
	string title;
	strBuf[0] = TEXT('\0');
	if(GetWindowText(hwnd, strBuf, strLen) > 0)
		title = utf_to_utf<char, wchar_t>(strBuf);
	else
		title = "** No title **";

	// Return the string to use
	return title;
}

string getWindowDebugString(HWND hwnd)
{
	if(!hwnd || !IsWindow(hwnd))
		return stringf("** Unknown ** (ID: %s)", pointerToString(hwnd).data());

	const int strLen = 128;
	wchar_t strBuf[strLen];

	// Get window class
	string classStr;
	strBuf[0] = TEXT('\0');
	memset(strBuf, 0, sizeof(strBuf));
	if(GetClassName(hwnd, strBuf, strLen) > 0)
		classStr = utf_to_utf<char, wchar_t>(strBuf);
	else
		classStr = "** No class **";

	// Get window title text
	string title;
	strBuf[0] = TEXT('\0');
	if(GetWindowText(hwnd, strBuf, strLen) > 0)
		title = utf_to_utf<char, wchar_t>(strBuf);
	else
		title = "** No title **";

	// Get process ".exe" filename
	string filename = getWindowExeFilename(hwnd);

	// Return the string to use
	return stringf("[%s] %s [%s] (ID: %s)",
		filename.data(), title.data(), classStr.data(),
		pointerToString(hwnd).data());
}

//=============================================================================
// Individual command helpers

bool g_hookingInitialized = false;
bool g_hookingInitSuccess = false;
string g_hookDllShortName;
string g_hookDllEntryPoint;
string g_hookDllFullPath;
uint64_t g_hookDllEntryPointOffset = 0;

bool hookProcessInit()
{
	if(g_hookingInitialized)
		return g_hookingInitSuccess; // Already initialized
	g_hookingInitialized = true;

	// Get the "locally unique identifier" (LUID) of the "SE_DEBUG_NAME"
	// privilege so we can edit it
	LUID luid;
	if(LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid) == FALSE) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to get privilege LUID. Reason = %u", err)
			<< endl;
		return false;
	}

	// Access this process's access token so we can modify it
	HANDLE hToken = NULL;
	if(OpenProcessToken(GetCurrentProcess(),
		TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken) == FALSE)
	{
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to open process token. Reason = %u", err)
			<< endl;
		return false;
	}

	// Update our process's access token to include the "SE_DEBUG_NAME"
	// privilege so the OS thinks we are a debugger
	TOKEN_PRIVILEGES priv;
	priv.PrivilegeCount = 1;
	priv.Privileges[0].Luid = luid;
	priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if(AdjustTokenPrivileges(hToken, FALSE, &priv, sizeof(priv), NULL, NULL) ==
		FALSE)
	{
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to adjust process access token privileges. Reason = %u",
			err) << endl;
		goto hookProcessInitExit1;
	}

	// Successfully updated process access token
	CloseHandle(hToken);
	g_hookingInitSuccess = true;
	return true;

	// Error handling
hookProcessInitExit1:
	CloseHandle(hToken);
	return false;
}

bool hookProcess(DWORD processId, HWND hwnd)
{
	cout << stringf(
		"log notice Hooking process 0x%X which is \"%s\"",
		processId, getWindowExeFilename(hwnd, true).data()) << endl;

	// We use the `CreateRemoteThread()` and `LoadLibrary()` technique for
	// injecting code into another process with a second call to
	// `CreateRemoteThread()` in 32-bit processes to prevent adding dangerous
	// code to `DllMain()`. On 64-bit systems we need to do ugly stuff inside
	// `DllMain()`. See the following URL for more info:
	// http://www.codeproject.com/Articles/4610/Three-Ways-to-Inject-Your-Code-into-Another-Proces

	// The first time we attempt to hook something we need to notify the OS
	// that we are a debugger. We need to do this is we want to be able to hook
	// into system services.
	if(!hookProcessInit())
		return false;

	// Get the process handle with the least amount of access rights as
	// possible
	HANDLE proc = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
		PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, processId);
	if(proc == NULL) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to open process. Reason = %u", err) << endl;
		return false;
	}

	// Allocate enough memory in the target process for the filename of the DLL
	// that we are injecting
	wstring filename = utf_to_utf<wchar_t, char>(g_hookDllFullPath);
	int filenameBytes = (int)filename.size() * sizeof(wchar_t);
	void *alloc =
		VirtualAllocEx(proc, NULL, filenameBytes, MEM_COMMIT, PAGE_READWRITE);
	if(alloc == NULL) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to allocate memory in target process. Reason = %u",
			err) << endl;
		return false;
	}

	// Copy the filename to the target process's memory so that we can call
	// `LoadLibrary()` below.
	if(WriteProcessMemory(
		proc, alloc, filename.data(), filenameBytes, NULL) == FALSE)
	{
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to copy filename to target process. Reason = %u",
			err) << endl;
		return false;
	}

	// Get the memory address of the `LoadLibraryW()` function that will be
	// executed inside of the target process. We assume that modules are loaded
	// at the same memory address in all processes which is the case at the
	// time of writing. In order for this to work though our process needs to
	// have the same bitness as the target process as 32-bit and 64-bit modules
	// are loaded at different addresses.
	void *loadLibraryPtr = (void *)GetProcAddress(
		GetModuleHandle(TEXT("kernel32.dll")), "LoadLibraryW");
	if(loadLibraryPtr == NULL) {
		// Should never happen
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to get the address of LoadLibrary(). Reason = %u",
			err) << endl;
		goto hookProcessExit1;
	}

	// Create our own thread inside of the target process that will have the
	// sole purpose of calling `LoadLibrary()` and then terminating.
	HANDLE thread = CreateRemoteThread(
		proc, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryPtr, alloc, 0, NULL);
	if(thread == NULL) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to create first remote thread. Reason = %u",
			err) << endl;
		goto hookProcessExit1;
	}

	// Block until the remote thread terminates and then test its return code.
	// The return code will be the result of `LoadLibrary()` which is the
	// HMODULE address and not a normal exit code. On 64-bit systems the hook
	// DLL with automatically start its own thread. WARNING: The returned
	// address is only valid on 32-bit systems!
	DWORD ret = STILL_ACTIVE;
	for(;;) {
		if(GetExitCodeThread(thread, &ret) == FALSE)
			ret = 0; // Reuse error message because I'm lazy
		if(ret != STILL_ACTIVE)
			break;
		WaitForSingleObject(thread, 100);
	}
	if(ret == 0) {
		cout << stringf(
			"log warning Remote thread returned with exit code 0x%X", ret)
			<< endl;
		goto hookProcessExit2;
	}

	// Create a second thread inside of the target process on 32-bit systems
	// that will actually do our hooking. This thread will not terminate
	// anytime soon. This step is not needed on 64-bit systems as the hook DLL
	// automatically creates its own thread.
#ifdef IS32
	CloseHandle(thread);
	thread = CreateRemoteThread(proc, NULL, 0,
		(LPTHREAD_START_ROUTINE)((uint64_t)ret + g_hookDllEntryPointOffset),
		NULL, 0, NULL);
	if(thread == NULL) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to create second remote thread. Reason = %u",
			err) << endl;
		goto hookProcessExit2;
	}
#endif // IS32

	// Clean up by releasing the thread handle and the memory in the target
	// process that we allocated for the DLL's filename
	CloseHandle(thread);
	VirtualFreeEx(proc, alloc, 0, MEM_RELEASE);

	return true;

	// Error handling
hookProcessExit2:
	CloseHandle(thread);
hookProcessExit1:
	VirtualFreeEx(proc, alloc, 0, MEM_RELEASE);
	return false;
}

/// <summary>
/// Calculate the offset of the hook DLL's entry point so that we can call it
/// remotely without using `GetProcAddress()` within the remote process.
/// </summary>
void calcEntryPointOffset()
{
	// We don't need the offset on 64-bit systems as we don't remotely start
	// the main thread. If we attempt to load the 64-bit library it will
	// automatically hook into us that will result in a crash when we try to
	// unload it.
#ifdef IS32
	g_hookDllEntryPointOffset = 0;

	// Load the hooking library
	wstring filename = utf_to_utf<wchar_t, char>(g_hookDllFullPath);
	HMODULE lib = LoadLibrary(filename.data());
	if(lib == NULL)
		return;

	// Get the address and convert it to an offset
	void *addr = GetProcAddress(lib, g_hookDllEntryPoint.data());
	g_hookDllEntryPointOffset = (uint64_t)addr - (uint64_t)lib;

	// Unload the hooking library
	FreeLibrary(lib);

	cout << stringf(
		"log notice DLL entry point offset is %u", g_hookDllEntryPointOffset)
		<< endl;
#endif // IS32
}

/// <returns>0 = Hooked, 1 = Error, 2 = No 3D detected</returns>
int hookIfRequired(HWND hwnd)
{
	string debug = getWindowDebugString(hwnd);

	// Determine the process ID of the window
	DWORD processId = 0;
	GetWindowThreadProcessId(hwnd,  &processId);
	if(processId == 0) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to get process ID from window \"%s\". Reason = %u",
			debug.data(), err) << endl;
		return 1;
	}

	// Get the process handle
	HANDLE proc = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
	if(proc == NULL) {
		int err = GetLastError();
		cout << stringf(
			"log warning Failed to open process of window \"%s\". Reason = %u",
			debug.data(), err) << endl;
		return 1;
	}

	// Get list of modules
	HMODULE mods[1024];
	DWORD numModules = 0;
	if(EnumProcessModulesEx(proc, mods, sizeof(mods), &numModules,
		LIST_MODULES_ALL) == 0)
	{
		int err = GetLastError();
		// 299 = ERROR_PARTIAL_COPY = Attempting to read 64-bit process from
		//                            32-bit process
		CloseHandle(proc);
		cout << stringf(
			"log warning Failed to get list of modules for window \"%s\". Reason = %u",
			debug.data(), err) << endl;
		return 1;
	}
	numModules = numModules / sizeof(HMODULE);
	if(numModules == 0) {
		CloseHandle(proc);
		cout << stringf(
			"log warning Returned a list of zero modules for window \"%s\"",
			debug.data()) << endl;
		return 1;
	}

	// Get the filenames of all modules
	vector<string> modules;
	modules.reserve(numModules);
	for(uint i = 0; i < numModules; i++) {
		wchar_t wFilename[MAX_PATH];
		DWORD len = GetModuleFileNameEx(
			proc, mods[i], wFilename, sizeof(wFilename) / sizeof(wchar_t));
		string filename = utf_to_utf<char, wchar_t>(wFilename);
		boost::algorithm::to_lower(filename);
		modules.push_back(filename);
	}

	// Clean up
	CloseHandle(proc);

	// Test if there is a chance that the window contains a 3D scene or if it
	// already contains our hook
	bool alreadyHooked = false;
	int maybeDX = 0; // Highest detected DirectX version (x10)
	bool maybeGL = false;
	for(uint i = 0; i < modules.size(); i++) {
		const string &filename = modules.at(i);
		if(!alreadyHooked) {
			if(filename.find(g_hookDllShortName) != string::npos)
				alreadyHooked = true;
		}
		if(!maybeDX) {
			//if(filename.find("d3d8thk.dll") != string::npos)
			//	maybeDX = max(maybeDX, 80);
			//else
			if(filename.find("d3d9.dll") != string::npos)
				maybeDX = max(maybeDX, 90);
			else if(filename.find("d3d10.dll") != string::npos)
				maybeDX = max(maybeDX, 100);
			else if(filename.find("d3d10_1.dll") != string::npos)
				maybeDX = max(maybeDX, 101);
			else if(filename.find("d3d11.dll") != string::npos)
				maybeDX = max(maybeDX, 110);
			//else if(filename.find("dxgi.dll") != string::npos)
			//	maybeDX = max(maybeDX, 1);
		}
		if(!maybeGL) {
			if(filename.find("opengl32.dll") != string::npos)
				maybeGL = true;
			else if(filename.find("libGLESv2.dll") != string::npos)
				maybeGL = true;
			else if(filename.find("libEGL.dll") != string::npos)
				maybeGL = true;
		}
		if(alreadyHooked) {
			// No point in continuing if we know we're already hooked. We can't
			// stop if we find DX or GL modules as there is still a chance that
			// we've already hooked the process
			break;
		}
	}

	// Debug output
#if 0
	cout << "log notice " << debug << endl;
	//for(uint i = 0; i < modules.size(); i++)
	//	cout << "log notice .       " << modules.at(i) << endl;
	if(alreadyHooked)
		cout << "log notice .   Already hooked" << endl;
	else {
		if(maybeDX)
			cout << "log notice .   Maybe DirectX " << maybeDX << endl;
		if(maybeGL)
			cout << "log notice .   Maybe OpenGL" << endl;
	}
#endif // 0

	// Hook only if required to
	if(alreadyHooked)
		return 0;
	if(maybeDX || maybeGL) {
		if(hookProcess(processId, hwnd))
			return 0;
		return 1;
	}
	return 2;
}

//=============================================================================
// Command processing

/// <summary>
/// Process a single command.
/// </summary>
/// <returns>True if the main loop should continue to execute</returns>
bool processCommand(const vector<string> &cmd)
{
	if(cmd.size() <= 0) {
		// Empty command
		cout << "error unknownCmd" << endl;
		return true;
	}
	if(cmd.at(0).compare("quit") == 0) {
		return false; // Terminate the server
	} else if(cmd.at(0).compare("ready") == 0) {
		// Client is now listening to our messages
		//cout << "log notice Test message 1" << endl;
		//cout << "log warning Test message 2" << endl;
		//cout << "log critical Test message 3" << endl;
		return true;
	} else if(cmd.at(0).compare("ping") == 0) {
		cout << "ping pong" << endl;
		return true;
	} else if(cmd.at(0).compare("setHookDll") == 0) {
		bool success = false;
		if(cmd.size() >= 4) {
			g_hookDllShortName = cmd.at(1);
			g_hookDllEntryPoint = cmd.at(2);

			vector<string> tmp;
			tmp.reserve(cmd.size() - 3);
			for(uint i = 3; i < cmd.size(); i++)
				tmp.push_back(cmd.at(i));
			g_hookDllFullPath = boost::join(tmp, " ");

			cout << stringf(
				"log notice Set hook DLL to \"%s\", \"%s\" and \"%s\"",
				g_hookDllShortName.data(), g_hookDllEntryPoint.data(),
				g_hookDllFullPath.data()) << endl;
			calcEntryPointOffset();
		}
		cout << "setHookDll " << (success ? 1 : 0) << endl;
		return true;
	} else if(cmd.at(0).compare("hook") == 0) {
		void *ptr = NULL;
		sscanf_s(cmd.at(1).data(), "0x%p", &ptr);
		int hooked = hookIfRequired((HWND)ptr);
		cout << "hook " << hooked << endl;
		return true;
	}
	cout << "error unknownCmd" << endl;
	return true;
}

/// <summary>
/// Main entry point
/// </summary>
int main(int argc, char *argv[])
{
#if defined(IS32)
	int bits = 32;
#elif defined(IS64)
	int bits = 64;
#else
#error Unknown pointer size
#endif

	// Prevent users from executing this file directly by checking for a
	// specific argument
	if(argc != 2)
		return 0;
	string arg(argv[1]);
	if(arg.compare(0, 5, "start") != 0)
		return 0;

	cout << "ready " << HELPER_PROTOCOL_VERSION << " " << bits << endl;
	for(;;) {
		// Read next command from stdin
		string cmdStr;
		std::getline(cin, cmdStr);
		if(cin.fail()) {
			cout << "error readFail" << endl;
			break;
		}

		// Split string into separate words
		vector<string> cmd;
		boost::trim_if(cmdStr, boost::is_any_of("\t "));
		boost::split(cmd, cmdStr, boost::is_any_of("\t "));
		if(!processCommand(cmd))
			break;
	}
	cout << "eof" << endl;

	return 0;
}
