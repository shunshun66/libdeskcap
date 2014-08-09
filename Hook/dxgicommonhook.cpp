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

#include "dxgicommonhook.h"
#include "hookmain.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

DXGICommonHook::DXGICommonHook(HDC hdc, IDXGISwapChain *chain)
	: CommonHook(hdc)
	, m_swapChain(chain)
	, m_bbWidth(0)
	, m_bbHeight(0)
	, m_bbFormat(DXGI_FORMAT_UNKNOWN)
	, m_bbMultisampled(false)

	// Scene objects
	, m_sceneObjectsCreated(false)
	//, m_sharedRes() // Zeroed below
	//, m_sharedResHandles() // Zeroed below

	// Previous captured frame
	, m_prevCapResource(-1)
	, m_prevCapTimestamp(0)
{
	memset(m_sharedRes, 0, sizeof(m_sharedRes));
	memset(m_sharedResHandles, 0, sizeof(m_sharedResHandles));
}

DXGICommonHook::~DXGICommonHook()
{
}

ShmCaptureType DXGICommonHook::getCaptureType()
{
	return SharedTextureShmType;
}

HANDLE *DXGICommonHook::getSharedTexHandles(uint *numTex)
{
	if(numTex != NULL)
		*numTex = NUM_SHARED_RESOURCES;
	return m_sharedResHandles;
}

void DXGICommonHook::calcBackBufferPixelFormat()
{
	// Determine the pixel format of the back buffer
	DXGI_SWAP_CHAIN_DESC desc;
	HRESULT res = m_swapChain->GetDesc(&desc);
	if(FAILED(res)) {
		// Should never happen?
		return;
	}

	m_bbFormat = desc.BufferDesc.Format;
	m_bbBpp = 4; // FIXME
	m_bbIsValidFormat = true;
	m_bbWidth = desc.BufferDesc.Width;
	m_bbHeight = desc.BufferDesc.Height;
	m_bbMultisampled = (desc.SampleDesc.Count > 1);
}

RawPixelFormat DXGICommonHook::getBackBufferPixelFormat()
{
	return (RawPixelFormat)(m_bbFormat + DXGIBeginPixelFormat);
}

bool DXGICommonHook::isBackBufferFlipped()
{
	return false;
}

void DXGICommonHook::getBackBufferSize(
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

void DXGICommonHook::createSceneObjects()
{
	if(m_sceneObjectsCreated)
		return; // Already created
	if(!isCapturable())
		return; // Not capturable

	if(createSharedResources())
		m_sceneObjectsCreated = true;
}

void DXGICommonHook::destroySceneObjects()
{
	if(!m_sceneObjectsCreated)
		return; // Already destroyed

	releaseSharedResources();

	// Clear memory
	memset(m_sharedRes, 0, sizeof(m_sharedRes));
	memset(m_sharedResHandles, 0, sizeof(m_sharedResHandles));
	m_prevCapResource = -1;
	m_prevCapTimestamp = 0;

	m_sceneObjectsCreated = false;
}

void DXGICommonHook::captureBackBuffer(bool captureFrame, uint64_t timestamp)
{
	// Due to the asynchronous nature of GPU rendering the copy command below
	// will most likely not have been sent to the GPU by the time we mark the
	// texture as used. This means that there is a possibility that if we write
	// to our shared CPU memory that the texture is ready then Mishira can
	// actually issue commands to the GPU that use the resource before we flush
	// our command buffer here in the hook. To prevent this we delay marking
	// the frame as ready by one frame.
	if(m_prevCapResource != -1) {
		// Mark previous shared texture as used
		writeSharedTexToShm(m_prevCapResource, m_prevCapTimestamp);
		m_prevCapResource = -1;
		m_prevCapTimestamp = 0;
	}

	if(!captureFrame)
		return; // Nothing to do

	// Get the next shared resource to write to
	int resId = findUnusedFrameNum();
	if(resId < 0)
		return;

	// Copy the back buffer to one of our shared textures
	if(!copyBackBufferToResource(resId))
		return; // Failed to copy

	// Remember the texture and timestamp so we can mark it as used next frame
	m_prevCapResource = resId;
	m_prevCapTimestamp = timestamp;
}

void DXGICommonHook::destructorEndCapturing()
{
	// We can safely destroy our objects in a separate thread unlike OpenGL
	endCapturing();
}
