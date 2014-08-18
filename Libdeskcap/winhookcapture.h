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

#ifndef WINHOOKCAPTURE_H
#define WINHOOKCAPTURE_H

#include "include/captureobject.h"
#include <Libvidgfx/libvidgfx.h>
#include <QtCore/QSize>
#include <QtCore/QObject>
#include <windows.h>

class CaptureSharedSegment;
class Texture;

//=============================================================================
class WinHookCapture : public QObject
{
	Q_OBJECT

private: // Members -----------------------------------------------------------
	HWND					m_hwnd;
	Texture *				m_texture;
	Texture **				m_sharedTexs;
	Texture *				m_activeSharedTex;
	int						m_activeFrameNum;
	int						m_numSharedTexs;
	bool					m_isFlipped;
	int						m_ref;
	bool					m_resourcesInitialized;
	CaptureSharedSegment *	m_capShm;

public: // Constructor/destructor ---------------------------------------------
	WinHookCapture(HWND hwnd);
	~WinHookCapture();

public: // Methods ------------------------------------------------------------
	void		incrementRef();
	HWND		getHwnd() const;
	void		release();

	void		queuedFrameEvent(uint fNum, int numDropped);
	void		initializeResources(VidgfxContext *gfx);
	void		destroyResources(VidgfxContext *gfx);

	QSize		getSize() const;
	Texture *	getTexture() const;
	bool		isFlipped() const;

private:
	void		updateTexture();

	public
Q_SLOTS: // Slots -------------------------------------------------------------
	void		windowReset(WinId winId);
};
//=============================================================================

inline HWND WinHookCapture::getHwnd() const
{
	return m_hwnd;
}

#endif // WINHOOKCAPTURE_H
