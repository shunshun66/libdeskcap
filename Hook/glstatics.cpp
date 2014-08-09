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

#include <windows.h>

// Must be after <windows.h>
#include "glstatics.h"

//=============================================================================
// Function pointers

typedef const GLubyte *(WINAPI *glGetString_t)(GLenum);
typedef GLenum (WINAPI *glGetError_t)();
typedef void (WINAPI *glGetIntegerv_t)(GLenum, GLint *);
typedef void (WINAPI *glReadBuffer_t)(GLenum);
typedef void (WINAPI *glReadPixels_t)(
	GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
typedef PROC (WINAPI *wglGetProcAddress_t)(LPCSTR);
typedef HGLRC (WINAPI *wglCreateContext_t)(HDC);
typedef BOOL (WINAPI *wglDeleteContext_t)(HGLRC);
typedef HGLRC (WINAPI *wglGetCurrentContext_t)();
typedef HDC (WINAPI *wglGetCurrentDC_t)();
typedef BOOL (WINAPI *wglMakeCurrent_t)(HDC, HGLRC);
typedef BOOL (WINAPI *wglSwapBuffers_t)(HDC);
typedef BOOL (WINAPI *wglSwapLayerBuffers_t)(HDC, UINT);

static glGetError_t glGetErrorPtr = NULL;
static glGetString_t glGetStringPtr = NULL;
static glGetIntegerv_t glGetIntegervPtr = NULL;
static glReadBuffer_t glReadBufferPtr = NULL;
static glReadPixels_t glReadPixelsPtr = NULL;
static wglGetProcAddress_t wglGetProcAddressPtr = NULL;
static wglCreateContext_t wglCreateContextPtr = NULL;
static wglDeleteContext_t wglDeleteContextPtr = NULL;
static wglGetCurrentContext_t wglGetCurrentContextPtr = NULL;
static wglGetCurrentDC_t wglGetCurrentDCPtr = NULL;
static wglMakeCurrent_t wglMakeCurrentPtr = NULL;
static wglSwapBuffers_t wglSwapBuffersPtr = NULL;
static wglSwapLayerBuffers_t wglSwapLayerBuffersPtr = NULL;

//=============================================================================
// Dynamic linking manager

static bool g_glLibraryLinked = false;

/// <summary>
/// Dynamically links the OpenGL library. If `allowLoad` is true then the
/// function will load the library into memory if it isn't already loaded.
/// </summary>
/// <returns>True if linking was successful</returns>
bool linkGLLibrary(bool allowLoad)
{
	if(g_glLibraryLinked)
		return true; // Already linked

	// Is the OpenGL library actually loaded?
	HMODULE module = GetModuleHandle(TEXT("opengl32.dll"));
	if(module == NULL) {
		// Nope, attempt to load it if we're allowed to
		if(!allowLoad)
			return false;
		module = LoadLibrary(TEXT("opengl32.dll"));
		if(module == NULL)
			return false;
	}
	g_glLibraryLinked = true;

	// Prepare statically linked stubs. These functions are safe to fetch using
	// `GetProcAddress()` as they're usually automatically linked in when the
	// executable is loaded.
	glGetStringPtr =
		(glGetString_t)GetProcAddress(module, "glGetString");
	glGetErrorPtr =
		(glGetError_t)GetProcAddress(module, "glGetError");
	glGetIntegervPtr =
		(glGetIntegerv_t)GetProcAddress(module, "glGetIntegerv");
	glReadBufferPtr =
		(glReadBuffer_t)GetProcAddress(module, "glReadBuffer");
	glReadPixelsPtr =
		(glReadPixels_t)GetProcAddress(module, "glReadPixels");
	wglGetProcAddressPtr =
		(wglGetProcAddress_t)GetProcAddress(module, "wglGetProcAddress");
	wglCreateContextPtr =
		(wglCreateContext_t)GetProcAddress(module, "wglCreateContext");
	wglDeleteContextPtr =
		(wglDeleteContext_t)GetProcAddress(module, "wglDeleteContext");
	wglGetCurrentContextPtr =
		(wglGetCurrentContext_t)GetProcAddress(module, "wglGetCurrentContext");
	wglGetCurrentDCPtr =
		(wglGetCurrentDC_t)GetProcAddress(module, "wglGetCurrentDC");
	wglMakeCurrentPtr =
		(wglMakeCurrent_t)GetProcAddress(module, "wglMakeCurrent");
	wglSwapBuffersPtr =
		(wglSwapBuffers_t)GetProcAddress(module, "wglSwapBuffers");
	wglSwapLayerBuffersPtr =
		(wglSwapLayerBuffers_t)GetProcAddress(module, "wglSwapLayerBuffers");

	return true;
}

void unlinkGLLibrary()
{
	// Force relink
	g_glLibraryLinked = false;

	// Unset for safety
	glGetStringPtr = NULL;
	glGetErrorPtr = NULL;
	glGetIntegervPtr = NULL;
	glReadBufferPtr = NULL;
	glReadPixelsPtr = NULL;
	wglGetProcAddressPtr = NULL;
	wglCreateContextPtr = NULL;
	wglDeleteContextPtr = NULL;
	wglGetCurrentContextPtr = NULL;
	wglGetCurrentDCPtr = NULL;
	wglMakeCurrentPtr = NULL;
	wglSwapBuffersPtr = NULL;
	wglSwapLayerBuffersPtr = NULL;
}

//=============================================================================
// Function stubs

extern "C" GLenum WINAPI glGetError_mishira()
{
	return (*glGetErrorPtr)();
}

extern "C" const GLubyte * WINAPI glGetString_mishira(GLenum name)
{
	return (*glGetStringPtr)(name);
}

extern "C" void WINAPI glGetIntegerv_mishira(GLenum pname, GLint *params)
{
	(*glGetIntegervPtr)(pname, params);
}

extern "C" void WINAPI glReadBuffer_mishira(GLenum mode)
{
	(*glReadBufferPtr)(mode);
}

extern "C" void WINAPI glReadPixels_mishira(
	GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
	GLenum type, GLvoid *pixels)
{
	(*glReadPixelsPtr)(x, y, width, height, format, type, pixels);
}

extern "C" PROC WINAPI wglGetProcAddress_mishira(LPCSTR lpszProc)
{
	return (*wglGetProcAddressPtr)(lpszProc);
}

extern "C" HGLRC WINAPI wglCreateContext_mishira(HDC hdc)
{
	return (*wglCreateContextPtr)(hdc);
}

extern "C" BOOL WINAPI wglDeleteContext_mishira(HGLRC hglrc)
{
	return (*wglDeleteContextPtr)(hglrc);
}

extern "C" HGLRC WINAPI wglGetCurrentContext_mishira()
{
	return (*wglGetCurrentContextPtr)();
}

extern "C" HDC WINAPI wglGetCurrentDC_mishira()
{
	return (*wglGetCurrentDCPtr)();
}

extern "C" BOOL WINAPI wglMakeCurrent_mishira(HDC hdc, HGLRC hglrc)
{
	return (*wglMakeCurrentPtr)(hdc, hglrc);
}

extern "C" BOOL WINAPI wglSwapBuffers_mishira(HDC hdc)
{
	return (*wglSwapBuffersPtr)(hdc);
}

extern "C" BOOL WINAPI wglSwapLayerBuffers_mishira(HDC hdc, UINT fuPlanes)
{
	return (*wglSwapLayerBuffersPtr)(hdc, fuPlanes);
}
