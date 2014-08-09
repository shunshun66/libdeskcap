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

#include "interprocesslog.h"
#include "stlhelpers.h"
#ifdef OS_WIN
#include <windows.h>
#else
#error Unsupported platform
#endif

InterprocessLog::InterprocessLog()
	: m_mutex()
	//, m_msgs() // Initialized below
	, m_nextMsg(0)
{
	// Zero memory
	memset(m_msgs, 0, sizeof(m_msgs));
}

/// <summary>
/// Empties the log queue and returns its contents.
/// </summary>
vector<InterprocessLog::LogData> InterprocessLog::emptyLog()
{
	vector<LogData> ret;
	m_mutex.lock();
	if(m_nextMsg <= 0) {
		// Log is already empty
		m_mutex.unlock();
		return ret;
	}
	ret.reserve(m_nextMsg);
	for(int i = 0; i < m_nextMsg; i++) {
		LogData data;
		data.lvl = m_msgs[i].lvl;
#pragma warning(push)
#pragma warning(disable: 4996)
		strncpy(data.cat, m_msgs[i].cat, sizeof(data.cat));
		strncpy(data.msg, m_msgs[i].msg, sizeof(data.msg));
#pragma warning(pop)
		ret.push_back(data);
	}
	m_nextMsg = 0;
	m_mutex.unlock();
	return ret;
}

/// <summary>
/// Logs a message.
/// </summary>
void InterprocessLog::log(LogLevel lvl, const string &cat, const string &msg)
{
#ifndef INTERPROCESS_NO_LOG

	m_mutex.lock();
	if(m_nextMsg >= NUM_MSGS) {
		// Queue is full
		m_mutex.unlock();
		return;
	}
	LogData *data = &m_msgs[m_nextMsg];
	data->lvl = lvl;
#pragma warning(push)
#pragma warning(disable: 4996)
	strncpy(data->cat, cat.data(), sizeof(data->cat));
	strncpy(data->msg, msg.data(), sizeof(data->msg));
#pragma warning(pop)
	data->cat[CAT_SIZE-1] = 0; // Just in case the string overflowed
	data->msg[MSG_SIZE-1] = 0; // Just in case the string overflowed
	m_nextMsg++;
	m_mutex.unlock();

#endif // INTERPROCESS_NO_LOG
}

/// <summary>
/// Logs a message with an automatically determined category based on the
/// current process's information.
/// </summary>
void InterprocessLog::log(LogLevel lvl, const string &msg)
{
#ifndef INTERPROCESS_NO_LOG

#ifdef OS_WIN
	DWORD procId = GetCurrentProcessId();
	string cat = stringf("Hook:0x%X", procId);
#else
#error Unsupported platform
#endif

	log(lvl, cat, msg);

#endif // INTERPROCESS_NO_LOG
}

/// <summary>
/// Logs a notice with an automatically determined category based on the
/// current process's information.
/// </summary>
void InterprocessLog::log(const string &msg)
{
#ifndef INTERPROCESS_NO_LOG

	log(Notice, msg);

#endif // INTERPROCESS_NO_LOG
}
