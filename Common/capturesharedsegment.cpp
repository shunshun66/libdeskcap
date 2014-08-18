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

#include "capturesharedsegment.h"
#include "managedsharedmemory.h"
#include "stlhelpers.h"

/// <summary>
/// Creates a new manager assuming that the the shared segment already exists.
/// </summary>
CaptureSharedSegment::CaptureSharedSegment(uint name, uint size)
	: m_shm(NULL)
	, m_isValid(false)
	, m_isCollision(false)
	, m_errorReason()
	, m_segmentName((uint32_t)name)
	, m_segmentSize(size)

	// Data
	, m_lock(NULL)
	, m_captureType(NULL)
	, m_width(NULL)
	, m_height(NULL)
	, m_extraData(NULL)
	, m_numFrames(NULL)
	, m_frameUsed(NULL)
	, m_timestamps(NULL)
	, m_dataStart(NULL)
{
	try {
		string nameStr = stringf("MishiraSHM-%u", m_segmentName);
		m_shm = new ManagedSharedMemory(nameStr.data(), m_segmentSize);

		// Add a version number to the very beginning of the shared segment so
		// that we can detect when we've upgraded Mishira on OS's that have
		// persistent shared segments.
		uchar *version = m_shm->unserialize<uchar>();
		if(*version > 1) {
			m_errorReason = "Unknown version number";
			return;
		}
		*version = 1;

		// Get the addresses of our shared objects
		m_lock = m_shm->unserialize<interprocess_mutex>();
		m_exists = m_shm->unserialize<uchar>();
		if(m_exists == NULL || *m_exists == 0) {
			// Shared segment doesn't already exist, cannot continue
			m_errorReason = "Capture SHM doesn't already exist";
			m_isCollision = true;
			return;
		}
		m_captureType = m_shm->unserialize<uchar>();
		m_width = m_shm->unserialize<uint32_t>();
		m_height = m_shm->unserialize<uint32_t>();
		switch(getCaptureType()) {
		case RawPixelsShmType:
			m_extraData = m_shm->unserialize<RawPixelsExtraData>();
			break;
		default:
		case SharedTextureShmType:
			// Nothing
			break;
		}
		m_numFrames = m_shm->unserialize<uint32_t>();
		m_frameUsed = m_shm->unserialize<uchar>(*m_numFrames);
		m_timestamps = m_shm->unserialize<uint64_t>(*m_numFrames);
		m_dataStart = m_shm->getAllocation(m_shm->getUnserializeOffset(),
			getFrameDataSize() * getNumFrames(), NULL);

		m_isValid = true;
	} catch(interprocess_exception &ex) {
		m_errorReason = string(ex.what());
	}
}

/// <summary>
/// Constructs a new shared segment that contains raw pixel data.
/// </summary>
CaptureSharedSegment::CaptureSharedSegment(
	uint name, uint width, uint height, uint numFrames,
	const RawPixelsExtraData &extra)
	: m_shm(NULL)
	, m_isValid(false)
	, m_isCollision(false)
	, m_errorReason()
	, m_segmentName((uint32_t)name)
	, m_segmentSize(0) // Calculated below

	// Data
	, m_lock(NULL)
	, m_captureType(NULL)
	, m_width(NULL)
	, m_height(NULL)
	, m_extraData(NULL)
	, m_numFrames(NULL)
	, m_frameUsed(NULL)
	, m_timestamps(NULL)
	, m_dataStart(NULL)
{
	constructNew(name, width, height, numFrames, &extra);
}

/// <summary>
/// Constructs a new shared segment that contains shared DX10 texture data.
/// </summary>
CaptureSharedSegment::CaptureSharedSegment(
	uint name, uint width, uint height, uint numFrames,
	const SharedTextureExtraData &extra)
	: m_shm(NULL)
	, m_isValid(false)
	, m_isCollision(false)
	, m_errorReason()
	, m_segmentName((uint32_t)name)
	, m_segmentSize(0) // Calculated below

	// Data
	, m_lock(NULL)
	, m_captureType(NULL)
	, m_width(NULL)
	, m_height(NULL)
	, m_extraData(NULL)
	, m_numFrames(NULL)
	, m_frameUsed(NULL)
	, m_timestamps(NULL)
	, m_dataStart(NULL)
{
	constructNew(name, width, height, numFrames, NULL);
}

void CaptureSharedSegment::constructNew(
	uint name, uint width, uint height, uint numFrames,
	const RawPixelsExtraData *extra)
{
	// Calculate the segment size. WARNING: This is only a rough estimate.
	if(extra != NULL)
		m_segmentSize = 16 * 1024 + numFrames * width * height * extra->bpp;
	else
		m_segmentSize = 16 * 1024 + numFrames * sizeof(uint32_t);

	try {
		string nameStr = stringf("MishiraSHM-%u", m_segmentName);
		m_shm = new ManagedSharedMemory(nameStr.data(), m_segmentSize);

		// Add a version number to the very beginning of the shared segment so
		// that we can detect when we've upgraded Mishira on OS's that have
		// persistent shared segments.
		uchar *version = m_shm->unserialize<uchar>();
		if(*version > 1) {
			m_errorReason = "Unknown version number";
			return;
		}
		*version = 1;

		// Get the addresses of our shared objects
		m_lock = m_shm->unserialize<interprocess_mutex>();
		m_exists = m_shm->unserialize<uchar>();
		if(m_exists == NULL || *m_exists != 0) {
			// Shared segment already exists, cannot continue
			m_errorReason = "Capture SHM already exists";
			m_isCollision = true;
			return;
		}
		*m_exists = 1;
		m_captureType = m_shm->unserialize<uchar>();
		m_width = m_shm->unserialize<uint32_t>();
		*m_width = width;
		m_height = m_shm->unserialize<uint32_t>();
		*m_height = height;
		if(extra != NULL) {
			*m_captureType = RawPixelsShmType;
			m_extraData = m_shm->unserialize<RawPixelsExtraData>();
			memcpy(m_extraData, extra, sizeof(*extra));
		} else
			*m_captureType = SharedTextureShmType;
		m_numFrames = m_shm->unserialize<uint32_t>();
		*m_numFrames = numFrames;
		m_frameUsed = m_shm->unserialize<uchar>(*m_numFrames);
		m_timestamps = m_shm->unserialize<uint64_t>(*m_numFrames);
		m_dataStart = m_shm->getAllocation(m_shm->getUnserializeOffset(),
			getFrameDataSize() * getNumFrames(), NULL);

		m_isValid = true;
	} catch(interprocess_exception &ex) {
		m_errorReason = string(ex.what());
	}
}

CaptureSharedSegment::~CaptureSharedSegment()
{
	// Free the shared memory segment manager. Note that this doesn't actually
	// delete the segment as it's persistent.
	if(m_shm != NULL)
		delete m_shm;
}

/// <summary>
/// Deletes the actual shared memory segment on operating systems that have
/// persistent segments.
/// </summary>
void CaptureSharedSegment::remove()
{
#ifdef OS_WIN
	// Windows automatically deletes once the segment is no longer referenced
#else
#error Unimplemented.
#endif
}

void CaptureSharedSegment::lock()
{
	if(m_lock == NULL)
		return;
	m_lock->lock();
}

void CaptureSharedSegment::unlock()
{
	if(m_lock == NULL)
		return;
	m_lock->unlock();
}

ShmCaptureType CaptureSharedSegment::getCaptureType()
{
	if(m_captureType == NULL)
		return (ShmCaptureType)0;
	return (ShmCaptureType)(*m_captureType);
}

uint CaptureSharedSegment::getWidth()
{
	if(m_width == NULL)
		return 0;
	return *m_width;
}

uint CaptureSharedSegment::getHeight()
{
	if(m_height == NULL)
		return 0;
	return *m_height;
}

CaptureSharedSegment::RawPixelsExtraData *
	CaptureSharedSegment::getRawPixelsExtraDataPtr()
{
	if(m_extraData == NULL)
		return NULL;
	return (RawPixelsExtraData *)m_extraData;
}

uint CaptureSharedSegment::getNumFrames()
{
	if(m_numFrames == NULL)
		return 0;
	return *m_numFrames;
}

bool CaptureSharedSegment::isFrameUsed(uint frameNum)
{
	if(m_numFrames == NULL || m_frameUsed == NULL)
		return false;
	if(frameNum >= *m_numFrames)
		return false;
	return (m_frameUsed[frameNum] != 0 ? true : false);
}

void CaptureSharedSegment::setFrameUsed(uint frameNum, bool used)
{
	if(m_numFrames == NULL || m_frameUsed == NULL)
		return;
	if(frameNum >= *m_numFrames)
		return;
	m_frameUsed[frameNum] = (used ? 1 : 0);
}

uint64_t CaptureSharedSegment::getFrameTimestamp(uint frameNum)
{
	if(m_numFrames == NULL || m_timestamps == NULL)
		return false;
	if(frameNum >= *m_numFrames)
		return false;
	return m_timestamps[frameNum];
}

void CaptureSharedSegment::setFrameTimestamp(uint frameNum, uint64_t timestamp)
{
	if(m_numFrames == NULL || m_timestamps == NULL)
		return;
	if(frameNum >= *m_numFrames)
		return;
	m_timestamps[frameNum] = timestamp;
}

/// <summary>
/// For raw pixels the data is the actual pixel data while for shared textures
/// it is a shared texture handle only.
/// </summary>
void *CaptureSharedSegment::getFrameDataPtr(uint frameNum)
{
	if(m_numFrames == NULL || m_dataStart == NULL)
		return false;
	if(frameNum >= *m_numFrames)
		return false;
	return (void *)((uint64_t)m_dataStart + getFrameDataSize() * frameNum);
}

/// <summary>
/// Calculates the size of each frame element in the data array.
/// </summary>
uint CaptureSharedSegment::getFrameDataSize()
{
	switch(getCaptureType()) {
	default:
		return 0;
	case RawPixelsShmType:
		return getWidth() * getHeight() * getRawPixelsExtraDataPtr()->bpp;
	case SharedTextureShmType:
		return sizeof(uint32_t); // sizeof(HANDLE) is always 32-bit
	}
	// Should never be reached
	return 0;
}

/// <summary>
/// Returns the earliest used or unused frame depending on the `used` argument.
/// If `minTime` is set then only frames that have a timestamp that's equal or
/// greater to the specified time are included (Used to find the first frame
/// after a certain time).
///
/// As hooks should never capture faster than the video framerate we don't
/// actually care what the frame timestamps are when using them, we are only
/// interested in the one with the lowest relative time.
/// </summary>
int CaptureSharedSegment::findEarliestFrame(bool used, uint64_t minTime)
{
	int frameNum = -1;
	uint64_t frameNumTime = UINT64_MAX;
	for(uint i = 0; i < getNumFrames(); i++) {
		if(isFrameUsed(i) == used) {
			uint64_t curTime = getFrameTimestamp(i);
			if(curTime >= minTime && curTime < frameNumTime) {
				// This frame is the earliest so far
				frameNum = i;
				frameNumTime = curTime;
			}
		}
	}
	return frameNum;
}

/// <summary>
/// Helper method for finding the second earliest used frame.
/// </summary>
int CaptureSharedSegment::findSecondEarliestUsedFrame()
{
	int frameNum = findEarliestFrame(true);
	if(frameNum < 0)
		return -1;
	uint64_t time = getFrameTimestamp(frameNum);
	return findEarliestFrame(true, time + 1ULL);
}

int CaptureSharedSegment::getNumUsedFrames()
{
	int numUsedFrames = 0;
	for(uint i = 0; i < getNumFrames(); i++) {
		if(isFrameUsed(i))
			numUsedFrames++;
	}
	return numUsedFrames;
}
