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

#ifndef HOOKMANAGER_H
#define HOOKMANAGER_H

#include "include/libdeskcap.h"
#include <QtCore/QObject>
#include <QtCore/QVector>

class GraphicsContext;
class InterprocessLog;
class MainSharedSegment;

//=============================================================================
class HookManager : public QObject
{
	Q_OBJECT

private: // Datatypes ---------------------------------------------------------
	struct KnownWin {
		WinId	winId;
		int		captureRef;
	};

protected: // Members ---------------------------------------------------------
	MainSharedSegment *	m_shm;
	InterprocessLog *	m_interprocessLog;
	QVector<KnownWin>	m_knownWindows;
	QVector<WinId>		m_capturingWindows;

public: // Static methods -----------------------------------------------------
	static void		doGraphicsContextInitialized(GraphicsContext *gfx);

public: // Constructor/destructor ---------------------------------------------
	HookManager();
	virtual ~HookManager();

public: // Methods ------------------------------------------------------------
	bool	initialize();

	MainSharedSegment *	getMainSharedSegment() const;

	bool	isWindowKnown(WinId win) const;
	bool	isWindowCapturing(WinId win) const;

	void	refWindowHooked(WinId win);
	void	derefWindowHooked(WinId win);

	void	processInterprocessLog(bool output = true);

private:
	void	refDerefWindowHooked(WinId win, bool capture);
	void	processRegistry();

	public
Q_SLOTS: // Slots -------------------------------------------------------------
	// Realtime frame event is needed as it is processed before queued frames
	// and real-time ticks.
	void	realTimeFrameEvent(int numDropped, int lateByUsec);
	void	graphicsContextInitialized(GraphicsContext *gfx);
	void	graphicsContextDestroyed(GraphicsContext *gfx);
	void	hasDxgi11Changed(bool hasDxgi11);
	void	hasBgraTexSupportChanged(bool hasBgraTexSupport);

Q_SIGNALS: // Signals ---------------------------------------------------------
	void	windowHooked(WinId winId);
	void	windowUnhooked(WinId winId);
	void	windowReset(WinId winId);
	void	windowStartedCapturing(WinId winId);
	void	windowStoppedCapturing(WinId winId);
};
//=============================================================================

inline MainSharedSegment *HookManager::getMainSharedSegment() const
{
	return m_shm;
}

#endif // HOOKMANAGER_H
