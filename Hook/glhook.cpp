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

#include "glhook.h"
#include "helpers.h"
#include "hookmain.h"
#include "../Common/interprocesslog.h"
#include "../Common/stlhelpers.h"

// Must be after <windows.h>
#include "glstatics.h"

//=============================================================================
// GLHook class

GLHook::GLHook(HDC hdc, HGLRC hglrc)
	: CommonHook(hdc)
	, m_hglrc(hglrc)
	, m_bbGLFormat(NULL)
	, m_bbGLType(NULL)

	// Scene objects
	, m_sceneObjectsCreated(false)
	//, m_pbos() // Zeroed below
	//, m_pboPending() // Zeroed below
	, m_nextPbo(0)
{
	memset(m_pbos, 0, sizeof(m_pbos));
	memset(m_pboPending, 0, sizeof(m_pboPending));
}

GLHook::~GLHook()
{
}

/// <summary>
/// Tests if an OpenGL error occured and, if so, logs it.
/// </summary>
/// <returns>True if an error occured.</returns>
bool GLHook::testForGLError()
{
	GLenum err = glGetError_mishira();
	if(err == GL_NO_ERROR)
		return false;
	string str = getGLErrorCode(err);
	HookLog2(InterprocessLog::Warning,
		stringf("OpenGL error occurred: %s", str.data()));
	return true;
}

void GLHook::calcBackBufferPixelFormat()
{
	// Reset variables
	m_bbGLFormat = 0;
	m_bbBpp = 4;
	m_bbIsValidFormat = false;

	// Determine the pixel format of the back buffer
	PIXELFORMATDESCRIPTOR pfd;
	int format = GetPixelFormat(m_hdc);
	DescribePixelFormat(m_hdc, format, sizeof(pfd), &pfd);
	if(pfd.iPixelType == PFD_TYPE_RGBA) {
		// We assume GDI RGB is always OpenGL BGR and not OpenGL RGB
		if(pfd.cColorBits == 32) {
			m_bbGLFormat = GL_BGRA;
			m_bbGLType = GL_UNSIGNED_BYTE;
			m_bbBpp = 4;
			m_bbIsValidFormat = true;
		} else if(pfd.cColorBits == 24) {
			m_bbGLFormat = GL_BGR;
			m_bbGLType = GL_UNSIGNED_BYTE;
			m_bbBpp = 3;
			m_bbIsValidFormat = true;
		}
	}
}

RawPixelFormat GLHook::getBackBufferPixelFormat()
{
	switch(m_bbGLFormat) {
	default:
		return UnknownPixelFormat;
	case GL_BGRA:
		return BGRAPixelFormat;
	case GL_BGR:
		return BGRPixelFormat;
	}
	// Should never be reached
	return UnknownPixelFormat;
}

bool GLHook::isBackBufferFlipped()
{
	return true;
}

ShmCaptureType GLHook::getCaptureType()
{
	return RawPixelsShmType;
}

void GLHook::createSceneObjects()
{
	if(m_sceneObjectsCreated)
		return; // Already created
	if(!isCapturable())
		return; // Not capturable

	HookLog(stringf("Creating OpenGL scene objects for window of size %d x %d",
		m_width, m_height));

	// Reset OpenGL error code so we can detect if any of the following failed
	glGetError_mishira();

	// Create and configure PBOs
	glGenBuffers(NUM_PBOS, m_pbos);
	for(int i = 0; i < NUM_PBOS; i++) {
		GLuint pbo = m_pbos[i];
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, m_width * m_height * m_bbBpp, NULL,
			GL_STREAM_READ);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	memset(m_pboPending, 0, sizeof(m_pboPending));

	// Did any error occur?
	testForGLError();

	m_sceneObjectsCreated = true;
}

void GLHook::destroySceneObjects()
{
	if(!m_sceneObjectsCreated)
		return; // Already destroyed

	HookLog("Destroying OpenGL scene objects");

	// Destroy PBOs
	glDeleteBuffers(NUM_PBOS, m_pbos);

	// Clear memory
	memset(m_pbos, 0, sizeof(m_pbos));
	memset(m_pboPending, 0, sizeof(m_pboPending));

	m_sceneObjectsCreated = false;
}

void GLHook::captureBackBuffer(bool captureFrame, uint64_t timestamp)
{
	// In order to decrease the amount of stalling we copy the backbuffer to a
	// temporary PBO that we then read back at a later time. While having three
	// PBOs is the most efficient it adds an unacceptable delay to the video
	// data when rendered back in the main application.

	// Get PBO to read from and write to
	GLuint writePbo = m_pbos[m_nextPbo];
	bool *writePending = &m_pboPending[m_nextPbo];
	m_nextPbo++;
	if(m_nextPbo >= NUM_PBOS)
		m_nextPbo = 0;
	GLuint readPbo = m_pbos[m_nextPbo];
	bool *readPending = &m_pboPending[m_nextPbo];

	// Reset OpenGL error code so we can detect if any of the following failed
	glGetError_mishira();

	//-------------------------------------------------------------------------
	// Copy backbuffer to our next PBO if we are capturing this frame

	if(captureFrame) {
		// Remember the previous state and bind the PBO. TODO: We assume that
		// the backbuffer will always be double buffered.
		GLint prevReadBuf = GL_BACK;
		glBindBuffer(GL_PIXEL_PACK_BUFFER, writePbo);
		glGetIntegerv_mishira(GL_READ_BUFFER, &prevReadBuf);
		glReadBuffer_mishira(GL_BACK);

		// Queue the pixels to be copied to system memory
		glReadPixels_mishira(
			0, 0, m_width, m_height, m_bbGLFormat, m_bbGLType, NULL);
		*writePending = true; // Mark PBO as used

		// Restore previous state
		glReadBuffer_mishira(prevReadBuf);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}

	//-------------------------------------------------------------------------
	// Copy previous PBO data to our shared memory if it's valid

	if(*readPending) {
		// Map buffer
		glBindBuffer(GL_PIXEL_PACK_BUFFER, readPbo);
		uchar *ptr = (uchar *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		if(ptr == NULL)
			HookLog2(InterprocessLog::Warning, "Mapped PBO is NULL");

		// Debug capturing by testing the colours of specific pixels
#define DO_PIXEL_DEBUG_TEST 0
#if DO_PIXEL_DEBUG_TEST
		uchar *test = NULL;
#define TEST_PBO_PIXEL(x, y, r, g, b) \
	test = &ptr[((x)+(y)*m_width)*m_bbBpp]; \
	if(test[0] != (b) || test[1] != (g) || test[2] != (r)) \
	HookLog2(InterprocessLog::Warning, stringf( \
	"(%u, %u, %u) != (%u, %u, %u)", test[0], test[1], test[2], (b), (g), (r)))

		// Minecraft main menu (Window size: 854x480)
		//TEST_PBO_PIXEL(4, 6, 255, 255, 255);
		//TEST_PBO_PIXEL(179, 57, 0, 0, 0);
		//TEST_PBO_PIXEL(199, 73, 90, 185, 51);

		// Osmos title screen with ESC menu open (Window size: 1854x962)
		TEST_PBO_PIXEL(878, 302, 255, 255, 255);
		TEST_PBO_PIXEL(876, 391, 125, 141, 160); // Not constant

#undef TEST_PBO_PIXEL
#endif // DO_PIXEL_DEBUG_TEST

		// Copy data to shared memory
		int frameNum = findUnusedFrameNum();
		if(frameNum >= 0) {
			writeRawPixelsToShm(frameNum, timestamp, ptr,
				m_width * m_height * m_bbBpp);
		}

		// Unmap buffer
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

		*readPending = false; // Mark PBO as unused
	}

	//-------------------------------------------------------------------------

	// Did any error occur?
	testForGLError();
}

void GLHook::destructorEndCapturing()
{
	// Destroy our scene objects if they exist. As this object can be deleted
	// outside of a callback we need to make sure that the correct OpenGL
	// context is bound. WARNING: As OpenGL isn't thread-safe there is a chance
	// that if the process is currently using OpenGL it will be using the wrong
	// context for the duration of the following code!
	// HACK: Also update the hook registry here by using `endCapturing()`

	if(!m_sceneObjectsCreated) {
		endCapturing();
		return;
	}

	// Get the current context so we can cover our tracks
	HDC prevDC = wglGetCurrentDC_mishira();
	HGLRC prevGLRC = wglGetCurrentContext_mishira();

	// WARNING: An OpenGL context can only be made active in a single
	// thread at any one time. As there is no way we can guarentee this we
	// just cross our fingers and hope that the following works.

	bool doSwitch = (prevDC != m_hdc);
	bool doDestroy = true;
	if(doSwitch) {
		if(wglMakeCurrent_mishira(m_hdc, m_hglrc) == FALSE) {
			DWORD err = GetLastError();
			HookLog2(InterprocessLog::Warning, stringf(
				"Failed to properly clean up scene objects on destruction. Reason = %u",
				err));
			doDestroy = false;
			doSwitch = false;
		}
	}

	// Destroy objects only if it's safe to do so
	endCapturing(doDestroy);

	// Revert to the previous context
	if(doSwitch)
		wglMakeCurrent_mishira(prevDC, prevGLRC);
}
