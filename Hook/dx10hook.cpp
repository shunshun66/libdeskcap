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

#include "dx10hook.h"
#include "helpers.h"
#include "hookmain.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

DX10Hook::DX10Hook(HDC hdc, ID3D10Device *device, IDXGISwapChain *chain)
	: DXGICommonHook(hdc, chain)
	, m_device(device)
{
}

DX10Hook::~DX10Hook()
{
	// It's our responsibility to release the device
	m_device->Release();
}

DXLibVersion DX10Hook::getLibVer() const
{
	return DX10Ver;
}

void *DX10Hook::getDevice() const
{
	return m_device;
}

bool DX10Hook::createSharedResources()
{
	HookLog(stringf("Creating DX10 scene objects for window of size %d x %d",
		m_width, m_height));

	// Create shared DX10 textures
	D3D10_TEXTURE2D_DESC desc;
	desc.Width = m_width;
	desc.Height = m_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = m_bbFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D10_USAGE_DEFAULT;
	desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED;
	HRESULT res = E_FAIL;
	for(int i = 0; i < NUM_SHARED_RESOURCES; i++) {
		res = m_device->CreateTexture2D(&desc, NULL, sharedTexPtr(i));
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to create shared DX10 texture. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto createResourcesFailed1;
		}
	}

	// Get DXGI shared handles from the textures
	for(int i = 0; i < NUM_SHARED_RESOURCES; i++) {
		IDXGIResource *dxgiRes = NULL;
		res = sharedTex(i)->QueryInterface(
			__uuidof(IDXGIResource), (void **)&dxgiRes);
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to get DXGI resource. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto createResourcesFailed1;
		}
		m_sharedResHandles[i] = NULL;
		res = dxgiRes->GetSharedHandle(&m_sharedResHandles[i]);
		dxgiRes->Release();
		if(FAILED(res)) {
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to get DXGI shared handle. Reason = %s",
				getDX10ErrorCode(res).data()));
			goto createResourcesFailed1;
		}
	}

	return true;

	// Error handling
createResourcesFailed1:
	// Return everything to the same state as when we started
	for(int i = 0; i < NUM_SHARED_RESOURCES; i++) {
		m_sharedResHandles[i] = NULL;
		if(sharedTex(i) != NULL) {
			sharedTex(i)->Release();
			*sharedTexPtr(i) = NULL;
		}
	}
	return false;
}

void DX10Hook::releaseSharedResources()
{
	HookLog("Destroying DX10 scene objects");

	for(int i = 0; i < NUM_SHARED_RESOURCES; i++) {
		if(sharedTex(i) != NULL)
			sharedTex(i)->Release();
	}
}

bool DX10Hook::copyBackBufferToResource(int resId)
{
	// Get back buffer surface
	ID3D10Resource *bufRes = NULL;
	HRESULT res =
		m_swapChain->GetBuffer(0, __uuidof(ID3D10Resource), (void **)&bufRes);
	if(FAILED(res)) {
		HookLog2(InterprocessLog::Warning, stringf(
			"Failed to get back buffer surface. Reason = %s",
			getDX10ErrorCode(res).data()));
		return false;
	}

	// Copy pixel data to our shared texture
	ID3D10Texture2D *sharedRes = sharedTex(resId);
	if(m_bbMultisampled)
		m_device->ResolveSubresource(sharedRes, 0, bufRes, 0, m_bbFormat);
	else
		m_device->CopyResource(sharedRes, bufRes);

	// Clean up
	bufRes->Release();

	return true;
}
