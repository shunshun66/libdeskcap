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

#include "include/caplog.h"
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QStringList>

//=============================================================================
// CapLogData class

class CapLogData
{
public: // Members ------------------------------------------------------------
	int					ref;
	QString				cat;
	CapLog::LogLevel	lvl;
	QString				msg;

public: // Constructor/destructor ---------------------------------------------
	CapLogData()
		: ref(0)
		, cat()
		, lvl(CapLog::Notice)
		, msg()
	{
	}
};

//=============================================================================
// CapLog class

static void defaultLog(
	const QString &cat, const QString &msg, CapLog::LogLevel lvl)
{
	// No-op
}
CapLog::CallbackFunc *CapLog::s_callbackFunc = &defaultLog;

CapLog::CapLog()
	: d(new CapLogData())
{
	d->ref++;
}

CapLog::CapLog(const CapLog &log)
	: d(log.d)
{
	d->ref++;
}

CapLog::~CapLog()
{
	d->ref--;
	if(d->ref)
		return; // Temporary object

	// Forward to the callback
	if(s_callbackFunc != NULL)
		s_callbackFunc(d->cat, d->msg, d->lvl);

	delete d;
}

CapLog operator<<(CapLog log, const QString &msg)
{
	log.d->msg.append(msg);
	return log;
}

CapLog operator<<(CapLog log, const QByteArray &msg)
{
	return log << QString::fromUtf8(msg);
}

CapLog operator<<(CapLog log, const char *msg)
{
	return log << QString(msg);
}

CapLog operator<<(CapLog log, int msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, unsigned int msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, qint64 msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, quint64 msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, qreal msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, float msg)
{
	return log << QString::number(msg);
}

CapLog operator<<(CapLog log, bool msg)
{
	if(msg)
		return log << QStringLiteral("true");
	return log << QStringLiteral("false");
}

CapLog operator<<(CapLog log, const QPoint &msg)
{
	return log << QStringLiteral("Point(%1, %2)")
		.arg(msg.x())
		.arg(msg.y());
}

CapLog operator<<(CapLog log, const QPointF &msg)
{
	return log << QStringLiteral("Point(%1, %2)")
		.arg(msg.x())
		.arg(msg.y());
}

CapLog operator<<(CapLog log, const QRect &msg)
{
	return log << QStringLiteral("Rect(%1, %2, %3, %4)")
		.arg(msg.left())
		.arg(msg.top())
		.arg(msg.width())
		.arg(msg.height());
}

CapLog operator<<(CapLog log, const QRectF &msg)
{
	return log << QStringLiteral("Rect(%1, %2, %3, %4)")
		.arg(msg.left())
		.arg(msg.top())
		.arg(msg.width())
		.arg(msg.height());
}

CapLog operator<<(CapLog log, const QSize &msg)
{
	return log << QStringLiteral("Size(%1, %2)")
		.arg(msg.width())
		.arg(msg.height());
}

CapLog operator<<(CapLog log, const QSizeF &msg)
{
	return log << QStringLiteral("Size(%1, %2)")
		.arg(msg.width())
		.arg(msg.height());
}

CapLog capLog(const QString &category, CapLog::LogLevel lvl)
{
	CapLog log;
	log.d->cat = category;
	log.d->lvl = lvl;
	return log;
}

CapLog capLog(CapLog::LogLevel lvl)
{
	return capLog("", lvl);
}
