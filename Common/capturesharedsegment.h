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

#ifndef COMMON_CAPTURESHAREDSEGMENT_H
#define COMMON_CAPTURESHAREDSEGMENT_H

#include "stlincludes.h"
#include <boost/interprocess/sync/interprocess_mutex.hpp>
using namespace boost::interprocess;

class ManagedSharedMemory;

/// <summary>
/// The format of each pixel in a pixel buffer. All values above 0x80000000 are
/// raw `DXGI_FORMAT` values with the MSB set.
/// </summary>
enum RawPixelFormat {
	UnknownPixelFormat = 0,
	BGRAPixelFormat,
	BGRPixelFormat,
	//ARGBPixelFormat,
	//RGBAPixelFormat,
	//RGBPixelFormat,

	DXGIBeginPixelFormat = 0x80000000,

	ForceUInt32PixelFormat = 0xFFFFFFFF // Used only to enlarge enum size
};

enum ShmCaptureType {
	RawPixelsShmType = 0,
	SharedTextureShmType = 1
};

//=============================================================================
/// <summary>
/// Represents the shared memory segment for interprocess transfer of captured
/// frame data.
/// </summary>
class CaptureSharedSegment
{
public: // Datatypes ----------------------------------------------------------
	struct RawPixelsExtraData {
		uint32_t	format; // See `PixelFormat`
		uint32_t	bpp; // Bytes per pixel
		uint8_t		isFlipped;

		RawPixelsExtraData() : format(0), bpp(0), isFlipped(0) {};
	};
	struct SharedTextureExtraData {
		//uint8_t		unused;

		//SharedTextureExtraData() : unused(0) {};
	};

private: // Members -----------------------------------------------------------
	ManagedSharedMemory *	m_shm;
	bool					m_isValid;
	bool					m_isCollision;
	string					m_errorReason;
	uint					m_segmentName;
	uint					m_segmentSize;

	// Data
	interprocess_mutex *	m_lock;
	uchar *					m_exists; // Used to detect collisions
	uchar *					m_captureType; // See `CaptureType`
	uint32_t *				m_width;
	uint32_t *				m_height;
	void *					m_extraData; // Variable-size, based on type
	uint32_t *				m_numFrames;
	uchar *					m_frameUsed; // Array
	uint64_t *				m_timestamps; // Array
	void *					m_dataStart; // Start of variable-size array

public: // Constructor/destructor ---------------------------------------------
	CaptureSharedSegment(uint name, uint size);
	CaptureSharedSegment(uint name, uint width, uint height, uint numFrames,
		const RawPixelsExtraData &extra);
	CaptureSharedSegment(uint name, uint width, uint height, uint numFrames,
		const SharedTextureExtraData &extra);
	void constructNew(uint name, uint width, uint height, uint numFrames,
		const RawPixelsExtraData *extra);
	virtual ~CaptureSharedSegment();

public: // Methods ------------------------------------------------------------
	bool				isValid() const;
	bool				isCollision() const;
	string				getErrorReason() const;
	uint				getSegmentName() const;
	uint				getSegmentSize() const;
	void				remove();

	void					lock();
	void					unlock();
	ShmCaptureType			getCaptureType();
	uint					getWidth();
	uint					getHeight();
	RawPixelsExtraData *	getRawPixelsExtraDataPtr();
	uint					getNumFrames();
	bool					isFrameUsed(uint frameNum);
	void					setFrameUsed(uint frameNum, bool used);
	uint64_t				getFrameTimestamp(uint frameNum);
	void					setFrameTimestamp(
		uint frameNum, uint64_t timestamp);
	void *					getFrameDataPtr(uint frameNum);
	int						findEarliestFrame(bool used, uint64_t minTime = 0);
	int						findSecondEarliestUsedFrame();
	int						getNumUsedFrames();

	uint				getFrameDataSize();
};
//=============================================================================

inline bool CaptureSharedSegment::isValid() const
{
	return m_isValid;
}

inline bool CaptureSharedSegment::isCollision() const
{
	return m_isCollision;
}

inline string CaptureSharedSegment::getErrorReason() const
{
	return m_errorReason;
}

inline uint CaptureSharedSegment::getSegmentSize() const
{
	return m_segmentSize;
}

inline uint CaptureSharedSegment::getSegmentName() const
{
	return m_segmentName;
}

#endif // COMMON_CAPTURESHAREDSEGMENT_H
