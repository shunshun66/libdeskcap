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

#ifndef COMMON_STLHELPERS_H
#define COMMON_STLHELPERS_H

#include "stlincludes.h"

//=============================================================================
// Helper functions

void *	fastmemcpy(void *dst, const void *src, size_t size);
string	stringf(const string fmt, ...);
string	pointerToString(void *ptr);
string	numberToHexString(uint64_t num);

/// <summary>
/// Returns a pointer that is offset from the input by `offset` bytes.
/// </summary>
inline void *offsetPointer(void *ptr, int offset)
{
	return ((char *)ptr + offset);
}

/// <summary>
/// Returns a pointer to a virtual function's memory address specified by its
/// position in the virtual table.
/// </summary>
inline void *vtableLookup(void *obj, int position)
{
	// If a C++ class has any virtual functions then a pointer to the virtual
	// table is located at the first memory location in the class's memory
	// structure--I.e. a pointer to an object is also a pointer to a pointer to
	// the object's virtual table. The virtual table itself is just a vector of
	// function pointers.
	void **vtable = *(void ***)obj;
	return vtable[position];
}

#endif // COMMON_STLHELPERS_H
