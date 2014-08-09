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

#include "winhookcapture.h"
#include "include/caplog.h"
#include "hookmanager.h"
#include "wincapturemanager.h"
#include "../Common/capturesharedsegment.h"
#include "../Common/imghelpers.h"
#include "../Common/mainsharedsegment.h"
#include <Libvidgfx/d3dcontext.h>

const QString LOG_CAT = QStringLiteral("WinCapture");

// If set to `1` we will copy the pixel data of DXGI-based shared textures to
// a temporary shared texture for use in the scene instead of using the shared
// texture directly. This was added as an attempt to fix DXGI shared texture
// stutter issues but it had no effect. Instead flushing the context command
// buffer after every queued frame event fixed the issue.
#define COPY_SHARED_TEX_TO_CACHE 0

//=============================================================================
// Helpers

/// <summary>
/// An overload of `imgDataCopy()` that uses Qt datatypes. `size` is the width
/// in bytes and the height in rows.
/// </summary>
static void imgDataCopy(
	quint8 *dst, quint8 *src, uint dstStride, uint srcStride,
	const QSize &size)
{
	imgDataCopy(dst, src, dstStride, srcStride, size.width(), size.height());
}

//=============================================================================
// WinHookCapture class

WinHookCapture::WinHookCapture(HWND hwnd)
	: QObject()
	, m_hwnd(hwnd)
	, m_texture(NULL)
	, m_sharedTexs(NULL)
	, m_activeSharedTex(NULL)
	, m_activeFrameNum(-1)
	, m_numSharedTexs(0)
	, m_isFlipped(false)
	, m_ref(1)
	, m_resourcesInitialized(false)
	, m_capShm(NULL)
{
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	QString title = mgr->getWindowDebugString(static_cast<WinId>(m_hwnd));
	capLog(LOG_CAT) << QStringLiteral("Creating hook capture of window: %1")
		.arg(title);

	// Connect the reset signal and fetch shared segment
	HookManager *hookMgr = mgr->getHookManager();
	connect(hookMgr, &HookManager::windowReset,
		this, &WinHookCapture::windowReset);
	windowReset(static_cast<WinId>(m_hwnd)); // Initializes resources as well
}

WinHookCapture::~WinHookCapture()
{
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	QString title = mgr->getWindowDebugString(static_cast<WinId>(m_hwnd));
	capLog(LOG_CAT) << QStringLiteral("Destroying hook capture of window: %1")
		.arg(title);

	GraphicsContext *gfx = mgr->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		destroyResources(gfx);
}

void WinHookCapture::incrementRef()
{
	m_ref++;
}

void WinHookCapture::release()
{
	m_ref--;
	if(m_ref > 0)
		return;
	WinCaptureManager *mgr =
		static_cast<WinCaptureManager *>(CaptureManager::getManager());
	mgr->releaseHookCapture(this);
}

void WinHookCapture::queuedFrameEvent(uint fNum, int numDropped)
{
	// Update texture size if required
	updateTexture();

	// Sanity check
	if(!(m_texture != NULL || (m_sharedTexs != NULL && m_numSharedTexs > 0 &&
		m_sharedTexs[0] != NULL)))
	{
		return;
	}
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid())
		return;
	if(m_capShm == NULL || !m_capShm->isValid())
		return;

	//-------------------------------------------------------------------------
	// Update texture contents

	m_capShm->lock();

	// Mark dropped frames as used so that we main in sync with the hook. If we
	// don't do this then if we're not broadcasting any captured games will
	// appear to be delayed. We want to keep at least one frame in the queue
	// though otherwise there is a chance we'll never render anything.
	for(int i = 0; i < numDropped; i++) {
		if(m_capShm->getNumUsedFrames() <= 1)
			break;
		int frameNum = m_capShm->findEarliestFrame(true);
		if(frameNum == -1)
			break; // No new frames in queue
		m_capShm->setFrameUsed(frameNum, false);
	}

	// Fetch the earliest frame to use
	int frameNum = m_capShm->findEarliestFrame(true);
	if(frameNum == -1) {
		// No new frames in queue
		m_capShm->unlock();
		return;
	}

	if(m_capShm->getCaptureType() == RawPixelsShmType) {
		quint8 *dataDst = (quint8 *)m_texture->map();
		if(dataDst == NULL) {
			// Error message already logged
			m_capShm->unlock();
			return;
		}
		quint8 *dataSrc = (quint8 *)m_capShm->getFrameDataPtr(frameNum);
		uint bpp = m_capShm->getRawPixelsExtraDataPtr()->bpp;
		uint srcStride = m_texture->getWidth() * bpp;
		imgDataCopy(dataDst, dataSrc, m_texture->getStride(), srcStride,
			QSize(srcStride, m_texture->getHeight()));
		m_capShm->setFrameUsed(frameNum, false); // Frame acknowledged
		m_capShm->unlock();
		m_texture->unmap();

		// Debug capturing by testing the colours of specific pixels
#define DO_PIXEL_DEBUG_TEST 0
#if DO_PIXEL_DEBUG_TEST
		quint8 *test = NULL;
#define TEST_PBO_PIXEL(x, y, r, g, b) \
	test = &dataSrc[((x)+(y)*m_texture->getWidth())*bpp]; \
	if(test[0] != (b) || test[1] != (g) || test[2] != (r)) \
	capLog(CapLog::Warning) << QStringLiteral("(%1, %2, %3) != (%4, %5, %6)") \
	.arg(test[0]).arg(test[1]).arg(test[2]).arg(b).arg(g).arg(r)

		// Minecraft main menu (Window size: 854x480)
		//TEST_PBO_PIXEL(4, 6, 255, 255, 255);
		//TEST_PBO_PIXEL(179, 57, 0, 0, 0);
		//TEST_PBO_PIXEL(199, 73, 90, 185, 51);

		// Osmos title screen with ESC menu open (Window size: 1854x962)
		TEST_PBO_PIXEL(878, 302, 255, 255, 255);
		TEST_PBO_PIXEL(876, 391, 125, 141, 160); // Not constant

#undef TEST_PBO_PIXEL
#endif // DO_PIXEL_DEBUG_TEST
	} else { // Shared DX10 textures
		//capLog() << "Shared tex updated";
		int numUsedFrames = m_capShm->getNumUsedFrames();
		//capLog() << "Used frame = " << frameNum
		//	<< "; Num used frames = " << numUsedFrames
		//	<< "; Num total frames = " << m_capShm->getNumFrames();

		// Use a very quick and easy multiprocess synchronisation system that
		// simply makes sure there is always at least one more frame buffered
		// immediately after our current one. This is because the GPU might not
		// have actually processed our hook's pixel copy commands yet and we do
		// not use any sort of internal DirectX synchronisation mechanism.
		if(numUsedFrames >= 3) { // 1 previous + 1 current + 1 next
			if(m_activeFrameNum != -1) {
				// Mark the frame that we used last iteration as safe to reuse.
				m_capShm->setFrameUsed(m_activeFrameNum, false);
				frameNum = m_capShm->findEarliestFrame(true);
			}
			m_activeFrameNum = frameNum;
			m_activeSharedTex = m_sharedTexs[frameNum];

			// Although this synchronisation works on the CPU it doesn't take
			// into account GPU synchronisation as we may still be rendering
			// the previous frame by the time mark it as unused and the hook
			// starts overriding its pixel data. We rely on the application to
			// flush the graphics command buffer after every queued frame event
			// in order to fix this issue.

#if COPY_SHARED_TEX_TO_CACHE
			gfx->copyTextureData(m_texture, m_activeSharedTex, QPoint(0, 0),
				QRect(QPoint(0, 0), m_texture->getSize()));
#endif // COPY_SHARED_TEX_TO_CACHE
		}
		m_capShm->unlock();
	}
}

void WinHookCapture::initializeResources(GraphicsContext *gfx)
{
	// Because CaptureObjects are referenced by both the CaptureManager and
	// scene layers it is possible for us to receive two initialize signals
	// instead of one.
	// TODO: As CaptureObjects should only ever be used in scene layers we
	// could possibly remove the CaptureManager signal forwarding
	if(m_resourcesInitialized)
		return;
	m_resourcesInitialized = true;

	updateTexture();
}

void WinHookCapture::updateTexture()
{
	if(!m_resourcesInitialized)
		return; // We may receive ticks before being initialized
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx == NULL || !gfx->isValid())
		return;
	if(m_capShm == NULL || !m_capShm->isValid())
		return;

	// Determine the window size
	QSize size(m_capShm->getWidth(), m_capShm->getHeight());

	// Has the window size changed? If so we need to recreate the texture. This
	// should never happen as we should receive a reset signal first but do it
	// just in case anyway.
	if(m_capShm->getCaptureType() == RawPixelsShmType) {
		if(m_texture != NULL && m_texture->getSize() != size) {
			gfx->deleteTexture(m_texture);
			m_texture = NULL;
		}
	} else { // Shared DX10 textures
		if(m_sharedTexs != NULL && m_numSharedTexs != 0) {
			if(m_sharedTexs[0]->getSize() != size) {
				// Deallocate shared texture array
				for(int i = 0; i < m_numSharedTexs; i++)
					gfx->deleteTexture(m_sharedTexs[i]);
				delete[] m_sharedTexs;
				m_sharedTexs = NULL;
				m_numSharedTexs = 0;

#if COPY_SHARED_TEX_TO_CACHE
				// Deallocate cache texture
				gfx->deleteTexture(m_texture);
				m_texture = NULL;
#endif // COPY_SHARED_TEX_TO_CACHE
			}
		}
	}

	// Do not create a texture if we failed to get the window size as the
	// window may no longer exist or if we already have a valid texture
	if(size.isEmpty() || m_texture != NULL || (m_sharedTexs != NULL &&
		m_numSharedTexs > 0 && m_sharedTexs[0] != NULL))
	{
		return;
	}

	if(m_capShm->getCaptureType() == RawPixelsShmType) {
		// Create a new texture
		CaptureSharedSegment::RawPixelsExtraData *extraData =
			m_capShm->getRawPixelsExtraDataPtr();
		RawPixelFormat format = (RawPixelFormat)extraData->format;
		if(format == UnknownPixelFormat) {
			//capLog(LOG_CAT, CapLog::Warning)
			//	<< QStringLiteral("Unknown pixel format");
			return;
		}
		m_isFlipped = (extraData->isFlipped > 0 ? true : false);
		// TODO: We assume BGRA format always
		m_texture = gfx->createTexture(size, true, false, true);
	} else { // Shared DX10 textures
		// Reallocate shared texture array
		m_numSharedTexs = m_capShm->getNumFrames();
		m_sharedTexs = new Texture*[m_numSharedTexs];
		memset(m_sharedTexs, 0, sizeof(Texture *) * m_numSharedTexs);

		// Create shared texture objects
		for(int i = 0; i < m_numSharedTexs; i++) {
			D3DContext *d3dGfx = static_cast<D3DContext *>(gfx);
			HANDLE *handle = (HANDLE *)m_capShm->getFrameDataPtr(i);
			m_sharedTexs[i] = d3dGfx->openSharedTexture(*handle);
		}

		// If we failed to create the first texture then assume that all of
		// them failed
		if(m_sharedTexs[0] == NULL) {
			// Deallocate shared texture array
			for(int i = 0; i < m_numSharedTexs; i++)
				gfx->deleteTexture(m_sharedTexs[i]);
			delete[] m_sharedTexs;
			m_sharedTexs = NULL;
			m_numSharedTexs = 0;
		}

#if COPY_SHARED_TEX_TO_CACHE
		// Allocate the cache texture
		if(m_numSharedTexs > 0) {
			m_texture = gfx->createTexture(
				size, m_sharedTexs[0], false, false);
		}
#endif // COPY_SHARED_TEX_TO_CACHE

		m_isFlipped = false;
	}
}

void WinHookCapture::destroyResources(GraphicsContext *gfx)
{
	if(!m_resourcesInitialized)
		return;
	m_resourcesInitialized = false;

	if(m_texture != NULL) {
		gfx->deleteTexture(m_texture);
		m_texture = NULL;
	}
	if(m_sharedTexs != NULL) {
		for(int i = 0; i < m_numSharedTexs; i++)
			gfx->deleteTexture(m_sharedTexs[i]);
		delete[] m_sharedTexs;
		m_sharedTexs = NULL;
	}
	m_activeSharedTex = NULL;
}

QSize WinHookCapture::getSize() const
{
#if !COPY_SHARED_TEX_TO_CACHE
	if(m_activeSharedTex != NULL)
		return m_activeSharedTex->getSize();
#endif // !COPY_SHARED_TEX_TO_CACHE
	if(m_texture != NULL)
		return m_texture->getSize();
	return QSize();
}

Texture *WinHookCapture::getTexture() const
{
#if !COPY_SHARED_TEX_TO_CACHE
	if(m_activeSharedTex != NULL)
		return m_activeSharedTex;
#endif // !COPY_SHARED_TEX_TO_CACHE
	return m_texture;
}

bool WinHookCapture::isFlipped() const
{
	return m_isFlipped;
}

void WinHookCapture::windowReset(WinId winId)
{
	if(winId != static_cast<WinId>(m_hwnd))
		return; // Not our window

	// Destroy existing resources
	GraphicsContext *gfx = CaptureManager::getManager()->getGraphicsContext();
	if(gfx != NULL && gfx->isValid())
		destroyResources(gfx);

	// Destroy existing shared segment. As the hook already called `remove()`
	// the shared segment will delete itself on OS's that have persistence once
	// we no longer reference it.
	if(m_capShm != NULL)
		delete m_capShm;
	m_capShm = NULL;

	// Fetch information about the new shared segment and connect to it
	HookManager *hookMgr = CaptureManager::getManager()->getHookManager();
	MainSharedSegment *shm = hookMgr->getMainSharedSegment();
	shm->lockHookRegistry();
	HookRegEntry *entry =
		shm->findWindowInHookRegistry(reinterpret_cast<uint32_t>(winId));
	if(entry == NULL) {
		shm->unlockHookRegistry();
		return;
	}
	m_capShm = new CaptureSharedSegment(entry->shmName, entry->shmSize);
	shm->unlockHookRegistry();
	if(!m_capShm->isValid()) {
		delete m_capShm;
		m_capShm = NULL;
		return;
	}

	// Release all queued frames that have buffered already. If we don't do
	// this then we'll be out-of-sync.
	m_capShm->lock();
	for(uint i = 0; i < m_capShm->getNumFrames(); i++) {
		if(m_capShm->isFrameUsed(i))
			m_capShm->setFrameUsed(i, false);
	}
	m_capShm->unlock();
	m_activeFrameNum = -1;

	// Reinitialize resources
	if(gfx != NULL && gfx->isValid())
		initializeResources(gfx);
}
