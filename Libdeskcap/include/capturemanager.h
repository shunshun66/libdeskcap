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

#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include "libdeskcap.h"
#include <Libvidgfx/libvidgfx.h>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QVector>

class CaptureObject;
class HookManager;

typedef QVector<MonitorInfo> MonitorInfoList;

//=============================================================================
class LDC_EXPORT CaptureManager : public QObject
{
	Q_OBJECT

protected: // Static members --------------------------------------------------
	static CaptureManager *	s_singleton;

protected: // Members ---------------------------------------------------------
	VidgfxContext *		m_gfxContext;
	HookManager *		m_hookManager;
	MonitorInfoList		m_monitors;
	int					m_lowJitterModeRef;

	// Helpers
	QProcess				m_helper32;
	QProcess				m_helper64;
	QVector<QStringList>	m_helper32Buf;
	QVector<QStringList>	m_helper64Buf;
	bool					m_helper32NormalExit;
	bool					m_helper64NormalExit;

public: // Static methods -----------------------------------------------------
	static CaptureManager *		initializeManager();
	static CaptureManager *		getManager();
	static void					destroyManager();

protected: // Constructor/destructor ------------------------------------------
	CaptureManager();
public:
	virtual ~CaptureManager();

public: // Methods ------------------------------------------------------------
	void					setGraphicsContext(VidgfxContext *gfx);
	VidgfxContext *			getGraphicsContext() const;
	HookManager *			getHookManager() const;

	const MonitorInfoList &	getMonitorInfo() const;
	const MonitorInfo *		getMonitorInfo(MonitorId id) const;
	QPoint					mapScreenToMonitorPos(
		MonitorId id, const QPoint &pos) const;

	bool					getFuzzyCapture() const;
	void					setFuzzyCapture(bool useFuzzyCap);

	uint					getVideoFrequencyNum();
	uint					getVideoFrequencyDenom();
	void					setVideoFrequency(
		uint numerator, uint denominator);

	bool					isInLowJitterMode() const;
	// Ref/deref is for Libdeskcap internal use only, TODO
	void					refLowJitterMode();
	void					derefLowJitterMode();

protected:
	bool					initialize();

	// Helper commands
	QVector<QStringList>	doHelperCommand(
		bool is64, const QString &msg, bool isMultiline = false);

private:
	// Helpers
	bool	startHelper(bool is64);
	void	terminateHelpers();
	void	doHelperFinished(
		bool is64, int exitCode, QProcess::ExitStatus exitStatus);
	bool	waitForReadLine(QIODevice *dev, uint msecs = 30000);
	void	readHelperMessages(QIODevice *dev);

protected: // Interface -------------------------------------------------------
	virtual bool			initializeImpl() = 0;

public:
	virtual CaptureObject *	captureWindow(WinId winId, CptrMethod method) = 0;
	virtual CaptureObject *	captureMonitor(
		MonitorId id, CptrMethod method) = 0;

	virtual QVector<WinId>	getWindowList() const = 0;

	/// <summary>
	/// Caches the current window list to allow faster batched operations.
	/// Every call to this method must have a matching uncache call. While the
	/// window list is cached window creation and destruction will still add
	/// and remove new entries to the cache but any changes to the window title
	/// or similar will not be detectable.
	/// </summary>
	virtual void	cacheWindowList() = 0;
	virtual void	uncacheWindowList() = 0;

	virtual QString	getWindowExeFilename(WinId winId) const = 0;
	virtual QString	getWindowTitle(WinId winId) const = 0;
	virtual QString	getWindowDebugString(WinId winId) const = 0;

	/// <summary>
	/// Maps a coordinate from screen space to window space.
	/// </summary>
	virtual QPoint	mapScreenToWindowPos(
		WinId winId, const QPoint &pos) const = 0;

	/// <summary>
	/// Find the closest matching window that has the specified information.
	/// </summary>
	/// <returns>NULL on failure</returns>
	virtual WinId	findWindow(
		const QString &exe, const QString &title) = 0;

	/// <summary>
	/// Compares the information of two windows to see if they considered
	/// equal. If using the fuzzy comparison method then window "A" is
	/// considered to be the "old" window and window "B" is the "new" one.
	/// </summary>
	/// <returns>True if the windows match</returns>
	virtual bool	doWindowsMatch(
		const QString &aExe, const QString &aTitle, const QString &bExe,
		const QString &bTitle, bool fuzzy = true) = 0;

private:
	virtual void	lowJitterRealTimeFrameEventImpl(
		int numDropped, int lateByUsec) = 0;
	virtual void	realTimeFrameEventImpl(
		int numDropped, int lateByUsec) = 0;
	virtual void	queuedFrameEventImpl(
		uint frameNum, int numDropped) = 0;

Q_SIGNALS: // Signals ---------------------------------------------------------
	void	monitorInfoChanged();
	void	windowCreated(WinId winId);
	void	windowDestroyed(WinId winId);

	/// <summary>
	/// Emitted whenever Libdeskcap requires calls to its low jitter methods
	/// to have the most accurate timings possible in order to reduce capture
	/// stutter. Libdeskcap is by default not in low jitter mode.
	/// </summary>
	void	enterLowJitterMode();

	/// <summary>
	/// Emitted whenever Libdeskcap does not require accurate timings for its
	/// low jitter methods. The application is still required to emit the
	/// signals but it can be done at a reduced accuracy in order to not waste
	/// CPU time.
	/// </summary>
	void	exitLowJitterMode();

	public
Q_SLOTS: // Slots -------------------------------------------------------------
	void	lowJitterRealTimeFrameEvent(int numDropped, int lateByUsec);
	void	realTimeFrameEvent(int numDropped, int lateByUsec);
	void	queuedFrameEvent(uint frameNum, int numDropped);

	private
Q_SLOTS: // Private slots -----------------------------------------------------
	void	helperReadyRead();
	void	helper32Finished(int exitCode, QProcess::ExitStatus exitStatus);
	void	helper64Finished(int exitCode, QProcess::ExitStatus exitStatus);
};
//=============================================================================

inline VidgfxContext *CaptureManager::getGraphicsContext() const
{
	return m_gfxContext;
}

inline HookManager *CaptureManager::getHookManager() const
{
	return m_hookManager;
}

inline const MonitorInfoList &CaptureManager::getMonitorInfo() const
{
	return m_monitors;
}

inline bool CaptureManager::isInLowJitterMode() const
{
	return m_lowJitterModeRef > 0;
}

#endif // CAPTUREMANAGER_H
