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

#include "dxgihookmanager.h"
#include "dx10hook.h"
#include "dx11hook.h"
#include "d3dstatics.h"
#include "helpers.h"
#include "hookmain.h"
#include "rewritehook.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

//=============================================================================
// Function stubs

// ID3D10Device::Release()
extern "C" typedef ULONG (STDMETHODCALLTYPE *DeviceRelease_t)(
	IUnknown *unknown);
extern "C" static ULONG STDMETHODCALLTYPE Device10ReleaseHook(
	IUnknown *unknown)
{
	DXGIHookManager *mgr = DXGIHookManager::getSingleton();
	return mgr->DeviceReleaseHooked(unknown, false);
}

// ID3D11Device::Release()
// Share `DeviceRelease_t` above
extern "C" static ULONG STDMETHODCALLTYPE Device11ReleaseHook(
	IUnknown *unknown)
{
	DXGIHookManager *mgr = DXGIHookManager::getSingleton();
	return mgr->DeviceReleaseHooked(unknown, true);
}

// IDXGISwapChain::Release()
extern "C" typedef ULONG (STDMETHODCALLTYPE *SwapChainRelease_t)(
	IUnknown *unknown);
extern "C" static ULONG STDMETHODCALLTYPE SwapChainReleaseHook(
	IUnknown *unknown)
{
	DXGIHookManager *mgr = DXGIHookManager::getSingleton();
	return mgr->SwapChainReleaseHooked(unknown);
}

// IDXGISwapChain::Present()
extern "C" typedef HRESULT (STDMETHODCALLTYPE *SwapChainPresent_t)(
	IDXGISwapChain *chain, UINT SyncInterval, UINT Flags);
extern "C" static HRESULT STDMETHODCALLTYPE SwapChainPresentHook(
	IDXGISwapChain *chain, UINT SyncInterval, UINT Flags)
{
	DXGIHookManager *mgr = DXGIHookManager::getSingleton();
	return mgr->SwapChainPresentHooked(chain, SyncInterval, Flags);
}

// IDXGISwapChain::ResizeBuffers()
extern "C" typedef HRESULT (STDMETHODCALLTYPE *SwapChainResizeBuffers_t)(
	IDXGISwapChain *chain, UINT BufferCount, UINT Width, UINT Height,
	DXGI_FORMAT NewFormat, UINT SwapChainFlags);
extern "C" static HRESULT STDMETHODCALLTYPE SwapChainResizeBuffersHook(
	IDXGISwapChain *chain, UINT BufferCount, UINT Width, UINT Height,
	DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	DXGIHookManager *mgr = DXGIHookManager::getSingleton();
	return mgr->SwapChainResizeBuffersHooked(
		chain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

//=============================================================================
// DXGIHookManager class

DXGIHookManager *DXGIHookManager::s_instance = NULL;

DXGIHookManager::DXGIHookManager()
	: m_hookMutex()
	, m_dxgiLibLoaded(false)
	, m_safeToHook(false)
	, m_isHooked(false)
	, m_hooks()

	// Hooks
	, m_Device10ReleaseHook(NULL)
	, m_Device11ReleaseHook(NULL)
	, m_SwapChainReleaseHook(NULL)
	, m_SwapChainPresentHook(NULL)
	, m_SwapChainResizeBuffersHook(NULL)
{
	//assert(s_instance == NULL);
	s_instance = this;

	m_hooks.reserve(8);
}

DXGIHookManager::~DXGIHookManager()
{
	// Wait until all callbacks have completed as they will most likely be
	// executed in a different thread than the one we are being deleted from
	m_hookMutex.lock();

	// Unhook everything so our callbacks don't get called while we are
	// destructing
	if(m_Device10ReleaseHook != NULL)
		m_Device10ReleaseHook->uninstall();
	if(m_Device11ReleaseHook != NULL)
		m_Device11ReleaseHook->uninstall();
	if(m_SwapChainReleaseHook != NULL)
		m_SwapChainReleaseHook->uninstall();
	if(m_SwapChainPresentHook != NULL)
		m_SwapChainPresentHook->uninstall();
	if(m_SwapChainResizeBuffersHook != NULL)
		m_SwapChainResizeBuffersHook->uninstall();

	// As another thread might have processed while we were uninstalling our
	// hooks temporary yield to make sure they have fully processed before
	// continuing
	m_hookMutex.unlock();
	Sleep(50);
	m_hookMutex.lock();

	// Delete all hooking contexts
	while(!m_hooks.empty()) {
		DXGICommonHook *hook = m_hooks.back();
		hook->release();
		m_hooks.pop_back();
	}

	// Delete hooks cleanly
	unhook();

	s_instance = NULL;
	m_hookMutex.unlock();
}

void DXGIHookManager::attemptToHook()
{
	if(!m_dxgiLibLoaded)
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

	// Define swap chain description for dummy device
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	memset(&swapChainDesc, 0, sizeof(swapChainDesc));
	//swapChainDesc.BufferDesc.Width = 0;
	//swapChainDesc.BufferDesc.Height = 0;
	//swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
	//swapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferDesc.ScanlineOrdering =
		DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = 1; // No anti-aliasing
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.OutputWindow = hwnd;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	//swapChainDesc.Flags = 0;

	// Create all available DirectX contexts and create the hook handers
	if(DX10LibraryLinked()) {
		// Create a DirectX 10 context reusing our existing DX10 code
		ID3D10Device *device = HookMain::s_instance->refDummyDX10Device();
		if(device != NULL) {
			// Get the DXGI factory from the device
			IDXGIDevice *dxgiDevice = NULL;
			IDXGIAdapter *dxgiAdapter = NULL;
			IDXGIFactory *dxgiFactory = NULL;
			HRESULT res = device->QueryInterface(
				__uuidof(IDXGIDevice), (void **)&dxgiDevice);
			if(SUCCEEDED(res)) {
				res = dxgiDevice->GetParent(
					__uuidof(IDXGIAdapter), (void **)&dxgiAdapter);
				if(SUCCEEDED(res)) {
					res = dxgiAdapter->GetParent(
						__uuidof(IDXGIFactory), (void **)&dxgiFactory);
					if(FAILED(res)) {
						dxgiAdapter->Release();
						dxgiAdapter = NULL;
					}
				} else {
					dxgiDevice->Release();
					dxgiDevice = NULL;
				}
			}

			if(dxgiFactory != NULL) {
				// Create our swap chain
				IDXGISwapChain *chain = NULL;
				res = dxgiFactory->CreateSwapChain(
					device, &swapChainDesc, &chain);
				if(SUCCEEDED(res)) {
					// Create hook handlers based on their position in the
					// object's virtual table
					m_Device10ReleaseHook = new RewriteHook(
						vtableLookup(device, 2), &Device10ReleaseHook);
					if(m_SwapChainReleaseHook == NULL) {
						m_SwapChainReleaseHook = new RewriteHook(
							vtableLookup(chain, 2), &SwapChainReleaseHook);
					}
					if(m_SwapChainPresentHook == NULL) {
						m_SwapChainPresentHook = new RewriteHook(
							vtableLookup(chain, 8), &SwapChainPresentHook);
					}
					if(m_SwapChainResizeBuffersHook == NULL) {
						m_SwapChainResizeBuffersHook = new RewriteHook(
							vtableLookup(chain, 13),
							&SwapChainResizeBuffersHook);
					}

					chain->Release();
				} else {
					HookLog2(InterprocessLog::Warning, stringf(
						"Failed to create DX10 swap chain. Reason = %s",
						getDX10ErrorCode(res).data()));
				}
			}

			// Clean up
			if(dxgiFactory != NULL)
				dxgiFactory->Release();
			if(dxgiAdapter != NULL)
				dxgiAdapter->Release();
			if(dxgiDevice != NULL)
				dxgiDevice->Release();
			HookMain::s_instance->derefDummyDX10Device();
		} else {
			// Failed to create device. Reason already logged
		}
	}
	if(DX11LibraryLinked()) {
		// Create a DirectX 11 device and swap chain
		UINT flags = D3D10_CREATE_DEVICE_SINGLETHREADED;
		ID3D11Device *device = NULL;
		ID3D11DeviceContext *context = NULL;
		IDXGISwapChain *chain = NULL;
		D3D_FEATURE_LEVEL featureLvl = D3D_FEATURE_LEVEL_11_0;
		HRESULT res = D3D11CreateDeviceAndSwapChain_mishira(
			NULL, // _In_ IDXGIAdapter *pAdapter,
			D3D_DRIVER_TYPE_HARDWARE, // _In_ D3D_DRIVER_TYPE DriverType,
			NULL, // _In_ HMODULE Software,
			flags, // _In_ UINT Flags,
			NULL, // _In_ const D3D_FEATURE_LEVEL *pFeatureLevels,
			0, // _In_ UINT FeatureLevels,
			D3D11_SDK_VERSION, // _In_ UINT SDKVersion,
			&swapChainDesc, // _In_ const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
			&chain, // _Out_ IDXGISwapChain **ppSwapChain,
			&device, // _Out_ ID3D11Device **ppDevice,
			&featureLvl, // _Out_ D3D_FEATURE_LEVEL *pFeatureLevel,
			&context); // _Out_ ID3D11DeviceContext **ppImmediateContext
		if(SUCCEEDED(res)) {
			// Create hook handlers based on their position in the
			// object's virtual table
			m_Device11ReleaseHook = new RewriteHook(
				vtableLookup(device, 2), &Device11ReleaseHook);
			if(m_SwapChainReleaseHook == NULL) {
				m_SwapChainReleaseHook = new RewriteHook(
					vtableLookup(chain, 2), &SwapChainReleaseHook);
			}
			if(m_SwapChainPresentHook == NULL) {
				m_SwapChainPresentHook = new RewriteHook(
					vtableLookup(chain, 8), &SwapChainPresentHook);
			}
			if(m_SwapChainResizeBuffersHook == NULL) {
				m_SwapChainResizeBuffersHook = new RewriteHook(
					vtableLookup(chain, 13), &SwapChainResizeBuffersHook);
			}

			chain->Release();
			context->Release();
			device->Release();
		} else {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to create DX11 device and swap chain. Reason = %s",
				getDX11ErrorCode(res).data()));
		}
	}

	// Destroy dummy window
	DestroyWindow(hwnd);

	// Install any hooks that we created
	if(m_Device10ReleaseHook != NULL)
		m_Device10ReleaseHook->install();
	if(m_Device11ReleaseHook != NULL)
		m_Device11ReleaseHook->install();
	if(m_SwapChainReleaseHook != NULL)
		m_SwapChainReleaseHook->install();
	if(m_SwapChainPresentHook != NULL)
		m_SwapChainPresentHook->install();
	if(m_SwapChainResizeBuffersHook != NULL)
		m_SwapChainResizeBuffersHook->install();
}

void DXGIHookManager::unhook()
{
	if(!m_isHooked)
		return; // Already unhooked
	HookLog("Destroying DXGI subsystem");

	// Uninstall and delete our global hook objects
	delete m_Device10ReleaseHook;
	delete m_Device11ReleaseHook;
	delete m_SwapChainReleaseHook;
	delete m_SwapChainPresentHook;
	delete m_SwapChainResizeBuffersHook;
	m_Device10ReleaseHook = NULL;
	m_Device11ReleaseHook = NULL;
	m_SwapChainReleaseHook = NULL;
	m_SwapChainPresentHook = NULL;
	m_SwapChainResizeBuffersHook = NULL;

	m_safeToHook = false; // No longer safe to hook
	m_dxgiLibLoaded = false; // Attempt to refetch function pointers

	//HookLog("Successfully destroyed DXGI subsystem");
	m_isHooked = false;
}

void DXGIHookManager::loadLibIfPossible()
{
	if(m_dxgiLibLoaded)
		return; // Already loaded and attempted to hook

	// Attempt to link with the base OpenGL library if it isn't already
	bool linkedDX10 = linkDX10Library(false);
	bool linkedDX11 = linkDX11Library(false);
	if(!linkedDX10 && !linkedDX11)
		return; // Failed to link
	m_dxgiLibLoaded = true;
	// Application is using DXGI

	// Which APIs are we using?
	if(linkedDX10 && linkedDX11)
		HookLog("Initialized DXGI subsystem using DirectX 10 and 11");
	else if(linkedDX11)
		HookLog("Initialized DXGI subsystem using DirectX 11 only");
	else if(linkedDX10)
		HookLog("Initialized DXGI subsystem using DirectX 10 only");
	else {
		// Should never happen
		HookLog2(InterprocessLog::Warning,
			"Initialized DXGI subsystem with no DirectX API");
	}

	m_safeToHook = true;
}

DXGICommonHook *DXGIHookManager::findHookForSwapChain(IDXGISwapChain *chain)
{
	for(uint i = 0; i < m_hooks.size(); i++) {
		DXGICommonHook *hook = m_hooks.data()[i];
		if(hook->getSwapChain() == chain)
			return hook;
	}
	return NULL;
}

ULONG DXGIHookManager::DeviceReleaseHooked(IUnknown *unknown, bool isDX11)
{
#if 0
	{
		if(isDX11)
			HookLog(stringf("ID3D11Device::Release(%p)", unknown));
		else
			HookLog(stringf("ID3D10Device::Release(%p)", unknown));
		m_hookMutex.lock();
		RewriteHook *rewriteHook = m_Device10ReleaseHook;
		if(isDX11)
			rewriteHook = m_Device11ReleaseHook;
		//rewriteHook->uninstall();
		//m_hookMutex.unlock();
		//HRESULT ret = unknown->Release();
		HRESULT ret = ((DeviceRelease_t)rewriteHook->getTrampoline())(
			unknown);
		//m_hookMutex.lock();
		//rewriteHook->install();
		m_hookMutex.unlock();
		return ret;
	}
#endif // 0
	//=============

	m_hookMutex.lock();

	RewriteHook *rewriteHook = m_Device10ReleaseHook;
	if(isDX11)
		rewriteHook = m_Device11ReleaseHook;

	// FIXME: We never seem to receive correct release events from devices
	// making the following code useless. We need to fix this so that DirectX
	// doesn't complain about referenced objects at process termination.
	// FIXME: This code also doesn't work with `USE_MINHOOK`
#define IMPLEMENT_DEVICE_RELEASE 0
#if IMPLEMENT_DEVICE_RELEASE
	// Will the device be deleted this call?
	rewriteHook->uninstall();
	unknown->AddRef();
	ULONG refs = unknown->Release();

	// Debugging
	//if(isDX11)
	//	HookLog(stringf("ID3D11Device::Release(%p, %d)", unknown, refs));
	//else
	//	HookLog(stringf("ID3D10Device::Release(%p, %d)", unknown, refs));

	HRESULT ret = E_FAIL;
	if(refs == 1) {
		// Device is about to be deleted, clean up
		if(isDX11)
			HookLog(stringf("ID3D11Device::Release(%p)", unknown));
		else
			HookLog(stringf("ID3D10Device::Release(%p)", unknown));

		// Get the `ID3D10Device` from the `IUnknown`
		void *device = NULL;
		HRESULT res;
		if(isDX11) {
			res = unknown->QueryInterface(
				__uuidof(ID3D11Device), (void **)&device);
		} else {
			res = unknown->QueryInterface(
				__uuidof(ID3D10Device), (void **)&device);
		}
		if(FAILED(res)) {
			// Should never happen
			HookLog("Accidentally hooked a non-device `Release()`");
			ret = unknown->Release();
			rewriteHook->install();
			m_hookMutex.unlock();
			return ret;
		}

		// Forward to the context handler if this is a known context and then
		// delete it as it's about to become invalid
		for(uint i = 0; i < m_hooks.size(); i++) {
			DXGICommonHook *hook = m_hooks.data()[i];
			if(hook->getDevice() == device) {
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
			ret = unknown->Release();
		} else {
			// Forward to the real function
			ret = unknown->Release();
			rewriteHook->install();
		}
	} else {
		// Device is not about to be deleted

		// Forward to the real function
		ret = unknown->Release();
		rewriteHook->install();
	}
#else
	// Forward to the real function
#if USE_MINHOOK
	HRESULT ret = ((DeviceRelease_t)rewriteHook->getTrampoline())(
		unknown);
#else
	rewriteHook->uninstall();
	HRESULT ret = unknown->Release();
	rewriteHook->install();
#endif // USE_MINHOOK
#endif // IMPLEMENT_DEVICE_RELEASE

	m_hookMutex.unlock();
	return ret;
}

ULONG DXGIHookManager::SwapChainReleaseHooked(IUnknown *unknown)
{
#if 0
	{
		HookLog(stringf("IDXGISwapChain::Release(%p)", unknown));
		m_hookMutex.lock();
		//m_SwapChainReleaseHook->uninstall();
		//m_hookMutex.unlock();
		//HRESULT ret = unknown->Release();
		HRESULT ret =
			((SwapChainRelease_t)m_SwapChainReleaseHook->getTrampoline())(
			unknown);
		//m_hookMutex.lock();
		//m_SwapChainReleaseHook->install();
		m_hookMutex.unlock();
		return ret;
	}
#endif // 0
	//=============

	m_hookMutex.lock();

	// Will the swap chain be deleted this call?
#if USE_MINHOOK
	unknown->AddRef();
	ULONG refs =
		((SwapChainRelease_t)m_SwapChainReleaseHook->getTrampoline())(
		unknown);
#else
	m_SwapChainReleaseHook->uninstall();
	unknown->AddRef();
	ULONG refs = unknown->Release();
#endif // USE_MINHOOK

	// Debugging
	//HookLog(stringf("IDXGISwapChain::Release(%p, %d)", unknown, refs));

	HRESULT ret = E_FAIL;
	if(refs == 1) {
		// Swap chain is about to be deleted, clean up
		//HookLog(stringf("IDXGISwapChain::Release(%p)", unknown));

		// Get the `IDXGISwapChain` from the `IUnknown`
		IDXGISwapChain *chain = NULL;
		HRESULT res;
		res = unknown->QueryInterface(
			__uuidof(IDXGISwapChain), (void **)&chain);
		if(FAILED(res)) {
			// Should never happen
			HookLog("Accidentally hooked a non-swap chain `Release()`");
#if USE_MINHOOK
			ret =
				((SwapChainRelease_t)m_SwapChainReleaseHook->getTrampoline())(
				unknown);
#else
			ret = unknown->Release();
#endif // USE_MINHOOK
			m_SwapChainReleaseHook->install();
			m_hookMutex.unlock();
			return ret;
		}

		// Forward to the context handler if this is a known context and then
		// delete it as it's about to become invalid
		for(uint i = 0; i < m_hooks.size(); i++) {
			DXGICommonHook *hook = m_hooks.data()[i];
			if(hook->getSwapChain() == chain) {
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
		// FIXME: This crashes some users so we just disable it for now
		if(false) { //m_hooks.size() <= 0) {
			unhook(); // Uninstalls and deletes our hooks

			// Forward to the real function
			ret = unknown->Release();
		} else {
			// Forward to the real function
#if USE_MINHOOK
			ret =
				((SwapChainRelease_t)m_SwapChainReleaseHook->getTrampoline())(
				unknown);
#else
			ret = unknown->Release();
			m_SwapChainReleaseHook->install();
#endif // USE_MINHOOK
		}
	} else {
		// Swap chain is not about to be deleted

		// Forward to the real function
#if USE_MINHOOK
		ret =
			((SwapChainRelease_t)m_SwapChainReleaseHook->getTrampoline())(
			unknown);
#else
		ret = unknown->Release();
		m_SwapChainReleaseHook->install();
#endif // USE_MINHOOK
	}

	m_hookMutex.unlock();
	return ret;
}

HRESULT DXGIHookManager::SwapChainPresentHooked(
	IDXGISwapChain *chain, UINT SyncInterval, UINT Flags)
{
#if 0
	{
		HookLog(stringf("IDXGISwapChain::Present(%p)", chain));
		m_hookMutex.lock();
		//m_SwapChainPresentHook->uninstall();
		//m_hookMutex.unlock();
		//HRESULT ret = chain->Present(SyncInterval, Flags);
		HRESULT ret =
			((SwapChainPresent_t)m_SwapChainPresentHook->getTrampoline())(
			chain, SyncInterval, Flags);
		//m_hookMutex.lock();
		//m_SwapChainPresentHook->install();
		m_hookMutex.unlock();
		return ret;
	}
#endif // 0
	//=============

	//HookLog(stringf("IDXGISwapChain::Present(%p)", chain));
	m_hookMutex.lock();

	// Create a new `DXGICommonHook` instance for every unique device so we can
	// keep track of multiple contexts
	DXGICommonHook *hook = findHookForSwapChain(chain);
	if(hook == NULL) {
		// This is a brand new context! Track it
		//HookLog(stringf("IDXGISwapChain::Present(%p)", chain));

		// Is this a DX10 or DX11 device?
		ID3D10Device *device10 = NULL;
		ID3D11Device *device11 = NULL;
		HRESULT res = chain->GetDevice(
			__uuidof(ID3D10Device), (void**)&device10);
		res = chain->GetDevice(
			__uuidof(ID3D11Device), (void**)&device11);
		if(device10 == NULL && device11 == NULL) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to get device from swap chain. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto swapChainPresentFailed1;
		}

		// DX10 devices can upgrade to DX11 devices but DX11 devices cannot
		// downgrade to DX10 devices. We must make sure that we create the
		// correct hook object otherwise the hook will not work.
		bool isDX11 = (device11 != NULL && device10 == NULL);

		// Get the HDC of the window that this device uses
		DXGI_SWAP_CHAIN_DESC desc;
		res = chain->GetDesc(&desc);
		if(FAILED(res)) {
			// Should never happen?
			goto swapChainPresentFailed1;
		}
		if(desc.OutputWindow == NULL) {
			HookLog2(InterprocessLog::Warning,
				"HWND of DXGI swap chain is NULL");
			goto swapChainPresentFailed1;
		}
		HDC hdc = GetDC(desc.OutputWindow);

		// FIXME: Test if the process has created another swap device for the
		// same window before it released the old device.

		// Create the appropriate hook object. It is the responsibility of the
		// hook to release the device that it uses.
		if(isDX11) {
			device11->AddRef();
			hook = new DX11Hook(hdc, device11, chain);
		} else {
			device10->AddRef();
			hook = new DX10Hook(hdc, device10, chain);
		}
		hook->initialize();
		m_hooks.push_back(hook);

		// Clean up
swapChainPresentFailed1:
		if(device10 != NULL) {
			m_Device10ReleaseHook->uninstall();
			device10->Release();
			m_Device10ReleaseHook->install();
		}
		if(device11 != NULL) {
			m_Device11ReleaseHook->uninstall();
			device11->Release();
			m_Device11ReleaseHook->install();
		}
	}

	// Forward to the context handler
	if(hook != NULL)
		hook->processBufferSwap();

	// Forward to the real function
#if USE_MINHOOK
	HRESULT ret =
		((SwapChainPresent_t)m_SwapChainPresentHook->getTrampoline())(
		chain, SyncInterval, Flags);
#else
	m_SwapChainPresentHook->uninstall();
	HRESULT ret = chain->Present(SyncInterval, Flags);
	m_SwapChainPresentHook->install();
#endif // USE_MINHOOK

	m_hookMutex.unlock();
	return ret;
}

HRESULT DXGIHookManager::SwapChainResizeBuffersHooked(
	IDXGISwapChain *chain, UINT BufferCount, UINT Width, UINT Height,
	DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
#if 0
	{
		HookLog(stringf("IDXGISwapChain::ResizeBuffers(%p)", chain));
		m_hookMutex.lock();
		//m_SwapChainResizeBuffersHook->uninstall();
		//m_hookMutex.unlock();
		//HRESULT ret = chain->ResizeBuffers(
		//	BufferCount, Width, Height, NewFormat, SwapChainFlags);
		HRESULT ret =
			((SwapChainResizeBuffers_t)m_SwapChainResizeBuffersHook->getTrampoline())(
			chain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		//m_hookMutex.lock();
		//m_SwapChainResizeBuffersHook->install();
		m_hookMutex.unlock();
		return ret;
	}
#endif // 0
	//=============

	// NOTE: We treat this the same way as we treat a Direct3D 9 "reset" which
	// is not actually required but it does allow reusing existing code.

	//HookLog(stringf("IDXGISwapChain::ResizeBuffers(%p)", chain));
	m_hookMutex.lock();

	// Forward to the context handler (Part 1)
	DXGICommonHook *hook = findHookForSwapChain(chain);
	if(hook != NULL)
		hook->processResetBefore();

	// Forward to the real function
#if USE_MINHOOK
	HRESULT ret =
		((SwapChainResizeBuffers_t)m_SwapChainResizeBuffersHook->getTrampoline())(
		chain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
#else
	m_SwapChainResizeBuffersHook->uninstall();
	HRESULT ret = chain->ResizeBuffers(
		BufferCount, Width, Height, NewFormat, SwapChainFlags);
	m_SwapChainResizeBuffersHook->install();
#endif // USE_MINHOOK

	// Forward to the context handler (Part 2)
	if(hook != NULL)
		hook->processResetAfter();

	m_hookMutex.unlock();
	return ret;
}
