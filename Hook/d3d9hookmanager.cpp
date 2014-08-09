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

#include "d3d9hookmanager.h"
#include "d3d9hook.h"
#include "d3dstatics.h"
#include "helpers.h"
#include "hookmain.h"
#include "rewritehook.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

//=============================================================================
// Function stubs

// IDirect3DDevice9::Present()
extern "C" static HRESULT STDMETHODCALLTYPE DevicePresentHook(
	IDirect3DDevice9 *device, const RECT *pSourceRect, const RECT *pDestRect,
	HWND hDestWindowOverride, const RGNDATA *pDirtyRegion)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DevicePresentHooked(
		device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// IDirect3DDevice9::EndScene()
extern "C" static HRESULT STDMETHODCALLTYPE DeviceEndSceneHook(
	IDirect3DDevice9 *device)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DeviceEndSceneHooked(device);
}

// IDirect3DDevice9::Reset()
extern "C" static HRESULT STDMETHODCALLTYPE DeviceResetHook(
	IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DeviceResetHooked(device, pPresentationParameters);
}

// IDirect3DDevice9::Release()
extern "C" static ULONG STDMETHODCALLTYPE DeviceReleaseHook(IUnknown *unknown)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DeviceReleaseHooked(unknown);
}

// IDirect3DDevice9Ex::PresentEx()
extern "C" static HRESULT STDMETHODCALLTYPE DeviceExPresentExHook(
	IDirect3DDevice9Ex *deviceEx, const RECT *pSourceRect,
	const RECT *pDestRect, HWND hDestWindowOverride,
	const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DeviceExPresentExHooked(
		deviceEx, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion,
		dwFlags);
}

// IDirect3DDevice9Ex::ResetEx()
extern "C" static HRESULT STDMETHODCALLTYPE DeviceExResetExHook(
	IDirect3DDevice9Ex *deviceEx,
	D3DPRESENT_PARAMETERS *pPresentationParameters,
	D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->DeviceExResetExHooked(
		deviceEx, pPresentationParameters, pFullscreenDisplayMode);
}

// IDirect3DSwapChain9::Present()
extern "C" static HRESULT STDMETHODCALLTYPE SwapChainPresentHook(
	IDirect3DSwapChain9 *chain, const RECT *pSourceRect, const RECT *pDestRect,
	HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
	D3D9HookManager *mgr = D3D9HookManager::getSingleton();
	return mgr->SwapChainPresentHooked(
		chain, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion,
		dwFlags);
}

//=============================================================================
// D3D9HookManager class

D3D9HookManager *D3D9HookManager::s_instance = NULL;

D3D9HookManager::D3D9HookManager()
	: m_hookMutex()
	, m_d3d9LibLoaded(false)
	, m_is9ExLib(false)
	, m_safeToHook(false)
	, m_isHooked(false)
	, m_hooks()

	// Hooks
	, m_DevicePresentHook(NULL)
	, m_DeviceEndSceneHook(NULL)
	, m_DeviceResetHook(NULL)
	, m_DeviceReleaseHook(NULL)
	, m_DeviceExPresentExHook(NULL)
	, m_DeviceExResetExHook(NULL)
{
	//assert(s_instance == NULL);
	s_instance = this;

	m_hooks.reserve(8);
}

D3D9HookManager::~D3D9HookManager()
{
	// Wait until all callbacks have completed as they will most likely be
	// executed in a different thread than the one we are being deleted from
	m_hookMutex.lock();

	// Unhook everything so our callbacks don't get called while we are
	// destructing
	if(m_DevicePresentHook != NULL)
		m_DevicePresentHook->uninstall();
	if(m_DeviceEndSceneHook != NULL)
		m_DeviceEndSceneHook->uninstall();
	if(m_DeviceResetHook != NULL)
		m_DeviceResetHook->uninstall();
	if(m_DeviceReleaseHook != NULL)
		m_DeviceReleaseHook->uninstall();
	if(m_DeviceExPresentExHook != NULL)
		m_DeviceExPresentExHook->uninstall();
	if(m_DeviceExResetExHook != NULL)
		m_DeviceExResetExHook->uninstall();
	for(uint i = 0; i < m_hooks.size(); i++) {
		D3D9HookData &data = m_hooks.at(i);
		if(data.SwapChainPresentHook != NULL)
			data.SwapChainPresentHook->uninstall();
	}

	// As another thread might have processed while we were uninstalling our
	// hooks temporary yield to make sure they have fully processed before
	// continuing
	m_hookMutex.unlock();
	Sleep(50);
	m_hookMutex.lock();

	// Delete all hooking contexts
	while(!m_hooks.empty()) {
		D3D9HookData &data = m_hooks.back();
		data.hook->release();
		m_hooks.pop_back();
	}

	// Delete hooks cleanly
	unhook();

	s_instance = NULL;
	m_hookMutex.unlock();
}

void D3D9HookManager::attemptToHook()
{
	if(!m_d3d9LibLoaded)
		loadLibIfPossible();
	if(!m_safeToHook)
		return;

	// Make sure we only ever hook once to prevent crashes from inter-thread
	// conflicts
	if(m_isHooked)
		return; // Already hooked
	m_isHooked = true;

	// Create dummy window
	HWND hwnd = HookMain::s_instance->createDummyWindow();
	if(hwnd == NULL)
		return;

	// Define presentation parameters for dummy device
	D3DPRESENT_PARAMETERS params;
	memset(&params, 0, sizeof(params));
	//params.BackBufferWidth = 0;
	//params.BackBufferHeight = 0;
	params.BackBufferFormat = D3DFMT_UNKNOWN;
	params.BackBufferCount = 1;
	params.MultiSampleType = D3DMULTISAMPLE_NONE;
	//params.MultiSampleQuality = 0;
	params.SwapEffect = D3DSWAPEFFECT_FLIP;
	params.hDeviceWindow = hwnd;
	params.Windowed = TRUE;
	//params.EnableAutoDepthStencil = FALSE;
	//params.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
	//params.Flags = 0;
	//params.FullScreen_RefreshRateInHz = 0;
	params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	// Create the appropriate Direct3D 9 context and create the hook handers
	bool hooksCreated = false;
	if(m_is9ExLib) {
		// Create a Direct3D 9Ex context
		IDirect3D9Ex *d3d9Ex = NULL;
		HRESULT res = Direct3DCreate9Ex_mishira(D3D_SDK_VERSION, &d3d9Ex);
		if(SUCCEEDED(res) && d3d9Ex != NULL) {
			//displayDriverInfo(d3d9Ex);

			IDirect3DDevice9Ex *deviceEx = NULL;
			res = d3d9Ex->CreateDeviceEx(
				D3DADAPTER_DEFAULT, // UINT Adapter
				D3DDEVTYPE_HAL, // D3DDEVTYPE DeviceType
				hwnd, // HWND hFocusWindow
				D3DCREATE_HARDWARE_VERTEXPROCESSING | // DWORD BehaviorFlags
				D3DCREATE_NOWINDOWCHANGES,
				&params, // D3DPRESENT_PARAMETERS *pPresentationParameters
				NULL, // D3DDISPLAYMODEEX *pFullscreenDisplayMode
				&deviceEx); // IDirect3DDevice9Ex **ppReturnedDeviceInterface
			if(SUCCEEDED(res)) {
				// Create hook handlers based on their position in the object's
				// virtual table
				m_DevicePresentHook = new RewriteHook(
					vtableLookup(deviceEx, 17), &DevicePresentHook);
				m_DeviceEndSceneHook = new RewriteHook(
					vtableLookup(deviceEx, 42), &DeviceEndSceneHook);
				m_DeviceResetHook = new RewriteHook(
					vtableLookup(deviceEx, 16), &DeviceResetHook);
				m_DeviceReleaseHook = new RewriteHook(
					vtableLookup(deviceEx, 2), &DeviceReleaseHook);
				m_DeviceExPresentExHook = new RewriteHook(
					vtableLookup(deviceEx, 121), &DeviceExPresentExHook);
				m_DeviceExResetExHook = new RewriteHook(
					vtableLookup(deviceEx, 132), &DeviceExResetExHook);
				hooksCreated = true;

				// Release dummy device
				deviceEx->Release();
			} else {
				HookLog2(InterprocessLog::Warning, stringf(
					"d3d9Ex->CreateDeviceEx() failed. Reason = %s",
					getD3D9ErrorCode(res).data()));
			}

			// Release Direct3D object
			d3d9Ex->Release();
		} else {
			HookLog2(InterprocessLog::Warning,
				stringf("Direct3DCreate9Ex() failed. Reason = %s",
				getD3D9ErrorCode(res).data()));
		}
	}
	if(!hooksCreated) {
		// Create a Direct3D 9 context
		IDirect3D9 *d3d9 = Direct3DCreate9_mishira(D3D_SDK_VERSION);
		if(d3d9 != NULL) {
			//displayDriverInfo(d3d9);

			IDirect3DDevice9 *device = NULL;
			HRESULT res = d3d9->CreateDevice(
				D3DADAPTER_DEFAULT, // UINT Adapter
				D3DDEVTYPE_HAL, // D3DDEVTYPE DeviceType
				hwnd, // HWND hFocusWindow
				D3DCREATE_HARDWARE_VERTEXPROCESSING | // DWORD BehaviorFlags
				D3DCREATE_NOWINDOWCHANGES,
				&params, // D3DPRESENT_PARAMETERS *pPresentationParameters
				&device); // IDirect3DDevice9 **ppReturnedDeviceInterface
			if(SUCCEEDED(res)) {
				// Create hook handlers based on their position in the object's
				// virtual table
				m_DevicePresentHook = new RewriteHook(
					vtableLookup(device, 17), &DevicePresentHook);
				m_DeviceEndSceneHook = new RewriteHook(
					vtableLookup(device, 42), &DeviceEndSceneHook);
				m_DeviceResetHook = new RewriteHook(
					vtableLookup(device, 16), &DeviceResetHook);
				m_DeviceReleaseHook = new RewriteHook(
					vtableLookup(device, 2), &DeviceReleaseHook);
				hooksCreated = true;

				// Release dummy device
				device->Release();
			} else {
				HookLog2(InterprocessLog::Warning, stringf(
					"d3d9->CreateDevice() failed. Reason = %s",
					getD3D9ErrorCode(res).data()));
			}

			// Release Direct3D object
			d3d9->Release();
		} else {
			HookLog2(InterprocessLog::Warning, "Direct3DCreate9() failed");
		}
	}

	// Destroy dummy window
	DestroyWindow(hwnd);

	// Install any hooks that we created
	if(m_DevicePresentHook != NULL)
		m_DevicePresentHook->install();
	if(m_DeviceEndSceneHook != NULL)
		m_DeviceEndSceneHook->install();
	if(m_DeviceResetHook != NULL)
		m_DeviceResetHook->install();
	if(m_DeviceReleaseHook != NULL)
		m_DeviceReleaseHook->install();
	if(m_DeviceExPresentExHook != NULL)
		m_DeviceExPresentExHook->install();
	if(m_DeviceExResetExHook != NULL)
		m_DeviceExResetExHook->install();
}

void D3D9HookManager::unhook()
{
	if(!m_isHooked)
		return; // Already unhooked
	HookLog("Destroying Direct3D 9 subsystem");

	// Uninstall and delete our global hook objects
	delete m_DevicePresentHook;
	delete m_DeviceEndSceneHook;
	delete m_DeviceResetHook;
	delete m_DeviceReleaseHook;
	delete m_DeviceExPresentExHook;
	delete m_DeviceExResetExHook;
	m_DevicePresentHook = NULL;
	m_DeviceEndSceneHook = NULL;
	m_DeviceResetHook = NULL;
	m_DeviceReleaseHook = NULL;
	m_DeviceExPresentExHook = NULL;
	m_DeviceExResetExHook = NULL;

	// Uninstall and delete our per-device hook objects if any exist
	for(uint i = 0; i < m_hooks.size(); i++) {
		D3D9HookData *data = &m_hooks.data()[i];
		if(data->SwapChainPresentHook == NULL)
			continue;
		delete data->SwapChainPresentHook;
		data->SwapChainPresentHook = NULL;
	}

	m_safeToHook = false; // No longer safe to hook
	m_d3d9LibLoaded = false; // Attempt to refetch function pointers

	//HookLog("Successfully destroyed Direct3D 9 subsystem");
	m_isHooked = false;
}

void D3D9HookManager::loadLibIfPossible()
{
	if(m_d3d9LibLoaded)
		return; // Already loaded and attempted to hook

	// Attempt to link with the base OpenGL library if it isn't already
	if(!linkD3D9Library(false))
		return; // Failed to link
	m_d3d9LibLoaded = true;
	// Application is using DirectX 9

	// Does the library that the application is using support Direct3D 9Ex?
	m_is9ExLib = Direct3DCreate9ExExists();
	if(m_is9ExLib)
		HookLog("Initialized Direct3D 9 subsystem with 9Ex support");
	else
		HookLog("Initialized Direct3D 9 subsystem");

	m_safeToHook = true;
}

void D3D9HookManager::displayDriverInfo(IDirect3D9 *d3d9)
{
	D3DADAPTER_IDENTIFIER9 ident;
	HRESULT res = d3d9->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, "Failed to get adapter identifier");
		return;
	}
	HookLog(stringf("D3D9 driver: %s", ident.Driver));
	HookLog(stringf("D3D9 description: %s", ident.Description));
	HookLog(stringf("D3D9 device name: %s", ident.DeviceName));
}

D3D9HookManager::D3D9HookData *D3D9HookManager::findDataForDevice(
	IDirect3DDevice9 *device)
{
	for(uint i = 0; i < m_hooks.size(); i++) {
		D3D9HookData *data = &m_hooks.data()[i];
		if(data->hook->getDevice() == device)
			return data;
	}
	return NULL;
}

D3D9CommonHook *D3D9HookManager::findHookForDevice(IDirect3DDevice9 *device)
{
	D3D9HookData *data = findDataForDevice(device);
	if(data == NULL)
		return NULL;
	return data->hook;
}

HRESULT D3D9HookManager::DevicePresentHooked(
	IDirect3DDevice9 *device, const RECT *pSourceRect,
	const RECT *pDestRect, HWND hDestWindowOverride,
	const RGNDATA *pDirtyRegion)
{
	//HookLog(stringf("IDirect3DDevice9::Present(%p)", device));
	m_hookMutex.lock();

	// Forward to the context handler
	D3D9CommonHook *hook = findHookForDevice(device);
	if(hook != NULL)
		hook->processBufferSwap();

	// Forward to the real function
	m_DevicePresentHook->uninstall();
	HRESULT ret = device->Present(
		pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
	m_DevicePresentHook->install();

	m_hookMutex.unlock();
	return ret;
}

HRESULT D3D9HookManager::DeviceEndSceneHooked(IDirect3DDevice9 *device)
{
	//HookLog(stringf("IDirect3DDevice9::EndScene(%p)", device));
	m_hookMutex.lock();

	// Every Direct3D 9 application must call `IDirect3DDevice9::EndScene()`
	// once per frame. As there are multiple `Present` methods where some of
	// them are not hooked yet we must detect new contexts using `EndScene`
	// instead.

	// Create a new `D3D9Hook` or `D3D9ExHook` instance for every unique device
	// so we can keep track of multiple contexts
	D3D9CommonHook *hook = findHookForDevice(device);
	if(hook == NULL) {
		// This is a brand new context! Track it
		//HookLog(stringf("IDirect3DDevice9::EndScene(%p)", device));

		// Is this a D3D9 or D3D9Ex device? Just because the library supports
		// D3D9Ex doesn't mean that the application actually created a D3D9Ex
		// device
		IDirect3DDevice9Ex *deviceEx = NULL;
		HRESULT res = device->QueryInterface(
			__uuidof(IDirect3DDevice9Ex), (void **)&deviceEx);
		if(FAILED(res))
			deviceEx = NULL;
		if(deviceEx != NULL)
			HookLog("Device is Direct3D 9Ex");

		// Get the HDC of the window that this device uses
		IDirect3DSwapChain9 *chain = NULL;
		uint numChains = device->GetNumberOfSwapChains();
		uint chainId = 0;
		if(numChains > 1)
			HookLog(stringf("Device has %d swap chains", numChains));
		for(; chainId < numChains; chainId++) {
			res = device->GetSwapChain(chainId, &chain);
			if(SUCCEEDED(res))
				break;
		}
		if(chain == NULL) {
			// Should never happen?
			goto endSceneFailed1;
		}
		D3DPRESENT_PARAMETERS params;
		res = chain->GetPresentParameters(&params);
		if(FAILED(res)) {
			// Should never happen?
			goto endSceneFailed2;
		}
		if(params.hDeviceWindow == NULL) {
			HookLog2(InterprocessLog::Warning, "HWND of D3D device is NULL");
			goto endSceneFailed2;
		}
		HDC hdc = GetDC(params.hDeviceWindow);

		// FIXME: Test if the process has created another swap device for the
		// same window before it released the old device. I think that the
		// Higan SNES emulator is an example of such a process.

		// Create the appropriate hook object, FIXME
		//if(deviceEx != NULL)
		//	hook = new D3D9ExHook(hdc, deviceEx, chainId);
		//else
		hook = new D3D9Hook(hdc, device, chainId);
		hook->initialize();

		// Hook the swap chain present for this device
		D3D9HookData data;
		data.hook = hook;
		data.SwapChainPresentHook = new RewriteHook(
			vtableLookup(chain, 3), &SwapChainPresentHook);
		m_hooks.push_back(data);
		data.SwapChainPresentHook->install();

		// Clean up
endSceneFailed2:
		chain->Release();
endSceneFailed1:
		if(deviceEx != NULL)
			deviceEx->Release();
	}

	// Forward to the real function
	m_DeviceEndSceneHook->uninstall();
	HRESULT ret = device->EndScene();
	m_DeviceEndSceneHook->install();

	m_hookMutex.unlock();
	return ret;
}

HRESULT D3D9HookManager::DeviceResetHooked(
	IDirect3DDevice9 *device,
	D3DPRESENT_PARAMETERS *pPresentationParameters)
{
	//HookLog(stringf("IDirect3DDevice9::Reset(%p)", device));
	m_hookMutex.lock();

	// Forward to the context handler (Part 1)
	D3D9CommonHook *hook = findHookForDevice(device);
	if(hook != NULL)
		hook->processResetBefore();

	// Forward to the real function
	m_DeviceResetHook->uninstall();
	HRESULT ret = device->Reset(pPresentationParameters);
	m_DeviceResetHook->install();

	// Forward to the context handler (Part 2)
	if(hook != NULL)
		hook->processResetAfter();

	m_hookMutex.unlock();
	return ret;
}

ULONG D3D9HookManager::DeviceReleaseHooked(IUnknown *unknown)
{
	//HookLog(stringf("IDirect3DDevice9::Release(%p)", unknown));
	m_hookMutex.lock();

	// Will the device be deleted this call?
	m_DeviceReleaseHook->uninstall();
	unknown->AddRef();
	ULONG refs = unknown->Release();
	HRESULT ret;
	if(refs == 1) {
		// Device is about to be deleted, clean up
		//HookLog(stringf("IDirect3DDevice9::Release(%p)", unknown));

		// Get the `IDirect3DDevice9` from the `IUnknown`
		IDirect3DDevice9 *device = NULL;
		HRESULT res = unknown->QueryInterface(
			__uuidof(IDirect3DDevice9), (void **)&device);
		if(FAILED(res)) {
			// Should never happen
			HookLog("Accidentally hooked a non-device `Release()`");
			ret = unknown->Release();
			m_DeviceReleaseHook->install();
			m_hookMutex.unlock();
			return ret;
		}

		// Forward to the context handler if this is a known context and then
		// delete it as it's about to become invalid
		for(uint i = 0; i < m_hooks.size(); i++) {
			D3D9HookData *data = &m_hooks.data()[i];
			D3D9CommonHook *hook = data->hook;
			if(hook->getDevice() == device) {
				if(data->SwapChainPresentHook != NULL) {
					data->SwapChainPresentHook->uninstall();
					delete data->SwapChainPresentHook;
					data->SwapChainPresentHook = NULL;
				}
				hook->processDeleteContext();

				// Remove from vector and fully release
				m_hooks.erase(m_hooks.begin() + i);
				hook->release();

				break;
			}
		}

		// If `Release()` is called and we have no other known contexts left
		// then the program is most likely shutting down. Use this opportunity
		// to cleanly unhook everything.
		if(m_hooks.size() <= 0) {
			unhook(); // Uninstalls and deletes our hooks

			// Forward to the real function
			ret = device->Release();
		} else {
			// Forward to the real function
			ret = device->Release();
			m_DeviceReleaseHook->install();
		}
	} else {
		// Device is not about to be deleted

		// Forward to the real function
		ret = unknown->Release();
		m_DeviceReleaseHook->install();
	}

	m_hookMutex.unlock();
	return ret;
}

HRESULT D3D9HookManager::DeviceExPresentExHooked(
	IDirect3DDevice9Ex *deviceEx, const RECT *pSourceRect,
	const RECT *pDestRect, HWND hDestWindowOverride,
	const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
	//HookLog(stringf("IDirect3DDevice9Ex::PresentEx(%p)", deviceEx));
	m_hookMutex.lock();

	// Get device object and forward to the context handler
	IDirect3DDevice9 *device = NULL;
	HRESULT res = deviceEx->QueryInterface(
		__uuidof(IDirect3DDevice9), (void **)&device);
	if(SUCCEEDED(res)) {
		D3D9CommonHook *hook = findHookForDevice(device);
		if(hook != NULL)
			hook->processBufferSwap();
	}
	if(device != NULL) {
		// Release our queried object without calling our callback
		m_DeviceReleaseHook->uninstall();
		device->Release();
		m_DeviceReleaseHook->install();
	}

	// Forward to the real function
	m_DeviceExPresentExHook->uninstall();
	HRESULT ret = deviceEx->PresentEx(
		pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
	m_DeviceExPresentExHook->install();

	m_hookMutex.unlock();
	//HookLog(stringf("IDirect3DDevice9Ex::PresentEx(%p) exit", deviceEx));
	return ret;
}

HRESULT D3D9HookManager::DeviceExResetExHooked(
	IDirect3DDevice9Ex *deviceEx,
	D3DPRESENT_PARAMETERS *pPresentationParameters,
	D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
	//HookLog(stringf("IDirect3DDevice9Ex::ResetEx(%p)", deviceEx));
	m_hookMutex.lock();

	// Get device object and forward to the context handler (Part 1)
	IDirect3DDevice9 *device = NULL;
	HRESULT res = deviceEx->QueryInterface(
		__uuidof(IDirect3DDevice9), (void **)&device);
	D3D9CommonHook *hook = NULL;
	if(SUCCEEDED(res)) {
		hook = findHookForDevice(device);
		if(hook != NULL)
			hook->processResetBefore();
	}

	// Forward to the real function
	m_DeviceExResetExHook->uninstall();
	HRESULT ret = deviceEx->ResetEx(
		pPresentationParameters, pFullscreenDisplayMode);
	m_DeviceExResetExHook->install();

	// Forward to the context handler (Part 2)
	if(hook != NULL)
		hook->processResetBefore();
	if(device != NULL) {
		// Release our queried object without calling our callback
		m_DeviceReleaseHook->uninstall();
		device->Release();
		m_DeviceReleaseHook->install();
	}

	m_hookMutex.unlock();
	return ret;
}

HRESULT D3D9HookManager::SwapChainPresentHooked(
	IDirect3DSwapChain9 *chain, const RECT *pSourceRect,
	const RECT *pDestRect, HWND hDestWindowOverride,
	const RGNDATA *pDirtyRegion, DWORD dwFlags)
{
	//HookLog(stringf("IDirect3DSwapChain9::Present(%p)", chain));
	m_hookMutex.lock();

	// Get device object and forward to the context handler
	IDirect3DDevice9 *device = NULL;
	HRESULT res = chain->GetDevice(&device);
	HRESULT ret;
	if(SUCCEEDED(res) && device != NULL) {
		D3D9HookData *data = findDataForDevice(device);
		if(data->hook != NULL)
			data->hook->processBufferSwap();

		// Release our queried object without calling our callback
		m_DeviceReleaseHook->uninstall();
		device->Release();
		m_DeviceReleaseHook->install();

		// Forward to the real function
		data->SwapChainPresentHook->uninstall();
		ret = chain->Present(pSourceRect, pDestRect, hDestWindowOverride,
			pDirtyRegion, dwFlags);
		data->SwapChainPresentHook->install();
	} else {
		// We don't know which hook object to uninstall so we can't forward to
		// the real function, FIXME?
		HookLog2(InterprocessLog::Warning,
			"Unknown swap chain, cannot forward to real function");
		ret = E_FAIL;
	}

	m_hookMutex.unlock();
	return ret;
}
