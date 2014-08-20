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

#include "wincapturemanager.h"
#include "include/caplog.h"
#include "wincaptureobject.h"
#include "windupcapture.h"
#include "wingdicapture.h"
#include "winhookcapture.h"
#include <dxgi.h>
#include <psapi.h>
#include <QtCore/QFileInfo>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>

#define DEBUG_WINDOW_EVENTS 0

const QString LOG_CAT = QStringLiteral("Capture");

//=============================================================================
// Helpers

static QString pointerToString(void *ptr)
{
#if QT_POINTER_SIZE == 4
	return QStringLiteral("0x") + QString::number((quint32)ptr, 16).toUpper();
#elif QT_POINTER_SIZE == 8
	return QStringLiteral("0x") + QString::number((quint64)ptr, 16).toUpper();
#else
#error Unknown pointer size
#endif
}

//=============================================================================
// HookReattempt class

HookReattempt::HookReattempt(
	WinCaptureManager *mgr, HWND hwnd, bool is64, int msec, int attemptNum)
	: QObject(mgr)
	, m_mgr(mgr)
	, m_timer(this)
	, m_hwnd(hwnd)
	, m_is64(is64)
	, m_attemptNum(attemptNum)
{
	connect(&m_timer, &QTimer::timeout, this, &HookReattempt::timeout);
	m_timer.setSingleShot(true);
	m_timer.start(msec);
}

void HookReattempt::timeout()
{
	if(IsWindow(m_hwnd)) // Only if it still exists
		m_mgr->hookIfRequired(m_hwnd, m_is64, m_attemptNum + 1);
	delete this;
}

//=============================================================================
// WinCaptureManager class

void CALLBACK WinEventProc(
	HWINEVENTHOOK hWinEventHook, DWORD ev, HWND hwnd, LONG idObject,
	LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	// We are only interested in top-level windows. WARNING: We cannot use
	// functions such as `IsWindow()` for destruction events as the window
	// handle is already invalid!
	if(!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
		return;
	if(ev != EVENT_OBJECT_DESTROY && !IsWindow(hwnd))
		return;

	// Forward to singleton
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	if(mgr == NULL)
		return;
	if(mgr->isIgnoringEvents())
		return;
	mgr->processWindowEvent(ev, hwnd, true);
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
	// We are only interested in top-level windows
	if(!hwnd)
		return TRUE;
	if(!IsWindow(hwnd))
		return TRUE;

	// Forward to singleton
	WinCaptureManager *mgr = (WinCaptureManager *)lParam;
	mgr->processWindowEvent(EVENT_OBJECT_SHOW, hwnd, false); // Fake show event

	return TRUE;
}

WinCaptureManager::WinCaptureManager()
	: CaptureManager()
	//, m_eventHook()
	, m_ignoreEvents(false) // TODO: Unused
	, m_knownHandles()
	, m_cache()
	, m_cacheRef(0)
	, m_deviceToFriendlyMap()
	, m_objects()
	, m_gdiObjects()
	, m_hookObjects()
	, m_dupObjects()
	, m_unknownMonitorId(100)
{
	m_knownHandles.reserve(16);
	m_cache.reserve(16);
	m_deviceToFriendlyMap.reserve(8);
	m_objects.reserve(8);
	m_gdiObjects.reserve(8);
	m_hookObjects.reserve(8);
	m_dupObjects.reserve(8);
}

WinCaptureManager::~WinCaptureManager()
{
	if(m_eventHook)
		UnhookWinEvent(m_eventHook);

	// Safely release capture objects
	while(m_objects.count())
		m_objects.last()->release(); // Releases dependencies as well

	// Cleanly empty the monitor list
	for(int i = 0; i < m_monitors.size(); i++) {
		void *extraPtr = m_monitors.at(i).extra;
		if(extraPtr != NULL)
			static_cast<IDXGIOutput *>(extraPtr)->Release();
	}
	m_monitors.clear();
}

bool WinCaptureManager::initializeImpl()
{
	// Watch OS for window creation or deletion. Note that we are using the
	// "show" event instead of "create" as a lot of applications create a
	// window with dummy values, make the required changes and then display
	// the window. Because of this we cannot rely on the initial values of the
	// window as they might change before the user is even aware of the window.
	// It is also possible for a single window to receive multiple "show"
	// events during its lifetime so we must keep track of what windows have
	// already been created.
	m_eventHook = SetWinEventHook(
		EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
		NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

	// Get the initial list of windows
	EnumChildWindows(GetDesktopWindow(), EnumChildProc, (LPARAM)this);

	// Get the list of connected monitors from the OS and detect when the user
	// connects or disconnects them
	updateMonitorInfo(false);
	connect(qApp->desktop(), &QDesktopWidget::resized,
		this, &WinCaptureManager::updateMonitorInfoSlot);
	connect(qApp->desktop(), &QDesktopWidget::screenCountChanged,
		this, &WinCaptureManager::updateMonitorInfoSlot);

#if 0
	// Test search algorithm
	QStringList testInfo;
	testInfo.reserve(32);
	testInfo << "devenv.exe" << "Mishira - Microsoft Visual Studio";
	testInfo << "firefox.exe" << "Some site - Test - Mozilla Firefox";
	testInfo << "notepad++.exe" << "*C:\\Users\\Lucas\\Desktop\\test.cpp - Notepad++";
	testInfo << "MultiMC.exe" << "MultiMC 4.3.0 jenkins-MultiMC4Windows-60";
	testInfo << "foobar2000.exe" << "foobar2000 v1.2.0";
	testInfo << "thunderbird.exe" << "Inbox - test@example.com - Mozilla Thunderbird";
	for(int i = 0; i < testInfo.count(); i += 2) {
		WinId winId = findWindow(
			testInfo.at(i), testInfo.at(i + 1));
		if(winId == NULL) {
			capLog() <<
				QStringLiteral("Could not find :: \"%1\" :: \"%2\"")
				.arg(testInfo.at(i))
				.arg(testInfo.at(i + 1));
			continue;
		}
		capLog() <<
			QStringLiteral("Found :: \"%1\" == \"%2\" :: \"%3\" == \"%4\"")
			.arg(testInfo.at(i))
			.arg(getWindowExeFilename(winId))
			.arg(testInfo.at(i + 1))
			.arg(getWindowTitle(winId));
	}
#endif

#if 0
	// Test matching algorithm
	QStringList testInfo;
	testInfo.reserve(32);
	testInfo << "devenv.exe" << "Mishira - Microsoft Visual Studio";
	testInfo << "devenv.exe" << "Mishira (Running) - Microsoft Visual Studio";
	testInfo << "firefox.exe" << "QtCore 5.0: QFileInfo - Mozilla Firefox";
	testInfo << "firefox.exe" << "Some site - Test - Mozilla Firefox";
	testInfo << "notepad++.exe" << "C:\\Users\\Lucas\\Desktop\\test.cpp - Notepad++";
	testInfo << "notepad++.exe" << "*C:\\Users\\Lucas\\Desktop\\test.cpp - Notepad++";
	testInfo << "MultiMC.exe" << "MultiMC 4.2.2 jenkins-MultiMC4Windows-60";
	testInfo << "MultiMC.exe" << "MultiMC 4.3.0 jenkins-MultiMC4Windows-60";
	testInfo << "mintty.exe" << "~";
	testInfo << "foobar2000.exe" << "foobar2000 v1.1.1";
	testInfo << "foobar2000.exe" << "foobar2000 v1.2.0";
	testInfo << "thunderbird.exe" << "reddit - Feeds - Mozilla Thunderbird";
	testInfo << "thunderbird.exe" << "Inbox - test@example.com - Mozilla Thunderbird";
	for(int i = 0; i < testInfo.count(); i += 2) {
		for(int j = i; j < testInfo.count(); j += 2) {
			bool match = doWindowsMatch(
				testInfo.at(i), testInfo.at(i + 1), testInfo.at(j),
				testInfo.at(j + 1));
			capLog() <<
				QStringLiteral("%1 :: \"%2\" == \"%3\" :: \"%4\" == \"%5\"")
				.arg(match)
				.arg(testInfo.at(i))
				.arg(testInfo.at(j))
				.arg(testInfo.at(i + 1))
				.arg(testInfo.at(j + 1));
		}
	}
#endif

	return true;
}

void WinCaptureManager::processWindowEvent(DWORD ev, HWND hwnd, bool isReal)
{
	// Ignore unknown events
	if(ev != EVENT_OBJECT_SHOW &&
		ev != EVENT_OBJECT_HIDE &&
		ev != EVENT_OBJECT_DESTROY)
		return;

	// We are only interested in the first "show" event for a window
	if(ev == EVENT_OBJECT_SHOW && m_knownHandles.contains(hwnd)) {
#if DEBUG_WINDOW_EVENTS
		capLog() <<
			QStringLiteral("*** Duplicate window created: %1")
			.arg(getWindowDebugString(static_cast<WinId>(hwnd)));
#endif
		return;
	}

	// Process event
	if(ev == EVENT_OBJECT_SHOW) {
		// Window created

		// Ignore invisible windows
		if(!IsWindowVisible(hwnd))
			return;

		// Filter windows by style flags
		DWORD exStyles = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
		DWORD styles = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
		//if(!(styles & WS_BORDER || exStyles & WS_EX_APPWINDOW &&
		//	!(styles & WS_POPUP)))
		if(styles & WS_CHILD || exStyles & WS_EX_TOOLWINDOW)
			return;

		// Blacklist certain windows
		if(isBlacklisted(hwnd))
			return;

		// HACK: Some applications such as WSplit have race conditions that
		// cause them to not initialize properly if we immediately capture
		// them. We get around this by adding a very short delay before
		// attempting to do anything with them.
		if(isReal) {
			Sleep(50);
			if(!IsWindow(hwnd))
				return; // Window deleted itself before we could process it
		}

		m_knownHandles.append(hwnd);
		hookIfRequired(hwnd, is64Bit(hwnd), 1);
		addToCache(hwnd);
#if DEBUG_WINDOW_EVENTS
		capLog() << QStringLiteral("*** Window created: %1")
			.arg(getWindowDebugString(static_cast<WinId>(hwnd)));
#endif
		emit windowCreated(static_cast<WinId>(hwnd));
	} else { // EVENT_OBJECT_HIDE or EVENT_OBJECT_DESTROY
		// Window destroyed

		// Only continue if we knew about the window in the first place
		int id = m_knownHandles.indexOf(hwnd);
		if(id < 0)
			return;

		// HACK: We receive `EVENT_OBJECT_HIDE` events when a window becomes
		// unresponsive but we never receive a corrosponding
		// `EVENT_OBJECT_SHOW` when the window becomes responsive again. Try to
		// ignore events emitted from unresponsive windows.
		if(ev == EVENT_OBJECT_HIDE && IsWindowVisible(hwnd)) {
			capLog(LOG_CAT, CapLog::Warning) << QStringLiteral(
				"Received a hide event for a window that is still visible, ignoring: %1")
				.arg(getWindowDebugString(static_cast<WinId>(hwnd)));
			return;
		}

		m_knownHandles.remove(id);
		removeFromCache(hwnd);
#if DEBUG_WINDOW_EVENTS
		capLog() << QStringLiteral("*** Window destroyed: %1")
			.arg(getWindowDebugString(static_cast<WinId>(hwnd)));
#endif
		emit windowDestroyed(static_cast<WinId>(hwnd));
	}
}

void WinCaptureManager::addToCache(HWND hwnd)
{
	if(m_cacheRef <= 0)
		return;
	removeFromCache(hwnd); // Prevent duplicates

	CachedInfo info;
	info.hwnd = hwnd;
	//info.is64bit = is64Bit(hwnd);
	//info.isHooked = hookIfRequired(hwnd, info.is64bit, 1);
	info.exeFilename = getWindowExeFilename(static_cast<WinId>(hwnd));
	info.windowTitle = getWindowTitle(static_cast<WinId>(hwnd));
	info.windowClass = getWindowClass(hwnd);
	m_cache.push_back(info);
}

void WinCaptureManager::removeFromCache(HWND hwnd)
{
	if(m_cacheRef <= 0)
		return;
	for(int i = 0; i < m_cache.count(); i++) {
		if(m_cache[i].hwnd == hwnd) {
			m_cache.remove(i);
			break;
		}
	}
}

bool WinCaptureManager::getCached(HWND hwnd, CachedInfo &info) const
{
	for(int i = 0; i < m_cache.count(); i++) {
		if(m_cache.at(i).hwnd == hwnd) {
			info = m_cache.at(i);
			return true;
		}
	}
	info.hwnd = 0;
	return false;
}

QString WinCaptureManager::getWindowClass(HWND hwnd) const
{
	// Return cached data if it exists
	CachedInfo info;
	if(getCached(hwnd, info))
		return info.windowClass;

	if(!hwnd || !IsWindow(hwnd))
		return tr("** Unknown **");

	const int strLen = 128;
	wchar_t strBuf[strLen];

	// Get window class
	QString classStr;
	strBuf[0] = TEXT('\0');
	memset(strBuf, 0, sizeof(strBuf));
	if(GetClassName(hwnd, strBuf, strLen) > 0) {
		classStr =
			QString::fromUtf16(reinterpret_cast<const ushort *>(strBuf));
	} else
		classStr = tr("** No class **");

	// Return the string to use
	return classStr;
}

/// <summary>
/// Returns true if the specified HWND should not be captured even if it's a
/// valid window for capturing.
/// </summary>
bool WinCaptureManager::isBlacklisted(HWND hwnd) const
{
	QString filename = getWindowExeFilename(static_cast<WinId>(hwnd));

	// We don't want DWM ghost windows from appearing in our window list as
	// they have no useful content
	if(!filename.compare(QStringLiteral("dwm.exe"), Qt::CaseInsensitive))
		return true;

	return false;
}

/// <summary>
/// Returns true if the process of the specified HWND should never be hooked.
/// </summary>
bool WinCaptureManager::isHookBlacklisted(HWND hwnd) const
{
	QString filename = getWindowExeFilename(static_cast<WinId>(hwnd));

	// Known protected processes
	if(!filename.compare(QStringLiteral("msiexec.exe"), Qt::CaseInsensitive))
		return true;

	// Black list many common application that are unlikely to ever need
	// accelerated capture yet currently crash sometimes due to Mishira. FIXME
	if(!filename.compare(QStringLiteral("iexplore.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("chrome.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("firefox.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("opera.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("spotify.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("steam.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("tweetdeck.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("wmplayer.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("vlc.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("mpc-hc.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("smplayer.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("kmplayer.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("winamp.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("gomplayer.exe"), Qt::CaseInsensitive))
		return true;
	if(!filename.compare(QStringLiteral("amarectv.exe"), Qt::CaseInsensitive))
		return true;

	// Never hook ourselves as we know we'll never need accelerated capture of
	// our own windows
	if(!filename.compare(QStringLiteral("mishira.exe"), Qt::CaseInsensitive))
		return true;

	// Processes that cause issues while debugging
	//#ifdef QT_DEBUG
	if(!filename.compare(QStringLiteral("devenv.exe"), Qt::CaseInsensitive))
		return true;
	//#endif

	return false;
}

bool WinCaptureManager::is64Bit(HWND hwnd) const
{
	// Return cached data if it exists
	//CachedInfo info;
	//if(getCached(hwnd, info))
	//	return info.is64bit;

	// Get process ID
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	if(processId == GetCurrentProcessId()) {
		// The process is ourself
#if QT_POINTER_SIZE == 4
		return false;
#elif QT_POINTER_SIZE == 8
		return true;
#else
#error Unknown pointer size
#endif
	}

	// Open the process
	HANDLE process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if(process == NULL) {
		DWORD err = GetLastError();
		capLog(LOG_CAT, CapLog::Warning)
			<< QStringLiteral("Failed to open process for window \"%1\". Reason = %2")
			.arg(getWindowDebugString(static_cast<WinId>(hwnd))).arg(err);
		return false;
	}

	// Determine if 64-bit
	BOOL isWoW64 = TRUE;
	if(IsWow64Process(process, &isWoW64) == 0) {
		DWORD err = GetLastError();
		capLog(LOG_CAT, CapLog::Warning)
			<< QStringLiteral("Failed to determine if running in WoW64 for window \"%1\". Reason = %2")
			.arg(getWindowDebugString(static_cast<WinId>(hwnd))).arg(err);
		CloseHandle(process);
		return false;
	}
	bool is64 = (isWoW64 ? false : true);

	// Close the process
	CloseHandle(process);

	return is64;
}

bool WinCaptureManager::hookIfRequired(HWND hwnd, bool is64, int attemptNum)
{
	const int MAX_HOOK_ATTEMPTS = 2;

	// Return cached data if it exists
	//CachedInfo info;
	//if(getCached(hwnd, info))
	//	return info.isHooked;

	// Don't hook processes that are known to have issues
	if(isHookBlacklisted(hwnd))
		return true;

	QVector<QStringList> res = doHelperCommand(is64,
		QStringLiteral("hook %1").arg(pointerToString(hwnd)));
	if(res.size() < 1 || res.at(0).size() < 1)
		return false; // Malformed reply
	QString type = res.at(0).at(0);
	if(type.isEmpty())
		return false; // Malformed reply
	int code = type.toInt(); // 0 = Hooked, 1 = Error, 2 = No 3D detected
	if(attemptNum < MAX_HOOK_ATTEMPTS && code == 2) {
		// No 3D detected right now but some games (Such as Metro 2033) do not
		// hook in their 3D library until after the window is shown. In order
		// to capture these we attempt to hook several times after a short
		// delay.
		new HookReattempt(this, hwnd, is64, 500, attemptNum); // 500 msec
		return false;
	}
	return (code ? false : true);
}

CaptureObject *WinCaptureManager::captureWindow(WinId winId, CptrMethod method)
{
	HWND hwnd = static_cast<HWND>(winId);
	if(!hwnd || !IsWindow(hwnd))
		return NULL;

	WinCaptureObject *obj = new WinCaptureObject(hwnd, method);
	m_objects.append(obj);
	return obj;
}

CaptureObject *WinCaptureManager::captureMonitor(
	MonitorId id, CptrMethod method)
{
	HMONITOR hMonitor = static_cast<HMONITOR>(id);
	if(!hMonitor)
		return NULL;

	WinCaptureObject *obj = new WinCaptureObject(hMonitor, method);
	m_objects.append(obj);
	return obj;
}

/// <summary>
/// Use `CaptureObject::release()` instead.
/// </summary>
void WinCaptureManager::releaseObject(WinCaptureObject *obj)
{
	if(obj == NULL)
		return;
	int id = m_objects.indexOf(obj);
	if(obj < 0)
		return;
	m_objects.remove(id);
	delete obj;
}

WinGDICapture *WinCaptureManager::createGdiCapture(
	HWND hwnd, HMONITOR hMonitor)
{
	// Do we already have an existing object?
	for(int i = 0; i < m_gdiObjects.count(); i++) {
		WinGDICapture *obj = m_gdiObjects.at(i);
		if(hwnd == obj->getHwnd() && hMonitor == obj->getHmonitor()) {
			obj->incrementRef();
			return obj;
		}
	}

	// Create a new object
	WinGDICapture *obj = new WinGDICapture(hwnd, hMonitor);
	m_gdiObjects.append(obj);
	return obj;
}

/// <summary>
/// Use `WinGDICapture::release()` instead.
/// </summary>
void WinCaptureManager::releaseGdiCapture(WinGDICapture *obj)
{
	if(obj == NULL)
		return;
	int id = m_gdiObjects.indexOf(obj);
	if(obj < 0)
		return;
	m_gdiObjects.remove(id);
	delete obj;
}

WinHookCapture *WinCaptureManager::createHookCapture(HWND hwnd)
{
	// Do we already have an existing object?
	for(int i = 0; i < m_hookObjects.count(); i++) {
		WinHookCapture *obj = m_hookObjects.at(i);
		if(hwnd == obj->getHwnd()) {
			obj->incrementRef();
			return obj;
		}
	}

	// Create a new object
	WinHookCapture *obj = new WinHookCapture(hwnd);
	m_hookObjects.append(obj);
	return obj;
}

/// <summary>
/// Use `WinHookCapture::release()` instead.
/// </summary>
void WinCaptureManager::releaseHookCapture(WinHookCapture *obj)
{
	if(obj == NULL)
		return;
	int id = m_hookObjects.indexOf(obj);
	if(obj < 0)
		return;
	m_hookObjects.remove(id);
	delete obj;
}

WinDupCapture *WinCaptureManager::createDuplicatorCapture(HMONITOR hMonitor)
{
	// Do we already have an existing object?
	for(int i = 0; i < m_dupObjects.count(); i++) {
		WinDupCapture *obj = m_dupObjects.at(i);
		if(hMonitor == obj->getHmonitor()) {
			obj->incrementRef();
			return obj;
		}
	}

	// Create a new object if possible
	WinDupCapture *obj = new WinDupCapture(hMonitor);
	if(!obj->isValid()) {
		// We cannot add a duplicator to this monitor for some reason
		delete obj;
		return NULL;
	}
	m_dupObjects.append(obj);
	return obj;
}

/// <summary>
/// Use `WinDupCapture::release()` instead.
/// </summary>
void WinCaptureManager::releaseDuplicatorCapture(WinDupCapture *obj)
{
	if(obj == NULL)
		return;
	int id = m_dupObjects.indexOf(obj);
	if(obj < 0)
		return;
	m_dupObjects.remove(id);
	delete obj;
}

QVector<WinId> WinCaptureManager::getWindowList() const
{
	QVector<WinId> ret;
	ret.reserve(m_knownHandles.count());
	for(int i = 0; i < m_knownHandles.count(); i++)
		ret.push_back(static_cast<WinId>(m_knownHandles.at(i)));
	return ret;
}

void WinCaptureManager::cacheWindowList()
{
	m_cacheRef++;
	if(m_cacheRef >= 2)
		return; // Already cached
	for(int i = 0; i < m_knownHandles.count(); i++)
		addToCache(m_knownHandles.at(i));
}

void WinCaptureManager::uncacheWindowList()
{
	if(m_cacheRef <= 0)
		return;
	m_cacheRef--;
	if(m_cacheRef > 0)
		return;
	m_cache.clear();
}

QPoint WinCaptureManager::mapScreenToWindowPos(
	WinId winId, const QPoint &pos) const
{
	HWND hwnd = static_cast<HWND>(winId);
	if(!hwnd || !IsWindow(hwnd))
		return pos;

	POINT point;
	point.x = pos.x();
	point.y = pos.y();
	ScreenToClient(hwnd, &point);
	return QPoint(point.x, point.y);
}

QString WinCaptureManager::getWindowExeFilename(WinId winId) const
{
	HWND hwnd = static_cast<HWND>(winId);

	// Return cached data if it exists
	CachedInfo info;
	if(getCached(hwnd, info))
		return info.exeFilename;

	if(!hwnd || !IsWindow(hwnd))
		return QString();

	// Get process ID
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	if(processId == GetCurrentProcessId()) {
		// The process is ourself
		QFileInfo info(qApp->applicationFilePath());
		return info.fileName();
	}

	return getProcExeFilename(processId, false);
}

QString WinCaptureManager::getWindowTitle(WinId winId) const
{
	HWND hwnd = static_cast<HWND>(winId);

	// Return cached data if it exists
	CachedInfo info;
	if(getCached(hwnd, info))
		return info.windowTitle;

	if(!hwnd || !IsWindow(hwnd))
		return tr("** Unknown **");

	const int strLen = 128;
	wchar_t strBuf[strLen];

	// Get window title text
	QString title;
	strBuf[0] = TEXT('\0');
	if(GetWindowText(hwnd, strBuf, strLen) > 0)
		title = QString::fromUtf16(reinterpret_cast<const ushort *>(strBuf));
	else
		title = tr("** No title **");

	// Return the string to use
	return title;
}

QString WinCaptureManager::getWindowDebugString(WinId winId) const
{
	HWND hwnd = static_cast<HWND>(winId);
	if(!hwnd || !IsWindow(hwnd)) {
		return QStringLiteral("%1 (ID: %2)")
			.arg(tr("** Unknown **"))
			.arg(pointerToString(hwnd));
	}

	const int strLen = 128;
	wchar_t strBuf[strLen];

	// Get window class
	QString classStr;
	strBuf[0] = TEXT('\0');
	memset(strBuf, 0, sizeof(strBuf));
	if(GetClassName(hwnd, strBuf, strLen) > 0) {
		classStr =
			QString::fromUtf16(reinterpret_cast<const ushort *>(strBuf));
	} else
		classStr = tr("** No class **");

	// Get window title text
	QString title;
	strBuf[0] = TEXT('\0');
	if(GetWindowText(hwnd, strBuf, strLen) > 0)
		title = QString::fromUtf16(reinterpret_cast<const ushort *>(strBuf));
	else
		title = tr("** No title **");

	// Get process ".exe" filename
	QString filename = getWindowExeFilename(winId);

	// Return the string to use
	return QStringLiteral("[%1] %2 [%3] (ID: %4)")
		.arg(filename)
		.arg(title)
		.arg(classStr)
		.arg(pointerToString(hwnd));
}

WinId WinCaptureManager::findWindow(const QString &exe, const QString &title)
{
	cacheWindowList();

	// Do an exact search first
	for(int i = 0; i < m_cache.count(); i++) {
		const CachedInfo &info = m_cache.at(i);
		if(doWindowsMatch(
			exe, title, info.exeFilename, info.windowTitle, false))
		{
			HWND hwnd = info.hwnd;
			uncacheWindowList(); // Invalidates `info`
			return static_cast<WinId>(hwnd);
		}
	}

	// Do a fuzzy search second
	for(int i = 0; i < m_cache.count(); i++) {
		const CachedInfo &info = m_cache.at(i);
		if(doWindowsMatch(
			exe, title, info.exeFilename, info.windowTitle, true))
		{
			HWND hwnd = info.hwnd;
			uncacheWindowList(); // Invalidates `info`
			return static_cast<WinId>(hwnd);
		}
	}

	// Window not found
	uncacheWindowList();
	return NULL;
}

bool WinCaptureManager::doWindowsMatch(
	const QString &aExe, const QString &aTitle, const QString &bExe,
	const QString &bTitle, bool fuzzy)
{
	if(!fuzzy) {
		// Exact comparison:
		if(aExe == bExe && aTitle == bTitle)
			return true;
		return false;
	}
	// Fuzzy comparison:

	// Executable filenames must match
	if(aExe != bExe)
		return false;

	// Lots of applications change the window title depending on what is
	// currently displayed in the window. For example a text editor displays
	// the filename of the current file and a web browser displays the HTML
	// page title of the current tab. Sometimes the entire window title changes
	// which is the case for Windows Explorer and Cygwin. Most of the time the
	// part that remains constant is on the right-hand side of the string
	// separated from the variable portion by " - ". Sometimes there are
	// multiple " - " in the window title which is the case for Thunderbird's
	// feed reader. Some programs also display a "file modified" symbol (Which
	// most of the time is a "*") somewhere in the title. Sometimes there are
	// also version numbers in the title.

	// Do a fast exact match
	if(aTitle == bTitle)
		return true;

	// Only compare the right portion of a string with a " - " in it
	QString aStr = aTitle.split(QStringLiteral(" - ")).last();
	QString bStr = bTitle.split(QStringLiteral(" - ")).last();
	if(aStr == bStr)
		return true;

	// Remove any file modified symbols ("*")
	aStr = aStr.replace(QChar('*'), QString());
	bStr = bStr.replace(QChar('*'), QString());
	if(aStr == bStr)
		return true;

	// Remove any version numbers
	QRegExp verRegex(QStringLiteral("\\bv?[0-9]*(\\.[0-9]*)+\\b"));
	aStr = aStr.replace(verRegex, QString());
	bStr = bStr.replace(verRegex, QString());
	if(aStr == bStr)
		return true;

#if 0
	// Test if it's Windows explorer by seeing if the window title is a valid
	// directory. Note that we can't test the old title (Input "A") as the
	// directory might have been deleted.
	if(bExe == QStringLiteral("explorer.exe")) {
		QFileInfo info(bTitle);
		if(info.exists() && info.isDir()) {
			// The window title is a valid directory, test to see if the old
			// window had a directory as well
		}
	}
#endif // 0

	return true;
}

QString WinCaptureManager::getProcExeFilename(
	DWORD procId, bool fullPath) const
{
	// Open the process
	HANDLE process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procId);
	if(process == NULL)
		return QString();

	// MSDN recommends using `GetProcessImageFileName()` or
	// `QueryFullProcessImageName()` over `GetModuleFileNameEx()`
	const int strLen = 256;
	wchar_t strBuf[strLen];
	GetProcessImageFileName(process, strBuf, strLen);

	// Close the process
	CloseHandle(process);

	// We just want the ".exe" name without the path. Note that
	// `GetProcessImageFileName()` uses device form paths instead of drive
	// letters
	QString str = QString::fromUtf16(reinterpret_cast<const ushort *>(strBuf));
	if(fullPath)
		return str;
	QStringList strList = str.split(QChar('\\'));
	return strList.last();
}

BOOL CALLBACK MonitorEnumProc(
	HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	WinCaptureManager *mgr = (WinCaptureManager *)dwData;
	mgr->addMonitor(hMonitor);
	return TRUE;
}

IDXGIOutput *WinCaptureManager::getDXGIOutputForMonitor(HMONITOR handle)
{
	// WARNING: We must not mix `IDXGIFactory` and `IDXGIFactory1` in the same
	// process!
	IDXGIFactory *factory = NULL;
	IDXGIFactory1 *factory1 = NULL;
	HRESULT res = vidgfx_d3d_create_dxgifactory1_dyn(&factory1);
	if(factory1 == NULL)
		res = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory);
	if(FAILED(res))
		return NULL;

	if(factory1 != NULL) {
		// DXGI 1.1
		IDXGIAdapter1 *adapter = NULL;
		for(uint i = 0;
			factory1->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
			i++)
		{
			IDXGIOutput *output = NULL;
			for(uint j = 0;
				adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; j++)
			{
				DXGI_OUTPUT_DESC desc;
				HRESULT res = output->GetDesc(&desc);
				if(SUCCEEDED(res)) {
					if(desc.Monitor == handle) {
						// We found our monitor, clean up and return
						adapter->Release();
						factory1->Release();
						return output;
					}
				}
				output->Release();
			}
			adapter->Release();
		}
	} else {
		// DXGI 1.0
		IDXGIAdapter *adapter = NULL;
		for(uint i = 0;
			factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND;
			i++)
		{
			IDXGIOutput *output = NULL;
			for(uint j = 0;
				adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; j++)
			{
				DXGI_OUTPUT_DESC desc;
				HRESULT res = output->GetDesc(&desc);
				if(SUCCEEDED(res)) {
					if(desc.Monitor == handle) {
						// We found our monitor, clean up and return
						adapter->Release();
						factory->Release();
						return output;
					}
				}
				output->Release();
			}
			adapter->Release();
		}
	}

	if(factory1 != NULL)
		factory1->Release();
	if(factory != NULL)
		factory->Release();

	// Output not found
	return NULL;
}

void WinCaptureManager::addMonitor(HMONITOR handle)
{
	MONITORINFOEX monInfo;
	monInfo.cbSize = sizeof(monInfo);
	if(GetMonitorInfo(handle, &monInfo) == 0)
		return;

	MonitorInfo info;
	info.handle = static_cast<MonitorId>(handle);
	info.rect = QRect(
		monInfo.rcMonitor.left, monInfo.rcMonitor.top,
		monInfo.rcMonitor.right - monInfo.rcMonitor.left,
		monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
	info.isPrimary = monInfo.dwFlags & MONITORINFOF_PRIMARY;
	info.deviceName =
		QString::fromUtf16(reinterpret_cast<const ushort *>(monInfo.szDevice));
	info.friendlyId = 0; // Set below
	if(m_deviceToFriendlyMap.contains(info.deviceName))
		info.friendlyName = m_deviceToFriendlyMap[info.deviceName];
	else
		info.friendlyName = info.deviceName;

	// Determine monitor ID from device name which is always in the format
	// "\\.\DISPLAY__" or "\\.\DISPLAYV__" (Virtual/mirror displays)
	QString idStr = info.deviceName.replace(
		QStringLiteral("\\\\.\\DISPLAY"), QString());
	bool ok = false;
	info.friendlyId = idStr.toUInt(&ok);
	if(!ok)
		info.friendlyId = m_unknownMonitorId++; // ID should be unique

	// Get the `IDXGIOutput` that matches this monitor
	info.extra = getDXGIOutputForMonitor(handle);
	//if(info.extra != NULL)
	//	capLog() << "Output found for monitor " << info.friendlyId;

	m_monitors.append(info);
}

void WinCaptureManager::updateMonitorInfo(bool emitSignal)
{
	// Generate the device name to adapter and monitor name map which is used
	// to converts from "\\.\DISPLAY1" to
	// "BenQ FP241W (Digital) (ATI Radeon HD 5700 Series)".
	DISPLAY_DEVICE dev, dev2;
	dev.cb = sizeof(dev);
	dev2.cb = sizeof(dev2);
	m_deviceToFriendlyMap.clear();
	capLog() << QStringLiteral("Available display devices:");
	for(int i = 0; EnumDisplayDevices(NULL, i, &dev, 0); i++) {
		if(dev.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
			continue; // Skip mirror drivers
		EnumDisplayDevices(dev.DeviceName, 0, &dev2, 0);

		// Matches `MONITORINFOEX::szDevice`
		QString devName = QString::fromUtf16(
			reinterpret_cast<const ushort *>(dev.DeviceName));

		// Adapter name
		QString devStr = QString::fromUtf16(
			reinterpret_cast<const ushort *>(dev.DeviceString));

		// Monitor name
		QString monStr = QString::fromUtf16(
			reinterpret_cast<const ushort *>(dev2.DeviceString));

		if(monStr.isEmpty())
			monStr = tr("** No monitor **");
		m_deviceToFriendlyMap[devName] =
			QStringLiteral("%1 (%2)").arg(monStr).arg(devStr);
		capLog() << QStringLiteral("  - [%1] %2")
			.arg(devName).arg(m_deviceToFriendlyMap[devName]);
	}

	// Cleanly empty the monitor list
	for(int i = 0; i < m_monitors.size(); i++) {
		void *extraPtr = m_monitors.at(i).extra;
		if(extraPtr != NULL)
			static_cast<IDXGIOutput *>(extraPtr)->Release();
	}
	m_monitors.clear();

	// We don't use QDesktopWidget to get the list of monitors as we want
	// additional information such as the device name
	m_unknownMonitorId = 100;
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)this);
	capLog() << QStringLiteral("Connected monitors:");
	for(int i = 0; i < m_monitors.count(); i++) {
		const MonitorInfo &info = m_monitors.at(i);
		capLog() << QStringLiteral("  - [%1] \"%2\" at ")
			.arg(info.friendlyId)
			.arg(info.friendlyName)
			<< info.rect;
	}

	// We don't want to emit the signal on initialization
	if(emitSignal)
		emit monitorInfoChanged();
}

void WinCaptureManager::lowJitterRealTimeFrameEventImpl(
	int numDropped, int lateByUsec)
{
	// Notify GDI capture objects
	for(int i = 0; i < m_gdiObjects.count(); i++) {
		m_gdiObjects.at(i)->lowJitterRealTimeFrameEvent(
			numDropped, lateByUsec);
	}

	// Notify duplicator capture objects
	for(int i = 0; i < m_dupObjects.count(); i++) {
		m_dupObjects.at(i)->lowJitterRealTimeFrameEvent(
			numDropped, lateByUsec);
	}
}

void WinCaptureManager::realTimeFrameEventImpl(int numDropped, int lateByUsec)
{
}

void WinCaptureManager::queuedFrameEventImpl(uint frameNum, int numDropped)
{
	// Notify other capture objects
	for(int i = 0; i < m_hookObjects.count(); i++)
		m_hookObjects.at(i)->queuedFrameEvent(frameNum, numDropped);
}

void WinCaptureManager::graphicsContextInitialized(VidgfxContext *gfx)
{
	if(!vidgfx_context_is_valid(gfx))
		return; // Context must exist and be useable

	// Notify capture objects
	for(int i = 0; i < m_gdiObjects.count(); i++)
		m_gdiObjects.at(i)->initializeResources(gfx);
	for(int i = 0; i < m_hookObjects.count(); i++)
		m_hookObjects.at(i)->initializeResources(gfx);
	for(int i = 0; i < m_dupObjects.count(); i++)
		m_dupObjects.at(i)->initializeResources(gfx);
}

void WinCaptureManager::graphicsContextDestroyed(VidgfxContext *gfx)
{
	if(!vidgfx_context_is_valid(gfx))
		return; // Context must exist and be useable

	// Notify capture objects
	for(int i = 0; i < m_gdiObjects.count(); i++)
		m_gdiObjects.at(i)->destroyResources(gfx);
	for(int i = 0; i < m_hookObjects.count(); i++)
		m_hookObjects.at(i)->destroyResources(gfx);
	for(int i = 0; i < m_dupObjects.count(); i++)
		m_dupObjects.at(i)->destroyResources(gfx);
}

void WinCaptureManager::updateMonitorInfoSlot()
{
	updateMonitorInfo(true);
}
