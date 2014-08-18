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

#ifndef COMMON_MACROS_H
#define COMMON_MACROS_H

typedef unsigned char uchar;
typedef unsigned int uint;

//=============================================================================
// What platform are we on?

#if defined(__APPLE__) && (defined(__GNUC__) || defined(__xlC__) || defined(__xlc__))
#define OS_MAC
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define OS_WIN
#else
#error Unknown platform
#endif

//=============================================================================
// Are we 32-bit or 64-bit?

// Windows
#if _WIN32 || _WIN64
#if _WIN64
#define IS64
#else
#define IS32
#endif
#endif

// GCC
#if __GNUC__
#if __x86_64__ || __ppc64__
#define IS64
#else
#define IS32
#endif
#endif

#if !defined(IS32) && !defined(IS64)
#error Unknown bitness
#endif

//=============================================================================
// SSE helper macros

// Aligns a pointer down to the previous 16-byte boundary for SSE
#ifdef IS64
#define alignPtr16Down(x) (void *)((uintptr_t)(x) & 0xFFFFFFFFFFFFFFF0ULL)
#else
#define alignPtr16Down(x) (void *)((uintptr_t)(x) & 0xFFFFFFF0U)
#endif

// Aligns a pointer up to the next 16-byte boundary for SSE
#ifdef IS64
#define alignPtr16Up(x) (void *)(((uintptr_t)(x) + 15ULL) & 0xFFFFFFFFFFFFFFF0ULL)
#else
#define alignPtr16Up(x) (void *)(((uintptr_t)(x) + 15U) & 0xFFFFFFF0U)
#endif

//=============================================================================

#endif // COMMON_MACROS_H
