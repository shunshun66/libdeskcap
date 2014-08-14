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

#include "include/capturemanager.h"
#include "include/caplog.h"
#include "hookmanager.h"
#ifdef Q_OS_WIN
#include "wincapturemanager.h"
#endif
#include "../Common/datatypes.h"
#include "../Common/mainsharedsegment.h"
#include <Libvidgfx/graphicscontext.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>

CaptureManager *CaptureManager::s_singleton = NULL;

/// <summary>
/// Creates an instance of the capture manager singleton or gets the existing
/// instance if one exists. Initializing also starts any required external
/// helper processes.
/// </summary>
CaptureManager *CaptureManager::initializeManager()
{
	if(s_singleton != NULL)
		return s_singleton;
#ifdef Q_OS_WIN
	s_singleton = new WinCaptureManager();
#else
	s_singleton = NULL;
#endif
	if(s_singleton != NULL && !s_singleton->initialize()) {
		// Failed to initialize properly
		delete s_singleton;
		s_singleton = NULL;
	}
	return s_singleton;
}

CaptureManager *CaptureManager::getManager()
{
	return s_singleton;
}

/// <summary>
/// Cleanly destroys the capture manager singleton if it exists.
/// </summary>
void CaptureManager::destroyManager()
{
	if(s_singleton == NULL)
		return;
	delete s_singleton;
	s_singleton = NULL;
}

CaptureManager::CaptureManager()
	: QObject()
	, m_gfxContext(NULL)
	, m_hookManager(NULL)
	, m_monitors()
	, m_lowJitterModeRef(0)

	// Helpers
	, m_helper32(this)
	, m_helper64(this)
	, m_helper32Buf()
	, m_helper64Buf()
	, m_helper32NormalExit(false)
	, m_helper64NormalExit(false)
{
	// Allocate memory
	m_helper32Buf.reserve(16);
	m_helper64Buf.reserve(16);
}

CaptureManager::~CaptureManager()
{
	// Exit low jitter mode if we're still in it
	while(m_lowJitterModeRef)
		derefLowJitterMode();

	// Destroy hook manager
	delete m_hookManager;
	m_hookManager = NULL;

	// Make sure that the helpers are always terminated
	terminateHelpers();
}

bool CaptureManager::initialize()
{
	// Create hook manager
	m_hookManager = new HookManager();
	if(!m_hookManager->initialize())
		return false;

	// Flush any existing interprocess log messages as they are from a previous
	// execution of Libdeskcap and would just confuse people who are reading
	// the log file
	m_hookManager->processInterprocessLog(false);

	// Start helper processes
	if(!startHelper(false)) // 32-bit helper
		return false;
	else if(!startHelper(true)) // 64-bit helper
		return false;

	// Platform-specific initialization
	if(!initializeImpl())
		return false;

	// The above initialization automatically hooks processes the moment it is
	// called. Flush the interprocess log immediately.
	m_hookManager->processInterprocessLog();

	return true;
}

/// <summary>
/// Sets the main graphics context that the manager will use for graphics
/// processing and for returning captured textures.
/// </summary>
void CaptureManager::setGraphicsContext(GraphicsContext *gfx)
{
	m_gfxContext = gfx;
	if(gfx == NULL)
		return;

	// Forward to hook manager
	HookManager::doGraphicsContextInitialized(gfx);

	// Forward to specific subclasses. TODO: Move to appropriate subclasses
#ifdef Q_OS_WIN
	WinCaptureManager *mgr = static_cast<WinCaptureManager *>(this);
	if(gfx->isValid())
		mgr->graphicsContextInitialized(gfx);
	else {
		connect(gfx, &GraphicsContext::initialized,
			mgr, &WinCaptureManager::graphicsContextInitialized);
	}
	connect(gfx, &GraphicsContext::destroying,
		mgr, &WinCaptureManager::graphicsContextInitialized);
#endif
}

const MonitorInfo *CaptureManager::getMonitorInfo(MonitorId id) const
{
	if(id == NULL)
		return NULL;
	for(int i = 0; i < m_monitors.size(); i++) {
		const MonitorInfo *info = &(m_monitors.at(i));
		if(static_cast<HMONITOR>(info->handle) == id)
			return info;
	}
	return NULL;
}

QPoint CaptureManager::mapScreenToMonitorPos(
	MonitorId id, const QPoint &pos) const
{
	const MonitorInfo *info = getMonitorInfo(id);
	if(info == NULL)
		return pos;
	return pos - info->rect.topLeft();
}

bool CaptureManager::getFuzzyCapture() const
{
	MainSharedSegment *shm = m_hookManager->getMainSharedSegment();
	if(shm == NULL)
		return false;
	return shm->getFuzzyCapture();
}

void CaptureManager::setFuzzyCapture(bool useFuzzyCap)
{
	MainSharedSegment *shm = m_hookManager->getMainSharedSegment();
	if(shm == NULL)
		return;
	shm->setFuzzyCapture(useFuzzyCap);
}

uint CaptureManager::getVideoFrequencyNum()
{
	MainSharedSegment *shm = m_hookManager->getMainSharedSegment();
	if(shm == NULL)
		return 0;
	return (uint)shm->getVideoFrequencyNum();
}

uint CaptureManager::getVideoFrequencyDenom()
{
	MainSharedSegment *shm = m_hookManager->getMainSharedSegment();
	if(shm == NULL)
		return 0;
	return (uint)shm->getVideoFrequencyDenom();
}

void CaptureManager::setVideoFrequency(uint numerator, uint denominator)
{
	MainSharedSegment *shm = m_hookManager->getMainSharedSegment();
	if(shm == NULL)
		return;
	shm->setVideoFrequency((uint32_t)numerator, (uint32_t)denominator);
}

void CaptureManager::refLowJitterMode()
{
	m_lowJitterModeRef++;
	if(m_lowJitterModeRef == 1)
		emit enterLowJitterMode();
}

void CaptureManager::derefLowJitterMode()
{
	if(m_lowJitterModeRef > 0) {
		m_lowJitterModeRef--;
		if(m_lowJitterModeRef == 0)
			emit exitLowJitterMode();
	}
}

/// <summary>
/// Issues a command to the specified helper and blocks processing until it
/// receives the full result.
/// </summary>
/// <returns>
/// The result of the command with any prefixes and terminators removed.
/// </returns>
QVector<QStringList> CaptureManager::doHelperCommand(
	bool is64, const QString &msg, bool isMultiline)
{
	QVector<QStringList> res;

	QProcess *proc = (is64 ? &m_helper64 : &m_helper32);
	QVector<QStringList> *buf = (is64 ? &m_helper64Buf : &m_helper32Buf);

	// Get command name from message
	if(msg.isEmpty())
		return res;
	QString cmd = msg;
	int index = msg.indexOf(QChar(' '));
	if(index > 0)
		cmd = msg.mid(0, index);
	if(cmd.isEmpty())
		return res;

	// Is the helper still running?
	if(proc->state() != QProcess::Running) {
#if 0
		if(is64) {
			capLog(CapLog::Warning)
				<< QStringLiteral("64-bit helper not running, cannot execute command");
		} else {
			capLog(CapLog::Warning)
				<< QStringLiteral("32-bit helper not running, cannot execute command");
		}
#endif // 0
		return res;
	}

	// Send the message to the helper process
	proc->write(msg.toUtf8() + "\n");

	// Wait for the full reply. TODO: There is no timeout for this!
	for(;;) {
		// Do we have a reply and, if it's a multiline response, a terminator?
		bool gotReply = false;
		for(int i = 0; i < buf->size(); i++) {
			const QStringList &line = buf->at(i);
			if(line.size() < 1)
				continue;
			if(line.at(0) == QStringLiteral("error")) {
				// Received an error, immediately cancel
				capLog(CapLog::Warning)
					<< QStringLiteral("Received error message from helper: \"%1\"")
					.arg(line.at(1));
				buf->remove(i);
				return res;
			}
			if(line.at(0) != cmd)
				continue;
			// It's our reply
			if(!isMultiline) {
				// Is a single line reply
				gotReply = true;
				break;
			}
			if(line.size() < 2)
				continue;
			if(line.at(1) == QStringLiteral("end")) {
				// Got multiline terminator
				gotReply = true;
				break;
			}
		}
		if(gotReply)
			break;
		proc->waitForReadyRead(100); // Short timeout just in case we missed it
	}

	// Process reply. We remove all prefixes and the ending terminator if there
	// is one.
	for(int i = 0; i < buf->size(); i++) {
		const QStringList &line = buf->at(i);
		if(line.size() < 1)
			continue;
		if(line.at(0) != cmd)
			continue;
		// It's our reply, move it from the buffer to our return variable
		res.append(line);
		buf->remove(i);
		i--;
		res[res.size()-1].pop_front(); // Remove prefix
	}
	if(isMultiline) {
		// The last line should always be our terminator
		res.pop_back();
	}

	return res;
}

/// <summary>
/// Starts either the 32- or 64-bit helper process.
/// </summary>
/// <returns>True if the helper successfully started</returns>
bool CaptureManager::startHelper(bool is64)
{
	QProcess *proc = (is64 ? &m_helper64 : &m_helper32);
	QVector<QStringList> *buf = (is64 ? &m_helper64Buf : &m_helper32Buf);

	if(proc->state() != QProcess::NotRunning)
		return true; // Already running

	// Truncate input buffer
	buf->clear();

	// Our helpers and hooks have different filenames in debug builds
#ifdef QT_DEBUG
	QString strDebug = QStringLiteral("d");
#else
	QString strDebug = QString();
#endif

	// Calculate bitness-specific variables
	int bits;
	QString exeStr;
	QString hookStr;
	QString hookShortStr;
	QString expect;
	void (CaptureManager:: *finSig)(int, QProcess::ExitStatus);
	if(is64) {
		bits = 64;
		exeStr = QStringLiteral("%1/MishiraHelper64%2.exe")
			.arg(qApp->applicationDirPath())
			.arg(strDebug);
		hookStr = QStringLiteral("%1/MishiraHook64%2.dll")
			.arg(qApp->applicationDirPath())
			.arg(strDebug);
		hookShortStr = QStringLiteral("mishirahook64%1.dll")
			.arg(strDebug);
		expect = QStringLiteral("ready %1 64").arg(HELPER_PROTOCOL_VERSION);
		finSig = &CaptureManager::helper64Finished;
	} else {
		bits = 32;
		exeStr = QStringLiteral("%1/MishiraHelper%2.exe")
			.arg(qApp->applicationDirPath())
			.arg(strDebug);
		hookStr = QStringLiteral("%1/MishiraHook%2.dll")
			.arg(qApp->applicationDirPath())
			.arg(strDebug);
		hookShortStr = QStringLiteral("mishirahook%1.dll")
			.arg(strDebug);
		expect = QStringLiteral("ready %1 32").arg(HELPER_PROTOCOL_VERSION);
		finSig = &CaptureManager::helper32Finished;
	}
	hookStr = QDir::toNativeSeparators(hookStr);

	// Reset signals
	void (QProcess:: *fpiesP)(int, QProcess::ExitStatus) = &QProcess::finished;
	connect(proc, fpiesP, this, finSig, Qt::UniqueConnection);
	disconnect(proc, &QIODevice::readyRead,
		this, &CaptureManager::helperReadyRead);

	// Do handshake
	QStringList args;
	args << QStringLiteral("start");
	proc->start(exeStr, args);
	if(!proc->waitForStarted(10000)) {
		if(is64) {
			// Semi-HACK: If the 64-bit launcher fails to launch then just
			// assume that we are on a 32-bit system.
			capLog(CapLog::Warning)
				<< QStringLiteral("%1-bit helper process failed to start, skipping")
				.arg(bits);
			return true;
		} else {
			capLog(CapLog::Critical)
				<< QStringLiteral("%1-bit helper process failed to start, cannot continue")
				.arg(bits);
			return false;
		}
	}
	if(!waitForReadLine(proc, 3000)) {
		capLog(CapLog::Critical)
			<< QStringLiteral("%1-bit helper process did not handshake correctly, cannot continue.")
			.arg(bits);
		return false;
	}
	QString handshake = QString::fromUtf8(proc->readLine().trimmed());
	if(handshake != expect) {
		capLog(CapLog::Critical)
			<< QStringLiteral("%1-bit helper process did not handshake correctly, cannot continue. Replied \"%2\"")
			.arg(bits).arg(handshake);
		return false;
	}
	connect(proc, &QIODevice::readyRead,
		this, &CaptureManager::helperReadyRead, Qt::UniqueConnection);
	proc->write("ready\n");

	// Define the location of our hook. We do this here instead of in the
	// helper as we have Qt's more powerful string library
	doHelperCommand(is64,
		QStringLiteral("setHookDll %1 startHook %2")
		.arg(hookShortStr).arg(hookStr));

	return true;
}

void CaptureManager::terminateHelpers()
{
	if(m_helper32.state() == QProcess::Running) {
		m_helper32NormalExit = true;
		m_helper32.write("quit\n");
		if(!m_helper32.waitForFinished(3000)) {
			capLog(CapLog::Warning)
				<< QStringLiteral("32-bit helper process did not terminate cleanly, killing");
			m_helper32.kill();
		}
		m_helper32NormalExit = false;
	}
	if(m_helper64.state() == QProcess::Running) {
		m_helper64NormalExit = true;
		m_helper64.write("quit\n");
		if(!m_helper64.waitForFinished(3000)) {
			capLog(CapLog::Warning)
				<< QStringLiteral("64-bit helper process did not terminate cleanly, killing");
			m_helper64.kill();
		}
		m_helper64NormalExit = false;
	}
}

/// <summary>
/// Blocks execution until `dev` has a full line available to be read or until
/// the specified time passes.
/// </summary>
/// <returns>True if a full line is available to be read</returns>
bool CaptureManager::waitForReadLine(QIODevice *dev, uint msecs)
{
	if(dev->peek(dev->bytesAvailable()).count('\n') > 0)
		return true;
	QTimer timer;
	timer.start(msecs);
	while(timer.isActive()) {
		int time = timer.remainingTime();
		dev->waitForReadyRead(time > -1 ? time : 0);
		if(dev->peek(dev->bytesAvailable()).count('\n') > 0)
			return true;
	}
	return false;
}

void CaptureManager::readHelperMessages(QIODevice *dev)
{
	// Is it the 32- or 64-bit helper?
	bool is64 = (dev == &m_helper64);

	// Continue processing messages for as long as there are full lines to read
	while(dev->peek(dev->bytesAvailable()).count('\n') > 0) {
		QString msg = QString::fromUtf8(dev->readLine().trimmed());
		QStringList args = msg.split(QChar(' '));

		// Process message
		if(args.at(0) == QStringLiteral("log")) {
			args.pop_front();

			// Determine level
			QString lvlStr = args.takeFirst();
			CapLog::LogLevel lvl;
			if(lvlStr == QStringLiteral("notice"))
				lvl = CapLog::Notice;
			else if(lvlStr == QStringLiteral("warning"))
				lvl = CapLog::Warning;
			else // "critical"
				lvl = CapLog::Critical;

			QString cat = (is64 ? QStringLiteral("Helper64") :
				QStringLiteral("Helper32"));
			capLog(cat, lvl) << args.join(" ");
		} else {
			// Unknown message, most likely a reply, append to buffer
			if(is64)
				m_helper64Buf.append(args);
			else
				m_helper32Buf.append(args);
		}
	}
}

void CaptureManager::helperReadyRead()
{
	readHelperMessages(&m_helper32);
	readHelperMessages(&m_helper64);
}

void CaptureManager::doHelperFinished(
	bool is64, int exitCode, QProcess::ExitStatus exitStatus)
{
	int bits = (is64 ? 64 : 32);
	bool normalExit = (is64 ? m_helper64NormalExit : m_helper32NormalExit);

	if(normalExit) {
		if(exitStatus == QProcess::NormalExit) {
			capLog()
				<< QStringLiteral("%1-bit helper process terminated normally with exit code %2")
				.arg(bits).arg(exitCode);
		} else {
			capLog(CapLog::Warning)
				<< QStringLiteral("%1-bit helper process crashed (%2) when terminating normally")
				.arg(bits).arg(exitCode);
		}
		return;
	}
	if(exitStatus == QProcess::NormalExit) {
		capLog(CapLog::Warning)
			<< QStringLiteral("%1-bit helper process terminated abnormally with exit code %2, restarting")
			.arg(bits).arg(exitCode);
	} else {
		capLog(CapLog::Critical)
			<< QStringLiteral("%1-bit helper process crashed (%2), restarting")
			.arg(bits).arg(exitCode);
	}
	startHelper(is64);
}

void CaptureManager::helper32Finished(
	int exitCode, QProcess::ExitStatus exitStatus)
{
	doHelperFinished(false, exitCode, exitStatus);
}

void CaptureManager::helper64Finished(
	int exitCode, QProcess::ExitStatus exitStatus)
{
	doHelperFinished(true, exitCode, exitStatus);
}

void CaptureManager::lowJitterRealTimeFrameEvent(
	int numDropped, int lateByUsec)
{
	lowJitterRealTimeFrameEventImpl(numDropped, lateByUsec);
}

void CaptureManager::realTimeFrameEvent(int numDropped, int lateByUsec)
{
	realTimeFrameEventImpl(numDropped, lateByUsec);

	// HACK: Forward to hook manager instead of normal slot connection
	m_hookManager->realTimeFrameEvent(numDropped, lateByUsec);
}

void CaptureManager::queuedFrameEvent(uint frameNum, int numDropped)
{
	queuedFrameEventImpl(frameNum, numDropped);
}
