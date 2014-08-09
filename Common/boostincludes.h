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

#ifndef COMMON_BOOSTINCLUDES_H
#define COMMON_BOOSTINCLUDES_H

#include <boost\algorithm\string.hpp>
#include <boost\locale\encoding_utf.hpp>

// Don't pollute the global namespace by only importing the symbols that we
// use very often
using boost::locale::conv::utf_to_utf;

#endif // COMMON_BOOSTINCLUDES_H
