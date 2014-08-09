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

#ifndef DXGIHOOKMANAGER_H
#define DXGIHOOKMANAGER_H

#include "../Common/stlincludes.h"
#include <windows.h>
#include <d3d10_1.h>
#include <boost/thread.hpp>

class DXGICommonHook;
class RewriteHook;

//=============================================================================
/// <summary>
/// Manages all DXGI-based (DirectX 10 and 11) hooking and dispatches callbacks
/// to the appropriate hook.
///
/// WARNING: This object must be thread-safe as hooked callbacks are executed
/// in another thread than this object is created and deleted in.
/// </summary>
class DXGIHookManager
{
private: // Static members ----------------------------------------------------
	static DXGIHookManager *	s_instance;

private: // Members -----------------------------------------------------------
	boost::recursive_mutex		m_hookMutex;
	bool						m_dxgiLibLoaded;
	bool						m_safeToHook;
	bool						m_isHooked;
	vector<DXGICommonHook *>	m_hooks; // Hook instances

	// Hooks (Release hooks are unique to each object type)
	RewriteHook *			m_Device10ReleaseHook; // ID3D10Device object
	RewriteHook *			m_Device11ReleaseHook; // ID3D11Device object
	RewriteHook *			m_SwapChainReleaseHook; // IDXGISwapChain object
	RewriteHook *			m_SwapChainPresentHook;
	RewriteHook *			m_SwapChainResizeBuffersHook;

public: // Static methods -----------------------------------------------------
	inline static DXGIHookManager *getSingleton() {
		return s_instance;
	};

public: // Constructor/destructor ---------------------------------------------
	DXGIHookManager();
	virtual	~DXGIHookManager();

public: // Methods ------------------------------------------------------------
	void		attemptToHook();

	// Hooks
	ULONG		DeviceReleaseHooked(IUnknown *unknown, bool isDX11);
	ULONG		SwapChainReleaseHooked(IUnknown *unknown);
	HRESULT		SwapChainPresentHooked(
		IDXGISwapChain *chain, UINT SyncInterval, UINT Flags);
	HRESULT		SwapChainResizeBuffersHooked(
		IDXGISwapChain *chain, UINT BufferCount, UINT Width, UINT Height,
		DXGI_FORMAT NewFormat, UINT SwapChainFlags);

private:
	void		loadLibIfPossible();

	void				unhook();
	DXGICommonHook *	findHookForSwapChain(IDXGISwapChain *chain);
};
//=============================================================================

#endif // DXGIHOOKMANAGER_H
