/*
===========================================================================
ef-launcher (Qt 6): the main launcher window. See launcher_window.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "launcher_window.h"
#include "dialogs.h"
#include "instance.h"

#include <cmath>

extern int g_argc;
extern char **g_argv;

using namespace Ui;

static const char *kMadeBy = "Made by Marino Hardin";

/* ---- the update badge ---------------------------------------------------------------------- */

class UpdateBadge : public QFrame {
public:
	UpdateBadge() { setFrameShape( QFrame::NoFrame ); }
protected:
	void paintEvent( QPaintEvent *e ) override
	{
		QPainter p( this );
		p.setRenderHint( QPainter::Antialiasing );
		QColor f = accent();
		f.setAlphaF( 0.13 );
		p.setPen( Qt::NoPen );
		p.setBrush( f );
		p.drawRoundedRect( QRectF( rect() ), 9, 9 );
		QFrame::paintEvent( e );
	}
};

/* ---- helpers ------------------------------------------------------------------------------------ */

static QWidget *gridLabelCell( const QString &text, bool enabled = true )
{
	QLabel *l = label( text, 13, QFont::Normal, enabled ? QColor() : tertiary() );
	l->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
	l->setFixedWidth( px( 112 ) );
	return l;
}

static QString elide( QLabel *l, const QString &t ) { l->setText( t ); return t; }

/* ======================================================================================================== */

LauncherWindow::LauncherWindow( bool hostOnly ) : QDialog( nullptr, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
	Qt::WindowSystemMenuHint )
{
	m_hostOnly = hostOnly;
	setWindowTitle( "Elite Force" );
	setModal( false );
	m_server = new lc_server_settings;
	memset( m_server, 0, sizeof *m_server );
	memset( &m_gfx, 0, sizeof m_gfx );
	memset( &m_cmod, 0, sizeof m_cmod );
	memset( &m_data, 0, sizeof m_data );
	memset( m_missingNames, 0, sizeof m_missingNames );

	lc_store *st = lc_store_shared();
	m_homeBase = U( lc_homebase() );
	m_mode = lc_store_get_int( st, LC_KEY_MODE, 0 );
	if ( m_mode < 0 || m_mode > 2 )
		m_mode = 0;
	const QString tm = testEnv( "VOYAGERSP_TEST_LAUNCHER_MODE" ).toLower();
	if ( !tm.isEmpty() )
		m_mode = tm == "hm" ? 1 : tm == "host" ? 2 : 0;

	/* LoadGraphics, once, with the mode remembered now (LDC §2.5) */
	const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry()
		: QRect( 0, 0, 1920, 1080 );
	lc_gfx_load( B( m_homeBase ).constData(), m_mode != LC_MODE_SP, avail.width(), avail.height(), &m_gfx, &m_fromConfig );

	/* cMod values, player name */
	char path[LC_MAX_PATH];
	lc_config_path( B( m_homeBase ).constData(), 1, path, sizeof path );
	lc_cfg hm;
	memset( &hm, 0, sizeof hm );
	lc_cfg_read( path, &hm );
	lc_cmod_load( &hm, st, &m_cmod );
	m_playerName = QString();
	{
		char nm[128];
		lc_player_name_from_config( &hm, lc_user_full_name(), nm, sizeof nm );
		m_playerName = U( nm );
	}
	lc_cfg_free( &hm );

	/* server settings, masters test hook, port override */
	lc_server_settings_load( m_server, st, lc_user_full_name() );
	const QString tmasters = testEnv( "VOYAGERSP_TEST_MASTERS" );
	if ( !tmasters.isEmpty() ) {
		const QStringList l = tmasters.split( ',', Qt::SkipEmptyParts );
		QList<QByteArray> bs;
		for ( const QString &s : l )
			bs << B( s.trimmed() );
		const char *ptrs[LC_MAX_MASTERS];
		int n = 0;
		for ( int i = 0; i < bs.size() && n < LC_MAX_MASTERS; ++i )
			ptrs[n++] = bs[i].constData();
		char clean[LC_MAX_MASTERS][65];
		const int c = lc_clean_masters( ptrs, n, clean );
		lc_masters_save( st, clean, c );
		lc_store_save( st );
	}
	const QString tport = testEnv( "VOYAGERSP_TEST_SERVER_PORT" );
	if ( !tport.isEmpty() )
		m_server->port = tport.toInt();

	build();

	/* public address lookup (LDC §2.8): ready by the time Play is pressed */
	lc_public_address_start();

	refreshData();
	refreshServerSummary();
	refreshMasters();
	selectMode( m_mode );
	showGraphics();
	if ( !m_hostOnly )
		startUpdateCheck();
}

LauncherWindow::~LauncherWindow()
{
	delete m_server;
}

LauncherWindow::HostInfo LauncherWindow::hostInfo()
{
	HostInfo h;
	h.settings = m_server;
	h.basepath = U( m_data.basepath );
	h.dataFolder = m_dataFolder;
	h.clientArgs = clientArgs();
	return h;
}

/* ---- building the window ---------------------------------------------------------------------------- */

void LauncherWindow::build()
{
	QVBoxLayout *outer = new QVBoxLayout( this );
	outer->setContentsMargins( 0, 0, 0, 0 );
	m_scroll = new QScrollArea;
	m_scroll->setFrameShape( QFrame::NoFrame );
	m_scroll->setWidgetResizable( true );
	m_scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	m_scroll->setAlignment( Qt::AlignHCenter | Qt::AlignTop );
	outer->addWidget( m_scroll );

	QWidget *content = new QWidget;
	content->setFixedWidth( px( 620 ) );
	QVBoxLayout *root = new QVBoxLayout( content );
	root->setContentsMargins( px( 28 ), px( 30 ), px( 28 ), px( 20 ) );
	root->setSpacing( px( 16 ) );
	root->addWidget( buildHeader() );
	root->addSpacing( px( 4 ) );
	root->addWidget( buildCards() );

	m_midStack = new QStackedWidget;
	m_midStack->addWidget( buildGraphics() );
	m_midStack->addWidget( buildServerGroup() );
	root->addWidget( m_midStack );
	root->addWidget( buildHolomatch() );
	root->addWidget( buildGameFiles() );
	root->addSpacing( px( 4 ) );
	root->addWidget( buildFooter() );
	m_scroll->setWidget( content );
	m_content = content;

	/* tab order: cards, mode accessories, groups, footer */
	setTabOrder( m_playButton, m_quitButton );
}

QWidget *LauncherWindow::buildHeader()
{
	QWidget *w = new QWidget;
	QHBoxLayout *h = new QHBoxLayout( w );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( px( 14 ) );

	QLabel *ic = new QLabel;
	ic->setFixedSize( px( 56 ), px( 56 ) );
	QPixmap pm( ":/icon.png" );
	if ( !pm.isNull() )
		ic->setPixmap( pm.scaled( QSize( px( 56 ), px( 56 ) ) * devicePixelRatioF(), Qt::KeepAspectRatio,
			Qt::SmoothTransformation ) );
	if ( !pm.isNull() )
		ic->pixmap().setDevicePixelRatio( devicePixelRatioF() );
	h->addWidget( ic );
	if ( !pm.isNull() )
		setWindowIcon( QIcon( pm ) );

	QVBoxLayout *col = new QVBoxLayout;
	col->setSpacing( px( 2 ) );
	col->addWidget( label( "Star Trek: Voyager", 12, QFont::DemiBold, secondary() ) );
	col->addWidget( label( "Elite Force", 24, QFont::Bold ) );
	col->addWidget( label( "Choose how you want to play.", 13, QFont::Normal, secondary() ) );
	h->addLayout( col );
	h->addStretch( 1 );

	/* update badge (hidden until a newer release is known) */
	UpdateBadge *b = new UpdateBadge;
	QHBoxLayout *bh = new QHBoxLayout( b );
	bh->setContentsMargins( px( 10 ), px( 7 ), px( 10 ), px( 7 ) );
	bh->setSpacing( px( 8 ) );
	QLabel *bi = new QLabel;
	bi->setPixmap( iconPixmap( "arrow.down.circle.fill", accent(), 20 ) );
	bh->addWidget( bi );
	QVBoxLayout *bc = new QVBoxLayout;
	bc->setSpacing( px( 1 ) );
	m_badgeTitle = label( "Update available", 12, QFont::DemiBold );
	m_badgeText = label( "", 11, QFont::Normal, secondary() );
	bc->addWidget( m_badgeTitle );
	bc->addWidget( m_badgeText );
	bh->addLayout( bc );
	QPushButton *dl = button( QString::fromUtf8( "Download\xe2\x80\xa6" ), "Open the release page on GitHub" );
	connect( dl, &QPushButton::clicked, this, [this]() {
		if ( !m_updateUrl.isEmpty() )
			lc_open_url( B( m_updateUrl ).constData() );
	} );
	bh->addWidget( dl );
	b->hide();
	m_badge = b;
	h->addWidget( b, 0, Qt::AlignVCenter );

	/* the application menu (About, Check for Updates…, Quit) */
	QToolButton *gear = iconButton( "gear", "Menu", 14 );
	gear->setPopupMode( QToolButton::InstantPopup );
	QMenu *menu = new QMenu( gear );
	QAction *about = menu->addAction( "About Elite Force" );
	connect( about, &QAction::triggered, this, &LauncherWindow::showAbout );
	QAction *upd = menu->addAction( QString::fromUtf8( "Check for Updates\xe2\x80\xa6" ) );
	connect( upd, &QAction::triggered, this, &LauncherWindow::checkForUpdatesNow );
	m_autoUpdateAction = menu->addAction( "Check for Updates Automatically" );
	m_autoUpdateAction->setCheckable( true );
	m_autoUpdateAction->setChecked( lc_store_get_bool( lc_store_shared(), LC_KEY_CHECK_UPDATES, 1 ) );
	connect( m_autoUpdateAction, &QAction::toggled, this, [this]( bool on ) {
		lc_store *st = lc_store_shared();
		lc_store_set( st, LC_KEY_CHECK_UPDATES, on ? "1" : "0" );
		lc_store_save( st );
		log( QString( "Updates: automatic check %1" ).arg( on ? "on" : "off" ) );
		if ( on )
			startUpdateCheck();
		else
			m_badge->hide();
	} );
	menu->addSeparator();
	QAction *q = menu->addAction( "Quit" );
	q->setShortcut( QKeySequence( "Ctrl+Q" ) );
	connect( q, &QAction::triggered, this, &LauncherWindow::quit );
	gear->setMenu( menu );
	m_menu = menu;
	h->addWidget( gear, 0, Qt::AlignTop );
	return w;
}

QWidget *LauncherWindow::buildCards()
{
	const bool coop = lc_coop_ui_enabled() != 0;
	if ( coop ) {
		m_coopButton = button( QString::fromUtf8( "Co-op\xe2\x80\xa6" ),
			"Play the campaign with a friend: host a co-op game, or check for one to join (experimental)" );
		connect( m_coopButton, &QPushButton::clicked, this, [this]() { openCoop(); } );
	}
	m_browseButton = button( QString::fromUtf8( "Browse\xe2\x80\xa6" ),
		"See the games on your network and the internet, and join one" );
	connect( m_browseButton, &QPushButton::clicked, this, [this]() { selectMode( 1 ); openBrowser(); } );
	m_configButton = button( QString::fromUtf8( "Config\xe2\x80\xa6" ),
		"Maps, game type, bots and the server's name, password and network" );
	connect( m_configButton, &QPushButton::clicked, this, [this]() { selectMode( 2 ); openConfig(); } );

	QWidget *row = new QWidget;
	QHBoxLayout *h = new QHBoxLayout( row );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( px( 10 ) );
	struct { const char *icon, *title, *detail; QWidget *acc; } defs[3] = {
		{ "person.fill", "Single Player", "The Voyager campaign, your saves and the Virtual Voyager.", m_coopButton },
		{ "person.3.fill", "Holomatch", "Join games on your network or the internet, or play with bots.", m_browseButton },
		{ "server.rack", "Host a Server", nullptr, m_configButton },
	};
	const QString hostDetail = "Run a Holomatch server on this " + nounLc() + " for friends.";
	for ( int i = 0; i < 3; ++i ) {
		ModeCard *c = new ModeCard( defs[i].icon, defs[i].title, defs[i].detail ? U( defs[i].detail ) : hostDetail,
			defs[i].acc );
		c->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
		connect( c, &ModeCard::picked, this, [this, i]() { selectMode( i ); } );
		connect( c, &ModeCard::playRequested, this, [this]() { play(); } );
		connect( c, &ModeCard::step, this, [this]( int d ) {
			const int n = ( m_mode + d + 3 ) % 3;
			selectMode( n );
			m_cards[n]->setFocus();
		} );
		h->addWidget( c, 1 );
		m_cards << c;
	}
	return row;
}

QWidget *LauncherWindow::buildGraphics()
{
	Group *g = new Group( "Graphics" );
	QGridLayout *gl = new QGridLayout;
	gl->setHorizontalSpacing( px( 10 ) );
	gl->setVerticalSpacing( px( 7 ) );
	gl->setColumnStretch( 1, 1 );
	int r = 0;
	auto add = [&]( const QString &lab, QWidget *w ) {
		if ( !lab.isEmpty() )
			gl->addWidget( gridLabelCell( lab ), r, 0 );
		gl->addWidget( w, r, 1, Qt::AlignLeft );
		++r;
	};

	m_quality = new Segmented( QStringList() << lc_preset_title( LC_PRESET_PERFORMANCE ) << lc_preset_title( LC_PRESET_BALANCED )
		<< lc_preset_title( LC_PRESET_BEST ) << "Custom" );
	m_quality->setSegmentEnabled( 3, false );
	m_quality->setFixedWidth( px( 380 ) );
	connect( m_quality, &Segmented::currentChanged, this, [this]( int i ) {
		if ( i < 3 )
			applyPreset( lc_preset( i ) );
		else
			showGraphics();
	} );
	add( "Quality:", m_quality );

	m_display = new QComboBox;
	m_display->addItem( "Window", 0 );
	m_display->addItem( "Full Screen", 1 );
	add( "Display:", m_display );
	m_winSize = new QComboBox;
	m_winSize->setMinimumWidth( px( 140 ) );
	add( "Window size:", m_winSize );
	m_highdpi = new QCheckBox( "Full resolution (high DPI)" );
	add( QString(), m_highdpi );
	m_msaa = new QComboBox;
	m_msaa->addItem( "Off", 0 );
	m_msaa->addItem( QString::fromUtf8( "2\xc3\x97" ), 2 );
	m_msaa->addItem( QString::fromUtf8( "4\xc3\x97" ), 4 );
	m_msaa->addItem( QString::fromUtf8( "8\xc3\x97" ), 8 );
	add( "Anti-aliasing:", m_msaa );
	m_picmip = new QComboBox;
	m_picmip->addItem( "High", 0 );
	m_picmip->addItem( "Medium", 1 );
	m_picmip->addItem( "Low", 2 );
	add( "Texture detail:", m_picmip );
	m_vsync = new QCheckBox( "Sync to the display (V-Sync)" );
	add( QString(), m_vsync );
	m_brightness = new QComboBox;
	for ( int i = 0; i < lc_brightness_count(); ++i )
		m_brightness->addItem( U( lc_brightness_title( i ) ), i );
	m_brightness->addItem( "Custom", LC_BRIGHTNESS_CUSTOM );
	if ( QStandardItemModel *m = qobject_cast<QStandardItemModel *>( m_brightness->model() ) )
		m->item( m_brightness->count() - 1 )->setEnabled( false );
	add( "Brightness:", m_brightness );
	m_brightHint = label( QString(), 11, QFont::Normal, secondary(), 400 );
	gl->addWidget( m_brightHint, r++, 1 );
	g->content()->addLayout( gl );

	for ( QComboBox *c : { m_display, m_winSize, m_msaa, m_picmip, m_brightness } )
		connect( c, QOverload<int>::of( &QComboBox::activated ), this, [this]() { graphicsChanged(); } );
	for ( QCheckBox *c : { m_highdpi, m_vsync } )
		connect( c, &QCheckBox::clicked, this, [this]() { graphicsChanged(); } );
	return g;
}

QWidget *LauncherWindow::buildServerGroup()
{
	Group *g = new Group( "Server" );
	QGridLayout *gl = new QGridLayout;
	gl->setHorizontalSpacing( px( 10 ) );
	gl->setVerticalSpacing( px( 7 ) );
	gl->setColumnStretch( 1, 1 );
	int r = 0;
	auto row = [&]( const QString &lab, QLabel *&val, bool mono = false ) {
		gl->addWidget( gridLabelCell( lab ), r, 0 );
		val = elidedLabel( QString(), 13, mono ? QFont::Medium : QFont::Normal );
			gl->addWidget( val, r, 1 );
		++r;
	};
	row( "Server name:", m_srvName );
	row( "Game:", m_srvGame );
	row( "Maps:", m_srvMaps );
	row( "Players:", m_srvPlayers );
	row( "Network:", m_srvNetwork );
	row( "Address:", m_srvAddress, true );
	QLabel *tip = label( "Friends connect to this address, or find the server in Holomatch's Join menu. It runs in its own window, "
		"where Play on This " + noun() + " joins it with your Holomatch graphics settings.", 11, QFont::Normal, secondary(), 400 );
	gl->addWidget( tip, r++, 1 );
	gl->setRowStretch( r, 1 );
	g->content()->addLayout( gl );
	g->content()->addStretch( 1 );
	return g;
}

QWidget *LauncherWindow::buildHolomatch()
{
	Group *g = new Group( "Holomatch" );
	QGridLayout *gl = new QGridLayout;
	gl->setHorizontalSpacing( px( 10 ) );
	gl->setVerticalSpacing( px( 4 ) );
	gl->setColumnStretch( 1, 1 );

	m_nameLabel = label( "Player name:", 13 );
	m_nameLabel->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
	m_nameLabel->setFixedWidth( px( 112 ) );
	gl->addWidget( m_nameLabel, 0, 0 );
	m_name = new QLineEdit( m_playerName );
	m_name->setPlaceholderText( "UnnamedPlayer" );
	m_name->setFixedWidth( px( 220 ) );
	gl->addWidget( m_name, 0, 1, Qt::AlignLeft );
	m_nameHint = label( QString(), 11, QFont::Normal, secondary(), 400 );
	gl->addWidget( m_nameHint, 1, 1 );

	gl->addWidget( gridLabelCell( "Internet games:" ), 2, 0 );
	QHBoxLayout *mh = new QHBoxLayout;
	m_masterSummary = elidedLabel( QString(), 13 );
	mh->addWidget( m_masterSummary, 1 );
	m_mastersButton = button( QString::fromUtf8( "Servers\xe2\x80\xa6" ),
		"The master servers that list internet games, and that a server you host announces itself to" );
	connect( m_mastersButton, &QPushButton::clicked, this, [this]() { openMasters(); } );
	mh->addWidget( m_mastersButton );
	gl->addLayout( mh, 2, 1 );
	gl->setRowMinimumHeight( 2, px( 30 ) );

	m_loopback = new QCheckBox( "Loopback Host Fix" );
	m_loopback->setToolTip( "Most routers don't route this " + nounLc() + " back to its own public address, so a server hosted here wouldn't "
		"answer in your own internet lists. This lists it as this " + nounLc() + " (127.0.0.1) instead." );
	m_loopback->setChecked( lc_store_get_bool( lc_store_shared(), LC_KEY_LOOPBACK_FIX, 1 ) != 0 );
	connect( m_loopback, &QCheckBox::toggled, this, [this]( bool on ) {
		lc_store *st = lc_store_shared();
		if ( on )
			lc_store_remove( st, LC_KEY_LOOPBACK_FIX );
		else
			lc_store_set( st, LC_KEY_LOOPBACK_FIX, "0" );
		lc_store_save( st );
	} );
	gl->addWidget( m_loopback, 3, 1 );
	gl->addWidget( label( "Shows a server you host on this " + nounLc() + " in your own internet lists.", 11, QFont::Normal,
		secondary(), 400 ), 4, 1 );

	m_cmodLabel = label( "Game modules:", 13 );
	m_cmodLabel->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
	m_cmodLabel->setFixedWidth( px( 112 ) );
	gl->addWidget( m_cmodLabel, 5, 0 );
	QHBoxLayout *ch = new QHBoxLayout;
	m_cmodBox = new QCheckBox( "Use cMod's game modules, by Noah Metzger" );
	m_cmodBox->setToolTip( "cMod's cgame and ui in place of the original ones (not on pure servers, and never in place of a mod's own): "
		"a widescreen HUD and menus, weapon fire prediction and many fixes." );
	m_cmodBox->setChecked( m_cmod.on != 0 );
	connect( m_cmodBox, &QCheckBox::toggled, this, [this]( bool ) { refreshHudEnabled(); } );
	ch->addWidget( m_cmodBox );
	ch->addStretch( 1 );
	m_hudButton = button( QString::fromUtf8( "HUD\xe2\x80\xa6" ), "cMod's widescreen HUD and weapon fire prediction" );
	connect( m_hudButton, &QPushButton::clicked, this, [this]() { openHud(); } );
	ch->addWidget( m_hudButton );
	gl->addLayout( ch, 5, 1 );
	gl->setRowMinimumHeight( 5, px( 30 ) );
	gl->setVerticalSpacing( px( 4 ) );
	g->content()->addLayout( gl );
	return g;
}

QWidget *LauncherWindow::buildGameFiles()
{
	Group *g = new Group( "Game Files" );
	QHBoxLayout *top = new QHBoxLayout;
	top->setSpacing( px( 10 ) );
	m_folderIcon = new QLabel;
	m_folderIcon->setFixedSize( px( 36 ), px( 36 ) );
	top->addWidget( m_folderIcon, 0, Qt::AlignTop );
	QVBoxLayout *col = new QVBoxLayout;
	col->setSpacing( px( 3 ) );
	m_pathLabel = elidedLabel( QString(), 12, QFont::Medium, QColor(), Qt::ElideMiddle );
	col->addWidget( m_pathLabel );
	QHBoxLayout *sr = new QHBoxLayout;
	sr->setSpacing( px( 4 ) );
	m_statusIcon = new QLabel;
	m_statusIcon->setFixedSize( px( 14 ), px( 14 ) );
	m_statusText = elidedLabel( QString(), 11, QFont::Normal, secondary() );
	sr->addWidget( m_statusIcon );
	sr->addWidget( m_statusText, 1 );
	col->addLayout( sr );
	QHBoxLayout *er = new QHBoxLayout;
	er->setSpacing( px( 4 ) );
	m_expIcon = new QLabel;
	m_expIcon->setFixedSize( px( 14 ), px( 14 ) );
	m_expText = elidedLabel( QString(), 11, QFont::Normal, secondary() );
	er->addWidget( m_expIcon );
	er->addWidget( m_expText, 1 );
	col->addLayout( er );
	top->addLayout( col, 1 );
	g->content()->addLayout( top );

	QHBoxLayout *br = new QHBoxLayout;
	br->setSpacing( px( 8 ) );
	br->addStretch( 1 );
	m_showFolder = button( "Show in File Manager" );
	connect( m_showFolder, &QPushButton::clicked, this, [this]() {
		if ( !m_dataFolder.isEmpty() )
			lc_open_folder( B( m_dataFolder ).constData() );
	} );
	m_addFiles = button( QString::fromUtf8( "Add Game Files\xe2\x80\xa6" ),
		"Copy more .pk3 files into the game, such as the Expansion Pack's or extra maps" );
	connect( m_addFiles, &QPushButton::clicked, this, [this]() { openAddFiles(); } );
	m_change = button( QString::fromUtf8( "Choose\xe2\x80\xa6" ) );
	connect( m_change, &QPushButton::clicked, this, [this]() { openChooser(); } );
	br->addWidget( m_showFolder );
	br->addWidget( m_addFiles );
	br->addWidget( m_change );
	g->content()->addLayout( br );
	return g;
}

QWidget *LauncherWindow::buildFooter()
{
	QWidget *w = new QWidget;
	QHBoxLayout *h = new QHBoxLayout( w );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( px( 10 ) );
	const QString ver = U( EF_APP_VERSION );
	QLabel *v = label( ( ver.isEmpty() ? QString() : "Version " + ver + QString::fromUtf8( " \xc2\xb7 " ) ) + kMadeBy, 11,
		QFont::Normal, tertiary() );
	if ( deck() ) {
		QVBoxLayout *left = new QVBoxLayout;
		left->setSpacing( px( 4 ) );
		left->addWidget( v );
		m_autoBox = new QCheckBox( "Start the last mode automatically" );
		m_autoBox->setToolTip( "Skip this window and start the mode you played last. Run the app with --launcher to see it again." );
		m_autoBox->setChecked( lc_store_get_bool( lc_store_shared(), LC_KEY_AUTOLAUNCH, 0 ) != 0 );
		connect( m_autoBox, &QCheckBox::toggled, this, []( bool on ) {
			lc_store *st = lc_store_shared();
			lc_store_set( st, LC_KEY_AUTOLAUNCH, on ? "1" : "0" );
			lc_store_save( st );
		} );
		left->addWidget( m_autoBox );
		h->addLayout( left );
	} else
		h->addWidget( v );
	h->addStretch( 1 );
	m_quitButton = button( "Quit" );
	m_quitButton->setMinimumHeight( px( 30 ) );
	connect( m_quitButton, &QPushButton::clicked, this, &LauncherWindow::quit );
	m_playButton = button( "Play" );
	m_playButton->setMinimumWidth( px( 110 ) );
	m_playButton->setMinimumHeight( px( 30 ) );
	m_playButton->setDefault( true );
	m_playButton->setAutoDefault( true );
	connect( m_playButton, &QPushButton::clicked, this, [this]() { play(); } );
	h->addWidget( m_quitButton );
	h->addWidget( m_playButton );
	return w;
}

void LauncherWindow::showEvent( QShowEvent *e )
{
	QDialog::showEvent( e );
	if ( m_sized )
		return;
	m_sized = true;
	const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry()
		: QRect( 0, 0, 1280, 800 );
	if ( deck() ) {
		setMinimumSize( 0, 0 );
		resize( avail.size().boundedTo( QSize( 1280, 800 ) ) );
		return;
	}
	const QSize sh = m_content->sizeHint().expandedTo( m_content->minimumSizeHint() );
	const int maxH = testEnv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" ).isEmpty() ? avail.height() - 60 : 4000;
	const int h = qMin( sh.height(), maxH );
	const int sb = h < sh.height() ? style()->pixelMetric( QStyle::PM_ScrollBarExtent ) : 0;
	setFixedSize( sh.width() + sb, h );
	move( avail.center() - QPoint( width() / 2, height() / 2 ) );
}

/* ---- selecting the mode ------------------------------------------------------------------------------ */

void LauncherWindow::selectMode( int m )
{
	m_mode = m;
	for ( int i = 0; i < m_cards.size(); ++i )
		m_cards[i]->setSelected( i == m );
	refreshModeEnabling();
	m_midStack->setCurrentIndex( m == 2 ? 1 : 0 );
	if ( m == 2 )
		refreshServerSummary();
}

void LauncherWindow::refreshModeEnabling()
{
	const bool hm = m_mode != 0;
	m_name->setEnabled( hm );
	setColor( m_nameLabel, hm ? QColor() : tertiary() );
	const bool cmodUsable = hm && m_cmod.available;
	m_cmodBox->setEnabled( cmodUsable );
	setColor( m_cmodLabel, cmodUsable ? QColor() : tertiary() );
	refreshHudEnabled();
	m_nameHint->setText( m_mode == 1 ? "Shown to other players in Holomatch."
		: m_mode == 2 ? "Your name when you use Play on This " + noun() + "." : "Used when you play Holomatch." );
	m_playButton->setText( m_mode == 0 ? "Play" : m_mode == 1 ? "Play Holomatch" : "Start Server" );
	refreshPlayEnabled();
}

void LauncherWindow::refreshHudEnabled()
{
	m_hudButton->setEnabled( m_cmodBox->isEnabled() && m_cmodBox->isChecked() );
}

void LauncherWindow::refreshPlayEnabled()
{
	m_playButton->setEnabled( canPlay() );
	m_playButton->setToolTip( m_missingPatch ? QString( "Add the 1.2 patch files first (see Game Files)" ) : QString() );
}

/* ---- graphics -------------------------------------------------------------------------------------------- */

void LauncherWindow::rebuildSizeList()
{
	const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry()
		: QRect( 0, 0, 1920, 1080 );
	int sizes[24][2];
	const int n = lc_window_sizes( avail.width(), avail.height(), m_gfx.width, m_gfx.height, sizes, 24 );
	QSignalBlocker b( m_winSize );
	m_winSize->clear();
	for ( int i = 0; i < n; ++i )
		m_winSize->addItem( QString::fromUtf8( "%1 \xc3\x97 %2" ).arg( sizes[i][0] ).arg( sizes[i][1] ),
			sizes[i][0] * 10000 + sizes[i][1] );
}

void LauncherWindow::showGraphics()
{
	m_graphicsLoading = true;
	rebuildSizeList();
	const lc_preset p = lc_gfx_match_preset( &m_gfx );
	m_quality->setCurrent( int( p ) );
	m_display->setCurrentIndex( m_gfx.fullscreen ? 1 : 0 );
	int idx = m_winSize->findData( m_gfx.width * 10000 + m_gfx.height );
	if ( idx >= 0 )
		m_winSize->setCurrentIndex( idx );
	m_winSize->setEnabled( !m_gfx.fullscreen );
	m_highdpi->setChecked( m_gfx.highdpi != 0 );
	idx = m_msaa->findData( m_gfx.msaa );
	m_msaa->setCurrentIndex( idx < 0 ? 0 : idx );
	m_picmip->setCurrentIndex( qBound( 0, m_gfx.picmip, 2 ) );
	m_vsync->setChecked( m_gfx.vsync != 0 );
	idx = m_brightness->findData( m_gfx.brightness );
	m_brightness->setCurrentIndex( idx < 0 ? m_brightness->count() - 1 : idx );
	m_brightHint->setText( U( lc_brightness_help( m_gfx.brightness ) ) );
	m_graphicsLoading = false;
}

void LauncherWindow::graphicsChanged()
{
	if ( m_graphicsLoading )
		return;
	m_gfx.fullscreen = m_display->currentData().toInt();
	const int wh = m_winSize->currentData().toInt();
	if ( wh > 0 ) {
		m_gfx.width = wh / 10000;
		m_gfx.height = wh % 10000;
	}
	m_gfx.highdpi = m_highdpi->isChecked();
	m_gfx.msaa = m_msaa->currentData().toInt();
	m_gfx.picmip = m_picmip->currentData().toInt();
	m_gfx.vsync = m_vsync->isChecked();
	m_gfx.brightness = m_brightness->currentData().toInt();
	showGraphics();
}

void LauncherWindow::applyPreset( lc_preset p )
{
	lc_gfx_apply_preset( &m_gfx, p );
	showGraphics();
}

/* ---- the Server summary (Host mode) ----------------------------------------------------------------------- */

void LauncherWindow::refreshServerSummary()
{
	const lc_server_settings &s = *m_server;
	const QString dot = QString::fromUtf8( " \xc2\xb7 " );
	m_srvName->setText( U( s.hostname ) );
	QString game = U( lc_gametype_name( s.gametype ) );
	QStringList mods;
	for ( int i = 0; i < LC_MOD_COUNT; ++i )
		if ( s.mods[i] )
			mods << U( lc_modifier_name( i ) );
	if ( !mods.isEmpty() )
		game += dot + mods.join( ", " );
	m_srvGame->setText( game );

	/* maps */
	const lc_map_catalog *cat = lc_map_catalog_get( B( m_dataFolder ).constData(), B( m_homeBase ).constData() );
	QStringList found;
	int missing = 0;
	QStringList names;
	for ( int i = 0; i < s.rotationCount; ++i ) {
		const lc_map_info *mi = cat ? lc_map_find( cat, s.rotation[i] ) : nullptr;
		names << U( s.rotation[i] );
		if ( mi )
			found << U( lc_map_display_name( mi ) );
		else
			++missing;
	}
	QString maps;
	QColor mapColor;
	if ( s.rotationCount == 0 ) {
		maps = "None chosen yet";
		mapColor = orange();
	} else if ( s.rotationCount == 1 ) {
		if ( missing ) {
			maps = "1 missing map";
			mapColor = orange();
		} else
			maps = found.value( 0 );
	} else {
		maps = QString( "%1 in rotation: %2" ).arg( s.rotationCount ).arg( found.join( ", " ) );
		if ( missing ) {
			maps += QString( " (%1 missing)" ).arg( missing );
			mapColor = orange();
		}
	}
	m_srvMaps->setText( maps );
	setColor( m_srvMaps, mapColor.isValid() ? mapColor : QGuiApplication::palette().color( QPalette::WindowText ) );
	m_srvMaps->setToolTip( names.join( QString::fromUtf8( " \xe2\x86\x92 " ) ) );

	QString players = QString( "Up to %1" ).arg( s.maxPlayers );
	if ( s.bots > 0 )
		players += QString( " \xc2\xb7 bots fill to %1%2 (%3)" ).arg( s.bots )
			.arg( s.gametype >= 3 ? " per team" : "" ).arg( U( lc_skill_name( s.botSkill ) ) );
	else
		players += QString::fromUtf8( " \xc2\xb7 no bots" );
	m_srvPlayers->setText( QString::fromUtf8( players.toUtf8() ) );

	const bool lan = s.port >= 27960 && s.port <= 27963;
	QString net = s.internet ? ( s.openPort ? "Internet, port opened on the router" : "Internet" ) : "Local network only";
	net += dot + QString( "port %1" ).arg( s.port );
	if ( !lan )
		net += " (not listed in the Join menu)";
	if ( s.password[0] )
		net += dot + "password";
	m_srvNetwork->setText( net );
	setColor( m_srvNetwork, lan ? QGuiApplication::palette().color( QPalette::WindowText ) : orange() );
	char lanAddr[64] = "";
	if ( !lc_lan_address( lanAddr, sizeof lanAddr ) || !lanAddr[0] )
		lc_strlcpy( lanAddr, "127.0.0.1", sizeof lanAddr );
	m_srvAddress->setText( QString( "%1:%2" ).arg( U( lanAddr ) ).arg( s.port ) );
}

void LauncherWindow::refreshMasters()
{
	char masters[LC_MAX_MASTERS][65];
	const int n = lc_masters_load( lc_store_shared(), masters );
	QString text;
	QStringList all;
	for ( int i = 0; i < n; ++i )
		all << U( masters[i] );
	QColor c;
	if ( n == 0 ) {
		text = "No master servers";
		c = orange();
	} else if ( n == 1 )
		text = QString::fromUtf8( "1 master server \xc2\xb7 " ) + all[0];
	else
		text = QString::fromUtf8( "%1 master servers \xc2\xb7 Internet 1: %2" ).arg( n ).arg( all[0] );
	m_masterSummary->setText( text );
	setColor( m_masterSummary, c.isValid() ? c : QGuiApplication::palette().color( QPalette::WindowText ) );
	m_masterSummary->setToolTip( all.join( "\n" ) );
}

/* ---- game files ----------------------------------------------------------------------------------------------- */

void LauncherWindow::refreshData()
{
	m_haveData = false;
	m_missingPatch = false;
	m_overridden = lc_data_overridden( g_argc, g_argv ) != 0;
	memset( &m_data, 0, sizeof m_data );
	QString path, status, tip, icon, expText, expIcon;
	QColor sc = secondary(), ec = secondary();
	bool showExp = false;
	if ( m_overridden ) {
		m_haveData = true;
		m_dataFolder.clear();
		path = "Set on the command line";
		status = "fs_basepath / fs_homepath given on the command line";
		icon = "terminal";
	} else {
		lc_data_find( g_argc, g_argv, &m_data );
		if ( m_data.found ) {
			m_haveData = true;
			m_dataFolder = U( m_data.base );
			path = tilde( m_dataFolder );
			char names[2][16];
			const int missing = lc_data_missing_patch_paks( B( m_homeBase ).constData(), m_data.base, names );
			if ( missing ) {
				m_missingPatch = true;
				QStringList l;
				for ( int i = 0; i < 2 && names[i][0]; ++i )
					l << U( names[i] );
				const QString joined = l.join( " and " );
				status = QString::fromUtf8( "Patch 1.2 missing \xc2\xb7 add %1 with Add Game Files\xe2\x80\xa6" ).arg( joined );
				tip = QString( "These game files are from the original CD (version 1.0). The game also needs %1 from the 1.1 and 1.2 patches: "
					"copy them from a patched installation (GOG, Steam, or a PC with patch 1.2) with Add Game Files\xe2\x80\xa6." )
					.arg( joined );
				icon = "exclamationmark.triangle.fill";
				sc = orange();
				log( QString( "Launcher: patch files missing (%1) in \"%2\"" ).arg( l.join( ", " ), m_dataFolder ) );
			} else {
				status = m_data.basepath[0] ? QString::fromUtf8( "Ready \xc2\xb7 using the files in this folder" )
					: QString::fromUtf8( "Ready \xc2\xb7 copied into the app's own folder" );
				icon = "check.circle.fill";
				sc = green();
			}
		} else {
			m_dataFolder.clear();
			path = "No game files yet";
			status = "Choose the Elite Force folder (the one with BaseEF), or the GOG setup .exe.";
			icon = "exclamationmark.triangle.fill";
			sc = orange();
		}
	}
	m_pathLabel->setText( path );
	m_pathLabel->setToolTip( m_dataFolder );
	m_statusText->setText( status );
	m_statusText->setToolTip( tip );
	m_statusIcon->setPixmap( iconPixmap( icon.replace( "check.circle", "check.circle" ), sc, 14 ) );
	setColor( m_statusText, m_missingPatch || !m_haveData ? orange() : secondary() );
	m_folderIcon->setPixmap( iconPixmap( "folder", m_dataFolder.isEmpty() ? tertiary() : accent(), 34 ) );
	m_showFolder->setVisible( !m_dataFolder.isEmpty() );
	m_addFiles->setVisible( m_haveData && !m_overridden );
	showExp = m_haveData && !m_overridden;
	if ( showExp ) {
		const bool has = lc_data_has_expansion( B( m_homeBase ).constData(), B( m_dataFolder ).constData() ) != 0;
		expText = has ? QString::fromUtf8( "Expansion Pack found \xc2\xb7 Virtual Voyager and its Holomatch maps" )
			: QString::fromUtf8( "No Expansion Pack \xc2\xb7 add its files to play Virtual Voyager and its maps" );
		m_expIcon->setPixmap( iconPixmap( has ? "puzzlepiece.extension.fill" : "puzzlepiece.extension",
			has ? green() : tertiary(), 14 ) );
		m_expText->setText( expText );
	}
	m_expIcon->setVisible( showExp );
	m_expText->setVisible( showExp );
	m_change->setEnabled( !m_overridden );
	m_change->setText( m_haveData ? QString::fromUtf8( "Change\xe2\x80\xa6" ) : QString::fromUtf8( "Choose\xe2\x80\xa6" ) );
	refreshPlayEnabled();
	if ( m_overridden )
		m_statusText->setToolTip( QString() );
	refreshServerSummary();
}

void LauncherWindow::openChooser()
{
	if ( DataUi::runChooser( this ) )
		log( "Launcher: game folder changed" );
	refreshData();
	raise();
	activateWindow();
}

void LauncherWindow::openAddFiles( const QString &testPath )
{
	const int n = DataUi::addGameFiles( this, testPath );
	if ( n > 0 )
		log( "Launcher: game files added" );
	refreshData();
	raise();
	activateWindow();
}

/* ---- sheets ----------------------------------------------------------------------------------------------------- */

template <class D> int LauncherWindow::runSheet( D *d )
{
	m_sheet = d;
	const int r = d->exec();
	m_sheet = nullptr;
	return r;
}

void LauncherWindow::openBrowser()
{
	BrowserDialog dlg( this, m_dataFolder );
	if ( runSheet( &dlg ) == QDialog::Accepted && !dlg.address().isEmpty() ) {
		m_joinAddress = dlg.address();
		m_joinPassword = dlg.password();
		selectMode( 1 );
		play();
	}
}

void LauncherWindow::openConfig()
{
	ServerConfigDialog dlg( this, m_server, m_dataFolder );
	runSheet( &dlg );
	refreshServerSummary();
}

void LauncherWindow::openMasters()
{
	MasterDialog dlg( this );
	runSheet( &dlg );
	refreshMasters();
}

void LauncherWindow::openHud()
{
	HudDialog dlg( this, m_cmod );
	if ( runSheet( &dlg ) == QDialog::Accepted )
		dlg.applyTo( &m_cmod, lc_store_shared() );
}

void LauncherWindow::openCoop( const QString &address )
{
	if ( m_sheet )
		return;
	CoopDialog dlg( this, canPlay(), address );
	connect( &dlg, &QDialog::accepted, this, [&]() {
		if ( dlg.choice() != CoopDialog::Cancelled ) {
			selectMode( 0 );
			QTimer::singleShot( 0, this, [this]() { play(); } );
		}
	} );
	runSheet( &dlg );
}

/* ---- Play ----------------------------------------------------------------------------------------------------------- */

void LauncherWindow::saveMode()
{
	lc_store *st = lc_store_shared();
	lc_store_set_int( st, LC_KEY_MODE, m_mode );
	lc_server_settings_save( m_server, st );
	lc_store_save( st );
}

void LauncherWindow::play()
{
	if ( !canPlay() )
		return;
	if ( m_mode == 2 ) {
		startServer();
		return;
	}
	doPlay();
}

void LauncherWindow::quit()
{
	log( "Launcher: quit" );
	QCoreApplication::exit( 0 );
}

void LauncherWindow::reject()
{
	quit();
}

QString LauncherWindow::loopbackIP( bool wait )
{
	if ( !lc_store_get_bool( lc_store_shared(), LC_KEY_LOOPBACK_FIX, 1 ) )
		return QString();
	/* VSP_LoopbackArgs: waits up to 1.5 s for the router's answer, keeping the UI alive */
	const double t0 = lc_now();
	for ( ;; ) {
		if ( const char *ip = lc_public_address() )
			return U( ip );
		if ( !wait || !lc_public_address_pending() || lc_now() - t0 > 1.5 )
			return QString();
		QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
		lc_sleep_ms( 20 );
	}
}

lc_launch LauncherWindow::makeLaunch( int mode, QByteArray &nameBytes, QByteArray &basepathBytes,
	QByteArray &loopBytes, QByteArray &joinAddr, QByteArray &joinPw )
{
	lc_launch o;
	memset( &o, 0, sizeof o );
	o.mode = mode;
	o.gfx = m_gfx;
	nameBytes = B( m_name->text() );
	o.name = nameBytes.constData();
	o.cmod = m_cmod;
	if ( m_cmod.available )
		o.cmod.on = m_cmodBox->isChecked();
	basepathBytes = B( U( m_data.basepath ) );
	o.basepath = basepathBytes.isEmpty() ? nullptr : basepathBytes.constData();
	if ( mode == LC_MODE_HOLOMATCH ) {
		char masters[LC_MAX_MASTERS][65];
		const int n = lc_masters_load( lc_store_shared(), masters );
		o.mastersCfgWritten = lc_masters_write_cfg( B( m_homeBase ).constData(), masters, n, 0 ) != 0;
		loopBytes = B( loopbackIP( true ) );
		o.loopbackIP = loopBytes.isEmpty() ? nullptr : loopBytes.constData();
		if ( !m_joinAddress.isEmpty() ) {
			joinAddr = B( m_joinAddress );
			o.joinAddress = joinAddr.constData();
			if ( !m_joinPassword.isEmpty() ) {
				joinPw = B( m_joinPassword );
				o.joinPassword = joinPw.constData();
			}
		}
	} else {
		struct lc_launch_coop_fields cf;
		memset( &cf, 0, sizeof cf );
		lc_coop_launch_fields( &cf );
		o.coopHost = cf.coopHost;
		o.coopBackground = lc_store_get_bool( lc_store_shared(), LC_KEY_COOP_BACKGROUND, 1 );
		o.coopConnect = cf.coopConnect;
		o.coopSex = cf.coopSex;
		o.coopName = cf.coopName;
		if ( cf.coopHost )
			o.coopBackground = cf.coopBackground;
	}
	return o;
}

QStringList LauncherWindow::clientArgs()
{
	QByteArray a, b, c, d, e;
	const lc_launch o = makeLaunch( LC_MODE_HOLOMATCH, a, b, c, d, e );
	lc_launch client = o;
	client.joinAddress = nullptr;
	client.joinPassword = nullptr;
	int argc = 0;
	char **argv = lc_launch_client_argv( &client, B( m_homeBase ).constData(), "efsp", &argc );
	QStringList out;
	for ( int i = 1; argv && i < argc; ++i )
		out << U( argv[i] );
	lc_argv_free( argv );
	return out;
}

static QString maskedArgs( char **argv, int argc, const QString &pw )
{
	QStringList l;
	for ( int i = 0; i < argc; ++i ) {
		QString a = U( argv[i] );
		if ( !pw.isEmpty() && a == pw )
			a = QString::fromUtf8( "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" );
		l << a;
	}
	return l.join( ' ' );
}

void LauncherWindow::doPlay()
{
	saveMode();
	char efsp[LC_MAX_PATH];
	if ( !lc_sibling_exe( "efsp", efsp, sizeof efsp ) ) {
		alert( this, Critical, "The game couldn't be started.", "The game's program (efsp) wasn't found next to the launcher." );
		return;
	}
	QByteArray a, b, c, d, e;
	lc_launch o = makeLaunch( m_mode == 0 ? LC_MODE_SP : LC_MODE_HOLOMATCH, a, b, c, d, e );
	int argc = 0;
	char **argv = lc_launch_build_argv( &o, B( m_homeBase ).constData(), efsp, &argc );
	if ( !argv ) {
		alert( this, Critical, "The game couldn't be started.", "The launcher couldn't build the game's command line." );
		return;
	}
	log( "Launcher: " + maskedArgs( argv, argc, m_joinPassword ) );

	QList<QByteArray> envs;
	envs << "EF_FROM_LAUNCHER=1";
	const QString coopPw = U( lc_coop_password() );
	if ( m_mode == 0 && !coopPw.isEmpty() )
		envs << "EF_COOP_PASSWORD=" + B( coopPw );
	QVector<const char *> envp;
	for ( const QByteArray &s : envs )
		envp << s.constData();
	envp << nullptr;

	if ( testSet( "EF_LAUNCHER_DRYRUN" ) ) {
		fprintf( stderr, "[test] exec:" );
		for ( int i = 0; i < argc; ++i )
			fprintf( stderr, " %s", argv[i] );
		fprintf( stderr, "\n" );
		lc_argv_free( argv );
		QCoreApplication::exit( 0 );
		return;
	}
	lc_store_save( lc_store_shared() );
	lc_exec( argv, envp.constData() );
	/* only returns on failure */
	alert( this, Critical, "The game couldn't be started.", U( strerror( errno ) ) );
	lc_argv_free( argv );
}

void LauncherWindow::startServer()
{
	saveMode();
	struct lc_running running[16];
	const QByteArray home = B( U( lc_homepath() ) );
	int n = lc_running_list( home.constData(), running, 16 );
	if ( n > 0 ) {
		std::sort( running, running + n, []( const lc_running &x, const lc_running &y ) { return x.started < y.started; } );
		const lc_running &last = running[n - 1];
		const QString msg = n == 1 ? "A server is already running on this " + nounLc() + "."
			: QString( "%1 servers are already running on this %2." ).arg( n ).arg( nounLc() );
		const QString nm = last.name[0] ? U( last.name ) : QString( "Server" );
		const QString info = QString::fromUtf8( "\xe2\x80\x9c%1\xe2\x80\x9d is running on port %2. Another server would start on the next free port, "
			"with the settings in Config\xe2\x80\xa6; players would see both in the server list." ).arg( nm ).arg( last.port );
		int ans;
		const QString t = testEnv( "VOYAGERSP_TEST_SERVER_RUNNING" ).toLower();
		if ( !t.isEmpty() ) {
			ans = t == "show" ? 0 : t == "another" ? 1 : 2;
			log( "[test] server running alert: " + t );
		} else
			ans = alert( this, Warning, msg, info, QStringList() << "Show Running Server" << "Start Another" << "Cancel", 0, 2 );
		if ( ans == 0 ) {
			log( QString( "Launcher: showing the running server (pid %1)" ).arg( last.pid ) );
			Instance::activateServer( last.pid );
			return;
		}
		if ( ans == 2 )
			return;
	}
	QByteArray self = B( U( lc_self_command() ) );
	const char *argv[3] = { self.constData(), LC_HOST_SERVER_ARG, nullptr };
	char err[256] = "";
	log( "Launcher: starting the server in its own window" );
	const long pid = lc_spawn_detached( argv, nullptr, "VOYAGERSP_TEST_", err, sizeof err );
	if ( pid < 0 ) {
		alert( this, Critical, "The server couldn't be started.", U( err ) );
		return;
	}
	log( QString( "Launcher: server window started, pid %1" ).arg( pid ) );
}

/* ---- updates, About ------------------------------------------------------------------------------------------------- */

void LauncherWindow::showUpdateBadge()
{
	const QString mine = U( EF_APP_VERSION );
	const bool newer = !m_updateVersion.isEmpty() && lc_version_is_newer( B( m_updateVersion ).constData(),
		B( mine ).constData() );
	if ( !newer || !m_autoUpdateAction->isChecked() ) {
		m_badge->hide();
		return;
	}
	m_badgeText->setText( QString::fromUtf8( "Version %1 \xc2\xb7 you have %2" ).arg( m_updateVersion, mine ) );
	m_badge->setToolTip( QString( "Elite Force for %1 %2 is on GitHub" ).arg( platformName(), m_updateVersion ) );
	m_badge->show();
}

QString LauncherWindow::platformName()
{
	return QStringLiteral( "Linux" );
}

void LauncherWindow::startUpdateCheck()
{
	lc_store *st = lc_store_shared();
	if ( !lc_store_get_bool( st, LC_KEY_CHECK_UPDATES, 1 ) )
		return;
	char sv[64] = "", su[512] = "";
	const int due = lc_update_auto_state( st, sv, sizeof sv, su, sizeof su );
	if ( sv[0] ) {
		m_updateVersion = U( sv );
		m_updateUrl = U( su );
		showUpdateBadge();
	}
	if ( !due )
		return;
	auto *res = new lc_update_result;
	memset( res, 0, sizeof *res );
	async( this, [res]() { lc_update_fetch( EF_APP_VERSION, res ); }, [this, res]() {
		const QString mine = U( EF_APP_VERSION );
		if ( !res->ok )
			log( "Updates: couldn't check (" + U( res->problem ) + ")" );
		else {
			lc_update_save( lc_store_shared(), res, EF_APP_VERSION );
			m_updateVersion = U( res->version );
			m_updateUrl = U( res->url );
			log( QString( "Updates: latest release %1, this is %2" ).arg( m_updateVersion.isEmpty() ? "(none)" : m_updateVersion, mine ) );
			showUpdateBadge();
		}
		delete res;
	} );
}

void LauncherWindow::checkForUpdatesNow()
{
	auto *res = new lc_update_result;
	memset( res, 0, sizeof *res );
	async( this, [res]() { lc_update_fetch( EF_APP_VERSION, res ); }, [this, res]() {
		const QString mine = U( EF_APP_VERSION );
		char msg[512] = "", info[1024] = "";
		int newer = 0;
		lc_update_alert_texts( res, EF_APP_VERSION, msg, sizeof msg, info, sizeof info, &newer );
		if ( res->ok ) {
			lc_update_save( lc_store_shared(), res, EF_APP_VERSION );
			m_updateVersion = U( res->version );
			m_updateUrl = U( res->url );
			showUpdateBadge();
		}
		log( QString( "[test] Check for Updates: \"%1\" \"%2\"" ).arg( U( msg ), U( info ) ) );
		int ans = 0;
		const QString snap = testEnv( "VOYAGERSP_TEST_UPDATE_ALERT" );
		if ( !snap.isEmpty() ) {
			QMessageBox *box = new QMessageBox( this );
			box->setIcon( QMessageBox::Information );
			box->setTextFormat( Qt::PlainText );
			box->setText( U( msg ) );
			box->setInformativeText( U( info ) );
			box->addButton( res->ok && newer ? QString::fromUtf8( "Download\xe2\x80\xa6" ) : QString( "OK" ), QMessageBox::AcceptRole );
			if ( res->ok && newer )
				box->addButton( "Later", QMessageBox::RejectRole );
			box->show();
			QCoreApplication::processEvents();
			QTimer::singleShot( 300, this, [this, box, snap]() { savePng( box, snap ); quitSoon(); } );
			delete res;
			return;
		}
		if ( res->ok && newer )
			ans = alert( this, Info, U( msg ), U( info ), QStringList() << QString::fromUtf8( "Download\xe2\x80\xa6" ) << "Later", 0, 1 );
		else
			alert( this, Info, U( msg ), U( info ) );
		if ( res->ok && newer && ans == 0 && !m_updateUrl.isEmpty() )
			lc_open_url( B( m_updateUrl ).constData() );
		delete res;
	} );
}

void LauncherWindow::showAbout()
{
	QMessageBox *box = makeAbout();
	box->exec();
	delete box;
}

QMessageBox *LauncherWindow::makeAbout()
{
	QMessageBox *box = new QMessageBox( this );
	box->setWindowTitle( "About Elite Force" );
	box->setTextFormat( Qt::RichText );
	const QString ver = U( EF_APP_VERSION );
	box->setIconPixmap( QPixmap( ":/icon.png" ).scaled( px( 64 ), px( 64 ), Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
	box->setText( QString( "<div align=center><h3>Elite Force</h3>%1</div>" )
		.arg( ver.isEmpty() ? QString() : "Version " + ver ) );
	box->setInformativeText(
		"<div align=center style='font-size:small'>"
		"<p><b>Made by Marino Hardin</b><br>(Marino's Build)</p>"
		"<p>Single player and Holomatch for Linux and the Steam Deck.<br>"
		"No game data included: requires your own retail copy of the game.</p>"
		"<p>Based on VoyagerSP-Android by imjustadudegamer,<br>"
		"lilium-voyager by Zack Middleton, ioquake3, id Software's Quake III Arena,<br>"
		"the Quake3e Vulkan renderer by Eugene C., and Raven Software's<br>"
		"Elite Force single-player game source.</p>"
		"<p>Many Holomatch fixes and improvements come from <b>cMod</b><br>"
		"by <b>Noah Metzger (Chomenor)</b>, with great thanks for his work<br>"
		"and his kind offer to collaborate.</p>"
		"<p>Holomatch's widescreen HUD, lag compensation and weapon prediction<br>"
		"are <b>cMod's game modules by Noah Metzger</b>, included unchanged<br>"
		"with his permission. Thank you, Noah!</p>"
		"<p>Star Trek and related marks are trademarks of Paramount. This is an unofficial,<br>"
		"non-commercial fan project, not affiliated with or endorsed by Paramount,<br>"
		"Raven Software or Activision.</p>"
		"<p>Made by Marino Hardin. Engine: GPLv2 (ioquake3 / lilium-voyager / Quake3e). "
		"Game code: Raven Software Limited Use Software License. Game data not included.</p>"
		"<p>The launcher uses Qt 6 (LGPL v3).</p></div>" );
	box->setStandardButtons( QMessageBox::Ok );
	return box;
}

/* ---- links ------------------------------------------------------------------------------------------------------------ */

void LauncherWindow::activateSelf()
{
	showNormal();
	raise();
	activateWindow();
}

void LauncherWindow::deliverLink( const QString &link )
{
	activateSelf();
	const QByteArray lb = B( link );
	char addr[300] = "";
	if ( lc_coop_ui_enabled() ) {
		const int c = lc_coop_link_address( lb.constData(), addr, sizeof addr );
		if ( c >= 0 ) {
			if ( c == 0 ) {
				alert( this, Info, "Join co-op", QString::fromUtf8( "The co-op invite %1 has no address in it." ).arg( link ) );
				log( "Link: " + link + " -> not a server address" );
				return;
			}
			log( "Link: " + link + " -> " + U( addr ) );
			if ( m_sheet )
				m_sheet->reject();
			selectMode( 0 );
			QTimer::singleShot( 0, this, [this]() { QTimer::singleShot( 0, this, [this]() {} ); } );
			const QString a = U( addr );
			QTimer::singleShot( 0, this, [this, a]() { openCoop( a ); } );
			return;
		}
	}
	char pw[128] = "";
	if ( !lc_link_address( lb.constData(), addr, sizeof addr, pw, sizeof pw ) ) {
		log( "Link: " + link + " -> not a server address" );
		alert( this, Warning, "This link can't be joined", link + "\n\nIt doesn't hold a server address." );
		return;
	}
	log( "Link: " + link + " -> " + U( addr ) );
	if ( m_sheet )
		m_sheet->reject();
	m_joinAddress = U( addr );
	m_joinPassword = U( pw );
	selectMode( 1 );
	QTimer::singleShot( 0, this, [this]() { play(); } );
}

/* ---- test hooks -------------------------------------------------------------------------------------------------------- */

void LauncherWindow::runTestHooks()
{
	const QString tp = testEnv( "VOYAGERSP_TEST_LAUNCHER_PRESET" ).toLower();
	if ( !tp.isEmpty() )
		applyPreset( tp == "performance" ? LC_PRESET_PERFORMANCE : tp == "balanced" ? LC_PRESET_BALANCED : LC_PRESET_BEST );
	const QString tb = testEnv( "VOYAGERSP_TEST_LAUNCHER_BRIGHTNESS" ).toLower();
	if ( !tb.isEmpty() ) {
		for ( int i = 0; i < lc_brightness_count(); ++i )
			if ( tb == U( lc_brightness_test_id( i ) ) ) {
				m_gfx.brightness = i;
				showGraphics();
			}
	}
	const QString snapshot = testEnv( "VOYAGERSP_TEST_LAUNCHER_SNAPSHOT" );
	const bool play = testSet( "VOYAGERSP_TEST_LAUNCHER_PLAY" );
	const bool serverStart = testSet( "VOYAGERSP_TEST_SERVER_START" );

	if ( play ) {
		QTimer::singleShot( 0, this, [this]() { this->play(); } );
		return;
	}
	const QString autoplay = testEnv( "VOYAGERSP_TEST_LAUNCHER_AUTOPLAY" );
	if ( !autoplay.isEmpty() ) {
		QTimer::singleShot( int( autoplay.toDouble() * 1000 ), this, [this]() {
			log( QString( "[test] launcher visible: %1" ).arg( isVisible() ? "yes" : "no" ) );
			this->play();
		} );
	}
	const QString add = testEnv( "VOYAGERSP_TEST_ADD" );
	if ( !add.isEmpty() )
		QTimer::singleShot( 100, this, [this, add]() { openAddFiles( add ); } );

	auto snapSheet = [this]( QDialog *d, const QString &png, bool quit ) {
		d->show();
		for ( int i = 0; i < 5; ++i )
			QCoreApplication::processEvents();
		QTimer::singleShot( 400, d, [d, png, quit]() {
			savePng( d, png );
			if ( quit )
				quitSoon();
		} );
	};
	const bool keepGoing = !snapshot.isEmpty() ? ( play || serverStart ) : false;

	if ( !snapshot.isEmpty() ) {
		QTimer::singleShot( 400, this, [this, snapshot, keepGoing]() {
			savePng( this, snapshot );
			if ( !keepGoing )
				quitSoon();
		} );
	}
	const QString hud = testEnv( "VOYAGERSP_TEST_LAUNCHER_HUD" );
	if ( !hud.isEmpty() ) {
		selectMode( 1 );
		HudDialog *d = new HudDialog( this, m_cmod );
		snapSheet( d, hud, true );
	}
	const QString about = testEnv( "VOYAGERSP_TEST_LAUNCHER_ABOUT" );
	if ( !about.isEmpty() ) {
		QTimer::singleShot( 1000, this, [this, about]() {
			QStringList items;
			for ( QAction *a : m_menu->actions() )
				items << QString( "%1=%2" ).arg( a->text(), a->isEnabled() ? "enabled" : "disabled" );
			log( "[test] app menu: " + items.join( " | " ) );
			QMessageBox *b = makeAbout();
			b->show();
			QTimer::singleShot( 1500, b, [b, about]() { savePng( b, about ); b->close(); quitSoon(); } );
		} );
	}
	if ( !testEnv( "VOYAGERSP_TEST_UPDATE_ALERT" ).isEmpty() )
		QTimer::singleShot( 500, this, [this]() { checkForUpdatesNow(); } );
	const QString link = testEnv( "VOYAGERSP_TEST_LINK" );
	if ( !link.isEmpty() )
		QTimer::singleShot( 1000, this, [this, link]() { deliverLink( link ); } );

	const QString masters = testEnv( "VOYAGERSP_TEST_MASTERS_SNAPSHOT" );
	if ( !masters.isEmpty() ) {
		QTimer::singleShot( 300, this, [this, masters, keepGoing]() {
			MasterDialog *d = new MasterDialog( this );
			d->show();
			const QString acts = testEnv( "VOYAGERSP_TEST_MASTERS_ACTIONS" );
			for ( const QString &a : acts.split( ',', Qt::SkipEmptyParts ) )
				d->testAction( a.trimmed() );
			d->waitForChecks( 20000 );
			QCoreApplication::processEvents();
			savePng( d, masters );
			if ( !keepGoing )
				quitSoon();
		} );
	}
	const QString cfg = testEnv( "VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT" );
	if ( !cfg.isEmpty() ) {
		QTimer::singleShot( 300, this, [this, cfg]() {
			selectMode( 2 );
			ServerConfigDialog *d = new ServerConfigDialog( this, m_server, m_dataFolder );
			d->selectTab( testEnv( "VOYAGERSP_TEST_SERVER_CONFIG_TAB" ) );
			const QString tr = testEnv( "VOYAGERSP_TEST_SERVER_TICKRATE" );
			if ( !tr.isEmpty() )
				d->setTickRate( tr.toInt() );
			d->show();
			for ( int i = 0; i < 5; ++i )
				QCoreApplication::processEvents();
			QTimer::singleShot( 600, d, [d, cfg]() { savePng( d, cfg ); quitSoon(); } );
		} );
	}
	const QString browser = testEnv( "VOYAGERSP_TEST_BROWSER_SNAPSHOT" );
	if ( !browser.isEmpty() ) {
		QTimer::singleShot( 300, this, [this, browser]() {
			selectMode( 1 );
			BrowserDialog *d = new BrowserDialog( this, m_dataFolder );
			d->selectSource( testEnv( "VOYAGERSP_TEST_BROWSER_TAB" ) );
			d->show();
			auto *poll = new QTimer( d );
			poll->setInterval( 200 );
			auto *elapsed = new QElapsedTimer;
			elapsed->start();
			connect( poll, &QTimer::timeout, d, [this, d, poll, elapsed, browser]() {
				if ( d->busy() && elapsed->elapsed() < 60000 )
					return;
				poll->stop();
				d->logRows();
				const QString sel = testEnv( "VOYAGERSP_TEST_BROWSER_SELECT" );
				if ( !sel.isEmpty() )
					d->selectRow( sel.toInt() );
				QTimer::singleShot( sel.isEmpty() ? 100 : 1500, d, [this, d, browser]() {
					savePng( d, browser );
					const QString join = testEnv( "VOYAGERSP_TEST_BROWSER_JOIN" );
					if ( !join.isEmpty() ) {
						m_sheet = d;
						d->joinRow( join.toInt() );
						if ( !d->address().isEmpty() ) {
							m_joinAddress = d->address();
							m_joinPassword = d->password();
							selectMode( 1 );
							this->play();
							return;
						}
					}
					const QString hold = testEnv( "VOYAGERSP_TEST_BROWSER_HOLD" );
					quitSoon( int( hold.toDouble() * 1000 ) );
				} );
			} );
			poll->start();
		} );
	}
	if ( testSet( "VOYAGERSP_TEST_LAUNCHER_BROWSE_CANCEL" ) ) {
		QTimer::singleShot( 300, this, [this]() {
			selectMode( 1 );
			BrowserDialog *d = new BrowserDialog( this, m_dataFolder );
			QTimer::singleShot( 800, d, [this, d]() {
				d->reject();
				log( QString( "[test] launcher stayed after Cancel: %1" ).arg( isVisible() ? "yes" : "no" ) );
				quitSoon();
			} );
			d->exec();
		} );
	}
	const QString coop = testEnv( "VOYAGERSP_TEST_COOP_SHEET" );
	if ( !coop.isEmpty() || testSet( "VOYAGERSP_TEST_COOP_HOST" ) || testSet( "VOYAGERSP_TEST_COOP_INTERNET" ) ) {
		QTimer::singleShot( 1000, this, [this, coop]() {
			selectMode( 0 );
			CoopDialog *d = new CoopDialog( this, canPlay() );
			m_sheet = d;
			d->runTestHooks();
			if ( !coop.isEmpty() ) {
				d->show();
				for ( int i = 0; i < 5; ++i )
					QCoreApplication::processEvents();
				QTimer::singleShot( 600, d, [d, coop]() {
					savePng( d, coop );
					const QString hold = testEnv( "VOYAGERSP_TEST_COOP_HOLD" );
					QTimer::singleShot( int( hold.toDouble() * 1000 ), d, [d]() { d->reject(); quitSoon(); } );
				} );
			}
		} );
	}
}
