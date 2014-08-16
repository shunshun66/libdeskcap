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

#include "glhookmanager.h"
#include "glhook.h"
#include "hookmain.h"
#include "rewritehook.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

// Must be after <windows.h>
#include "glstatics.h"

#if USE_MINHOOK
#error MinHook causes Minecraft to freeze on startup.
#endif // USE_MINHOOK

//=============================================================================
// Function stubs

GLEWContext *glewGetContext()
{
	GLHookManager *mgr = GLHookManager::getSingleton();
	return mgr->getCurrentGLEWContext();
}

extern "C" typedef BOOL (WINAPI *SwapBuffers_t)(HDC hdc);
extern "C" static BOOL WINAPI SwapBuffersHook(HDC hdc)
{
	// Identical to `wglSwapBuffers()`
	GLHookManager *mgr = GLHookManager::getSingleton();
	return mgr->wglSwapBuffersHooked(false, hdc);
}

extern "C" typedef BOOL (WINAPI *wglSwapBuffers_t)(HDC hdc);
extern "C" static BOOL WINAPI wglSwapBuffersHook(HDC hdc)
{
	// Identical to `SwapBuffers()`
	GLHookManager *mgr = GLHookManager::getSingleton();
	return mgr->wglSwapBuffersHooked(true, hdc);
}

extern "C" typedef BOOL (WINAPI *wglSwapLayerBuffers_t)(
	HDC hdc, UINT fuPlanes);
extern "C" static BOOL WINAPI wglSwapLayerBuffersHook(
	HDC hdc, UINT fuPlanes)
{
	GLHookManager *mgr = GLHookManager::getSingleton();
	return mgr->wglSwapLayerBuffersHooked(hdc, fuPlanes);
}

extern "C" typedef BOOL (WINAPI *wglDeleteContext_t)(HGLRC hglrc);
extern "C" static BOOL WINAPI wglDeleteContextHook(HGLRC hglrc)
{
	GLHookManager *mgr = GLHookManager::getSingleton();
	return mgr->wglDeleteContextHooked(hglrc);
}

//=============================================================================
// GLHookManager class

GLHookManager *GLHookManager::s_instance = NULL;

GLHookManager::GLHookManager()
	: m_hookMutex()
	, m_glLibLoaded(false)
	, m_safeToHook(false)
	, m_isHooked(false)
	, m_hooks()
	, m_contexts()
	, m_currentContext(-1)

	// Hooks
	, m_SwapBuffersHook(NULL)
	, m_wglSwapBuffersHook(NULL)
	, m_wglSwapLayerBuffersHook(NULL)
	, m_wglDeleteContextHook(NULL)
{
	//assert(s_instance == NULL);
	s_instance = this;

	m_hooks.reserve(8);
	m_contexts.reserve(8);
}

GLHookManager::~GLHookManager()
{
	// Wait until all callbacks have completed as they will most likely be
	// executed in a different thread than the one we are being deleted from
	m_hookMutex.lock();

	// Unhook everything so our callbacks don't get called while we are
	// destructing
	if(m_SwapBuffersHook != NULL)
		m_SwapBuffersHook->uninstall();
	if(m_wglSwapBuffersHook != NULL)
		m_wglSwapBuffersHook->uninstall();
	if(m_wglSwapLayerBuffersHook != NULL)
		m_wglSwapLayerBuffersHook->uninstall();
	if(m_wglDeleteContextHook != NULL)
		m_wglDeleteContextHook->uninstall();

	// As another thread might have processed while we were uninstalling our
	// hooks temporary yield to make sure they have fully processed before
	// continuing
	m_hookMutex.unlock();
	Sleep(50);
	m_hookMutex.lock();

	// Delete all hooking contexts. Must be done while a GLEW context exists
	while(!m_hooks.empty()) {
		GLHook *hook = m_hooks.back();
		hook->release();
		m_hooks.pop_back();
	}

	// Delete hooks cleanly
	unhook();

	// Delete all GLEW contexts
	while(!m_contexts.empty()) {
		delete m_contexts.back();
		m_contexts.pop_back();
	}

	s_instance = NULL;
	m_hookMutex.unlock();
}

void GLHookManager::attemptToHook()
{
	if(!m_glLibLoaded)
		loadLibIfPossible();
	if(!m_safeToHook)
		return;

	// Make sure we only ever hook once to prevent crashes from inter-thread
	// conflicts
	if(m_isHooked)
		return; // Already hooked
	m_isHooked = true;

	// Create hook handlers, TODO: These need to be per-context
	HMODULE module = GetModuleHandle(TEXT("opengl32.dll"));
	m_SwapBuffersHook =
		new RewriteHook(&SwapBuffers, // Apart of GDI, not `opengl32.dll`
		&SwapBuffersHook);
	m_wglSwapBuffersHook =
		new RewriteHook(GetProcAddress(module, "wglSwapBuffers"),
		&wglSwapBuffersHook);
	m_wglSwapLayerBuffersHook =
		new RewriteHook(GetProcAddress(module, "wglSwapLayerBuffers"),
		&wglSwapLayerBuffersHook);
	m_wglDeleteContextHook =
		new RewriteHook(GetProcAddress(module, "wglDeleteContext"),
		&wglDeleteContextHook);

	// Install all our hooks
	m_SwapBuffersHook->install();
	m_wglSwapBuffersHook->install();
	m_wglSwapLayerBuffersHook->install();
	m_wglDeleteContextHook->install();
}

void GLHookManager::unhook()
{
	if(!m_isHooked)
		return; // Already unhooked
	HookLog("Destroying OpenGL subsystem");

	// Uninstall and delete our hook objects
	delete m_SwapBuffersHook;
	delete m_wglSwapBuffersHook;
	delete m_wglSwapLayerBuffersHook;
	delete m_wglDeleteContextHook;
	m_SwapBuffersHook = NULL;
	m_wglSwapBuffersHook = NULL;
	m_wglSwapLayerBuffersHook = NULL;
	m_wglDeleteContextHook = NULL;

	// Delete GLEW context
	m_contexts.erase(m_contexts.begin() + m_currentContext);
	m_currentContext--;
	m_safeToHook = false; // No longer safe to hook
	m_glLibLoaded = false; // Attempt to refetch context function pointers
	unlinkGLLibrary(); // Attempt to refetch global function pointers

	//HookLog("Successfully destroyed OpenGL subsystem");
	m_isHooked = false;
}

void GLHookManager::loadLibIfPossible()
{
	if(m_glLibLoaded)
		return; // Already loaded and attempted to hook

	// Attempt to link with the base OpenGL library if it isn't already
	if(!linkGLLibrary(false))
		return; // Failed to link
	m_glLibLoaded = true;
	// Application is using OpenGL

	HookLog("Initializing OpenGL subsystem");

	// As OpenGL function pointers are theoretically unique to each context we
	// need to initialize once per context. In practice though function
	// pointers are usually shared between contexts as long as they are on a
	// graphics adapter that is owned by the same vendor and they have the same
	// pixel format.
	//
	// Right now we just assume that all applications use the same pixel format
	// which is obviously not the case. TODO
	int index = createGLEWContext(PFD_TYPE_RGBA, 32, 0, 32, 0);
	if(index < 0)
		return;
	HookLog("Successfully initialized OpenGL subsystem");
	m_safeToHook = true;
}

/// <summary>
/// Creates a GLEWContext for the specified pixel format.
/// </summary>
/// <returns>The index of the context in `m_contexts` or -1 on failure</returns>
int GLHookManager::createGLEWContext(
	char iPixelType, char cColorBits, char cAccumBits, char cDepthBits,
	char cStencilBits)
{
	// Create dummy window and fetch device context
	HWND hwnd = HookMain::s_instance->createDummyWindow();
	if(hwnd == NULL)
		return -1;
	HDC hdc = GetDC(hwnd);

	// Set the pixel format of the device context
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = iPixelType; // `PFD_TYPE_RGBA` or `PFD_TYPE_COLORINDEX`
	pfd.cColorBits = cColorBits; // 16, 24, 32...
	pfd.cAccumBits = cAccumBits;
	pfd.cDepthBits = cDepthBits;
	pfd.cStencilBits = cStencilBits;
	if(SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd) == FALSE) {
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to set window pixel format. Reason = %u",
			err));
		goto exitCreateContext1;
	}

	//-------------------------------------------------------------------------
	// Begin OpenGL code. WARNING: As OpenGL isn't thread-safe there is a
	// chance that if the process is currently using OpenGL it will be using
	// the wrong context for the duration of the following code!

	// Get the current context so we can cover our tracks
	HDC prevDC = wglGetCurrentDC_mishira();
	HGLRC prevGLRC = wglGetCurrentContext_mishira();

	// Create a new OpenGL context and make it current
	HGLRC glrc = wglCreateContext_mishira(hdc);
	if(glrc == NULL) {
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to create OpenGL context. Reason = %u",
			err));
		goto exitCreateContext1;
	}
	if(wglMakeCurrent_mishira(hdc, glrc) == FALSE) {
		DWORD err = GetLastError();
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to make our OpenGL context current. Reason = %u",
			err));
		goto exitCreateContext2;
	}

	// Fully initialize OpenGL with everything that we need using GLEW
	m_contexts.push_back(new GLEWContext());
	m_currentContext++;
	GLenum err = glewInit();
	if(err != GLEW_OK) {
		HookLog2(InterprocessLog::Warning,
			stringf("Failed to initialize OpenGL. Reason = %s",
			glewGetErrorString(err)));
		goto exitCreateContext3;
	}
	if(!GLEW_ARB_pixel_buffer_object) {
		HookLog2(InterprocessLog::Warning,
			"Failed to initialize OpenGL. Reason = No PBO support");
		goto exitCreateContext3;
	}

	// Debugging stuff
	HookLog(stringf("OpenGL version: %s", glGetString_mishira(GL_VERSION)));
	//HookLog(stringf("OpenGL vender: %s", glGetString_mishira(GL_VENDOR)));
	//HookLog(stringf("OpenGL renderer: %s", glGetString_mishira(GL_RENDERER)));

	// Clean up OpenGL-related stuff
	wglMakeCurrent_mishira(prevDC, prevGLRC);
	wglDeleteContext_mishira(glrc);

	//-------------------------------------------------------------------------

	// Clean up Windows-related stuff
	DestroyWindow(hwnd);

	return m_currentContext;

	// Error handling
exitCreateContext3:
	wglMakeCurrent_mishira(prevDC, prevGLRC);
	m_currentContext--;
	delete m_contexts.back();
	m_contexts.pop_back();
exitCreateContext2:
	wglDeleteContext_mishira(glrc);
exitCreateContext1:
	DestroyWindow(hwnd);
	return -1;
}

GLHook *GLHookManager::findHookForHdc(HDC hdc)
{
	for(uint i = 0; i < m_hooks.size(); i++) {
		GLHook *hook = m_hooks.at(i);
		if(hook->getHdc() == hdc)
			return hook;
	}
	return NULL;
}

void GLHookManager::processBufferSwap(HDC hdc)
{
	// Create a new `GLHook` instance for every unique HDC so we can keep track
	// of multiple contexts.
	GLHook *hook = findHookForHdc(hdc);
	if(hook == NULL) {
		// This is a brand new context! Track it
		hook = new GLHook(hdc, wglGetCurrentContext_mishira());
		hook->initialize();
		m_hooks.push_back(hook);
	}

	// Forward to the context handler
	hook->processBufferSwap();
}

BOOL GLHookManager::wglSwapBuffersHooked(bool wasWgl, HDC hdc)
{
	m_hookMutex.lock();

	// Handles both `SwapBuffers()` and `wglSwapBuffers()`
	//if(wasWgl)
	//	HookLog("wglSwapBuffers()");
	//else
	//	HookLog("SwapBuffers()");

	// Capture the buffer
	processBufferSwap(hdc);

	// Forward to the real function. We unhook both variations as they are most
	// likely aliases of each other and we don't want to process the same frame
	// twice.
	BOOL ret = FALSE;
#if USE_MINHOOK
	if(wasWgl)
		ret = ((wglSwapBuffers_t)m_wglSwapBuffersHook->getTrampoline())(hdc);
	else
		ret = ((SwapBuffers_t)m_SwapBuffersHook->getTrampoline())(hdc);
#else
	m_wglSwapBuffersHook->uninstall();
	m_SwapBuffersHook->uninstall();
	if(wasWgl)
		ret = wglSwapBuffers_mishira(hdc);
	else
		ret = SwapBuffers(hdc);
	m_SwapBuffersHook->install();
	m_wglSwapBuffersHook->install();
#endif // USE_MINHOOK

	m_hookMutex.unlock();
	return ret;
}

BOOL GLHookManager::wglSwapLayerBuffersHooked(HDC hdc, UINT fuPlanes)
{
	m_hookMutex.lock();

	//HookLog("wglSwapLayerBuffers()");

	// Capture the buffer
	processBufferSwap(hdc);

	// Forward to the real function
#if USE_MINHOOK
	BOOL ret =
		((wglSwapLayerBuffers_t)m_wglSwapLayerBuffersHook->getTrampoline())(
		hdc, fuPlanes);
#else
	m_wglSwapLayerBuffersHook->uninstall();
	BOOL ret = wglSwapLayerBuffers_mishira(hdc, fuPlanes);
	m_wglSwapLayerBuffersHook->install();
#endif // USE_MINHOOK

	m_hookMutex.unlock();
	return ret;
}

BOOL GLHookManager::wglDeleteContextHooked(HGLRC hglrc)
{
	m_hookMutex.lock();

	//HookLog("wglDeleteContext()");

	// Forward to the context handler if this is a known context and then
	// delete it as it's about to become invalid. We need to keep in mind that
	// the HDC is not currently set so `wglGetCurrentDC()` will return
	// something else.
	for(uint i = 0; i < m_hooks.size(); i++) {
		GLHook *hook = m_hooks.at(i);
		if(hook->getHglrc() == hglrc) {
			hook->processDeleteContext();
			m_hooks.erase(m_hooks.begin() + i);
			hook->release();
			break;
		}
	}

	// If `wglDeleteContext()` is called and we have no other known contexts
	// left then the program is most likely shutting down. Use this opportunity
	// to cleanly unhook everything.
	// FIXME: This crashes some users so we just disable it for now
	BOOL ret = TRUE;
	if(false) { //if(m_hooks.size() <= 0) {
		// Forward to the real function
		m_wglDeleteContextHook->uninstall();
		ret = wglDeleteContext_mishira(hglrc);
		unhook(); // Uninstalls and deletes our hooks
	} else {
		// Forward to the real function
#if USE_MINHOOK
		ret = ((wglDeleteContext_t)m_wglDeleteContextHook->getTrampoline())(
			hglrc);
#else
		m_wglDeleteContextHook->uninstall();
		ret = wglDeleteContext_mishira(hglrc);
		m_wglDeleteContextHook->install();
#endif // USE_MINHOOK
	}

	m_hookMutex.unlock();
	return ret;
}
