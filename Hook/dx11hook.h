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

#ifndef DX11HOOK_H
#define DX11HOOK_H

#include "dxgicommonhook.h"
#include <d3d11.h>

//=============================================================================
/// <summary>
/// Manages a single DirectX 11 window.
/// </summary>
class DX11Hook : public DXGICommonHook
{
private: // Members -----------------------------------------------------------
	ID3D11Device *		m_device;

public: // Constructor/destructor ---------------------------------------------
	DX11Hook(HDC hdc, ID3D11Device *device, IDXGISwapChain *chain);
protected:
	virtual	~DX11Hook();

public: // Methods ------------------------------------------------------------
	virtual DXLibVersion	getLibVer() const;
	virtual void *			getDevice() const;

private:
	ID3D11Texture2D *		sharedTex(int resId);
	ID3D11Texture2D **		sharedTexPtr(int resId);

	virtual bool			createSharedResources();
	virtual void			releaseSharedResources();
	virtual bool			copyBackBufferToResource(int resId);
};
//=============================================================================

inline ID3D11Texture2D *DX11Hook::sharedTex(int resId)
{
	return reinterpret_cast<ID3D11Texture2D *>(m_sharedRes[resId]);
}

inline ID3D11Texture2D **DX11Hook::sharedTexPtr(int resId)
{
	return reinterpret_cast<ID3D11Texture2D **>(&m_sharedRes[resId]);
}

#endif // DX11HOOK_H
