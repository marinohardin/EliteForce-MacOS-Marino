/*
===========================================================================
ef-launcher (Qt 6): the main launcher window. Spec: launcher-data-coop.md
§2 (window), §3 (Play), §7 (links, About), §8 (updates); servers.md §1.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef EF_QT_LAUNCHER_WINDOW_H
#define EF_QT_LAUNCHER_WINDOW_H

#include "widgets.h"
#include <QMutex>
#include <atomic>

class UpdateBadge;

class LauncherWindow : public QDialog {
	Q_OBJECT
public:
	explicit LauncherWindow( bool hostOnly = false );
	~LauncherWindow() override;

	/* what the --host-server instance needs from the launcher state (servers.md §1.4) */
	struct HostInfo {
		lc_server_settings *settings;
		QString basepath, dataFolder;
		QStringList clientArgs;
	};
	HostInfo hostInfo();

	/* test hooks and link delivery (the main program calls these once the window is up) */
	void runTestHooks();
	void deliverLink( const QString &link );
	void activateSelf();
	void openChooserSlot() { openChooser(); }

	QDialog *activeSheet() const { return m_sheet; }
	bool canPlay() const { return m_haveData && !m_missingPatch; }

	/* settings shared with the sheets */
	lc_server_settings *server() { return m_server; }
	QString dataFolder() const { return m_dataFolder; }

public slots:
	void play();
	void quit();

protected:
	void reject() override;			/* Esc and the close button = Quit */
	void showEvent( QShowEvent * ) override;

private:
	friend class TestDriver;
	void build();
	QWidget *buildHeader();
	QWidget *buildCards();
	QWidget *buildGraphics();
	QWidget *buildServerGroup();
	QWidget *buildHolomatch();
	QWidget *buildGameFiles();
	QWidget *buildFooter();

	void selectMode( int m );
	void refreshModeEnabling();
	void graphicsChanged();
	void showGraphics();
	void applyPreset( lc_preset p );
	void refreshServerSummary();
	void refreshMasters();
	void refreshData();
	void refreshHudEnabled();
	void refreshPlayEnabled();
	void rebuildSizeList();

	void startUpdateCheck();
	void showUpdateBadge();
	void checkForUpdatesNow();
	void showAbout();
	QMessageBox *makeAbout();
	static QString platformName();

	void doPlay();
	void startServer();
	QStringList clientArgs();
	lc_launch makeLaunch( int mode, QByteArray &nameBytes, QByteArray &basepathBytes,
		QByteArray &loopBytes, QByteArray &joinAddr, QByteArray &joinPw );
	QString loopbackIP( bool wait );
	void saveMode();

	void openChooser();
	void openAddFiles( const QString &testPath = QString() );
	void openBrowser();
	void openConfig();
	void openMasters();
	void openCoop( const QString &address = QString() );
	void openHud();
	template <class D> int runSheet( D *d );

	/* state */
	int m_mode = 0;
	lc_gfx m_gfx;
	int m_fromConfig = 0;
	lc_cmod m_cmod;
	lc_data m_data;
	bool m_haveData = false, m_overridden = false, m_missingPatch = false;
	char m_missingNames[2][16];
	QString m_homeBase, m_dataFolder;
	lc_server_settings *m_server;
	QString m_joinAddress, m_joinPassword;
	QPointer<QDialog> m_sheet;
	QString m_updateVersion, m_updateUrl;
	bool m_graphicsLoading = false;
	lc_nat *m_natLookup = nullptr;
	QMutex m_ipLock;
	QString m_publicIP;
	bool m_publicIPKnown = false;
	QStringList m_pendingLinks;

	/* widgets */
	QList<Ui::ModeCard *> m_cards;
	QPushButton *m_coopButton = nullptr, *m_browseButton = nullptr, *m_configButton = nullptr;
	QStackedWidget *m_midStack = nullptr;
	Ui::Segmented *m_quality = nullptr;
	QComboBox *m_display = nullptr, *m_winSize = nullptr, *m_msaa = nullptr, *m_picmip = nullptr,
		*m_brightness = nullptr;
	QCheckBox *m_highdpi = nullptr, *m_vsync = nullptr, *m_loopback = nullptr, *m_cmodBox = nullptr;
	QLabel *m_brightHint = nullptr;
	QLabel *m_srvName = nullptr, *m_srvGame = nullptr, *m_srvMaps = nullptr, *m_srvPlayers = nullptr,
		*m_srvNetwork = nullptr, *m_srvAddress = nullptr;
	QLabel *m_nameLabel = nullptr, *m_nameHint = nullptr, *m_cmodLabel = nullptr;
	QLineEdit *m_name = nullptr;
	QLabel *m_masterSummary = nullptr;
	QPushButton *m_hudButton = nullptr, *m_mastersButton = nullptr;
	QLabel *m_pathLabel = nullptr, *m_statusIcon = nullptr, *m_statusText = nullptr, *m_expIcon = nullptr,
		*m_expText = nullptr, *m_folderIcon = nullptr;
	QPushButton *m_showFolder = nullptr, *m_addFiles = nullptr, *m_change = nullptr;
	QPushButton *m_playButton = nullptr, *m_quitButton = nullptr;
	QWidget *m_badge = nullptr;
	QLabel *m_badgeTitle = nullptr, *m_badgeText = nullptr;
	QAction *m_autoUpdateAction = nullptr;
	QScrollArea *m_scroll = nullptr;
	QWidget *m_content = nullptr;
	QMenu *m_menu = nullptr;
	bool m_sized = false;
	QString m_playerName;
	bool m_hostOnly = false;
	QCheckBox *m_autoBox = nullptr;
};

#endif
