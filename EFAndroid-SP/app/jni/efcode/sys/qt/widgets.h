/*
===========================================================================
ef-launcher (Qt 6): small reusable widgets (segmented control, number field,
mode card) and the Steam Deck focus navigation filter.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_WIDGETS_H
#define EF_QT_WIDGETS_H

#include "ui_common.h"

namespace Ui {

/* Equal-width exclusive buttons (NSSegmentedControl). */
class Segmented : public QWidget {
	Q_OBJECT
public:
	explicit Segmented( const QStringList &titles, QWidget *parent = nullptr );
	int current() const { return m_current; }
	void setCurrent( int i, bool emitSignal = false );
	void setSegmentEnabled( int i, bool on );
	void setSegmentToolTip( int i, const QString &t );
	int count() const { return m_buttons.size(); }
	QAbstractButton *segment( int i ) { return m_buttons.value( i ); }
signals:
	void currentChanged( int index );
private:
	QList<QAbstractButton *> m_buttons;
	int m_current = -1;
};

/* Right-aligned integer field with an optional stepper and unit label (servers.md §4.3). */
class NumberField : public QWidget {
	Q_OBJECT
public:
	NumberField( int min, int max, const QString &unit = QString(), bool stepper = true,
		int fieldWidth = 50, QWidget *parent = nullptr );
	int value() const;
	void setValue( int v );					/* no signal */
	QSpinBox *spin() { return m_spin; }
signals:
	void valueEdited( int v );				/* each keystroke / step */
private:
	QSpinBox *m_spin;
};

/* One of the three selectable mode cards (LDC §2.3). */
class ModeCard : public QWidget {
	Q_OBJECT
public:
	ModeCard( const QString &icon, const QString &title, const QString &detail,
		QWidget *accessory, QWidget *parent = nullptr );
	void setSelected( bool on );
	bool selected() const { return m_selected; }
	QString title() const { return m_title; }
signals:
	void picked();							/* mouse down / space / arrow landed here */
	void playRequested();					/* double click */
	void step( int delta );					/* left / right arrow: -1 / +1 */
protected:
	void paintEvent( QPaintEvent * ) override;
	void mousePressEvent( QMouseEvent * ) override;
	void mouseDoubleClickEvent( QMouseEvent * ) override;
	void keyPressEvent( QKeyEvent * ) override;
private:
	QString m_icon, m_title;
	QLabel *m_iconLabel;
	QLabel *m_titleLabel;
	QLabel *m_detailLabel;
	bool m_selected = false;
};

/* Steam Deck: D-pad / stick arrows become arrow keys (Steam Input). Make them move the
   keyboard focus between controls and make Return / A press the focused control. */
void installDeckNavigation( QApplication *app );

}	/* namespace Ui */

#endif
