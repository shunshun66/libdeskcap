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

#ifndef D3D9COMMONHOOK_H
#define D3D9COMMONHOOK_H

#include "commonhook.h"
#include <d3d9.h>

//=============================================================================
/// <summary>
/// Common functionality that is shared between Direct3D 9 and Direct3D 9Ex
/// capturing.
/// </summary>
class D3D9CommonHook : public CommonHook
{
protected: // Members ---------------------------------------------------------
	IDirect3DDevice9 *	m_device;
	uint				m_swapChainId;
	uint				m_bbWidth;
	uint				m_bbHeight;
	D3DFORMAT			m_bbD3D9Format;

public: // Constructor/destructor ---------------------------------------------
	D3D9CommonHook(HDC hdc, IDirect3DDevice9 *device, uint swapChainId);
protected:
	virtual	~D3D9CommonHook();

public: // Methods ------------------------------------------------------------
	virtual bool		is9Ex() const = 0;
	IDirect3DDevice9 *	getDevice() const;

protected:
	RawPixelFormat		d3dPixelFormatToRawFormat(D3DFORMAT format) const;

protected: // Interface -------------------------------------------------------
	virtual void			calcBackBufferPixelFormat();
	virtual RawPixelFormat	getBackBufferPixelFormat();
	virtual bool			isBackBufferFlipped();
	virtual void			getBackBufferSize(
		uint *width, uint *height, int *left = NULL, int *top = NULL);
};
//=============================================================================

inline IDirect3DDevice9 *D3D9CommonHook::getDevice() const
{
	return m_device;
}

#endif // D3D9COMMONHOOK_H
