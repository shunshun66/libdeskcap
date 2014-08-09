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

#ifndef COMMON_MAINSHAREDSEGMENT_H
#define COMMON_MAINSHAREDSEGMENT_H

#include "stlincludes.h"
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_recursive_mutex.hpp>
using namespace boost::interprocess;

class InterprocessLog;
class ManagedSharedMemory;

//=============================================================================
// Hook registry

// WARNING: All datatypes must have the same size on both 32- and 64-bit
// systems as the memory could be shared between processes of different
// bitness!
struct HookRegEntry {
	enum HookRegFlags {
		// Set by the main application to let the hook know if a window should
		// be captured.
		CaptureFlag = 0x01,

		// Set by the hook to notify the main application that the shared
		// memory segment that contains capture data is ready and that the
		// `shmName` and `shmSize` fields are valid.
		ShmValidFlag = 0x02,

		// Set by the hook to notify the main application that there is a new
		// shared memory segment and that the previous one should be closed.
		// The main application clears this flag once it acknowledges it.
		ShmResetFlag = 0x04
	};

	uint32_t	winId; // Window that can be hooked
	uint32_t	hookProcId; // Hook process ID that manages the window
	uint32_t	shmName; // SHM segment unique ID (Random number)
	uint32_t	shmSize; // Size of the SHM segment
	uchar		flags;

	HookRegEntry() : winId(0), hookProcId(0), shmName(0), flags(0) {};
};

//=============================================================================
/// <summary>
/// Represents the shared memory segment for interprocess communication.
/// </summary>
class MainSharedSegment
{
public: // Constants ----------------------------------------------------------
	static const int SEGMENT_SIZE = 512 * 1024; // 512 KB
	static const int HOOK_REGISTRY_SIZE = 128;

private: // Datatypes ----------------------------------------------------------
	struct LockedUInt32 {
		uint32_t			val;
		interprocess_mutex	lock;

		LockedUInt32() : val(0), lock() {};
	};

	// Hook registry
	struct HookRegistry {
		interprocess_recursive_mutex	lock;
		uint32_t						numEntries;
		HookRegEntry					entries[HOOK_REGISTRY_SIZE];

		HookRegistry() : lock(), numEntries(0) {
			memset(entries, 0, sizeof(entries));
		};
	};

private: // Members -----------------------------------------------------------
	ManagedSharedMemory *	m_shm;
	bool					m_isValid;
	string					m_errorReason;

	// Data
	char *					m_processRunning;
	uint32_t *				m_videoFreqNum;
	uint32_t *				m_videoFreqDenom;
	char *					m_hasDxgi11;
	char *					m_hasBgraTexSupport;
	char *					m_fuzzyCapture;
	InterprocessLog *		m_interprocessLog;
	HookRegistry *			m_hookRegistry;

public: // Constructor/destructor ---------------------------------------------
	MainSharedSegment();
	virtual ~MainSharedSegment();

public: // Methods ------------------------------------------------------------
	bool				isValid() const;
	string				getErrorReason() const;

	bool				getProcessRunning();
	void				setProcessRunning(bool running);

	uint32_t			getVideoFrequencyNum();
	uint32_t			getVideoFrequencyDenom();
	void				setVideoFrequency(
		uint32_t numerator, uint32_t denominator);

	bool				getHasDxgi11();
	void				setHasDxgi11(bool hasDxgi11);

	bool				getHasBgraTexSupport();
	void				setHasBgraTexSupport(bool hasBgraTexSupport);

	bool				getFuzzyCapture();
	void				setFuzzyCapture(bool fuzzyCapture);

	InterprocessLog *	getInterprocessLog();

	bool				lockHookRegistry(uint timeoutMsec = 0);
	void				unlockHookRegistry();
	HookRegEntry *		findWindowInHookRegistry(uint32_t winId);
	HookRegEntry *		iterateHookRegistry(uint &numEntriesOut);
	void				addHookRegistry(const HookRegEntry &data);
	void				removeHookRegistry(uint32_t winId);
};
//=============================================================================

inline bool MainSharedSegment::isValid() const
{
	return m_isValid;
}

inline string MainSharedSegment::getErrorReason() const
{
	return m_errorReason;
}

#endif // COMMON_MAINSHAREDSEGMENT_H
