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

#ifndef COMMON_IMGHELPERS_H
#define COMMON_IMGHELPERS_H

#include "stlincludes.h"

//=============================================================================
// Helper functions

void	imgDataCopy(
	void *dst, void *src, uint dstStride, uint srcStride, int widthBytes,
	int heightRows);

#endif // COMMON_IMGHELPERS_H
