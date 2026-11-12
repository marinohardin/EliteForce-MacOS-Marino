/*
===========================================================================
ef-launcher (Qt 6): the Holomatch Servers sheet ("Browse…"). servers.md §8.
Map Info / 3D Map Preview open efservers.com in the browser instead of an
embedded web view (no QtWebEngine: a deliberate, documented parity gap).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

#include <QMutex>
#include <QSet>

using namespace Ui;

namespace {

const char *kDot = " \xc2\xb7 ";

/* Draw a Quake 3 coloured string (servers.md §7.9) into r, truncating at the tail. */
void paintQ3( QPainter *p, const QRect &r, const QString &raw, const QFont &f, const QColor &normal, Qt::Alignment al,
	const QString &suffix = QString() )
{
	const QByteArray in = raw.toUtf8();
	QByteArray out( in.size() + 1, 0 );
	QVector<unsigned char> idx( in.size() + 1, 7 );
	lc_decode_colors( in.constData(), out.data(), size_t( out.size() ), idx.data() );
	const QString plain = QString::fromUtf8( out.constData() ).trimmed();
	/* colours index the UTF-8 bytes; names are ASCII in practice, map by byte offset */
	const QByteArray plainBytes = QByteArray( out.constData() );
	int lead = 0;
	while ( lead < plainBytes.size() && isspace( (unsigned char)plainBytes[lead] ) )
		++lead;
	p->save();
	p->setFont( f );
	const QFontMetrics fm( f );
	QString shown = plain;
	int total = fm.horizontalAdvance( shown ) + ( suffix.isEmpty() ? 0 : fm.horizontalAdvance( suffix ) );
	int x = r.left();
	if ( al & Qt::AlignRight )
		x = r.right() - total;
	const int y = r.center().y() + ( fm.ascent() - fm.descent() ) / 2;
	p->setClipRect( r );
	int bytePos = lead;
	for ( int i = 0; i < shown.size(); ++i ) {
		const QChar ch = shown[i];
		const int ci = bytePos < idx.size() ? idx[bytePos] : 7;
		p->setPen( ci == 7 ? normal : q3Color( ci ) );
		p->drawText( x, y, QString( ch ) );
		x += fm.horizontalAdvance( ch );
		bytePos += QString( ch ).toUtf8().size();
	}
	if ( !suffix.isEmpty() ) {
		p->setPen( normal );
		p->drawText( x, y, suffix );
	}
	p->restore();
}

QString plainName( const QString &raw )
{
	const QByteArray in = raw.toUtf8();
	QByteArray out( in.size() + 1, 0 );
	lc_decode_colors( in.constData(), out.data(), size_t( out.size() ), nullptr );
	return QString::fromUtf8( out.constData() ).trimmed();
}

QString cmodText( bool isCmod, const char *version, QString *tip )
{
	if ( !isCmod )
		return QString();
	char num[64] = "";
	const bool has = lc_cmod_number( version, num, sizeof num ) != 0;
	if ( tip ) {
		QString v = U( version );
		if ( v.startsWith( "cMod ", Qt::CaseInsensitive ) )
			v = v.mid( 5 );
		*tip = v.isEmpty() ? QString( "cMod (the server doesn't say which version)" ) : "cMod " + v;
	}
	return has ? U( num ) : QString::fromUtf8( "\xe2\x9c\x93" );
}

QMutex g_liveLock;
QSet<void *> g_live;

}

/* ---- the table model ---------------------------------------------------------------------------------------------- */

class ServerModel : public QAbstractTableModel {
public:
	QVector<lc_browser_row> rows;
	std::function<QString( const lc_browser_row & )> cmodOf;
	std::function<int( const lc_browser_row & )> mapState;
	std::function<QString( const QString & )> mapTip;
	QSet<QString> *haveMaps = nullptr;
	int rowCount( const QModelIndex & = QModelIndex() ) const override { return int( rows.size() ); }
	int columnCount( const QModelIndex & = QModelIndex() ) const override { return 9; }
	QVariant headerData( int s, Qt::Orientation o, int role ) const override
	{
		if ( o != Qt::Horizontal )
			return QVariant();
		static const char *titles[9] = { "", "Server", "", "Map", "Players", "Humans", "Game", "cMod", "Ping" };
		static const char *tips[9] = { "Favorites", "", "Needs a password", "", "Players on the server, bots included, and its maximum",
			"People playing, without bots (? if the server doesn't say)", "", "Servers running cMod (by Noah Metzger), with its version", "" };
		if ( role == Qt::DisplayRole )
			return QString::fromUtf8( titles[s] );
		if ( role == Qt::ToolTipRole )
			return QString::fromUtf8( tips[s] );
		if ( role == Qt::TextAlignmentRole && ( s == 4 || s == 5 || s == 8 ) )
			return int( Qt::AlignRight | Qt::AlignVCenter );
		if ( role == Qt::DecorationRole ) {
			if ( s == 0 )
				return icon( "star", QColor(), 10 );
			if ( s == 2 )
				return icon( "lock.fill", QColor(), 10 );
		}
		return QVariant();
	}
	QVariant data( const QModelIndex &ix, int role ) const override
	{
		if ( ix.row() < 0 || ix.row() >= rows.size() )
			return QVariant();
		const lc_browser_row &r = rows[ix.row()];
		const lc_server_info &i = r.info;
		const int c = ix.column();
		if ( role == Qt::UserRole )
			return U( i.address );
		if ( role == Qt::TextAlignmentRole ) {
			if ( c == 4 || c == 5 || c == 8 )
				return int( Qt::AlignRight | Qt::AlignVCenter );
			if ( c == 7 )
				return int( Qt::AlignHCenter | Qt::AlignVCenter );
			return int( Qt::AlignLeft | Qt::AlignVCenter );
		}
		if ( role == Qt::ToolTipRole ) {
			switch ( c ) {
			case 0:
				return r.favorite ? "Remove from favorites" : "Add to favorites";
			case 1: {
				QString t = r.thisComputer ? QString( "%1, hosted on this %2 (Loopback Host Fix)" ).arg( U( i.address ), nounLc() ) : U( i.address );
				if ( r.listedOnCount > 0 ) {
					QStringList l;
					for ( int k = 0; k < r.listedOnCount; ++k )
						l << U( r.listedOn[k] );
					t += "\nListed on " + l.join( ", " );
				}
				return t;
			}
			case 2:
				return i.needPass ? QVariant( "Needs a password" ) : QVariant();
			case 3:
				return mapTip ? mapTip( U( i.map ) ) : QString();
			case 4:
				if ( i.humans >= 0 && i.players > i.humans ) {
					const int b = i.players - i.humans;
					return QString( "%1 %2, %3 %4" ).arg( i.humans ).arg( i.humans == 1 ? "person" : "people" ).arg( b ).arg( b == 1 ? "bot" : "bots" );
				}
				return QVariant();
			case 5:
				return i.humans < 0 ? QVariant( "This server doesn't say how many of its players are bots" ) : QVariant();
			case 7: {
				QString tip;
				cmodText( r.isCmod, r.cmodVersion, &tip );
				return r.isCmod ? QVariant( tip ) : QVariant();
			}
			default:
				return QVariant();
			}
		}
		if ( role == Qt::ForegroundRole ) {
			switch ( c ) {
			case 3: {
				const int st = mapState ? mapState( r ) : 0;
				if ( st == 2 )
					return purple();
				if ( st == 1 )
					return amber();
				return QVariant();
			}
			case 4:
				return i.players == 0 ? QVariant( secondary() ) : QVariant();
			case 5:
				return i.humans <= 0 ? QVariant( secondary() ) : QVariant();
			case 8:
				return i.ping < 80 ? QVariant( green() ) : i.ping >= 160 ? QVariant( orange() ) : QVariant();
			default:
				return QVariant();
			}
		}
		if ( role != Qt::DisplayRole )
			return QVariant();
		switch ( c ) {
		case 3:
			return U( i.map );
		case 4:
			return i.maxPlayers > 0 ? QString( "%1/%2" ).arg( i.players ).arg( i.maxPlayers ) : QString::number( i.players );
		case 5:
			return i.humans < 0 ? QString( "?" ) : QString::number( i.humans );
		case 6: {
			char buf[32];
			return U( lc_browser_gametype_name( i.gametype, buf, sizeof buf ) );
		}
		case 7: {
			QString t = cmodOf ? cmodOf( r ) : QString();
			return t;
		}
		case 8:
			return QString::number( i.ping );
		default:
			return QVariant();
		}
	}
	void setRows( const lc_browser_row *p, int n )
	{
		beginResetModel();
		rows.clear();
		for ( int i = 0; i < n; ++i )
			rows << p[i];
		endResetModel();
	}
};

class ServerDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint( QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &ix ) const override
	{
		const ServerModel *m = static_cast<const ServerModel *>( ix.model() );
		const lc_browser_row &r = m->rows[ix.row()];
		const int c = ix.column();
		if ( c != 0 && c != 1 && c != 2 ) {
			QStyledItemDelegate::paint( p, opt, ix );
			return;
		}
		QStyleOptionViewItem o = opt;
		o.text.clear();
		o.state &= ~QStyle::State_HasFocus;
		QApplication::style()->drawControl( QStyle::CE_ItemViewItem, &o, p, opt.widget );
		const bool sel = opt.state & QStyle::State_Selected;
		const QColor txt = sel ? opt.palette.color( QPalette::HighlightedText ) : opt.palette.color( QPalette::Text );
		if ( c == 0 ) {
			const QPixmap pm = iconPixmap( r.favorite ? "star.fill" : "star", r.favorite ? yellow() : ( sel ? txt : tertiary() ), 10 );
			p->drawPixmap( opt.rect.center() - QPoint( px( 5 ), px( 5 ) ), pm );
		} else if ( c == 2 ) {
			if ( r.info.needPass ) {
				const QPixmap pm = iconPixmap( "lock.fill", sel ? txt : secondary(), 10 );
				p->drawPixmap( opt.rect.center() - QPoint( px( 5 ), px( 5 ) ), pm );
			}
		} else {
			QString name = U( r.info.hostname );
			if ( name.isEmpty() )
				name = U( r.info.name );
			paintQ3( p, opt.rect.adjusted( px( 3 ), 0, -px( 3 ), 0 ), name, font( 13 ), txt, Qt::AlignLeft,
				r.thisComputer ? QString( " (this %1)" ).arg( nounLc() ) : QString() );
		}
	}
	QSize sizeHint( const QStyleOptionViewItem &o, const QModelIndex &i ) const override
	{
		QSize s = QStyledItemDelegate::sizeHint( o, i );
		s.setHeight( px( 22 ) );
		return s;
	}
};

class PlayerDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint( QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &ix ) const override
	{
		if ( ix.column() != 0 ) {
			QStyleOptionViewItem o = opt;
			o.state &= ~( QStyle::State_Selected | QStyle::State_HasFocus );
			o.font = font( 11 );
			if ( ix.column() == 2 && ix.data( Qt::UserRole + 1 ).toBool() )
				o.palette.setColor( QPalette::Text, tertiary() );
			if ( ix.column() == 3 )
				o.palette.setColor( QPalette::Text, secondary() );
			QStyledItemDelegate::paint( p, o, ix );
			return;
		}
		QStyleOptionViewItem o = opt;
		o.text.clear();
		o.state &= ~( QStyle::State_Selected | QStyle::State_HasFocus );
		QApplication::style()->drawControl( QStyle::CE_ItemViewItem, &o, p, opt.widget );
		paintQ3( p, opt.rect.adjusted( px( 2 ), 0, -px( 2 ), 0 ), ix.data( Qt::UserRole ).toString(), font( 11 ),
			opt.palette.color( QPalette::Text ), Qt::AlignLeft );
	}
	QSize sizeHint( const QStyleOptionViewItem &o, const QModelIndex &i ) const override
	{
		QSize s = QStyledItemDelegate::sizeHint( o, i );
		s.setHeight( px( 18 ) );
		return s;
	}
};

/* ---- the dialog ------------------------------------------------------------------------------------------------------ */

struct BrowserDialog::Impl {
	BrowserDialog *q;
	QString dataFolder;
	char masters[LC_MAX_MASTERS][65];
	int masterCount = 0;
	const lc_map_catalog *catalog = nullptr;
	lc_browser *b = nullptr;
	QSet<QString> haveMaps, builtinMaps;
	QSet<QString> favorites;
	int source = 1;
	QHash<int, bool> fetched;
	QVector<lc_sort_key> keys;
	QHash<QString, lc_server_status> statuses;
	QString asking;
	QHash<QString, QString> views3D;			/* map -> url, or "none" */
	QSet<QString> checking3D;
	QHash<QString, QString> cmodOverride;		/* address -> version */
	QString selectedAddress;
	QString address, password;
	bool reloadPending = false;

	Segmented *tabs;
	QProgressBar *spinner;
	QLabel *statusLabel;
	QCheckBox *onlyFav, *onlyPeople;
	QTableView *table;
	ServerModel *model;
	QLabel *playersTitle, *playersNote;
	QToolButton *playersRefresh;
	QPushButton *mapInfo, *map3D;
	QTableWidget *players;
	QWidget *linkRow;
	QLabel *linkLabel;
	QLineEdit *addr;
	QPushButton *connectBtn, *joinBtn;
	QTimer reloadTimer;

	void build();
	void show_();
	void fetch( int src );
	void showPlayers();
	void show3D();
	void toggleFav( const QString &a );
	QString selected() const;
	int mapStateOf( const QString &map ) const;
	QString sourceLabel( int src ) const;
	void sortBy( int column );
	void finish( const QString &a, const QString &pw );
	void joinRowImpl( int row );
	bool busyNow() const;
	QString statusOf( int src ) const { return U( lc_browser_status_text( b, src ) ); }
	bool fetching( int src ) const
	{
		const QString t = statusOf( src );
		return t.startsWith( "Asking" ) || t.endsWith( QString::fromUtf8( "asking each\xe2\x80\xa6" ) ) || t.startsWith( "Looking on" );
	}
	void refreshLinkRow();
};

QString BrowserDialog::Impl::sourceLabel( int src ) const
{
	if ( src == 0 )
		return "Local";
	if ( src == 1 )
		return "Combined";
	return masterCount > 7 ? QString::number( src - 1 ) : QString( "Internet %1" ).arg( src - 1 );
}

int BrowserDialog::Impl::mapStateOf( const QString &map ) const
{
	/* 0 have, 1 missing, 2 Expansion Pack not installed */
	if ( map.isEmpty() )
		return 0;
	const QString l = map.toLower();
	if ( haveMaps.contains( l ) )
		return 0;
	if ( lc_is_expansion_map( B( l ).constData() ) )
		return 2;
	return 1;
}

BrowserDialog::BrowserDialog( QWidget *parent, const QString &dataFolder ) : QDialog( parent ), d( new Impl )
{
	d->q = this;
	d->dataFolder = dataFolder;
	setWindowTitle( "Holomatch Servers" );
	setModal( true );
	{
		QMutexLocker l( &g_liveLock );
		g_live.insert( d );
	}
	d->masterCount = lc_masters_load( lc_store_shared(), d->masters );
	d->catalog = lc_map_catalog_get( B( dataFolder ).constData(), lc_homebase() );
	if ( d->catalog ) {
		for ( int i = 0; i < lc_map_catalog_count( d->catalog ); ++i ) {
			const lc_map_info *m = lc_map_catalog_at( d->catalog, i );
			d->haveMaps.insert( U( m->name ).toLower() );
			if ( m->retail || m->expansion )
				d->builtinMaps.insert( U( m->name ).toLower() );
		}
	}
	for ( int i = 0; i < 64; ++i ) {
		/* the Expansion Pack list: efservers.com has no page for these either */
	}
	{
		char items[256][64];
		const int n = lc_store_get_list( lc_store_shared(), "browserfavorites", &items[0][0], 64, 256 );
		for ( int i = 0; i < n; ++i )
			d->favorites.insert( U( items[i] ) );
	}
	d->b = lc_browser_new( d->masters, d->masterCount, d->catalog );
	d->keys << lc_sort_key{ LC_COL_FAV, 0 } << lc_sort_key{ LC_COL_HUMANS, 0 } << lc_sort_key{ LC_COL_PLAYERS, 0 } << lc_sort_key{ LC_COL_PING, 1 };
	d->build();
	d->source = d->masterCount > 0 ? 1 : 0;
	d->tabs->setCurrent( d->source );
	d->fetch( d->source );
}

BrowserDialog::~BrowserDialog()
{
	{
		QMutexLocker l( &g_liveLock );
		g_live.remove( d );
	}
	lc_browser_cancel_all( d->b );
	delete d;
}

QString BrowserDialog::address() const { return d->address; }
QString BrowserDialog::password() const { return d->password; }

void BrowserDialog::Impl::build()
{
	q->setFixedWidth( px( 780 ) );
	QVBoxLayout *root = new QVBoxLayout( q );
	root->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 20 ) );
	root->setSpacing( px( 8 ) );

	/* top row */
	QHBoxLayout *top = new QHBoxLayout;
	QStringList titles;
	titles << "Local";
	if ( masterCount > 0 ) {
		titles << "Combined";
		for ( int i = 0; i < masterCount; ++i )
			titles << sourceLabel( 2 + i );
	}
	tabs = new Segmented( titles );
	tabs->setToolTip( "Local: this network. Combined: every master server's list. Internet 1, 2\xe2\x80\xa6: one master server each, in the order set in Servers\xe2\x80\xa6" );
	tabs->setSegmentToolTip( 0, "Your network" );
	if ( masterCount > 0 )
		tabs->setSegmentToolTip( 1, "Every master server's list, together" );
	for ( int i = 0; i < masterCount; ++i )
		tabs->setSegmentToolTip( 2 + i, U( masters[i] ) );
	tabs->setMaximumWidth( px( 120 * qMin( 8, int( titles.size() ) ) ) );
	top->addWidget( tabs );
	top->addStretch( 1 );
	QToolButton *globe = iconButton( "globe", "Open efservers.com: who's playing where, server and map history" );
	globe->setAccessibleName( "Open efservers.com" );
	QToolButton *refresh = iconButton( "arrow.clockwise", "Ask again" );
	refresh->setAccessibleName( "Refresh" );
	refresh->setShortcut( QKeySequence( "Ctrl+R" ) );
	top->addWidget( globe );
	top->addWidget( refresh );
	root->addLayout( top );

	/* status row */
	QHBoxLayout *sr = new QHBoxLayout;
	spinner = new QProgressBar;
	spinner->setRange( 0, 0 );
	spinner->setTextVisible( false );
	spinner->setFixedSize( px( 28 ), px( 8 ) );
	spinner->hide();
	sr->addWidget( spinner );
	statusLabel = label( QString(), 11, QFont::Normal, secondary() );
	statusLabel->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	sr->addWidget( statusLabel, 1 );
	onlyFav = new QCheckBox( "Only favorites" );
	onlyFav->setToolTip( "Star servers in the list to make them favorites" );
	onlyFav->setChecked( lc_store_get_bool( lc_store_shared(), "browseronlyfavorites", 0 ) != 0 );
	onlyPeople = new QCheckBox( "Only servers with people" );
	onlyPeople->setToolTip( "Hide servers with only bots, or no one. Servers that don't say (?) stay when they have players" );
	onlyPeople->setChecked( lc_store_get_bool( lc_store_shared(), "browseronlypeople", 0 ) != 0 );
	sr->addWidget( onlyFav );
	sr->addSpacing( px( 14 ) );
	sr->addWidget( onlyPeople );
	root->addLayout( sr );

	/* server table */
	model = new ServerModel;
	model->cmodOf = [this]( const lc_browser_row &r ) {
		QString v = r.cmodVersion;
		bool is = r.isCmod != 0;
		const QString a = U( r.info.address );
		if ( !is && cmodOverride.contains( a ) ) {
			is = true;
			v = cmodOverride[a];
		}
		return cmodText( is, B( v ).constData(), nullptr );
	};
	model->mapState = [this]( const lc_browser_row &r ) { return mapStateOf( U( r.info.map ) ); };
	model->mapTip = [this]( const QString &m ) -> QString {
		const int st = mapStateOf( m );
		if ( st == 2 )
			return QString( "%1 is an Expansion Pack map, and the Expansion Pack isn't in your game files. Servers can't send it: add the "
				"Expansion Pack's files with Add Game Files\xe2\x80\xa6 in the launcher to join." ).arg( m );
		if ( st == 1 )
			return QString( "%1 isn't in your game files. It downloads when you join, if the server allows downloads." ).arg( m );
		return QString();
	};
	table = new QTableView;
	table->setModel( model );
	table->setItemDelegate( new ServerDelegate( table ) );
	styleView( table );
	table->setSelectionBehavior( QAbstractItemView::SelectRows );
	table->setSelectionMode( QAbstractItemView::SingleSelection );
	table->setShowGrid( false );
	table->verticalHeader()->hide();
	table->verticalHeader()->setDefaultSectionSize( px( 22 ) );
	table->setEditTriggers( QAbstractItemView::NoEditTriggers );
	table->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
	QHeaderView *hh = table->horizontalHeader();
	hh->setSectionsClickable( true );
	hh->setSortIndicatorShown( true );
	hh->setHighlightSections( false );
	hh->setStretchLastSection( false );
	const int widths[9] = { 18, 162, 16, 110, 74, 68, 120, 40, 40 };
	for ( int c = 0; c < 9; ++c ) {
		hh->setSectionResizeMode( c, c == 1 ? QHeaderView::Stretch : ( c == 0 || c == 2 ) ? QHeaderView::Fixed : QHeaderView::Interactive );
		if ( c != 1 )
			table->setColumnWidth( c, px( widths[c] ) );
	}
	Panel *tbox = new Panel;
	QVBoxLayout *tv = new QVBoxLayout( tbox );
	tv->setContentsMargins( 2, 2, 2, 2 );
	tv->addWidget( table );
	tbox->setFixedHeight( px( 280 ) );
	root->addWidget( tbox );
	root->addSpacing( px( 4 ) );

	/* players header */
	QHBoxLayout *ph = new QHBoxLayout;
	playersTitle = label( "Players", 12, QFont::DemiBold );
	playersNote = label( QString(), 11, QFont::Normal, secondary() );
	playersRefresh = iconButton( "arrow.clockwise", "Ask the server again", 10 );
	playersRefresh->setAccessibleName( "Ask again who's playing" );
	playersRefresh->setAutoRaise( true );
	playersRefresh->hide();
	mapInfo = button( "Map Info" );
	map3D = button( "3D Map Preview" );
	mapInfo->hide();
	map3D->hide();
	ph->addWidget( playersTitle );
	ph->addWidget( playersNote );
	ph->addWidget( playersRefresh );
	ph->addStretch( 1 );
	ph->addWidget( mapInfo );
	ph->addWidget( map3D );
	root->addLayout( ph );

	players = new QTableWidget( 0, 4 );
	styleView( players );
	players->setItemDelegate( new PlayerDelegate( players ) );
	players->setHorizontalHeaderLabels( QStringList() << "Name" << "Score" << "Ping" << "" );
	players->verticalHeader()->hide();
	players->verticalHeader()->setDefaultSectionSize( px( 18 ) );
	players->setSelectionMode( QAbstractItemView::NoSelection );
	players->setFocusPolicy( Qt::NoFocus );
	players->setShowGrid( false );
	players->setEditTriggers( QAbstractItemView::NoEditTriggers );
	QHeaderView *ph2 = players->horizontalHeader();
	ph2->setSectionResizeMode( 0, QHeaderView::Stretch );
	ph2->setSectionResizeMode( 1, QHeaderView::Fixed );
	ph2->setSectionResizeMode( 2, QHeaderView::Fixed );
	ph2->setSectionResizeMode( 3, QHeaderView::Fixed );
	players->setColumnWidth( 1, px( 50 ) );
	players->setColumnWidth( 2, px( 44 ) );
	players->setColumnWidth( 3, px( 34 ) );
	Panel *pbox = new Panel;
	QVBoxLayout *pv = new QVBoxLayout( pbox );
	pv->setContentsMargins( 2, 2, 2, 2 );
	pv->addWidget( players );
	pbox->setFixedHeight( px( 112 ) );
	root->addWidget( pbox );
	root->addSpacing( px( 10 ) );

	/* link row */
	linkRow = new QWidget;
	QHBoxLayout *lr = new QHBoxLayout( linkRow );
	lr->setContentsMargins( 0, 0, 0, 0 );
	linkLabel = label( QString(), 11, QFont::Normal, secondary() );
	lr->addWidget( linkLabel, 1 );
	QPushButton *linkBtn = button( "Open Join Links with Elite Force" );
	lr->addWidget( linkBtn );
	root->addWidget( linkRow );
	refreshLinkRow();
	QObject::connect( linkBtn, &QPushButton::clicked, q, [this]() {
		char err[256] = "";
		const int ok = lc_link_register( err, sizeof err );
		log( QString( "Browser: open stvef:// links with this app: %1" ).arg( ok ? QString( "done" ) : U( err ) ) );
		refreshLinkRow();
	} );

	/* footer */
	QHBoxLayout *foot = new QHBoxLayout;
	addr = new QLineEdit;
	addr->setFixedWidth( px( 260 ) );
	addr->setPlaceholderText( "Or an address, like 203.0.113.5:27960" );
	connectBtn = new QPushButton( "Connect" );
	connectBtn->setEnabled( false );
	QPushButton *cancel = new QPushButton( "Cancel" );
	joinBtn = new QPushButton( "Join" );
	cancel->setMinimumWidth( px( 80 ) );
	joinBtn->setMinimumWidth( px( 80 ) );
	joinBtn->setDefault( true );
	joinBtn->setEnabled( false );
	foot->addWidget( addr );
	foot->addWidget( connectBtn );
	foot->addStretch( 1 );
	foot->addWidget( cancel );
	foot->addWidget( joinBtn );
	root->addLayout( foot );

	reloadTimer.setSingleShot( true );
	reloadTimer.setInterval( 100 );
	QObject::connect( &reloadTimer, &QTimer::timeout, q, [this]() { reloadPending = false; show_(); } );
	QObject::connect( tabs, &Segmented::currentChanged, q, [this]( int s ) {
		source = s;
		if ( !fetched.value( s ) )
			fetch( s );
		else
			show_();
	} );
	QObject::connect( refresh, &QToolButton::clicked, q, [this]() { fetch( source ); } );
	QObject::connect( globe, &QToolButton::clicked, q, []() { lc_open_url( "https://efservers.com/" ); } );
	QObject::connect( onlyFav, &QCheckBox::toggled, q, [this]( bool on ) {
		lc_store_set( lc_store_shared(), "browseronlyfavorites", on ? "1" : "0" );
		lc_store_save( lc_store_shared() );
		show_();
	} );
	QObject::connect( onlyPeople, &QCheckBox::toggled, q, [this]( bool on ) {
		lc_store_set( lc_store_shared(), "browseronlypeople", on ? "1" : "0" );
		lc_store_save( lc_store_shared() );
		show_();
	} );
	QObject::connect( hh, &QHeaderView::sectionClicked, q, [this]( int c ) { if ( c != 1 || true ) sortBy( c ); } );
	QObject::connect( table, &QTableView::clicked, q, [this]( const QModelIndex &ix ) {
		if ( ix.column() == 0 )
			toggleFav( ix.data( Qt::UserRole ).toString() );
	} );
	QObject::connect( table->selectionModel(), &QItemSelectionModel::selectionChanged, q, [this]() {
		selectedAddress = selected();
		joinBtn->setEnabled( !selectedAddress.isEmpty() );
		showPlayers();
	} );
	QObject::connect( table, &QTableView::doubleClicked, q, [this]( const QModelIndex &ix ) {
		if ( ix.column() != 0 )
			joinRowImpl( ix.row() );
	} );
	QObject::connect( addr, &QLineEdit::textChanged, q, [this]( const QString &t ) { connectBtn->setEnabled( !t.trimmed().isEmpty() ); } );
	QObject::connect( connectBtn, &QPushButton::clicked, q, [this]() {
		const QString a = addr->text().trimmed();
		static const QRegularExpression ok( "^[A-Za-z0-9.:\\[\\]_-]+$" );
		if ( a.isEmpty() || !ok.match( a ).hasMatch() ) {
			QApplication::beep();
			return;
		}
		finish( a, QString() );
	} );
	QObject::connect( cancel, &QPushButton::clicked, q, &QDialog::reject );
	QObject::connect( joinBtn, &QPushButton::clicked, q, [this]() {
		/* Return in the address field means Connect when it has text, as on macOS */
		if ( addr->hasFocus() && connectBtn->isEnabled() ) {
			connectBtn->click();
			return;
		}
		const QModelIndex ix = table->currentIndex();
		if ( ix.isValid() )
			joinRowImpl( ix.row() );
	} );
	QObject::connect( playersRefresh, &QToolButton::clicked, q, [this]() {
		statuses.remove( selectedAddress );
		showPlayers();
	} );
	QObject::connect( mapInfo, &QPushButton::clicked, q, [this]() {
		const QModelIndex ix = table->currentIndex();
		if ( !ix.isValid() )
			return;
		char url[512];
		lc_efservers_info_url( B( U( model->rows[ix.row()].info.map ) ).constData(), url, sizeof url );
		log( QString( "Browser: map info %1" ).arg( U( url ) ) );
		lc_open_url( url );
	} );
	QObject::connect( map3D, &QPushButton::clicked, q, [this]() {
		const QModelIndex ix = table->currentIndex();
		if ( !ix.isValid() )
			return;
		const QString map = U( model->rows[ix.row()].info.map );
		const QString u = views3D.value( map );
		if ( !u.isEmpty() && u != "none" ) {
			log( "Browser: map preview " + u );
			lc_open_url( B( u ).constData() );
		}
	} );
	QObject::connect( q, &QDialog::finished, q, [this]() { lc_browser_cancel_all( b ); } );
}

void BrowserDialog::Impl::refreshLinkRow()
{
	char other[256] = "";
	int st = lc_link_handler_state( other, sizeof other );
	QString app = U( other );
	const QString test = testEnv( "VOYAGERSP_TEST_LINK_HANDLER" );
	if ( !test.isEmpty() ) {
		st = 2;
		app = QFileInfo( test ).completeBaseName();
	}
	log( QString( "Browser: stvef:// links open in %1" ).arg( st == 2 ? app : st == 1 ? QString( "this app" ) : QString( "no app" ) ) );
	linkRow->setVisible( st == 2 );
	linkLabel->setText( QString( "Join links on websites (stvef://) open in %1." ).arg( app ) );
}

QString BrowserDialog::Impl::selected() const
{
	const QModelIndex ix = table->currentIndex();
	if ( !ix.isValid() || !table->selectionModel()->isRowSelected( ix.row(), QModelIndex() ) )
		return QString();
	return ix.sibling( ix.row(), 0 ).data( Qt::UserRole ).toString();
}

void BrowserDialog::Impl::fetch( int src )
{
	fetched[src] = true;
	QString publicIP;
	lc_browser_fetch( b, src, publicIP.isEmpty() ? nullptr : B( publicIP ).constData(), []( int s, void *u ) {
		QMutexLocker l( &g_liveLock );
		if ( !g_live.contains( u ) )
			return;
		Impl *self = static_cast<Impl *>( u );
		QMetaObject::invokeMethod( self->q, [self, s]() {
			Q_UNUSED( s );
			if ( !self->reloadPending ) {
				self->reloadPending = true;
				self->reloadTimer.start();
			}
		}, Qt::QueuedConnection );
	}, this );
	show_();
}

void BrowserDialog::Impl::show_()
{
	const lc_browser_row *rows = nullptr;
	QList<QByteArray> favBytes;
	QVector<const char *> favPtrs;
	for ( const QString &f : favorites ) {
		favBytes << B( f );
	}
	for ( const QByteArray &fb : favBytes )
		favPtrs << fb.constData();
	const int n = lc_browser_rows( b, source, onlyFav->isChecked(), onlyPeople->isChecked(), favPtrs.constData(), int( favPtrs.size() ),
		keys.constData(), int( keys.size() ), &rows );
	const QString sel = selectedAddress;
	{
		QSignalBlocker blk( table->selectionModel() );
		model->setRows( rows, n < 0 ? 0 : n );
	}
	int reselect = -1;
	for ( int i = 0; i < model->rows.size() && !sel.isEmpty(); ++i )
		if ( U( model->rows[i].info.address ) == sel ) {
			reselect = i;
			break;
		}
	if ( reselect >= 0 ) {
		QSignalBlocker blk( table->selectionModel() );
		table->selectRow( reselect );
		table->setCurrentIndex( model->index( reselect, 1 ) );
	} else
		selectedAddress.clear();
	joinBtn->setEnabled( !selectedAddress.isEmpty() );
	statusLabel->setText( statusOf( source ) );
	const bool f = fetching( source );
	spinner->setVisible( f );
	/* sort indicator follows the primary key */
	if ( !keys.isEmpty() ) {
		static const int colOf[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
		QSignalBlocker hb( table->horizontalHeader() );
		table->horizontalHeader()->setSortIndicator( colOf[int( keys[0].column )],
			keys[0].ascending ? Qt::AscendingOrder : Qt::DescendingOrder );
	}
	showPlayers();
}

void BrowserDialog::Impl::sortBy( int c )
{
	if ( c < 0 || c > 8 )
		return;
	const lc_column col = lc_column( c );
	bool asc;
	if ( !keys.isEmpty() && keys[0].column == col )
		asc = !keys[0].ascending;
	else {
		/* direction on the first click (servers.md §8.3) */
		asc = col == LC_COL_NAME || col == LC_COL_MAP || col == LC_COL_TYPE || col == LC_COL_PING;
	}
	for ( int i = keys.size() - 1; i >= 0; --i )
		if ( keys[i].column == col )
			keys.removeAt( i );
	keys.prepend( lc_sort_key{ col, asc ? 1 : 0 } );
	show_();
}

void BrowserDialog::Impl::toggleFav( const QString &a )
{
	if ( favorites.contains( a ) )
		favorites.remove( a );
	else
		favorites.insert( a );
	QStringList l = favorites.values();
	l.sort();
	QVector<char> items( size_t( l.size() ) * 64 + 1, 0 );
	for ( int i = 0; i < l.size(); ++i )
		lc_strlcpy( &items[size_t( i ) * 64], B( l[i] ).constData(), 64 );
	lc_store_set_list( lc_store_shared(), "browserfavorites", items.constData(), 64, int( l.size() ) );
	lc_store_save( lc_store_shared() );
	show_();
}

void BrowserDialog::Impl::showPlayers()
{
	const QModelIndex ix = table->currentIndex();
	const bool have = !selectedAddress.isEmpty() && ix.isValid();
	players->setRowCount( 0 );
	mapInfo->hide();
	map3D->hide();
	if ( !have ) {
		playersTitle->setText( "Players" );
		playersNote->setText( "Select a server to see who's playing." );
		playersRefresh->hide();
		return;
	}
	const lc_browser_row &row = model->rows[ix.row()];
	const QString map = U( row.info.map );
	QString nm = plainName( U( row.info.hostname ) );
	if ( nm.isEmpty() )
		nm = plainName( U( row.info.name ) );
	playersTitle->setText( "Players on " + nm );

	/* Map Info / 3D Map Preview */
	if ( !map.isEmpty() && !builtinMaps.contains( map.toLower() ) && !lc_is_expansion_map( B( map.toLower() ).constData() ) ) {
		mapInfo->show();
		mapInfo->setToolTip( QString( "See %1 on efservers.com: screenshots and its files" ).arg( map ) );
		show3D();
	}

	const QString a = selectedAddress;
	if ( !statuses.contains( a ) ) {
		playersNote->setText( "Asking the server\xe2\x80\xa6" );
		playersRefresh->hide();
		if ( asking != a ) {
			asking = a;
			auto *st = new lc_server_status;
			memset( st, 0, sizeof *st );
			auto *ok = new int( 0 );
			async( q, [a, st, ok]() { *ok = lc_query_status( B( a ).constData(), st ); }, [this, a, st, ok]() {
				if ( asking == a )
					asking.clear();
				if ( *ok ) {
					st->when = lc_time();
					statuses[a] = *st;
					if ( st->isCmod )
						cmodOverride[a] = U( st->cmodVersion );
					reloadTimer.start();
					if ( selectedAddress == a )
						showPlayers();
				} else if ( selectedAddress == a ) {
					playersNote->setText( "The server didn't answer." );
					playersRefresh->show();
				}
				delete st;
				delete ok;
			} );
		}
		return;
	}
	lc_server_status st = statuses[a];
	lc_sort_players( st.players, st.playerCount );
	int pinged = 0;
	for ( int i = 0; i < st.playerCount; ++i )
		if ( st.players[i].ping > 0 )
			++pinged;
	const int humans = st.humans >= 0 ? st.humans : row.info.humans;
	const bool tag = humans < 0 || pinged == humans;
	if ( st.playerCount == 0 )
		playersNote->setText( "No one is playing." );
	else
		playersNote->setText( "as of " + QLocale().toString( QDateTime::fromSecsSinceEpoch( st.when ).time(), QLocale::ShortFormat ) );
	playersRefresh->show();
	players->setRowCount( st.playerCount );
	for ( int i = 0; i < st.playerCount; ++i ) {
		QTableWidgetItem *n = new QTableWidgetItem( U( st.players[i].name ) );
		n->setData( Qt::UserRole, U( st.players[i].name ) );
		n->setText( QString() );
		players->setItem( i, 0, n );
		QTableWidgetItem *s = new QTableWidgetItem( QString::number( st.players[i].score ) );
		s->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		players->setItem( i, 1, s );
		QTableWidgetItem *p = new QTableWidgetItem( QString::number( st.players[i].ping ) );
		p->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		p->setData( Qt::UserRole + 1, st.players[i].ping == 0 );
		players->setItem( i, 2, p );
		QTableWidgetItem *bt = new QTableWidgetItem( tag && st.players[i].ping == 0 ? "bot" : "" );
		bt->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
		players->setItem( i, 3, bt );
	}
}

void BrowserDialog::Impl::show3D()
{
	const QModelIndex ix = table->currentIndex();
	if ( !ix.isValid() )
		return;
	const QString map = U( model->rows[ix.row()].info.map );
	if ( views3D.contains( map ) ) {
		const QString u = views3D[map];
		if ( u != "none" ) {
			map3D->show();
			map3D->setToolTip( QString( "Look around %1 in 3D, on efservers.com" ).arg( map ) );
		}
		return;
	}
	if ( checking3D.contains( map ) )
		return;
	checking3D.insert( map );
	auto *url = new QString;
	auto *err = new bool( false );
	async( q, [map, url, err]() {
		char lookup[512];
		lc_efservers_3d_lookup_url( B( map ).constData(), lookup, sizeof lookup );
		char *body = nullptr;
		size_t len = 0;
		long status = 0;
		char e[256] = "";
		const int rc = lc_http_get( lookup, nullptr, 8, &body, &len, &status, e, sizeof e );
		if ( rc != LC_HTTP_OK || status != 200 || !body ) {
			*err = true;
		} else {
			char u[512] = "";
			if ( lc_efservers_parse_3d( body, u, sizeof u ) )
				*url = U( u );
		}
		free( body );
	}, [this, map, url, err]() {
		checking3D.remove( map );
		if ( !*err )
			views3D[map] = url->isEmpty() ? QString( "none" ) : *url;
		log( QString( "Browser: 3D view of %1: %2" ).arg( map, *err ? QString( "error" ) : url->isEmpty() ? QString( "none" ) : *url ) );
		delete url;
		delete err;
		const QModelIndex ix2 = table->currentIndex();
		if ( ix2.isValid() && U( model->rows[ix2.row()].info.map ) == map )
			show3D();
	} );
}

bool BrowserDialog::Impl::busyNow() const
{
	return fetching( source ) || !asking.isEmpty() || !checking3D.isEmpty();
}

void BrowserDialog::Impl::finish( const QString &a, const QString &pw )
{
	lc_browser_cancel_all( b );
	address = a;
	password = pw;
	log( "Browser: join " + a );
	q->accept();
}

void BrowserDialog::Impl::joinRowImpl( int row )
{
	if ( row < 0 || row >= model->rows.size() )
		return;
	const lc_browser_row &r = model->rows[row];
	const QString map = U( r.info.map );
	QString nm = plainName( U( r.info.hostname ) );
	if ( nm.isEmpty() )
		nm = plainName( U( r.info.name ) );
	if ( mapStateOf( map ) == 2 ) {
		int ans;
		const QString t = testEnv( "VOYAGERSP_TEST_BROWSER_EXPANSION" ).toLower();
		if ( !t.isEmpty() ) {
			ans = t == "anyway" ? 1 : 0;
			log( "[test] expansion warning: " + t );
		} else
			ans = alert( q, Warning, "This server's map is from the Expansion Pack.",
				QString::fromUtf8( "\xe2\x80\x9c%1\xe2\x80\x9d is running %2, which isn't in your game files. Servers can't send the Expansion Pack's maps, so joining "
					"will fail. Add the Expansion Pack's files with Add Game Files\xe2\x80\xa6 in the launcher, or pick another server." ).arg( nm, map ),
				QStringList() << "Cancel" << "Join Anyway", 0, 0 );
		if ( ans != 1 )
			return;
	}
	QString pw;
	if ( r.info.needPass ) {
		QDialog d( q );
		d.setWindowTitle( "Password" );
		d.setModal( true );
		QVBoxLayout *v = new QVBoxLayout( &d );
		v->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 16 ) );
		v->setSpacing( px( 8 ) );
		v->addWidget( label( QString::fromUtf8( "\xe2\x80\x9c%1\xe2\x80\x9d needs a password." ).arg( nm ), 13, QFont::DemiBold, QColor(), 300 ) );
		v->addWidget( label( "Ask the person running the server for it.", 12, QFont::Normal, QColor(), 300 ) );
		QLineEdit *e = new QLineEdit;
		e->setEchoMode( QLineEdit::Password );
		e->setFixedWidth( px( 240 ) );
		v->addWidget( e );
		QHBoxLayout *h = new QHBoxLayout;
		h->addStretch( 1 );
		QPushButton *join = new QPushButton( "Join" );
		QPushButton *cancel = new QPushButton( "Cancel" );
		join->setDefault( true );
		h->addWidget( join );
		h->addWidget( cancel );
		v->addLayout( h );
		QObject::connect( join, &QPushButton::clicked, &d, &QDialog::accept );
		QObject::connect( cancel, &QPushButton::clicked, &d, &QDialog::reject );
		e->setFocus();
		if ( testSet( "VOYAGERSP_TEST_NODIALOG" ) || testSet( "VOYAGERSP_TEST_CONFIRM" ) ) {
			log( "[dialog] password needed" );
		} else if ( d.exec() != QDialog::Accepted )
			return;
		pw = e->text();
		pw.remove( '"' );
	}
	finish( U( r.info.address ), pw );
}

/* ---- test hooks --------------------------------------------------------------------------------------------------------- */

void BrowserDialog::selectSource( const QString &tab )
{
	const QString t = tab.toLower();
	int s = d->source;
	if ( t == "local" )
		s = 0;
	else if ( t == "combined" && d->masterCount > 0 )
		s = 1;
	else if ( !t.isEmpty() && t.toInt() > 0 && t.toInt() <= d->masterCount )
		s = 1 + t.toInt();
	if ( s == d->source )
		return;
	d->tabs->setCurrent( s, true );
}

bool BrowserDialog::busy() const { return d->busyNow(); }

void BrowserDialog::selectRow( int row )
{
	if ( row >= 0 && row < d->model->rows.size() )
		d->table->selectRow( row );
}

void BrowserDialog::logRows()
{
	for ( const lc_browser_row &r : d->model->rows ) {
		QString t;
		const QString c = d->model->cmodOf ? d->model->cmodOf( r ) : QString();
		QString v = r.cmodVersion;
		t = c.isEmpty() ? QString( "-" ) : ( v.isEmpty() ? c : c + " (" + v + ")" );
		log( QString( "[test] row: %1 | %2 | %3 | cMod %4" ).arg( U( r.info.address ), plainName( U( r.info.hostname ) ), U( r.info.map ), t ) );
	}
}

void BrowserDialog::joinRow( int row )
{
	d->joinRowImpl( row );
}
