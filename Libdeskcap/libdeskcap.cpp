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

#include "include/libdeskcap.h"
#include <Libvidgfx/libvidgfx.h>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

//=============================================================================
// Validate library versions

#if LIBVIDGFX_VER_MAJOR != 0 || \
	LIBVIDGFX_VER_MINOR != 5 || \
	LIBVIDGFX_VER_BUILD != 0
#error Mismatched Libvidgfx version!
#endif

//=============================================================================
// Helpers

#ifdef Q_OS_WIN
/// <summary>
/// Converts a `QString` into a `wchar_t` array. We cannot use
/// `QString::toStdWString()` as it's not enabled in our Qt build. The caller
/// is responsible for calling `delete[]` on the returned string.
/// </summary>
static wchar_t *QStringToWChar(const QString &str)
{
	// We must keep the QByteArray in memory as otherwise its `data()` gets
	// freed and we end up converting undefined data
	QByteArray data = str.toUtf8();
	const char *msg = data.data();
	int len = MultiByteToWideChar(CP_UTF8, 0, msg, -1, NULL, 0);
	wchar_t *wstr = new wchar_t[len];
	MultiByteToWideChar(CP_UTF8, 0, msg, -1, wstr, len);
	return wstr;
}
#endif

/// <summary>
/// Displays a very basic error dialog box. Used as a last resort.
/// </summary>
static void showBasicErrorMessageBox(
	const QString &msg, const QString &caption)
{
#ifdef Q_OS_WIN
	wchar_t *wMessage = QStringToWChar(msg);
	wchar_t *wCaption = QStringToWChar(caption);
	MessageBox(NULL, wMessage, wCaption, MB_OK | MB_ICONERROR);
	delete[] wMessage;
	delete[] wCaption;
#else
#error Unsupported platform
#endif
}

//=============================================================================
// Library initialization

bool initLibdeskcap_internal(int libVerMajor, int libVerMinor, int libVerPatch)
{
	static bool inited = false;
	if(inited)
		return false; // Already initialized
	inited = true;

	// Test Libdeskcap version. TODO: When the API is stable we should not test
	// to see if the patch version is the same
	if(libVerMajor != LIBDESKCAP_VER_MAJOR ||
		libVerMinor != LIBDESKCAP_VER_MINOR ||
		libVerPatch != LIBDESKCAP_VER_BUILD)
	{
		QString msg = QStringLiteral("Fatal: Mismatched Libdeskcap version!");

		// Output to the terminal
		QByteArray output = msg.toLocal8Bit();
		std::cout << output.constData() << std::endl;

		// Visual Studio does not display stdout in the debug console so we
		// need to use a special Windows API
#if defined(Q_OS_WIN) && defined(QT_DEBUG)
		// Output to the Visual Studio or system debugger in debug builds only
		OutputDebugStringA(output.constData());
		OutputDebugStringA("\r\n");
#endif

		// Display a message box so the user knows something went wrong
		showBasicErrorMessageBox(msg, QStringLiteral("Libdeskcap"));

		return false;
	}

	return true;
}
