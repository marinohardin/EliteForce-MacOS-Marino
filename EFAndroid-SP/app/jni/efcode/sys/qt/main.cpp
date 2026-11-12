/*
===========================================================================
ef-launcher (Qt 6): the Linux launcher executable. Shows the launcher window
and execs efsp on Play; `--host-server` shows only the server window.
Spec: docs/launcher-spec/ARCHITECTURE.md ("Linux"), LDC §3.1, §7.1.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "launcher_window.h"
#include "dialogs.h"
#include "instance.h"

#include <QProxyStyle>
#include <QSocketNotifier>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

int g_argc = 0;
char **g_argv = nullptr;

using namespace Ui;

/* ---- the Steam Deck style: bigger controls for a touch screen / gamepad --------------------------------------- */

namespace {

class DeckStyle : public QProxyStyle {
public:
	DeckStyle() : QProxyStyle( "Fusion" ) {}
	QSize sizeFromContents( ContentsType ct, const QStyleOption *opt, const QSize &sz, const QWidget *w ) const override
	{
		QSize s = QProxyStyle::sizeFromContents( ct, opt, sz, w );
		switch ( ct ) {
		case CT_PushButton:
		case CT_ComboBox:
		case CT_LineEdit:
		case CT_SpinBox:
		case CT_ToolButton:
			s.setHeight( qMax( s.height(), px( 34 ) ) );
			if ( ct == CT_PushButton || ct == CT_ToolButton )
				s.setWidth( qMax( s.width(), px( 40 ) ) );
			break;
		case CT_CheckBox:
		case CT_RadioButton:
			s.setHeight( qMax( s.height(), px( 30 ) ) );
			break;
		default:
			break;
		}
		return s;
	}
	int pixelMetric( PixelMetric m, const QStyleOption *opt, const QWidget *w ) const override
	{
		switch ( m ) {
		case PM_IndicatorWidth:
		case PM_IndicatorHeight:
		case PM_ExclusiveIndicatorWidth:
		case PM_ExclusiveIndicatorHeight:
			return px( 22 );
		case PM_ScrollBarExtent:
			return px( 18 );
		default:
			return QProxyStyle::pixelMetric( m, opt, w );
		}
	}
};

QPalette darkPalette()
{
	QPalette p;
	p.setColor( QPalette::Window, QColor( 40, 40, 42 ) );
	p.setColor( QPalette::WindowText, QColor( 235, 235, 240 ) );
	p.setColor( QPalette::Base, QColor( 30, 30, 32 ) );
	p.setColor( QPalette::AlternateBase, QColor( 44, 44, 46 ) );
	p.setColor( QPalette::ToolTipBase, QColor( 60, 60, 62 ) );
	p.setColor( QPalette::ToolTipText, QColor( 235, 235, 240 ) );
	p.setColor( QPalette::Text, QColor( 235, 235, 240 ) );
	p.setColor( QPalette::Button, QColor( 62, 62, 66 ) );
	p.setColor( QPalette::ButtonText, QColor( 235, 235, 240 ) );
	p.setColor( QPalette::BrightText, Qt::red );
	p.setColor( QPalette::Highlight, QColor( 10, 132, 255 ) );
	p.setColor( QPalette::HighlightedText, Qt::white );
	p.setColor( QPalette::PlaceholderText, QColor( 140, 140, 146 ) );
	p.setColor( QPalette::Disabled, QPalette::Text, QColor( 120, 120, 126 ) );
	p.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( 120, 120, 126 ) );
	p.setColor( QPalette::Disabled, QPalette::WindowText, QColor( 120, 120, 126 ) );
	return p;
}

QPalette lightPalette()
{
	QPalette p;
	p.setColor( QPalette::Window, QColor( 236, 236, 238 ) );
	p.setColor( QPalette::WindowText, QColor( 28, 28, 30 ) );
	p.setColor( QPalette::Base, Qt::white );
	p.setColor( QPalette::AlternateBase, QColor( 245, 245, 247 ) );
	p.setColor( QPalette::Text, QColor( 28, 28, 30 ) );
	p.setColor( QPalette::Button, QColor( 246, 246, 248 ) );
	p.setColor( QPalette::ButtonText, QColor( 28, 28, 30 ) );
	p.setColor( QPalette::Highlight, QColor( 0, 100, 224 ) );
	p.setColor( QPalette::HighlightedText, Qt::white );
	p.setColor( QPalette::PlaceholderText, QColor( 130, 130, 136 ) );
	return p;
}

/* SIGTERM / SIGINT through a self-pipe (servers.md §9.14) */
int g_sigPipe[2] = { -1, -1 };
void onSignal( int ) { char c = 1; ssize_t r = write( g_sigPipe[1], &c, 1 ); (void)r; }

void installSignals( std::function<void()> handler )
{
	if ( socketpair( AF_UNIX, SOCK_STREAM, 0, g_sigPipe ) != 0 )
		return;
	QSocketNotifier *n = new QSocketNotifier( g_sigPipe[0], QSocketNotifier::Read, qApp );
	QObject::connect( n, &QSocketNotifier::activated, qApp, [handler]() {
		char c;
		ssize_t r = read( g_sigPipe[0], &c, 1 );
		(void)r;
		handler();
	} );
	struct sigaction sa;
	memset( &sa, 0, sizeof sa );
	sa.sa_handler = onSignal;
	sigaction( SIGTERM, &sa, nullptr );
	sigaction( SIGINT, &sa, nullptr );
	sigaction( SIGHUP, &sa, nullptr );
	signal( SIGPIPE, SIG_IGN );
}

}	/* namespace */

/* ---- main ------------------------------------------------------------------------------------------------------- */

int main( int argc, char **argv )
{
	g_argc = argc;
	g_argv = argv;

	bool hostOnly = testSet( "VOYAGERSP_TEST_SERVER_START" );
	bool forceLauncher = false, chooseFolder = false;
	QStringList links;
	for ( int i = 1; i < argc; ++i ) {
		const char *a = argv[i];
		if ( !strcmp( a, LC_HOST_SERVER_ARG ) )
			hostOnly = true;
		else if ( !strcmp( a, LC_LAUNCHER_ARG ) )
			forceLauncher = true;
		else if ( !strcmp( a, LC_CHOOSE_FOLDER_ARG ) )
			chooseFolder = true;
		else if ( !strcmp( a, "--uri" ) && i + 1 < argc )
			links << QString::fromUtf8( argv[++i] );
		else if ( lc_is_link_arg( a ) )
			links << QString::fromUtf8( a );
	}

	/* a link: hand it to a running launcher and leave */
	if ( !hostOnly && !links.isEmpty() ) {
		QCoreApplication probe( argc, argv );
		if ( Instance::forwardToLauncher( "link " + links.first() ) )
			return 0;
	}

	if ( Ui::deck() && qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) && !qEnvironmentVariableIsEmpty( "GAMESCOPE_WAYLAND_DISPLAY" ) ) {
		/* gamescope runs XWayland and a Wayland socket; either works, Wayland avoids the xcb deps */
	}

	QApplication app( argc, argv );
	QApplication::setApplicationName( "EliteForce" );
	QApplication::setApplicationDisplayName( "Elite Force" );
	QApplication::setDesktopFileName( "eliteforce-marino" );
	QApplication::setQuitOnLastWindowClosed( false );

	/* look: Fusion with the platform's palette; the Deck gets bigger controls */
	if ( Ui::deck() ) {
		QApplication::setStyle( new DeckStyle );
		QFont f = app.font();
		f.setPixelSize( px( 13 ) );
		app.setFont( f );
		Ui::installDeckNavigation( &app );
	} else
		QApplication::setStyle( "Fusion" );
	const QString ap = testEnv( "VOYAGERSP_TEST_LAUNCHER_APPEARANCE" ).toLower();
	if ( ap == "dark" )
		app.setPalette( darkPalette() );
	else if ( ap == "light" )
		app.setPalette( lightPalette() );
	else if ( qEnvironmentVariableIsEmpty( "DISPLAY" ) && qEnvironmentVariableIsEmpty( "WAYLAND_DISPLAY" )
		&& qgetenv( "QT_QPA_PLATFORM" ) == "offscreen" )
		app.setPalette( lightPalette() );

	/* the app's own log goes to stderr (lc_log's default sink) */

	if ( hostOnly ) {
		/* the --host-server instance: only the server window (servers.md §1.4) */
		LauncherWindow *launcher = new LauncherWindow( true );
		LauncherWindow::HostInfo info = launcher->hostInfo();
		if ( info.dataFolder.isEmpty() && !launcher->canPlay() )
			log( "Server: no game files found" );
		ServerWindow *win = new ServerWindow( *info.settings, info.basepath, info.dataFolder, info.clientArgs );
		Instance::listenServer( [win]() { win->raiseWindow(); } );
		installSignals( [win]() { win->requestStop(); } );
		win->show();
		win->startServer();
		win->runTestHooks();
		const int rc = app.exec();
		log( "Server: window closed" );
		return rc;
	}

	LauncherWindow w( false );
	Instance::listenLauncher( [&w]( const QString &m ) {
		if ( m.startsWith( "link " ) )
			w.deliverLink( m.mid( 5 ) );
		else if ( m == "raise" )
			w.activateSelf();
	} );
	installSignals( [&w]() { w.quit(); } );

	/* register as the stvef: handler when nobody is (a link then reaches us) */
	char other[256] = "";
	if ( qEnvironmentVariableIsSet( "APPIMAGE" ) && lc_link_handler_state( other, sizeof other ) == 0 ) {
		char err[256] = "";
		const int ok = lc_link_register( err, sizeof err );
		log( QString( "Launcher: registered as the stvef:// handler: %1" ).arg( ok ? "done" : U( err ) ) );
	}

	/* Steam Deck: start the last mode without showing the launcher (not when we came back from the game) */
	const bool autoStart = Ui::deck() && lc_store_get_bool( lc_store_shared(), LC_KEY_AUTOLAUNCH, 0 ) && !forceLauncher
		&& !lc_from_launcher() && !lc_env( "EF_NO_LAUNCHER" ) && links.isEmpty() && !chooseFolder
		&& w.canPlay() && testEnv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ).isEmpty();
	if ( autoStart ) {
		log( "Launcher: starting the last mode automatically" );
		QTimer::singleShot( 0, &w, [&w]() { w.play(); } );
	}

	if ( Ui::deck() && qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) )
		w.showMaximized();
	else
		w.show();
	w.raise();
	w.activateWindow();
	w.runTestHooks();
	if ( chooseFolder )
		QTimer::singleShot( 100, &w, [&w]() { w.openChooserSlot(); } );
	if ( !links.isEmpty() ) {
		const QString l = links.first();
		QTimer::singleShot( 300, &w, [&w, l]() { w.deliverLink( l ); } );
	}
	return app.exec();
}
