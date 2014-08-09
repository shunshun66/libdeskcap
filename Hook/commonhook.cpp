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

#include "commonhook.h"
#include "hookmain.h"
#include "../Common/interprocesslog.h"
#include "../Common/mainsharedsegment.h"
#include "../Common/stlhelpers.h"
#include "../Common/imghelpers.h"

CommonHook::CommonHook(HDC hdc)
	: m_hdc(hdc)
	, m_hwnd(WindowFromDC(hdc))
	, m_bbIsValidFormat(false)
	, m_bbBpp(0)
	, m_width(0)
	, m_height(0)

	// Private
	, m_topHwnd(NULL)
	, m_fillsWindow(false)
	, m_isCapturing(false)
	, m_isAdvertised(false)
	, m_capShm(NULL)
	, m_captureUsecOrigin(0)
	, m_prevCaptureFrameNum(0)
{
}

/// <summary>
/// Called immediately after construction. This method is required as we need a
/// fully constructed virtual table to properly initialize.
/// </summary>
void CommonHook::initialize()
{
	// Get the top-level window that contains this context
	m_topHwnd = getTopLevelHwnd();

	// Determine the pixel format of the back buffer
	calcBackBufferPixelFormat();

	// Does this graphics context fill the entire window? Some applications
	// such as the Higan SNES emulator have a graphics context that doesn't
	// fill the entire window (Status bar, etc.) but we still want to capture
	// them to increase record performance. As this "fuzzy" comparison results
	// in slight cropping of windows we want an option to disable it.
	int left, top;
	RECT topRect;
	getBackBufferSize(&m_width, &m_height, &left, &top);
	GetClientRect(m_topHwnd, &topRect);
	int winWidth = max(topRect.right - topRect.left, 0);
	int winHeight = max(topRect.bottom - topRect.top, 0);
	m_fillsWindow = false;
	if(left == 0 && top == 0 && m_width == winWidth && m_height == winHeight)
		m_fillsWindow = true;
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	if(!m_fillsWindow && shm->getFuzzyCapture()) {
		// Fuzzy comparison is enabled. Comparison is 5% of the size or 30px,
		// whichever is larger.
		int maxWDiff = max(winWidth / 20, 30);
		int maxHDiff = max(winHeight / 20, 30);
		if((int)m_width >= winWidth - maxWDiff &&
			(int)m_height >= winHeight - maxHDiff)
		{
			m_fillsWindow = true;
		}
		if(!m_fillsWindow) {
			// Do application-specific tests. These applications benefit from
			// having accelerated capture even if the graphics context doesn't
			// fill the entire window
			string filename = HookMain::s_instance->getExeFilename();
			if(filename.compare("higan-accuracy.exe") == 0 ||
				filename.compare("higan-balanced.exe") == 0 ||
				filename.compare("higan-performance.exe") == 0)
			{ // Higan SNES emulator
				m_fillsWindow = true;
			}
		}
		if(m_fillsWindow == true) {
			HookLog(stringf(
				"Fuzzy context window capture triggered on %d x %d context in %d x %d window",
				m_width, m_height, winWidth, winHeight));
		}
	}

	// If the context fills the window and has a known pixel format then
	// advertise it to the main application
	advertiseWindow();

	// Debugging
	//HookLog(stringf(
	//	"Back buffer size: %d x %d; Fills window: %d; Capturable: %d",
	//	m_width, m_height, m_fillsWindow ? 1 : 0, isCapturable() ? 1 : 0));
}

/// <summary>
/// Delete this object. This method is required as we need to use the virtual
/// table before we begin the destructor.
/// </summary>
void CommonHook::release()
{
	// Advertise to the main application that this window is no longer
	// available for accelerated capture.
	deadvertiseWindow();

	// Destroy our scene objects if they exist
	destructorEndCapturing();

	// This should already be destroyed but just in case...
	if(m_capShm != NULL)
		delete m_capShm;
	m_capShm = NULL;

	// Call the child class's destructor
	delete this;
}

CommonHook::~CommonHook()
{
}

/// <summary>
/// Returns true if the target context is actually capturable.
/// </summary>
bool CommonHook::isCapturable() const
{
	if(m_topHwnd == 0)
		return false;
	if(!m_fillsWindow || !m_bbIsValidFormat)
		return false;
	if(m_width == 0 || m_height == 0)
		return false;
	return true;
}

void CommonHook::processBufferSwap()
{
	// Has the window size changed? This should be done first as it can affect
	// whether or not the window capturable.
	uint width, height;
	getBackBufferSize(&width, &height);
	if(m_width != width || m_height != height) {
		bool prevIsCapturable = isCapturable();
		m_width = width;
		m_height = height;
		if(prevIsCapturable == isCapturable()) {
			if(m_isCapturing)
				resetCapturing();
		} else {
			if(isCapturable()) {
				// Window is now capturable
				advertiseWindow();
			} else {
				// Window is no longer capturable
				deadvertiseWindow();
				if(m_isCapturing)
					endCapturing();
			}
		}
	}

	// Test if the main application wants this window captured or not
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	shm->lockHookRegistry();
	HookRegEntry *entry = shm->findWindowInHookRegistry((uint32_t)m_topHwnd);
	if(entry != NULL) {
		bool reqCapture = (entry->flags & HookRegEntry::CaptureFlag);
		shm->unlockHookRegistry();
		if(reqCapture != m_isCapturing) {
			if(reqCapture) {
				// Application requested that we start capturing
				beginCapturing();
			} else {
				// Application requested that we stop capturing
				endCapturing();
			}
		}
	} else
		shm->unlockHookRegistry();

	// Do nothing if we're not capturing this context
	if(!m_isCapturing)
		return;

	//-------------------------------------------------------------------------
	// Capture the buffer making sure that we only capture one frame per video
	// frame period. We need to take into account jitter and we must capture at
	// EXACTLY the same frequency as the main application so all frame times
	// should be relative to an origin. We still need to call
	// `captureBackBuffer()` though as we want to read back any previous frames
	// as quickly as possible

	uint64_t now = HookMain::s_instance->getUsecSinceExec();

	// HACK: As there is jitter between each call to this method there is a
	// chance that our origin will be inside of this jitter region and will
	// result in us missing some frames that appear to be "too early" for us to
	// capture. Attempt to reduce the chance of this by moving the origin
	// slightly away from the timestamp of the first captured frame. As our
	// maximum supported framerate is 60fps (16.7 msec period) we move the
	// origin 5 msec ahead as that's approximately 1/3 of the period.
	const int JITTER_PREVENTION_USEC = 5000; // 5 msec
	if(m_prevCaptureFrameNum == 0 && m_captureUsecOrigin == 0)
		m_captureUsecOrigin = now - JITTER_PREVENTION_USEC;

	// Determine the number of the current frame is relative to our origin
	uint64_t usec = now - m_captureUsecOrigin;
	uint64_t freqNum = (uint64_t)shm->getVideoFrequencyNum();
	uint64_t freqDenom = (uint64_t)shm->getVideoFrequencyDenom();
	uint64_t frameNum = usec * freqNum / freqDenom / 1000000ULL;
	if(frameNum > m_prevCaptureFrameNum) {
		// This is a frame that we should capture
		captureBackBuffer(true, now);
		m_prevCaptureFrameNum = frameNum;
	} else {
		// The game is rendering frames faster than our video framerate, skip
		// this frame as it's not required
		captureBackBuffer(false, now);
	}
}

/// <summary>
/// Called immediately before a DirectX `Reset()` is called. Resets invalidate
/// all texture surfaces so we must destroy our objects before it's called.
/// </summary>
void CommonHook::processResetBefore()
{
	destroySceneObjects();
}

/// <summary>
/// Called immediately after a DirectX `Reset()` is called.
/// </summary>
void CommonHook::processResetAfter()
{
	// Anything can happen after a reset
	calcBackBufferPixelFormat();

	// Recheck buffer size
	uint width, height;
	getBackBufferSize(&width, &height);
	bool prevIsCapturable = isCapturable();
	m_width = width;
	m_height = height;
	if(prevIsCapturable == isCapturable()) {
		if(m_isCapturing)
			resetCapturing();
	} else {
		if(isCapturable()) {
			// Window is now capturable
			advertiseWindow();
		} else {
			// Window is no longer capturable
			deadvertiseWindow();
			if(m_isCapturing)
				endCapturing();
		}
	}
}

void CommonHook::processDeleteContext()
{
	// Advertise to the main application that this window is no longer
	// available for accelerated capture.
	deadvertiseWindow();

	// Stop capturing
	endCapturing();
}

void CommonHook::writeRawPixelsToShm(
	uint frameNum, uint64_t timestamp, void *srcData, size_t srcSize)
{
	// Sanity check inputs
#ifdef _DEBUG
	assert(srcSize == m_width * m_height * m_bbBpp);
#endif
	size_t size = min(srcSize, m_width * m_height * m_bbBpp);

	m_capShm->lock();
	if(!m_capShm->isFrameUsed(frameNum)) {
		m_capShm->setFrameTimestamp(frameNum, timestamp);
		void *dstData = m_capShm->getFrameDataPtr(frameNum);
		memcpy(dstData, srcData, size);
		m_capShm->setFrameUsed(frameNum, true);
	}
	m_capShm->unlock();
}

void CommonHook::writeRawPixelsToShmWithStride(
	uint frameNum, uint64_t timestamp, void *srcData, uint srcStride,
	int widthBytes, int heightRows)
{
	// Sanity check inputs
#ifdef _DEBUG
	assert(widthBytes == m_width * m_bbBpp);
	assert(heightRows == m_height);
#endif
	widthBytes = min(widthBytes, (int)(m_width * m_bbBpp));
	heightRows = min(heightRows, (int)(m_height));

	m_capShm->lock();
	if(!m_capShm->isFrameUsed(frameNum)) {
		m_capShm->setFrameTimestamp(frameNum, timestamp);
		void *dstData = m_capShm->getFrameDataPtr(frameNum);
		imgDataCopy(dstData, srcData, m_width * m_bbBpp, srcStride, widthBytes,
			heightRows);
		m_capShm->setFrameUsed(frameNum, true);
	}
	m_capShm->unlock();
}

void CommonHook::writeSharedTexToShm(uint frameNum, uint64_t timestamp)
{
	m_capShm->lock();
	if(!m_capShm->isFrameUsed(frameNum)) {
		m_capShm->setFrameTimestamp(frameNum, timestamp);
		m_capShm->setFrameUsed(frameNum, true);
	}
	m_capShm->unlock();
}

/// <summary>
/// Finds the first frame in our shared memory segment that is free.
/// </summary>
/// <returns>-1 if all frames are used</returns>
int CommonHook::findUnusedFrameNum() const
{
	// There is no need to lock the registry as we're the only process to ever
	// create new frames
#define IGNORE_UNUSED_TIMESTAMPS 0
#if IGNORE_UNUSED_TIMESTAMPS
	for(uint i = 0; i < m_capShm->getNumFrames(); i++) {
		if(!m_capShm->isFrameUsed(i))
			return i;
	}
	return -1;
#else
	// HACK: In order to reduce the chance of stuttering we use the first
	// unused frame that has the lowest previous timestamp.
	return m_capShm->findEarliestFrame(false);
#endif // IGNORE_UNUSED_TIMESTAMPS
}

bool CommonHook::isFrameNumUsed(uint frameNum) const
{
	// There is no need to lock the registry as we're the only process to ever
	// create new frames
	return m_capShm->isFrameUsed(frameNum);
}

/// <summary>
/// Notify the main application that this window is now available for
/// accelerated capture.
/// </summary>
void CommonHook::advertiseWindow()
{
	if(m_isAdvertised)
		return; // Already advertised
	if(!isCapturable())
		return; // Not capturable

	MainSharedSegment *shm = HookMain::s_instance->getShm();
	HookRegEntry entry;
	entry.winId = (uint32_t)m_topHwnd; // HWNDs are always 32-bit
	entry.hookProcId = GetCurrentProcessId();
	entry.shmName = 0;
	entry.shmSize = 0;
	entry.flags = 0;
	shm->lockHookRegistry();
	shm->addHookRegistry(entry);
	shm->unlockHookRegistry();

	m_isAdvertised = true;
}

/// <summary>
/// Notify the main application that this window is no longer available for
/// accelerated capture.
/// </summary>
void CommonHook::deadvertiseWindow()
{
	if(!m_isAdvertised)
		return; // Already not advertised

	MainSharedSegment *shm = HookMain::s_instance->getShm();
	shm->lockHookRegistry();
	shm->removeHookRegistry((uint32_t)m_topHwnd);
	shm->unlockHookRegistry();

	m_isAdvertised = false;
}

/// <summary>
/// Creates a `CaptureSharedSegment` object for the current capture settings.
/// </summary>
/// <returns>True if the object is valid</returns>
bool CommonHook::createCaptureSharedSegment()
{
	do {
		if(m_capShm != NULL) {
			// We had a collision last iteration
			delete m_capShm;
			m_capShm = NULL;
		}

		if(getCaptureType() == RawPixelsShmType) {
			CaptureSharedSegment::RawPixelsExtraData extra;
			extra.bpp = m_bbBpp;
			extra.format = getBackBufferPixelFormat();
			extra.isFlipped = isBackBufferFlipped() ? 1 : 0;
			m_capShm = new CaptureSharedSegment(
				rand(), m_width, m_height, MAX_BUFFERED_FRAMES, extra);
		} else { // Shared DX10 textures
			uint numFrames = 0;
			HANDLE *handles = getSharedTexHandles(&numFrames);

			// Create shared memory segment
			CaptureSharedSegment::SharedTextureExtraData extra; // Dummy struct
			m_capShm = new CaptureSharedSegment(
				rand(), m_width, m_height, numFrames, extra);

			if(m_capShm->isValid()) {
				// Write handles to shared memory and zero timestamps
				for(uint i = 0; i < numFrames; i++) {
					HANDLE *data = (HANDLE *)m_capShm->getFrameDataPtr(i);
					*data = handles[i];
					m_capShm->setFrameTimestamp(i, 0);
				}
			}
		}
	} while(m_capShm->isCollision());
	if(!m_capShm->isValid()) {
		HookLog2(InterprocessLog::Warning,
			"Failed to create shared memory segment");
		delete m_capShm;
		m_capShm = NULL;
		return false;
	}
	return true;
}

/// <summary>
/// Called exactly once when we begin capturing the window.
/// </summary>
void CommonHook::beginCapturing()
{
	if(m_isCapturing)
		return; // Already capturing
	if(!isCapturable())
		return; // Not capturable

	HookLog("Preparing to begin context capture...");

	// Create our scene objects if we haven't already
	createSceneObjects();

	// Create shared memory segment
	createCaptureSharedSegment();

	// Notify the main application that we have begun to capture
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	shm->lockHookRegistry();
	HookRegEntry *entry = shm->findWindowInHookRegistry((uint32_t)m_topHwnd);
	if(entry != NULL) {
		entry->shmName = m_capShm->getSegmentName();
		entry->shmSize = m_capShm->getSegmentSize();
		entry->flags |= HookRegEntry::ShmValidFlag;
	}
	shm->unlockHookRegistry();

	// Prepare to set the origin for frame capturing to prevent us from
	// capturing more frames that we can use. We actually set the origin when
	// we receive the first frame to capture.
	m_captureUsecOrigin = 0;
	m_prevCaptureFrameNum = 0;

	HookLog("Begun context capture");
	m_isCapturing = true;
}

/// <summary>
/// Called whenever the window size changes.
/// </summary>
void CommonHook::resetCapturing()
{
	if(!m_isCapturing)
		return; // Already not capturing

	HookLog("Preparing to reset context capture...");

	// Lock the hook registry to prevent transient errors in the main app
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	shm->lockHookRegistry();

	// Recreate our scene objects
	destroySceneObjects();
	createSceneObjects();

	// Recreate our `CaptureSharedSegment` object with the new settings
	if(m_capShm != NULL) {
		m_capShm->remove();
		delete m_capShm;
		m_capShm = NULL;
	}
	createCaptureSharedSegment();

	// Find our old hook registry entry and update its settings
	HookRegEntry *entry = shm->findWindowInHookRegistry((uint32_t)m_topHwnd);
	if(entry == NULL) {
		// Should never happen
		shm->unlockHookRegistry();
		return;
	}
	entry->shmName = m_capShm->getSegmentName();
	entry->shmSize = m_capShm->getSegmentSize();
	entry->flags |= HookRegEntry::ShmResetFlag; // Notify that SHM changed
	shm->unlockHookRegistry();

	HookLog("Finished context capture reset");
}

/// <summary>
/// Called exactly once when we finish capturing the window. Should only ever
/// be called by child classes inside of their `destructorEndCapturing()`
/// implementation.
/// </summary>
void CommonHook::endCapturing(bool contextValid)
{
	if(!m_isCapturing)
		return; // Already not capturing

	HookLog("Preparing to finish context capture...");

	// Notify the main application that we have ended our capture
	MainSharedSegment *shm = HookMain::s_instance->getShm();
	shm->lockHookRegistry();
	HookRegEntry *entry = shm->findWindowInHookRegistry((uint32_t)m_topHwnd);
	if(entry != NULL) {
		entry->shmName = 0;
		entry->shmSize = 0;
		// TODO: Do we need to use the reset flag as well?
		entry->flags &= ~HookRegEntry::ShmValidFlag;
	}
	shm->unlockHookRegistry();

	// Delete our scene objects only if the graphics context is valid
	if(contextValid)
		destroySceneObjects();

	// Remove and destroy the shared memory segment
	if(m_capShm != NULL) {
		m_capShm->remove();
		delete m_capShm;
		m_capShm = NULL;
	}

	HookLog("Finished context capture");
	m_isCapturing = false;
}

HANDLE *CommonHook::getSharedTexHandles(uint *numTex)
{
	if(numTex != NULL)
		*numTex = 0;
	return NULL;
}

HWND CommonHook::getTopLevelHwnd()
{
	return GetAncestor(m_hwnd, GA_ROOT);
}

void CommonHook::getBackBufferSize(
	uint *width, uint *height, int *left, int *top)
{
	// Cheat and get the back buffer size from the window size
	RECT rect;
	GetClientRect(m_hwnd, &rect);
	if(width != NULL)
		*width = rect.right - rect.left;
	if(height != NULL)
		*height = rect.bottom - rect.top;
	if(left != NULL)
		*left = rect.left;
	if(top != NULL)
		*top = rect.top;
}
