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

#ifndef REWRITEHOOK_H
#define REWRITEHOOK_H

//=============================================================================
/// <summary>
/// Hooks a function that already exists in memory by rewriting the executable
/// code to simply jump to our new function.
/// </summary>
class RewriteHook
{
private: // Constants ---------------------------------------------------------
	// Unconditional 32-bit relative near jump. This is available both in x86
	// and x86-64. We use near as we assume that code segments are not used.
	//
	// Assembly (`jmp` is absolute in assembly but machine code is relative):
	//
	//		.byte 0xe9
	//		.long 0x11223344
	//
	// Machine code:
	//
	//	"E9" followed by the 32-bit little-endian offset relative to the next
	// instruction (`addr - REL_JMP_CODE_SIZE`).
	static const int REL_JMP_CODE_SIZE = 5;

	// Unconditional 64-bit absolute far jump using a fake intermediate. The
	// only alternatives require modifying registers or the stack which is not
	// something that we want to do.
	//
	// Assembly:
	//
	//			jmp	[rel foo]
	//	foo:	dq	0x1122334455667788
	//
	// Machine code:
	//
	//	"FF 25 00 00 00 00" followed by the 64-bit little-endian absolute
	//	address.
	static const int ABS64_JMP_CODE_SIZE = 14;

private: // Members -----------------------------------------------------------
	void *			m_funcToHook;
	void *			m_funcToJumpTo;
	bool			m_isHookable;
	bool			m_isHooked;
	unsigned char	m_oldCode[ABS64_JMP_CODE_SIZE]; // Largest size
	unsigned char	m_newCode[ABS64_JMP_CODE_SIZE]; // Largest size
	int				m_codeSize;
	bool			m_protectRevertFailed;
	bool			m_flushFailed;

public: // Constructor/destructor ---------------------------------------------
	RewriteHook(void *funcToHook, void *funcToJumpTo);
	virtual	~RewriteHook();

public: // Methods ------------------------------------------------------------
	bool	isHooked() const;

	bool	install();
	bool	uninstall();

private:
	bool	installUninstall(bool isInstall);
	void	generateCode();
};
//=============================================================================

inline bool RewriteHook::isHooked() const
{
	return m_isHooked;
}

#endif // REWRITEHOOK_H
