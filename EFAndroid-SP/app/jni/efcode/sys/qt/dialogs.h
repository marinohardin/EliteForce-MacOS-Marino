/*
===========================================================================
ef-launcher (Qt 6): the sheets and windows the main launcher opens.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_DIALOGS_H
#define EF_QT_DIALOGS_H

#include "widgets.h"

/* ---- HUD… (LDC §2.10) ---------------------------------------------------------------------- */
class HudDialog : public QDialog {
	Q_OBJECT
public:
	HudDialog( QWidget *parent, const lc_cmod &c );
	void applyTo( lc_cmod *c, lc_store *store ) const;
private:
	QComboBox *m_aspect;
	QSlider *m_pos;
	QCheckBox *m_predict;
	bool m_moved = false;
	QString m_center;
};

/* ---- Co-op (LDC §6.1) ----------------------------------------------------------------------- */
class CoopDialog : public QDialog {
	Q_OBJECT
public:
	/* canPlay: data present and no missing patch; address: an invite's address (checked at once) */
	CoopDialog( QWidget *parent, bool canPlay, const QString &address = QString() );
	~CoopDialog() override;
	enum Result { Cancelled = 0, Host = 1, Connect = 2 };
	int choice() const { return m_choice; }
	void reject() override;
	void runTestHooks();
	QString playName() const { return m_playName; }
private slots:
	void doCheck();
	void doHost();
	void doConnect();
	void natToggled( bool on );
	void copyInvite();
private:
	void showInternet();
	void showResult( const QString &icon, const QColor &color, const QString &title, const QString &text );
	void updateConnect();
	QString inviteLink() const;
	void releaseNat();

	bool m_canPlay;
	QLineEdit *m_name, *m_hostPw, *m_address, *m_joinPw;
	QCheckBox *m_background, *m_router;
	QLabel *m_internetStatus, *m_resultIcon, *m_resultTitle, *m_resultText, *m_connectNote, *m_lanLabel;
	QPushButton *m_copyInvite, *m_check, *m_connect, *m_host;
	Ui::Segmented *m_munro;
	QProgressBar *m_spinner;
	QWidget *m_resultWidget;
	QStringList m_lan;
	int m_choice = 0;
	bool m_checking = false;
	QString m_foundAddress, m_foundHostname, m_status;
	bool m_needsPassword = false;
	QString m_playName;
	/* NAT mapping state (guarded by the UI thread: callbacks are marshalled) */
	lc_nat *m_nat = nullptr;
	enum NatState { NatIdle, NatAsking, NatMapped, NatFailed } m_natState = NatIdle;
	QString m_natPublic, m_natReason;
	QTimer *m_natTimer;
	QString m_lastInvite;
	void onNat( int result, const QString &ip, int extPort, int err );
signals:
	void natResultSignal( int result, QString ip, int extPort, int err );
};

/* ---- Server Settings (servers.md §4) ------------------------------------------------------------ */
class ServerConfigDialog : public QDialog {
	Q_OBJECT
public:
	ServerConfigDialog( QWidget *parent, lc_server_settings *original, const QString &dataFolder );
	~ServerConfigDialog() override;
	void selectTab( const QString &tab );
	void setTickRate( int v );
	void commit();							/* Done without closing (tests) */
private:
	struct Impl;
	Impl *d;
};

/* ---- Master Servers (servers.md §6) ----------------------------------------------------------------- */
class MasterDialog : public QDialog {
	Q_OBJECT
public:
	explicit MasterDialog( QWidget *parent );
	~MasterDialog() override;
	void waitForChecks( int msTimeout );
	void testAction( const QString &action );
private:
	struct Impl;
	Impl *d;
};

/* ---- Holomatch Servers (servers.md §8) ---------------------------------------------------------------- */
class BrowserDialog : public QDialog {
	Q_OBJECT
public:
	BrowserDialog( QWidget *parent, const QString &dataFolder );
	~BrowserDialog() override;
	QString address() const;
	QString password() const;
	/* test hooks */
	void selectSource( const QString &tab );
	bool busy() const;
	void selectRow( int row );
	void logRows();
	void joinRow( int row );
private:
	struct Impl;
	Impl *d;
};

/* ---- the hosting window (servers.md §9); runs in the --host-server instance ----------------------------- */
class ServerWindow : public QWidget {
	Q_OBJECT
public:
	ServerWindow( const lc_server_settings &s, const QString &basepath, const QString &dataFolder,
		const QStringList &clientArgs );
	~ServerWindow() override;
	void startServer();
	void requestStop();
	void runTestHooks();
	void raiseWindow();
protected:
	void closeEvent( QCloseEvent * ) override;
private:
	struct Impl;
	Impl *d;
};

/* ---- game data UI (LDC §4) ------------------------------------------------------------------------------- */
namespace DataUi {
/* the chooser (§4.2); true when game files are now set up. testPath: VOYAGERSP_TEST_CHOOSE */
bool runChooser( QWidget *parent );
/* Add Game Files… (§4.4); returns the number of files added. testPath: VOYAGERSP_TEST_ADD */
int addGameFiles( QWidget *parent, const QString &testPath = QString() );
}

#endif
