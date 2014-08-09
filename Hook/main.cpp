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
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"
#include <windows.h>
#include <boost/thread.hpp>

// Set to `1` to use Boost threads instead of Windows native threads for
// creating the main hook thread in 64-bit processes
#define USE_BOOST_MAIN_THREAD_64 1

void unloadLibraryThread()
{
	Sleep(100);

	// Used for debugging crashes as the library must be loaded in order to
	// read its debug symbols
	//Sleep(15000);

	FreeLibraryAndExitThread(HookMain::s_hinstDll, 0);
	// The above function never returns
}

/// <summary>
/// Main entry point
/// </summary>
extern "C"
	__declspec(dllexport)
	DWORD __cdecl startHook(void *param)
{
	int ret = 1;
	if(HookMain::s_instance != NULL) {
		// This method has already been called once before. As this should only
		// ever be called during hooking if we're called twice then it means
		// that the main application broke as we now have multiple references
		// to our DLL. This can happen on the next launch of the main
		// application after a crash.
		//
		// If we don't have a correct reference count to the DLL then when we
		// attempt to unload our hook won't be uninstalled automatically
		// preventing anything from modifying the file (E.g. patcher, compiler,
		// etc.). We must correct this my explicitly dereferencing the DLL.
		HookLog2(InterprocessLog::Warning,
			"Attempted to hook the same process multiple times");
		FreeLibrary(HookMain::s_hinstDll);
		return ret;
	}

#define CREATE_MAIN_OBJ_ON_STACK 1
#if CREATE_MAIN_OBJ_ON_STACK
	{ // Create on stack instead of heap
		HookMain hook;
		ret = hook.exec(param);
	}
#else
	HookMain *hook = new HookMain();
	ret = hook->exec(param);
	delete hook;
#endif // CREATE_MAIN_OBJ_ON_STACK

	// This function must return otherwise we will crash. As we want to unload
	// the DLL to allow Mishira to rehook at a later time, potentially with a
	// different DLL file (E.g. during development/debugging), we will create
	// yet another thread that will automatically free ourselves after a short
	// delay.
	boost::thread unloadThread(&unloadLibraryThread);

	return ret;
}

/// <summary>
/// Used to execute the main entry point from within `DllMain()`.
/// </summary>
#if USE_BOOST_MAIN_THREAD_64
void startHookInternal()
{
	startHook(NULL);
}
#else
DWORD __cdecl startHookInternal(HANDLE mainThread)
{
	// Wait for the `DllMain()` thread to exit before continuing to make sure
	// everything is initialized. This is most likely not required due to
	// automatic serialization of `DllMain()` but as it's not a documented
	// feature be extra safe.
	DWORD ret = STILL_ACTIVE;
	for(;;) {
		if(GetExitCodeThread(mainThread, &ret) == FALSE)
			ret = 0;
		if(ret != STILL_ACTIVE)
			break;
		WaitForSingleObject(mainThread, 100);
	}
	CloseHandle(mainThread);

	// Start the normal executable code
	return startHook(NULL);
}
#endif // USE_BOOST_MAIN_THREAD_64

/// <summary>
/// Dummy entry point used for debugging 64-bit builds.
/// </summary>
extern "C"
	__declspec(dllexport)
	DWORD __cdecl dummy(void *param)
{
	int breakPoint = 0;
	if(!breakPoint)
		breakPoint++;
	return 0;
}

/// <summary>
/// DLL entry point
/// </summary>
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// WARNING: Doing anything non-trivial in `DllMain()` is extremely
	// dangerous and should be avoided at all costs. As only one `DllMain()`
	// can be executing at any one time and creating or deleting a new thread
	// requires `DllMain()` itself we need to make sure that we never attempt
	// to do any synchronisation here.
	//
	// Useful resources:
	// http://blogs.msdn.com/b/oldnewthing/archive/2007/09/04/4731478.aspx
	// http://blogs.msdn.com/b/oldnewthing/archive/2004/01/28/63880.aspx

	switch(fdwReason) {
	default:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		return TRUE;
	case DLL_PROCESS_ATTACH:
		HookMain::s_hinstDll = hinstDLL;
#ifdef IS64
		// We need to create our own thread on 64-bit systems as we can't do it
		// remotely without having to directly write machine code to the
		// process.
		if(HookMain::s_instance == NULL) {
#if USE_BOOST_MAIN_THREAD_64
			boost::thread startThread(&startHookInternal);
#else
			HANDLE mainThread = OpenThread(
				THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
			if(mainThread == NULL)
				return FALSE;
			HANDLE thread = CreateThread(NULL, 0,
				(LPTHREAD_START_ROUTINE)startHookInternal, (LPVOID)mainThread,
				0, NULL);
			if(thread == NULL) {
				CloseHandle(mainThread);
				return FALSE;
			}
			CloseHandle(thread);
#endif // USE_BOOST_MAIN_THREAD_64
		}
#endif // IS64
		return TRUE;
	case DLL_PROCESS_DETACH:
		if(HookMain::s_instance != NULL) {
			// If our thread is still executing when we get here then there is
			// a high chance that we will crash as the OS is about to unload
			// the executable code from memory that we are still executing. Do
			// our best to try and end the thread ASAP but there isn't much
			// that we can do.
			HookMain::s_instance->exit(0);
		}
		return TRUE;
	}
	// Should never be reached
	return TRUE;
}
