/*
===========================================================================
ef-launcher (Qt 6): the Master Servers sheet ("Servers…"). servers.md §6.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

#include <QSet>

using namespace Ui;

namespace {

/* distinct ip:port entries a master lists, -1 when it didn't answer (VSP_QueryMasterCount) */
int masterCount( const QString &master )
{
	struct Ctx { QSet<quint64> seen; } ctx;
	const int rc = lc_query_master( B( master ).constData(), 2000,
		[]( lc_addr4 a, void *u ) {
			Ctx *c = static_cast<Ctx *>( u );
			quint64 k = ( quint64( a.ip[0] ) << 40 ) | ( quint64( a.ip[1] ) << 32 ) | ( quint64( a.ip[2] ) << 24 )
				| ( quint64( a.ip[3] ) << 16 ) | quint64( a.port );
			c->seen.insert( k );
		}, &ctx );
	return rc < 0 ? -1 : int( ctx.seen.size() );
}

}

/* The row delegate: slot label, address, status. */
class MasterDelegate : public QStyledItemDelegate {
public:
	std::function<int( const QString & )> answer;			/* -2 unknown, -1 no answer, n */
	std::function<bool( const QString & )> checking;
	std::function<void( int row, const QString &text )> ended;
	QSize sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const override { return QSize( 100, px( 30 ) ); }

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
		const QColor txt = sel ? opt.palette.color( QPalette::HighlightedText ) : opt.palette.color( QPalette::Text );
		const QColor sec = sel ? txt : secondary();
		const QRect r = opt.rect.adjusted( px( 6 ), 0, -px( 6 ), 0 );
		const int row = idx.row();
		const QString addr = idx.data( Qt::DisplayRole ).toString();
		/* slot label */
		p->setFont( Ui::font( 12 ) );
		p->setPen( sec );
		p->drawText( QRect( r.left(), r.top(), px( 92 ), r.height() ), Qt::AlignVCenter | Qt::AlignLeft,
			row < 5 ? QString( "Internet %1" ).arg( row + 1 ) : QString( "Announce only" ) );
		/* status */
		const int sw = px( 128 );
		QRect sr( r.right() - sw, r.top(), sw, r.height() );
		QString st;
		QColor sc = sec;
		QString ic;
		QColor icc;
		if ( !addr.isEmpty() ) {
			if ( checking && checking( addr ) ) {
				st = QString::fromUtf8( "Checking\xe2\x80\xa6" );
				ic = "arrow.triangle.2.circlepath";
				icc = sec;
			} else {
				const int a = answer ? answer( addr ) : -2;
				if ( a >= 0 ) {
					st = a == 1 ? "Answers \xc2\xb7 1 game" : QString( "Answers \xc2\xb7 %1 games" ).arg( a );
					ic = "check.circle.fill";
					icc = green();
				} else if ( a == -1 ) {
					st = "No answer";
					sc = sel ? txt : orange();
					ic = "xmark.circle.fill";
					icc = orange();
				}
			}
		}
		p->setFont( Ui::font( 11 ) );
		p->setPen( sc );
		p->drawText( sr, Qt::AlignVCenter | Qt::AlignRight, QString::fromUtf8( st.toUtf8() ) );
		if ( !ic.isEmpty() ) {
			const int tw = QFontMetrics( Ui::font( 11 ) ).horizontalAdvance( st );
			const QPixmap pm = iconPixmap( ic, sel ? txt : icc, 12 );
			p->drawPixmap( sr.right() - tw - px( 18 ), r.center().y() - px( 6 ), pm );
		}
		/* address */
		const int ax = r.left() + px( 92 ) + px( 8 );
		const int aw = sr.left() - px( 22 ) - ax;
		p->setFont( Ui::font( 13, QFont::Medium ) );
		if ( addr.isEmpty() ) {
			p->setPen( sel ? txt : tertiary() );
			p->drawText( QRect( ax, r.top(), aw, r.height() ), Qt::AlignVCenter | Qt::AlignLeft, "host or host:port" );
		} else {
			p->setPen( txt );
			p->drawText( QRect( ax, r.top(), aw, r.height() ), Qt::AlignVCenter | Qt::AlignLeft,
				QFontMetrics( p->font() ).elidedText( addr, Qt::ElideRight, aw ) );
		}
		p->restore();
	}

	QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &, const QModelIndex & ) const override
	{
		QLineEdit *e = new QLineEdit( parent );
		e->setPlaceholderText( "host or host:port" );
		e->setFont( Ui::font( 13, QFont::Medium ) );
		return e;
	}
	void setEditorData( QWidget *e, const QModelIndex &idx ) const override
	{
		static_cast<QLineEdit *>( e )->setText( idx.data( Qt::DisplayRole ).toString() );
		static_cast<QLineEdit *>( e )->selectAll();
	}
	void updateEditorGeometry( QWidget *e, const QStyleOptionViewItem &opt, const QModelIndex & ) const override
	{
		const QRect r = opt.rect.adjusted( px( 6 ) + px( 92 ) + px( 8 ), px( 2 ), -px( 6 ) - px( 128 ) - px( 22 ), -px( 2 ) );
		e->setGeometry( r );
	}
	void setModelData( QWidget *e, QAbstractItemModel *, const QModelIndex &idx ) const override
	{
		const QString t = static_cast<QLineEdit *>( e )->text();
		const int row = idx.row();
		QTimer::singleShot( 0, qApp, [this, row, t]() { if ( ended ) ended( row, t ); } );
	}
};

struct MasterDialog::Impl {
	MasterDialog *q;
	QStringList masters;
	QHash<QString, int> answers;
	QSet<QString> checkingSet;
	QListWidget *list;
	MasterDelegate *delegate;
	QLabel *caption;
	QToolButton *add, *rm, *up, *down;
	QString editingWas;
	int editingRow = -1;

	void reload( const QList<int> &select = QList<int>() )
	{
		QSignalBlocker b( list );
		list->clear();
		for ( const QString &m : masters ) {
			QListWidgetItem *it = new QListWidgetItem( m );
			it->setFlags( it->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled );
			list->addItem( it );
		}
		for ( int i : select )
			if ( i >= 0 && i < list->count() )
				list->item( i )->setSelected( true );
		caption->setText( QString( "MASTER SERVERS \xc2\xb7 %1" ).arg( masters.size() ) );
		for ( int i = 0; i < list->count(); ++i ) {
			const int a = answers.value( masters[i], -2 );
			list->item( i )->setToolTip( i < 5 ? QString( "Holomatch's Join menu lists this master's games under Internet %1" ).arg( i + 1 )
				: QString( "Not in the Join menu (it has Internet 1 to 5), but a server you host announces itself here too" ) );
			if ( a == -1 )
				list->item( i )->setToolTip( "It didn't answer from this network. It may be down, or reachable only over IPv6. "
					"Games it lists still show up for players who can reach it." );
		}
		list->viewport()->update();
		buttons();
	}

	QList<int> selection() const
	{
		QList<int> s;
		for ( QListWidgetItem *it : list->selectedItems() )
			s << list->row( it );
		std::sort( s.begin(), s.end() );
		return s;
	}

	void buttons()
	{
		const QList<int> s = selection();
		add->setEnabled( masters.size() < LC_MAX_MASTERS );
		rm->setEnabled( !s.isEmpty() );
		up->setEnabled( !s.isEmpty() && s.first() > 0 );
		down->setEnabled( !s.isEmpty() && s.last() < masters.size() - 1 );
	}

	void check( const QString &m )
	{
		if ( m.isEmpty() || checkingSet.contains( m ) )
			return;
		checkingSet.insert( m );
		answers.remove( m );
		list->viewport()->update();
		auto *n = new int( -1 );
		async( q, [m, n]() { *n = masterCount( m ); }, [this, m, n]() {
			checkingSet.remove( m );
			answers[m] = *n;
			log( *n < 0 ? QString( "Master %1: no answer" ).arg( m ) : QString( "Master %1: %2 servers" ).arg( m ).arg( *n ) );
			delete n;
			reload( selection() );
		} );
	}

	void checkAll( bool onlyUnanswered )
	{
		for ( const QString &m : masters )
			if ( !onlyUnanswered || !answers.contains( m ) )
				check( m );
	}

	void endEdit( int row, const QString &text )
	{
		const QString was = editingWas;
		editingRow = -1;
		char clean[65];
		lc_clean_master( B( text ).constData(), clean );
		const QString m = U( clean );
		/* find the row by its address before editing (rows can't move while editing here) */
		if ( row < 0 || row >= masters.size() ) {
			reload();
			return;
		}
		if ( m.isEmpty() ) {
			masters.removeAt( row );
		} else if ( masters.contains( m ) && masters.indexOf( m ) != row ) {
			QApplication::beep();
			if ( was.isEmpty() )
				masters.removeAt( row );
		} else
			masters[row] = m;
		reload( row < masters.size() ? QList<int>() << row : QList<int>() );
		if ( !m.isEmpty() && masters.contains( m ) && !answers.contains( m ) )
			check( m );
	}

	void commitEdit()
	{
		if ( QWidget *f = QApplication::focusWidget() )
			if ( QLineEdit *e = qobject_cast<QLineEdit *>( f ) )
				if ( list->isAncestorOf( e ) )
					e->clearFocus();
	}

	void edit( int row )
	{
		if ( row < 0 || row >= masters.size() )
			return;
		editingRow = row;
		editingWas = masters[row];
		list->clearSelection();
		list->item( row )->setSelected( true );
		list->scrollToItem( list->item( row ) );
		list->editItem( list->item( row ) );
	}

	void move( int delta )
	{
		commitEdit();
		const QList<int> sel = selection();
		if ( sel.isEmpty() )
			return;
		QStringList block;
		for ( int i : sel )
			block << masters[i];
		for ( int i = sel.size() - 1; i >= 0; --i )
			masters.removeAt( sel[i] );
		int at = delta < 0 ? sel.first() - 1 : sel.last() + 2 - int( sel.size() );
		at = qBound( 0, at, int( masters.size() ) );
		QList<int> ns;
		for ( int i = 0; i < block.size(); ++i ) {
			masters.insert( at + i, block[i] );
			ns << at + i;
		}
		reload( ns );
	}

	void removeSelected()
	{
		commitEdit();
		const QList<int> sel = selection();
		if ( sel.isEmpty() )
			return;
		for ( int i = sel.size() - 1; i >= 0; --i )
			masters.removeAt( sel[i] );
		reload( masters.isEmpty() ? QList<int>() : QList<int>() << qMin( sel.first(), int( masters.size() ) - 1 ) );
	}
};

MasterDialog::MasterDialog( QWidget *parent ) : QDialog( parent ), d( new Impl )
{
	d->q = this;
	setWindowTitle( "Master Servers" );
	setModal( true );
	setFixedWidth( px( 660 ) );

	char list[LC_MAX_MASTERS][65];
	const int n = lc_masters_load( lc_store_shared(), list );
	for ( int i = 0; i < n; ++i )
		d->masters << U( list[i] );

	QVBoxLayout *root = new QVBoxLayout( this );
	root->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 20 ) );
	root->setSpacing( px( 8 ) );
	d->caption = label( QString(), 11, QFont::DemiBold, secondary() );
	root->addWidget( d->caption );
	root->addWidget( label( "Master servers keep the list of Holomatch games on the internet. The first five are the Join menu's Internet 1 to 5 "
		"sources, in this order. When you host an internet server, it announces itself to every one, so add new masters here as the "
		"community starts them.", 11, QFont::Normal, secondary(), 610 ) );
	root->addSpacing( px( 2 ) );

	d->list = new QListWidget;
	styleView( d->list );
	d->list->setSelectionMode( QAbstractItemView::ExtendedSelection );
	d->list->setDragEnabled( true );
	d->list->setAcceptDrops( true );
	d->list->setDragDropMode( QAbstractItemView::InternalMove );
	d->list->setDefaultDropAction( Qt::MoveAction );
	d->list->setEditTriggers( QAbstractItemView::NoEditTriggers );
	d->list->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
	d->delegate = new MasterDelegate;
	d->delegate->answer = [this]( const QString &a ) { return d->answers.value( a, -2 ); };
	d->delegate->checking = [this]( const QString &a ) { return d->checkingSet.contains( a ); };
	d->delegate->ended = [this]( int row, const QString &t ) { d->endEdit( row, t ); };
	d->list->setItemDelegate( d->delegate );
	Panel *box = new Panel;
	QVBoxLayout *bv = new QVBoxLayout( box );
	bv->setContentsMargins( 2, 2, 2, 2 );
	bv->addWidget( d->list );
	box->setFixedHeight( px( 280 ) );
	root->addWidget( box );

	QHBoxLayout *br = new QHBoxLayout;
	d->add = iconButton( "plus", "Add a master server" );
	d->rm = iconButton( "minus", "Remove" );
	d->up = iconButton( "chevron.up", "Move up" );
	d->down = iconButton( "chevron.down", "Move down" );
	br->addWidget( d->add );
	br->addWidget( d->rm );
	br->addWidget( d->up );
	br->addWidget( d->down );
	br->addSpacing( px( 12 ) );
	br->addWidget( label( "Drag to reorder, double-click to edit", 11, QFont::Normal, tertiary() ) );
	br->addStretch( 1 );
	QPushButton *checkNow = button( "Check Now", "Ask each master server for its list of games" );
	br->addWidget( checkNow );
	root->addLayout( br );
	root->addSpacing( px( 18 ) );

	QHBoxLayout *foot = new QHBoxLayout;
	QStringList rec;
	for ( int i = 0; i < 5; ++i )
		rec << U( lc_default_masters[i] );
	QPushButton *recommended = button( "Restore Recommended Defaults", "The live community masters first: " + rec.join( ", " ) );
	QPushButton *original = button( "Restore Original Defaults",
		"As the game came: Internet 1 Raven's master (no longer running), Internet 2 efmaster.tjps.eu, 3 to 5 blank" );
	QPushButton *cancel = new QPushButton( "Cancel" );
	QPushButton *done = new QPushButton( "Done" );
	cancel->setMinimumWidth( px( 80 ) );
	done->setMinimumWidth( px( 80 ) );
	done->setDefault( true );
	foot->addWidget( recommended );
	foot->addWidget( original );
	foot->addStretch( 1 );
	foot->addWidget( cancel );
	foot->addWidget( done );
	root->addLayout( foot );

	connect( d->list, &QListWidget::itemSelectionChanged, this, [this]() { d->buttons(); } );
	connect( d->list, &QListWidget::itemDoubleClicked, this, [this]( QListWidgetItem *it ) { d->edit( d->list->row( it ) ); } );
	connect( d->list->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
		QStringList l;
		for ( int i = 0; i < d->list->count(); ++i )
			l << d->list->item( i )->text();
		d->masters = l;
		QList<int> sel = d->selection();
		QTimer::singleShot( 0, this, [this, sel]() { d->reload( sel ); } );
	} );
	connect( d->add, &QToolButton::clicked, this, [this]() {
		if ( d->masters.size() >= LC_MAX_MASTERS )
			return;
		d->commitEdit();
		d->masters << QString();
		d->reload();
		d->edit( int( d->masters.size() ) - 1 );
	} );
	connect( d->rm, &QToolButton::clicked, this, [this]() { d->removeSelected(); } );
	connect( d->up, &QToolButton::clicked, this, [this]() { d->move( -1 ); } );
	connect( d->down, &QToolButton::clicked, this, [this]() { d->move( +1 ); } );
	connect( checkNow, &QPushButton::clicked, this, [this]() { d->commitEdit(); d->checkAll( false ); } );
	connect( recommended, &QPushButton::clicked, this, [this]() {
		d->commitEdit();
		d->masters.clear();
		for ( int i = 0; i < 5; ++i )
			d->masters << U( lc_default_masters[i] );
		d->reload();
		d->checkAll( true );
	} );
	connect( original, &QPushButton::clicked, this, [this]() {
		d->commitEdit();
		d->masters.clear();
		for ( int i = 0; i < 2; ++i )
			d->masters << U( lc_original_masters[i] );
		d->reload();
		d->checkAll( true );
	} );
	connect( cancel, &QPushButton::clicked, this, &QDialog::reject );
	connect( done, &QPushButton::clicked, this, [this]() {
		d->commitEdit();
		QList<QByteArray> bs;
		for ( const QString &m : d->masters )
			bs << B( m );
		QVector<const char *> ptrs;
		for ( const QByteArray &b : bs )
			ptrs << b.constData();
		char clean[LC_MAX_MASTERS][65];
		const int c = lc_clean_masters( ptrs.constData(), int( ptrs.size() ), clean );
		lc_masters_save( lc_store_shared(), clean, c );
		lc_store_save( lc_store_shared() );
		QStringList l;
		for ( int i = 0; i < c; ++i )
			l << U( clean[i] );
		log( "Master servers: " + l.join( ", " ) );
		accept();
	} );

	d->reload();
	d->checkAll( false );
	done->setFocus();
}

MasterDialog::~MasterDialog() { delete d; }

void MasterDialog::waitForChecks( int msTimeout )
{
	QElapsedTimer t;
	t.start();
	while ( !d->checkingSet.isEmpty() && t.elapsed() < msTimeout ) {
		QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
		lc_sleep_ms( 10 );
	}
}

void MasterDialog::testAction( const QString &a )
{
	auto rowOf = [&]( const QString &s, const QString &prefix ) { return s.mid( prefix.size() ).toInt(); };
	if ( a == "add" ) {
		d->add->click();
	} else if ( a.startsWith( "dclick" ) || a.startsWith( "edit" ) ) {
		d->edit( rowOf( a, a.startsWith( "dclick" ) ? "dclick" : "edit" ) );
	} else if ( a.startsWith( "select" ) ) {
		const int r = rowOf( a, "select" );
		d->list->clearSelection();
		if ( r >= 0 && r < d->list->count() )
			d->list->item( r )->setSelected( true );
	} else if ( a.startsWith( "type:" ) ) {
		if ( QLineEdit *e = d->list->findChild<QLineEdit *>() )
			e->setText( a.mid( 5 ) );
	} else if ( a == "end" ) {
		d->commitEdit();
	} else if ( a.startsWith( "drag" ) ) {
		const QStringList p = a.mid( 4 ).split( "to" );
		if ( p.size() == 2 ) {
			const int from = p[0].toInt(), to = p[1].toInt();
			if ( from >= 0 && from < d->masters.size() ) {
				const QString m = d->masters.takeAt( from );
				d->masters.insert( qBound( 0, to, int( d->masters.size() ) ), m );
				d->reload( QList<int>() << qBound( 0, to, int( d->masters.size() ) - 1 ) );
			}
		}
	}
	QCoreApplication::processEvents();
	QStringList shown;
	for ( int i = 0; i < d->masters.size(); ++i )
		shown << QString( "%1=%2" ).arg( i < 5 ? QString( "Internet %1" ).arg( i + 1 ) : QString( "Announce only" ), d->masters[i] );
	QStringList sel;
	for ( int i : d->selection() )
		sel << QString::number( i );
	log( QString( "[test] %1 -> %2 | shown: %3 | selected %4" ).arg( a, d->masters.join( " | " ), shown.join( ", " ), sel.join( "," ) ) );
}
