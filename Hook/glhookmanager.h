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

#ifndef GLHOOKMANAGER_H
#define GLHOOKMANAGER_H

#include "../Common/stlincludes.h"
#include <GL/glew.h>
#include <windows.h>
#include <boost/thread.hpp>

class GLHook;
class RewriteHook;

extern GLEWContext *glewGetContext();

//=============================================================================
/// <summary>
/// Manages OpenGL hooking and dispatches callbacks to the appropriate hook.
///
/// WARNING: This object must be thread-safe as hooked callbacks are executed
/// in another thread than this object is created and deleted in.
/// </summary>
class GLHookManager
{
private: // Static members ----------------------------------------------------
	static GLHookManager *	s_instance;

private: // Members -----------------------------------------------------------
	boost::mutex			m_hookMutex;
	bool					m_glLibLoaded;
	bool					m_safeToHook;
	bool					m_isHooked;
	vector<GLHook *>		m_hooks; // Hook instances
	vector<GLEWContext *>	m_contexts;
	int						m_currentContext;

	// Hooks
	RewriteHook *			m_SwapBuffersHook;
	RewriteHook *			m_wglSwapBuffersHook;
	RewriteHook *			m_wglSwapLayerBuffersHook;
	RewriteHook *			m_wglDeleteContextHook;

public: // Static methods -----------------------------------------------------
	inline static GLHookManager *getSingleton() {
		return s_instance;
	};

public: // Constructor/destructor ---------------------------------------------
	GLHookManager();
	virtual	~GLHookManager();

public: // Methods ------------------------------------------------------------
	void			attemptToHook();

	GLEWContext *	getCurrentGLEWContext() const;

	// Hooks
	BOOL			wglSwapBuffersHooked(bool wasWgl, HDC hdc);
	BOOL			wglSwapLayerBuffersHooked(HDC hdc, UINT fuPlanes);
	BOOL			wglDeleteContextHooked(HGLRC hglrc);

private:
	void			loadLibIfPossible();
	int				createGLEWContext(
		char iPixelType, char cColorBits, char cAccumBits, char cDepthBits,
		char cStencilBits);

	void			unhook();
	void			processBufferSwap(HDC hdc);
	GLHook *		findHookForHdc(HDC hdc);
};
//=============================================================================

inline GLEWContext *GLHookManager::getCurrentGLEWContext() const
{
	if(m_currentContext < 0 || m_currentContext >= (int)m_contexts.size())
		return NULL;
	return m_contexts.at(m_currentContext);
}

#endif // GLHOOKMANAGER_H
