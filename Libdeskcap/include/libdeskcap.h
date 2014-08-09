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

#ifndef LIBDESKCAP_H
#define LIBDESKCAP_H

#include <QtCore/QRect>
#include <QtCore/QString>

// Export symbols from the DLL while allowing header file reuse by users
#ifdef LIBDESKCAP_LIB
#define LDC_EXPORT Q_DECL_EXPORT
#else
#define LDC_EXPORT Q_DECL_IMPORT
#endif

//=============================================================================
// Global application constants

// Library version. NOTE: Don't forget to update the values in _ALL_ of the
// resource files as well ("Libdeskcap.rc", helpers and hooks)
#define LIBDESKCAP_VER_STR "v0.5.0"
#define LIBDESKCAP_VER_MAJOR 0
#define LIBDESKCAP_VER_MINOR 5
#define LIBDESKCAP_VER_BUILD 0

//=============================================================================
// Enumerations

// HACK
#ifdef Q_OS_WIN
typedef void * WinId; // HWND
typedef void * MonitorId; // HMONITOR
#endif

struct MonitorInfo {
	MonitorId	handle;
	QRect		rect;
	bool		isPrimary;
	QString		deviceName; // "\\.\DISPLAY1"
	int			friendlyId; // Friendly ID number (1, 2, 3...)
	QString		friendlyName; // "BenQ FP241W (Digital) (ATI Radeon HD 5700 Series)"
	void *		extra; // `IDXGIOutput *` on Windows
};

enum CptrMethod {
	CptrAutoMethod = 0,
	CptrStandardMethod, // GDI
	CptrCompositorMethod, // Aero
	CptrHookMethod,
	CptrDuplicatorMethod // Windows 8 desktop duplicator
};

enum CptrType {
	CptrWindowType = 0,
	CptrMonitorType
};

//=============================================================================
// Library initialization

LDC_EXPORT bool	initLibdeskcap_internal(
	int libVerMajor, int libVerMinor, int libVerPatch);

/// <summary>
/// Initializes Libdeskcap. Must be called as the very first thing in `main()`.
/// </summary>
#define INIT_LIBDESKCAP() \
	if(!initLibdeskcap_internal( \
	LIBDESKCAP_VER_MAJOR, LIBDESKCAP_VER_MINOR, LIBDESKCAP_VER_BUILD)) \
	return 1

#endif // LIBDESKCAP_H
