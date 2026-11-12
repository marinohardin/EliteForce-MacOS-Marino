/*
===========================================================================
ef-launcher (Qt 6): shared UI helpers: colours, fonts, icons, alerts, test
hooks, worker threads. Spec: docs/launcher-spec (LDC §11, servers.md §15.3).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_UI_COMMON_H
#define EF_QT_UI_COMMON_H

#include <QtWidgets>
#include <functional>

extern "C" {
#include "lc_common.h"
#include "lc_settings.h"
#include "lc_launch.h"
#include "lc_data.h"
#include "lc_update.h"
#include "lc_links.h"
#include "lc_server.h"
#include "lc_net.h"
#include "lc_host.h"
#include "lc_nat.h"
#include "lc_coop.h"
}

#ifndef EF_APP_VERSION
#	define EF_APP_VERSION ""
#endif

namespace Ui {

inline QString U( const char *s ) { return QString::fromUtf8( s ? s : "" ); }
inline QByteArray B( const QString &s ) { return s.toUtf8(); }
void log( const QString &line );			/* lc_log with a "%s" format */

/* "Computer" / "computer": LC_PLATFORM_NOUN of lc_common.h */
inline QString noun() { return U( LC_PLATFORM_NOUN ); }
inline QString nounLc() { return U( LC_PLATFORM_NOUN_LC ); }

/* ---- environment test hooks ------------------------------------------------------- */
QString testEnv( const char *name );		/* "" if unset */
bool testSet( const char *name );			/* set, not empty, not "0" */

/* ---- Steam Deck / gamescope ---------------------------------------------------------- */
bool deck();								/* lc_is_steam_deck() or VOYAGERSP_TEST_DECK=1 */
double scale();								/* 1.0, or a larger factor on the Deck */
inline int px( int v ) { return int( v * scale() + 0.5 ); }

/* ---- colours (all derived from the active palette, so light and dark both work) ------- */
bool dark();
QColor secondary();
QColor tertiary();
QColor quaternary();
QColor accent();
QColor orange();
QColor green();
QColor red();
QColor amber();
QColor purple();
QColor blue();
QColor yellow();
QColor teal();
QColor pink();
QColor groupFill();
QColor separator();
QColor q3Color( int idx );					/* Quake 3 colour code index 0..7 (servers.md §7.9) */
void setColor( QWidget *w, const QColor &c );	/* text colour of a label-like widget */

/* ---- widgets ---------------------------------------------------------------------------- */
QFont font( double pt, QFont::Weight w = QFont::Normal, bool mono = false );
/* a label: pt size, weight, colour (invalid = default), wrap width (0 = no wrap) */
QLabel *label( const QString &text, double pt = 13, QFont::Weight w = QFont::Normal,
	const QColor &c = QColor(), int wrapWidth = 0 );
/* a single-line label that elides its text (tail or middle) instead of clipping */
QLabel *elidedLabel( const QString &text, double pt = 13, QFont::Weight w = QFont::Normal,
	const QColor &c = QColor(), Qt::TextElideMode mode = Qt::ElideRight );
QPushButton *button( const QString &text, const QString &tooltip = QString() );
QToolButton *iconButton( const QString &icon, const QString &tooltip, int iconPx = 12 );
QIcon icon( const QString &name, const QColor &c = QColor(), int px = 16 );
QPixmap iconPixmap( const QString &name, const QColor &c, int px );
QFrame *hline();

/* A captioned rounded box (LDC §2.1 "Group style"): caption above, content inside. */
class Group : public QWidget {
public:
	Group( const QString &caption, QWidget *parent = nullptr );
	QVBoxLayout *content() { return m_content; }
	void setCaption( const QString &c );
private:
	QLabel *m_caption;
	QVBoxLayout *m_content;
};

/* the rounded panel (fill + 1 px separator) used by groups, lists and tables */
class Panel : public QFrame {
public:
	explicit Panel( QWidget *parent = nullptr, int radius = 10 );
protected:
	void paintEvent( QPaintEvent *e ) override;
private:
	int m_radius;
};

/* A list/table view inside a Panel look: used for map lists, masters, servers. */
void styleView( QAbstractItemView *v );

/* ---- alerts ----------------------------------------------------------------------------- */
enum AlertStyle { Info, Warning, Critical };
/* Shows a message box; returns the index of the clicked button (0-based). Honours
   VOYAGERSP_TEST_NODIALOG / VOYAGERSP_TEST_CONFIRM (first button, logged). The
   button whose index is `escape` answers Esc (-1: the last). */
int alert( QWidget *parent, AlertStyle style, const QString &message, const QString &info,
	const QStringList &buttons = QStringList( "OK" ), int defaultButton = 0, int escape = -1,
	int destructive = -1 );

/* ---- threads ---------------------------------------------------------------------------- */
/* Run work() on a worker thread, then done() on the UI thread (ctx's thread). If ctx is
   destroyed first, done is not called. */
void async( QObject *ctx, std::function<void()> work, std::function<void()> done );

/* ---- snapshots ------------------------------------------------------------------------------ */
bool savePng( QWidget *w, const QString &path );
/* quit the app after the event loop has rendered what the test hook asked for */
void quitSoon( int ms = 0 );

/* formatting */
QString tilde( const QString &path );
QString bytes( unsigned long long n );

}	/* namespace Ui */

#endif
