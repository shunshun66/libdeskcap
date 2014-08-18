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

#include "wingdicapture.h"
#include "include/caplog.h"
#include "wincapturemanager.h"
#include <Libvidgfx/d3dcontext.h>
#include <QtGui/QImage>

// Undocumented Qt helper functions that are exported from the QtGui library
extern QImage qt_imageFromWinHBITMAP(HDC hdc, HBITMAP bitmap, int w, int h);

const QString LOG_CAT = QStringLiteral("WinCapture");

WinGDICapture::WinGDICapture(HWND hwnd, HMONITOR hMonitor)
	: QObject()
	, m_hwnd(hwnd)
	, m_hMonitor(hMonitor)
	, m_hdc(GetDC(hwnd))
	, m_texture(NULL)
	, m_ref(1)
	, m_resourcesInitialized(false)
	, m_useDxgi11BgraMethod(false)
	, m_failedOnce(false)
{
	if(m_hMonitor != NULL) {
		// Monitor capture
		CaptureManager *mgr = CaptureManager::getManager();
		const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
		if(info == NULL) {
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Error creating standard capture of monitor");
			m_hMonitor = NULL;
		} else {
			capLog(LOG_CAT) << QStringLiteral(
				"Creating standard capture of monitor: [%1] \"%2\"")
				.arg(info->friendlyId)
				.arg(info->friendlyName);
		}
	} else {
		// Window capture
		WinCaptureManager *mgr =
			static_cast<WinCaptureManager *>(CaptureManager::getManager());
		QString title = mgr->getWindowDebugString(static_cast<WinId>(m_hwnd));
		capLog(LOG_CAT) << QStringLiteral(
			"Creating standard capture of window: %1")
			.arg(title);
	}

	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		initializeResources(gfx);
}

WinGDICapture::~WinGDICapture()
{
	if(m_hMonitor != NULL) {
		// Monitor capture
		CaptureManager *mgr = CaptureManager::getManager();
		const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
		if(info != NULL) {
			capLog(LOG_CAT) << QStringLiteral(
				"Destroying standard capture of monitor: [%1] \"%2\"")
				.arg(info->friendlyId)
				.arg(info->friendlyName);
		}
	} else {
		// Window capture
		WinCaptureManager *mgr =
			static_cast<WinCaptureManager *>(CaptureManager::getManager());
		QString title = mgr->getWindowDebugString(static_cast<WinId>(m_hwnd));
		capLog(LOG_CAT) << QStringLiteral(
			"Destroying standard capture of window: %1")
			.arg(title);
	}

	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		destroyResources(gfx);

	ReleaseDC(m_hwnd, m_hdc);
}

void WinGDICapture::incrementRef()
{
	m_ref++;
}

void WinGDICapture::release()
{
	m_ref--;
	if(m_ref > 0)
		return;
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	mgr->releaseGdiCapture(this);
}

void WinGDICapture::lowJitterRealTimeFrameEvent(int numDropped, int lateByUsec)
{
	// Update texture size if required
	updateTexture();

	// Determine the position and size of the source texture to copy from
	// TODO: Forward cropping regions from layers to here so we copy less data?
	int srcX = 0, srcY = 0;
	int srcWidth = 0, srcHeight = 0;
	if(m_hMonitor != NULL) {
		// Monitor capture
		CaptureManager *mgr = CaptureManager::getManager();
		const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
		if(info != NULL) {
			srcX = info->rect.x();
			srcY = info->rect.y();
		}
		RECT rect;
		if(GetClientRect(m_hwnd, &rect) != 0) {
			srcWidth = rect.right - rect.left;
			srcHeight = rect.bottom - rect.top;
		}
	}

	// Update texture contents
	if(m_texture == NULL)
		return; // No texture to paint on
	if(m_useDxgi11BgraMethod) {
		// DXGI 1.1 is available and BGRA textures are supported
		D3DTexture *tex = static_cast<D3DTexture *>(m_texture);
		HDC texDC = tex->getDC();
		// TODO: We should clear the destination first as the source may
		// contain pixels with transparency
		if(BitBlt(
			texDC, 0, 0, tex->getSize().width(), tex->getSize().height(),
			m_hdc, srcX, srcY, SRCCOPY) == 0)
		{
			// Don't log failure as it'll spam the log file
		}
		tex->releaseDC();
	} else {
		// Fallback if DXGI 1.1 or BGRA texture support isn't available.
		// WARNING: This can be very slow as it blocks.

		int width = m_texture->getSize().width();
		int height = m_texture->getSize().height();
		if(srcWidth == 0 || srcHeight == 0) {
			srcWidth = width;
			srcHeight = height;
		}
		HDC hdc = CreateCompatibleDC(m_hdc);

		// Copy pixel data to a CPU bitmap
		BITMAPINFO bmpInfo;
		memset(&bmpInfo, 0, sizeof(bmpInfo));
		bmpInfo.bmiHeader.biSize = sizeof(bmpInfo);
		bmpInfo.bmiHeader.biWidth = srcWidth;
		bmpInfo.bmiHeader.biHeight = srcHeight;
		bmpInfo.bmiHeader.biPlanes = 1;
		bmpInfo.bmiHeader.biBitCount = 32;
		bmpInfo.bmiHeader.biCompression = BI_RGB;
		//bmpInfo.bmiHeader.biSizeImage = 0;
		//bmpInfo.bmiHeader.biXPelsPerMeter = 0;
		//bmpInfo.bmiHeader.biYPelsPerMeter = 0;
		//bmpInfo.bmiHeader.biClrUsed = 0;
		//bmpInfo.bmiHeader.biClrImportant = 0;
		uchar *bmpData = NULL;
		HBITMAP hbmp = CreateDIBSection(
			hdc, &bmpInfo, DIB_RGB_COLORS, (void **)&bmpData, NULL, 0);
		if(hbmp == NULL) {
			DeleteDC(hdc);
			return;
		}
		HGDIOBJ prevObj = SelectObject(hdc, hbmp);
		BitBlt(hdc, 0, 0, width, height, m_hdc, srcX, srcY, SRCCOPY);

		// Convert the bitmap to a QImage
		QImage img = qt_imageFromWinHBITMAP(hdc, hbmp, width, height);

		// Update texture pixel data
		m_texture->updateData(img);

		// Clean up
		SelectObject(hdc, prevObj);
		DeleteObject(hbmp);
		DeleteDC(hdc);
	}
}

void WinGDICapture::initializeResources(GraphicsContext *gfx)
{
	// Because CaptureObjects are referenced by both the CaptureManager and
	// scene layers it is possible for us to receive two initialize signals
	// instead of one.
	// TODO: As CaptureObjects should only ever be used in scene layers we
	// could possibly remove the CaptureManager signal forwarding
	if(m_resourcesInitialized)
		return;
	m_resourcesInitialized = true;

	updateTexture();

	// As this capture method has no timing information attached to the frames
	// we need to be make sure we call the API at the exact time to prevent
	// choppy video. Enable the low jitter tick mode.
	// TODO: If the layer that this texture will be displayed on isn't visible
	// then there is no need to waste the CPU by entering low jitter mode.
	CaptureManager::getManager()->refLowJitterMode();
}

void WinGDICapture::updateTexture()
{
	if(!m_resourcesInitialized)
		return; // We may receive ticks before being initialized
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid())
		return;

	// Determine the window size
	RECT rect;
	QSize size(0, 0);
	if(m_hMonitor != NULL) {
		// Monitor capture
		CaptureManager *mgr = CaptureManager::getManager();
		const MonitorInfo *info = mgr->getMonitorInfo(m_hMonitor);
		if(info != NULL)
			size = info->rect.size();
	}
	if(size.isEmpty()) {
		// Window capture or something broke with monitor capture
		if(GetClientRect(m_hwnd, &rect) != 0)
			size = QSize(rect.right - rect.left, rect.bottom - rect.top);
	}

	// Has the window size changed? If so we need to recreate the texture
	if(m_texture != NULL && m_texture->getSize() != size) {
		gfx->deleteTexture(m_texture);
		m_texture = NULL;
	}

	// Do not create a texture if we failed to get the window size as the
	// window may no longer exist or if we already have a valid texture
	if(size.isEmpty() || m_texture != NULL)
		return;

	// Copying pixel data from a HDC to DX10 directly using the GDI API is only
	// supported in DXGI 1.1 and if BGRA textures are supported
	D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);
	m_useDxgi11BgraMethod =
		(d3dGfx->hasDxgi11() && d3dGfx->hasBgraTexSupport());

	if(m_useDxgi11BgraMethod) {
		// Create a GDI-compatible texture. If texture creation fails then
		// don't try it again as it'll spam our log file
		if(!m_failedOnce)
			m_texture = d3dGfx->createGDITexture(size);
		if(m_texture == NULL) {
			capLog(LOG_CAT, CapLog::Warning)
				<< QStringLiteral("Failed to create GDI-compatible texture");
			m_failedOnce = true;
		}
	} else {
		// Create a standard RGBA texture that is writable by the CPU. If
		// texture creation fails then don't try it again as it'll spam our log
		// file. We still request an BGRA pixel format though as that's how we
		// will be writing it to the texture as (The graphics context will
		// automatically swizzle for us if BGRA is not natively supported).
		if(!m_failedOnce)
			m_texture = d3dGfx->createTexture(size, true, false, true);
		if(m_texture == NULL) {
			capLog(LOG_CAT, CapLog::Warning)
				<< QStringLiteral("Failed to create writable RGBA texture");
			m_failedOnce = true;
		}
	}
}

void WinGDICapture::destroyResources(GraphicsContext *gfx)
{
	if(!m_resourcesInitialized)
		return;
	m_resourcesInitialized = false;

	if(m_texture != NULL) {
		gfx->deleteTexture(m_texture);
		m_texture = NULL;
	}
	m_failedOnce = false;

	CaptureManager::getManager()->derefLowJitterMode();
}

QSize WinGDICapture::getSize() const
{
	if(m_texture == NULL)
		return QSize();
	return m_texture->getSize();
}

Texture *WinGDICapture::getTexture() const
{
	return m_texture;
}
