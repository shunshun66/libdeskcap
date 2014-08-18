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

#ifndef GLSTATICS_H
#define GLSTATICS_H

//=============================================================================
// Define datatypes and macros that are normally defined in <windows.h> or
// <gl/gl.h>

#ifndef NULL
#define NULL 0
#endif

//---------------------------
#ifndef WINAPI

#ifdef _WIN64
#define WINAPI
typedef __int64	INT_PTR;
typedef INT_PTR	(WINAPI *PROC)();
#else
#define WINAPI	__stdcall
typedef int		(WINAPI *PROC)();
#endif // _WIN64

typedef const char *	LPCSTR;
typedef int				BOOL;
typedef unsigned int	UINT;

#define DECLARE_HANDLE(name) struct name##__{ int unused; }; \
	typedef struct name##__ *name
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);

#endif // WINAPI
//---------------------------

typedef void GLvoid;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned char GLubyte;

//=============================================================================
// Dynamic linking manager

bool	linkGLLibrary(bool allowLoad);
void	unlinkGLLibrary();

//=============================================================================
// Define functions that should normally be statically linked. We do this as we
// don't want to pull in the OpenGL library unless the application is already
// using OpenGL.

// Use C name mangling for GLEW support
extern "C"
{
	extern const GLubyte * WINAPI glGetString_mishira(GLenum);
	extern GLenum WINAPI glGetError_mishira();
	extern void WINAPI glGetIntegerv_mishira(GLenum, GLint *);
	extern void WINAPI glReadBuffer_mishira(GLenum);
	extern void WINAPI glReadPixels_mishira(
		GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
	extern PROC WINAPI wglGetProcAddress_mishira(LPCSTR);
	extern HGLRC WINAPI wglCreateContext_mishira(HDC);
	extern BOOL WINAPI wglDeleteContext_mishira(HGLRC);
	extern HGLRC WINAPI wglGetCurrentContext_mishira();
	extern HDC WINAPI wglGetCurrentDC_mishira();
	extern BOOL WINAPI wglMakeCurrent_mishira(HDC, HGLRC);
	extern BOOL WINAPI wglSwapBuffers_mishira(HDC);
	extern BOOL WINAPI wglSwapLayerBuffers_mishira(HDC, UINT);
}

#endif // GLSTATICS_H
