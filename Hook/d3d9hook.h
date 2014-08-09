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

#ifndef D3D9HOOK_H
#define D3D9HOOK_H

#include "d3d9commonhook.h"
#include <d3d10_1.h>

//=============================================================================
/// <summary>
/// Manages a single DirectX 9 window. This class uses two different methods of
/// capture depending on the system's capabilities: Copying via GDI to DirectX
/// 10 if the system has accelerated GDI (DXGI 1.1+ on Windows 7+) or a direct
/// CPU copy if the system doesn't have accelerated GDI (DXGI 1.0 on Windows
/// Vista).
///
/// See the diagram on the following URL to see how DirectX 9 works within the
/// larger DXGI system:
/// http://msdn.microsoft.com/en-us/library/windows/desktop/ee913554%28v=vs.85%29.aspx
/// </summary>
class D3D9Hook : public D3D9CommonHook
{
private: // Constants ---------------------------------------------------------
	static const int	NUM_PLAIN_SURFACES = 2;
	static const int	NUM_SHARED_TEXTURES = MAX_GPU_BUFFERED_FRAMES;

private: // Members -----------------------------------------------------------
	bool	m_useCpuCopy;
	bool	m_sceneObjectsCreated;

	// Shared scene objects
	IDirect3DSurface9 *	m_rtSurface; // Render target surface

	// Direct CPU capturing scene objects
	IDirect3DSurface9 *	m_plainSurfaces[NUM_PLAIN_SURFACES];
	bool				m_plainSurfacePending[NUM_PLAIN_SURFACES]; // `true` if surface contains valid data
	uint				m_nextPlainSurface;

	// DirectX 10 via GDI capturing scene objects
	ID3D10Device *		m_dx10Device;
	//IDirect3DTexture9 *	m_dx9Tex;
	ID3D10Texture2D *	m_dx10Texs[NUM_SHARED_TEXTURES];
	HANDLE				m_dx10TexHandles[NUM_SHARED_TEXTURES];
	uint				m_nextDx10Tex;

public: // Constructor/destructor ---------------------------------------------
	D3D9Hook(HDC hdc, IDirect3DDevice9 *device, uint swapChainId);
protected:
	virtual	~D3D9Hook();

public: // Methods ------------------------------------------------------------
	virtual bool	is9Ex() const;

private:
	DXGI_FORMAT	d3d9ToDxgiFormat(D3DFORMAT format);
	DXGI_FORMAT	d3d9ToGdiCompatible(D3DFORMAT format);

	// Direct CPU capturing
	void	cpuCreateSceneObjects();
	void	cpuDestroySceneObjects();
	void	cpuCaptureBackBuffer(bool captureFrame, uint64_t timestamp);

	// DirectX 10 via GDI capturing
	void	gdiCreateSceneObjects();
	void	gdiDestroySceneObjects();
	void	gdiCaptureBackBuffer(bool captureFrame, uint64_t timestamp);

protected: // Interface -------------------------------------------------------
	virtual	ShmCaptureType	getCaptureType();
	virtual	HANDLE *		getSharedTexHandles(uint *numTex);
	virtual void			createSceneObjects();
	virtual void			destroySceneObjects();
	virtual void			captureBackBuffer(
		bool captureFrame, uint64_t timestamp);
	virtual void			destructorEndCapturing();
};
//=============================================================================

#endif // D3D9HOOK_H
