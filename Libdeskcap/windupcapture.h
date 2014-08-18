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

#ifndef WINDUPCAPTURE_H
#define WINDUPCAPTURE_H

#include "include/captureobject.h"
#include <Libvidgfx/libvidgfx.h>
#include <QtCore/QSize>
#include <QtCore/QObject>
#include <windows.h>

class Texture;
struct IDXGIOutputDuplication;

//=============================================================================
class WinDupCapture : public QObject
{
	Q_OBJECT

private: // Members -----------------------------------------------------------
	HMONITOR	m_hMonitor;
	IDXGIOutputDuplication *	m_duplicator;
	Texture *	m_texture;
	int			m_ref;
	bool		m_resourcesInitialized;
	bool		m_isValid;
	bool		m_failedOnce;
	bool		m_attemptReaquire;

public: // Constructor/destructor ---------------------------------------------
	WinDupCapture(HMONITOR hMonitor);
	~WinDupCapture();

public: // Methods ------------------------------------------------------------
	void		incrementRef();
	bool		isValid() const;
	HMONITOR	getHmonitor() const;
	void		release();

	void		lowJitterRealTimeFrameEvent(int numDropped, int lateByUsec);
	void		initializeResources(VidgfxContext *gfx);
	void		destroyResources(VidgfxContext *gfx);

	QSize		getSize() const;
	Texture *	getTexture() const;

private:
	void		acquireDuplicator();
	void		updateTexture(Texture *frameTex);
};
//=============================================================================

inline bool WinDupCapture::isValid() const
{
	return m_isValid;
}

inline HMONITOR WinDupCapture::getHmonitor() const
{
	return m_hMonitor;
}

#endif // WINDUPCAPTURE_H
