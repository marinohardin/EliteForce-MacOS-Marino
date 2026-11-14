/*
===========================================================================
ef-launcher (Qt 6): the Co-op sheet. LDC §6.1 (the button is hidden unless
lc_coop_ui_enabled(); everything is built).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

#include <arpa/inet.h>

using namespace Ui;

namespace {

int coopPort()
{
	const QString p = testEnv( "VOYAGERSP_TEST_COOP_PORT" );
	return p.isEmpty() ? LC_COOP_PORT : p.toInt();
}

}

CoopDialog::CoopDialog( QWidget *parent, bool canPlay, const QString &address, const QString &dataFolder )
	: QDialog( parent ), m_canPlay( canPlay ), m_dataFolder( dataFolder )
{
	setWindowTitle( "Co-op" );
	setModal( true );
	const QString thisNoun = nounLc();

	QWidget *content = new QWidget;
	QVBoxLayout *root = new QVBoxLayout( content );
	root->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 8 ) );
	root->setSpacing( px( 12 ) );

	/* heading */
	QHBoxLayout *hd = new QHBoxLayout;
	hd->addWidget( label( "Co-op", 17, QFont::Bold ) );
	hd->addWidget( label( "EXPERIMENTAL", 10, QFont::DemiBold, orange() ), 0, Qt::AlignBottom );
	hd->addStretch( 1 );
	root->addLayout( hd );
	root->addSpacing( -px( 8 ) );
	root->addWidget( label( "Play the campaign with a friend. The host is player 1 and plays the story; the friend joins as player 2.",
		12, QFont::Normal, secondary(), 520 ) );

	/* name row */
	lc_store *st = lc_store_shared();
	QHBoxLayout *nr = new QHBoxLayout;
	nr->addWidget( label( "Your name", 12 ) );
	m_name = new QLineEdit;
	m_name->setFixedWidth( px( 220 ) );
	char safe[64] = "";
	lc_coop_safe_name( lc_store_get( st, LC_KEY_COOP_NAME ) ? lc_store_get( st, LC_KEY_COOP_NAME ) : "", safe, sizeof safe );
	char comp[64] = "";
	lc_coop_safe_name( lc_computer_name(), comp, sizeof comp );
	if ( !comp[0] )
		lc_strlcpy( comp, LC_PLATFORM_NOUN, sizeof comp );
	m_name->setText( U( safe[0] ? safe : comp ) );
	m_name->setPlaceholderText( U( comp ) );
	m_name->setToolTip( QString::fromUtf8( "Shown to the other player in co-op messages, such as \xe2\x80\x9cHold Use to revive \xe2\x80\xa6\xe2\x80\x9d" ) );
	m_name->setMaxLength( 31 );
	nr->addWidget( m_name );
	nr->addWidget( label( "up to 31 letters", 11, QFont::Normal, secondary() ) );
	nr->addStretch( 1 );
	root->addLayout( nr );

	/* HOST group */
	Group *hg = new Group( "Host" );
	QHBoxLayout *hrow = new QHBoxLayout;
	QVBoxLayout *hl = new QVBoxLayout;
	hl->setSpacing( px( 8 ) );
	hl->addWidget( label( QString::fromUtf8( "Starts single player with co-op on: choose New Game or Load as usual. Your friend then joins as player 2 "
		"with this %1\xe2\x80\x99s address:" ).arg( thisNoun ), 12, QFont::Normal, QColor(), 400 ) );
	char addrs[8][16];
	char ifn[8][64];
	const int na = lc_coop_lan_addresses( addrs, ifn, 8 );
	QStringList lines;
	for ( int i = 0; i < na; ++i ) {
		m_lan << U( addrs[i] );
		lines << QString( "%1    (%2)" ).arg( U( addrs[i] ), U( ifn[i] ) );
	}
	if ( na > 0 ) {
		m_lanLabel = label( lines.join( "\n" ), 13, QFont::Medium );
		QFont f = Ui::font( 13, QFont::Medium, true );
		m_lanLabel->setFont( f );
		m_lanLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
	} else
		m_lanLabel = label( QString( "No network found. Connect this %1 to Wi-Fi or Ethernet first." ).arg( thisNoun ), 12, QFont::Normal, orange() );
	hl->addWidget( m_lanLabel );

	QHBoxLayout *pw = new QHBoxLayout;
	pw->addWidget( label( "Password (optional)", 12 ) );
	m_hostPw = new QLineEdit;
	m_hostPw->setEchoMode( QLineEdit::Password );
	m_hostPw->setPlaceholderText( "none" );
	m_hostPw->setFixedWidth( px( 160 ) );
	pw->addWidget( m_hostPw );
	pw->addStretch( 1 );
	hl->addLayout( pw );

	m_background = new QCheckBox( "Keep playing in the background" );
	m_background->setChecked( lc_store_get_bool( st, LC_KEY_COOP_BACKGROUND, 1 ) != 0 );
	m_background->setToolTip( QString::fromUtf8( "The game goes on while its window isn\xe2\x80\x99t in front, e.g. while you click into a second copy "
		"on this %1 (keys and the mouse still go only to the window in front)" ).arg( thisNoun ) );
	hl->addWidget( m_background );
	m_router = new QCheckBox( "Open the port on my router (for a friend on the internet)" );
	hl->addWidget( m_router );
	m_internetStatus = label( QString(), 11, QFont::Normal, secondary(), 400 );
	m_internetStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
	hl->addWidget( m_internetStatus );
	m_copyInvite = button( "Copy Invite", "Copy a stvef://coop/ link for your friend: opening it shows Co-op with your address filled in" );
	hl->addWidget( m_copyInvite, 0, Qt::AlignLeft );
	hl->addWidget( label( QString::fromUtf8( "Co-op uses UDP port %1. Player 2 follows you until your friend joins; then your friend plays player 2 "
		"and sees your game from player 2\xe2\x80\x99s eyes." ).arg( coopPort() ), 11, QFont::Normal, secondary(), 400 ) );
	hrow->addLayout( hl, 1 );
	m_host = button( "Host", m_canPlay ? "Play single player with co-op on" : "Add the game files first" );
	m_host->setMinimumWidth( px( 80 ) );
	m_host->setEnabled( m_canPlay );
	hrow->addWidget( m_host, 0, Qt::AlignTop );
	hg->content()->addLayout( hrow );
	root->addWidget( hg );
	root->addSpacing( px( 6 ) );

	/* JOIN group */
	Group *jg = new Group( "Join" );
	QVBoxLayout *jl = new QVBoxLayout;
	jl->setSpacing( px( 12 ) );
	QHBoxLayout *ar = new QHBoxLayout;
	ar->addWidget( label( QString::fromUtf8( "Host\xe2\x80\x99s address" ), 12 ) );
	m_address = new QLineEdit;
	m_address->setMinimumWidth( px( 260 ) );
	m_address->setPlaceholderText( "192.168.1.20, or address:port (default 29760)" );
	ar->addWidget( m_address, 1 );
	m_check = button( "Check", "Ask that address whether a co-op game runs there" );
	m_check->setMinimumWidth( px( 80 ) );
	ar->addWidget( m_check );
	jl->addLayout( ar );

	QHBoxLayout *r2 = new QHBoxLayout;
	r2->setSpacing( px( 24 ) );
	QHBoxLayout *jp = new QHBoxLayout;
	jp->addWidget( label( "Password", 12 ) );
	m_joinPw = new QLineEdit;
	m_joinPw->setEchoMode( QLineEdit::Password );
	m_joinPw->setPlaceholderText( "if the host set one" );
	m_joinPw->setFixedWidth( px( 160 ) );
	jp->addWidget( m_joinPw );
	r2->addLayout( jp );
	QHBoxLayout *jm = new QHBoxLayout;
	jm->addWidget( label( "Munro", 12 ) );
	m_munro = new Segmented( QStringList() << "Male" << "Female" );
	m_munro->setToolTip( QString::fromUtf8( "How player 2 looks on both screens. The story still speaks to the host\xe2\x80\x99s Munro." ) );
	m_munro->setFixedWidth( px( 140 ) );
	const char *sex = lc_store_get( st, LC_KEY_COOP_MUNRO );
	m_munro->setCurrent( sex && !strcmp( sex, "female" ) ? 1 : 0 );
	connect( m_munro, &Segmented::currentChanged, this, []( int ) {} );
	jm->addWidget( m_munro );
	r2->addLayout( jm );
	r2->addStretch( 1 );
	jl->addLayout( r2 );

	/* player 2's character (coop_characters.h): Munro, or one of the crew / the Hazard Team */
	QHBoxLayout *lr = new QHBoxLayout;
	lr->addWidget( label( "Character", 12 ) );
	m_look = new QComboBox;
	m_look->setFixedWidth( px( 220 ) );
	m_look->setToolTip( QString::fromUtf8( "Player 2\xe2\x80\x99s look on both screens: Munro, or a member of Voyager\xe2\x80\x99s crew or the Hazard Team. "
		"Only characters from the game\xe2\x80\x99s own files, so the host has them too." ) );
	m_look->addItem( "Munro (default)", QString() );
	{
		const char *saved = lc_store_get( st, LC_KEY_COOP_LOOK );
		const char *lastGroup = "";
		int select = 0;
		for ( int i = 0; i < lc_coop_character_count(); ++i ) {
			const char *id, *name, *group;
			if ( !lc_coop_character( i, &id, &name, &group ) )
				continue;
			if ( strcmp( group, lastGroup ) ) {
				lastGroup = group;
				m_look->insertSeparator( m_look->count() );
				m_look->addItem( QString::fromUtf8( strcmp( group, "crew" ) ? "Hazard Team" : "Voyager crew" ) );
				if ( QStandardItemModel *m = qobject_cast<QStandardItemModel *>( m_look->model() ) )
					if ( QStandardItem *it = m->item( m_look->count() - 1 ) )
						it->setFlags( it->flags() & ~( Qt::ItemIsSelectable | Qt::ItemIsEnabled ) );
			}
			m_look->addItem( QString( "    " ) + U( name ), U( id ) );
			if ( saved && !strcmp( saved, id ) )
				select = m_look->count() - 1;
		}
		m_look->setCurrentIndex( select );
	}
	lr->addWidget( m_look );
	lr->addWidget( label( "with the voice of the Munro above", 11, QFont::Normal, secondary() ) );
	lr->addStretch( 1 );
	jl->addLayout( lr );

	/* result row */
	QWidget *result = new QWidget;
	QHBoxLayout *rr = new QHBoxLayout( result );
	rr->setContentsMargins( 0, 0, 0, 0 );
	QWidget *iconBox = new QWidget;
	iconBox->setFixedWidth( px( 26 ) );
	QVBoxLayout *ib = new QVBoxLayout( iconBox );
	ib->setContentsMargins( 0, 0, 0, 0 );
	m_resultIcon = new QLabel;
	m_resultIcon->setFixedSize( px( 20 ), px( 20 ) );
	m_spinner = new QProgressBar;
	m_spinner->setRange( 0, 0 );
	m_spinner->setTextVisible( false );
	m_spinner->setFixedSize( px( 22 ), px( 8 ) );
	m_spinner->hide();
	ib->addWidget( m_resultIcon );
	ib->addWidget( m_spinner );
	ib->addStretch( 1 );
	rr->addWidget( iconBox, 0, Qt::AlignTop );
	QVBoxLayout *rt = new QVBoxLayout;
	m_resultTitle = label( QString(), 13, QFont::DemiBold, QColor(), 440 );
	m_resultText = label( QString(), 12, QFont::Normal, secondary(), 440 );
	m_resultText->setTextInteractionFlags( Qt::TextSelectableByMouse );
	rt->addWidget( m_resultTitle );
	rt->addWidget( m_resultText );
	rr->addLayout( rt, 1 );
	result->hide();
	m_resultWidget = result;
	jl->addWidget( result );

	QHBoxLayout *cr = new QHBoxLayout;
	m_connect = button( "Connect", "Check the address first" );
	m_connect->setMinimumWidth( px( 80 ) );
	m_connect->setEnabled( false );
	cr->addWidget( m_connect, 0, Qt::AlignTop );
	m_connectNote = label( QString::fromUtf8( "Connect starts single player and joins the host\xe2\x80\x99s game as player 2: you see it from player 2\xe2\x80\x99s "
		"eyes and play player 2 while the host plays the story." ), 11, QFont::Normal, secondary(), 380 );
	cr->addWidget( m_connectNote, 1 );
	jl->addLayout( cr );
	jg->content()->addLayout( jl );
	root->addWidget( jg );

	/* the page scrolls when the screen is short (Steam Deck) */
	QScrollArea *scroll = new QScrollArea;
	scroll->setFrameShape( QFrame::NoFrame );
	scroll->setWidgetResizable( true );
	scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	scroll->setWidget( content );
	content->setMinimumWidth( px( 560 ) );

	QVBoxLayout *outer = new QVBoxLayout( this );
	outer->setContentsMargins( 0, 0, 0, 0 );
	outer->addWidget( scroll, 1 );
	QHBoxLayout *foot = new QHBoxLayout;
	foot->setContentsMargins( px( 20 ), px( 4 ), px( 20 ), px( 18 ) );
	foot->addStretch( 1 );
	QPushButton *cancel = new QPushButton( "Cancel" );
	cancel->setMinimumWidth( px( 80 ) );
	foot->addWidget( cancel );
	outer->addLayout( foot );
	connect( cancel, &QPushButton::clicked, this, &QDialog::reject );

	const QSize sh = content->sizeHint();
	const QRect avail = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect( 0, 0, 1280, 800 );
	resize( sh.width() + px( 20 ), qMin( sh.height() + px( 60 ), avail.height() - px( 60 ) ) );

	connect( m_check, &QPushButton::clicked, this, &CoopDialog::doCheck );
	connect( m_address, &QLineEdit::returnPressed, this, &CoopDialog::doCheck );
	connect( m_address, &QLineEdit::textEdited, this, [this]() {
		m_status.clear();
		updateConnect();
	} );
	connect( m_host, &QPushButton::clicked, this, &CoopDialog::doHost );
	connect( m_connect, &QPushButton::clicked, this, &CoopDialog::doConnect );
	connect( m_router, &QCheckBox::toggled, this, &CoopDialog::natToggled );
	connect( m_copyInvite, &QPushButton::clicked, this, &CoopDialog::copyInvite );
	connect( this, &CoopDialog::natResultSignal, this, &CoopDialog::onNat, Qt::QueuedConnection );
	m_natTimer = new QTimer( this );
	m_natTimer->setSingleShot( true );
	connect( m_natTimer, &QTimer::timeout, this, [this]() {
		if ( m_natState == NatAsking ) {
			m_natState = NatFailed;
			m_natReason = QString::fromUtf8( "Your router didn\xe2\x80\x99t answer the request to open the port: it may not support automatic port "
				"opening (NAT-PMP / UPnP), or has it turned off." );
			showInternet();
		}
	} );
	showInternet();
	m_address->setFocus();
	if ( !address.isEmpty() ) {
		m_address->setText( address );
		QTimer::singleShot( 0, this, &CoopDialog::doCheck );
	}
}

CoopDialog::~CoopDialog()
{
	/* the NAT mapping is released by Cancel / Connect; Host keeps it */
}

/* ---- internet / NAT ------------------------------------------------------------------------------------------ */

QString CoopDialog::inviteLink() const
{
	QString a;
	if ( m_natState == NatMapped )
		a = m_natPublic;
	else if ( !m_lan.isEmpty() )
		a = QString( "%1:%2" ).arg( m_lan.first() ).arg( coopPort() );
	if ( a.isEmpty() )
		return QString();
	char out[256] = "";
	lc_coop_invite_link( B( a ).constData(), out, sizeof out );
	return U( out );
}

void CoopDialog::showInternet()
{
	QString text;
	QColor c = secondary();
	switch ( m_natState ) {
	case NatMapped:
		text = QString( "Port open: a friend on the internet joins %1. Copy Invite and send it to them. The port stays open while Elite Force runs." ).arg( m_natPublic );
		c = green();
		break;
	case NatAsking:
		text = QString::fromUtf8( "Asking your router to open the port\xe2\x80\xa6" );
		break;
	case NatFailed:
		text = QString( "Port not opened. %1 Forward UDP port %2 to this %3 (%4) in your router\xe2\x80\x99s settings instead, or use Tailscale (see the README)." )
			.arg( m_natReason ).arg( coopPort() ).arg( nounLc(), m_lan.isEmpty() ? QString( "this %1\xe2\x80\x99s address" ).arg( nounLc() ) : m_lan.first() );
		c = orange();
		break;
	default:
		text = QString( "Not needed on the same network. Copy Invite gives your friend a link to this %1\xe2\x80\x99s address." ).arg( nounLc() );
		break;
	}
	m_internetStatus->setText( text );
	setColor( m_internetStatus, c );
	m_copyInvite->setEnabled( !inviteLink().isEmpty() && m_natState != NatAsking );
	{
		QSignalBlocker b( m_router );
		m_router->setChecked( m_natState == NatAsking || m_natState == NatMapped );
	}
}

void CoopDialog::natToggled( bool on )
{
	if ( !on ) {
		releaseNat();
		showInternet();
		return;
	}
	m_natState = NatAsking;
	m_natReason.clear();
	m_natPublic.clear();
	m_nat = lc_nat_map_udp( coopPort(), []( lc_nat_result r, const char *ip, int ext, int err, void *u ) {
		emit static_cast<CoopDialog *>( u )->natResultSignal( int( r ), U( ip ), ext, err );
	}, this );
	m_natTimer->start( 15000 );
	showInternet();
}

void CoopDialog::onNat( int result, const QString &ip, int extPort, int err )
{
	if ( m_natState != NatAsking )
		return;
	QString why;
	switch ( result ) {
	case LC_NAT_UNSUPPORTED:
		why = QString::fromUtf8( "Your router doesn\xe2\x80\x99t support automatic port opening (NAT-PMP / UPnP), or has it turned off." );
		break;
	case LC_NAT_DISABLED:
		why = QString::fromUtf8( "Automatic port opening (NAT-PMP / UPnP) is turned off in your router\xe2\x80\x99s settings." );
		break;
	case LC_NAT_DOUBLE:
		why = "Your network has two routers in a row (double NAT), so the outer one still blocks the port.";
		break;
	case LC_NAT_TIMEOUT:
		why = QString::fromUtf8( "Your router didn\xe2\x80\x99t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off." );
		break;
	case LC_NAT_PRIVATE_EXTERNAL:
		why = QString::fromUtf8( "Your router opened the port, but its own internet address (%1) is private: your provider or another router puts it behind a "
			"second NAT, which nobody outside can get through." ).arg( ip );
		break;
	case LC_NAT_OTHER:
		why = QString::fromUtf8( "The router didn\xe2\x80\x99t open the port (error %1)." ).arg( err );
		break;
	case LC_NAT_OK:
		if ( ip.isEmpty() || ip == "0.0.0.0" )
			return;					/* no answer yet: wait for another callback */
		{
			struct in_addr a;
			if ( inet_pton( AF_INET, B( ip ).constData(), &a ) == 1 && lc_ipv4_private( ntohl( a.s_addr ) ) ) {
				why = QString::fromUtf8( "Your router opened the port, but its own internet address (%1) is private: your provider or another router puts it "
					"behind a second NAT, which nobody outside can get through." ).arg( ip );
				break;
			}
		}
		m_natState = NatMapped;
		m_natPublic = QString( "%1:%2" ).arg( ip ).arg( extPort ? extPort : coopPort() );
		m_natTimer->stop();
		log( QString( "Co-op: the router forwards %1 to this %2" ).arg( coopPort() ).arg( nounLc() ) );
		showInternet();
		return;
	default:
		return;
	}
	m_natState = NatFailed;
	m_natReason = why;
	m_natTimer->stop();
	showInternet();
}

void CoopDialog::releaseNat()
{
	m_natTimer->stop();
	if ( m_nat ) {
		lc_nat_release( m_nat );
		m_nat = nullptr;
	}
	m_natState = NatIdle;
	m_natPublic.clear();
}

void CoopDialog::copyInvite()
{
	const QString link = inviteLink();
	if ( link.isEmpty() )
		return;
	QGuiApplication::clipboard()->setText( link );
	m_internetStatus->setText( QString::fromUtf8( "Copied %1. Your friend opens it (Elite Force with co-op) to check your game and join." ).arg( link ) );
	setColor( m_internetStatus, secondary() );
	log( "Co-op: invite " + link );
}

/* ---- Check ------------------------------------------------------------------------------------------------------ */

void CoopDialog::showResult( const QString &icon, const QColor &color, const QString &title, const QString &text )
{
	m_resultWidget->show();
	m_spinner->hide();
	m_resultIcon->show();
	m_resultIcon->setPixmap( iconPixmap( icon, color, 20 ) );
	m_resultTitle->setText( title );
	m_resultText->setText( text );
	m_resultText->setVisible( !text.isEmpty() );
}

void CoopDialog::updateConnect()
{
	const bool ok = m_status == "ok" && m_canPlay;
	m_connect->setEnabled( ok );
	m_connect->setText( ok && !m_warning.isEmpty() ? "Join Anyway" : "Connect" );
	m_connect->setToolTip( ok ? QString( "Join %1 as player 2" ).arg( m_foundAddress ) : QString( "Check the address first" ) );
}

void CoopDialog::doCheck()
{
	if ( m_checking )
		return;
	const QString addr = m_address->text().trimmed();
	m_checking = true;
	m_check->setEnabled( false );
	m_status.clear();
	updateConnect();
	m_resultWidget->show();
	m_resultIcon->hide();
	m_spinner->show();
	m_resultTitle->setText( addr.isEmpty() ? QString::fromUtf8( "Checking the address\xe2\x80\xa6" ) : QString::fromUtf8( "Checking %1\xe2\x80\xa6" ).arg( addr ) );
	m_resultText->hide();
	auto *q = new lc_coop_query;
	memset( q, 0, sizeof *q );
	const QByteArray ab = B( addr );
	const QByteArray df = B( m_dataFolder );
	async( this, [q, ab, df]() {
		lc_coop_query_run( ab.constData(), 2.0, q );
		lc_coop_check_paks( q, df.isEmpty() ? nullptr : df.constData() );		/* only after an "ok" */
	}, [this, q, addr]() {
		m_checking = false;
		m_check->setEnabled( true );
		const QString status = U( q->status );
		const QString address = U( q->address );
		char host[256] = "", needPw[32] = "";
		lc_coop_info_value( q->info, "hostname", host, sizeof host );
		lc_coop_info_value( q->info, "needPassword", needPw, sizeof needPw );
		char desc[2048] = "";
		lc_coop_describe( q->info, desc, sizeof desc );
		const QString hostname = U( host );
		const QString on = hostname.isEmpty() ? QString() : QString::fromUtf8( " on \xe2\x80\x9c%1\xe2\x80\x9d" ).arg( hostname );
		QString shown = status;
		const QString more = desc[0] ? "\n\n" + U( desc ) : QString();
		m_warning = U( q->warning );
		if ( status == "ok" ) {
			m_status = "ok";
			m_foundAddress = address;
			m_foundHostname = hostname;
			m_needsPassword = !strcmp( needPw, "1" );
			if ( !m_warning.isEmpty() )
				showResult( "exclamationmark.triangle.fill", yellow(),
					QString( "Co-op game found%1 (%2), but the Expansion Pack differs" ).arg( on, address ), m_warning + more );
			else
				showResult( "check.circle.fill", green(), QString( "Co-op game found%1 (%2)" ).arg( on, address ), U( desc ) );
		} else if ( status == "paks" ) {
			m_status = "paks";
			showResult( "xmark.circle.fill", red(),
				QString::fromUtf8( "Your game files don\xe2\x80\x99t match the host\xe2\x80\x99s%1 (%2)" ).arg( on, address ), U( q->error ) + more );
		} else if ( status == "mismatch" ) {
			m_status = "mismatch";
			showResult( "exclamationmark.triangle.fill", orange(),
				QString::fromUtf8( "Version mismatch: the co-op game%1 (%2) can\xe2\x80\x99t be joined from this %3" ).arg( on, address, nounLc() ),
				U( q->error ) + more );
		} else if ( status == "noanswer" || status == "error" ) {
			m_status = "none";
			showResult( "xmark.circle.fill", secondary(), QString( "No co-op game found at %1" ).arg( address.isEmpty() ? addr : address ),
				q->error[0] ? U( q->error ) : QString( "Nothing answered within 2 seconds. Check the address, that the host pressed Host and is running co-op, "
					"and that both computers are on the same network." ) );
			shown = "none";
		} else {
			m_status = "bad";
			showResult( "exclamationmark.circle.fill", red(), QString::fromUtf8( "This address can\xe2\x80\x99t be used" ), U( q->error ) );
			shown = "bad";
		}
		log( QString( "Co-op: check %1 -> %2" ).arg( addr, shown ) );
		if ( !testEnv( "VOYAGERSP_TEST_COOP_QUERY" ).isEmpty() )
			log( QString( "[test] co-op check %1: %2" ).arg( addr, shown ) );
		updateConnect();
		delete q;
	} );
}

/* ---- Host / Connect ----------------------------------------------------------------------------------------------- */

void CoopDialog::doHost()
{
	if ( !m_canPlay )
		return;
	lc_store *st = lc_store_shared();
	char name[64] = "";
	lc_coop_safe_name( B( m_name->text() ).constData(), name, sizeof name );
	lc_store_set_int( st, LC_KEY_COOP_BACKGROUND, m_background->isChecked() ? 1 : 0 );
	lc_store_set( st, LC_KEY_COOP_NAME, name[0] ? name : nullptr );
	lc_store_save( st );
	m_playName = U( name );
	lc_coop_choose_host( m_background->isChecked(), name, B( m_hostPw->text() ).constData() );
	log( QString( "Co-op: hosting (single player with sp_coop 1) as %1%2" ).arg( name[0] ? U( name ) : U( lc_computer_name() ),
		m_hostPw->text().isEmpty() ? QString() : QString( ", with a password" ) ) );
	m_choice = Host;
	accept();
}

void CoopDialog::doConnect()
{
	if ( m_status != "ok" || !m_canPlay )
		return;
	if ( m_needsPassword && m_joinPw->text().isEmpty() ) {
		showResult( "lock.fill", orange(), "This game needs a password", "Type the password the host gave you, then press Connect." );
		m_joinPw->setFocus();
		return;
	}
	releaseNat();
	lc_store *st = lc_store_shared();
	char name[64] = "";
	lc_coop_safe_name( B( m_name->text() ).constData(), name, sizeof name );
	const char *sex = m_munro->current() == 1 ? "female" : "male";
	lc_store_set( st, LC_KEY_COOP_NAME, name[0] ? name : nullptr );
	const QByteArray look = B( m_look->currentData().toString() );
	lc_store_set( st, LC_KEY_COOP_MUNRO, sex );
	lc_store_set( st, LC_KEY_COOP_LOOK, look.isEmpty() ? nullptr : look.constData() );
	lc_store_save( st );
	lc_coop_choose_join( B( m_foundAddress ).constData(), sex, look.constData(), name, B( m_joinPw->text() ).constData() );
	log( QString( "Co-op: joining %1 as player 2 (single player with coop_connect), %2, %3 Munro%4" )
		.arg( m_foundAddress, name[0] ? U( name ) : U( lc_computer_name() ), U( sex ),
			look.isEmpty() ? QString() : QString( " as %1" ).arg( U( look.constData() ) ) ) );
	m_choice = Connect;
	accept();
}

void CoopDialog::reject()
{
	releaseNat();
	lc_coop_choose_none();
	QDialog::reject();
}

/* ---- test hooks ------------------------------------------------------------------------------------------------------- */

void CoopDialog::runTestHooks()
{
	const QString q = testEnv( "VOYAGERSP_TEST_COOP_QUERY" );
	if ( !q.isEmpty() ) {
		m_address->setText( q );
		QTimer::singleShot( 100, this, &CoopDialog::doCheck );
	}
	if ( testSet( "VOYAGERSP_TEST_COOP_HOST" ) )
		QTimer::singleShot( 300, this, &CoopDialog::doHost );
	if ( testSet( "VOYAGERSP_TEST_COOP_INTERNET" ) ) {
		QTimer::singleShot( 100, this, [this]() { m_router->setChecked( true ); } );
		auto *t = new QElapsedTimer;
		t->start();
		auto *poll = new QTimer( this );
		connect( poll, &QTimer::timeout, this, [this, poll, t]() {
			if ( m_natState == NatAsking && t->elapsed() < 20000 )
				return;
			poll->stop();
			log( QString( "[test] co-op internet: %1 %2" ).arg( m_natState == NatMapped ? "mapped" : m_natState == NatFailed ? "failed" : "asking",
				m_natState == NatMapped ? m_natPublic : m_natReason ) );
			log( "[test] co-op invite: " + inviteLink() );
		} );
		poll->start( 200 );
	}
}
