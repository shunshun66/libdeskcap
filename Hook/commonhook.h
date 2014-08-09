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

#ifndef COMMONHOOK_H
#define COMMONHOOK_H

#include "../Common/stlincludes.h"
#include "../Common/capturesharedsegment.h"
#include <windows.h>

class CaptureSharedSegment;

//=============================================================================
/// <summary>
/// Manages a single hooked context. It is the responsibility of this class to
/// advertise to the main application that a window is hookable for accelerated
/// capture and for the actual capture process.
/// </summary>
class CommonHook
{
protected: // Constants -------------------------------------------------------
	static const int MAX_BUFFERED_FRAMES = 15;
	//static const int MAX_BUFFERED_FRAMES = 1;

	// While GPU memory is more scarce we also need a little bit extra for our
	// multiprocess synchronisation. This extra buffer is the equivalent to the
	// PBO's used for CPU capturing.
	static const int MAX_GPU_BUFFERED_FRAMES = 10 + 2;
	//static const int MAX_GPU_BUFFERED_FRAMES = 1;

protected: // Members ---------------------------------------------------------
	HDC		m_hdc;
	HWND	m_hwnd; // HWND of the actual context window
	bool	m_bbIsValidFormat;
	uint	m_bbBpp; // Bytes per pixel
	uint	m_width;
	uint	m_height;

private:
	HWND		m_topHwnd; // HWND of the top-level window that contains `m_hwnd`
	bool		m_fillsWindow;
	bool		m_isCapturing;
	bool		m_isAdvertised;
	CaptureSharedSegment *	m_capShm;
	uint64_t	m_captureUsecOrigin;
	uint64_t	m_prevCaptureFrameNum; // The frame number of the previous captured frame

public: // Constructor/destructor ---------------------------------------------
	CommonHook(HDC hdc);
	void	initialize();
	void	release();
protected:
	virtual	~CommonHook();

public: // Methods ------------------------------------------------------------
	HDC		getHdc() const;
	bool	isCapturing() const;
	bool	isCapturable() const;

	void	processBufferSwap();
	void	processResetBefore();
	void	processResetAfter();
	void	processDeleteContext();

protected:
	void	writeRawPixelsToShm(
		uint frameNum, uint64_t timestamp, void *srcData, size_t srcSize);
	void	writeRawPixelsToShmWithStride(
		uint frameNum, uint64_t timestamp, void *srcData, uint srcStride,
		int widthBytes, int heightRows);
	void	writeSharedTexToShm(uint frameNum, uint64_t timestamp);
	int		findUnusedFrameNum() const;
	bool	isFrameNumUsed(uint frameNum) const;

private:
	void	advertiseWindow();
	void	deadvertiseWindow();
	bool	createCaptureSharedSegment();
	void	beginCapturing();
	void	resetCapturing();
protected: // HACK
	void	endCapturing(bool contextValid = true);

protected: // Interface -------------------------------------------------------
	virtual void			calcBackBufferPixelFormat() = 0;
	virtual RawPixelFormat	getBackBufferPixelFormat() = 0;
	virtual bool			isBackBufferFlipped() = 0;
	virtual	ShmCaptureType	getCaptureType() = 0;
	virtual	HANDLE *		getSharedTexHandles(uint *numTex);
	virtual HWND			getTopLevelHwnd();
	virtual void			getBackBufferSize(
		uint *width, uint *height, int *left = NULL, int *top = NULL);
	virtual void			createSceneObjects() = 0;
	virtual void			destroySceneObjects() = 0;
	virtual void			captureBackBuffer(
		bool captureFrame, uint64_t timestamp) = 0;
	virtual void			destructorEndCapturing() = 0;
};
//=============================================================================

inline HDC CommonHook::getHdc() const
{
	return m_hdc;
}

inline bool CommonHook::isCapturing() const
{
	return m_isCapturing;
}

#endif // COMMONHOOK_H
