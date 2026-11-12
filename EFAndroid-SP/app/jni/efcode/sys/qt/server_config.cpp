/*
===========================================================================
ef-launcher (Qt 6): the Server Settings sheet ("Config…"). servers.md §4.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"
#include "levelshot.h"

using namespace Ui;

namespace {

const char *kDot = " \xc2\xb7 ";

struct Seg {
	QString text;
	QColor color;		/* invalid = secondary */
	bool strong = false;
};

class RotationList;

/* The map cell (servers.md §4.2.1): thumbnail, title, coloured detail line. */
class MapDelegate : public QStyledItemDelegate {
public:
	std::function<QList<Seg>( const QString & )> details;
	std::function<QPixmap( const QString &, bool *missing )> thumb;
	QSize sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const override { return QSize( 100, px( 50 ) ); }
	void paint( QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx ) const override
	{
		p->save();
		p->setRenderHint( QPainter::Antialiasing );
		const bool sel = opt.state & QStyle::State_Selected;
		if ( sel ) {
			p->setPen( Qt::NoPen );
			p->setBrush( opt.palette.color( opt.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive, QPalette::Highlight ) );
			p->drawRoundedRect( opt.rect.adjusted( 2, 1, -2, -1 ), 6, 6 );
		}
		const QString name = idx.data( Qt::UserRole ).toString();
		const QString title = idx.data( Qt::DisplayRole ).toString();
		const QRect r = opt.rect;
		const QColor txt = sel ? opt.palette.color( QPalette::HighlightedText ) : opt.palette.color( QPalette::Text );
		/* thumbnail */
		const QRect tr( r.left() + px( 6 ), r.center().y() - px( 21 ), px( 56 ), px( 42 ) );
		bool missing = false;
		const QPixmap pm = thumb ? thumb( name, &missing ) : QPixmap();
		p->save();
		QPainterPath clip;
		clip.addRoundedRect( tr, 4, 4 );
		p->setClipPath( clip );
		if ( !pm.isNull() )
			p->drawPixmap( tr, pm );
		else {
			QColor f = groupFill();
			f.setAlpha( 60 );
			p->fillRect( tr, QColor( 128, 128, 128, 40 ) );
			p->fillRect( tr, f );
			p->setOpacity( 0.5 );
			const QPixmap ic = iconPixmap( missing ? "questionmark" : "map", txt, 18 );
			p->drawPixmap( tr.center() - QPoint( px( 9 ), px( 9 ) ), ic );
		}
		p->restore();
		/* texts */
		const int x = tr.right() + px( 9 );
		const int w = r.right() - x - 6;
		p->setPen( txt );
		p->setFont( Ui::font( 13, QFont::Medium ) );
		const QFontMetrics fm( p->font() );
		p->drawText( QRect( x, r.top(), w, r.height() / 2 ), Qt::AlignLeft | Qt::AlignBottom,
			fm.elidedText( title, Qt::ElideRight, w ) );
		QFont df = Ui::font( 11 );
		p->setFont( df );
		const QFontMetrics dm( df );
		int cx = x;
		const int y = r.center().y() + 1;
		const QList<Seg> segs = details ? details( name ) : QList<Seg>();
		p->setClipRect( QRect( x, r.top(), w, r.height() ) );
		for ( int i = 0; i < segs.size(); ++i ) {
			if ( i > 0 ) {
				p->setPen( sel ? txt : secondary() );
				p->drawText( cx, y + dm.ascent(), QString::fromUtf8( kDot ) );
				cx += dm.horizontalAdvance( QString::fromUtf8( kDot ) );
			}
			QFont f = df;
			f.setWeight( segs[i].strong ? QFont::Medium : QFont::Normal );
			p->setFont( f );
			p->setPen( sel ? txt : ( segs[i].color.isValid() ? segs[i].color : secondary() ) );
			p->drawText( cx, y + QFontMetrics( f ).ascent(), segs[i].text );
			cx += QFontMetrics( f ).horizontalAdvance( segs[i].text );
		}
		p->restore();
	}
};

/* the rotation list: accepts drops from the map list (copy) and from itself (move) */
class RotationList : public QListWidget {
public:
	std::function<void( const QStringList &names, int row, bool internal )> dropped;
protected:
	void dropEvent( QDropEvent *e ) override
	{
		int row = count();
		const QModelIndex idx = indexAt( e->position().toPoint() );
		if ( idx.isValid() ) {
			row = idx.row();
			if ( dropIndicatorPosition() == QAbstractItemView::BelowItem )
				row = idx.row() + 1;
		}
		row = qBound( 0, row, count() );
		QListWidget *src = qobject_cast<QListWidget *>( e->source() );
		QStringList names;
		if ( src ) {
			for ( QListWidgetItem *it : src->selectedItems() )
				names << it->data( Qt::UserRole ).toString();
			/* selectedItems is in selection order, not list order: sort by row */
			QList<QPair<int, QString>> rows;
			for ( QListWidgetItem *it : src->selectedItems() )
				rows << qMakePair( src->row( it ), it->data( Qt::UserRole ).toString() );
			std::sort( rows.begin(), rows.end(), []( const QPair<int, QString> &a, const QPair<int, QString> &b ) { return a.first < b.first; } );
			names.clear();
			for ( const auto &p : rows )
				names << p.second;
		}
		e->setDropAction( Qt::CopyAction );
		e->accept();
		if ( dropped && !names.isEmpty() )
			dropped( names, row, src == this );
	}
};

QWidget *panelWith( QWidget *inner )
{
	Panel *p = new Panel;
	QVBoxLayout *v = new QVBoxLayout( p );
	v->setContentsMargins( 2, 2, 2, 2 );
	v->addWidget( inner );
	return p;
}

QLabel *formLabel( const QString &t, int width = 112 )
{
	QLabel *l = label( t, 13 );
	l->setAlignment( Qt::AlignRight | Qt::AlignTop );
	l->setFixedWidth( px( width ) );
	return l;
}

}	/* namespace */

struct ServerConfigDialog::Impl {
	ServerConfigDialog *q;
	lc_server_settings *original;
	lc_server_settings s;
	const lc_map_catalog *catalog = nullptr;
	QStringList rot;
	bool loading = false;

	Segmented *tabs;
	QStackedWidget *pages;

	/* maps page */
	QLabel *mapsCaption, *rotCaption;
	QLineEdit *search;
	QListWidget *mapList;
	RotationList *rotList;
	QPushButton *addBtn;
	QToolButton *rmBtn, *upBtn, *downBtn;

	/* game page */
	QComboBox *gametype, *skill;
	QLabel *gtHint, *botsHint;
	QCheckBox *mod[LC_MOD_COUNT];
	QLabel *modHint[LC_MOD_COUNT];
	NumberField *frag, *capture, *time, *bots;
	QLabel *fragLabel, *capLabel;
	QCheckBox *ff;

	/* server page */
	QLineEdit *hostname, *password;
	NumberField *maxPlayers, *port;
	QRadioButton *local, *internet;
	QCheckBox *openPort, *download, *pure;
	QLabel *netHint;
	QComboBox *tick;

	/* cMod page */
	QCheckBox *cmodGame, *smooth, *predict;
	QComboBox *unlagged;
	QLabel *cmodHint;
	bool cmodAvailable = false;

	void build();
	QWidget *mapsPage();
	QWidget *gamePage();
	QWidget *serverPage();
	QWidget *cmodPage();

	void showAll();
	void reloadMapList();
	void showRotation( const QList<int> &select = QList<int>() );
	void syncRotation();
	void showGame();
	void showLimits();
	void showGameTypeHint();
	void showServerTab();
	void showCmod();
	void updateButtons();
	QList<int> rotSelection() const;
	QList<Seg> detailSegs( const QString &name, bool rotation ) const;
	QString tooltipFor( const QString &name ) const;
	void addNames( const QStringList &names, int row );
	void moveRows( const QStringList &names, int row );
	void commit();
};

/* ---- construction ------------------------------------------------------------------------------------------- */

ServerConfigDialog::ServerConfigDialog( QWidget *parent, lc_server_settings *original, const QString &dataFolder )
	: QDialog( parent ), d( new Impl )
{
	d->q = this;
	d->original = original;
	d->s = *original;
	d->catalog = lc_map_catalog_get( B( dataFolder ).constData(), lc_homebase() );
	char pak[LC_MAX_PATH];
	d->cmodAvailable = lc_cmod_pak_path( pak, sizeof pak ) != 0;
	for ( int i = 0; i < d->s.rotationCount; ++i )
		d->rot << U( d->s.rotation[i] );
	setWindowTitle( "Server Settings" );
	setModal( true );
	d->build();
}

void ServerConfigDialog::Impl::build()
{
	QVBoxLayout *root = new QVBoxLayout( q );
	root->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 20 ) );
	root->setSpacing( px( 14 ) );

	tabs = new Segmented( QStringList() << "Maps" << "Game" << "Server" << "cMod" );
	tabs->setFixedWidth( px( 400 ) );
	root->addWidget( tabs, 0, Qt::AlignHCenter );

	pages = new QStackedWidget;
	pages->addWidget( mapsPage() );
	pages->addWidget( gamePage() );
	pages->addWidget( serverPage() );
	pages->addWidget( cmodPage() );
	Panel *box = new Panel;
	QVBoxLayout *bv = new QVBoxLayout( box );
	bv->setContentsMargins( 0, 0, 0, 0 );
	bv->addWidget( pages );
	box->setFixedSize( px( 660 ), px( 480 ) );
	root->addWidget( box, 0, Qt::AlignHCenter );

	QHBoxLayout *foot = new QHBoxLayout;
	QPushButton *restore = new QPushButton( "Restore Defaults" );
	QPushButton *cancel = new QPushButton( "Cancel" );
	QPushButton *done = new QPushButton( "Done" );
	cancel->setMinimumWidth( px( 80 ) );
	done->setMinimumWidth( px( 80 ) );
	done->setDefault( true );
	foot->addWidget( restore );
	foot->addStretch( 1 );
	foot->addWidget( cancel );
	foot->addWidget( done );
	root->addLayout( foot );

	QObject::connect( tabs, &Segmented::currentChanged, q, [this]( int i ) { pages->setCurrentIndex( i ); } );
	QObject::connect( restore, &QPushButton::clicked, q, [this]() {
		lc_server_settings_defaults( &s, lc_user_full_name() );
		rot.clear();
		for ( int i = 0; i < s.rotationCount; ++i )
			rot << U( s.rotation[i] );
		showAll();
	} );
	QObject::connect( cancel, &QPushButton::clicked, q, &QDialog::reject );
	QObject::connect( done, &QPushButton::clicked, q, [this]() { commit(); q->accept(); } );

	showAll();
	tabs->setCurrent( 0 );
	pages->setCurrentIndex( 0 );
	q->setFixedWidth( px( 700 ) );
	done->setFocus();
}

void ServerConfigDialog::Impl::commit()
{
	if ( QWidget *f = QApplication::focusWidget() )
		f->clearFocus();		/* commits any text being edited */
	syncRotation();
	*original = s;
	lc_server_settings_save( original, lc_store_shared() );
	lc_store_save( lc_store_shared() );
	log( QString( "Server settings saved: %1, %2, %3 maps" ).arg( U( original->hostname ), U( lc_gametype_name( original->gametype ) ) )
		.arg( original->rotationCount ) );
}

void ServerConfigDialog::commit() { d->commit(); }

void ServerConfigDialog::selectTab( const QString &tab )
{
	const QString t = tab.toLower();
	const int i = t == "game" ? 1 : t == "server" ? 2 : t == "cmod" ? 3 : 0;
	d->tabs->setCurrent( i );
	d->pages->setCurrentIndex( i );
}

void ServerConfigDialog::setTickRate( int v )
{
	d->s.tickRate = v == 40 ? 40 : 20;
	d->showServerTab();
}

/* ---- Maps page ------------------------------------------------------------------------------------------------ */

QList<Seg> ServerConfigDialog::Impl::detailSegs( const QString &name, bool ) const
{
	QList<Seg> out;
	const lc_map_info *m = catalog ? lc_map_find( catalog, B( name ).constData() ) : nullptr;
	if ( !m ) {
		out << Seg{ name, QColor(), false } << Seg{ "Missing from your game files", red(), true };
		return out;
	}
	out << Seg{ name, QColor(), false };
	if ( m->expansion )
		out << Seg{ "Expansion Pack", purple(), true };
	else if ( !m->retail )
		out << Seg{ "Add-on", blue(), true };
	if ( !lc_map_supports_gametype( m, s.gametype ) )
		out << Seg{ QString( "Not made for %1" ).arg( U( lc_gametype_name( s.gametype ) ) ), orange(), true };
	else {
		char desc[256] = "";
		lc_map_types_description( m, desc, sizeof desc );
		if ( desc[0] )
			out << Seg{ U( desc ), QColor(), false };
	}
	return out;
}

QString ServerConfigDialog::Impl::tooltipFor( const QString &name ) const
{
	const lc_map_info *m = catalog ? lc_map_find( catalog, B( name ).constData() ) : nullptr;
	if ( !m )
		return "Not in any .pk3 file in your game folders any more. The server skips it.";
	if ( m->expansion )
		return QString( "%1 (in %2). Players need the Expansion Pack: servers never send its files." ).arg( name, U( m->pk3 ) );
	if ( !m->retail )
		return QString( "%1 (in %2). Players without it can download it if map downloads are on." )
			.arg( name, m->pk3[0] ? U( m->pk3 ) : QString( "a loose .bsp file" ) );
	return QString( "%1 (in %2)" ).arg( name, U( m->pk3 ) );
}

QWidget *ServerConfigDialog::Impl::mapsPage()
{
	QWidget *w = new QWidget;
	QHBoxLayout *h = new QHBoxLayout( w );
	h->setContentsMargins( px( 16 ), px( 14 ), px( 16 ), px( 14 ) );
	h->setSpacing( px( 20 ) );

	auto delegate = [this]( bool rotation ) {
		MapDelegate *md = new MapDelegate;
		md->details = [this, rotation]( const QString &n ) { return detailSegs( n, rotation ); };
		md->thumb = [this]( const QString &n, bool *missing ) {
			const lc_map_info *m = catalog ? lc_map_find( catalog, B( n ).constData() ) : nullptr;
			*missing = m == nullptr;
			return levelshotThumb( m, q->devicePixelRatioF() );
		};
		return md;
	};

	/* left column */
	QVBoxLayout *left = new QVBoxLayout;
	left->setSpacing( px( 8 ) );
	mapsCaption = label( QString(), 11, QFont::DemiBold, secondary() );
	left->addWidget( mapsCaption );
	search = new QLineEdit;
	search->setPlaceholderText( "Search maps" );
	search->setClearButtonEnabled( true );
	left->addWidget( search );
	mapList = new QListWidget;
	styleView( mapList );
	mapList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	mapList->setItemDelegate( delegate( false ) );
	mapList->setDragEnabled( true );
	mapList->setDragDropMode( QAbstractItemView::DragOnly );
	mapList->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
	left->addWidget( panelWith( mapList ), 1 );
	addBtn = new QPushButton( QString::fromUtf8( "Add to Rotation" ) );
	addBtn->setIcon( icon( "plus", QColor(), 11 ) );
	left->addWidget( addBtn, 0, Qt::AlignLeft );
	h->addLayout( left, 1 );

	/* right column */
	QVBoxLayout *right = new QVBoxLayout;
	right->setSpacing( px( 8 ) );
	rotCaption = label( QString(), 11, QFont::DemiBold, secondary() );
	right->addWidget( rotCaption );
	right->addWidget( label( "The server plays these in order, then starts over.", 11, QFont::Normal, secondary(), 300 ) );
	right->addSpacing( px( 1 ) );
	rotList = new RotationList;
	styleView( rotList );
	rotList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	rotList->setItemDelegate( delegate( true ) );
	rotList->setAcceptDrops( true );
	rotList->setDragEnabled( true );
	rotList->setDragDropMode( QAbstractItemView::DragDrop );
	rotList->setDropIndicatorShown( true );
	rotList->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
	rotList->dropped = [this]( const QStringList &names, int row, bool internal ) {
		if ( internal )
			moveRows( names, row );
		else
			addNames( names, row );
	};
	right->addWidget( panelWith( rotList ), 1 );
	QHBoxLayout *br = new QHBoxLayout;
	rmBtn = iconButton( "minus", "Remove from the rotation" );
	upBtn = iconButton( "chevron.up", "Play earlier" );
	downBtn = iconButton( "chevron.down", "Play later" );
	br->addWidget( rmBtn );
	br->addWidget( upBtn );
	br->addWidget( downBtn );
	br->addStretch( 1 );
	br->addWidget( label( "Drag to reorder", 11, QFont::Normal, tertiary() ) );
	right->addLayout( br );
	h->addLayout( right, 1 );

	QObject::connect( search, &QLineEdit::textChanged, q, [this]() { reloadMapList(); } );
	QObject::connect( mapList, &QListWidget::itemSelectionChanged, q, [this]() { updateButtons(); } );
	QObject::connect( rotList, &QListWidget::itemSelectionChanged, q, [this]() { updateButtons(); } );
	QObject::connect( mapList, &QListWidget::itemDoubleClicked, q, [this]( QListWidgetItem *it ) {
		addNames( QStringList( it->data( Qt::UserRole ).toString() ), rot.size() );
	} );
	QObject::connect( addBtn, &QPushButton::clicked, q, [this]() {
		QList<QPair<int, QString>> rows;
		for ( QListWidgetItem *it : mapList->selectedItems() )
			rows << qMakePair( mapList->row( it ), it->data( Qt::UserRole ).toString() );
		std::sort( rows.begin(), rows.end(), []( const QPair<int, QString> &a, const QPair<int, QString> &b ) { return a.first < b.first; } );
		QStringList names;
		for ( const auto &p : rows )
			names << p.second;
		addNames( names, rot.size() );
	} );
	QObject::connect( rmBtn, &QToolButton::clicked, q, [this]() {
		QList<int> sel = rotSelection();
		if ( sel.isEmpty() )
			return;
		for ( int i = sel.size() - 1; i >= 0; --i )
			rot.removeAt( sel[i] );
		const int first = sel.first();
		showRotation( rot.isEmpty() ? QList<int>() : QList<int>() << qMin( first, int( rot.size() ) - 1 ) );
	} );
	QObject::connect( upBtn, &QToolButton::clicked, q, [this]() {
		QList<int> sel = rotSelection();
		if ( sel.isEmpty() || sel.first() <= 0 )
			return;
		QStringList block;
		for ( int i : sel )
			block << rot[i];
		for ( int i = sel.size() - 1; i >= 0; --i )
			rot.removeAt( sel[i] );
		const int at = sel.first() - 1;
		QList<int> ns;
		for ( int i = 0; i < block.size(); ++i ) {
			rot.insert( at + i, block[i] );
			ns << at + i;
		}
		showRotation( ns );
	} );
	QObject::connect( downBtn, &QToolButton::clicked, q, [this]() {
		QList<int> sel = rotSelection();
		if ( sel.isEmpty() || sel.last() >= rot.size() - 1 )
			return;
		QStringList block;
		for ( int i : sel )
			block << rot[i];
		const int last = sel.last();
		for ( int i = sel.size() - 1; i >= 0; --i )
			rot.removeAt( sel[i] );
		const int at = qMin( last + 2 - int( sel.size() ), int( rot.size() ) );
		QList<int> ns;
		for ( int i = 0; i < block.size(); ++i ) {
			rot.insert( at + i, block[i] );
			ns << at + i;
		}
		showRotation( ns );
	} );
	/* Delete / Backspace remove the selected rotation rows */
	QShortcut *del = new QShortcut( QKeySequence( Qt::Key_Delete ), rotList, nullptr, nullptr, Qt::WidgetShortcut );
	QObject::connect( del, &QShortcut::activated, rmBtn, &QToolButton::click );
	QShortcut *bs = new QShortcut( QKeySequence( Qt::Key_Backspace ), rotList, nullptr, nullptr, Qt::WidgetShortcut );
	QObject::connect( bs, &QShortcut::activated, rmBtn, &QToolButton::click );
	return w;
}

QList<int> ServerConfigDialog::Impl::rotSelection() const
{
	QList<int> sel;
	for ( QListWidgetItem *it : rotList->selectedItems() )
		sel << rotList->row( it );
	std::sort( sel.begin(), sel.end() );
	return sel;
}

void ServerConfigDialog::Impl::addNames( const QStringList &names, int row )
{
	row = qBound( 0, row, int( rot.size() ) );
	QList<int> ns;
	for ( const QString &n : names ) {
		if ( rot.size() >= 64 )
			break;
		rot.insert( row, n );
		ns << row;
		++row;
	}
	showRotation( ns );
	if ( !ns.isEmpty() )
		rotList->scrollToItem( rotList->item( ns.last() ) );
}

void ServerConfigDialog::Impl::moveRows( const QStringList &, int row )
{
	QList<int> sel = rotSelection();
	if ( sel.isEmpty() )
		return;
	QStringList block;
	int above = 0;
	for ( int i : sel ) {
		block << rot[i];
		if ( i < row )
			++above;
	}
	for ( int i = sel.size() - 1; i >= 0; --i )
		rot.removeAt( sel[i] );
	const int at = qBound( 0, row - above, int( rot.size() ) );
	QList<int> ns;
	for ( int i = 0; i < block.size(); ++i ) {
		rot.insert( at + i, block[i] );
		ns << at + i;
	}
	showRotation( ns );
}

void ServerConfigDialog::Impl::syncRotation()
{
	s.rotationCount = qMin( int( rot.size() ), 64 );
	for ( int i = 0; i < s.rotationCount; ++i )
		lc_strlcpy( s.rotation[i], B( rot[i] ).constData(), sizeof s.rotation[i] );
}

void ServerConfigDialog::Impl::reloadMapList()
{
	const QString query = search->text().trimmed();
	QSet<QString> sel;
	for ( QListWidgetItem *it : mapList->selectedItems() )
		sel << it->data( Qt::UserRole ).toString();
	mapList->clear();
	const int total = catalog ? lc_map_catalog_count( catalog ) : 0;
	int shown = 0;
	for ( int i = 0; i < total; ++i ) {
		const lc_map_info *m = lc_map_catalog_at( catalog, i );
		const QString name = U( m->name ), disp = U( lc_map_display_name( m ) );
		if ( !query.isEmpty() && !disp.contains( query, Qt::CaseInsensitive ) && !name.contains( query, Qt::CaseInsensitive ) )
			continue;
		QListWidgetItem *it = new QListWidgetItem( disp );
		it->setData( Qt::UserRole, name );
		it->setToolTip( tooltipFor( name ) );
		mapList->addItem( it );
		it->setSelected( sel.contains( name ) );
		++shown;
	}
	mapsCaption->setText( query.isEmpty() ? QString::fromUtf8( "MAPS \xc2\xb7 %1" ).arg( total )
		: QString::fromUtf8( "MAPS \xc2\xb7 %1 OF %2" ).arg( shown ).arg( total ) );
	updateButtons();
}

void ServerConfigDialog::Impl::showRotation( const QList<int> &select )
{
	syncRotation();
	rotList->clear();
	for ( int i = 0; i < rot.size(); ++i ) {
		const lc_map_info *m = catalog ? lc_map_find( catalog, B( rot[i] ).constData() ) : nullptr;
		QListWidgetItem *it = new QListWidgetItem( QString( "%1.  %2" ).arg( i + 1 ).arg( m ? U( lc_map_display_name( m ) ) : rot[i] ) );
		it->setData( Qt::UserRole, rot[i] );
		it->setToolTip( tooltipFor( rot[i] ) );
		rotList->addItem( it );
	}
	for ( int i : select )
		if ( i >= 0 && i < rotList->count() )
			rotList->item( i )->setSelected( true );
	rotCaption->setText( QString::fromUtf8( "ROTATION \xc2\xb7 %1 MAP%2" ).arg( rot.size() ).arg( rot.size() == 1 ? "" : "S" ) );
	mapList->viewport()->update();
	updateButtons();
	showGameTypeHint();
}

void ServerConfigDialog::Impl::updateButtons()
{
	addBtn->setEnabled( !mapList->selectedItems().isEmpty() );
	const QList<int> sel = rotSelection();
	rmBtn->setEnabled( !sel.isEmpty() );
	upBtn->setEnabled( !sel.isEmpty() && sel.first() > 0 );
	downBtn->setEnabled( !sel.isEmpty() && sel.last() < rot.size() - 1 );
}

/* ---- Game page ------------------------------------------------------------------------------------------------- */

QWidget *ServerConfigDialog::Impl::gamePage()
{
	QWidget *w = new QWidget;
	QGridLayout *g = new QGridLayout( w );
	g->setContentsMargins( px( 16 ), px( 18 ), px( 16 ), px( 14 ) );
	g->setHorizontalSpacing( px( 10 ) );
	g->setVerticalSpacing( px( 8 ) );
	int r = 0;

	g->addWidget( formLabel( "Game type:" ), r, 0 );
	gametype = new QComboBox;
	gametype->addItem( "Free For All", LC_GT_FFA );
	gametype->addItem( "Tournament", LC_GT_TOURNAMENT );
	gametype->addItem( "Team Holomatch", LC_GT_TEAM );
	gametype->addItem( "Capture the Flag", LC_GT_CTF );
	g->addWidget( gametype, r++, 1, Qt::AlignLeft );
	gtHint = label( QString(), 11, QFont::Normal, secondary(), 420 );
	g->addWidget( gtHint, r++, 1 );

	QLabel *ml = formLabel( "Modifiers:" );
	g->addWidget( ml, r, 0, Qt::AlignTop );
	QGridLayout *mg = new QGridLayout;
	mg->setHorizontalSpacing( px( 8 ) );
	mg->setVerticalSpacing( px( 5 ) );
	const char *hints[LC_MOD_COUNT] = {
		"Killed players join the enemy team, the Borg. Team Holomatch only.",
		"Players choose a class, each with its own weapons and abilities.",
		"One direct hit causes instant death.",
		"Last one standing wins. Not with Capture the Flag.",
		"One random player gets all the weapons and armor. Free For All and Tournament only.",
	};
	const char *titles[LC_MOD_COUNT] = { "Assimilation", "Specialties", "Disintegration", "Elimination", "Action Hero" };
	for ( int i = 0; i < LC_MOD_COUNT; ++i ) {
		mod[i] = new QCheckBox( titles[i] );
		mod[i]->setToolTip( hints[i] );
		mod[i]->setMinimumWidth( px( 112 ) );
		modHint[i] = label( hints[i], 11, QFont::Normal, secondary(), 370 );
		mg->addWidget( mod[i], i, 0 );
		mg->addWidget( modHint[i], i, 1 );
		QObject::connect( mod[i], &QCheckBox::clicked, q, [this, i]( bool on ) {
			if ( loading )
				return;
			s.mods[i] = on;
			lc_server_settings_apply_modifier_rules( &s, on ? i : -1 );
			showGame();
		} );
	}
	g->addLayout( mg, r++, 1 );
	g->setRowMinimumHeight( r - 1, 0 );
	g->addWidget( label( "Some modifiers exclude others, as in the game's own Create Server menu.", 11, QFont::Normal, secondary(), 420 ), r++, 1 );

	fragLabel = formLabel( "Frag limit:" );
	g->addWidget( fragLabel, r, 0 );
	frag = new NumberField( 0, 999, "points" );
	g->addWidget( frag, r++, 1 );
	capLabel = formLabel( "Capture limit:" );
	g->addWidget( capLabel, r, 0 );
	capture = new NumberField( 0, 99, "flags" );
	g->addWidget( capture, r++, 1 );
	g->addWidget( formLabel( "Time limit:" ), r, 0 );
	time = new NumberField( 0, 999, "minutes" );
	g->addWidget( time, r++, 1 );
	ff = new QCheckBox( "Friendly fire: teammates can hurt each other" );
	g->addWidget( ff, r++, 1 );

	g->addWidget( formLabel( "Bots:" ), r, 0 );
	QHBoxLayout *bh = new QHBoxLayout;
	bots = new NumberField( 0, 31, QString() );
	bh->addWidget( bots );
	bh->addSpacing( px( 12 ) );
	skill = new QComboBox;
	for ( int i = 1; i <= 5; ++i )
		skill->addItem( QString::fromUtf8( "%1 \xc2\xb7 %2" ).arg( i ).arg( U( lc_skill_name( i ) ) ), i );
	bh->addWidget( skill );
	bh->addStretch( 1 );
	g->addLayout( bh, r++, 1 );
	botsHint = label( QString(), 11, QFont::Normal, secondary(), 420 );
	g->addWidget( botsHint, r++, 1 );
	g->setRowStretch( r, 1 );
	g->setColumnStretch( 1, 1 );

	QObject::connect( gametype, QOverload<int>::of( &QComboBox::activated ), q, [this]() {
		s.gametype = gametype->currentData().toInt();
		lc_server_settings_apply_modifier_rules( &s, -1 );
		showGame();
		reloadMapList();
		mapList->viewport()->update();
		rotList->viewport()->update();
	} );
	QObject::connect( frag, &NumberField::valueEdited, q, [this]( int v ) { s.fraglimit = v; } );
	QObject::connect( capture, &NumberField::valueEdited, q, [this]( int v ) { s.capturelimit = v; } );
	QObject::connect( time, &NumberField::valueEdited, q, [this]( int v ) { s.timelimit = v; } );
	QObject::connect( bots, &NumberField::valueEdited, q, [this]( int v ) { s.bots = v; showLimits(); } );
	QObject::connect( skill, QOverload<int>::of( &QComboBox::activated ), q, [this]() { s.botSkill = skill->currentData().toInt(); } );
	QObject::connect( ff, &QCheckBox::clicked, q, [this]( bool on ) { s.friendlyFire = on; } );
	return w;
}

void ServerConfigDialog::Impl::showGame()
{
	loading = true;
	gametype->setCurrentIndex( gametype->findData( s.gametype ) );
	for ( int i = 0; i < LC_MOD_COUNT; ++i ) {
		mod[i]->setChecked( s.mods[i] != 0 );
		mod[i]->setEnabled( lc_server_settings_modifier_allowed( &s, i ) != 0 );
		setColor( modHint[i], mod[i]->isEnabled() ? secondary() : tertiary() );
	}
	frag->setValue( s.fraglimit );
	capture->setValue( s.capturelimit );
	time->setValue( s.timelimit );
	bots->setValue( s.bots );
	skill->setCurrentIndex( qBound( 0, s.botSkill - 1, 4 ) );
	ff->setChecked( s.friendlyFire != 0 );
	loading = false;
	showLimits();
	showGameTypeHint();
}

void ServerConfigDialog::Impl::showLimits()
{
	frag->setEnabled( s.gametype < 4 );
	setColor( fragLabel, s.gametype < 4 ? QColor() : tertiary() );
	capture->setEnabled( s.gametype == 4 );
	setColor( capLabel, s.gametype == 4 ? QColor() : tertiary() );
	ff->setEnabled( s.gametype >= 3 );
	skill->setEnabled( s.bots > 0 );
	QString t;
	if ( s.bots == 0 )
		t = "No bots.";
	else if ( s.gametype >= 3 )
		t = QString( "Bots keep each team at %1 players; they leave as people join." ).arg( s.bots );
	else
		t = QString( "Bots keep the game at %1 players; they leave as people join." ).arg( s.bots );
	botsHint->setText( t );
}

void ServerConfigDialog::Impl::showGameTypeHint()
{
	int total = 0, made = 0;
	for ( const QString &n : rot ) {
		const lc_map_info *m = catalog ? lc_map_find( catalog, B( n ).constData() ) : nullptr;
		if ( !m )
			continue;
		++total;
		if ( lc_map_supports_gametype( m, s.gametype ) )
			++made;
	}
	if ( total == 0 ) {
		gtHint->setText( "Add maps to the rotation on the Maps tab." );
		setColor( gtHint, orange() );
	} else if ( made == total ) {
		gtHint->setText( total == 1 ? QString( "The map in the rotation is made for it." )
			: QString( "All %1 maps in the rotation are made for it." ).arg( total ) );
		setColor( gtHint, secondary() );
	} else {
		gtHint->setText( QString( "%1 of the %2 maps in the rotation are made for it; the others may lack spawn points or flags." ).arg( made ).arg( total ) );
		setColor( gtHint, orange() );
	}
}

/* ---- Server page ------------------------------------------------------------------------------------------------ */

QWidget *ServerConfigDialog::Impl::serverPage()
{
	QWidget *w = new QWidget;
	QGridLayout *g = new QGridLayout( w );
	g->setContentsMargins( px( 16 ), px( 18 ), px( 16 ), px( 14 ) );
	g->setHorizontalSpacing( px( 10 ) );
	g->setVerticalSpacing( px( 8 ) );
	int r = 0;
	g->addWidget( formLabel( "Server name:" ), r, 0 );
	hostname = new QLineEdit;
	hostname->setFixedWidth( px( 300 ) );
	g->addWidget( hostname, r++, 1, Qt::AlignLeft );
	g->addWidget( formLabel( "Password:" ), r, 0 );
	password = new QLineEdit;
	password->setFixedWidth( px( 200 ) );
	password->setPlaceholderText( "None" );
	g->addWidget( password, r++, 1, Qt::AlignLeft );
	g->addWidget( label( "Players type it in Holomatch's Join menu. Leave it empty for an open server.", 11, QFont::Normal, secondary(), 420 ), r++, 1 );

	g->addWidget( formLabel( "Max players:" ), r, 0 );
	maxPlayers = new NumberField( 2, 32, "including bots" );
	g->addWidget( maxPlayers, r++, 1 );

	g->addWidget( formLabel( "Network:" ), r, 0 );
	local = new QRadioButton( "Local network only" );
	g->addWidget( local, r++, 1 );
	internet = new QRadioButton( "Internet: also list it on the master server" );
	g->addWidget( internet, r++, 1 );
	openPort = new QCheckBox( "Open the port on my router automatically" );
	openPort->setToolTip( "Asks your router (NAT-PMP / UPnP) to forward the port while the server runs, so you don't have to set up port forwarding." );
	g->addWidget( openPort, r++, 1 );
	g->addWidget( formLabel( "Port:" ), r, 0 );
	QHBoxLayout *ph = new QHBoxLayout;
	ph->setSpacing( px( 8 ) );
	port = new NumberField( 1024, 65535, QString(), false, 64 );
	ph->addWidget( port );
	ph->addWidget( label( QString::fromUtf8( "UDP \xc2\xb7 27960\xe2\x80\x93" "27963 show up in the Join menu" ), 11, QFont::Normal, secondary() ) );
	ph->addStretch( 1 );
	g->addLayout( ph, r++, 1 );
	netHint = label( QString(), 11, QFont::Normal, secondary(), 400 );
	g->addWidget( netHint, r++, 1 );

	g->addWidget( formLabel( "Downloads:" ), r, 0 );
	download = new QCheckBox( "Let players download maps they don't have" );
	g->addWidget( download, r++, 1 );
	g->addWidget( label( "Add-on maps only: the retail and Expansion Pack files are never sent.", 11, QFont::Normal, secondary(), 420 ), r++, 1 );
	g->addWidget( formLabel( "Files:" ), r, 0 );
	pure = new QCheckBox( "Pure server: players must use the same game files" );
	g->addWidget( pure, r++, 1 );
	g->addWidget( formLabel( "Tick Rate:" ), r, 0 );
	tick = new QComboBox;
	tick->addItem( "Standard (20)", 20 );
	tick->addItem( "High (40)", 40 );
	tick->setToolTip( "How many times a second the server updates the game (sv_fps). High uses more upload bandwidth." );
	g->addWidget( tick, r++, 1, Qt::AlignLeft );
	g->addWidget( label( "Updates per second. High uses more upload bandwidth.", 11, QFont::Normal, secondary(), 420 ), r++, 1 );
	g->setRowStretch( r, 1 );
	g->setColumnStretch( 1, 1 );

	QObject::connect( hostname, &QLineEdit::textEdited, q, [this]( const QString &t ) {
		lc_clean_cvar_text( B( t ).constData(), 48, s.hostname, sizeof s.hostname );
	} );
	QObject::connect( password, &QLineEdit::textEdited, q, [this]( const QString &t ) {
		lc_clean_cvar_text( B( t ).constData(), 32, s.password, sizeof s.password );
	} );
	QObject::connect( maxPlayers, &NumberField::valueEdited, q, [this]( int v ) { s.maxPlayers = v; } );
	QObject::connect( local, &QRadioButton::clicked, q, [this]() { s.internet = 0; showServerTab(); } );
	QObject::connect( internet, &QRadioButton::clicked, q, [this]() { s.internet = 1; showServerTab(); } );
	QObject::connect( openPort, &QCheckBox::clicked, q, [this]( bool on ) { s.openPort = on; showServerTab(); } );
	QObject::connect( port, &NumberField::valueEdited, q, [this]( int v ) { s.port = v; showServerTab(); } );
	QObject::connect( download, &QCheckBox::clicked, q, [this]( bool on ) { s.allowDownload = on; } );
	QObject::connect( pure, &QCheckBox::clicked, q, [this]( bool on ) { s.pure = on; } );
	QObject::connect( tick, QOverload<int>::of( &QComboBox::activated ), q, [this]() { s.tickRate = tick->currentData().toInt(); } );
	return w;
}

void ServerConfigDialog::Impl::showServerTab()
{
	loading = true;
	hostname->setText( U( s.hostname ) );
	password->setText( U( s.password ) );
	maxPlayers->setValue( s.maxPlayers );
	local->setChecked( !s.internet );
	internet->setChecked( s.internet != 0 );
	openPort->setChecked( s.openPort != 0 );
	openPort->setEnabled( s.internet != 0 );
	port->setValue( s.port );
	download->setChecked( s.allowDownload != 0 );
	pure->setChecked( s.pure != 0 );
	tick->setCurrentIndex( s.tickRate == 40 ? 1 : 0 );
	loading = false;

	const bool disc = s.port >= 27960 && s.port <= 27963;
	QString t;
	QColor c = secondary();
	if ( !disc ) {
		t = "Outside 27960\xe2\x80\x93" "27963, Holomatch's Join menu won't find the server on your network: players have to connect by address.";
		c = orange();
	} else if ( s.internet && s.openPort )
		t = QString( "The server window shows your public address once the router has opened UDP port %1. If it can't, forward that port to this %2 by hand." )
			.arg( s.port ).arg( nounLc() );
	else if ( s.internet )
		t = QString( "Players outside your network need UDP port %1 forwarded to this %2 on your router." ).arg( s.port ).arg( nounLc() );
	else
		t = "Players on your network find it in Holomatch's Join menu.";
	netHint->setText( QString::fromUtf8( t.toUtf8() ) );
	setColor( netHint, c );
}

/* ---- cMod page --------------------------------------------------------------------------------------------------- */

QWidget *ServerConfigDialog::Impl::cmodPage()
{
	QWidget *w = new QWidget;
	QGridLayout *g = new QGridLayout( w );
	g->setContentsMargins( px( 16 ), px( 18 ), px( 16 ), px( 14 ) );
	g->setHorizontalSpacing( px( 10 ) );
	g->setVerticalSpacing( px( 8 ) );
	int r = 0;
	g->addWidget( formLabel( "Game code:", 128 ), r, 0 );
	cmodGame = new QCheckBox( "Run cMod's game code" );
	cmodGame->setToolTip( "cMod's server game module (qagame) by Noah Metzger, in place of the original one (sv_cmodGame)." );
	g->addWidget( cmodGame, r++, 1 );
	cmodHint = label( QString(), 11, QFont::Normal, secondary(), 420 );
	g->addWidget( cmodHint, r++, 1 );
	g->setRowMinimumHeight( r, px( 6 ) );
	g->addWidget( formLabel( "Lag compensation:", 128 ), r, 0 );
	unlagged = new QComboBox;
	unlagged->addItem( "Off", 0 );
	unlagged->addItem( "Instant-hit weapons", 1 );
	unlagged->addItem( "All weapons", 2 );
	unlagged->setToolTip( "g_unlagged: the server checks each shot against where the shooter saw the other players." );
	g->addWidget( unlagged, r++, 1, Qt::AlignLeft );
	g->addWidget( label( "Shots hit what the shooter saw on screen, so players with a high ping can aim normally. Works for every player, "
		"whatever game they run. Instant-hit weapons is the common choice; All weapons also covers projectiles.", 11, QFont::Normal,
		secondary(), 420 ), r++, 1 );
	smooth = new QCheckBox( "Smooth other players' movement" );
	smooth->setToolTip( "g_smoothClients" );
	g->addWidget( smooth, r++, 1 );
	predict = new QCheckBox( "Let cMod players see their own shots at once" );
	predict->setToolTip( "g_unlaggedPredict: players with cMod's cgame (this app's Holomatch, cMod) predict their weapon fire." );
	g->addWidget( predict, r++, 1 );
	g->setRowMinimumHeight( r, px( 10 ) );
	g->addWidget( label( "cMod is by Noah Metzger (Chomenor). Players never download it from your server.", 11, QFont::Normal, secondary(), 420 ), r++, 1 );
	g->setRowStretch( r, 1 );
	g->setColumnStretch( 1, 1 );

	QObject::connect( cmodGame, &QCheckBox::clicked, q, [this]( bool on ) { s.cmodGame = on; showCmod(); } );
	QObject::connect( unlagged, QOverload<int>::of( &QComboBox::activated ), q, [this]() { s.unlagged = unlagged->currentData().toInt(); showCmod(); } );
	QObject::connect( smooth, &QCheckBox::clicked, q, [this]( bool on ) { s.smoothClients = on; } );
	QObject::connect( predict, &QCheckBox::clicked, q, [this]( bool on ) { s.unlaggedPredict = on; } );
	return w;
}

void ServerConfigDialog::Impl::showCmod()
{
	const bool have = cmodAvailable, on = have && s.cmodGame;
	cmodGame->setChecked( s.cmodGame != 0 );
	cmodGame->setEnabled( have );
	if ( have ) {
		cmodHint->setText( "cMod's version of the Holomatch game: the original rules and modes, many bug fixes, and lag compensation." );
		setColor( cmodHint, secondary() );
	} else {
		cmodHint->setText( "cMod's game modules aren't in this copy of the app." );
		setColor( cmodHint, orange() );
	}
	unlagged->setCurrentIndex( qBound( 0, s.unlagged, 2 ) );
	unlagged->setEnabled( on );
	smooth->setChecked( s.smoothClients != 0 );
	smooth->setEnabled( on );
	predict->setChecked( s.unlaggedPredict != 0 );
	predict->setEnabled( on && s.unlagged > 0 );
}

void ServerConfigDialog::Impl::showAll()
{
	search->clear();
	reloadMapList();
	showRotation();
	showGame();
	showServerTab();
	showCmod();
}
