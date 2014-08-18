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

#include "windupcapture.h"
#include "include/caplog.h"
#include "wincapturemanager.h"
#include <dxgi1_2.h>
#include <d3d10_1.h>
#include <Libvidgfx/d3dcontext.h>

const QString LOG_CAT = QStringLiteral("WinCapture");

WinDupCapture::WinDupCapture(HMONITOR hMonitor)
	: QObject()
	, m_hMonitor(hMonitor)
	, m_duplicator(NULL)
	, m_texture(NULL)
	, m_ref(1)
	, m_resourcesInitialized(false)
	, m_isValid(false)
	, m_failedOnce(false)
	, m_attemptReaquire(false)
{
	CaptureManager *mgr = CaptureManager::getManager();
	const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
	if(info == NULL) {
		capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
			"Error creating duplicator capture of monitor. Reason = No info");
		return;
	} else {
		capLog(LOG_CAT) << QStringLiteral(
			"Creating duplicator capture of monitor: [%1] \"%2\"")
			.arg(info->friendlyId)
			.arg(info->friendlyName);
	}

	GraphicsContext *gfx = mgr->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		initializeResources(gfx);
}

WinDupCapture::~WinDupCapture()
{
	CaptureManager *mgr = CaptureManager::getManager();
	const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
	if(info != NULL) {
		capLog(LOG_CAT) << QStringLiteral(
			"Destroying duplicator capture of monitor: [%1] \"%2\"")
			.arg(info->friendlyId)
			.arg(info->friendlyName);
	}

	GraphicsContext *gfx = mgr->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		destroyResources(gfx);
}

void WinDupCapture::acquireDuplicator()
{
	if(!m_resourcesInitialized)
		return;

	// Release the existing duplicator if one exists
	if(m_duplicator != NULL)
		m_duplicator->Release();
	m_duplicator = NULL;
	m_isValid = false;

	// Get monitor information
	CaptureManager *mgr = CaptureManager::getManager();
	const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
	if(info == NULL) {
		capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
			"Error creating duplicator capture of monitor. Reason = No info");
		return;
	}

	// WARNING: We assume that the graphics context is valid
	GraphicsContext *gfx = mgr->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid()) {
		capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
			"Error creating duplicator capture of monitor. Reason = Context not valid");
		return;
	}
	D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);

	// Get duplicator interface
	IDXGIOutput1 *output = NULL;
	HRESULT res = static_cast<IDXGIOutput *>(info->extra)->QueryInterface(
		__uuidof(IDXGIOutput1), (void **)&output);
	if(FAILED(res)) {
		capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
			"Error creating duplicator capture of monitor. Reason = No DXGI 1.2");
		return;
	}
	res = output->DuplicateOutput(d3dGfx->getDevice(), &m_duplicator);
	output->Release();
	if(FAILED(res)) {
		if(res == E_INVALIDARG) {
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Error creating duplicator capture of monitor. Reason = Monitor on different adapter");
		} else if(res == E_NOTIMPL) {
			// Dispite the documentation claiming that this error will be
			// returned on Windows 7 that is not the case. Instead it seems to
			// return `DXGI_ERROR_UNSUPPORTED`
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Error creating duplicator capture of monitor. Reason = Incompatible Windows version");
		} else if(res == DXGI_ERROR_UNSUPPORTED) {
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Error creating duplicator capture of monitor. Reason = Unsupported mode or OS");
		} else {
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Error creating duplicator capture of monitor. Reason = Failed to duplicate");
		}
		return;
	}

	// We now have a valid duplicator object
	capLog(LOG_CAT) << QStringLiteral(
		"Duplicator successfully acquired");
	m_isValid = true;
	m_attemptReaquire = false;
}

void WinDupCapture::incrementRef()
{
	m_ref++;
}

void WinDupCapture::release()
{
	m_ref--;
	if(m_ref > 0)
		return;
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	mgr->releaseDuplicatorCapture(this);
}

void WinDupCapture::lowJitterRealTimeFrameEvent(int numDropped, int lateByUsec)
{
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid())
		return;
	D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);
	if(m_duplicator == NULL && m_attemptReaquire) {
		// We lost the duplicator during a monitor mode change. Try again.
		// WARNING: This may cause spam if the monitor never enters a mode that
		// we can safely duplicate!
		acquireDuplicator();
	}
	if(!m_isValid || m_duplicator == NULL)
		return;

	// There are two ways we can capture frames using the duplicator API: We
	// can aquire the next frame and use it directly by holding on to it until
	// the next real time frame event or we can copy the frame to a separate
	// texture and immediately release the acquired one. We use the second
	// method as it allows us to not block as the OS will automatically copy
	// new frames to the buffer while the resource is not locked by us.

	// Aquire the next frame
	DXGI_OUTDUPL_FRAME_INFO info;
	IDXGIResource *frameRes = NULL;
	const UINT DUP_FRAME_TIMEOUT_MSEC = 0; // Don't block at all
	HRESULT res = m_duplicator->AcquireNextFrame(
		DUP_FRAME_TIMEOUT_MSEC, &info, &frameRes);
	if(res == DXGI_ERROR_ACCESS_LOST) {
		// Reaquire the duplicator as the monitor has changed modes and try to
		// aquire the frame again (Which will fail if we have a 0ms timeout)
		capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
			"Lost access to the duplicator, attempting reaquire");
		m_attemptReaquire = true;
		acquireDuplicator();
		if(m_duplicator != NULL) {
			res = m_duplicator->AcquireNextFrame(
				DUP_FRAME_TIMEOUT_MSEC, &info, &frameRes);
		}
	}
	if(FAILED(res)) {
		// Don't log as it'll spam
		return;
	}

	// Convert the resource to a texture that we can use
	ID3D10Texture2D *frameD3DTex = NULL;
	res = frameRes->QueryInterface(
		__uuidof(ID3D10Texture2D), (void **)(&frameD3DTex));
	if(FAILED(res)) {
		// Don't log as it'll spam
		goto exitFrameEvent1;
	}
	frameRes->Release();
	Texture *frameTex = d3dGfx->openDX10Texture(frameD3DTex);
	if(frameTex == NULL) {
		// Don't log as it'll spam
		frameD3DTex->Release();
		goto exitFrameEvent1;
	}

	// Update our cache texture's size if required
	updateTexture(frameTex);
	if(m_texture == NULL) {
		// Don't log as it'll spam
		goto exitFrameEvent2;
	}

	// Copy the acquired resource to another texture
	if(!gfx->copyTextureData(m_texture, frameTex, QPoint(0, 0),
		QRect(QPoint(0, 0), frameTex->getSize())))
	{
		// Failed to copy, don't log as it'll spam
		goto exitFrameEvent2;
	}

exitFrameEvent2:
	gfx->deleteTexture(frameTex);
	frameTex = NULL;
exitFrameEvent1:
	// Release the acquired resource so the OS can copy to the next frame to it
	// at a later time
	m_duplicator->ReleaseFrame();
}

void WinDupCapture::initializeResources(GraphicsContext *gfx)
{
	// Because CaptureObjects are referenced by both the CaptureManager and
	// scene layers it is possible for us to receive two initialize signals
	// instead of one.
	// TODO: As CaptureObjects should only ever be used in scene layers we
	// could possibly remove the CaptureManager signal forwarding
	if(m_resourcesInitialized)
		return;
	m_resourcesInitialized = true;

	acquireDuplicator();

	// While this capture method has timing information attached to the frames
	// we pretend that it does not to save development time. We assume it
	// behaves the same way as the GDI capture method which means we need to be
	// make sure we call the API at the exact time to prevent choppy video.
	// Enable the low jitter tick mode.
	// TODO: If the layer that this texture will be displayed on isn't visible
	// then there is no need to waste the CPU by entering low jitter mode.
	CaptureManager::getManager()->refLowJitterMode();
}

/// <summary>
/// Update the cache texture to match the specified frame texture's dimensions
/// and format. Does NOT actually copy any pixel data.
/// </summary>
void WinDupCapture::updateTexture(Texture *frameTex)
{
	if(!m_isValid)
		return;
	if(!m_resourcesInitialized)
		return; // We may receive ticks before being initialized
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid())
		return;
	D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);
	if(frameTex == NULL || !frameTex->isValid())
		return;

	// Has the texture size changed? If so we need to recreate the texture
	if(m_texture != NULL && m_texture->getSize() != frameTex->getSize()) {
		gfx->deleteTexture(m_texture);
		m_texture = NULL;
	}

	// Do not create a texture if we failed to get the window size as the
	// window may no longer exist or if we already have a valid texture
	if(frameTex->getSize().isEmpty() || m_texture != NULL)
		return;

	// Create a standard BGRA texture that is writable by the GPU. If
	// texture creation fails then don't try it again as it'll spam our log
	// file. We don't need to worry about BGRA not being supported as the
	// duplicator API is guarenteed to return a BGRA format
	if(!m_failedOnce) {
		m_texture = d3dGfx->createTexture(
			frameTex->getSize(), false, false, true);
	}
	if(m_texture == NULL) {
		capLog(LOG_CAT, CapLog::Warning)
			<< QStringLiteral("Failed to create writable RGBA texture");
		m_failedOnce = true;
	}
}

void WinDupCapture::destroyResources(GraphicsContext *gfx)
{
	if(!m_resourcesInitialized)
		return;
	m_resourcesInitialized = false;

	if(m_texture != NULL) {
		gfx->deleteTexture(m_texture);
		m_texture = NULL;
	}
	m_failedOnce = false;
	m_attemptReaquire = false;

	if(m_duplicator != NULL)
		m_duplicator->Release();
	m_duplicator = NULL;
	m_isValid = false;

	CaptureManager::getManager()->derefLowJitterMode();
}

QSize WinDupCapture::getSize() const
{
	if(m_texture == NULL)
		return QSize();
	return m_texture->getSize();
}

Texture *WinDupCapture::getTexture() const
{
	return m_texture;
}
