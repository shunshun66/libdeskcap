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

#ifndef COMMON_MANAGEDSHAREDMEMORY_H
#define COMMON_MANAGEDSHAREDMEMORY_H

#include "macros.h"
#ifdef OS_WIN
#include <boost/interprocess/windows_shared_memory.hpp>
#else
#include <boost/interprocess/shared_memory_object.hpp>
#endif
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

using namespace boost::interprocess;

//=============================================================================
/// <summary>
/// While Boost's `managed_shared_memory` basically does what we want it
/// unfortunately doesn't work across the 32<->64-bit boundary. Attempting to
/// open a shared memory segment that was created with another bitness results
/// in crashes or freezes due to the underlying Boost objects used to manage
/// allocations are not set to a fixed size. Because of this we need to
/// implement our own manager using the raw shared memory objects.
///
/// This manager is VERY basic and doesn't support dynamic allocation of data.
/// The only thing it does support is detecting if a memory offset has
/// previously been used in order to allow automatic construction of new
/// objects using their default contructor.
/// </summary>
class ManagedSharedMemory
{
public: // Constants ----------------------------------------------------------
	static const int ALLOCATION_OVERHEAD = 1;

private: // Datatypes ---------------------------------------------------------
	struct Header {
		interprocess_mutex	mutex;
	};

private: // Members -----------------------------------------------------------
#ifdef OS_WIN
	windows_shared_memory	m_shm;
#else
	shared_memory_object	m_shm;
#endif
	mapped_region			m_region;
	bool					m_isHeaderAlloc;
	offset_t				m_unserializeOffset;

public: // Constructor/destructor ---------------------------------------------
	ManagedSharedMemory(const char *name, int size);
	~ManagedSharedMemory();

public: // Methods ------------------------------------------------------------
	offset_t	getStartOffset() const;
	offset_t	getUnserializeOffset() const;
	void		setUnserializeOffset(offset_t offset);
	void		resetUnserializeOffset();
	void *		getAllocation(offset_t offset, size_t size, bool *isNew);

	template <typename T>
	inline T *getObject(offset_t offset, uint size = 1)
	{
		bool isNew = false;
		T *addr = (T *)getAllocation(offset, sizeof(T) * size, &isNew);
		if(isNew) {
			// Construct object on first access
			for(uint i = 0; i < size; i++)
				new(addr + sizeof(T) * i) T();
		}
		return addr;
	};

	template <typename T>
	inline T *unserialize(uint size = 1)
	{
		T *obj = getObject<T>(m_unserializeOffset, size);
		if(obj == NULL)
			return NULL;
		m_unserializeOffset += sizeof(T) * size + ALLOCATION_OVERHEAD;
		return obj;
	};

private:
	Header *	header() const;
};
//=============================================================================

/// <summary>
/// Returns the address of the first byte that can be used to store data. I.e.
/// the address is immediately after the header.
/// </summary>
inline offset_t ManagedSharedMemory::getStartOffset() const
{
	return sizeof(Header) + ALLOCATION_OVERHEAD;
}

inline offset_t ManagedSharedMemory::getUnserializeOffset() const
{
	return m_unserializeOffset;
}

inline void ManagedSharedMemory::setUnserializeOffset(offset_t offset)
{
	m_unserializeOffset = offset;
}

/// <summary>
/// Resets the unserializing system so that offsets can be recalculated from
/// the beginning of the data block.
/// </summary>
inline void ManagedSharedMemory::resetUnserializeOffset()
{
	m_unserializeOffset = getStartOffset();
}

/// <summary>
/// Convenience method to get a pointer to the shared header object.
/// </summary>
inline ManagedSharedMemory::Header *ManagedSharedMemory::header() const
{
	return (Header *)((offset_t)m_region.get_address() + ALLOCATION_OVERHEAD);
}

#endif // COMMON_MANAGEDSHAREDMEMORY_H
