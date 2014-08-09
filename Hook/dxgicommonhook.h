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

#ifndef DXGICOMMONHOOK_H
#define DXGICOMMONHOOK_H

#include "commonhook.h"
#include <dxgi.h>

enum DXLibVersion {
	DXUnknownVer = 0,
	DX10Ver,
	//DX101Ver, // Windows automatically creates 10.1 devices using 10.0 API
	DX11Ver
};

//=============================================================================
/// <summary>
/// Common functionality that is shared between all DXGI-based (DirectX 10 and
/// 11) capturing.
/// </summary>
class DXGICommonHook : public CommonHook
{
protected: // Constants -------------------------------------------------------
	static const int	NUM_SHARED_RESOURCES = MAX_GPU_BUFFERED_FRAMES;

protected: // Members ---------------------------------------------------------
	IDXGISwapChain *	m_swapChain;
	uint				m_bbWidth;
	uint				m_bbHeight;
	DXGI_FORMAT			m_bbFormat;
	bool				m_bbMultisampled;

	// Scene objects
	bool				m_sceneObjectsCreated;
	void *				m_sharedRes[NUM_SHARED_RESOURCES];
	HANDLE				m_sharedResHandles[NUM_SHARED_RESOURCES];

	// Previous captured frame
	int					m_prevCapResource;
	uint64_t			m_prevCapTimestamp;

protected: // Constructor/destructor ------------------------------------------
	DXGICommonHook(HDC hdc, IDXGISwapChain *chain);
	virtual	~DXGICommonHook();

public: // Methods ------------------------------------------------------------
	virtual DXLibVersion	getLibVer() const = 0;
	virtual void *			getDevice() const = 0;

	IDXGISwapChain *		getSwapChain() const;

private:
	virtual bool			createSharedResources() = 0;
	virtual void			releaseSharedResources() = 0;
	virtual bool			copyBackBufferToResource(int resId) = 0;

protected: // Interface -------------------------------------------------------
	virtual	ShmCaptureType	getCaptureType();
	virtual	HANDLE *		getSharedTexHandles(uint *numTex);
	virtual void			calcBackBufferPixelFormat();
	virtual RawPixelFormat	getBackBufferPixelFormat();
	virtual bool			isBackBufferFlipped();
	virtual void			getBackBufferSize(
		uint *width, uint *height, int *left = NULL, int *top = NULL);
	virtual void			createSceneObjects();
	virtual void			destroySceneObjects();
	virtual void			captureBackBuffer(
		bool captureFrame, uint64_t timestamp);
	virtual void			destructorEndCapturing();
};
//=============================================================================

inline IDXGISwapChain *DXGICommonHook::getSwapChain() const
{
	return m_swapChain;
}

#endif // DXGICOMMONHOOK_H
