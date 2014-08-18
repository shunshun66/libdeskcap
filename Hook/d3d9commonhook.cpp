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

#include "d3d9commonhook.h"

D3D9CommonHook::D3D9CommonHook(
	HDC hdc, IDirect3DDevice9 *device, uint swapChainId)
	: CommonHook(hdc)
	, m_device(device)
	, m_swapChainId(swapChainId)
	, m_bbWidth(0)
	, m_bbHeight(0)
	, m_bbD3D9Format(D3DFMT_UNKNOWN)
{
}

D3D9CommonHook::~D3D9CommonHook()
{
}

RawPixelFormat D3D9CommonHook::d3dPixelFormatToRawFormat(
	D3DFORMAT format) const
{
	switch(format) {
	default:
		return UnknownPixelFormat;
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8: // TODO
		return BGRAPixelFormat;
	case D3DFMT_R8G8B8:
		return BGRPixelFormat;
	}
	// Should never be reached
	return UnknownPixelFormat;
}

void D3D9CommonHook::calcBackBufferPixelFormat()
{
	// Determine the pixel format of the back buffer
	IDirect3DSwapChain9 *chain;
	HRESULT res = m_device->GetSwapChain(m_swapChainId, &chain);
	if(FAILED(res))
		return;
	D3DPRESENT_PARAMETERS params;
	res = chain->GetPresentParameters(&params);
	if(FAILED(res))
		goto bbFormatFailed1;

	m_bbD3D9Format = params.BackBufferFormat;
	RawPixelFormat format = d3dPixelFormatToRawFormat(m_bbD3D9Format);
	m_bbIsValidFormat = (format != UnknownPixelFormat);
	switch(format) {
	default:
	case UnknownPixelFormat:
	case BGRAPixelFormat:
		m_bbBpp = 4;
		break;
	case BGRPixelFormat:
		m_bbBpp = 3;
		break;
	}
	m_bbWidth = params.BackBufferWidth;
	m_bbHeight = params.BackBufferHeight;

	// Clean up and return
	chain->Release();
	return;

	// Error handling
bbFormatFailed1:
	chain->Release();
}

RawPixelFormat D3D9CommonHook::getBackBufferPixelFormat()
{
	return d3dPixelFormatToRawFormat(m_bbD3D9Format);
}

bool D3D9CommonHook::isBackBufferFlipped()
{
	return false;
}

void D3D9CommonHook::getBackBufferSize(
	uint *width, uint *height, int *left, int *top)
{
	// We assume `calcBackBufferPixelFormat()` is called before this method
	if(width != NULL)
		*width = m_bbWidth;
	if(height != NULL)
		*height = m_bbHeight;
	if(left != NULL || top != NULL)
		CommonHook::getBackBufferSize(NULL, NULL, left, top);
}
