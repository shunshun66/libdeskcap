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

#include "rewritehook.h"
#include "hookmain.h"
#include "../Common/stlhelpers.h"
#include "../Common/interprocesslog.h"
#include <windows.h>

//=============================================================================
#if USE_MINHOOK

RewriteHook::RewriteHook(void *funcToHook, void *funcToJumpTo)
	: m_funcToHook(funcToHook)
	, m_funcTrampoline(NULL)
	, m_isHooked(false)
	, m_isHookable(false)
{
	MH_STATUS ret = MH_CreateHook(funcToHook, funcToJumpTo, &m_funcTrampoline);
	if(ret == MH_OK)
		m_isHookable = true;
	else {
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to create MinHook hook. Reason = %u",
			ret));
	}
}

RewriteHook::~RewriteHook()
{
	if(m_isHooked)
		uninstall();

	if(m_isHookable) {
		MH_STATUS ret = MH_RemoveHook(m_funcToHook);
		if(ret != MH_OK) {
			HookLog2(InterprocessLog::Warning,
				stringf("Failed to remove MinHook hook. Reason = %u",
				ret));
		}
	}
}

/// <summary>
/// Installs the hook.
/// </summary>
/// <returns>True if the hook is installed.</returns>
bool RewriteHook::install()
{
	return installUninstall(true);
}

/// <summary>
/// Uninstalls the hook.
/// </summary>
/// <returns>True if the hook is uninstalled.</returns>
bool RewriteHook::uninstall()
{
	return installUninstall(false);
}

bool RewriteHook::installUninstall(bool isInstall)
{
	if(!m_isHookable)
		return false;
	if(m_isHooked == isInstall)
		return true; // Already hooked

	if(isInstall) {
		MH_STATUS ret = MH_EnableHook(m_funcToHook);
		if(ret != MH_OK) {
			HookLog2(InterprocessLog::Warning,
				stringf("Failed to enable MinHook hook. Reason = %u",
				ret));
			return false;
		}
	} else {
		MH_STATUS ret = MH_DisableHook(m_funcToHook);
		if(ret != MH_OK) {
			HookLog2(InterprocessLog::Warning,
				stringf("Failed to disable MinHook hook. Reason = %u",
				ret));
			return false;
		}
	}

	m_isHooked = isInstall;
	return true;
}

//=============================================================================
#else // USE_MINHOOK
//=============================================================================

// Settings for when and how `VirtualProtect()` is called. This method is very
// dangerous so if any of these settings are changed make sure you do VERY good
// testing.
#define DO_VIRTUAL_PROTECT_ON_INIT 1
#define DO_BRUTE_FORCE_VIRTUAL_PROTECT 1
#define DO_REVERT_VIRTUAL_PROTECT 1

RewriteHook::RewriteHook(void *funcToHook, void *funcToJumpTo)
	: m_funcToHook(funcToHook)
	, m_funcToJumpTo(funcToJumpTo)
	, m_isHookable(true)
	, m_isHooked(false)
	//, m_oldCode() // Zeroed below
	//, m_newCode() // Zeroed below
	, m_codeSize(0)
	, m_protectRevertFailed(false)
	, m_flushFailed(false)
{
	memset(m_oldCode, 0, sizeof(m_oldCode));
	memset(m_newCode, 0, sizeof(m_newCode));
	generateCode();

	// Make the memory region writable
#if DO_VIRTUAL_PROTECT_ON_INIT
	DWORD prevProtect = 0;
	if(VirtualProtect(m_funcToHook, m_codeSize, PAGE_EXECUTE_READWRITE,
		&prevProtect) == FALSE)
	{
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to change memory protection. Reason = %u",
			err));
		m_isHookable = false;
	}
#endif // DO_VIRTUAL_PROTECT_ON_INIT
}

RewriteHook::~RewriteHook()
{
	if(m_isHooked)
		uninstall();
}

/// <summary>
/// Installs the hook.
/// </summary>
/// <returns>True if the hook is installed.</returns>
bool RewriteHook::install()
{
	return installUninstall(true);
}

/// <summary>
/// Uninstalls the hook.
/// </summary>
/// <returns>True if the hook is uninstalled.</returns>
bool RewriteHook::uninstall()
{
	return installUninstall(false);
}

bool RewriteHook::installUninstall(bool isInstall)
{
	if(!m_isHookable)
		return false; // The memory is write protected
	if(m_isHooked == isInstall)
		return true; // Already hooked

	// Make the memory region writable
#if DO_BRUTE_FORCE_VIRTUAL_PROTECT || DO_REVERT_VIRTUAL_PROTECT
	DWORD prevProtect = 0;
	if(VirtualProtect(m_funcToHook, m_codeSize, PAGE_EXECUTE_READWRITE,
		&prevProtect) == FALSE)
	{
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to change memory protection. Reason = %u",
			err));
		return false;
	}
#endif // DO_BRUTE_FORCE_VIRTUAL_PROTECT || DO_REVERT_VIRTUAL_PROTECT

	if(isInstall) {
		//HookLog(stringf("Installing hook %p", m_funcToHook));

		// Take a copy of the existing code
		memcpy(m_oldCode, m_funcToHook, m_codeSize);

		// Replace the existing code with our own code
		memcpy(m_funcToHook, m_newCode, m_codeSize);
	} else {
		//HookLog(stringf("Uninstalling hook %p", m_funcToHook));

		// Replace the existing code with the original code
		memcpy(m_funcToHook, m_oldCode, m_codeSize);
	}

	// Revert the memory region protection to make it look unmodified
#if DO_REVERT_VIRTUAL_PROTECT
	if(!m_protectRevertFailed) {
		// If this ever failed in the past then in order to prevent against
		// crashes never attempt more than once. This also prevents spamming
		// our log file with errors
		if(VirtualProtect(m_funcToHook, m_codeSize, prevProtect, &prevProtect)
			== FALSE)
		{
			m_protectRevertFailed = true;
			DWORD err = GetLastError();
			HookLog2(InterprocessLog::Warning,
				stringf("Failed to revert memory protection. Reason = %u",
				err));
		}
	}
#endif // DO_REVERT_VIRTUAL_PROTECT

	// Flush the instruction cache so that the processor doesn't execute the
	// old code.
	if(FlushInstructionCache(GetCurrentProcess(), m_funcToHook, m_codeSize)
		== FALSE)
	{
		if(!m_flushFailed) {
			// Only ever log the error once to prevent spamming the log
			DWORD err = GetLastError();
			HookLog2(InterprocessLog::Warning,
				stringf("Failed to flush instruction cache. Reason = %u",
				err));
		}
		m_flushFailed = true;
	}

	m_isHooked = isInstall;
	return true;
}

void RewriteHook::generateCode()
{
	// Determine which jumping code we should use. We always use the 32-bit
	// jump on 32-bit systems as it can address every possible memory address
	// due to wrapping. WARNING: Signed integer over/underflow is undefined and
	// optimizations can ruin the code if not done correctly! We DON'T do it
	// correctly below.
	bool use64BitJump = false;
#ifndef IS32
	int64_t relAddr64 =
		((int64_t)m_funcToJumpTo - (int64_t)m_funcToHook) -
		(int64_t)REL_JMP_CODE_SIZE;
	if(relAddr64 >= _I32_MIN && relAddr64 <= _I32_MAX)
		use64BitJump = true; // Can use 32-bit jump
#endif

	if(use64BitJump) {
		// Determine absolute address
		uint64_t absAddr = (uint64_t)m_funcToJumpTo;

		// Write code (See header for details)
		m_codeSize = ABS64_JMP_CODE_SIZE;
		m_newCode[0] = 0xFF;
		m_newCode[1] = 0x25;
		m_newCode[2] = 0x00;
		m_newCode[3] = 0x00;
		m_newCode[4] = 0x00;
		m_newCode[5] = 0x00;
		*((uint64_t *)&m_newCode[6]) = absAddr;
	} else {
		// Determine relative address
		int32_t relAddr =
			((int32_t)m_funcToJumpTo - (int32_t)m_funcToHook) -
			(int32_t)REL_JMP_CODE_SIZE;

		// Write code (See header for details)
		m_codeSize = REL_JMP_CODE_SIZE;
		m_newCode[0] = 0xE9;
		*((int32_t *)&m_newCode[1]) = relAddr;
	}
}

//=============================================================================
#endif // USE_MINHOOK
