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

#ifndef HOOKMAIN_H
#define HOOKMAIN_H

#include "../Common/mainsharedsegment.h"
#include <windows.h>

class D3D9HookManager;
class DXGIHookManager;
class GLHookManager;
class InterprocessLog;
struct ID3D10Device;

#define HookLog(msg) \
	if(HookMain::s_instance->getLog() != NULL) \
	HookMain::s_instance->getLog()->log((msg))
#define HookLog2(lvl, msg) \
	if(HookMain::s_instance->getLog() != NULL) \
	HookMain::s_instance->getLog()->log((lvl), (msg))

//=============================================================================
class HookMain
{
public: // Static members -----------------------------------------------------
	static HookMain *	s_instance;
	static HINSTANCE	s_hinstDll;

private: // Members -----------------------------------------------------------
	bool				m_exitMainLoop;
	int					m_exitCode;
	MainSharedSegment	m_shm;
	InterprocessLog *	m_log;
	ID3D10Device *		m_dummyDX10;
	int					m_dummyDX10Ref;
	string				m_exeFilename;

	// Performance timer
	DWORD				m_startTick;
	LONGLONG			m_lastTime;
	LARGE_INTEGER		m_startTime;
	LARGE_INTEGER		m_frequency;
	DWORD_PTR			m_timerMask;

	// Hook managers
	D3D9HookManager *	m_d3d9Manager;
	DXGIHookManager *	m_dxgiManager;
	GLHookManager *		m_glManager;

public: // Constructor/destructor ---------------------------------------------
	HookMain();
	virtual	~HookMain();

public: // Methods ------------------------------------------------------------
	int			exec(void *param);
	void		exit(int exitCode = 0);

	MainSharedSegment *	getShm();
	InterprocessLog *	getLog() const;
	string				getExeFilename() const;

	HWND				createDummyWindow() const;
	ID3D10Device *		refDummyDX10Device();
	void				derefDummyDX10Device();

	// Performance timer
	uint64_t	getUsecSinceExec();
private:
	bool		beginPerformanceTimer();

private:
	void		attemptToHook();
};
//=============================================================================

inline MainSharedSegment *HookMain::getShm()
{
	return &m_shm;
}

inline InterprocessLog *HookMain::getLog() const
{
	return m_log;
}

inline string HookMain::getExeFilename() const
{
	return m_exeFilename;
}

#endif // HOOKMAIN_H
