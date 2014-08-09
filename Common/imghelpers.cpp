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

#include "imghelpers.h"

/// <summary>
/// An optimized memory copy designed for 2D image transfer that takes into
/// account the row strides of the input and output image buffers. `widthBytes`
/// is the width in bytes (NumPixels * BytesPerPixel) while `heightRows` is the
/// height in rows.
/// </summary>
void imgDataCopy(
	void *dst, void *src, uint dstStride, uint srcStride, int widthBytes,
	int heightRows)
{
	if(widthBytes < 0 || heightRows < 0)
		return; // Width and height must be positive!
	if(dstStride == srcStride) {
		// The input and output buffers are exactly the same size so we can get
		// away with a single memory copy operation
		memcpy(dst, src, dstStride * heightRows);
		return;
	}
	// Copy each row separately
	char *dstChar = (char *)dst;
	char *srcChar = (char *)src;
	for(int i = 0; i < heightRows; i++) {
		memcpy(dstChar, srcChar, widthBytes);
		dstChar += dstStride;
		srcChar += srcStride;
	}
}
