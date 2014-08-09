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

#ifndef GLHOOK_H
#define GLHOOK_H

#include "commonhook.h"
#include <GL/glew.h>

extern GLEWContext *glewGetContext();

//=============================================================================
/// <summary>
/// Manages a single OpenGL window.
/// </summary>
class GLHook : public CommonHook
{
private: // Constants ---------------------------------------------------------
	static const int NUM_PBOS = 2;

private: // Members -----------------------------------------------------------
	HGLRC	m_hglrc;
	GLenum	m_bbGLFormat;
	GLenum	m_bbGLType;

	// Scene objects
	bool	m_sceneObjectsCreated;
	GLuint	m_pbos[NUM_PBOS];
	bool	m_pboPending[NUM_PBOS]; // `true` if PBO contains valid data
	uint	m_nextPbo;

public: // Constructor/destructor ---------------------------------------------
	GLHook(HDC hdc, HGLRC hglrc);
protected:
	virtual	~GLHook();

public: // Methods ------------------------------------------------------------
	HGLRC	getHglrc() const;

private:
	bool	testForGLError();

protected: // Interface -------------------------------------------------------
	virtual void			calcBackBufferPixelFormat();
	virtual RawPixelFormat	getBackBufferPixelFormat();
	virtual bool			isBackBufferFlipped();
	virtual	ShmCaptureType	getCaptureType();
	virtual void			createSceneObjects();
	virtual void			destroySceneObjects();
	virtual void			captureBackBuffer(
		bool captureFrame, uint64_t timestamp);
	virtual void			destructorEndCapturing();
};
//=============================================================================

inline HGLRC GLHook::getHglrc() const
{
	return m_hglrc;
}

#endif // GLHOOK_H
