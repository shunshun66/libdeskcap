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

#include "d3d9hook.h"
#include "d3d9hookmanager.h"
#include "helpers.h"
#include "hookmain.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

D3D9Hook::D3D9Hook(HDC hdc, IDirect3DDevice9 *device, uint swapChainId)
	: D3D9CommonHook(hdc, device, swapChainId)
	, m_useCpuCopy(true)
	, m_sceneObjectsCreated(false)

	// Shared scene objects
	, m_rtSurface(NULL)

	// Direct CPU capturing scene objects
	//, m_plainSurfaces() // Zeroed below
	//, m_plainSurfacePending() // Zeroed below
	, m_nextPlainSurface(0)

	// DirectX 10 via GDI capturing scene objects
	, m_dx10Device(NULL)
	//, m_dx9Tex(NULL)
	//, m_dx10Texs() // Zeroed below
	//, m_dx10TexHandles() // Zeroed below
	, m_nextDx10Tex(0)
{
	memset(m_plainSurfaces, 0, sizeof(m_plainSurfaces));
	memset(m_plainSurfacePending, 0, sizeof(m_plainSurfacePending));
	memset(m_dx10Texs, 0, sizeof(m_dx10Texs));
	memset(m_dx10TexHandles, 0, sizeof(m_dx10TexHandles));
}

D3D9Hook::~D3D9Hook()
{
}

bool D3D9Hook::is9Ex() const
{
	return false;
}

DXGI_FORMAT D3D9Hook::d3d9ToDxgiFormat(D3DFORMAT format)
{
	// "Only R10G10B10A2_UNORM, R16G16B16A16_FLOAT and R8G8B8A8_UNORM formats
	// are allowed"
	// http://msdn.microsoft.com/en-us/library/windows/desktop/bb173598%28v=vs.85%29.aspx
	switch(format) {
	case D3DFMT_A8R8G8B8:
		return DXGI_FORMAT_B8G8R8A8_UNORM; // Not officially supported
	case D3DFMT_X8R8G8B8:
		return DXGI_FORMAT_B8G8R8X8_UNORM; // Not officially supported
	case D3DFMT_A8B8G8R8:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case D3DFMT_A2B10G10R10:
		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case D3DFMT_A16B16G16R16F:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	default:
		return DXGI_FORMAT_UNKNOWN;
	};
	// Should never be reached
	return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT D3D9Hook::d3d9ToGdiCompatible(D3DFORMAT format)
{
	// "IDirect3DSurface9::GetDC is valid on the following formats only:
	// D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_R8G8B8, and D3DFMT_X8R8G8B8."
	// http://msdn.microsoft.com/en-us/library/windows/desktop/bb205894%28v=vs.85%29.aspx
	switch(format) {
	case D3DFMT_A8R8G8B8:
		return DXGI_FORMAT_B8G8R8A8_UNORM; // Not officially supported
	case D3DFMT_R5G6B5:
		return DXGI_FORMAT_B5G6R5_UNORM;
	case D3DFMT_X1R5G5B5:
		//return DXGI_FORMAT_B5G5R5X1_UNORM;
		return DXGI_FORMAT_B5G5R5A1_UNORM;
	case D3DFMT_X8R8G8B8:
		return DXGI_FORMAT_B8G8R8X8_UNORM;
		//case D3DFMT_R8G8B8:
		//return DXGI_FORMAT_B8G8R8_UNORM;
	default:
		return DXGI_FORMAT_UNKNOWN;
	};
	// Should never be reached
	return DXGI_FORMAT_UNKNOWN;
}

void D3D9Hook::cpuCreateSceneObjects()
{
	if(m_sceneObjectsCreated)
		return; // Already created
	if(!isCapturable())
		return; // Not capturable

	// Unlike OpenGL Direct3D 9 is very strict about how you can copy pixel
	// data around GPU and CPU memory. Applications can only read data from
	// D3D9 surfaces that are in `D3DPOOL_SYSTEMMEM` or an "offscreen plain
	// surface" and the only way to copy data to a surface in
	// `D3DPOOL_SYSTEMMEM` from GPU memory is with `GetRenderTargetData()` from
	// a surface created with `CreateRenderTarget()`. This means that in order
	// to read from the back buffer we need to do the following:
	//
	// 1) Copy the back buffer surface to a render target surface (Removing
	//    multisampling at the same time if the back buffer has any).
	// 2) Copy the render target surface to one of many offscreen plain
	//    surfaces that are in `D3DPOOL_SYSTEMMEM`. These act similarly to
	//    GPU->CPU PBOs in OpenGL. While having three surfaces is the most
	//    efficient in terms of stalling it adds an unacceptable delay to the
	//    video data when rendered back in the main application.
	// 3) Lock and read the offscreen plain surface pixel data to shared
	//    memory.

	HookLog(stringf("Creating D3D9 scene objects for window of size %d x %d",
		m_width, m_height));

	// Create render target surface
	HRESULT res = m_device->CreateRenderTarget(
		m_width, m_height, m_bbD3D9Format, D3DMULTISAMPLE_NONE, 0, FALSE,
		&m_rtSurface, NULL);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create render target. Reason = %s",
			getD3D9ErrorCode(res).data()));
		goto cpuCreateSceneObjectsFailed1;
	}

	// Create offscreen plain surfaces
	for(int i = 0; i < NUM_PLAIN_SURFACES; i++) {
		res = m_device->CreateOffscreenPlainSurface(
			m_width, m_height, m_bbD3D9Format, D3DPOOL_SYSTEMMEM,
			&m_plainSurfaces[i], NULL);
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to create offscreen plain surface. Reason = %s",
				getD3D9ErrorCode(res).data()));
			goto cpuCreateSceneObjectsFailed1;
		}
	}
	memset(m_plainSurfacePending, 0, sizeof(m_plainSurfacePending));

	m_sceneObjectsCreated = true;
	return;

	// Error handling
cpuCreateSceneObjectsFailed1:
	// Return everything to the same state as when we started
	if(m_rtSurface != NULL)
		m_rtSurface->Release();
	for(int i = 0; i < NUM_PLAIN_SURFACES; i++) {
		if(m_plainSurfaces[i] != NULL) {
			m_plainSurfaces[i]->Release();
			m_plainSurfaces[i] = NULL;
		}
	}
}

void D3D9Hook::cpuDestroySceneObjects()
{
	if(!m_sceneObjectsCreated)
		return; // Already destroyed

	HookLog("Destroying D3D9 scene objects");

	// Destroy all surfaces
	if(m_rtSurface != NULL)
		m_rtSurface->Release();
	for(int i = 0; i < NUM_PLAIN_SURFACES; i++) {
		if(m_plainSurfaces[i] != NULL)
			m_plainSurfaces[i]->Release();
	}

	// Clear memory
	m_rtSurface = NULL;
	memset(m_plainSurfaces, 0, sizeof(m_plainSurfaces));
	memset(m_plainSurfacePending, 0, sizeof(m_plainSurfacePending));

	m_sceneObjectsCreated = false;
}

void D3D9Hook::cpuCaptureBackBuffer(bool captureFrame, uint64_t timestamp)
{
	// Get plain surface to read from and write to
	IDirect3DSurface9 *writeSurface = m_plainSurfaces[m_nextPlainSurface];
	bool *writePending = &m_plainSurfacePending[m_nextPlainSurface];
	m_nextPlainSurface++;
	if(m_nextPlainSurface >= NUM_PLAIN_SURFACES)
		m_nextPlainSurface = 0;
	IDirect3DSurface9 *readSurface = m_plainSurfaces[m_nextPlainSurface];
	bool *readPending = &m_plainSurfacePending[m_nextPlainSurface];

	//-------------------------------------------------------------------------
	// Copy back buffer to our next surface if we are capturing this frame

	if(captureFrame) {
		// Copy the back buffer to our temporary render target surface so it's
		// available to be copied to system memory. TODO: We assume that there
		// is only a single back buffer (I.e. double buffered and not triple
		// buffered).
		IDirect3DSurface9 *bbSurface = NULL;
		HRESULT res = m_device->GetBackBuffer(
			m_swapChainId, 0, D3DBACKBUFFER_TYPE_MONO, &bbSurface);
		bool rtValid = false;
		if(SUCCEEDED(res)) {
			res = m_device->StretchRect(
				bbSurface, NULL, m_rtSurface, NULL, D3DTEXF_NONE);
			if(SUCCEEDED(res))
				rtValid = true;
			bbSurface->Release();
		}

		// Queue the pixels to be copied to system memory
		if(rtValid) {
			res = m_device->GetRenderTargetData(m_rtSurface, writeSurface);
			if(SUCCEEDED(res))
				*writePending = true; // Mark surface as used
		}
	}

	//-------------------------------------------------------------------------
	// Copy previous plain surface data to our shared memory if it's valid

	if(*readPending) {
		// Map buffer
		D3DLOCKED_RECT rect;
		HRESULT res = readSurface->LockRect(&rect, NULL, D3DLOCK_READONLY);
		if(FAILED(res) || rect.pBits == NULL) {
			HookLog2(InterprocessLog::Warning,
				"Failed to lock surface for reading");
			goto readSurfaceFailed1;
		}

		// Debug capturing by testing the colours of specific pixels
#define DO_PIXEL_DEBUG_TEST 0
#if DO_PIXEL_DEBUG_TEST
		uchar *test = NULL;
#define TEST_PBO_PIXEL(x, y, r, g, b) \
	test = &((uchar *)rect.pBits)[(x)*m_bbBpp+(y)*rect.Pitch]; \
	if(test[0] != (b) || test[1] != (g) || test[2] != (r)) \
	HookLog2(InterprocessLog::Warning, stringf( \
	"(%u, %u, %u) != (%u, %u, %u)", test[0], test[1], test[2], (b), (g), (r)))

		// Dustforce game screen at start (Window size: 1184x762)
		TEST_PBO_PIXEL(27, 66, 64, 65, 60);
		TEST_PBO_PIXEL(103, 186, 57, 49, 86);

#undef TEST_PBO_PIXEL
#endif // DO_PIXEL_DEBUG_TEST

		// Copy data to shared memory. TODO: We should probably keep the same
		// stride to improve copy performance
		int frameNum = findUnusedFrameNum();
		if(frameNum >= 0) {
			writeRawPixelsToShmWithStride(
				frameNum, timestamp, rect.pBits, rect.Pitch,
				m_bbWidth * m_bbBpp, m_bbHeight);
		}

		// Unmap buffer
		readSurface->UnlockRect();

readSurfaceFailed1:
		*readPending = false; // Mark surface as unused
	}
}

void D3D9Hook::gdiCreateSceneObjects()
{
	if(m_sceneObjectsCreated)
		return; // Already created
	if(!isCapturable())
		return; // Not capturable

	// Is the back buffer format compatible with DXGI?
	if(d3d9ToGdiCompatible(m_bbD3D9Format) == DXGI_FORMAT_UNKNOWN) {
		HookLog2(InterprocessLog::Warning,
			"Back buffer not compatible with DXGI, falling back to CPU capture");
		m_useCpuCopy = true;
		return;
	}

	// Create a dummy DirectX 10 or 10.1 device depending on the system
	ID3D10Device *m_dx10Device = HookMain::s_instance->refDummyDX10Device();
	if(m_dx10Device == NULL) {
		HookLog2(InterprocessLog::Warning,
			"Failed to create DirectX 10 device, falling back to CPU capture");
		m_useCpuCopy = true;
		return;
	}

	HookLog(stringf("Creating D3D9 scene objects for window of size %d x %d",
		m_width, m_height));

	// Create D3D9 render target surface
	HRESULT res = m_device->CreateRenderTarget(
		m_width, m_height, m_bbD3D9Format, D3DMULTISAMPLE_NONE, 0, TRUE,
		&m_rtSurface, NULL);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create shared D3D9 render target. Reason = %s",
			getD3D9ErrorCode(res).data()));
		goto gdiCreateSceneObjectsFailed1;
	}

	// Create shared DX10 textures
	D3D10_TEXTURE2D_DESC desc;
	desc.Width = m_width;
	desc.Height = m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d9ToGdiCompatible(m_bbD3D9Format);
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D10_USAGE_DEFAULT;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags =
		D3D10_RESOURCE_MISC_SHARED | D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
	for(int i = 0; i < NUM_SHARED_TEXTURES; i++) {
		res = m_dx10Device->CreateTexture2D(&desc, NULL, &m_dx10Texs[i]);
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to create shared DX10 target. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto gdiCreateSceneObjectsFailed1;
		}
	}
	m_nextDx10Tex = 0;

	// Get DXGI shared handles from the textures
	for(int i = 0; i < NUM_SHARED_TEXTURES; i++) {
		IDXGIResource *dxgiRes = NULL;
		res = m_dx10Texs[i]->QueryInterface(
			__uuidof(IDXGIResource), (void **)&dxgiRes);
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to get DXGI resource. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto gdiCreateSceneObjectsFailed1;
		}
		m_dx10TexHandles[i] = NULL;
		res = dxgiRes->GetSharedHandle(&m_dx10TexHandles[i]);
		dxgiRes->Release();
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to get DXGI shared handle. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto gdiCreateSceneObjectsFailed1;
		}
	}

#if 0
	// Create D3D9 render target surface
	HRESULT res = m_device->CreateRenderTarget(
		m_width, m_height, m_bbD3D9Format, D3DMULTISAMPLE_NONE, 0, FALSE,
		&m_rtSurface, NULL);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create shared D3D9 render target. Reason = %s",
			getD3D9ErrorCode(res).data()));
		return; // TODO: Not safe to return here
	}

	// Create shared D3D9 texture
	HANDLE sharedHandle = NULL;
	res = m_device->CreateTexture(
		m_width, m_height, 1, D3DUSAGE_RENDERTARGET, m_bbD3D9Format,
		D3DPOOL_DEFAULT, &m_dx9Tex, &sharedHandle);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create D3D9 texture. Reason = %s",
			getD3D9ErrorCode(res).data()));
		return; // TODO: Not safe to return here
	}

	// Open shared surface as a DX10 texture
	ID3D10Resource *resource = NULL;
	res = m_dx10Device->OpenSharedResource(
		sharedHandle, __uuidof(ID3D10Resource), (void **)(&resource));
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to open D3D9 surface as a DX10 resource. Reason = %s",
			getDX10ErrorCode(res).data()));
		return; // TODO: Not safe to return here
	}
	res = resource->QueryInterface(
		__uuidof(ID3D10Texture2D), (void **)(&m_dx10Tex));
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to query DX10 texture interface. Reason = %s",
			getDX10ErrorCode(res).data()));
		return; // TODO: Not safe to return here
	}
	resource->Release();
#endif // 0

#if 0
	// Create shared DX10 texture
	D3D10_TEXTURE2D_DESC desc;
	desc.Width = m_width;
	desc.Height = m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d9ToDxgiFormat(m_bbD3D9Format);
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D10_USAGE_DEFAULT;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED;
	HRESULT res = m_dx10Device->CreateTexture2D(&desc, NULL, &m_dx10Tex);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create DX10 target. Reason = %s",
			getDX10ErrorCode(res).data()));
		// TODO: Don't continue
	}

	// Get DXGI shared handle from the texture
	IDXGIResource *dxgiRes = NULL;
	res = m_dx10Tex->QueryInterface(
		__uuidof(IDXGIResource), (void **)&dxgiRes);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get DXGI resource. Reason = %s",
			getDX10ErrorCode(res).data()));
		// TODO: Don't continue
	}
	HANDLE sharedHandle = NULL;
	res = dxgiRes->GetSharedHandle(&sharedHandle);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get DXGI shared handle. Reason = %s",
			getDX10ErrorCode(res).data()));
		// TODO: Don't continue
	}
	dxgiRes->Release();

	// Create D3D9 render target surface from the shared handle
	res = m_device->CreateRenderTarget(
		m_width, m_height, m_bbD3D9Format, D3DMULTISAMPLE_NONE, 0, TRUE,
		&m_rtSurface, &sharedHandle);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to create shared D3D9 render target. Reason = %s",
			getD3D9ErrorCode(res).data()));
		// TODO: Don't continue
	}
#endif // 0

	m_sceneObjectsCreated = true;
	return;

	// Error handling
gdiCreateSceneObjectsFailed1:
	// Return everything to the same state as when we started
	if(m_rtSurface != NULL)
		m_rtSurface->Release();
	for(int i = 0; i < NUM_SHARED_TEXTURES; i++) {
		m_dx10TexHandles[i] = NULL;
		if(m_dx10Texs[i] != NULL) {
			m_dx10Texs[i]->Release();
			m_dx10Texs[i] = NULL;
		}
	}
}

void D3D9Hook::gdiDestroySceneObjects()
{
	if(!m_sceneObjectsCreated)
		return; // Already destroyed

	HookLog("Destroying D3D9 scene objects");

	// Destroy all surfaces
	if(m_rtSurface != NULL)
		m_rtSurface->Release();
	//if(m_dx9Tex != NULL)
	//	m_dx9Tex->Release();
	for(int i = 0; i < NUM_SHARED_TEXTURES; i++) {
		if(m_dx10Texs[i] != NULL)
			m_dx10Texs[i]->Release();
	}
	if(m_dx10Device != NULL)
		HookMain::s_instance->derefDummyDX10Device();

	// Clear memory
	m_rtSurface = NULL;
	//m_dx9Tex = NULL;
	memset(m_dx10Texs, 0, sizeof(m_dx10Texs));
	memset(m_dx10TexHandles, 0, sizeof(m_dx10TexHandles));
	m_nextDx10Tex = 0;
	m_dx10Device = NULL;

	m_sceneObjectsCreated = false;
}

void D3D9Hook::gdiCaptureBackBuffer(bool captureFrame, uint64_t timestamp)
{
	if(!captureFrame)
		return; // Nothing to do

	// Get the next shared texture to write to
	ID3D10Texture2D *dx10Tex = m_dx10Texs[m_nextDx10Tex];
	HANDLE dx10TexHandle = m_dx10TexHandles[m_nextDx10Tex];
	if(isFrameNumUsed(m_nextDx10Tex))
		return; // Frame queue is full, cannot do anything right now
	uint frameNum = m_nextDx10Tex;
	m_nextDx10Tex++;
	if(m_nextDx10Tex >= NUM_SHARED_TEXTURES)
		m_nextDx10Tex = 0;

	// Copy the back buffer to our temporary render target surface so it's
	// available to be read by GDI. TODO: We assume that there is only a single
	// back buffer (I.e. double buffered and not triple buffered).
	IDirect3DSurface9 *bbSurface = NULL;
	HRESULT res = m_device->GetBackBuffer(
		m_swapChainId, 0, D3DBACKBUFFER_TYPE_MONO, &bbSurface);
	if(FAILED(res)) {
		// TODO: Warning message?
		goto gdiCaptureBackBufferFailed0;
	}
	res = m_device->StretchRect(
		bbSurface, NULL, m_rtSurface, NULL, D3DTEXF_NONE);
	if(FAILED(res)) {
		// TODO: Warning message?
		bbSurface->Release();
		goto gdiCaptureBackBufferFailed0;
	}
	bbSurface->Release();

	//-------------------------------------------------------------------------
	// Copy from D3D9 to DX10 via GDI

	// Open D3D9 surface in GDI
	HDC d3d9Hdc = NULL;
	res = m_rtSurface->GetDC(&d3d9Hdc);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get HDC of D3D9 render target. Reason = %s",
			getD3D9ErrorCode(res).data()));
		goto gdiCaptureBackBufferFailed0;
	}

	// Open DX10 texture in GDI
	HDC dx10Hdc = NULL;
	IDXGISurface1 *dx10Surface = NULL;
	res = dx10Tex->QueryInterface(
		__uuidof(IDXGISurface1), (void **)&dx10Surface);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get DXGI 1.1 surface of DX10 texture. Reason = %s",
			getDX10ErrorCode(res).data()));
		goto gdiCaptureBackBufferFailed1;
	}
	res = dx10Surface->GetDC(TRUE, &dx10Hdc);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get HDC of DX10 texture. Reason = %s",
			getDX10ErrorCode(res).data()));
		goto gdiCaptureBackBufferFailed2;
	}

	// Blit from D3D9 to DX10. TODO: We should clear the destination first as
	// the source may contain pixels with transparency? TODO: Very slow
	if(BitBlt(dx10Hdc, 0, 0, m_width, m_height, d3d9Hdc, 0, 0, SRCCOPY) == 0) {
		// TODO: Warning message?
		goto gdiCaptureBackBufferFailed3;
	}

	// Clean up
	res = dx10Surface->ReleaseDC(NULL);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to release HDC of DX10 texture. Reason = %s",
			getDX10ErrorCode(res).data()));
	}
	dx10Surface->Release();
	res = m_rtSurface->ReleaseDC(d3d9Hdc);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to release HDC of D3D9 render target. "
			"Reason = %s", getD3D9ErrorCode(res).data()));
	}

	//-------------------------------------------------------------------------

	// Mark shared texture as used
	writeSharedTexToShm(frameNum, timestamp);

	return;

	// Error handling
gdiCaptureBackBufferFailed3:
	res = dx10Surface->ReleaseDC(NULL);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to release HDC of DX10 texture. Reason = %s",
			getDX10ErrorCode(res).data()));
	}
gdiCaptureBackBufferFailed2:
	dx10Surface->Release();
gdiCaptureBackBufferFailed1:
	res = m_rtSurface->ReleaseDC(d3d9Hdc);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to release HDC of D3D9 render target. "
			"Reason = %s", getD3D9ErrorCode(res).data()));
	}
gdiCaptureBackBufferFailed0:
	if(m_nextDx10Tex > 0)
		m_nextDx10Tex--;
	else
		m_nextDx10Tex = NUM_SHARED_TEXTURES - 1;
}

ShmCaptureType D3D9Hook::getCaptureType()
{
	if(m_useCpuCopy)
		return RawPixelsShmType;
	return SharedTextureShmType;
}

HANDLE *D3D9Hook::getSharedTexHandles(uint *numTex)
{
	if(numTex != NULL)
		*numTex = NUM_SHARED_TEXTURES;
	return m_dx10TexHandles;
}

void D3D9Hook::createSceneObjects()
{
	// Does the system support DXGI 1.1 or not?
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	bool hasDxgi11 = shm->getHasDxgi11();
	bool hasBgraTex = shm->getHasBgraTexSupport();
	m_useCpuCopy = !hasDxgi11 || !hasBgraTex;

	// TODO/WARNING: GDI copy support is not fully implemented. The code is not
	// tested properly and very slow, do not use.
	m_useCpuCopy = true;

	if(!m_useCpuCopy)
		gdiCreateSceneObjects();
	if(m_useCpuCopy) // Fallback if GDI failed
		cpuCreateSceneObjects();
}

void D3D9Hook::destroySceneObjects()
{
	if(m_useCpuCopy)
		cpuDestroySceneObjects();
	else
		gdiDestroySceneObjects();
}

void D3D9Hook::captureBackBuffer(bool captureFrame, uint64_t timestamp)
{
	if(m_useCpuCopy)
		cpuCaptureBackBuffer(captureFrame, timestamp);
	else
		gdiCaptureBackBuffer(captureFrame, timestamp);
}

void D3D9Hook::destructorEndCapturing()
{
	// We can safely destroy our objects in a separate thread unlike OpenGL
	endCapturing();
}
