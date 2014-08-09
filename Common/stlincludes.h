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

#ifndef COMMON_STLINCLUDES_H
#define COMMON_STLINCLUDES_H

#include "macros.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Don't pollute the global namespace by only importing the symbols that we
// use very often
using std::cin;
using std::cout;
using std::endl;
using std::string;
using std::wstring;
using std::vector;

#endif // COMMON_STLINCLUDES_H
