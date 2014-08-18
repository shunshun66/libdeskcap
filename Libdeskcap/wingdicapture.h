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

#ifndef WINGDICAPTURE_H
#define WINGDICAPTURE_H

#include "include/captureobject.h"
#include <QtCore/QSize>
#include <QtCore/QObject>
#include <windows.h>

class GraphicsContext;
class Texture;

//=============================================================================
class WinGDICapture : public QObject
{
	Q_OBJECT

private: // Members -----------------------------------------------------------
	HWND		m_hwnd;
	HMONITOR	m_hMonitor;
	HDC			m_hdc;
	Texture *	m_texture;
	int			m_ref;
	bool		m_resourcesInitialized;
	bool		m_useDxgi11BgraMethod;
	bool		m_failedOnce;

public: // Constructor/destructor ---------------------------------------------
	WinGDICapture(HWND hwnd, HMONITOR hMonitor = NULL);
	~WinGDICapture();

public: // Methods ------------------------------------------------------------
	void		incrementRef();
	HWND		getHwnd() const;
	HMONITOR	getHmonitor() const;
	void		release();

	void		lowJitterRealTimeFrameEvent(int numDropped, int lateByUsec);
	void		initializeResources(GraphicsContext *gfx);
	void		destroyResources(GraphicsContext *gfx);

	QSize		getSize() const;
	Texture *	getTexture() const;

private:
	void		updateTexture();
};
//=============================================================================

inline HWND WinGDICapture::getHwnd() const
{
	return m_hwnd;
}

inline HMONITOR WinGDICapture::getHmonitor() const
{
	return m_hMonitor;
}

#endif // WINGDICAPTURE_H
