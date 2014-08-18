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

#include "managedsharedmemory.h"

ManagedSharedMemory::ManagedSharedMemory(const char *name, int size)
#ifdef OS_WIN
	: m_shm()
#else
	: m_shm(open_or_create, name, read_write)
#endif
	, m_region()
	, m_isHeaderAlloc(false)
	, m_unserializeOffset(getStartOffset())
{
	// Construct a shared memory object detecting if it has been previously
	// used before so the memory can be zeroed.
	bool needsZero = false;
#ifdef OS_WIN
	try {
		m_shm = windows_shared_memory(open_only, name, read_write);
	} catch(interprocess_exception) {
		m_shm = windows_shared_memory(create_only, name, read_write, size);
		needsZero = true;
	}
#else
	offset_t oldSize;
	if(!m_shm.get_size(oldSize) || oldSize == 0) {
		m_shm.truncate(size);
		needsZero = true;
	}
#endif

	// Map shared memory to local address space
	m_region = mapped_region(m_shm, read_write);

	// Zero the entire region if required and allocate the header.
	// TODO: We never verify that the region size is large enough to fit the
	// header
	if(needsZero) {
		memset(m_region.get_address(), 0, m_region.get_size());

		m_isHeaderAlloc = true;
		if(getObject<Header>(0) == NULL) {
			// TODO: Failed to allocate header!
		}
		m_isHeaderAlloc = false;
	}
}

ManagedSharedMemory::~ManagedSharedMemory()
{
}

void *ManagedSharedMemory::getAllocation(
	offset_t offset, size_t size, bool *isNew)
{
	if(isNew != NULL)
		*isNew = false;
	size_t regSize = m_region.get_size();

	// Verify that the allocation is inside the memory region and, if it's not
	// the header itself, doesn't overlap the header.
	if((m_isHeaderAlloc && offset < 0) || (!m_isHeaderAlloc &&
		offset < sizeof(Header)) || size + ALLOCATION_OVERHEAD >= regSize)
	{
		return NULL; // Outside valid memory region
	}

	void *addr = (void *)((offset_t)m_region.get_address() + offset);

	// Detect if this was the first time that this allocation was accessed.
	char *head = (char *)addr;
	if(!m_isHeaderAlloc)
		header()->mutex.lock(); // Cannot lock if the header doesn't exist yet!
	if(*head == 0) {
		// This is the first access
		if(isNew != NULL)
			*isNew = true;
		*head = 1;
	}
	if(!m_isHeaderAlloc)
		header()->mutex.unlock();

	return (void *)((offset_t)addr + ALLOCATION_OVERHEAD);
};
