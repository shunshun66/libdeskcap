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

#include "mainsharedsegment.h"
#include "interprocesslog.h"
#include "managedsharedmemory.h"

MainSharedSegment::MainSharedSegment()
	: m_shm(NULL)
	, m_isValid(false)
	, m_errorReason()

	// Data
	, m_processRunning(NULL)
	, m_videoFreqNum(NULL)
	, m_videoFreqDenom(NULL)
	, m_hasDxgi11(NULL)
	, m_hasBgraTexSupport(NULL)
	, m_fuzzyCapture(NULL)
	, m_interprocessLog(NULL)
	, m_hookRegistry(NULL)
{
	try {
		m_shm = new ManagedSharedMemory("LibdeskcapSHM", SEGMENT_SIZE);

		// Add a version number to the very beginning of the shared segment so
		// that we can detect when we've upgraded Libdeskcap on OS's that have
		// persistent shared segments and the segment has a different format.
		uchar *version = m_shm->unserialize<uchar>();
		if(*version > 1) {
			m_errorReason = "Unknown version number";
			return;
		}
		*version = 1;

		// Get the addresses of our shared objects
		m_processRunning = m_shm->unserialize<char>();
		m_videoFreqNum = m_shm->unserialize<uint32_t>();
		m_videoFreqDenom = m_shm->unserialize<uint32_t>();
		m_hasDxgi11 = m_shm->unserialize<char>();
		m_hasBgraTexSupport = m_shm->unserialize<char>();
		m_fuzzyCapture = m_shm->unserialize<char>();
		m_interprocessLog = m_shm->unserialize<InterprocessLog>();
		m_hookRegistry = m_shm->unserialize<HookRegistry>();

		m_isValid = true;
	} catch(interprocess_exception &ex) {
		m_errorReason = string(ex.what());
	}
}

MainSharedSegment::~MainSharedSegment()
{
	// Free the shared memory segment manager. Note that this doesn't actually
	// delete the segment as it's persistent.
	if(m_shm != NULL)
		delete m_shm;
}

bool MainSharedSegment::getProcessRunning()
{
	if(m_processRunning == NULL)
		return false;
	return (*m_processRunning != 0) ? true : false;
}

void MainSharedSegment::setProcessRunning(bool running)
{
	if(m_processRunning == NULL)
		return;
	*m_processRunning = (running ? 1 : 0);
}

uint32_t MainSharedSegment::getVideoFrequencyNum()
{
	if(m_videoFreqNum == NULL)
		return 0;
	// WARNING: Doesn't lock
	return *m_videoFreqNum;
}

uint32_t MainSharedSegment::getVideoFrequencyDenom()
{
	if(m_videoFreqDenom == NULL)
		return 0;
	// WARNING: Doesn't lock
	return *m_videoFreqDenom;
}

void MainSharedSegment::setVideoFrequency(
	uint32_t numerator, uint32_t denominator)
{
	if(m_videoFreqNum == NULL || m_videoFreqDenom == NULL)
		return;
	// WARNING: Doesn't lock
	*m_videoFreqNum = numerator;
	*m_videoFreqDenom = denominator;
}

bool MainSharedSegment::getHasDxgi11()
{
	if(m_hasDxgi11 == NULL)
		return false;
	return (*m_hasDxgi11 != 0) ? true : false;
}

void MainSharedSegment::setHasDxgi11(bool hasDxgi11)
{
	if(m_hasDxgi11 == NULL)
		return;
	*m_hasDxgi11 = (hasDxgi11 ? 1 : 0);
}

bool MainSharedSegment::getHasBgraTexSupport()
{
	if(m_hasBgraTexSupport == NULL)
		return false;
	return (*m_hasBgraTexSupport != 0) ? true : false;
}

void MainSharedSegment::setHasBgraTexSupport(bool hasBgraTexSupport)
{
	if(m_hasBgraTexSupport == NULL)
		return;
	*m_hasBgraTexSupport = (hasBgraTexSupport ? 1 : 0);
}

bool MainSharedSegment::getFuzzyCapture()
{
	if(m_fuzzyCapture == NULL)
		return false;
	return (*m_fuzzyCapture != 0) ? true : false;
}

void MainSharedSegment::setFuzzyCapture(bool fuzzyCapture)
{
	if(m_fuzzyCapture == NULL)
		return;
	*m_fuzzyCapture = (fuzzyCapture ? 1 : 0);
}

InterprocessLog *MainSharedSegment::getInterprocessLog()
{
	return m_interprocessLog;
}

/// <summary>
/// Attempts to lock the hook registry from being written to. If `timeoutMsec`
/// is `0` then the lock will never time out.
/// </summary>
/// <returns>True if the lock was gained</returns>
bool MainSharedSegment::lockHookRegistry(uint timeoutMsec)
{
	if(timeoutMsec == 0)
		m_hookRegistry->lock.lock();
	else {
		// WARNING: Boost's interprocess mutexes are apparently not "robust" so
		// the following doesn't do what we think it does. See:
		// http://stackoverflow.com/questions/15772768/boost-interprocess-mutexes-and-checking-for-abandonment
		boost::posix_time::ptime timeout(
			boost::posix_time::microsec_clock::local_time() +
			boost::posix_time::millisec(timeoutMsec));
		if(!m_hookRegistry->lock.timed_lock(timeout))
			return false;
	}
	return true;
}

void MainSharedSegment::unlockHookRegistry()
{
	m_hookRegistry->lock.unlock();
}

/// <summary>
/// WARNING: The hook registry must be locked before calling this method!
/// </summary>
HookRegEntry *MainSharedSegment::findWindowInHookRegistry(uint32_t winId)
{
	for(uint i = 0; i < m_hookRegistry->numEntries; i++) {
		HookRegEntry *entry = &m_hookRegistry->entries[i];
		if(winId == entry->winId)
			return entry;
	}
	return NULL;
}

/// <summary>
/// WARNING: The hook registry must be locked before calling this method!
/// </summary>
HookRegEntry *MainSharedSegment::iterateHookRegistry(uint &numEntriesOut)
{
	numEntriesOut = m_hookRegistry->numEntries;
	return m_hookRegistry->entries;
}

/// <summary>
/// WARNING: The hook registry must be locked before calling this method!
/// </summary>
void MainSharedSegment::addHookRegistry(const HookRegEntry &data)
{
	// Search if the window already exists and if it does delete it
	if(findWindowInHookRegistry(data.winId) != NULL)
		removeHookRegistry(data.winId);

	// Prevent overflow
	if(m_hookRegistry->numEntries >= HOOK_REGISTRY_SIZE)
		return;

	m_hookRegistry->entries[m_hookRegistry->numEntries] = data;
	m_hookRegistry->numEntries++;
}

/// <summary>
/// WARNING: The hook registry must be locked before calling this method!
/// </summary>
void MainSharedSegment::removeHookRegistry(uint32_t winId)
{
	HookRegEntry *entry = findWindowInHookRegistry(winId);
	if(entry == NULL)
		return; // Already removed

	// Get number of bytes that we need to move
	int index =
		(int)((entry - m_hookRegistry->entries) / sizeof(HookRegEntry));
	int bytesAfter = (HOOK_REGISTRY_SIZE - index - 1) * sizeof(HookRegEntry);

	// Remove the entry
	if(bytesAfter > 0)
		memmove(entry, entry + sizeof(HookRegEntry), bytesAfter);
	m_hookRegistry->numEntries--;
}
