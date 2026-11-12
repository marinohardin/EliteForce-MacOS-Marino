/*
===========================================================================
ef-launcher (Qt 6): small reusable widgets. See widgets.h.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "widgets.h"

namespace Ui {

/* ---- Segmented ------------------------------------------------------------------------ */

Segmented::Segmented( const QStringList &titles, QWidget *parent ) : QWidget( parent )
{
	QHBoxLayout *h = new QHBoxLayout( this );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( 0 );
	for ( int i = 0; i < titles.size(); ++i ) {
		QPushButton *b = new QPushButton( titles[i] );
		b->setCheckable( true );
		b->setAutoDefault( false );
		b->setFocusPolicy( Qt::StrongFocus );
		b->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
		b->setMinimumWidth( 0 );
		h->addWidget( b );
		m_buttons << b;
		connect( b, &QPushButton::clicked, this, [this, i]() { setCurrent( i, true ); } );
	}
}

void Segmented::setCurrent( int i, bool emitSignal )
{
	const bool changed = i != m_current;
	m_current = i;
	for ( int k = 0; k < m_buttons.size(); ++k )
		m_buttons[k]->setChecked( k == i );
	if ( emitSignal && changed )
		emit currentChanged( i );
	else if ( emitSignal )
		emit currentChanged( i );
}

void Segmented::setSegmentEnabled( int i, bool on )
{
	if ( i >= 0 && i < m_buttons.size() )
		m_buttons[i]->setEnabled( on );
}

void Segmented::setSegmentToolTip( int i, const QString &t )
{
	if ( i >= 0 && i < m_buttons.size() )
		m_buttons[i]->setToolTip( t );
}

/* ---- NumberField ------------------------------------------------------------------------- */

NumberField::NumberField( int min, int max, const QString &unit, bool stepper, int fieldWidth,
	QWidget *parent ) : QWidget( parent )
{
	QHBoxLayout *h = new QHBoxLayout( this );
	h->setContentsMargins( 0, 0, 0, 0 );
	h->setSpacing( px( 4 ) );
	m_spin = new QSpinBox;
	m_spin->setRange( min, max );
	m_spin->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
	m_spin->setKeyboardTracking( true );
	m_spin->setWrapping( false );
	if ( !stepper )
		m_spin->setButtonSymbols( QAbstractSpinBox::NoButtons );
	m_spin->setFixedWidth( px( fieldWidth + ( stepper ? 24 : 0 ) ) );
	h->addWidget( m_spin );
	if ( !unit.isEmpty() )
		h->addWidget( label( unit, 13, QFont::Normal, secondary() ) );
	h->addStretch( 1 );
	connect( m_spin, QOverload<int>::of( &QSpinBox::valueChanged ), this,
		[this]( int v ) { emit valueEdited( v ); } );
}

int NumberField::value() const { return m_spin->value(); }

void NumberField::setValue( int v )
{
	QSignalBlocker b( m_spin );
	m_spin->setValue( v );
}

/* ---- ModeCard ------------------------------------------------------------------------------- */

ModeCard::ModeCard( const QString &icn, const QString &title, const QString &detail,
	QWidget *accessory, QWidget *parent ) : QWidget( parent ), m_icon( icn ), m_title( title )
{
	setFocusPolicy( Qt::StrongFocus );
	setAttribute( Qt::WA_AcceptTouchEvents, false );
	setAccessibleName( title );
	setAccessibleDescription( detail );
	QVBoxLayout *v = new QVBoxLayout( this );
	v->setContentsMargins( px( 14 ), px( 13 ), px( 14 ), px( 11 ) );
	v->setSpacing( px( 6 ) );
	QHBoxLayout *h = new QHBoxLayout;
	h->setSpacing( px( 4 ) );
	m_iconLabel = new QLabel;
	m_iconLabel->setFixedSize( px( 26 ), px( 22 ) );
	h->addWidget( m_iconLabel );
	m_titleLabel = label( title, 15, QFont::DemiBold );
	h->addWidget( m_titleLabel, 1 );
	v->addLayout( h );
	m_detailLabel = label( detail, 12, QFont::Normal, secondary() );
	m_detailLabel->setWordWrap( true );
	{
		/* wraps at 150 points (LDC §2.3): fix its height so the card row is tall enough in a scroll area */
		const int w = px( 150 );
		m_detailLabel->setFixedWidth( w );
		const QRect br = QFontMetrics( m_detailLabel->font() ).boundingRect( QRect( 0, 0, w, 1000 ), Qt::TextWordWrap, detail );
		m_detailLabel->setFixedHeight( br.height() + 2 );
	}
	v->addWidget( m_detailLabel );
	v->addStretch( 1 );
	if ( accessory )
		v->addWidget( accessory, 0, Qt::AlignLeft );
	setSelected( false );
	/* child labels must not eat the mouse clicks */
	m_iconLabel->setAttribute( Qt::WA_TransparentForMouseEvents );
	m_titleLabel->setAttribute( Qt::WA_TransparentForMouseEvents );
	m_detailLabel->setAttribute( Qt::WA_TransparentForMouseEvents );
}

void ModeCard::setSelected( bool on )
{
	m_selected = on;
	m_iconLabel->setPixmap( iconPixmap( m_icon, on ? accent() : secondary(), 18 ) );
	update();
}

void ModeCard::paintEvent( QPaintEvent * )
{
	QPainter p( this );
	p.setRenderHint( QPainter::Antialiasing );
	if ( m_selected ) {
		QColor f = accent();
		f.setAlphaF( 0.14 );
		p.setBrush( f );
		p.setPen( QPen( accent(), 2 ) );
		p.drawRoundedRect( QRectF( rect() ).adjusted( 1, 1, -1, -1 ), 12, 12 );
	} else {
		p.setBrush( groupFill() );
		p.setPen( QPen( separator(), 1 ) );
		p.drawRoundedRect( QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 ), 12, 12 );
	}
	if ( hasFocus() ) {
		p.setBrush( Qt::NoBrush );
		QColor fc = accent();
		fc.setAlphaF( 0.55 );
		p.setPen( QPen( fc, 3 ) );
		p.drawRoundedRect( QRectF( rect() ).adjusted( -0.5, -0.5, 0.5, 0.5 ), 13, 13 );
	}
}

void ModeCard::mousePressEvent( QMouseEvent *e )
{
	if ( e->button() == Qt::LeftButton ) {
		setFocus( Qt::MouseFocusReason );
		emit picked();
	}
}

void ModeCard::mouseDoubleClickEvent( QMouseEvent *e )
{
	if ( e->button() == Qt::LeftButton ) {
		emit picked();
		emit playRequested();
	}
}

void ModeCard::keyPressEvent( QKeyEvent *e )
{
	switch ( e->key() ) {
	case Qt::Key_Left:
		emit step( -1 );
		return;
	case Qt::Key_Right:
		emit step( +1 );
		return;
	case Qt::Key_Space:
		emit picked();
		return;
	default:
		break;
	}
	QWidget::keyPressEvent( e );
}

/* ---- Steam Deck navigation -------------------------------------------------------------------- */

namespace {

class DeckNav : public QObject {
public:
	bool eventFilter( QObject *o, QEvent *e ) override
	{
		if ( e->type() != QEvent::KeyPress )
			return false;
		QKeyEvent *k = static_cast<QKeyEvent *>( e );
		if ( k->modifiers() & ( Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier ) )
			return false;
		QWidget *w = qobject_cast<QWidget *>( o );
		if ( !w || w != QApplication::focusWidget() )
			return false;
		const int key = k->key();
		const bool arrow = key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right;
		const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
		if ( !arrow && !enter )
			return false;

		QWidget *top = w->window();
		auto move = [&]( bool next ) {
			QKeyEvent tab( QEvent::KeyPress, next ? Qt::Key_Tab : Qt::Key_Backtab, next ? Qt::NoModifier : Qt::ShiftModifier );
			QApplication::sendEvent( w, &tab );
			Q_UNUSED( top );
			return true;
		};
		const bool fwd = key == Qt::Key_Down || key == Qt::Key_Right;

		if ( qobject_cast<QComboBox *>( w ) ) {
			QComboBox *c = static_cast<QComboBox *>( w );
			if ( c->view() && c->view()->isVisible() )
				return false;
			if ( enter ) {
				c->showPopup();
				return true;
			}
			return move( fwd );
		}
		if ( qobject_cast<QAbstractItemView *>( w ) || qobject_cast<QAbstractItemView *>( w->parentWidget() ) ) {
			/* lists keep Up/Down; Left/Right leave them */
			if ( key == Qt::Key_Left )
				return move( false );
			if ( key == Qt::Key_Right )
				return move( true );
			return false;
		}
		if ( qobject_cast<ModeCard *>( w ) ) {
			if ( key == Qt::Key_Up )
				return move( false );
			if ( key == Qt::Key_Down )
				return move( true );
			if ( enter )
				return false;			/* the default button (Play) */
			return false;
		}
		if ( qobject_cast<QSlider *>( w ) ) {
			if ( key == Qt::Key_Up )
				return move( false );
			if ( key == Qt::Key_Down )
				return move( true );
			return false;
		}
		if ( qobject_cast<QAbstractSpinBox *>( w ) || qobject_cast<QLineEdit *>( w ) ) {
			QAbstractSpinBox *sb = qobject_cast<QAbstractSpinBox *>( w );
			if ( !sb )
				sb = qobject_cast<QAbstractSpinBox *>( w->parentWidget() );
			if ( key == Qt::Key_Up || key == Qt::Key_Down ) {
				if ( sb )
					return false;		/* steps the number */
				return move( key == Qt::Key_Down );
			}
			return false;
		}
		if ( qobject_cast<QAbstractButton *>( w ) ) {
			QAbstractButton *b = static_cast<QAbstractButton *>( w );
			if ( enter ) {
				b->click();
				return true;
			}
			return move( fwd );
		}
		return false;
	}
};

}

void installDeckNavigation( QApplication *app )
{
	app->installEventFilter( new DeckNav );
}

}	/* namespace Ui */
