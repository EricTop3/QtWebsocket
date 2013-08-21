/*
Copyright (C) 2013 Antoine Lafarge qtwebsocket@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "QWsHandshake.h"

#include <QCryptographicHash>
#include <QtEndian>
#include <QHostInfo>
#include <QDataStream>

#include "QWsSocket.h"

QWsHandshake::QWsHandshake(bool clientSide) :
	clientSide(clientSide),
	complete(false),
	httpRequestValid(false)
{
}

QWsHandshake::~QWsHandshake()
{
}

// Read the handshake
// return true if the header is completely readed
// return false if some lines are missing
bool QWsHandshake::read(QTcpSocket* tcpSocket)
{
	QWsSocket::regExpHttpField.setMinimal(true);

	while (tcpSocket->canReadLine())
	{
		// read a line
		QString line = QString::fromLatin1(tcpSocket->readLine());
		rawHandshake += line;
		// check length of line
		if (line.length() > 1000) // incase of garbage input
		{
			return false;
		}
		// start of handshake
		if (!httpRequestValid)
		{
			httpRequestValid = true;
			if (clientSide)
			{
				// CHECK HTTP GET REQUEST
				QWsSocket::regExpHttpRequest.setMinimal(true);
				if (QWsSocket::regExpHttpRequest.indexIn(line) == -1)
				{
					httpRequestValid = false;
					return false;
				}
				// Check HTTP GET REQUEST version
				httpRequestVersion = QWsSocket::regExpHttpRequest.cap(2);
				if (httpRequestVersion.toFloat() < 1.1)
				{
					httpRequestValid = false;
					return false;
				}
				// get Resource name
				resourceName = QWsSocket::regExpHttpRequest.cap(1);
				continue;
			}
			else // serverSide
			{
				// check HTTP code
				QWsSocket::regExpHttpResponse.setMinimal(true);
				if (QWsSocket::regExpHttpResponse.indexIn(line) == -1)
				{
					httpRequestValid = false;
					return false;
				}
				quint16 httpCode = QWsSocket::regExpHttpResponse.cap(1).toUShort();
				QString httpMessage = QWsSocket::regExpHttpResponse.cap(2);
				if (httpCode != 101)
				{
					httpRequestValid = false;
					return false;
				}
			}
		}
		// end of handshake
		if (line == QWsSocket::emptyLine)
		{
			complete = true;
			break;
		}
		// check field
		if (QWsSocket::regExpHttpField.indexIn(line) == -1)
		{
			// Bad http field
			continue;
		}
		// Extract field
		fields.insert(QWsSocket::regExpHttpField.cap(1), QWsSocket::regExpHttpField.cap(2));
	}
	if ((!complete) && (fields.size() > 1000)) // incase of garbage input
	{
		return false;
	}
	// read key3 if existing (for first websocket version)
	if (tcpSocket->bytesAvailable() == 8 && fields.contains(QLatin1String("Sec-WebSocket-Key1")) && fields.contains(QLatin1String("Sec-WebSocket-Key2")))
	{
		key3 = tcpSocket->read(8);
		rawHandshake += QString::fromLatin1(key3);
	}

	return true;
}

bool QWsHandshake::isValid()
{
	// commonPart
	if (!isValidCommonPart())
	{
		return false;
	}
	
	// clientSide
	if (clientSide)
	{
		return isValidClientPart();
	}
	else // serverSide
	{
		return isValidServerPart();
	}
}

bool QWsHandshake::isValidCommonPart()
{
	if (!httpRequestValid)
	{
		return false;
	}
	
	// Upgrade
	if ((!fields.contains(QLatin1String("Upgrade"))) || (fields.value(QLatin1String("Upgrade")).compare(QLatin1String("websocket"), Qt::CaseInsensitive)))
	{
		return false;
	}

	// Connection
	if ((!fields.contains(QLatin1String("Connection"))) || (fields.value(QLatin1String("Connection")).compare(QLatin1String("Upgrade"), Qt::CaseInsensitive)))
	{
		return false;
	}
	
	// Protocol
	if (fields.contains(QLatin1String("Sec-WebSocket-Protocol")))
	{
		protocol = fields.value(QLatin1String("Sec-WebSocket-Protocol"));
	}

	// Extensions
	if (fields.contains(QLatin1String("Sec-WebSocket-Extensions")))
	{
		extensions = fields.value(QLatin1String("Sec-WebSocket-Extensions"));
	}

	return true;
}

bool QWsHandshake::isValidClientPart()
{
	// MANDATORY
	// Host (address & port)
	if (fields.contains(QLatin1String("Host")))
	{
		host = fields.value(QLatin1String("Host"));
		if (host.count(QLatin1Char(':')) <= 1)
		{
			QStringList splitted = host.split(QLatin1Char(':'));
			hostAddress = splitted.first();
			hostPort = splitted.last();
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	
	// Version and keys
	if (fields.contains(QLatin1String("Sec-WebSocket-Version")))
	{
		if (fields.contains(QLatin1String("Sec-WebSocket-Key")))
		{
			version = ((EWebsocketVersion)(fields.value(QLatin1String("Sec-WebSocket-Version")).toUInt()));
			key = fields.value(QLatin1String("Sec-WebSocket-Key")).toLatin1();
		}
		else
		{
			return false;
		}
	}
	else if (fields.contains(QLatin1String("Sec-WebSocket-Key1")) && fields.contains(QLatin1String("Sec-WebSocket-Key2")) && !key3.isEmpty())
	{
		version = WS_V0;
		key1 = fields.value(QLatin1String("Sec-WebSocket-Key1")).toLatin1();
		key2 = fields.value(QLatin1String("Sec-WebSocket-Key2")).toLatin1();
	}
	else
	{
		version = WS_VUnknow;
		return false;
	}

	// OPTIONAL
	// Origin
	if (fields.contains(QLatin1String("Origin")))
	{
		origin = fields.value(QLatin1String("Origin"));
	}
	else if (fields.contains(QLatin1String("Sec-WebSocket-Origin")))
	{
		origin = fields.value(QLatin1String("Sec-WebSocket-Origin"));
	}

	return true;
}

bool QWsHandshake::isValidServerPart()
{	
	// accept
	if (fields.contains(QLatin1String("Sec-WebSocket-Accept")))
	{
		accept = fields.value(QLatin1String("Sec-WebSocket-Accept")).toLatin1();
	}
	else
	{
		return false;
	}

	return true;
}
