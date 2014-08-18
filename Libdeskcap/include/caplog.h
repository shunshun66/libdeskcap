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

#ifndef CAPLOG_H
#define CAPLOG_H

#include "libdeskcap.h"
#include <QtCore/QVector>

class CapLogData;
class QPoint;
class QPointF;
class QRect;
class QRectF;
class QSize;
class QSizeF;

//=============================================================================
class CapLog
{
public: // Datatypes ----------------------------------------------------------
	enum LogLevel {
		Notice = 0,
		Warning,
		Critical
	};

	typedef void CallbackFunc(
		const QString &cat, const QString &msg, LogLevel lvl);

protected: // Static members --------------------------------------------------
	static CallbackFunc *	s_callbackFunc;

public: // Members ------------------------------------------------------------
	CapLogData *	d;

public: // Static methods -----------------------------------------------------
	LDC_EXPORT static void	setCallback(CallbackFunc *funcPtr);

public: // Constructor/destructor ---------------------------------------------
	CapLog();
	CapLog(const CapLog &log);
	~CapLog();
};
CapLog operator<<(CapLog log, const QString &msg);
CapLog operator<<(CapLog log, const QByteArray &msg);
CapLog operator<<(CapLog log, const char *msg);
CapLog operator<<(CapLog log, int msg);
CapLog operator<<(CapLog log, unsigned int msg);
CapLog operator<<(CapLog log, qint64 msg);
CapLog operator<<(CapLog log, quint64 msg);
CapLog operator<<(CapLog log, qreal msg);
CapLog operator<<(CapLog log, float msg);
CapLog operator<<(CapLog log, bool msg);
CapLog operator<<(CapLog log, const QPoint &msg);
CapLog operator<<(CapLog log, const QPointF &msg);
CapLog operator<<(CapLog log, const QRect &msg);
CapLog operator<<(CapLog log, const QRectF &msg);
CapLog operator<<(CapLog log, const QSize &msg);
CapLog operator<<(CapLog log, const QSizeF &msg);
CapLog capLog(const QString &category, CapLog::LogLevel lvl = CapLog::Notice);
CapLog capLog(CapLog::LogLevel lvl = CapLog::Notice);
//=============================================================================

inline void CapLog::setCallback(CallbackFunc *funcPtr)
{
	s_callbackFunc = funcPtr;
}

#endif // CAPLOG_H
