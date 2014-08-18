//*****************************************************************************
// Libdeskcap: A high-performance desktop capture library
//
// Copyright (C) 2014 Lucas Murray <lucas@polyflare.com>
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

#include "d3dstatics.h"

//=============================================================================
// Function pointers

// Direct3D 9
typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT);
typedef HRESULT (WINAPI *Direct3DCreate9Ex_t)(UINT, IDirect3D9Ex **);
static Direct3DCreate9_t Direct3DCreate9Ptr = NULL;
static Direct3DCreate9Ex_t Direct3DCreate9ExPtr = NULL;

// DirectX 10
typedef HRESULT (WINAPI *D3D10CreateDevice_t)(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device **);
typedef HRESULT (WINAPI *D3D10CreateDevice1_t)(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
	UINT, ID3D10Device1 **);
typedef HRESULT (WINAPI *D3D10CreateDeviceAndSwapChain_t)(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT,
	DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device **);
typedef HRESULT (WINAPI *D3D10CreateDeviceAndSwapChain1_t)(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
	UINT, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device1 **);
static D3D10CreateDevice_t D3D10CreateDevicePtr = NULL;
static D3D10CreateDevice1_t D3D10CreateDevice1Ptr = NULL;
static D3D10CreateDeviceAndSwapChain_t D3D10CreateDeviceAndSwapChainPtr = NULL;
static D3D10CreateDeviceAndSwapChain1_t D3D10CreateDeviceAndSwapChain1Ptr
	= NULL;

// DirectX 11
typedef HRESULT (WINAPI *D3D11CreateDevice_t)(
	IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
	UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *D3D11CreateDeviceAndSwapChain_t)(
	IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
	UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
	ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
static D3D11CreateDevice_t D3D11CreateDevicePtr = NULL;
static D3D11CreateDeviceAndSwapChain_t D3D11CreateDeviceAndSwapChainPtr = NULL;

//=============================================================================
// Dynamic linking manager

//-----------------------------------------------------------------------------
// Direct3D 9

static bool g_d3d9LibraryLinked = false;

/// <summary>
/// Dynamically links the Direct3D 9 library. If `allowLoad` is true then the
/// function will load the library into memory if it isn't already loaded.
/// </summary>
/// <returns>True if linking was successful</returns>
bool linkD3D9Library(bool allowLoad)
{
	if(g_d3d9LibraryLinked)
		return true; // Already linked

	// Is the Direct3D 9 library actually loaded?
	HMODULE module = GetModuleHandle(TEXT("d3d9.dll"));
	if(module == NULL) {
		// Nope, attempt to load it if we're allowed to
		if(!allowLoad)
			return false;
		module = LoadLibrary(TEXT("d3d9.dll"));
		if(module == NULL)
			return false;
	}
	g_d3d9LibraryLinked = true;

	// Prepare statically linked stubs
	Direct3DCreate9Ptr =
		(Direct3DCreate9_t)GetProcAddress(module, "Direct3DCreate9");
	Direct3DCreate9ExPtr =
		(Direct3DCreate9Ex_t)GetProcAddress(module, "Direct3DCreate9Ex");

	return true;
}

void unlinkD3D9Library()
{
	// Force relink
	g_d3d9LibraryLinked = false;

	// Unset for safety
	Direct3DCreate9Ptr = NULL;
	Direct3DCreate9ExPtr = NULL;
}

bool D3D9LibraryLinked()
{
	return g_d3d9LibraryLinked;
}

bool Direct3DCreate9ExExists()
{
	return (Direct3DCreate9ExPtr != NULL);
}

//-----------------------------------------------------------------------------
// DirectX 10

static bool g_dx10LibraryLinked = false;

/// <summary>
/// Dynamically links the DirectX 10 and/or 10.1 library. If `allowLoad` is
/// true then the function will load the library into memory if it isn't
/// already loaded.
///
/// WARNING: It is possible for this function to only load one of DX10 or
/// DX10.1 depending on the application.
/// </summary>
/// <returns>True if linking was successful</returns>
bool linkDX10Library(bool allowLoad)
{
	if(g_dx10LibraryLinked)
		return true; // Already linked

	//-------------------------------------------------------------------------
	// Attempt to load DirectX 10.1 first

	// Is the DirectX 10.1 library actually loaded?
	HMODULE module = GetModuleHandle(TEXT("d3d10_1.dll"));
	if(module == NULL) {
		// Nope, attempt to load it if we're allowed to
		if(allowLoad)
			module = LoadLibrary(TEXT("d3d10_1.dll"));
	}
	if(module != NULL) {
		g_dx10LibraryLinked = true;

		// Prepare statically linked stubs
		D3D10CreateDevice1Ptr =
			(D3D10CreateDevice1_t)GetProcAddress(module, "D3D10CreateDevice1");
		D3D10CreateDeviceAndSwapChain1Ptr =
			(D3D10CreateDeviceAndSwapChain1_t)GetProcAddress(
			module, "D3D10CreateDeviceAndSwapChain1");
		// DirectX 10.1 is now loaded
	}

	//-------------------------------------------------------------------------
	// Attempt to load DirectX 10

	// Is the DirectX 10 library actually loaded?
	module = GetModuleHandle(TEXT("d3d10.dll"));
	if(module == NULL) {
		// Nope, attempt to load it if we're allowed to
		if(!allowLoad)
			return g_dx10LibraryLinked;
		module = LoadLibrary(TEXT("d3d10.dll"));
		if(module == NULL)
			return g_dx10LibraryLinked;
	}
	g_dx10LibraryLinked = true;

	// Prepare statically linked stubs
	D3D10CreateDevicePtr =
		(D3D10CreateDevice_t)GetProcAddress(module, "D3D10CreateDevice");
	D3D10CreateDeviceAndSwapChainPtr =
		(D3D10CreateDeviceAndSwapChain_t)GetProcAddress(
		module, "D3D10CreateDeviceAndSwapChain");
	// DirectX 10 is now loaded

	return true;
}

void unlinkDX10Library()
{
	// Force relink
	g_dx10LibraryLinked = false;

	// Unset for safety
	D3D10CreateDevicePtr = NULL;
	D3D10CreateDevice1Ptr = NULL;
	D3D10CreateDeviceAndSwapChainPtr = NULL;
	D3D10CreateDeviceAndSwapChain1Ptr = NULL;
}

bool DX10LibraryLinked()
{
	return g_dx10LibraryLinked;
}

bool D3D10CreateDeviceExists()
{
	return (D3D10CreateDevicePtr != NULL);
}

bool D3D10CreateDevice1Exists()
{
	return (D3D10CreateDevice1Ptr != NULL);
}

//-----------------------------------------------------------------------------
// DirectX 11

static bool g_dx11LibraryLinked = false;

/// <summary>
/// Dynamically links the DirectX 11 library. If `allowLoad` is true then the
/// function will load the library into memory if it isn't already loaded.
/// </summary>
/// <returns>True if linking was successful</returns>
bool linkDX11Library(bool allowLoad)
{
	if(g_dx11LibraryLinked)
		return true; // Already linked

	// Is the DirectX 11 library actually loaded?
	HMODULE module = GetModuleHandle(TEXT("d3d11.dll"));
	if(module == NULL) {
		// Nope, attempt to load it if we're allowed to
		if(!allowLoad)
			return false;
		module = LoadLibrary(TEXT("d3d11.dll"));
		if(module == NULL)
			return false;
	}
	g_dx11LibraryLinked = true;

	// Prepare statically linked stubs
	D3D11CreateDevicePtr =
		(D3D11CreateDevice_t)GetProcAddress(module, "D3D11CreateDevice");
	D3D11CreateDeviceAndSwapChainPtr =
		(D3D11CreateDeviceAndSwapChain_t)GetProcAddress(
		module, "D3D11CreateDeviceAndSwapChain");

	return true;
}

void unlinkDX11Library()
{
	// Force relink
	g_dx11LibraryLinked = false;

	// Unset for safety
	D3D11CreateDevicePtr = NULL;
	D3D11CreateDeviceAndSwapChainPtr = NULL;
}

bool DX11LibraryLinked()
{
	return g_dx11LibraryLinked;
}

//=============================================================================
// Function stubs

//-----------------------------------------------------------------------------
// Direct3D 9

IDirect3D9 * WINAPI Direct3DCreate9_mishira(UINT SDKVersion)
{
	return (*Direct3DCreate9Ptr)(SDKVersion);
}

HRESULT WINAPI Direct3DCreate9Ex_mishira(UINT SDKVersion, IDirect3D9Ex **ppD3D)
{
	return (*Direct3DCreate9ExPtr)(SDKVersion, ppD3D);
}

//-----------------------------------------------------------------------------
// DirectX 10

HRESULT WINAPI D3D10CreateDevice_mishira(
	IDXGIAdapter *pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, UINT SDKVersion, ID3D10Device **ppDevice)
{
	return (*D3D10CreateDevicePtr)(
		pAdapter, DriverType, Software, Flags, SDKVersion, ppDevice);
}

HRESULT WINAPI D3D10CreateDevice1_mishira(
	IDXGIAdapter *pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, D3D10_FEATURE_LEVEL1 HardwareLevel, UINT SDKVersion,
	ID3D10Device1 **ppDevice)
{
	return (*D3D10CreateDevice1Ptr)(
		pAdapter, DriverType, Software, Flags, HardwareLevel, SDKVersion,
		ppDevice);
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain_mishira(
	IDXGIAdapter *pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, UINT SDKVersion, DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
	IDXGISwapChain **ppSwapChain, ID3D10Device **ppDevice)
{
	return (*D3D10CreateDeviceAndSwapChainPtr)(
		pAdapter, DriverType, Software, Flags, SDKVersion, pSwapChainDesc,
		ppSwapChain, ppDevice);
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain1_mishira(
	IDXGIAdapter *pAdapter, D3D10_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, D3D10_FEATURE_LEVEL1 HardwareLevel, UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain,
	ID3D10Device1 **ppDevice)
{
	return (*D3D10CreateDeviceAndSwapChain1Ptr)(
		pAdapter, DriverType, Software, Flags, HardwareLevel, SDKVersion,
		pSwapChainDesc, ppSwapChain, ppDevice);
}

//-----------------------------------------------------------------------------
// DirectX 11

HRESULT WINAPI D3D11CreateDevice_mishira(
	IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
	UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel,
	ID3D11DeviceContext **ppImmediateContext)
{
	return (*D3D11CreateDevicePtr)(
		pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
		SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain_mishira(
	IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
	UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
	UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
	IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice,
	D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)
{
	return (*D3D11CreateDeviceAndSwapChainPtr)(
		pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
		SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
		ppImmediateContext);
}
