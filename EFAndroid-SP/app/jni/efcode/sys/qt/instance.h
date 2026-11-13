/*
===========================================================================
ef-launcher (Qt 6): single-instance forwarding of stvef:// links and
activation of a running server window (LDC §7.1, servers.md §1.3).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_INSTANCE_H
#define EF_QT_INSTANCE_H

#include <QString>
#include <functional>

namespace Instance {

/* Send a message ("link <url>" or "raise") to the running launcher instance; true if one
   answered (the caller then exits). */
bool forwardToLauncher( const QString &message );
/* Become the launcher instance others forward to; handler is called on the UI thread. */
void listenLauncher( std::function<void( const QString & )> handler );
/* The --host-server instance listens too, so the launcher can bring its window to the front. */
void listenServer( std::function<void()> raiseWindow );
/* Launcher side: ask the server instance with that pid to come to the front. */
bool activateServer( int pid );

}

#endif
