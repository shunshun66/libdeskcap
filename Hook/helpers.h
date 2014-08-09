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

#ifndef HELPERS_H
#define HELPERS_H

#include "../Common/stlincludes.h"
#include <windows.h>
#include <GL/glew.h>

string getD3D9ErrorCode(HRESULT res);
string getDX10ErrorCode(HRESULT res);
string getDX11ErrorCode(HRESULT res);
string getGLErrorCode(GLenum err);

#endif // HELPERS_H
