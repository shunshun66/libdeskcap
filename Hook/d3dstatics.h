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

#ifndef D3DSTATICS_H
#define D3DSTATICS_H

#include <windows.h>

//=============================================================================
// Define datatypes and macros that are normally defined in the DirectX headers

// Direct3D 9
struct IDirect3D9;
struct IDirect3D9Ex;

// DXGI
struct IDXGIAdapter;
struct DXGI_SWAP_CHAIN_DESC;
struct IDXGISwapChain;

// DirectX 10
struct ID3D10Device;
struct ID3D10Device1;
enum D3D10_DRIVER_TYPE;
enum D3D10_FEATURE_LEVEL1;

// DirectX 11
struct ID3D11Device;
struct ID3D11DeviceContext;
enum D3D_DRIVER_TYPE;
enum D3D_FEATURE_LEVEL;

//=============================================================================
// Dynamic linking manager

// Direct3D 9
bool	linkD3D9Library(bool allowLoad);
void	unlinkD3D9Library();
bool	D3D9LibraryLinked();
bool	Direct3DCreate9ExExists();

// DirectX 10
bool	linkDX10Library(bool allowLoad);
void	unlinkDX10Library();
bool	DX10LibraryLinked();
bool	D3D10CreateDeviceExists();
bool	D3D10CreateDevice1Exists();

// DirectX 11
bool	linkDX11Library(bool allowLoad);
void	unlinkDX11Library();
bool	DX11LibraryLinked();

//=============================================================================
// Define functions that should normally be statically linked. We do this as we
// don't want to pull in the external libraries unless the application is
// already using those libraries.

// Direct3D 9
extern IDirect3D9 * WINAPI Direct3DCreate9_mishira(UINT);
extern HRESULT WINAPI Direct3DCreate9Ex_mishira(UINT, IDirect3D9Ex **);

// DirectX 10
extern HRESULT WINAPI D3D10CreateDevice_mishira(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device **);
extern HRESULT WINAPI D3D10CreateDevice1_mishira(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
	UINT, ID3D10Device1 **);
extern HRESULT WINAPI D3D10CreateDeviceAndSwapChain_mishira(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT,
	DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device **);
extern HRESULT WINAPI D3D10CreateDeviceAndSwapChain1_mishira(
	IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
	UINT, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device1 **);

// DirectX 11
extern HRESULT WINAPI D3D11CreateDevice_mishira(
	IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
	UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
extern HRESULT WINAPI D3D11CreateDeviceAndSwapChain_mishira(
	IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
	UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
	ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

#endif // D3DSTATICS_H
