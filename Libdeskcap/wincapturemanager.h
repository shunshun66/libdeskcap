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

#ifndef WINCAPTUREMANAGER_H
#define WINCAPTUREMANAGER_H

#include "include/capturemanager.h"
#include <QtCore/QHash>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <windows.h>

class GraphicsContext;
class WinCaptureManager;
class WinCaptureObject;
class WinDupCapture;
class WinGDICapture;
class WinHookCapture;
struct IDXGIOutput;

//=============================================================================
class HookReattempt : public QObject
{
	Q_OBJECT

private: // Members -----------------------------------------------------------
	WinCaptureManager *	m_mgr;
	QTimer				m_timer;
	HWND				m_hwnd;
	bool				m_is64;
	int					m_attemptNum;

public: // Constructor/destructor ---------------------------------------------
	HookReattempt(WinCaptureManager *mgr, HWND hwnd, bool is64, int msec,
		int attemptNum);

	private
Q_SLOTS: // Slots -------------------------------------------------------------
	void	timeout();
};
//=============================================================================

//=============================================================================
class WinCaptureManager : public CaptureManager
{
	friend class HookReattempt;
	Q_OBJECT

private: // Datatypes ---------------------------------------------------------
	struct CachedInfo {
		HWND		hwnd;
		//bool		is64bit;
		//bool		isHooked;
		QString		exeFilename;
		QString		windowTitle;
		QString		windowClass;
	};

private: // Members -----------------------------------------------------------
	HWINEVENTHOOK				m_eventHook;
	bool						m_ignoreEvents;
	QVector<HWND>				m_knownHandles;
	QVector<CachedInfo>			m_cache;
	int							m_cacheRef;
	QHash<QString, QString>		m_deviceToFriendlyMap;
	QVector<WinCaptureObject *>	m_objects;
	QVector<WinGDICapture *>	m_gdiObjects;
	QVector<WinHookCapture *>	m_hookObjects;
	QVector<WinDupCapture *>	m_dupObjects;
	int							m_unknownMonitorId;

public: // Constructor/destructor ---------------------------------------------
	WinCaptureManager();
	virtual ~WinCaptureManager();

public: // Methods ------------------------------------------------------------
	bool				isIgnoringEvents() const;
	void				processWindowEvent(DWORD ev, HWND hwnd, bool isReal);
	void				addMonitor(HMONITOR handle);
	void				releaseObject(WinCaptureObject *obj);
	WinGDICapture *		createGdiCapture(HWND hwnd, HMONITOR hMonitor = NULL);
	void				releaseGdiCapture(WinGDICapture *obj);
	WinHookCapture *	createHookCapture(HWND hwnd);
	void				releaseHookCapture(WinHookCapture *obj);
	WinDupCapture *		createDuplicatorCapture(HMONITOR hMonitor);
	void				releaseDuplicatorCapture(WinDupCapture *obj);

private:
	QString			getProcExeFilename(DWORD procId, bool fullPath) const;
	IDXGIOutput *	getDXGIOutputForMonitor(HMONITOR handle);
	void			updateMonitorInfo(bool emitSignal);
	void			addToCache(HWND hwnd);
	void			removeFromCache(HWND hwnd);
	bool			getCached(HWND hwnd, CachedInfo &info) const;
	QString			getWindowClass(HWND hwnd) const;
	bool			isBlacklisted(HWND hwnd) const;
	bool			isHookBlacklisted(HWND hwnd) const;
	bool			is64Bit(HWND hwnd) const;
	bool			hookIfRequired(HWND hwnd, bool is64, int attemptNum);

protected: // Interface -------------------------------------------------------
	virtual bool			initializeImpl();

public:
	virtual CaptureObject *	captureWindow(WinId winId, CptrMethod method);
	virtual CaptureObject *	captureMonitor(MonitorId id, CptrMethod method);
	virtual QVector<WinId>	getWindowList() const;
	virtual void			cacheWindowList();
	virtual void			uncacheWindowList();
	virtual QString			getWindowExeFilename(WinId winId) const;
	virtual QString			getWindowTitle(WinId winId) const;
	virtual QString			getWindowDebugString(WinId winId) const;
	virtual QPoint			mapScreenToWindowPos(
		WinId winId, const QPoint &pos) const;
	virtual WinId			findWindow(
		const QString &exe, const QString &title);
	virtual bool			doWindowsMatch(
		const QString &aExe, const QString &aTitle, const QString &bExe,
		const QString &bTitle, bool fuzzy = true);

private:
	virtual void			lowJitterRealTimeFrameEventImpl(
		int numDropped, int lateByUsec);
	virtual void			realTimeFrameEventImpl(
		int numDropped, int lateByUsec);
	virtual void			queuedFrameEventImpl(
		uint frameNum, int numDropped);

	public
Q_SLOTS: // Slots -------------------------------------------------------------
	void	graphicsContextInitialized(GraphicsContext *gfx);
	void	graphicsContextDestroyed(GraphicsContext *gfx);

	private
Q_SLOTS:
	void	updateMonitorInfoSlot();
};
//=============================================================================

inline bool WinCaptureManager::isIgnoringEvents() const
{
	return m_ignoreEvents;
}

#endif // WINCAPTUREMANAGER_H
