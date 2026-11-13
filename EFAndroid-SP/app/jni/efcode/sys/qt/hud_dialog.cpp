/*
===========================================================================
ef-launcher (Qt 6): the HUD… sheet (cMod's HUD settings). LDC §2.10.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

using namespace Ui;

HudDialog::HudDialog( QWidget *parent, const lc_cmod &c ) : QDialog( parent )
{
	setWindowTitle( "HUD" );
	setModal( true );
	setFixedWidth( px( 480 ) );
	QVBoxLayout *root = new QVBoxLayout( this );
	root->setContentsMargins( px( 20 ), px( 20 ), px( 20 ), px( 16 ) );
	root->setSpacing( px( 18 ) );

	QGridLayout *g = new QGridLayout;
	g->setHorizontalSpacing( px( 10 ) );
	g->setVerticalSpacing( px( 10 ) );
	auto lab = [&]( const QString &t ) {
		QLabel *l = label( t, 13 );
		l->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
		return l;
	};

	g->addWidget( lab( "HUD:" ), 0, 0 );
	m_aspect = new QComboBox;
	m_aspect->addItem( "Widescreen: sharp, in proportion", 1 );
	m_aspect->addItem( "Stretched to the screen (original look)", 0 );
	m_aspect->setCurrentIndex( c.hudAspect ? 0 : 1 );
	g->addWidget( m_aspect, 0, 1 );

	g->addWidget( lab( "Position:" ), 1, 0 );
	QHBoxLayout *ph = new QHBoxLayout;
	ph->addWidget( label( "Edges", 11, QFont::Normal, secondary() ) );
	m_pos = new QSlider( Qt::Horizontal );
	m_pos->setRange( 0, 100 );
	m_pos->setTickPosition( QSlider::TicksBelow );
	m_pos->setTickInterval( 25 );
	m_pos->setFixedWidth( px( 200 ) );
	bool ok = false;
	const double v = QString::fromUtf8( c.hudCenter ).toDouble( &ok );
	m_pos->setValue( ok && v >= 0 && v <= 1 ? int( std::lround( v * 100 ) ) : 0 );
	m_center = QString::fromUtf8( c.hudCenter );
	connect( m_pos, &QSlider::actionTriggered, this, [this]() { m_moved = true; } );
	ph->addWidget( m_pos );
	ph->addWidget( label( "Middle", 11, QFont::Normal, secondary() ) );
	ph->addStretch( 1 );
	g->addLayout( ph, 1, 1 );

	g->addWidget( lab( "Weapons:" ), 2, 0 );
	m_predict = new QCheckBox( "Show my shots at once on servers that offer it" );
	m_predict->setChecked( c.predictWeapons != 0 );
	g->addWidget( m_predict, 2, 1 );
	g->addWidget( label( "From cMod's game modules by Noah Metzger. Weapon fire prediction needs a server running cMod's game code "
		"with it on (cMod servers, and Host a Server's default).", 11, QFont::Normal, secondary(), 300 ), 3, 1 );
	g->setColumnStretch( 1, 1 );
	root->addLayout( g );

	auto refresh = [this]() { m_pos->setEnabled( m_aspect->currentData().toInt() == 1 ); };
	connect( m_aspect, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, refresh );
	refresh();

	QHBoxLayout *b = new QHBoxLayout;
	b->addStretch( 1 );
	QPushButton *cancel = new QPushButton( "Cancel" );
	QPushButton *done = new QPushButton( "Done" );
	done->setDefault( true );
	connect( cancel, &QPushButton::clicked, this, &QDialog::reject );
	connect( done, &QPushButton::clicked, this, &QDialog::accept );
	b->addWidget( cancel );
	b->addWidget( done );
	root->addLayout( b );
	done->setFocus();
}

void HudDialog::applyTo( lc_cmod *c, lc_store *store ) const
{
	c->hudAspect = m_aspect->currentData().toInt();
	if ( m_moved ) {
		const double v = std::round( m_pos->value() ) / 100.0;
		lc_snprintf( c->hudCenter, sizeof c->hudCenter, "%g", v );
	}
	c->predictWeapons = m_predict->isChecked();
	lc_cmod_save_store( c, store );
	lc_store_save( store );
}
