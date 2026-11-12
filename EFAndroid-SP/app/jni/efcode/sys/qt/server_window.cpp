/*
===========================================================================
ef-launcher (Qt 6): the hosting window (`ef-launcher --host-server`).
servers.md §9, §10.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

#include <QClipboard>
#include <QTextCursor>
#include <unistd.h>

extern char **environ;

using namespace Ui;

namespace {

const char *kDot = " \xc2\xb7 ";

QColor consoleColor( int c )
{
	switch ( c ) {
	case 1: return orange();
	case 2: return red();
	case 3: return green();
	case 4: return secondary();
	case 5: return accent();
	default: return QGuiApplication::palette().color( QPalette::Text );
	}
}

}

struct ServerWindow::Impl {
	ServerWindow *q;
	lc_server_settings s;
	QString basepath, dataFolder;
	QStringList clientArgs;
	const lc_map_catalog *catalog = nullptr;
	QString lanAddress;
	int port;

	/* child + parser */
	lc_srv *srv = nullptr;
	lc_proc *proc = nullptr;
	bool childRunning = false;
	QByteArray exePath;
	lc_srv_state state = LC_SRV_STARTING;
	QString stateDetail;
	QString currentMap;
	QVector<lc_srv_player> players;
	bool ready = false;
	bool stopping = false;
	QTimer pollTimer, killTimer;
	QString stopPidNote;

	/* router mapping */
	enum NatS { NatIdle, NatOpening, NatMapped, NatFailed } natState = NatIdle;
	lc_nat *nat = nullptr;
	QString natPublic, natWhy;
	int natPublicPort = 0;
	QTimer natTimer;

	/* internet listing check */
	bool checking = false;
	int checkCount = 0, maxChecks = 9;
	int firstDelay = 8000, repeatDelay = 20000;
	lc_nat *lookup = nullptr;
	QString lookupAddress;
	QMutex lookupLock;
	QStringList listMasters;
	QVector<int> listState;				/* 0 unchecked, 1 listed, 2 missing, 3 no answer */
	QSet<int> everListed;
	bool checkedOnce = false, checkDoneNothing = false, checkAllDown = false;
	QTimer listTimer;
	bool registered = false;

	/* widgets */
	QLabel *title, *dot, *stateText;
	QLabel *addrValue, *addrHint, *inetIcon, *inetValue, *inetHint, *listStatus, *listHint, *mapValue, *gameValue, *playersValue;
	QPushButton *copyAddr, *copyInet, *checkAgain;
	QWidget *inetRow, *inetHintRow, *listedRow, *listStatusRow, *inetLabelW, *listedLabelW;
	QVBoxLayout *listedBox;
	QLabel *inetLabel, *listedLabel;
	QPushButton *nextMap, *restart, *addBot, *removeBots, *message, *kickBtn;
	QToolButton *changeMap;
	QMenu *mapMenu;
	QTableWidget *table;
	QTextEdit *console;
	QLineEdit *command;
	QPushButton *sendBtn, *playBtn, *stopBtn;
	QPushButton *roles[1];
	int statusRequests = 0;

	void build();
	void showAll();
	void showState();
	void showInfo();
	void showInternet();
	void showListed();
	void showPlayersTable();
	void showControls();
	void startChild();
	void sendCmd( const QString &cmd, bool echo = true );
	void conLog( const QString &line, int color = 0 );
	void becameReady( int boundPort );
	void openRouterPort();
	void natFailedWith( const QString &why );
	void natResult( int r, const QString &ip, int extPort, int err );
	void closeRouterPort();
	void startListCheck();
	void stopListCheck();
	void checkList();
	void listResult( const QVector<int> &st );
	QString masterLabel( const QString &m, const QStringList &all ) const;
	void doKick( int row );
	void doMessage();
	void doPlay();
	void stopThen();
	void childExited( int signaled, int code );
	void doCommand();
	void rebuildMapMenu();
	QString mapTitle( const QString &name, bool withName ) const;
	bool isInternet() const { return s.internet != 0; }
	void registerRunning();

	QString myAddress() const { return QString( "%1:%2" ).arg( lanAddress.isEmpty() ? QString( "127.0.0.1" ) : lanAddress ).arg( port ); }
};

/* ---- construction ------------------------------------------------------------------------------------------------------- */

ServerWindow::ServerWindow( const lc_server_settings &s, const QString &basepath, const QString &dataFolder, const QStringList &clientArgs )
	: QWidget( nullptr ), d( new Impl )
{
	d->q = this;
	d->s = s;
	d->basepath = basepath;
	d->dataFolder = dataFolder;
	d->clientArgs = clientArgs;
	if ( testSet( "VOYAGERSP_TEST_SERVER_INTERNET" ) )
		d->s.internet = d->s.openPort = 1;
	d->port = d->s.port;
	d->catalog = lc_map_catalog_get( B( dataFolder ).constData(), lc_homebase() );
	char lan[64] = "";
	if ( lc_lan_address( lan, sizeof lan ) )
		d->lanAddress = U( lan );
	if ( testSet( "VOYAGERSP_TEST_LIST_FAST" ) ) {
		d->firstDelay = d->repeatDelay = 2000;
		d->maxChecks = 2;
	}
	d->build();
	d->showAll();
}

ServerWindow::~ServerWindow()
{
	if ( d->registered )
		lc_running_unregister( lc_homepath() );
	delete d;
}

void ServerWindow::raiseWindow()
{
	showNormal();
	raise();
	activateWindow();
}

void ServerWindow::Impl::build()
{
	q->setWindowTitle( QString::fromUtf8( "%1 \xc2\xb7 Elite Force Server" ).arg( U( s.hostname ) ) );
	QPixmap pm( ":/icon.png" );
	if ( !pm.isNull() )
		q->setWindowIcon( QIcon( pm ) );
	q->setMinimumSize( px( 640 ), px( 600 ) );
	q->resize( px( 720 ), px( isInternet() ? 840 : 720 ) );
	const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect( 0, 0, 1280, 800 );
	q->resize( q->size().boundedTo( avail.size() - QSize( 20, 40 ) ) );

	QVBoxLayout *outer = new QVBoxLayout( q );
	outer->setContentsMargins( 0, 0, 0, 0 );
	QScrollArea *scroll = new QScrollArea;
	scroll->setFrameShape( QFrame::NoFrame );
	scroll->setWidgetResizable( true );
	QWidget *content = new QWidget;
	scroll->setWidget( content );
	outer->addWidget( scroll );
	QVBoxLayout *root = new QVBoxLayout( content );
	root->setContentsMargins( px( 28 ), px( 28 ), px( 28 ), px( 22 ) );
	root->setSpacing( px( 16 ) );

	/* header */
	QHBoxLayout *hd = new QHBoxLayout;
	hd->setSpacing( px( 14 ) );
	QLabel *ic = new QLabel;
	ic->setFixedSize( px( 52 ), px( 52 ) );
	if ( !pm.isNull() )
		ic->setPixmap( pm.scaled( px( 52 ) * 2, px( 52 ) * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation ).scaledToWidth( px( 52 ), Qt::SmoothTransformation ) );
	hd->addWidget( ic );
	QVBoxLayout *hc = new QVBoxLayout;
	hc->setSpacing( px( 3 ) );
	title = label( U( s.hostname ), 22, QFont::Bold );
	title->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	hc->addWidget( title );
	QHBoxLayout *sr = new QHBoxLayout;
	sr->setSpacing( px( 6 ) );
	dot = new QLabel;
	dot->setFixedSize( px( 10 ), px( 10 ) );
	stateText = label( QString(), 13 );
	sr->addWidget( dot );
	sr->addWidget( stateText );
	sr->addStretch( 1 );
	hc->addLayout( sr );
	hd->addLayout( hc, 1 );
	root->addLayout( hd );
	root->addSpacing( px( 4 ) );

	/* Server group */
	Group *g = new Group( "Server" );
	QGridLayout *gl = new QGridLayout;
	gl->setHorizontalSpacing( px( 10 ) );
	gl->setVerticalSpacing( px( 6 ) );
	gl->setColumnStretch( 1, 1 );
	auto lab = [&]( const QString &t ) {
		QLabel *l = label( t, 13 );
		l->setAlignment( Qt::AlignRight | Qt::AlignTop );
		l->setFixedWidth( px( 80 ) );
		return l;
	};
	int r = 0;
	gl->addWidget( lab( "Address:" ), r, 0 );
	QHBoxLayout *ar = new QHBoxLayout;
	addrValue = label( QString(), 15, QFont::DemiBold );
	addrValue->setFont( font( 15, QFont::DemiBold ) );
	addrValue->setTextInteractionFlags( Qt::TextSelectableByMouse );
	copyAddr = button( "Copy" );
	ar->addWidget( addrValue );
	ar->addWidget( copyAddr );
	ar->addStretch( 1 );
	gl->addLayout( ar, r++, 1 );
	addrHint = label( QString(), 11, QFont::Normal, secondary(), 480 );
	gl->addWidget( addrHint, r++, 1 );

	inetLabel = lab( "Internet:" );
	gl->addWidget( inetLabel, r, 0 );
	inetRow = new QWidget;
	QHBoxLayout *ir = new QHBoxLayout( inetRow );
	ir->setContentsMargins( 0, 0, 0, 0 );
	inetIcon = new QLabel;
	inetIcon->setFixedSize( px( 16 ), px( 16 ) );
	inetValue = label( QString(), 13 );
	inetValue->setTextInteractionFlags( Qt::TextSelectableByMouse );
	copyInet = button( "Copy" );
	ir->addWidget( inetIcon );
	ir->addWidget( inetValue );
	ir->addWidget( copyInet );
	ir->addStretch( 1 );
	gl->addWidget( inetRow, r++, 1 );
	inetHint = label( QString(), 11, QFont::Normal, secondary(), 480 );
	gl->addWidget( inetHint, r++, 1 );

	listedLabel = lab( "Listed:" );
	gl->addWidget( listedLabel, r, 0 );
	listedRow = new QWidget;
	listedBox = new QVBoxLayout( listedRow );
	listedBox->setContentsMargins( 0, 0, 0, 0 );
	listedBox->setSpacing( px( 6 ) );
	gl->addWidget( listedRow, r++, 1 );
	listStatusRow = new QWidget;
	QVBoxLayout *lv = new QVBoxLayout( listStatusRow );
	lv->setContentsMargins( 0, 0, 0, 0 );
	lv->setSpacing( px( 2 ) );
	QHBoxLayout *ls = new QHBoxLayout;
	listStatus = label( QString(), 11, QFont::Normal, secondary() );
	checkAgain = button( "Check Again" );
	ls->addWidget( listStatus );
	ls->addWidget( checkAgain );
	ls->addStretch( 1 );
	lv->addLayout( ls );
	listHint = label( QString(), 11, QFont::Normal, orange(), 480 );
	lv->addWidget( listHint );
	gl->addWidget( listStatusRow, r++, 1 );

	gl->addWidget( lab( "Map:" ), r, 0 );
	mapValue = label( QString(), 13 );
	mapValue->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	gl->addWidget( mapValue, r++, 1 );
	gl->addWidget( lab( "Game:" ), r, 0 );
	gameValue = label( QString(), 13 );
	gl->addWidget( gameValue, r++, 1 );
	gl->addWidget( lab( "Players:" ), r, 0 );
	playersValue = label( QString(), 13 );
	gl->addWidget( playersValue, r++, 1 );
	g->content()->addLayout( gl );
	root->addWidget( g );
	root->addSpacing( -px( 4 ) );

	/* controls */
	QGridLayout *cg = new QGridLayout;
	cg->setSpacing( px( 8 ) );
	nextMap = button( "Next Map", "Go to the next map in the rotation" );
	nextMap->setIcon( icon( "forward.end.fill", QColor(), 13 ) );
	restart = button( "Restart Map", "Restart the current map; scores start over" );
	restart->setIcon( icon( "arrow.counterclockwise", QColor(), 13 ) );
	changeMap = new QToolButton;
	changeMap->setText( "Change Map" );
	changeMap->setIcon( icon( "map", QColor(), 13 ) );
	changeMap->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
	changeMap->setPopupMode( QToolButton::InstantPopup );
	changeMap->setToolTip( "Switch to another map now" );
	mapMenu = new QMenu( changeMap );
	changeMap->setMenu( mapMenu );
	addBot = button( "Add Bot", "Add one bot at the skill set in Config\xe2\x80\xa6" );
	addBot->setIcon( icon( "person.badge.plus", QColor(), 13 ) );
	removeBots = button( "Remove Bots", "Kick all bots (and stop filling the game with bots)" );
	removeBots->setIcon( icon( "person.2.slash", QColor(), 13 ) );
	message = button( QString::fromUtf8( "Message\xe2\x80\xa6" ), "Send a message to everyone on the server" );
	message->setIcon( icon( "text.bubble", QColor(), 13 ) );
	QWidget *cbs[6] = { nextMap, restart, changeMap, addBot, removeBots, message };
	for ( int i = 0; i < 6; ++i ) {
		cbs[i]->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
		cg->addWidget( cbs[i], i / 3, i % 3 );
	}
	root->addLayout( cg );

	/* players and console */
	QHBoxLayout *pc = new QHBoxLayout;
	pc->setSpacing( px( 14 ) );
	QVBoxLayout *left = new QVBoxLayout;
	QHBoxLayout *lh = new QHBoxLayout;
	lh->addWidget( label( "PLAYERS", 11, QFont::DemiBold, secondary() ) );
	lh->addStretch( 1 );
	kickBtn = button( "Kick", "Kick the selected player or bot" );
	lh->addWidget( kickBtn );
	left->addLayout( lh );
	table = new QTableWidget( 0, 3 );
	styleView( table );
	table->setHorizontalHeaderLabels( QStringList() << "Player" << "Score" << "Ping" );
	table->verticalHeader()->hide();
	table->verticalHeader()->setDefaultSectionSize( px( 20 ) );
	table->setSelectionBehavior( QAbstractItemView::SelectRows );
	table->setSelectionMode( QAbstractItemView::SingleSelection );
	table->setShowGrid( false );
	table->setEditTriggers( QAbstractItemView::NoEditTriggers );
	table->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch );
	table->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Fixed );
	table->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Fixed );
	table->setColumnWidth( 1, px( 42 ) );
	table->setColumnWidth( 2, px( 38 ) );
	table->horizontalHeader()->setDefaultAlignment( Qt::AlignLeft | Qt::AlignVCenter );
	table->setContextMenuPolicy( Qt::CustomContextMenu );
	Panel *tb = new Panel;
	QVBoxLayout *tbl = new QVBoxLayout( tb );
	tbl->setContentsMargins( 2, 2, 2, 2 );
	tbl->addWidget( table );
	left->addWidget( tb, 1 );
	QWidget *leftW = new QWidget;
	leftW->setLayout( left );
	left->setContentsMargins( 0, 0, 0, 0 );
	leftW->setFixedWidth( px( 230 ) );
	leftW->setMinimumHeight( px( 160 ) );
	pc->addWidget( leftW );
	QVBoxLayout *right = new QVBoxLayout;
	right->addWidget( label( "CONSOLE", 11, QFont::DemiBold, secondary() ) );
	console = new QTextEdit;
	console->setReadOnly( true );
	console->setFont( font( 11, QFont::Normal, true ) );
	console->setFrameShape( QFrame::NoFrame );
	console->document()->setDocumentMargin( 6 );
	console->setLineWrapMode( QTextEdit::WidgetWidth );
	console->viewport()->setAutoFillBackground( false );
	Panel *cb = new Panel;
	QVBoxLayout *cbl = new QVBoxLayout( cb );
	cbl->setContentsMargins( 2, 2, 2, 2 );
	cbl->addWidget( console );
	right->addWidget( cb, 1 );
	QWidget *rightW = new QWidget;
	rightW->setLayout( right );
	right->setContentsMargins( 0, 0, 0, 0 );
	rightW->setMinimumHeight( px( 160 ) );
	pc->addWidget( rightW, 1 );
	root->addLayout( pc, 1 );

	/* command row */
	QHBoxLayout *cr = new QHBoxLayout;
	command = new QLineEdit;
	command->setPlaceholderText( "Console command, such as say Hello, map hm_borg1 or kick <name>" );
	sendBtn = new QPushButton( "Send" );
	cr->addWidget( command, 1 );
	cr->addWidget( sendBtn );
	root->addLayout( cr );
	root->addSpacing( px( 4 ) );

	/* footer */
	QHBoxLayout *ft = new QHBoxLayout;
	playBtn = new QPushButton( "Play on This " + noun() );
	playBtn->setIcon( icon( "play.fill", QColor(), 13 ) );
	playBtn->setToolTip( "Start Elite Force in Holomatch and join this server" );
	playBtn->setMinimumHeight( px( 30 ) );
	stopBtn = new QPushButton( "Stop Server" );
	stopBtn->setMinimumWidth( px( 110 ) );
	stopBtn->setMinimumHeight( px( 30 ) );
	ft->addWidget( playBtn );
	ft->addStretch( 1 );
	ft->addWidget( stopBtn );
	root->addLayout( ft );

	QObject::connect( copyAddr, &QPushButton::clicked, q, [this]() { QGuiApplication::clipboard()->setText( myAddress() ); } );
	QObject::connect( copyInet, &QPushButton::clicked, q, [this]() {
		if ( natState == NatMapped )
			QGuiApplication::clipboard()->setText( QString( "%1:%2" ).arg( natPublic ).arg( natPublicPort ) );
	} );
	QObject::connect( checkAgain, &QPushButton::clicked, q, [this]() {
		if ( childRunning )
			sendCmd( "heartbeat", false );
		startListCheck();
	} );
	QObject::connect( nextMap, &QPushButton::clicked, q, [this]() { sendCmd( "vstr nextmap" ); } );
	QObject::connect( restart, &QPushButton::clicked, q, [this]() { sendCmd( "map_restart" ); } );
	QObject::connect( mapMenu, &QMenu::aboutToShow, q, [this]() { rebuildMapMenu(); } );
	QObject::connect( addBot, &QPushButton::clicked, q, [this]() {
		char buf[256] = "";
		const char *c = lc_srv_addbot_command( srv, s.botSkill, buf, sizeof buf );
		if ( c && *c )
			sendCmd( U( c ) );
	} );
	QObject::connect( removeBots, &QPushButton::clicked, q, [this]() {
		sendCmd( "set bot_minplayers 0" );
		sendCmd( "kick allbots" );
		conLog( "Bots won't refill this game; Add Bot still adds them.", 4 );
	} );
	QObject::connect( message, &QPushButton::clicked, q, [this]() { doMessage(); } );
	QObject::connect( kickBtn, &QPushButton::clicked, q, [this]() { doKick( table->currentRow() ); } );
	QObject::connect( table, &QTableWidget::customContextMenuRequested, q, [this]( const QPoint &p ) {
		const int row = table->rowAt( p.y() );
		if ( row < 0 || row >= players.size() )
			return;
		QMenu m;
		QAction *k = m.addAction( "Kick" );
		k->setEnabled( childRunning && state == LC_SRV_RUNNING );
		if ( m.exec( table->viewport()->mapToGlobal( p ) ) == k )
			doKick( row );
	} );
	QObject::connect( table, &QTableWidget::itemSelectionChanged, q, [this]() { showControls(); } );
	QObject::connect( command, &QLineEdit::returnPressed, q, [this]() { doCommand(); } );
	QObject::connect( sendBtn, &QPushButton::clicked, q, [this]() { doCommand(); } );
	QObject::connect( playBtn, &QPushButton::clicked, q, [this]() { doPlay(); } );
	QObject::connect( stopBtn, &QPushButton::clicked, q, [this]() {
		if ( state == LC_SRV_STOPPED || state == LC_SRV_FAILED ) {
			QCoreApplication::exit( 0 );
			return;
		}
		stopThen();
	} );
	pollTimer.setInterval( 2000 );
	QObject::connect( &pollTimer, &QTimer::timeout, q, [this]() { if ( srv ) lc_srv_poll_tick( srv, lc_now() ); } );
	killTimer.setSingleShot( true );
	natTimer.setSingleShot( true );
	QObject::connect( &natTimer, &QTimer::timeout, q, [this]() {
		if ( natState == NatOpening )
			natFailedWith( "Your router didn't answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." );
	} );
	listTimer.setSingleShot( true );
	QObject::connect( &listTimer, &QTimer::timeout, q, [this]() { checkList(); } );
	command->setFocus();
}

/* ---- texts ---------------------------------------------------------------------------------------------------------------- */

QString ServerWindow::Impl::mapTitle( const QString &name, bool withName ) const
{
	const lc_map_info *m = catalog ? lc_map_find( catalog, B( name ).constData() ) : nullptr;
	if ( !m )
		return name;
	const QString disp = U( lc_map_display_name( m ) );
	return ( withName && disp != name ) ? QString( "%1 (%2)" ).arg( disp, name ) : disp;
}

void ServerWindow::Impl::showAll()
{
	showState();
	showInfo();
	showInternet();
	showListed();
	showPlayersTable();
	showControls();
}

void ServerWindow::Impl::showState()
{
	QString text;
	QColor dc = orange();
	switch ( state ) {
	case LC_SRV_STARTING: text = QString::fromUtf8( "Starting\xe2\x80\xa6" ); break;
	case LC_SRV_RUNNING:
		text = isInternet() ? "Running \xc2\xb7 on your network and the internet" : "Running \xc2\xb7 on your local network";
		dc = green();
		break;
	case LC_SRV_IDLE: text = "Running, but no map is loaded"; break;
	case LC_SRV_STOPPING: text = QString::fromUtf8( "Stopping\xe2\x80\xa6" ); break;
	case LC_SRV_STOPPED: text = "Stopped"; dc = tertiary(); break;
	case LC_SRV_FAILED: text = "Stopped"; dc = red(); break;
	}
	text = QString::fromUtf8( text.toUtf8() );
	if ( !stateDetail.isEmpty() )
		text += QString::fromUtf8( kDot ) + stateDetail;
	stateText->setText( text );
	setColor( stateText, state == LC_SRV_FAILED ? red() : secondary() );
	dot->setPixmap( iconPixmap( "circle.fill", dc, 10 ) );
}

void ServerWindow::Impl::showInfo()
{
	addrValue->setText( myAddress() );
	QString hint;
	QColor c = secondary();
	const bool disc = port >= 27960 && port <= 27963;
	if ( lanAddress.isEmpty() )
		hint = "This " + nounLc() + " isn't on a network: only Play on This " + noun() + " can join.";
	else if ( !disc ) {
		hint = QString::fromUtf8( "Port %1 is outside 27960\xe2\x80\x93" "27963, so Holomatch's Join menu won't list it on your network: players connect to this address." ).arg( port );
		if ( isInternet() )
			hint += " From the internet: your public address, with the port forwarded.";
		c = orange();
	} else if ( isInternet() )
		hint = "On your network, players find it in Holomatch's Join menu, or connect to this address.";
	else
		hint = "Players on your network find it in Holomatch's Join menu, or connect to this address.";
	addrHint->setText( hint );
	addrHint->setToolTip( hint );
	setColor( addrHint, c );

	QString map = QString::fromUtf8( "\xe2\x80\x94" );
	if ( !currentMap.isEmpty() ) {
		map = mapTitle( currentMap, true );
		if ( s.rotationCount > 1 )
			map += QString( " \xc2\xb7 %1 maps in rotation" ).arg( s.rotationCount );
	}
	mapValue->setText( QString::fromUtf8( map.toUtf8() ) );
	QString game = U( lc_gametype_name( s.gametype ) );
	QStringList mods;
	for ( int i = 0; i < LC_MOD_COUNT; ++i )
		if ( s.mods[i] )
			mods << U( lc_modifier_name( i ) );
	if ( !mods.isEmpty() )
		game += QString::fromUtf8( kDot ) + mods.join( ", " );
	gameValue->setText( game );
	QString pl;
	if ( state == LC_SRV_STARTING || currentMap.isEmpty() )
		pl = QString( "Up to %1" ).arg( s.maxPlayers );
	else {
		int bots = 0;
		for ( const lc_srv_player &p : players )
			if ( p.bot )
				++bots;
		pl = QString( "%1 of %2" ).arg( players.size() ).arg( s.maxPlayers );
		if ( bots > 0 )
			pl += QString::fromUtf8( " \xc2\xb7 %1 bot%2" ).arg( bots ).arg( bots == 1 ? "" : "s" );
	}
	playersValue->setText( pl );
}

void ServerWindow::Impl::showInternet()
{
	const bool inet = isInternet();
	inetLabel->setVisible( inet );
	inetRow->setVisible( inet );
	listedLabel->setVisible( inet );
	listedRow->setVisible( inet );
	if ( !inet ) {
		inetHint->hide();
		listStatusRow->hide();
		return;
	}
	QString manual = QString( "Forward UDP port %1 to this %2 (%3) in your router's settings, or use Tailscale (see the README)." )
		.arg( port ).arg( nounLc(), lanAddress.isEmpty() ? QString( "this %1's address" ).arg( nounLc() ) : lanAddress );
	QString icon_, value, hint;
	QColor ic, hc = secondary();
	bool mono = false, showCopy = false;
	if ( natState == NatMapped ) {
		icon_ = "check.circle.fill";
		ic = green();
		value = QString( "%1:%2" ).arg( natPublic ).arg( natPublicPort );
		mono = true;
		showCopy = true;
		hint = "Your router forwards this address to the server while it runs. Friends on the internet connect to it, or find the server in Holomatch's internet list.";
	} else if ( natState == NatOpening ) {
		icon_ = "arrow.triangle.2.circlepath";
		ic = secondary();
		value = QString::fromUtf8( "Opening the port on your router\xe2\x80\xa6" );
	} else if ( natState == NatFailed ) {
		icon_ = "exclamationmark.triangle.fill";
		ic = orange();
		value = "Port not opened";
		hint = natWhy + " " + manual;
		hc = orange();
	} else if ( !s.openPort ) {
		value = "Your public address";
		hint = "Automatic port opening is off. " + manual;
	} else
		value = childRunning ? QString::fromUtf8( "Waiting for the server\xe2\x80\xa6" ) : QString::fromUtf8( "\xe2\x80\x94" );
	inetIcon->setVisible( !icon_.isEmpty() );
	if ( !icon_.isEmpty() )
		inetIcon->setPixmap( iconPixmap( icon_, ic, 16 ) );
	inetValue->setText( QString::fromUtf8( value.toUtf8() ) );
	inetValue->setFont( font( mono ? 15 : 13, mono ? QFont::DemiBold : QFont::Normal ) );
	copyInet->setVisible( showCopy );
	inetHint->setVisible( !hint.isEmpty() );
	inetHint->setText( hint );
	setColor( inetHint, hc );
}

QString ServerWindow::Impl::masterLabel( const QString &m, const QStringList &all ) const
{
	const int i = all.indexOf( m );
	return ( i >= 0 && i < 5 ) ? QString( "Internet %1" ).arg( i + 1 ) : m;
}

void ServerWindow::Impl::showListed()
{
	if ( !isInternet() )
		return;
	/* clear the rows */
	while ( QLayoutItem *it = listedBox->takeAt( 0 ) ) {
		if ( QWidget *w = it->widget() )
			w->deleteLater();
		else if ( QLayout *l = it->layout() ) {
			while ( QLayoutItem *i2 = l->takeAt( 0 ) )
				if ( i2->widget() )
					i2->widget()->deleteLater();
		}
		delete it;
	}
	char list[LC_MAX_MASTERS][65];
	const int n = lc_masters_load( lc_store_shared(), list );
	QStringList all;
	for ( int i = 0; i < n; ++i )
		all << U( list[i] );
	if ( n == 0 ) {
		listedBox->addWidget( label( "No master servers (see Servers\xe2\x80\xa6 in the launcher)", 12, QFont::Normal, orange() ) );
	}
	for ( int i = 0; i < n; ++i ) {
		const QString lbl = masterLabel( all[i], all );
		const int st = ( listMasters == all && i < listState.size() ) ? listState[i] : 0;
		QString icn, tip;
		QColor c;
		switch ( st ) {
		case 1:
			icn = "check.circle.fill";
			c = green();
			tip = QString( "Listed: players find your server under %1 in the Join menu." ).arg( lbl );
			break;
		case 2:
			icn = "xmark.circle.fill";
			c = red();
			tip = "Not listed: this master answers but doesn't list your server (yet). Some masters list new servers late, or only copy another master's list.";
			break;
		case 3:
			icn = "exclamationmark.circle.fill";
			c = amber();
			tip = "No answer: this master didn't respond.";
			break;
		default:
			icn = "circle.dotted";
			c = tertiary();
			tip = "Not checked yet";
			break;
		}
		QWidget *row = new QWidget;
		QHBoxLayout *h = new QHBoxLayout( row );
		h->setContentsMargins( 0, 0, 0, 0 );
		h->setSpacing( px( 6 ) );
		QLabel *il = new QLabel;
		il->setPixmap( iconPixmap( icn, c, 14 ) );
		il->setFixedSize( px( 14 ), px( 14 ) );
		h->addWidget( il );
		if ( lbl != all[i] ) {
			QLabel *nl = label( lbl, 13 );
			nl->setMinimumWidth( px( 74 ) );
			h->addWidget( nl );
		}
		h->addWidget( label( all[i], 12, QFont::Normal, secondary() ) );
		h->addStretch( 1 );
		row->setToolTip( QString( "%1 (%2): %3" ).arg( lbl, all[i], tip ) );
		listedBox->addWidget( row );
	}

	/* status + hint */
	QString status;
	if ( checking && checkCount == 0 )
		status = QString::fromUtf8( "Checking the masters\xe2\x80\xa6" );
	else if ( checking )
		status = QString( "Checking again\xe2\x80\xa6 (%1 of %2)" ).arg( checkCount ).arg( maxChecks );
	else if ( !checkedOnce && childRunning )
		status = QString::fromUtf8( "Waiting for the server\xe2\x80\xa6" );
	listStatus->setText( status );
	listStatus->setVisible( !status.isEmpty() );
	QString hint;
	if ( checkDoneNothing && !checkAllDown )
		hint = QString( "No master could reach your server, so players outside your network can't either%1. Forward UDP %2 to %3 in your router's settings, "
			"or use Tailscale (see the README)." ).arg( natState == NatMapped ? " (your router accepted the port request but doesn't forward it)" : "" )
			.arg( port ).arg( lanAddress.isEmpty() ? QString( "this %1" ).arg( nounLc() ) : lanAddress );
	listHint->setText( hint );
	listHint->setVisible( !hint.isEmpty() );
	bool listedEverywhere = checkedOnce && !listState.isEmpty();
	for ( int v : listState )
		if ( v == 2 )
			listedEverywhere = false;
	checkAgain->setVisible( childRunning && !checking && !listedEverywhere );
	listStatusRow->setVisible( !status.isEmpty() || !hint.isEmpty() || checkAgain->isVisible() );
}

void ServerWindow::Impl::showPlayersTable()
{
	const int cur = table->currentRow();
	table->setRowCount( 0 );
	table->setRowCount( players.size() );
	const QFont mono = font( 12, QFont::Normal, true );
	for ( int i = 0; i < players.size(); ++i ) {
		const lc_srv_player &p = players[i];
		QTableWidgetItem *n = new QTableWidgetItem( U( p.name ) );
		n->setFont( mono );
		n->setToolTip( p.bot ? QString( "Bot" ) : U( p.address ) );
		QTableWidgetItem *sc = new QTableWidgetItem( U( p.score ) );
		sc->setFont( mono );
		sc->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		QTableWidgetItem *pg = new QTableWidgetItem( U( p.ping ) );
		pg->setFont( mono );
		pg->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		table->setItem( i, 0, n );
		table->setItem( i, 1, sc );
		table->setItem( i, 2, pg );
	}
	if ( cur >= 0 && cur < players.size() )
		table->selectRow( cur );
}

void ServerWindow::Impl::showControls()
{
	const bool can = childRunning && state == LC_SRV_RUNNING;
	for ( QWidget *w : { (QWidget *)nextMap, (QWidget *)restart, (QWidget *)changeMap, (QWidget *)addBot, (QWidget *)removeBots, (QWidget *)message } )
		w->setEnabled( can );
	const int row = table->currentRow();
	kickBtn->setEnabled( can && row >= 0 && row < players.size() );
	playBtn->setEnabled( state == LC_SRV_RUNNING );
	const bool cmdOk = state == LC_SRV_STARTING || state == LC_SRV_RUNNING || state == LC_SRV_IDLE;
	command->setEnabled( cmdOk );
	sendBtn->setEnabled( cmdOk );
	const bool done = state == LC_SRV_STOPPED || state == LC_SRV_FAILED;
	stopBtn->setText( done ? "Close" : "Stop Server" );
	stopBtn->setEnabled( state != LC_SRV_STOPPING );
}

/* ---- console -------------------------------------------------------------------------------------------------------------------- */

void ServerWindow::Impl::conLog( const QString &line, int color )
{
	QScrollBar *sb = console->verticalScrollBar();
	const bool atBottom = sb->value() >= sb->maximum() - 4;
	QTextCursor c( console->document() );
	c.movePosition( QTextCursor::End );
	QTextCharFormat f;
	f.setForeground( consoleColor( color ) );
	c.insertText( line + "\n", f );
	if ( console->document()->characterCount() > 300000 ) {
		QTextCursor t( console->document() );
		t.setPosition( 100000 );
		t.movePosition( QTextCursor::EndOfBlock, QTextCursor::KeepAnchor );
		t.setPosition( 0, QTextCursor::KeepAnchor );
		t.removeSelectedText();
		t.deleteChar();
	}
	if ( atBottom )
		sb->setValue( sb->maximum() );
	if ( testSet( "VOYAGERSP_TEST_SERVER_START" ) )
		fprintf( stderr, "[server] %s\n", line.toUtf8().constData() );
}

void ServerWindow::Impl::sendCmd( const QString &cmd, bool echo )
{
	if ( !childRunning || !proc ) {
		return;
	}
	if ( echo )
		conLog( "] " + cmd, 5 );
	const QByteArray b = cmd.toUtf8() + "\n";
	if ( lc_proc_write( proc, b.constData(), size_t( b.size() ) ) < 0 )
		log( QString( "Server: couldn't send \"%1\": %2" ).arg( cmd, U( strerror( errno ) ) ) );
}

void ServerWindow::Impl::doCommand()
{
	const QString t = command->text().trimmed();
	if ( t.isEmpty() || !childRunning )
		return;
	if ( t.compare( "quit", Qt::CaseInsensitive ) == 0 ) {
		command->clear();
		stopThen();
		return;
	}
	conLog( "] " + t, 5 );
	lc_srv_user_command( srv, B( t ).constData() );
	const QByteArray b = t.toUtf8() + "\n";
	lc_proc_write( proc, b.constData(), size_t( b.size() ) );
	command->clear();
}

/* ---- the child ---------------------------------------------------------------------------------------------------------------- */

void ServerWindow::startServer() { d->startChild(); }

void ServerWindow::Impl::registerRunning()
{
	lc_running_register( lc_homepath(), B( U( s.hostname ) ).constData(), port );
	registered = true;
}

void ServerWindow::Impl::startChild()
{
	registerRunning();

	lc_srv_callbacks cb;
	memset( &cb, 0, sizeof cb );
	cb.user = this;
	cb.send = []( const char *cmd, void *u ) {
		Impl *i = static_cast<Impl *>( u );
		if ( !i->childRunning || !i->proc )
			return;
		const QByteArray b = QByteArray( cmd ) + "\n";
		lc_proc_write( i->proc, b.constData(), size_t( b.size() ) );
	};
	cb.log = []( const char *line, int color, void *u ) { static_cast<Impl *>( u )->conLog( U( line ), color ); };
	cb.state = []( lc_srv_state st, const char *detail, void *u ) {
		Impl *i = static_cast<Impl *>( u );
		i->state = st;
		i->stateDetail = U( detail );
		i->showState();
		i->showControls();
		i->showInfo();
		if ( testSet( "VOYAGERSP_TEST_SERVER_START" ) )
			fprintf( stderr, "[test] status: %d %s\n", int( st ), detail ? detail : "" );
	};
	cb.players = []( const lc_srv_player *p, int n, void *u ) {
		Impl *i = static_cast<Impl *>( u );
		i->players.clear();
		for ( int k = 0; k < n; ++k )
			i->players << p[k];
		i->showPlayersTable();
		i->showInfo();
		i->showControls();
	};
	cb.info = []( const char *map, int boundPort, void *u ) {
		Impl *i = static_cast<Impl *>( u );
		i->currentMap = U( map );
		if ( boundPort > 0 )
			i->port = boundPort;
		i->showInfo();
	};
	cb.ready = []( int boundPort, void *u ) { static_cast<Impl *>( u )->becameReady( boundPort ); };
	srv = lc_srv_new( &cb, port );

	/* vsp_server.cfg (§5.1) */
	char masters[LC_MAX_MASTERS][65];
	const int mc = lc_masters_load( lc_store_shared(), masters );
	char *text = nullptr;
	char err[512] = "";
	const int ok = lc_server_cfg_text( &s, catalog, masters, mc, testSet( "VOYAGERSP_TEST_SERVER_NOMASTER" ), &text, err, sizeof err,
		[]( const char *line, void *u ) { log( U( line ) ); Q_UNUSED( u ); }, nullptr );
	const QString cfgPath = U( lc_homebase() ) + "/vsp_server.cfg";
	bool wrote = false;
	if ( ok && text ) {
		lc_mkdir_p( lc_homebase() );
		wrote = lc_write_file_atomic( B( cfgPath ).constData(), text, strlen( text ) ) != 0;
	}
	free( text );
	if ( !wrote ) {
		conLog( "Couldn't write " + cfgPath + ( err[0] ? QString( ": " ) + U( err ) : QString() ), 2 );
		state = LC_SRV_FAILED;
		stateDetail = "couldn't start";
		showAll();
		return;
	}

	char efsp[LC_MAX_PATH] = "";
	if ( !lc_sibling_exe( "efsp", efsp, sizeof efsp ) )
		lc_strlcpy( efsp, "efsp", sizeof efsp );
	exePath = efsp;
	QStringList args;
	args << "+set" << "com_holomatch" << "1" << "+set" << "dedicated" << ( isInternet() ? "2" : "1" ) << "+set" << "net_port" << QString::number( port );
	if ( !basepath.isEmpty() )
		args << "+set" << "fs_basepath" << basepath;
	args << "+exec" << "vsp_server.cfg";
	QList<QByteArray> avs;
	avs << QByteArray( efsp );
	for ( const QString &a : args )
		avs << B( a );
	QVector<const char *> argv;
	for ( const QByteArray &a : avs )
		argv << a.constData();
	argv << nullptr;

	QList<QByteArray> envs;
	for ( char **e = environ; e && *e; ++e )
		if ( strncmp( *e, "VOYAGERSP_TEST_", 15 ) != 0 )
			envs << QByteArray( *e );
	envs << "EF_NO_LAUNCHER=1";
	QVector<const char *> envp;
	for ( const QByteArray &e : envs )
		envp << e.constData();
	envp << nullptr;

	proc = lc_proc_spawn( efsp, argv.constData(), envp.constData(),
		[]( const char *b, size_t n, void *u ) {
			Impl *i = static_cast<Impl *>( u );
			const QByteArray data( b, int( n ) );
			QMetaObject::invokeMethod( i->q, [i, data]() { if ( i->srv ) lc_srv_feed( i->srv, data.constData(), size_t( data.size() ) ); }, Qt::QueuedConnection );
		},
		[]( int signaled, int code, void *u ) {
			Impl *i = static_cast<Impl *>( u );
			QMetaObject::invokeMethod( i->q, [i, signaled, code]() { i->childExited( signaled, code ); }, Qt::QueuedConnection );
		}, this );
	if ( !proc ) {
		conLog( QString( "Couldn't start the server: %1" ).arg( U( strerror( errno ) ) ), 2 );
		state = LC_SRV_FAILED;
		stateDetail = "couldn't start";
		showAll();
		return;
	}
	childRunning = true;
	log( QString( "Server: starting %1 %2" ).arg( U( efsp ), args.join( ' ' ) ) );
	conLog( "Starting the server: " + args.join( ' ' ), 4 );
	pollTimer.start();
	showAll();
}

void ServerWindow::Impl::becameReady( int boundPort )
{
	if ( ready )
		return;
	ready = true;
	if ( boundPort > 0 )
		port = boundPort;
	log( QString( "Server: ready on %1 (map %2)" ).arg( myAddress(), currentMap ) );
	registerRunning();
	showInfo();
	if ( isInternet() && s.openPort )
		openRouterPort();
	if ( isInternet() )
		startListCheck();
	/* test hooks */
	const QString cmd = testEnv( "VOYAGERSP_TEST_SERVER_COMMAND" );
	if ( !cmd.isEmpty() )
		sendCmd( cmd );
	const QString acts = testEnv( "VOYAGERSP_TEST_SERVER_ACTIONS" );
	int delay = 0;
	for ( const QString &a : acts.split( ',', Qt::SkipEmptyParts ) ) {
		delay += 4000;
		const QString act = a.trimmed();
		QTimer::singleShot( delay, q, [this, act]() {
			if ( act == "next" )
				nextMap->click();
			else if ( act == "restart" )
				restart->click();
			else if ( act == "addbot" )
				addBot->click();
			else if ( act == "removebots" )
				removeBots->click();
			else if ( act.startsWith( "slot" ) )
				sendCmd( QString( "vstr vsp_map%1" ).arg( act.mid( 4 ).toInt() ) );
			else if ( act.startsWith( "map:" ) )
				sendCmd( "map " + act.mid( 4 ) );
			else if ( act.startsWith( "kick" ) )
				doKick( act.mid( 4 ).toInt() );
		} );
	}
}

void ServerWindow::Impl::childExited( int signaled, int code )
{
	log( QString( "Server: exited (%1 %2)" ).arg( signaled ? "signal" : "code" ).arg( code ) );
	childRunning = false;
	proc = nullptr;
	pollTimer.stop();
	killTimer.stop();
	closeRouterPort();
	stopListCheck();
	currentMap.clear();
	players.clear();
	const bool wasReady = ready;
	if ( srv )
		lc_srv_exited( srv, signaled, code );
	if ( stopping ) {
		state = LC_SRV_STOPPED;
		conLog( "The server stopped.", 4 );
		showAll();
		QCoreApplication::exit( 0 );
		return;
	}
	if ( signaled )
		conLog( QString( "The server crashed (signal %1)." ).arg( code ), 2 );
	else if ( code == 1 && !wasReady )
		conLog( "The server found no Elite Force game files (exit code 1).", 2 );
	else if ( code == 3 && !wasReady )
		conLog( "The server couldn't use the game folder (exit code 3).", 2 );
	else
		conLog( QString( "The server quit (exit code %1)." ).arg( code ), 2 );
	state = LC_SRV_FAILED;
	stateDetail = wasReady ? "the server quit unexpectedly" : code == 1 ? "no game files" : code == 3 ? "the game folder can't be used"
		: "the server didn't start, see the console below";
	showAll();
}

void ServerWindow::Impl::stopThen()
{
	if ( !childRunning || !proc ) {
		QCoreApplication::exit( 0 );
		return;
	}
	if ( stopping )
		return;
	stopping = true;
	state = LC_SRV_STOPPING;
	stateDetail.clear();
	showAll();
	sendCmd( "quit" );
	QTimer::singleShot( 5000, q, [this]() {
		if ( !childRunning || !proc )
			return;
		log( "Server: no exit after quit, sending SIGTERM" );
		const int pid = lc_proc_pid( proc );
		lc_proc_terminate( proc );
		QTimer::singleShot( 3000, q, [this, pid]() {
			if ( childRunning && proc && lc_proc_pid( proc ) == pid ) {
				log( "Server: still running, sending SIGKILL" );
				lc_proc_kill( proc );
			}
		} );
	} );
}

void ServerWindow::requestStop() { d->stopThen(); }

void ServerWindow::closeEvent( QCloseEvent *e )
{
	e->ignore();
	if ( d->state == LC_SRV_STOPPED || d->state == LC_SRV_FAILED || !d->childRunning )
		QCoreApplication::exit( 0 );
	else
		d->stopThen();
}

/* ---- controls --------------------------------------------------------------------------------------------------------------------- */

void ServerWindow::Impl::doKick( int row )
{
	if ( !childRunning || state != LC_SRV_RUNNING || row < 0 || row >= players.size() )
		return;
	const lc_srv_player &p = players[row];
	if ( !p.bot ) {
		if ( alert( q, Warning, QString( "Kick %1?" ).arg( U( p.name ) ), "They're disconnected from the server, but can join again.",
			QStringList() << "Kick" << "Cancel", 1, 1, 0 ) != 0 )
			return;
	}
	sendCmd( QString( "clientkick %1" ).arg( U( p.num ) ) );
}

void ServerWindow::Impl::doMessage()
{
	QDialog dlg( q );
	dlg.setWindowTitle( "Message" );
	dlg.setModal( true );
	QVBoxLayout *v = new QVBoxLayout( &dlg );
	v->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 16 ) );
	v->setSpacing( px( 8 ) );
	v->addWidget( label( "Message everyone on the server", 13, QFont::DemiBold ) );
	v->addWidget( label( "Shown in the game's chat area as coming from the server.", 12, QFont::Normal, QColor(), 300 ) );
	QLineEdit *e = new QLineEdit;
	e->setPlaceholderText( "Message" );
	e->setMinimumWidth( px( 300 ) );
	v->addWidget( e );
	QHBoxLayout *h = new QHBoxLayout;
	h->addStretch( 1 );
	QPushButton *send = new QPushButton( "Send" );
	QPushButton *cancel = new QPushButton( "Cancel" );
	send->setDefault( true );
	h->addWidget( send );
	h->addWidget( cancel );
	v->addLayout( h );
	QObject::connect( send, &QPushButton::clicked, &dlg, &QDialog::accept );
	QObject::connect( cancel, &QPushButton::clicked, &dlg, &QDialog::reject );
	e->setFocus();
	if ( dlg.exec() != QDialog::Accepted )
		return;
	char buf[512] = "";
	lc_srv_say_command( B( e->text() ).constData(), buf, sizeof buf );
	if ( buf[0] )
		sendCmd( U( buf ) );
}

void ServerWindow::Impl::rebuildMapMenu()
{
	mapMenu->clear();
	QSet<QString> inRot;
	if ( s.rotationCount > 0 ) {
		QAction *h = mapMenu->addAction( "Rotation" );
		h->setEnabled( false );
		for ( int i = 0; i < s.rotationCount; ++i ) {
			const QString n = U( s.rotation[i] );
			inRot.insert( n );
			QAction *a = mapMenu->addAction( "    " + mapTitle( n, true ) );
			a->setCheckable( true );
			a->setChecked( n == currentMap );
			QObject::connect( a, &QAction::triggered, q, [this, i]() { sendCmd( QString( "vstr vsp_map%1" ).arg( i ) ); } );
		}
	}
	if ( !catalog )
		return;
	QList<QPair<QString, const lc_map_info *>> others;
	for ( int i = 0; i < lc_map_catalog_count( catalog ); ++i ) {
		const lc_map_info *m = lc_map_catalog_at( catalog, i );
		if ( inRot.contains( U( m->name ) ) || !lc_map_supports_gametype( m, s.gametype ) )
			continue;
		others << qMakePair( U( lc_map_display_name( m ) ), m );
	}
	std::sort( others.begin(), others.end(), []( const QPair<QString, const lc_map_info *> &a, const QPair<QString, const lc_map_info *> &b ) {
		return a.first.localeAwareCompare( b.first ) < 0;
	} );
	if ( others.isEmpty() )
		return;
	if ( s.rotationCount > 0 )
		mapMenu->addSeparator();
	QAction *h = mapMenu->addAction( "Other maps" );
	h->setEnabled( false );
	for ( const auto &o : others ) {
		const QString n = U( o.second->name );
		QString t = "    " + mapTitle( n, true );
		if ( o.second->expansion )
			t += QString::fromUtf8( " \xc2\xb7 Expansion Pack" );
		QAction *a = mapMenu->addAction( t );
		a->setCheckable( true );
		a->setChecked( n == currentMap );
		QObject::connect( a, &QAction::triggered, q, [this, n]() { sendCmd( "map " + n ); } );
	}
}

void ServerWindow::Impl::doPlay()
{
	QStringList args = clientArgs;
	if ( s.password[0] )
		args << "+set" << "password" << U( s.password );
	args << "+connect" << QString( "127.0.0.1:%1" ).arg( port );
	char efsp[LC_MAX_PATH] = "";
	if ( !lc_sibling_exe( "efsp", efsp, sizeof efsp ) ) {
		conLog( "Couldn't start the game: the game's program (efsp) wasn't found.", 2 );
		return;
	}
	log( QString( "Server: Play on This %1: %2" ).arg( noun(), args.join( ' ' ) ) );
	conLog( "Starting Elite Force to join this server\xe2\x80\xa6", 4 );
	QList<QByteArray> avs;
	avs << QByteArray( efsp );
	for ( const QString &a : args )
		avs << B( a );
	QVector<const char *> argv;
	for ( const QByteArray &a : avs )
		argv << a.constData();
	argv << nullptr;
	const char *env[] = { "EF_FROM_LAUNCHER=1", nullptr };
	char err[256] = "";
	const long pid = lc_spawn_detached( argv.constData(), env, "VOYAGERSP_TEST_", err, sizeof err );
	if ( pid < 0 )
		conLog( QString( "Couldn't start the game: %1" ).arg( U( err ) ), 2 );
	else
		log( QString( "Server: Play on This %1 started pid %2" ).arg( noun() ).arg( pid ) );
}

/* ---- the router port ------------------------------------------------------------------------------------------------------------------ */

void ServerWindow::Impl::openRouterPort()
{
	if ( natState == NatOpening )
		return;
	natPublic.clear();
	natWhy.clear();
	natState = NatOpening;
	log( QString( "Server: asking the router to forward UDP %1" ).arg( port ) );
	nat = lc_nat_map_udp( port, []( lc_nat_result r, const char *ip, int ext, int err, void *u ) {
		Impl *i = static_cast<Impl *>( u );
		const QString ips = U( ip );
		QMetaObject::invokeMethod( i->q, [i, r, ips, ext, err]() { i->natResult( int( r ), ips, ext, err ); }, Qt::QueuedConnection );
	}, this );
	natTimer.start( 15000 );
	showInternet();
}

void ServerWindow::Impl::natFailedWith( const QString &why )
{
	natTimer.stop();
	natState = NatFailed;
	natWhy = why;
	natPublic.clear();
	log( "Server: port not opened: " + why );
	conLog( "Router: " + why, 1 );
	showInternet();
	showListed();
}

void ServerWindow::Impl::natResult( int r, const QString &ip, int extPort, int err )
{
	if ( natState != NatOpening )
		return;
	switch ( r ) {
	case LC_NAT_UNSUPPORTED:
		natFailedWith( "Your router doesn't support automatic port opening (NAT-PMP / UPnP), or has it turned off." );
		return;
	case LC_NAT_DISABLED:
		natFailedWith( "Automatic port opening (NAT-PMP / UPnP) is turned off in your router's settings." );
		return;
	case LC_NAT_DOUBLE:
		natFailedWith( "Your network has two routers in a row (double NAT), so the outer one still blocks the port." );
		return;
	case LC_NAT_TIMEOUT:
		natFailedWith( "Your router didn't answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." );
		return;
	case LC_NAT_OTHER:
		natFailedWith( QString( "The router didn't open the port (error %1)." ).arg( err ) );
		return;
	case LC_NAT_PRIVATE_EXTERNAL:
		natFailedWith( QString( "Your router opened the port, but its own internet address (%1) is private: your provider or another router puts it behind a "
			"second NAT, which nobody outside can get through." ).arg( ip ) );
		return;
	case LC_NAT_OK: {
		if ( ip.isEmpty() || ip == "0.0.0.0" )
			return;
		struct in_addr a;
		if ( inet_pton( AF_INET, B( ip ).constData(), &a ) == 1 && lc_ipv4_private( ntohl( a.s_addr ) ) ) {
			natFailedWith( QString( "Your router opened the port, but its own internet address (%1) is private: your provider or another router puts it behind a "
				"second NAT, which nobody outside can get through." ).arg( ip ) );
			return;
		}
		natTimer.stop();
		natState = NatMapped;
		natPublic = ip;
		natPublicPort = extPort ? extPort : port;
		log( QString( "Server: the router forwards %1:%2 to this %3" ).arg( ip ).arg( natPublicPort ).arg( noun() ) );
		conLog( QString( "Router: port opened, players on the internet connect to %1:%2" ).arg( ip ).arg( natPublicPort ), 3 );
		showInternet();
		showListed();
		return;
	}
	default:
		return;
	}
}

void ServerWindow::Impl::closeRouterPort()
{
	natTimer.stop();
	if ( nat ) {
		lc_nat_release( nat );
		nat = nullptr;
		log( "Server: router port mapping removed" );
	}
	natPublic.clear();
	natState = NatIdle;
	natWhy.clear();
	showInternet();
}

/* ---- the internet listing check ------------------------------------------------------------------------------------------------------------------ */

void ServerWindow::Impl::startListCheck()
{
	stopListCheck();
	checkCount = 0;
	checking = true;
	checkedOnce = false;
	checkDoneNothing = false;
	checkAllDown = false;
	listState.clear();
	everListed.clear();
	if ( !lookup ) {
		lookup = lc_nat_public_address( []( lc_nat_result r, const char *ip, int, void *u ) {
			Impl *i = static_cast<Impl *>( u );
			if ( r == LC_NAT_OK && ip && *ip ) {
				QMutexLocker l( &i->lookupLock );
				i->lookupAddress = U( ip );
			}
		}, this );
	}
	listTimer.start( firstDelay );
	showListed();
}

void ServerWindow::Impl::stopListCheck()
{
	listTimer.stop();
	checking = false;
	if ( lookup ) {
		lc_nat_release( lookup );
		lookup = nullptr;
	}
	showListed();
}

void ServerWindow::Impl::checkList()
{
	if ( !childRunning || !checking )
		return;
	QString publicIP;
	if ( natState == NatMapped )
		publicIP = natPublic;
	else {
		QMutexLocker l( &lookupLock );
		publicIP = lookupAddress;
	}
	const QString want = publicIP.isEmpty() ? QString() : QString( "%1:%2" ).arg( publicIP ).arg( natState == NatMapped ? natPublicPort : port );
	++checkCount;
	char masters[LC_MAX_MASTERS][65];
	const int n = lc_masters_load( lc_store_shared(), masters );
	QStringList all;
	for ( int i = 0; i < n; ++i )
		all << U( masters[i] );
	listMasters = all;
	showListed();
	auto *res = new lc_list_result;
	memset( res, 0, sizeof *res );
	auto *mcopy = new QVector<char>( size_t( LC_MAX_MASTERS ) * 65 );
	memcpy( mcopy->data(), masters, mcopy->size() );
	const QByteArray wantB = B( want );
	async( q, [res, mcopy, n, wantB]() {
		lc_list_check( reinterpret_cast<const char ( * )[65]>( mcopy->constData() ), n, wantB.isEmpty() ? nullptr : wantB.constData(), res );
	}, [this, res, mcopy, n, all]() {
		QVector<int> st( n, 0 );
		for ( int i = 0; i < n; ++i )
			st[i] = res->down[i] ? 3 : res->listed[i] ? 1 : res->missing[i] ? 2 : 2;
		delete res;
		delete mcopy;
		if ( all == listMasters )
			listResult( st );
	} );
}

void ServerWindow::Impl::listResult( const QVector<int> &st )
{
	if ( !childRunning || !checking )
		return;
	listState = st;
	checkedOnce = true;
	QStringList listed, notListed, down;
	for ( int i = 0; i < st.size(); ++i ) {
		const QString lbl = masterLabel( listMasters[i], listMasters );
		const QString full = lbl == listMasters[i] ? lbl : QString( "%1 (%2)" ).arg( lbl, listMasters[i] );
		( st[i] == 1 ? listed : st[i] == 3 ? down : notListed ) << full;
	}
	log( QString( "Server: internet list check %1: listed on [%2], not on [%3], no answer from [%4]" ).arg( checkCount )
		.arg( listed.join( ", " ), notListed.join( ", " ), down.join( ", " ) ) );
	QStringList newly;
	for ( int i = 0; i < st.size(); ++i )
		if ( st[i] == 1 && !everListed.contains( i ) ) {
			everListed.insert( i );
			newly << masterLabel( listMasters[i], listMasters );
		}
	if ( !newly.isEmpty() )
		conLog( QString( "Internet: listed on %1, so players anywhere can reach it." ).arg( newly.join( ", " ) ), 3 );
	const bool found = !listed.isEmpty();
	const bool allDown = !found && notListed.isEmpty();
	const bool noneMissing = notListed.isEmpty();
	if ( ( found && noneMissing ) || checkCount >= maxChecks ) {
		checking = false;
		if ( !found ) {
			checkDoneNothing = true;
			checkAllDown = allDown;
			conLog( allDown ? "Internet: no master server answered, so the listing couldn't be checked."
				: "Internet: not on the server list. The master server couldn't reach this server from the internet.", 1 );
		} else if ( !notListed.isEmpty() ) {
			QStringList labels;
			for ( int i = 0; i < st.size(); ++i )
				if ( st[i] == 2 )
					labels << masterLabel( listMasters[i], listMasters );
			conLog( QString( "Internet: not listed on %1 (yet)." ).arg( labels.join( ", " ) ), 4 );
		}
		if ( lookup ) {
			lc_nat_release( lookup );
			lookup = nullptr;
		}
	} else
		listTimer.start( repeatDelay );
	showListed();
}

/* ---- test hooks -------------------------------------------------------------------------------------------------------------------------------------- */

void ServerWindow::runTestHooks()
{
	const QString snap = testEnv( "VOYAGERSP_TEST_SERVER_SNAPSHOT" );
	if ( !snap.isEmpty() ) {
		const QString after = testEnv( "VOYAGERSP_TEST_SERVER_SNAPSHOT_AFTER" );
		QTimer::singleShot( int( ( after.isEmpty() ? 15.0 : after.toDouble() ) * 1000 ), this, [this, snap]() { savePng( this, snap ); } );
	}
	const QString playAfter = testEnv( "VOYAGERSP_TEST_SERVER_PLAY_AFTER" );
	if ( !playAfter.isEmpty() )
		QTimer::singleShot( int( playAfter.toDouble() * 1000 ), this, [this]() { d->doPlay(); } );
	const QString stopAfter = testEnv( "VOYAGERSP_TEST_SERVER_STOP_AFTER" );
	if ( !stopAfter.isEmpty() )
		QTimer::singleShot( int( stopAfter.toDouble() * 1000 ), this, [this]() { d->stopThen(); } );
	if ( testSet( "VOYAGERSP_TEST_SERVER_START" ) && QGuiApplication::platformName() != "offscreen" )
		move( -20000, -20000 );
}
