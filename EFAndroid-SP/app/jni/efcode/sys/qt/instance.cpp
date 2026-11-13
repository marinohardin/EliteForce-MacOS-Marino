/*
===========================================================================
ef-launcher (Qt 6): single-instance forwarding. See instance.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "instance.h"
#include "ui_common.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <unistd.h>

namespace Instance {

static QString launcherName() { return QString( "ef-launcher-%1" ).arg( getuid() ); }
static QString serverName( long pid ) { return QString( "ef-launcher-server-%1-%2" ).arg( getuid() ).arg( pid ); }

static bool sendTo( const QString &name, const QString &msg )
{
	QLocalSocket s;
	s.connectToServer( name );
	if ( !s.waitForConnected( 500 ) )
		return false;
	s.write( msg.toUtf8() + "\n" );
	s.flush();
	s.waitForBytesWritten( 500 );
	s.disconnectFromServer();
	return true;
}

static void serve( const QString &name, std::function<void( const QString & )> handler )
{
	QLocalServer::removeServer( name );
	QLocalServer *srv = new QLocalServer( qApp );
	srv->setSocketOptions( QLocalServer::UserAccessOption );
	if ( !srv->listen( name ) ) {
		Ui::log( "Launcher: single-instance socket: " + srv->errorString() );
		return;
	}
	QObject::connect( srv, &QLocalServer::newConnection, srv, [srv, handler]() {
		while ( QLocalSocket *c = srv->nextPendingConnection() ) {
			QObject::connect( c, &QLocalSocket::readyRead, c, [c, handler]() {
				while ( c->canReadLine() )
					handler( QString::fromUtf8( c->readLine() ).trimmed() );
			} );
			QObject::connect( c, &QLocalSocket::disconnected, c, &QObject::deleteLater );
		}
	} );
}

bool forwardToLauncher( const QString &message )
{
	return sendTo( launcherName(), message );
}

void listenLauncher( std::function<void( const QString & )> handler )
{
	serve( launcherName(), handler );
}

void listenServer( std::function<void()> raiseWindow )
{
	serve( serverName( getpid() ), [raiseWindow]( const QString &m ) {
		if ( m == "raise" )
			raiseWindow();
	} );
}

bool activateServer( int pid )
{
	return sendTo( serverName( pid ), "raise" );
}

}
