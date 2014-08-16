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

#include "hookmain.h"
#include "d3d9hookmanager.h"
#include "d3dstatics.h"
#include "dxgihookmanager.h"
#include "glhookmanager.h"
#include "rewritehook.h"
#include "../Common/boostincludes.h"
#include "../Common/datatypes.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"
#include <d3d10_1.h>
#include <psapi.h>

// If opening our shared memory segment fails then we have no way to log the
// error. For debugging purposes save the error to a file on the filesystem.
#define DUMP_SHM_ERROR_REASON_TO_FILE 0
#if DUMP_SHM_ERROR_REASON_TO_FILE
#include <fstream>
#endif // DUMP_SHM_ERROR_REASON_TO_FILE

HookMain *HookMain::s_instance = NULL;
HINSTANCE HookMain::s_hinstDll;

// The class name of created dummy windows
const LPCWSTR DUMMY_WIN_CLASS = TEXT("MishiraDummyHookWindow");

HookMain::HookMain()
	: m_exitMainLoop(false)
	, m_exitCode(1)
	, m_shm()
	, m_log(NULL)
	, m_dummyDX10(NULL)
	, m_dummyDX10Ref(0)
	, m_exeFilename()

	// Performance timer
	//, m_startTick()
	//, m_lastTime()
	//, m_startTime()
	//, m_frequency()
	, m_timerMask(0)

	// Hook managers
	, m_d3d9Manager(NULL)
	, m_dxgiManager(NULL)
	, m_glManager(NULL)
{
	s_instance = this;

	// If our shared memory segment isn't valid we'll terminate early in the
	// `exec()` method below
	if(!m_shm.isValid())
		return;

	// Seed RNG
	srand((uint)time(NULL));

	// Fetch the interprocess log object
	m_log = m_shm.getInterprocessLog();
	//assert(m_interprocessLog != NULL);

	m_d3d9Manager = new D3D9HookManager();
	m_dxgiManager = new DXGIHookManager();
	m_glManager = new GLHookManager();
}

HookMain::~HookMain()
{
	// Destroy dummy DX10 context if it exists
	if(m_dummyDX10Ref > 0) {
		m_dummyDX10Ref = 1;
		derefDummyDX10Device();
	}

	delete m_d3d9Manager;
	delete m_dxgiManager;
	delete m_glManager;
	m_d3d9Manager = NULL;
	m_dxgiManager = NULL;
	m_glManager = NULL;

	s_instance = NULL;
}

int HookMain::exec(void *param)
{
	// Some processes such as Adobe Flash do not allow us to fetch our shared
	// memory segment for some reason. TODO: Find out why. It seems that the
	// process doesn't even have permission to write to a file on the
	// filesystem. Testing can be done by opening a Twitch channel fullscreen.
	if(!m_shm.isValid()) {
#if DUMP_SHM_ERROR_REASON_TO_FILE
		std::ofstream logFile;
		logFile.open("C:\\Users\\Lucas\\Desktop\\MishiraHookLog.txt");
		logFile << m_shm.getErrorReason();
		logFile.close();
#endif // DUMP_SHM_ERROR_REASON_TO_FILE
		return 1;
	}

	HookLog("Successfully hooked");

	// Begin performance timer
	beginPerformanceTimer();

	// Register dummy window class
	WNDCLASS wc;
	memset(&wc, 0, sizeof(wc));
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = DefWindowProc; // Default blackhole
	wc.hInstance = s_hinstDll;
	wc.hIcon = NULL;
	wc.hCursor = NULL;
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = DUMMY_WIN_CLASS;
	if(!RegisterClass(&wc)) {
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Critical,
			stringf("Failed to register dummy window class. Reason = %u",
			err));
		return 1;
	}

	// Get current process filename. Note that this forces linking in a Windows
	// library. As the main application already uses this library it should be
	// relatively fast to link in to every hook.
	HANDLE process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
	if(process != NULL) {
		// MSDN recommends using `GetProcessImageFileName()` or
		// `QueryFullProcessImageName()` over `GetModuleFileNameEx()`
		const int MAX_FILEPATH_LENGTH = 256;
		wchar_t strBuf[MAX_FILEPATH_LENGTH];
		GetProcessImageFileName(process, strBuf, MAX_FILEPATH_LENGTH);
		CloseHandle(process);

		// We just want the ".exe" name without the path. Note that
		// `GetProcessImageFileName()` uses device form paths instead of drive
		// letters
		string str = utf_to_utf<char, wchar_t>(strBuf);
		vector<string> strList;
		boost::split(strList, str, boost::is_any_of("\\"));
		m_exeFilename = strList.back();
	}

#if USE_MINHOOK
	MH_STATUS ret = MH_Initialize();
	if(ret != MH_OK) {
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to initialize MinHook. Reason = %u",
			ret));
		m_exitMainLoop = true;
	}
#endif // USE_MINHOOK

	// This thread's only purpose is to constantly attempt hooking and to make
	// sure that the library unloads itself when the main application quits.
	// The actual transfer of data is done in the hook callbacks.
	while(!m_exitMainLoop) {
		if(m_shm.getVideoFrequencyNum() != 0) // "0/anything" is zero
			attemptToHook();

		Sleep(500);

		if(!m_shm.getProcessRunning()) {
			// Main application is no longer running
			HookLog("Main application terminated, unhooking");
			exit(0);
		}

#define DEBUG_TERMINATE_WITH_AUTO_UNHOOK 0
#if DEBUG_TERMINATE_WITH_AUTO_UNHOOK
		static int iterations = 10;
		if(iterations-- <= 0)
			exit(0);
#endif // DEBUG_TERMINATE_WITH_AUTO_UNHOOK
	}

#if USE_MINHOOK
	ret = MH_Uninitialize();
	if(ret != MH_OK) {
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to uninitialize MinHook. Reason = %u",
			ret));
	}
#endif // USE_MINHOOK

	// Unregister dummy window class
	UnregisterClass(DUMMY_WIN_CLASS, s_hinstDll);

	HookLog("Terminating hook");
	return m_exitCode;
}

void HookMain::exit(int exitCode)
{
	m_exitMainLoop = true;
	m_exitCode = exitCode;
}

/// <summary>
/// Creates a dummy window and returns the HWND for it. It is up to the caller
/// to call `DestroyWindow()` if the result is non-NULL.
/// </summary>
HWND HookMain::createDummyWindow() const
{
	// Create dummy window and fetch device context
	HWND hwnd = CreateWindow(
		DUMMY_WIN_CLASS, DUMMY_WIN_CLASS,
		WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1, NULL, NULL,
		s_hinstDll, NULL);
	if(hwnd == NULL) {
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to create dummy window. Reason = %u",
			err));
	}
	return hwnd;
}

ID3D10Device *HookMain::refDummyDX10Device()
{
	m_dummyDX10Ref++;
	if(m_dummyDX10Ref >= 2)
		return m_dummyDX10;

	// TODO: This should really be the same as what's used in the main
	// application (It attempts to use 10.1 first) but this should still work
	// as we're not doing anything advanced with the devices (Just copying some
	// pixels).
	//
	// In order to support Vista Gold (No service packs) we must only link to
	// DirectX 10.0 but in order to support older hardware we require DirectX
	// 10.1 using the "9_3" feature level which was introduced in Vista SP2 and
	// Windows 7.
	//
	// The DirectX 10.0 `D3D10CreateDeviceAndSwapChain()` function
	// automatically attempts to create a DirectX 10.1 context with feature
	// level "10_1" if it is available otherwise it will attempt to create a
	// standard DirectX 10.0 context. If the hardware is not DirectX 10
	// compatible the function will return `DXGI_ERROR_UNSUPPORTED`. If we get
	// that error message then we know we are running old hardware and that we
	// should attempt to use the DirectX 10.1 API with the "9_3" feature level.
	//
	// We must also remember that it is possible that the application that we
	// are hooked into only has DirectX 10.1 loaded and not DirectX 10.

	// Link in DirectX 10 and 10.1 if required
	linkDX10Library(true);

	// Create device. Currently always uses the primary adapter
	UINT flags = D3D10_CREATE_DEVICE_SINGLETHREADED;
	HRESULT res = DXGI_ERROR_UNSUPPORTED;
#define FORCE_DIRECTX_10_1_API 0
#if !FORCE_DIRECTX_10_1_API
	if(D3D10CreateDeviceExists()) {
		res = D3D10CreateDevice_mishira(
			NULL, // _In_ IDXGIAdapter *pAdapter,
			D3D10_DRIVER_TYPE_HARDWARE, // _In_ D3D10_DRIVER_TYPE DriverType,
			NULL, // _In_ HMODULE Software,
			flags, // _In_ UINT Flags,
			D3D10_SDK_VERSION, // _In_ UINT SDKVersion,
			&m_dummyDX10); // _Out_ ID3D10Device **ppDevice
	}
#endif // FORCE_DIRECTX_10_1
	if(FAILED(res)) {
		if(res == DXGI_ERROR_UNSUPPORTED) {
			// We might be using DirectX 9 hardware. Attempt to create a
			// DirectX 10.1 Level 9 context.
			if(!D3D10CreateDevice1Exists()) {
				m_dummyDX10Ref--;
				return NULL;
			}

			// Create DirectX 10.1 device
			ID3D10Device1 *d3d101Dev = NULL;
			HRESULT res = D3D10CreateDevice1_mishira(
				NULL, // _In_ IDXGIAdapter *pAdapter,
				D3D10_DRIVER_TYPE_HARDWARE, // _In_ D3D10_DRIVER_TYPE DriverType,
				NULL, // _In_ HMODULE Software,
				flags, // _In_ UINT Flags,
				D3D10_FEATURE_LEVEL_9_3, // _In_ D3D10_FEATURE_LEVEL1 HardwareLevel,
				D3D10_1_SDK_VERSION, // _In_ UINT SDKVersion,
				&d3d101Dev); // _Out_ ID3D10Device1 **ppDevice
			if(FAILED(res)) {
				HookLog2(InterprocessLog::Warning, stringf(
					"Failed to create DirectX 10.1 device. Reason = 0x%x",
					res));
				m_dummyDX10Ref--;
				return NULL;
			}

			// Convert DirectX 10.1 device to DirectX 10
			res = d3d101Dev->QueryInterface(
				__uuidof(ID3D10Device), (void **)&m_dummyDX10);
			if(FAILED(res)) {
				HookLog2(InterprocessLog::Warning, stringf(
					"Failed to create DirectX 10 device from DirectX 10.1 device. "
					"Reason = 0x%x", res));
				m_dummyDX10Ref--;
				return NULL;
			}
			d3d101Dev->Release(); // `QueryInterface()` adds a reference
		} else {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to create DirectX 10 device. Reason = 0x%x", res));
			m_dummyDX10Ref--;
			return NULL;
		}
	}

	return m_dummyDX10;
}

void HookMain::derefDummyDX10Device()
{
	if(m_dummyDX10Ref <= 0)
		return; // Already dereferenced
	m_dummyDX10Ref--;
	if(m_dummyDX10Ref > 0)
		return; // Still referenced
	m_dummyDX10->Release();
	m_dummyDX10 = NULL;
}

/// <summary>
/// Begins the high-performance timer for the main loop. Also sets thread
/// affinity for timer stability if it is required.
/// </summary>
/// <returns>True if a timer was successfully created.</returns>
bool HookMain::beginPerformanceTimer()
{
	// This method is based on `Ogre::Timer`

	DWORD_PTR	procMask;
	DWORD_PTR	sysMask;

	// Get the current process core mask
	GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask);
	if(procMask == 0)
		procMask = 1; // Assume there is only one core available

	// Find the highest core that this process uses
	if(m_timerMask == 0) {
		m_timerMask = (DWORD_PTR)1 << (sizeof(m_timerMask) * 8 - 1);
		while((m_timerMask & procMask) == 0) {
			m_timerMask >>= 1;
			if(m_timerMask == 0) {
				//HookLog2(InterprocessLog::Warning,
				//	"Cannot determine process affinity");
				return false;
			}
		}
	}

#define SET_THREAD_AFFINITY 0
#if SET_THREAD_AFFINITY
	// Set current thread affinity to the highest core
	oldMask = SetThreadAffinityMask(GetCurrentThread(), m_timerMask);
#endif

	// Get the frequency of the performance counter
	QueryPerformanceFrequency(&m_frequency);
	if(m_frequency.QuadPart == 0) {
		// System doesn't has a performance timer
		//HookLog2(InterprocessLog::Warning,
		//	"No performance timer available on system");
		return false;
	}
	//HookLog(stringf("Performance timer frequency = %d Hz",
	//	m_frequency.QuadPart));
	if(m_frequency.QuadPart < 200) {
		// Performance timer has a resolution of less than 5ms
		//HookLog2(InterprocessLog::Warning,
		//	"Performance timer isn't performant enough to use");
		return false;
	}

	// Query the timer
	QueryPerformanceCounter(&m_startTime);
	m_startTick = GetTickCount();
	m_lastTime = 0;

	return true;
}

/// <summary>
/// Returns the number of microseconds that have passed since the main loop
/// began.
/// </summary>
uint64_t HookMain::getUsecSinceExec()
{
	// This method is based on `Ogre::Timer`

	LARGE_INTEGER	curTime;
	LONGLONG		timeSinceStart;

	if(m_timerMask == 0)
		return 0; // Haven't started timer yet

	// Query the timer
	QueryPerformanceCounter(&curTime);
	timeSinceStart = curTime.QuadPart - m_startTime.QuadPart;

	// Convert to milliseconds for `GetTickCount()` comparison
	unsigned long newTicks =
		(unsigned long)(1000LL * timeSinceStart / m_frequency.QuadPart);

	// Compensate for performance counter leaps (See Microsoft KB: Q274323)
	unsigned long check = GetTickCount() - m_startTick;
	signed long msecOff = (signed long)(newTicks - check);
	if(msecOff < -100 || msecOff > 100) {
		// Anomaly detected, compensate
		LONGLONG adjust = min(
			msecOff * m_frequency.QuadPart / 1000,
			timeSinceStart - m_lastTime);
		m_startTime.QuadPart += adjust;
		timeSinceStart -= adjust;
	}
	m_lastTime = timeSinceStart;

	// Convert to microseconds and return
	return (uint64_t)(1000000LL * timeSinceStart / m_frequency.QuadPart);
}

void HookMain::attemptToHook()
{
	m_d3d9Manager->attemptToHook();
	m_dxgiManager->attemptToHook();
	m_glManager->attemptToHook();
}
