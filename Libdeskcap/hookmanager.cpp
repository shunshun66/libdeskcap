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

#include "hookmanager.h"
#include "include/caplog.h"
#include "include/capturemanager.h"
#include "../Common/interprocesslog.h"
#include "../Common/mainsharedsegment.h"
#include <Libvidgfx/d3dcontext.h>

const QString LOG_CAT = QStringLiteral("Hooking");

void HookManager::doGraphicsContextInitialized(GraphicsContext *gfx)
{
	if(gfx == NULL)
		return; // Extra safe

	// Forward the signal to our instance
	HookManager *mgr = CaptureManager::getManager()->getHookManager();
	if(gfx->isValid())
		mgr->graphicsContextInitialized(gfx);
	else {
		connect(gfx, &GraphicsContext::initialized,
			mgr, &HookManager::graphicsContextInitialized);
	}
	connect(gfx, &GraphicsContext::destroying,
		mgr, &HookManager::graphicsContextInitialized);
}

HookManager::HookManager()
	: QObject()
	, m_shm(NULL)
	, m_interprocessLog(NULL)
	, m_knownWindows()
	, m_capturingWindows()
{
	m_knownWindows.reserve(16);
	m_capturingWindows.reserve(16);

	// Make sure we know when we can initialize or destroy hardware resources
	// for our child capture objects
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx != NULL) {
		if(gfx->isValid())
			graphicsContextInitialized(gfx);
		else {
			connect(gfx, &GraphicsContext::initialized,
				this, &HookManager::graphicsContextInitialized);
		}
		connect(gfx, &GraphicsContext::destroying,
			this, &HookManager::graphicsContextDestroyed);
	}
}

HookManager::~HookManager()
{
	// Delete memory segment
	if(m_shm != NULL) {
		// Notify hooks that they should terminate
		m_shm->setProcessRunning(false);

		delete m_shm;
		m_shm = NULL;
	}
}

bool HookManager::initialize()
{
	//-------------------------------------------------------------------------
	// Create a named shared memory segment so we can pass messages to and from
	// our installed hooks if we have any. Actually pixel data from hooks do
	// not get sent over this segment as it would require creating a very large
	// memory allocation at all times which is extremely wasteful if not
	// required.
	//
	// We use Boost instead of Qt as we don't want to have to pull Qt into our
	// hooks. Our wrapper class uses the native Windows shared memory system
	// when building on Windows and the standard UNIX-like shared memory on all
	// other platforms. Windows shared memory is not persistent while UNIX
	// shared memory is persistent until it is explicitly deleted. Because of
	// persistence we need to test if the process ID that we place in the
	// segment actually refers to a Mishira instance and is not ourselves.

	// Open or create shared memory segment
	m_shm = new MainSharedSegment();
	if(!m_shm->isValid()) {
		// An unexpected error occured
		capLog(LOG_CAT, CapLog::Critical) << QStringLiteral(
			"Failed to open or create shared memory segment. Reason: %1")
			.arg(QString::fromLatin1(m_shm->getErrorReason().data()));

		delete m_shm;
		m_shm = NULL;
		return false;
	}

	// Fetch interprocess log object
	m_interprocessLog = m_shm->getInterprocessLog();
	if(m_interprocessLog == NULL) {
		// Something went wrong
		capLog(LOG_CAT, CapLog::Critical) << QStringLiteral(
			"Failed to fetch the interprocess log");
		delete m_shm;
		m_shm = NULL;
		return false;
	}

	// Notify hooks that we are now managing the shared memory
	m_shm->setProcessRunning(true);

	//-------------------------------------------------------------------------

	return true;
}

bool HookManager::isWindowKnown(WinId win) const
{
	for(int i = 0; i < m_knownWindows.size(); i++) {
		if(m_knownWindows.at(i).winId == win)
			return true;
	}
	return false;
}

bool HookManager::isWindowCapturing(WinId win) const
{
	for(int i = 0; i < m_capturingWindows.size(); i++) {
		if(m_capturingWindows.at(i) == win)
			return true;
	}
	return false;
}

void HookManager::refWindowHooked(WinId win)
{
	refDerefWindowHooked(win, true);
}

void HookManager::derefWindowHooked(WinId win)
{
	refDerefWindowHooked(win, false);
}

void HookManager::refDerefWindowHooked(WinId win, bool capture)
{
	m_shm->lockHookRegistry();

	// Get registry entry
	HookRegEntry *entry =
		m_shm->findWindowInHookRegistry(reinterpret_cast<uint32_t>(win));
	if(entry == NULL) {
		// Not found in registry
		m_shm->unlockHookRegistry();
		return;
	}

	// Get known structure
	KnownWin *known = NULL;
	for(int i = 0; i < m_knownWindows.size(); i++) {
		if(m_knownWindows.at(i).winId == win) {
			known = &m_knownWindows[i];
			break;
		}
	}
	if(known == NULL) {
		// Unknown window
		m_shm->unlockHookRegistry();
		return;
	}

	if(capture) {
		known->captureRef++;
		if(known->captureRef == 1) {
			// Begin capturing
			entry->flags |= HookRegEntry::CaptureFlag;
		}
	} else {
		if(known->captureRef == 1) {
			// End capturing
			entry->flags &= ~HookRegEntry::CaptureFlag;
		}
		if(known->captureRef > 0)
			known->captureRef--;
	}

	m_shm->unlockHookRegistry();
}

/// <summary>
/// Check the interprocess log for messages and process them if there is any.
/// </summary>
void HookManager::processInterprocessLog(bool output)
{
	if(m_interprocessLog == NULL)
		return;
	vector<InterprocessLog::LogData> msgs = m_interprocessLog->emptyLog();
	if(msgs.empty() || !output)
		return;
	for(uint i = 0; i < msgs.size(); i++) {
		// Map log level
		CapLog::LogLevel lvl;
		switch(msgs[i].lvl) {
		case InterprocessLog::Notice:
			lvl = CapLog::Notice;
			break;
		case InterprocessLog::Warning:
			lvl = CapLog::Warning;
			break;
		default:
		case InterprocessLog::Critical:
			lvl = CapLog::Critical;
			break;
		}

		// Convert C-strings to QStrings
		QString cat = QString::fromUtf8(msgs[i].cat);
		QString msg = QString::fromUtf8(msgs[i].msg);

		// Output to our log
		capLog(cat, lvl) << msg;
	}
}

/// <summary>
/// Poll the hook registry for changes and emit the required signals if it has.
/// </summary>
void HookManager::processRegistry()
{
	CaptureManager *capMgr = CaptureManager::getManager();

	// To reduce the chance of interprocess deadlocks we emit our signals
	// outside of the lock.
	QVector<WinId> emitHooked;
	QVector<WinId> emitUnhooked;
	QVector<WinId> emitReset;
	QVector<WinId> emitStartedCapturing;
	QVector<WinId> emitStoppedCapturing;
	if(!m_shm->lockHookRegistry(5)) {
		// Failed to lock registry, this most likely means something is broken!
		capLog(LOG_CAT, CapLog::Warning)
			<< QStringLiteral("Failed to lock hook registry, possible crash");
		return;
	}

	// Find new windows
	uint numEntries = 0;
	HookRegEntry *entries = m_shm->iterateHookRegistry(numEntries);
	for(uint i = 0; i < numEntries; i++) {
		HookRegEntry *entry = &entries[i];
		WinId winId = reinterpret_cast<WinId>(entry->winId);
		bool found = false;
		for(int j = 0; j < m_knownWindows.size(); j++) {
			if(winId == m_knownWindows.at(j).winId) {
				found = true;
				break;
			}
		}
		if(!found) {
			capLog(LOG_CAT)
				<< QStringLiteral("Window \"%1\" is available for accelerated capture")
				.arg(capMgr->getWindowDebugString(winId));
			KnownWin known;
			known.winId = winId;
			known.captureRef = 0;
			m_knownWindows.append(known);
			emitHooked.append(winId);
		}
	}

	// Find removed windows
	for(int i = 0; i < m_knownWindows.size(); i++) {
		WinId winId = m_knownWindows.at(i).winId;
		uint32_t winId32 = reinterpret_cast<uint32_t>(winId);
		bool found = false;
		for(uint j = 0; j < numEntries; j++) {
			if(winId32 == entries[j].winId) {
				found = true;
				break;
			}
		}
		if(!found) {
			if(isWindowCapturing(winId)) {
				// Must be emitted before `windowUnhooked()`
				capLog(LOG_CAT)
					<< QStringLiteral("Window \"%1\" has stopped capturing")
					.arg(capMgr->getWindowDebugString(winId));
				int index = m_capturingWindows.indexOf(winId);
				m_capturingWindows.remove(index);
				emitStoppedCapturing.append(winId);
			}
			capLog(LOG_CAT)
				<< QStringLiteral("Window \"%1\" is no longer available for accelerated capture")
				.arg(capMgr->getWindowDebugString(winId));
			m_knownWindows.remove(i);
			i--;
			emitUnhooked.append(winId);
		}
	}

	// Detect when windows have begun or stopped capturing or when an existing
	// window capture has been reset (Most likely due to changing size)
	for(uint i = 0; i < numEntries; i++) {
		HookRegEntry *entry = &entries[i];
		WinId winId = reinterpret_cast<WinId>(entry->winId);

		// SHM reset signal
		if(entry->flags & HookRegEntry::ShmResetFlag) {
			capLog(LOG_CAT)
				<< QStringLiteral("Window \"%1\" has reset capturing")
				.arg(capMgr->getWindowDebugString(winId));
			entry->flags &= ~HookRegEntry::ShmResetFlag; // Clear flag
			emitReset.append(winId);
		}

		// Start/stop capturing signal
		bool isCapturing = (entry->flags & HookRegEntry::ShmValidFlag);
		bool wasCapturing = isWindowCapturing(winId);
		if(isCapturing == wasCapturing)
			continue; // No change
		if(isCapturing) {
			// Started capturing
			capLog(LOG_CAT)
				<< QStringLiteral("Window \"%1\" has started capturing")
				.arg(capMgr->getWindowDebugString(winId));
			m_capturingWindows.append(winId);
			emitStartedCapturing.append(winId);
		} else {
			// Stopped capturing
			capLog(LOG_CAT)
				<< QStringLiteral("Window \"%1\" has stopped capturing")
				.arg(capMgr->getWindowDebugString(winId));
			int index = m_capturingWindows.indexOf(winId);
			m_capturingWindows.remove(index);
			emitStoppedCapturing.append(winId);
		}
	}

	m_shm->unlockHookRegistry();

	// Emit signals outside of the lock to help prevent deadlocks
	for(int i = 0; i < emitHooked.size(); i++)
		emit windowHooked(emitHooked.at(i));
	for(int i = 0; i < emitUnhooked.size(); i++)
		emit windowUnhooked(emitUnhooked.at(i));
	for(int i = 0; i < emitReset.size(); i++)
		emit windowReset(emitReset.at(i));
	for(int i = 0; i < emitStartedCapturing.size(); i++)
		emit windowStartedCapturing(emitStartedCapturing.at(i));
	for(int i = 0; i < emitStoppedCapturing.size(); i++)
		emit windowStoppedCapturing(emitStoppedCapturing.at(i));
}

void HookManager::realTimeFrameEvent(int numDropped, int lateByUsec)
{
	processRegistry();
	processInterprocessLog();
}

void HookManager::graphicsContextInitialized(GraphicsContext *gfx)
{
#ifdef Q_OS_WIN
	D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);
	connect(d3dGfx, &D3DContext::hasDxgi11Changed,
		this, &HookManager::hasDxgi11Changed);
	connect(d3dGfx, &D3DContext::hasBgraTexSupportChanged,
		this, &HookManager::hasBgraTexSupportChanged);
#elif
#error Unsupported platform
#endif
}

void HookManager::graphicsContextDestroyed(GraphicsContext *gfx)
{
}

void HookManager::hasDxgi11Changed(bool hasDxgi11)
{
	// Notify any hooks that the system has DXGI 1.1 so they don't need to test
	// it themselves
	m_shm->setHasDxgi11(hasDxgi11);
}

void HookManager::hasBgraTexSupportChanged(bool hasBgraTexSupport)
{
	// Notify any hooks that the system has BGRA texture support so they don't
	// need to test it themselves
	m_shm->setHasBgraTexSupport(hasBgraTexSupport);
}
