/*
===========================================================================
ef-launcher (Qt 6): shared UI helpers. See ui_common.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "ui_common.h"

#include <QThread>
#include <cmath>

namespace Ui {

void log( const QString &line )
{
	lc_log( "%s", line.toUtf8().constData() );
}

QString testEnv( const char *name )
{
	const char *v = lc_env( name );
	return v ? QString::fromUtf8( v ) : QString();
}

bool testSet( const char *name )
{
	return lc_test_env( name ) != 0;
}

/* ---- Deck ---------------------------------------------------------------------------- */

bool deck()
{
	static int d = -1;
	if ( d < 0 )
		d = ( lc_is_steam_deck() || testSet( "VOYAGERSP_TEST_DECK" ) ) ? 1 : 0;
	return d == 1;
}

double scale()
{
	return deck() ? 1.3 : 1.0;
}

/* ---- colours ---------------------------------------------------------------------------- */

bool dark()
{
	return QGuiApplication::palette().color( QPalette::Window ).lightness() < 128;
}

static QColor mix( const QColor &a, const QColor &b, double t )
{
	return QColor::fromRgbF( a.redF() * ( 1 - t ) + b.redF() * t, a.greenF() * ( 1 - t ) + b.greenF() * t,
		a.blueF() * ( 1 - t ) + b.blueF() * t );
}

QColor secondary()
{
	const QPalette p = QGuiApplication::palette();
	return mix( p.color( QPalette::WindowText ), p.color( QPalette::Window ), 0.38 );
}
QColor tertiary()
{
	const QPalette p = QGuiApplication::palette();
	return mix( p.color( QPalette::WindowText ), p.color( QPalette::Window ), 0.58 );
}
QColor quaternary()
{
	const QPalette p = QGuiApplication::palette();
	return mix( p.color( QPalette::WindowText ), p.color( QPalette::Window ), 0.78 );
}
QColor accent() { return QGuiApplication::palette().color( QPalette::Highlight ); }
QColor orange() { return dark() ? QColor( 255, 159, 10 ) : QColor( 201, 100, 0 ); }
QColor green() { return dark() ? QColor( 48, 209, 88 ) : QColor( 36, 138, 61 ); }
QColor red() { return dark() ? QColor( 255, 105, 97 ) : QColor( 200, 36, 36 ); }
QColor amber() { return dark() ? QColor( 255, 214, 10 ) : QColor::fromRgbF( 0.70, 0.47, 0.0 ); }
QColor purple() { return dark() ? QColor( 218, 143, 255 ) : QColor( 134, 58, 190 ); }
QColor blue() { return dark() ? QColor( 100, 170, 255 ) : QColor( 0, 98, 214 ); }
QColor yellow() { return dark() ? QColor( 255, 214, 10 ) : QColor( 214, 160, 0 ); }
QColor teal() { return dark() ? QColor( 100, 210, 230 ) : QColor( 0, 140, 160 ); }
QColor pink() { return dark() ? QColor( 255, 110, 160 ) : QColor( 200, 40, 100 ); }

QColor groupFill()
{
	return dark() ? QColor( 255, 255, 255, 13 ) : QColor( 0, 0, 0, 9 );
}
QColor separator()
{
	return dark() ? QColor( 255, 255, 255, 40 ) : QColor( 0, 0, 0, 40 );
}

QColor q3Color( int idx )
{
	switch ( idx & 7 ) {
	case 0: return secondary();
	case 1: return red();
	case 2: return green();
	case 3: return yellow();
	case 4: return blue();
	case 5: return teal();
	case 6: return pink();
	default: return QGuiApplication::palette().color( QPalette::WindowText );
	}
}

void setColor( QWidget *w, const QColor &c )
{
	if ( !c.isValid() )
		return;
	QPalette p = w->palette();
	p.setColor( QPalette::WindowText, c );
	p.setColor( QPalette::Text, c );
	p.setColor( QPalette::ButtonText, c );
	w->setPalette( p );
}

/* ---- widgets ------------------------------------------------------------------------------ */

QFont font( double pt, QFont::Weight w, bool mono )
{
	QFont f = QGuiApplication::font();
	if ( mono ) {
		f = QFontDatabase::systemFont( QFontDatabase::FixedFont );
	}
	f.setPixelSize( int( std::lround( pt * scale() ) ) );
	f.setWeight( w );
	return f;
}

QLabel *label( const QString &text, double pt, QFont::Weight w, const QColor &c, int wrapWidth )
{
	QLabel *l = new QLabel( text );
	l->setFont( font( pt, w ) );
	setColor( l, c );
	if ( wrapWidth > 0 ) {
		l->setWordWrap( true );
		l->setMaximumWidth( int( wrapWidth * scale() ) );
		l->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Minimum );
	}
	l->setTextInteractionFlags( Qt::NoTextInteraction );
	return l;
}

namespace {
class ElideLabel : public QLabel {
public:
	explicit ElideLabel( Qt::TextElideMode m ) : m_mode( m ) {}
	QSize minimumSizeHint() const override { return QSize( 0, QLabel::sizeHint().height() ); }
	QSize sizeHint() const override { return QSize( QLabel::sizeHint().width(), QLabel::sizeHint().height() ); }
protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter p( this );
		p.setFont( font() );
		p.setPen( palette().color( foregroundRole() ) );
		const QFontMetrics fm( font() );
		p.drawText( rect(), int( alignment() ) | Qt::AlignVCenter, fm.elidedText( text(), m_mode, width() ) );
	}
private:
	Qt::TextElideMode m_mode;
};
}

QLabel *elidedLabel( const QString &text, double pt, QFont::Weight w, const QColor &c, Qt::TextElideMode mode )
{
	QLabel *l = new ElideLabel( mode );
	l->setText( text );
	l->setFont( font( pt, w ) );
	setColor( l, c );
	l->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
	l->setTextInteractionFlags( Qt::NoTextInteraction );
	return l;
}

QPushButton *button( const QString &text, const QString &tooltip )
{
	QPushButton *b = new QPushButton( text );
	if ( !tooltip.isEmpty() )
		b->setToolTip( tooltip );
	b->setAutoDefault( false );
	return b;
}

QToolButton *iconButton( const QString &ic, const QString &tooltip, int iconPx )
{
	QToolButton *b = new QToolButton;
	b->setIcon( icon( ic, QColor(), iconPx ) );
	b->setIconSize( QSize( px( iconPx ), px( iconPx ) ) );
	b->setToolTip( tooltip );
	b->setAccessibleName( tooltip );
	b->setAutoRaise( false );
	b->setFocusPolicy( Qt::StrongFocus );
	return b;
}

QFrame *hline()
{
	QFrame *f = new QFrame;
	f->setFrameShape( QFrame::HLine );
	f->setFrameShadow( QFrame::Plain );
	return f;
}

/* ---- icons: simple vector shapes standing in for SF Symbols ------------------------------ */

static void paintShape( QPainter &p, const QString &n, const QColor &c )
{
	/* drawing space 0..16 */
	p.setRenderHint( QPainter::Antialiasing );
	QPen pen( c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
	p.setPen( pen );
	p.setBrush( Qt::NoBrush );
	const QColor bg = QGuiApplication::palette().color( QPalette::Window );
	auto disc = [&]( const QColor &col ) {
		p.setPen( Qt::NoPen );
		p.setBrush( col );
		p.drawEllipse( QRectF( 1, 1, 14, 14 ) );
		p.setBrush( Qt::NoBrush );
		p.setPen( QPen( bg.lightness() > 128 ? QColor( Qt::white ) : QColor( Qt::black ), 1.8,
			Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
	};
	if ( n == "check.circle.fill" ) {
		disc( c );
		p.drawPolyline( QPolygonF() << QPointF( 4.6, 8.3 ) << QPointF( 7, 10.7 ) << QPointF( 11.4, 5.6 ) );
	} else if ( n == "xmark.circle.fill" ) {
		disc( c );
		p.drawLine( QPointF( 5.3, 5.3 ), QPointF( 10.7, 10.7 ) );
		p.drawLine( QPointF( 10.7, 5.3 ), QPointF( 5.3, 10.7 ) );
	} else if ( n == "exclamationmark.circle.fill" ) {
		disc( c );
		p.drawLine( QPointF( 8, 4.2 ), QPointF( 8, 9 ) );
		p.drawPoint( QPointF( 8, 11.5 ) );
	} else if ( n == "exclamationmark.triangle.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawPolygon( QPolygonF() << QPointF( 8, 1.5 ) << QPointF( 15.2, 14 ) << QPointF( 0.8, 14 ) );
		p.setBrush( Qt::NoBrush );
		p.setPen( QPen( bg.lightness() > 128 ? QColor( Qt::white ) : QColor( Qt::black ), 1.8,
			Qt::SolidLine, Qt::RoundCap ) );
		p.drawLine( QPointF( 8, 6 ), QPointF( 8, 10 ) );
		p.drawPoint( QPointF( 8, 12 ) );
	} else if ( n == "circle.dotted" ) {
		pen.setStyle( Qt::DotLine );
		pen.setWidthF( 1.8 );
		p.setPen( pen );
		p.drawEllipse( QRectF( 2, 2, 12, 12 ) );
	} else if ( n == "circle.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawEllipse( QRectF( 3, 3, 10, 10 ) );
	} else if ( n == "arrow.clockwise" || n == "arrow.triangle.2.circlepath" ) {
		p.drawArc( QRectF( 2.5, 2.5, 11, 11 ), 40 * 16, 270 * 16 );
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawPolygon( QPolygonF() << QPointF( 12.5, 1.5 ) << QPointF( 13.8, 6.4 ) << QPointF( 8.8, 5.2 ) );
	} else if ( n == "arrow.counterclockwise" ) {
		p.drawArc( QRectF( 2.5, 2.5, 11, 11 ), 230 * 16, 270 * 16 );
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawPolygon( QPolygonF() << QPointF( 3.5, 1.5 ) << QPointF( 2.2, 6.4 ) << QPointF( 7.2, 5.2 ) );
	} else if ( n == "star" || n == "star.fill" ) {
		QPolygonF s;
		for ( int i = 0; i < 10; ++i ) {
			const double a = -M_PI / 2 + i * M_PI / 5, r = ( i & 1 ) ? 3.4 : 7.2;
			s << QPointF( 8 + r * std::cos( a ), 8.4 + r * std::sin( a ) );
		}
		if ( n == "star.fill" ) {
			p.setBrush( c );
			p.setPen( Qt::NoPen );
		} else
			pen.setWidthF( 1.2 );
		if ( n == "star" )
			p.setPen( pen );
		p.drawPolygon( s );
	} else if ( n == "lock.fill" ) {
		p.setBrush( c );
		p.setPen( Qt::NoPen );
		p.drawRoundedRect( QRectF( 3, 7, 10, 7.5 ), 1.5, 1.5 );
		p.setBrush( Qt::NoBrush );
		p.setPen( QPen( c, 1.6 ) );
		p.drawArc( QRectF( 4.8, 2, 6.4, 8 ), 0, 180 * 16 );
		p.drawLine( QPointF( 4.8, 6 ), QPointF( 4.8, 7.5 ) );
		p.drawLine( QPointF( 11.2, 6 ), QPointF( 11.2, 7.5 ) );
	} else if ( n == "plus" ) {
		p.drawLine( QPointF( 3, 8 ), QPointF( 13, 8 ) );
		p.drawLine( QPointF( 8, 3 ), QPointF( 8, 13 ) );
	} else if ( n == "minus" ) {
		p.drawLine( QPointF( 3, 8 ), QPointF( 13, 8 ) );
	} else if ( n == "chevron.up" ) {
		p.drawPolyline( QPolygonF() << QPointF( 3.5, 10.5 ) << QPointF( 8, 5.5 ) << QPointF( 12.5, 10.5 ) );
	} else if ( n == "chevron.down" ) {
		p.drawPolyline( QPolygonF() << QPointF( 3.5, 5.5 ) << QPointF( 8, 10.5 ) << QPointF( 12.5, 5.5 ) );
	} else if ( n == "globe" ) {
		pen.setWidthF( 1.3 );
		p.setPen( pen );
		p.drawEllipse( QRectF( 1.5, 1.5, 13, 13 ) );
		p.drawEllipse( QRectF( 5, 1.5, 6, 13 ) );
		p.drawLine( QPointF( 1.5, 8 ), QPointF( 14.5, 8 ) );
	} else if ( n == "play.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawPolygon( QPolygonF() << QPointF( 4, 2.5 ) << QPointF( 13, 8 ) << QPointF( 4, 13.5 ) );
	} else if ( n == "forward.end.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawPolygon( QPolygonF() << QPointF( 2.5, 3 ) << QPointF( 11, 8 ) << QPointF( 2.5, 13 ) );
		p.drawRect( QRectF( 11.8, 3, 2, 10 ) );
	} else if ( n == "person.badge.plus" ) {
		p.setBrush( c );
		p.setPen( Qt::NoPen );
		p.drawEllipse( QRectF( 3, 2, 6, 6 ) );
		p.drawRoundedRect( QRectF( 1.5, 9, 9, 5 ), 2.5, 2.5 );
		p.setPen( QPen( c, 1.4 ) );
		p.drawLine( QPointF( 10.5, 4 ), QPointF( 14.5, 4 ) );
		p.drawLine( QPointF( 12.5, 2 ), QPointF( 12.5, 6 ) );
	} else if ( n == "person.2.slash" ) {
		p.setBrush( c );
		p.setPen( Qt::NoPen );
		p.drawEllipse( QRectF( 1.5, 2.5, 5, 5 ) );
		p.drawEllipse( QRectF( 9, 2.5, 5, 5 ) );
		p.drawRoundedRect( QRectF( 0.5, 9, 7, 5 ), 2.5, 2.5 );
		p.drawRoundedRect( QRectF( 8.5, 9, 7, 5 ), 2.5, 2.5 );
		p.setPen( QPen( bg, 3 ) );
		p.drawLine( QPointF( 2, 14 ), QPointF( 14, 2 ) );
		p.setPen( QPen( c, 1.4 ) );
		p.drawLine( QPointF( 2, 14 ), QPointF( 14, 2 ) );
	} else if ( n == "text.bubble" ) {
		p.drawRoundedRect( QRectF( 1.8, 2, 12.4, 9 ), 2.5, 2.5 );
		p.drawPolyline( QPolygonF() << QPointF( 5, 11 ) << QPointF( 4.5, 14 ) << QPointF( 8, 11 ) );
	} else if ( n == "map" ) {
		p.drawPolygon( QPolygonF() << QPointF( 1.5, 3.5 ) << QPointF( 5.5, 2 ) << QPointF( 10.5, 3.8 )
			<< QPointF( 14.5, 2.5 ) << QPointF( 14.5, 12.5 ) << QPointF( 10.5, 14 ) << QPointF( 5.5, 12.2 )
			<< QPointF( 1.5, 13.5 ) );
		p.drawLine( QPointF( 5.5, 2 ), QPointF( 5.5, 12.2 ) );
		p.drawLine( QPointF( 10.5, 3.8 ), QPointF( 10.5, 14 ) );
	} else if ( n == "questionmark" ) {
		p.drawArc( QRectF( 4.5, 2, 7, 6.5 ), 160 * 16, -230 * 16 );
		p.drawLine( QPointF( 8, 8.6 ), QPointF( 8, 10 ) );
		p.drawPoint( QPointF( 8, 13 ) );
	} else if ( n == "person.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawEllipse( QRectF( 4.5, 1.5, 7, 7 ) );
		p.drawRoundedRect( QRectF( 2.5, 9.5, 11, 5 ), 2.5, 2.5 );
	} else if ( n == "person.3.fill" ) {
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawEllipse( QRectF( 5.8, 1.5, 4.4, 4.4 ) );
		p.drawRoundedRect( QRectF( 4.5, 6.5, 7, 4.5 ), 2, 2 );
		p.drawEllipse( QRectF( 0.8, 4.5, 3.6, 3.6 ) );
		p.drawRoundedRect( QRectF( 0, 8.8, 5.5, 4.2 ), 1.8, 1.8 );
		p.drawEllipse( QRectF( 11.6, 4.5, 3.6, 3.6 ) );
		p.drawRoundedRect( QRectF( 10.5, 8.8, 5.5, 4.2 ), 1.8, 1.8 );
	} else if ( n == "server.rack" ) {
		p.drawRoundedRect( QRectF( 1.8, 1.8, 12.4, 5 ), 1.5, 1.5 );
		p.drawRoundedRect( QRectF( 1.8, 9.2, 12.4, 5 ), 1.5, 1.5 );
		p.setPen( Qt::NoPen );
		p.setBrush( c );
		p.drawEllipse( QRectF( 4, 3.6, 1.4, 1.4 ) );
		p.drawEllipse( QRectF( 4, 11, 1.4, 1.4 ) );
	} else if ( n == "puzzlepiece.extension.fill" || n == "puzzlepiece.extension" ) {
		QPainterPath pp;
		pp.addRoundedRect( QRectF( 2, 5, 11, 9 ), 1.5, 1.5 );
		pp.addEllipse( QRectF( 5.5, 1.5, 4, 4 ) );
		pp.addEllipse( QRectF( 11.5, 7.5, 4, 4 ) );
		if ( n.endsWith( "fill" ) ) {
			p.setPen( Qt::NoPen );
			p.setBrush( c );
			p.drawPath( pp.simplified() );
		} else {
			pen.setWidthF( 1.3 );
			p.setPen( pen );
			p.drawPath( pp.simplified() );
		}
	} else if ( n == "terminal" ) {
		p.drawRoundedRect( QRectF( 1.5, 2.5, 13, 11 ), 2, 2 );
		p.drawPolyline( QPolygonF() << QPointF( 4.5, 6 ) << QPointF( 7, 8 ) << QPointF( 4.5, 10 ) );
		p.drawLine( QPointF( 8.5, 10.4 ), QPointF( 11.5, 10.4 ) );
	} else if ( n == "folder" ) {
		p.setBrush( c );
		p.setPen( Qt::NoPen );
		p.drawRoundedRect( QRectF( 1, 4, 14, 10 ), 1.5, 1.5 );
		p.drawRoundedRect( QRectF( 1, 2.5, 6, 3.5 ), 1, 1 );
	} else if ( n == "arrow.down.circle.fill" ) {
		disc( c );
		p.drawLine( QPointF( 8, 4.5 ), QPointF( 8, 11 ) );
		p.drawPolyline( QPolygonF() << QPointF( 5, 8.3 ) << QPointF( 8, 11.3 ) << QPointF( 11, 8.3 ) );
	} else if ( n == "gear" ) {
		p.drawEllipse( QRectF( 5, 5, 6, 6 ) );
		for ( int i = 0; i < 8; ++i ) {
			const double a = i * M_PI / 4;
			p.drawLine( QPointF( 8 + 4.2 * std::cos( a ), 8 + 4.2 * std::sin( a ) ),
				QPointF( 8 + 6.6 * std::cos( a ), 8 + 6.6 * std::sin( a ) ) );
		}
	} else {
		p.drawRect( QRectF( 3, 3, 10, 10 ) );
	}
}

QPixmap iconPixmap( const QString &name, const QColor &col, int pxSize )
{
	const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
	const int s = int( std::lround( pxSize * scale() * dpr ) );
	QPixmap pm( s, s );
	pm.fill( Qt::transparent );
	pm.setDevicePixelRatio( dpr );
	QPainter p( &pm );
	p.scale( s / dpr / 16.0, s / dpr / 16.0 );
	paintShape( p, name, col.isValid() ? col : QGuiApplication::palette().color( QPalette::ButtonText ) );
	return pm;
}

QIcon icon( const QString &name, const QColor &c, int pxSize )
{
	return QIcon( iconPixmap( name, c, pxSize ) );
}

/* ---- Group / Panel ------------------------------------------------------------------------- */

Panel::Panel( QWidget *parent, int radius ) : QFrame( parent ), m_radius( radius )
{
	setFrameShape( QFrame::NoFrame );
}

void Panel::paintEvent( QPaintEvent *e )
{
	QPainter p( this );
	p.setRenderHint( QPainter::Antialiasing );
	QRectF r = QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 );
	p.setBrush( groupFill() );
	p.setPen( QPen( separator(), 1 ) );
	p.drawRoundedRect( r, m_radius, m_radius );
	QFrame::paintEvent( e );
}

Group::Group( const QString &caption, QWidget *parent ) : QWidget( parent )
{
	QVBoxLayout *v = new QVBoxLayout( this );
	v->setContentsMargins( 0, 0, 0, 0 );
	v->setSpacing( px( 6 ) );
	m_caption = label( caption.toUpper(), 11, QFont::DemiBold, secondary() );
	m_caption->setContentsMargins( px( 4 ), 0, 0, 0 );
	v->addWidget( m_caption );
	Panel *box = new Panel;
	m_content = new QVBoxLayout( box );
	m_content->setContentsMargins( px( 14 ), px( 12 ), px( 14 ), px( 12 ) );
	m_content->setSpacing( px( 6 ) );
	v->addWidget( box );
}

void Group::setCaption( const QString &c )
{
	m_caption->setText( c.toUpper() );
}

void styleView( QAbstractItemView *v )
{
	v->setFrameShape( QFrame::NoFrame );
	v->setAttribute( Qt::WA_MacShowFocusRect, false );
	QPalette p = v->palette();
	p.setColor( QPalette::Base, Qt::transparent );
	p.setColor( QPalette::AlternateBase, Qt::transparent );
	v->setPalette( p );
	v->viewport()->setAutoFillBackground( false );
	v->setAlternatingRowColors( false );
}

/* ---- alerts --------------------------------------------------------------------------------- */

static QString htmlify( const QString &t )
{
	return t.toHtmlEscaped().replace( '\n', "<br>" );
}

int alert( QWidget *parent, AlertStyle style, const QString &message, const QString &info,
	const QStringList &buttons, int defaultButton, int escape, int destructive )
{
	Q_UNUSED( destructive );
	log( "[dialog] " + message + ( info.isEmpty() ? QString() : QString::fromUtf8( " \xe2\x80\x94 " ) + info ) );
	if ( testSet( "VOYAGERSP_TEST_NODIALOG" ) || testSet( "VOYAGERSP_TEST_CONFIRM" ) )
		return 0;
	QMessageBox box( parent );
	box.setIcon( style == Critical ? QMessageBox::Critical : style == Warning ? QMessageBox::Warning
		: QMessageBox::Information );
	box.setWindowTitle( QStringLiteral( "Elite Force" ) );
	box.setTextFormat( Qt::RichText );
	box.setText( info.isEmpty() ? htmlify( message ) : "<b>" + htmlify( message ) + "</b>" );
	if ( !info.isEmpty() )
		box.setInformativeText( htmlify( info ) );
	QList<QAbstractButton *> bs;
	for ( int i = 0; i < buttons.size(); ++i )
		bs << box.addButton( buttons[i], i == 0 ? QMessageBox::AcceptRole : QMessageBox::RejectRole );
	if ( defaultButton >= 0 && defaultButton < bs.size() )
		box.setDefaultButton( qobject_cast<QPushButton *>( bs[defaultButton] ) );
	const int esc = escape >= 0 ? escape : int( bs.size() ) - 1;
	if ( esc >= 0 && esc < bs.size() )
		box.setEscapeButton( bs[esc] );
	box.setWindowModality( Qt::WindowModal );
	box.exec();
	const int idx = bs.indexOf( box.clickedButton() );
	return idx < 0 ? esc : idx;
}

/* ---- threads ----------------------------------------------------------------------------------- */

void async( QObject *ctx, std::function<void()> work, std::function<void()> done )
{
	QPointer<QObject> guard( ctx );
	QThread *t = QThread::create( [work, done, guard]() {
		work();
		if ( guard ) {
			QMetaObject::invokeMethod( guard.data(), [done, guard]() {
				if ( guard )
					done();
			}, Qt::QueuedConnection );
		}
	} );
	QObject::connect( t, &QThread::finished, t, &QObject::deleteLater );
	t->start();
}

/* ---- snapshots -------------------------------------------------------------------------------- */

bool savePng( QWidget *w, const QString &path )
{
	QPixmap pm = w->grab();
	const bool ok = pm.save( path, "PNG" );
	log( QString( "[test] snapshot %1: %2 (%3x%4)" ).arg( path, ok ? "ok" : "FAILED" )
		.arg( pm.width() ).arg( pm.height() ) );
	return ok;
}

void quitSoon( int ms )
{
	QTimer::singleShot( ms, qApp, []() { QCoreApplication::exit( 0 ); } );
}

QString tilde( const QString &path )
{
	const QString home = U( lc_home_dir() );
	if ( !home.isEmpty() && path.startsWith( home ) )
		return "~" + path.mid( home.size() );
	return path;
}

QString bytes( unsigned long long n )
{
	char buf[64];
	lc_format_bytes( n, buf, sizeof buf );
	return U( buf );
}

}	/* namespace Ui */
