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

#ifndef WINCAPTUREOBJECT_H
#define WINCAPTUREOBJECT_H

#include "include/captureobject.h"
#include <windows.h>

class WinDupCapture;
class WinGDICapture;
class WinHookCapture;

//=============================================================================
class WinCaptureObject : public CaptureObject
{
	Q_OBJECT

private: // Members -----------------------------------------------------------
	CptrType			m_type;
	HWND				m_hwnd;
	HMONITOR			m_hMonitor;
	CptrMethod			m_userMethod;
	CptrMethod			m_actualMethod;
	WinGDICapture *		m_gdiCapture;
	WinHookCapture *	m_hookCapture;
	WinDupCapture *		m_dupCapture;
	bool				m_hookIsReffed;

public: // Constructor/destructor ---------------------------------------------
	WinCaptureObject(HWND hwnd, CptrMethod method); // Window
	WinCaptureObject(HMONITOR hMonitor, CptrMethod method); // Monitor
	virtual	~WinCaptureObject();
	void	construct();

private: // Methods -----------------------------------------------------------
	CptrMethod			determineBestMethod();
	void				resetCaptureObjects();

public: // Interface ----------------------------------------------------------
	virtual CptrType	getType() const;
	virtual WinId		getWinId() const;
	virtual MonitorId	getMonitorId() const;
	virtual void		release();
	virtual void		setMethod(CptrMethod method);
	virtual CptrMethod	getMethod() const;
	virtual QSize		getSize() const;
	virtual VidgfxTex *	getTexture() const;
	virtual bool		isTextureValid() const;
	virtual bool		isFlipped() const;
	virtual QPoint		mapScreenPosToLocal(const QPoint &pos) const;

	public
Q_SLOTS: // Slots -------------------------------------------------------------
	void	windowHooked(WinId winId);
	void	windowUnhooked(WinId winId);
	void	windowStartedCapturing(WinId winId);
	void	windowStoppedCapturing(WinId winId);
};
//=============================================================================

#endif // WINCAPTUREOBJECT_H
