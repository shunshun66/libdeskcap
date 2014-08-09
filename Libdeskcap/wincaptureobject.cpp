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

#include "wincaptureobject.h"
#include "include/caplog.h"
#include "hookmanager.h"
#include "wincapturemanager.h"
#include "windupcapture.h"
#include "wingdicapture.h"
#include "winhookcapture.h"

WinCaptureObject::WinCaptureObject(HWND hwnd, CptrMethod method)
	: CaptureObject()
	, m_type(CptrWindowType)
	, m_hwnd(hwnd)
	, m_hMonitor(NULL)
	, m_userMethod(method)
	, m_actualMethod(method)
	, m_gdiCapture(NULL)
	, m_hookCapture(NULL)
	, m_dupCapture(NULL)
	, m_hookIsReffed(false)
{
	construct();
}

WinCaptureObject::WinCaptureObject(HMONITOR hMonitor, CptrMethod method)
	: CaptureObject()
	, m_type(CptrMonitorType)
	, m_hwnd(GetDesktopWindow())
	, m_hMonitor(hMonitor)
	, m_userMethod(method)
	, m_actualMethod(method)
	, m_gdiCapture(NULL)
	, m_hookCapture(NULL)
	, m_dupCapture(NULL)
	, m_hookIsReffed(false)
{
	construct();
}

void WinCaptureObject::construct()
{
	// Watch the hook manager so we know when a window is available for
	// accelerated capture.
	HookManager *hookMgr = CaptureManager::getManager()->getHookManager();
	connect(hookMgr, &HookManager::windowHooked,
		this, &WinCaptureObject::windowHooked);
	connect(hookMgr, &HookManager::windowUnhooked,
		this, &WinCaptureObject::windowUnhooked);
	connect(hookMgr, &HookManager::windowStartedCapturing,
		this, &WinCaptureObject::windowStartedCapturing);
	connect(hookMgr, &HookManager::windowStoppedCapturing,
		this, &WinCaptureObject::windowStoppedCapturing);

	m_actualMethod = determineBestMethod();
	resetCaptureObjects();
}

WinCaptureObject::~WinCaptureObject()
{
	// Stop capturing with a hook if one exists
	if(m_hookIsReffed) {
		HookManager *hookMgr = CaptureManager::getManager()->getHookManager();
		WinId winId = static_cast<WinId>(m_hwnd);
		if(hookMgr->isWindowKnown(winId))
			hookMgr->derefWindowHooked(winId);
		m_hookIsReffed = false;
	}

	if(m_gdiCapture != NULL)
		m_gdiCapture->release();
	if(m_hookCapture != NULL)
		m_hookCapture->release();
	if(m_dupCapture != NULL)
		m_dupCapture->release();
}

/// <summary>
/// Supposed to only determine what the best method of capture is for automatic
/// selection but also handles hook referencing. TODO: Clean up
/// </summary>
CptrMethod WinCaptureObject::determineBestMethod()
{
	if(m_userMethod == CptrStandardMethod)
		return CptrStandardMethod;

	//-------------------------------------------------------------------------
	// Hooking

	if(m_type != CptrMonitorType) {
		// Initialize the hook by referencing it
		HookManager *hookMgr = CaptureManager::getManager()->getHookManager();
		WinId winId = static_cast<WinId>(m_hwnd);
		if(hookMgr->isWindowKnown(winId)) {
			if(hookMgr->isWindowCapturing(winId)) {
				return CptrHookMethod;
			}
			// Issue the command to start accelerated capture ASAP
			if(!m_hookIsReffed) {
				hookMgr->refWindowHooked(winId);
				m_hookIsReffed = true;
			}
		} else {
			// Manager automatically dereffed for us
			m_hookIsReffed = false;
		}

		if(m_userMethod == CptrHookMethod)
			return CptrHookMethod;
	}

	//-------------------------------------------------------------------------
	// Windows 8 duplicator

	if(m_type == CptrMonitorType) {
		// Detect if the duplicator is available by attempting to create a
		// capture object
		WinCaptureManager *mgr =
			static_cast<WinCaptureManager *>(CaptureManager::getManager());
		m_dupCapture = mgr->createDuplicatorCapture(m_hMonitor);
		if(m_dupCapture != NULL) {
			if(m_dupCapture->isValid()) {
				// Successfully created a duplicator, use this method
				return CptrDuplicatorMethod;
			}
			// Failed to create a duplicator, release and fallback
			m_dupCapture->release();
		}

		if(m_userMethod == CptrDuplicatorMethod)
			return CptrDuplicatorMethod;
	}

	//-------------------------------------------------------------------------

	return CptrStandardMethod;
}

/// <summary>
/// Ensures that there is only one child capture object constructed.
/// </summary>
void WinCaptureObject::resetCaptureObjects()
{
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	switch(m_actualMethod) {
	default:
	case CptrAutoMethod:
		Q_ASSERT(false); // Should never happen
		break;
	case CptrStandardMethod:
		// Destroy other objects if required
		if(m_hookCapture != NULL)
			m_hookCapture->release();
		if(m_dupCapture != NULL)
			m_dupCapture->release();
		m_hookCapture = NULL;
		m_dupCapture = NULL;

		// Create GDI object if required
		if(m_gdiCapture == NULL)
			m_gdiCapture = mgr->createGdiCapture(m_hwnd, m_hMonitor);
		break;
	case CptrCompositorMethod:
		// Destroy other objects if required
		if(m_gdiCapture != NULL)
			m_gdiCapture->release();
		if(m_hookCapture != NULL)
			m_hookCapture->release();
		if(m_dupCapture != NULL)
			m_dupCapture->release();
		m_gdiCapture = NULL;
		m_hookCapture = NULL;
		m_dupCapture = NULL;

		// Create DWM object if required
		Q_ASSERT(false); // TODO: Unimplemented
		break;
	case CptrHookMethod:
		// Destroy other objects if required
		if(m_gdiCapture != NULL)
			m_gdiCapture->release();
		if(m_dupCapture != NULL)
			m_dupCapture->release();
		m_gdiCapture = NULL;
		m_dupCapture = NULL;

		// Create hook object if required
		if(m_hookCapture == NULL)
			m_hookCapture = mgr->createHookCapture(m_hwnd);
		break;
	case CptrDuplicatorMethod:
		// Destroy other objects if required
		if(m_gdiCapture != NULL)
			m_gdiCapture->release();
		m_gdiCapture = NULL;

		// Create hook object if required
		if(m_dupCapture == NULL)
			m_dupCapture = mgr->createDuplicatorCapture(m_hMonitor);
		break;
	}
}

CptrType WinCaptureObject::getType() const
{
	return m_type;
}

WinId WinCaptureObject::getWinId() const
{
	if(m_type != CptrWindowType)
		return NULL;
	return static_cast<WinId>(m_hwnd);
}

MonitorId WinCaptureObject::getMonitorId() const
{
	if(m_type != CptrMonitorType)
		return NULL;
	return static_cast<MonitorId>(m_hMonitor);
}

void WinCaptureObject::release()
{
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	mgr->releaseObject(this);
}

void WinCaptureObject::setMethod(CptrMethod method)
{
	if(m_userMethod == method)
		return;
	m_userMethod = method;
	CptrMethod newMethod = determineBestMethod();
	if(m_actualMethod == newMethod)
		return;
	m_actualMethod = newMethod;
	resetCaptureObjects();
}

CptrMethod WinCaptureObject::getMethod() const
{
	return m_userMethod;
}

QSize WinCaptureObject::getSize() const
{
	switch(m_actualMethod) {
	default:
	case CptrAutoMethod:
	case CptrCompositorMethod:
		return QSize();
	case CptrStandardMethod:
		if(m_gdiCapture == NULL)
			return QSize();
		return m_gdiCapture->getSize();
	case CptrHookMethod:
		if(m_hookCapture == NULL)
			return QSize();
		return m_hookCapture->getSize();
	case CptrDuplicatorMethod:
		if(m_dupCapture == NULL)
			return QSize();
		return m_dupCapture->getSize();
	}
}

Texture *WinCaptureObject::getTexture() const
{
	switch(m_actualMethod) {
	default:
	case CptrAutoMethod:
	case CptrCompositorMethod:
		return NULL;
	case CptrStandardMethod:
		if(m_gdiCapture == NULL)
			return NULL;
		return m_gdiCapture->getTexture();
	case CptrHookMethod:
		if(m_hookCapture == NULL)
			return NULL;
		return m_hookCapture->getTexture();
	case CptrDuplicatorMethod:
		if(m_dupCapture == NULL)
			return NULL;
		return m_dupCapture->getTexture();
	}
}

bool WinCaptureObject::isTextureValid() const
{
	return getTexture() != NULL;
}

bool WinCaptureObject::isFlipped() const
{
	switch(m_actualMethod) {
	default:
	case CptrAutoMethod:
	case CptrCompositorMethod:
		return false;
	case CptrStandardMethod:
		return false;
	case CptrHookMethod:
		if(m_hookCapture == NULL)
			return false;
		return m_hookCapture->isFlipped();
	case CptrDuplicatorMethod:
		return false; // FIXME: Textures can be rotated, not flipped
	}
}

QPoint WinCaptureObject::mapScreenPosToLocal(const QPoint &pos) const
{
	if(m_type == CptrMonitorType) {
		return CaptureManager::getManager()->mapScreenToMonitorPos(
			getMonitorId(), pos);
	}
	return CaptureManager::getManager()->mapScreenToWindowPos(getWinId(), pos);
}

void WinCaptureObject::windowHooked(WinId winId)
{
	if(winId != static_cast<WinId>(m_hwnd))
		return; // Not our window
	m_actualMethod = determineBestMethod();
	resetCaptureObjects();
}

void WinCaptureObject::windowStartedCapturing(WinId winId)
{
	if(winId != static_cast<WinId>(m_hwnd))
		return; // Not our window
	m_actualMethod = determineBestMethod();
	resetCaptureObjects();
}

void WinCaptureObject::windowStoppedCapturing(WinId winId)
{
	if(winId != static_cast<WinId>(m_hwnd))
		return; // Not our window
	m_actualMethod = determineBestMethod();
	resetCaptureObjects();
}

void WinCaptureObject::windowUnhooked(WinId winId)
{
	if(winId != static_cast<WinId>(m_hwnd))
		return; // Not our window

	// Reference state has been lost
	m_hookIsReffed = false;
}
