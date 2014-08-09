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

#include "stlhelpers.h"
#include <stdarg.h>

/// <summary>
/// WARNING: UNTESTED UNTESTED UNTESTED UNTESTED UNTESTED
///
/// Provides a faster version of `memcpy()` that permits the compiler to use
/// intrinsics for dynamic copy sizes. Usually the compiler only uses
/// intrinsics for constant sizes which means we cannot get maximum performance
/// out of it for most of our use cases.
/// </summary>
void *fastmemcpy(void *dst, const void *src, size_t size)
{
	void *ret = dst; // Always return the input `dst`

#define ALIGN_DESTINATION 0
#if ALIGN_DESTINATION
	// Align to a 16-byte boundary. Not really that useful at all as both the
	// source and destination need to be aligned in order to make use of SSE.
	void *dstAligned = alignPtr16Up(dst);
	size_t alignedSize = (uintptr_t)dstAligned - (uintptr_t)dst;
	if(alignedSize >= size)
		return memcpy(dst, src, size);
	memcpy(dst, src, alignedSize);
	char *dstChar = (char *)dst + alignedSize;
	char *srcChar = (char *)src + alignedSize;
	size -= alignedSize;
#else
	char *dstChar = (char *)dst;
	char *srcChar = (char *)src;
#endif // ALIGN_DESTINATION

	// Copy using constant sizes with the largest block size possible. Using
	// constants allows the compiler to optimize with intrinsics
#define SIZE_4MB (1024 * 1024 * 4)
#define SIZE_1MB (1024 * 1024 * 1)
#define SIZE_256KB (1024 * 256)
#define SIZE_64KB (1024 * 64)
#define SIZE_16KB (1024 * 16)
#define SIZE_4KB (1024 * 4)
#define SIZE_1KB (1024 * 1)
	while(size >= SIZE_4MB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_4MB;
		srcChar += SIZE_4MB;
		size -= SIZE_4MB;
	}
	while(size >= SIZE_1MB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_1MB;
		srcChar += SIZE_1MB;
		size -= SIZE_1MB;
	}
	while(size >= SIZE_256KB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_256KB;
		srcChar += SIZE_256KB;
		size -= SIZE_256KB;
	}
	while(size >= SIZE_64KB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_64KB;
		srcChar += SIZE_64KB;
		size -= SIZE_64KB;
	}
	while(size >= SIZE_16KB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_16KB;
		srcChar += SIZE_16KB;
		size -= SIZE_16KB;
	}
	while(size >= SIZE_4KB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_4KB;
		srcChar += SIZE_4KB;
		size -= SIZE_4KB;
	}
	while(size >= SIZE_1KB) {
		memcpy(dstChar, srcChar, size);
		dstChar += SIZE_1KB;
		srcChar += SIZE_1KB;
		size -= SIZE_1KB;
	}

	// Copy the remainder
	memcpy(dstChar, srcChar, size);

	return ret;
}

string stringf(const string fmt, ...)
{
	int size = 100;
	string str;
	va_list ap;
	for(;;) {
		str.resize(size);
		va_start(ap, fmt);
#pragma warning(push)
#pragma warning(disable: 4996)
		int n = vsnprintf((char *)str.c_str(), size, fmt.c_str(), ap);
#pragma warning(pop)
		va_end(ap);
		if(n > -1 && n < size) {
			str.resize(n);
			return str;
		}
		if(n > -1)
			size = n + 1;
		else
			size *= 2;
	}
	return str;
}

string pointerToString(void *ptr)
{
	// TODO: Remove excess "0"
	return stringf("0x%p", ptr);
}

string numberToHexString(uint64_t num)
{
	return stringf("0x%X", num);
}
