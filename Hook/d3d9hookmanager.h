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

#ifndef D3D9HOOKMANAGER_H
#define D3D9HOOKMANAGER_H

#include "../Common/stlincludes.h"
#include <windows.h>
#include <d3d9.h>
#include <boost/thread.hpp>

class D3D9CommonHook;
class RewriteHook;

//=============================================================================
/// <summary>
/// Manages Direct3D 9 and Direct3D 9Ex hooking and dispatches callbacks to the
/// appropriate hook.
///
/// WARNING: This object must be thread-safe as hooked callbacks are executed
/// in another thread than this object is created and deleted in.
/// </summary>
class D3D9HookManager
{
private: // Datatypes ---------------------------------------------------------
	struct D3D9HookData {
		RewriteHook *		SwapChainPresentHook; // SwapChain9 object
		D3D9CommonHook *	hook;
	};

private: // Static members ----------------------------------------------------
	static D3D9HookManager *	s_instance;

private: // Members -----------------------------------------------------------
	boost::recursive_mutex	m_hookMutex;
	bool					m_d3d9LibLoaded;
	bool					m_is9ExLib;
	bool					m_safeToHook;
	bool					m_isHooked;
	vector<D3D9HookData>	m_hooks; // Hook instances

	// Hooks
	RewriteHook *			m_DevicePresentHook; // Device9 object
	RewriteHook *			m_DeviceEndSceneHook;
	RewriteHook *			m_DeviceResetHook;
	RewriteHook *			m_DeviceReleaseHook;
	RewriteHook *			m_DeviceExPresentExHook; // Device9Ex object
	RewriteHook *			m_DeviceExResetExHook;

public: // Static methods -----------------------------------------------------
	inline static D3D9HookManager *getSingleton() {
		return s_instance;
	};

public: // Constructor/destructor ---------------------------------------------
	D3D9HookManager();
	virtual	~D3D9HookManager();

public: // Methods ------------------------------------------------------------
	void			attemptToHook();

	// Hooks
	HRESULT			DevicePresentHooked(
		IDirect3DDevice9 *device, const RECT *pSourceRect,
		const RECT *pDestRect, HWND hDestWindowOverride,
		const RGNDATA *pDirtyRegion);
	HRESULT			DeviceEndSceneHooked(IDirect3DDevice9 *device);
	HRESULT			DeviceResetHooked(
		IDirect3DDevice9 *device,
		D3DPRESENT_PARAMETERS *pPresentationParameters);
	ULONG			DeviceReleaseHooked(IUnknown *unknown);
	HRESULT			DeviceExPresentExHooked(
		IDirect3DDevice9Ex *deviceEx, const RECT *pSourceRect,
		const RECT *pDestRect, HWND hDestWindowOverride,
		const RGNDATA *pDirtyRegion, DWORD dwFlags);
	HRESULT			DeviceExResetExHooked(
		IDirect3DDevice9Ex *deviceEx,
		D3DPRESENT_PARAMETERS *pPresentationParameters,
		D3DDISPLAYMODEEX *pFullscreenDisplayMode);
	HRESULT			SwapChainPresentHooked(
		IDirect3DSwapChain9 *chain, const RECT *pSourceRect,
		const RECT *pDestRect, HWND hDestWindowOverride,
		const RGNDATA *pDirtyRegion, DWORD dwFlags);

private:
	void				loadLibIfPossible();
	void				displayDriverInfo(IDirect3D9 *d3d9);

	void				unhook();
	D3D9HookData *		findDataForDevice(IDirect3DDevice9 *device);
	D3D9CommonHook *	findHookForDevice(IDirect3DDevice9 *device);
};
//=============================================================================

#endif // D3D9HOOKMANAGER_H
