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

#include "helpers.h"
#include "d3dstatics.h"
#include "../Common/stlhelpers.h"
#include <d3d9.h>

// Must be after <windows.h>
#include "glstatics.h"

//=============================================================================
// Helpers

string getD3D9ErrorCode(HRESULT res)
{
	switch(res) {
	case D3DERR_WRONGTEXTUREFORMAT:
		return string("D3DERR_WRONGTEXTUREFORMAT");
	case D3DERR_UNSUPPORTEDCOLOROPERATION:
		return string("D3DERR_UNSUPPORTEDCOLOROPERATION");
	case D3DERR_UNSUPPORTEDCOLORARG:
		return string("D3DERR_UNSUPPORTEDCOLORARG");
	case D3DERR_UNSUPPORTEDALPHAOPERATION:
		return string("D3DERR_UNSUPPORTEDALPHAOPERATION");
	case D3DERR_UNSUPPORTEDALPHAARG:
		return string("D3DERR_UNSUPPORTEDALPHAARG");
	case D3DERR_TOOMANYOPERATIONS:
		return string("D3DERR_TOOMANYOPERATIONS");
	case D3DERR_CONFLICTINGTEXTUREFILTER:
		return string("D3DERR_CONFLICTINGTEXTUREFILTER");
	case D3DERR_UNSUPPORTEDFACTORVALUE:
		return string("D3DERR_UNSUPPORTEDFACTORVALUE");
	case D3DERR_CONFLICTINGRENDERSTATE:
		return string("D3DERR_CONFLICTINGRENDERSTATE");
	case D3DERR_UNSUPPORTEDTEXTUREFILTER:
		return string("D3DERR_UNSUPPORTEDTEXTUREFILTER");
	case D3DERR_CONFLICTINGTEXTUREPALETTE:
		return string("D3DERR_CONFLICTINGTEXTUREPALETTE");
	case D3DERR_DRIVERINTERNALERROR:
		return string("D3DERR_DRIVERINTERNALERROR");
	case D3DERR_NOTFOUND:
		return string("D3DERR_NOTFOUND");
	case D3DERR_MOREDATA:
		return string("D3DERR_MOREDATA");
	case D3DERR_DEVICELOST:
		return string("D3DERR_DEVICELOST");
	case D3DERR_DEVICENOTRESET:
		return string("D3DERR_DEVICENOTRESET");
	case D3DERR_NOTAVAILABLE:
		return string("D3DERR_NOTAVAILABLE");
	case D3DERR_OUTOFVIDEOMEMORY:
		return string("D3DERR_OUTOFVIDEOMEMORY");
	case D3DERR_INVALIDDEVICE:
		return string("D3DERR_INVALIDDEVICE");
	case D3DERR_INVALIDCALL:
		return string("D3DERR_INVALIDCALL");
	case D3DERR_DRIVERINVALIDCALL:
		return string("D3DERR_DRIVERINVALIDCALL");
	case D3DERR_WASSTILLDRAWING:
		return string("D3DERR_WASSTILLDRAWING");
	case D3DOK_NOAUTOGEN:
		return string("D3DOK_NOAUTOGEN");
	case D3DERR_DEVICEREMOVED:
		return string("D3DERR_DEVICEREMOVED");
	case S_NOT_RESIDENT:
		return string("S_NOT_RESIDENT");
	case S_RESIDENT_IN_SHARED_MEMORY:
		return string("S_RESIDENT_IN_SHARED_MEMORY");
	case S_PRESENT_MODE_CHANGED:
		return string("S_PRESENT_MODE_CHANGED");
	case S_PRESENT_OCCLUDED:
		return string("S_PRESENT_OCCLUDED");
	case D3DERR_DEVICEHUNG:
		return string("D3DERR_DEVICEHUNG");
	case D3DERR_UNSUPPORTEDOVERLAY:
		return string("D3DERR_UNSUPPORTEDOVERLAY");
	case D3DERR_UNSUPPORTEDOVERLAYFORMAT:
		return string("D3DERR_UNSUPPORTEDOVERLAYFORMAT");
	case D3DERR_CANNOTPROTECTCONTENT:
		return string("D3DERR_CANNOTPROTECTCONTENT");
	case D3DERR_UNSUPPORTEDCRYPTO:
		return string("D3DERR_UNSUPPORTEDCRYPTO");
	case D3DERR_PRESENT_STATISTICS_DISJOINT:
		return string("D3DERR_PRESENT_STATISTICS_DISJOINT");
	default:
		return numberToHexString((uint64_t)res);
	}
}

string getDX10ErrorCode(HRESULT res)
{
	switch(res) {
	case DXGI_ERROR_INVALID_CALL:
		return string("DXGI_ERROR_INVALID_CALL");
	case DXGI_ERROR_NOT_FOUND:
		return string("DXGI_ERROR_NOT_FOUND");
	case DXGI_ERROR_MORE_DATA:
		return string("DXGI_ERROR_MORE_DATA");
	case DXGI_ERROR_UNSUPPORTED:
		return string("DXGI_ERROR_UNSUPPORTED");
	case DXGI_ERROR_DEVICE_REMOVED:
		return string("DXGI_ERROR_DEVICE_REMOVED");
	case DXGI_ERROR_DEVICE_HUNG:
		return string("DXGI_ERROR_DEVICE_HUNG");
	case DXGI_ERROR_DEVICE_RESET:
		return string("DXGI_ERROR_DEVICE_RESET");
	case DXGI_ERROR_WAS_STILL_DRAWING:
		return string("DXGI_ERROR_WAS_STILL_DRAWING");
	case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:
		return string("DXGI_ERROR_FRAME_STATISTICS_DISJOINT");
	case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:
		return string("DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE");
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
		return string("DXGI_ERROR_DRIVER_INTERNAL_ERROR");
	case DXGI_ERROR_NONEXCLUSIVE:
		return string("DXGI_ERROR_NONEXCLUSIVE");
	case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
		return string("DXGI_ERROR_NOT_CURRENTLY_AVAILABLE");
	case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:
		return string("DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED");
	case DXGI_ERROR_REMOTE_OUTOFMEMORY:
		return string("DXGI_ERROR_REMOTE_OUTOFMEMORY");
	case DXGI_ERROR_ACCESS_LOST:
		return string("DXGI_ERROR_ACCESS_LOST");
	case DXGI_ERROR_WAIT_TIMEOUT:
		return string("DXGI_ERROR_WAIT_TIMEOUT");
	case DXGI_ERROR_SESSION_DISCONNECTED:
		return string("DXGI_ERROR_SESSION_DISCONNECTED");
	case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:
		return string("DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE");
	case DXGI_ERROR_CANNOT_PROTECT_CONTENT:
		return string("DXGI_ERROR_CANNOT_PROTECT_CONTENT");
	case DXGI_ERROR_ACCESS_DENIED:
		return string("DXGI_ERROR_ACCESS_DENIED");
	case DXGI_ERROR_NAME_ALREADY_EXISTS:
		return string("DXGI_ERROR_NAME_ALREADY_EXISTS");
	case DXGI_ERROR_MODE_CHANGE_IN_PROGRESS:
		return string("DXGI_ERROR_MODE_CHANGE_IN_PROGRESS");
	case DXGI_DDI_ERR_WASSTILLDRAWING:
		return string("DXGI_DDI_ERR_WASSTILLDRAWING");
	case DXGI_DDI_ERR_UNSUPPORTED:
		return string("DXGI_DDI_ERR_UNSUPPORTED");
	case DXGI_DDI_ERR_NONEXCLUSIVE:
		return string("DXGI_DDI_ERR_NONEXCLUSIVE");
	case D3D10_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
		return string("D3D10_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS");
	case D3D10_ERROR_FILE_NOT_FOUND:
		return string("D3D10_ERROR_FILE_NOT_FOUND");
	case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
		return string("D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS");
	case D3D11_ERROR_FILE_NOT_FOUND:
		return string("D3D11_ERROR_FILE_NOT_FOUND");
	case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
		return string("D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS");
	case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD:
		return string(
			"D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD");
	case E_UNEXPECTED:
		return string("E_UNEXPECTED");
	case E_NOTIMPL:
		return string("E_NOTIMPL");
	case E_OUTOFMEMORY:
		return string("E_OUTOFMEMORY");
	case E_INVALIDARG:
		return string("E_INVALIDARG");
	case E_NOINTERFACE:
		return string("E_NOINTERFACE");
	case E_POINTER:
		return string("E_POINTER");
	case E_HANDLE:
		return string("E_HANDLE");
	case E_ABORT:
		return string("E_ABORT");
	case E_FAIL:
		return string("E_FAIL");
	case E_ACCESSDENIED:
		return string("E_ACCESSDENIED");
	case S_FALSE:
		return string("S_FALSE");
	case S_OK:
		return string("S_OK");
	default:
		return numberToHexString((uint64_t)res);
	}
}

string getDX11ErrorCode(HRESULT res)
{
	return getDX10ErrorCode(res);
}

string getGLErrorCode(GLenum err)
{
	switch(err) {
	case GL_NO_ERROR:
		return string("GL_NO_ERROR");
	case GL_INVALID_ENUM:
		return string("GL_INVALID_ENUM");
	case GL_INVALID_VALUE:
		return string("GL_INVALID_VALUE");
	case GL_INVALID_OPERATION:
		return string("GL_INVALID_OPERATION");
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		return string("GL_INVALID_FRAMEBUFFER_OPERATION");
	case GL_OUT_OF_MEMORY:
		return string("GL_OUT_OF_MEMORY");
	case GL_STACK_UNDERFLOW:
		return string("GL_STACK_UNDERFLOW");
	case GL_STACK_OVERFLOW:
		return string("GL_STACK_OVERFLOW");
	default:
		return numberToHexString((uint64_t)err);
	}
}
