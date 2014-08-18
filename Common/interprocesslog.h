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

#ifndef COMMON_INTERPROCESSLOG_H
#define COMMON_INTERPROCESSLOG_H

#include "stlincludes.h"
#include <boost/interprocess/sync/interprocess_mutex.hpp>
using namespace boost::interprocess;

//=============================================================================
/// <summary>
/// A log queue that can be placed in shared memory in order to writes messages
/// to a log file that is opened in another process. WARNING: If this object is
/// modified then any persistent shared memory segments need to be reset!
///
/// If `INTERPROCESS_NO_LOG` is defined then the `log()` methods are not
/// implemented. This is to prevent dependencies that are only used for
/// automatic log category detection.
/// </summary>
class InterprocessLog
{
public: // Datatypes ----------------------------------------------------------
	static const int	CAT_SIZE = 16;
	static const int	MSG_SIZE = 256;
	static const int	NUM_MSGS = 64;
	enum LogLevel {
		Notice = 0,
		Warning,
		Critical
	};
	struct LogData {
		// TODO: Timestamps?
		unsigned char	lvl;
		char			cat[CAT_SIZE];
		char			msg[MSG_SIZE];
	};

private: // Members -----------------------------------------------------------
	// WARNING: All datatypes must have the same size on both 32- and 64-bit
	// systems as the memory could be shared between processes of different
	// bitness!
	interprocess_mutex	m_mutex;
	LogData				m_msgs[NUM_MSGS];
	unsigned char		m_nextMsg;

public: // Constructor/destructor ---------------------------------------------
	InterprocessLog();

public: // Methods ------------------------------------------------------------
	vector<LogData>	emptyLog();

	void			log(LogLevel lvl, const string &cat, const string &msg);
	void			log(LogLevel lvl, const string &msg);
	void			log(const string &msg);
};
//=============================================================================

#endif // COMMON_INTERPROCESSLOG_H
