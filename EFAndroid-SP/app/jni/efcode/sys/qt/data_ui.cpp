/*
===========================================================================
ef-launcher (Qt 6): the game-data UI: the chooser, Add Game Files and the
copy-progress panel. LDC §4.2-§4.4.

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#include "dialogs.h"

using namespace Ui;

namespace {

const unsigned long long kSlack = 64ull * 1024 * 1024;

QString msgUseBaseEF()
{
	return "Run GOG's installer instead (with Wine on Linux), or install the game on a Windows PC, then choose the folder it installed to.";
}

QString homeBase() { return U( lc_homebase() ); }

QString externalBase()
{
	const char *gf = lc_store_get( lc_store_shared(), LC_KEY_GAMEFOLDER );
	if ( !gf || !*gf )
		return QString();
	char out[LC_MAX_PATH];
	if ( lc_child_ci( gf, LC_BASEGAME, out, sizeof out ) && lc_is_dir( out ) )
		return U( out );
	return QString();
}

QFileDialog::Options fileOptions()
{
	return deck() ? QFileDialog::DontUseNativeDialog : QFileDialog::Options();
}

/* The copy-progress panel (LDC §4.4): 440 x 120, determinate bar, Cancel. */
class CopyProgress : public QDialog {
public:
	CopyProgress( QWidget *parent, const QString &title ) : QDialog( parent )
	{
		setWindowTitle( title );
		setModal( true );
		setWindowFlag( Qt::WindowCloseButtonHint, false );
		setFixedWidth( px( 440 ) );
		QVBoxLayout *v = new QVBoxLayout( this );
		v->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 16 ) );
		v->setSpacing( px( 10 ) );
		m_label = label( QString(), 13 );
		m_label->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
		v->addWidget( m_label );
		m_bar = new QProgressBar;
		m_bar->setRange( 0, 1000 );
		m_bar->setTextVisible( false );
		v->addWidget( m_bar );
		QHBoxLayout *h = new QHBoxLayout;
		h->addStretch( 1 );
		m_cancel = new QPushButton( "Cancel" );
		h->addWidget( m_cancel );
		v->addLayout( h );
		connect( m_cancel, &QPushButton::clicked, this, [this]() { m_cancelFlag = 1; m_cancel->setEnabled( false ); } );
	}
	void keyPressEvent( QKeyEvent *e ) override
	{
		if ( e->key() == Qt::Key_Escape ) {
			m_cancelFlag = 1;
			return;
		}
		QDialog::keyPressEvent( e );
	}
	void reject() override { m_cancelFlag = 1; }
	QLabel *m_label;
	QProgressBar *m_bar;
	QPushButton *m_cancel;
	volatile signed char m_cancelFlag = 0;
};

/* Runs a planned job with the progress panel. 1 ok, 0 failed (err filled), -1 cancelled. */
int runJob( QWidget *parent, lc_copyjob *job, QString *errOut )
{
	char title[256] = "";
	lc_copyjob_title( job, title, sizeof title );
	const bool unpack = lc_copyjob_is_unpack( job ) != 0;
	CopyProgress dlg( parent, title[0] ? U( title ) : ( unpack ? QString( "Unpacking the GOG installer" ) : QString( "Copying game files" ) ) );
	const unsigned long long total = lc_copyjob_total( job );
	volatile unsigned long long progress = 0;
	char err[512] = "";
	int result = 0;
	bool done = false;

	QThread *t = QThread::create( [&]() {
		result = lc_copyjob_run( job, &progress, &dlg.m_cancelFlag, err, sizeof err );
	} );
	QObject::connect( t, &QThread::finished, &dlg, [&]() { done = true; dlg.accept(); } );
	QTimer poll;
	poll.setInterval( 20 );
	QObject::connect( &poll, &QTimer::timeout, &dlg, [&]() {
		const unsigned long long p = progress;
		dlg.m_bar->setValue( total ? int( double( p ) / double( total ) * 1000.0 ) : 0 );
		char txt[512] = "";
		lc_copyjob_progress_text( job, p, txt, sizeof txt );
		if ( txt[0] )
			dlg.m_label->setText( U( txt ) );
		else
			dlg.m_label->setText( QString( "%1 to %2 \xe2\x80\xa6" ).arg( unpack ? "Unpacking game files" : "Copying game files", homeBase() ) );
	} );
	char first[512] = "";
	lc_copyjob_progress_text( job, 0, first, sizeof first );
	dlg.m_label->setText( first[0] ? U( first ) : QString::fromUtf8( "%1 to %2 \xe2\x80\xa6" )
		.arg( unpack ? "Unpacking game files" : "Copying game files", homeBase() ) );
	t->start();
	poll.start();
	/* the dialog is shown only if the job takes a moment, so tiny copies don't flash a window */
	if ( !t->wait( 150 ) ) {
		dlg.exec();
	}
	t->wait();
	poll.stop();
	delete t;
	if ( errOut )
		*errOut = U( err );
	return result;
}

int freeSpaceCheck( QWidget *parent, unsigned long long total, const QString &verb, bool offerInPlace )
{
	const QByteArray hb = B( homeBase() );
	lc_mkdir_p( hb.constData() );
	const unsigned long long freeB = lc_free_space( hb.constData() );
	if ( freeB && freeB < total + kSlack ) {
		QString info;
		if ( offerInPlace )
			info = QString( "%1 needs %2, but only %3 is free on the disk containing\n%4.\n\nFree up some space, or tick "
				"\"Use BaseEF files in place rather than copy\" to use the game folder directly." )
				.arg( verb, bytes( total + kSlack ), bytes( freeB ), homeBase() );
		else
			info = QString( "%1 needs %2, but only %3 is free." ).arg( verb, bytes( total ), bytes( freeB ) );
		alert( parent, Warning, "Not enough disk space", info );
		return 0;
	}
	return 1;
}

/* GOG installer unpack (LDC §4.3): returns files unpacked, 0 nothing to do, -1 failed */
int unpackInstaller( QWidget *parent, const QString &exe, bool onlyNew )
{
	const QByteArray eb = B( exe );
	log( "GOG installer: reading \"" + exe + "\"" );
	char err[512] = "";
	int present = 0;
	const QString ext = externalBase();
	lc_copyjob *job = lc_plan_unpack_installer( eb.constData(), onlyNew, B( homeBase() ).constData(),
		ext.isEmpty() ? nullptr : B( ext ).constData(), &present, err, sizeof err );
	if ( !job ) {
		log( "GOG installer: " + U( err ) );
		alert( parent, Warning, "This installer can't be used",
			QString( "%1\n\nThis app can't unpack it: %2.\n\n%3" ).arg( QFileInfo( exe ).fileName(), U( err ), msgUseBaseEF() ) );
		return -1;
	}
	const int count = lc_copyjob_count( job );
	log( QString( "GOG installer: %1 .pk3 files (%2 to unpack), %3 already present" ).arg( count )
		.arg( bytes( lc_copyjob_total( job ) ) ).arg( present ) );
	if ( count == 0 ) {
		lc_copyjob_free( job );
		alert( parent, Warning, "Nothing to add", "The game already has all of this installer's .pk3 files." );
		return 0;
	}
	if ( !freeSpaceCheck( parent, lc_copyjob_total( job ), "Unpacking the game files", false ) ) {
		lc_copyjob_free( job );
		return -1;
	}
	QString e;
	const int r = runJob( parent, job, &e );
	lc_copyjob_free( job );
	if ( r == 0 ) {
		log( "Copy failed: " + e );
		alert( parent, Warning, "Unpacking the GOG installer failed",
			QString( "%1.\n\nThe partly unpacked files were removed. If the download is damaged, download it again from GOG; otherwise: %2" )
				.arg( e, msgUseBaseEF() ) );
		return -1;
	}
	if ( r < 0 )
		return -1;
	log( QString( "GOG installer: unpacked %1 files to \"%2\"" ).arg( count ).arg( homeBase() ) );
	return count;
}

bool copyGameFiles( QWidget *parent, const QString &srcBase )
{
	int pk3s = 0, saves = 0, kept = 0;
	lc_copyjob *job = lc_plan_copy_game_files( B( srcBase ).constData(), B( homeBase() ).constData(), &pk3s, &saves, &kept );
	if ( !job ) {
		alert( parent, Warning, "Copying the game files failed", "Nothing could be copied from that folder." );
		return false;
	}
	log( QString( "Copying %1 .pk3 files and %2 saves (%3 existing saves kept) from \"%4\" to \"%5\": %6" )
		.arg( pk3s ).arg( saves ).arg( kept ).arg( srcBase, homeBase(), bytes( lc_copyjob_total( job ) ) ) );
	if ( !freeSpaceCheck( parent, lc_copyjob_total( job ), "Copying the game files", true ) ) {
		lc_copyjob_free( job );
		return false;
	}
	QString e;
	const unsigned long long total = lc_copyjob_total( job );
	const int r = runJob( parent, job, &e );
	lc_copyjob_free( job );
	if ( r == 0 ) {
		log( "Copy failed: " + e );
		alert( parent, Warning, "Copying the game files failed", e + "\n\nThe partial copy was removed." );
		return false;
	}
	if ( r < 0 )
		return false;
	char p0[LC_MAX_PATH];
	if ( !lc_child_ci( B( homeBase() ).constData(), "pak0.pk3", p0, sizeof p0 ) ) {
		alert( parent, Warning, "Copying the game files failed", "pak0.pk3 did not end up in the destination folder." );
		return false;
	}
	log( QString( "Copied %1 of game files to \"%2\"" ).arg( bytes( total ), homeBase() ) );
	return true;
}

/* the chooser's panel: what to pick and whether to copy */
class ChooserPanel : public QDialog {
public:
	ChooserPanel( QWidget *parent ) : QDialog( parent )
	{
		setWindowTitle( "Choose your Elite Force folder, or GOG's installer" );
		setModal( true );
		setFixedWidth( px( 520 ) );
		QVBoxLayout *v = new QVBoxLayout( this );
		v->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 16 ) );
		v->setSpacing( px( 12 ) );
		v->addWidget( label( "Choose your Elite Force folder, or GOG's installer", 15, QFont::DemiBold ) );
		v->addWidget( label( "Select the folder where Star Trek: Voyager \xe2\x80\x93 Elite Force is installed (the one containing BaseEF with "
			"pak0.pk3), the BaseEF folder itself, or GOG's Windows installer (the setup .exe). By default the game files are copied into "
			"the app's own folder, so the original folder or CD is no longer needed.", 12, QFont::Normal, QColor(), 480 ) );
		m_inPlace = new QCheckBox( "Use BaseEF files in place rather than copy" );
		v->addWidget( m_inPlace );
		m_status = label( QString(), 11, QFont::Normal, secondary(), 480 );
		v->addWidget( m_status );
		connect( m_inPlace, &QCheckBox::toggled, this, [this]() { refresh(); } );
		refresh();
		QHBoxLayout *h = new QHBoxLayout;
		QPushButton *folder = new QPushButton( QString::fromUtf8( "Choose Folder\xe2\x80\xa6" ) );
		QPushButton *gog = new QPushButton( QString::fromUtf8( "Choose GOG Installer\xe2\x80\xa6" ) );
		QPushButton *cancel = new QPushButton( "Cancel" );
		folder->setDefault( true );
		h->addWidget( folder );
		h->addWidget( gog );
		h->addStretch( 1 );
		h->addWidget( cancel );
		v->addLayout( h );
		connect( cancel, &QPushButton::clicked, this, &QDialog::reject );
		connect( folder, &QPushButton::clicked, this, [this]() {
			const QString p = QFileDialog::getExistingDirectory( this, "Choose your Elite Force folder, or GOG's installer",
				startDir(), QFileDialog::ShowDirsOnly | fileOptions() );
			if ( !p.isEmpty() ) {
				path = p;
				accept();
			}
		} );
		connect( gog, &QPushButton::clicked, this, [this]() {
			const QString p = QFileDialog::getOpenFileName( this, "Choose GOG's installer", startDir(),
				"GOG installer (*.exe);;All files (*)", nullptr, fileOptions() );
			if ( !p.isEmpty() ) {
				path = p;
				accept();
			}
		} );
	}
	bool inPlace() const { return m_inPlace->isChecked(); }
	QString path;
private:
	QString startDir() const
	{
		const QString h = U( lc_home_dir() );
		return h.isEmpty() ? QDir::homePath() : h;
	}
	void refresh()
	{
		m_status->setText( m_inPlace->isChecked() ? "Files will be read from this folder; nothing is copied."
			: "Files will be copied into the app's data folder (about 600 MB for a standard install)." );
	}
	QCheckBox *m_inPlace;
	QLabel *m_status;
};

}	/* namespace */

namespace DataUi {

bool runChooser( QWidget *parent )
{
	const QString testChoose = testEnv( "VOYAGERSP_TEST_CHOOSE" );
	for ( ;; ) {
		QString path;
		bool inPlace = false;
		if ( !testChoose.isEmpty() ) {
			path = testChoose;
			inPlace = testSet( "VOYAGERSP_TEST_INPLACE" );
		} else {
			ChooserPanel panel( parent );
			if ( panel.exec() != QDialog::Accepted ) {
				log( "Game folder selection cancelled" );
				return false;
			}
			path = panel.path;
			inPlace = panel.inPlace();
		}
		const QByteArray pb = B( path );

		if ( lc_is_installer( pb.constData() ) ) {
			const int r = unpackInstaller( parent, path, false );
			if ( r > 0 ) {
				char p0[LC_MAX_PATH];
				if ( lc_child_ci( B( homeBase() ).constData(), "pak0.pk3", p0, sizeof p0 ) ) {
					lc_remember_game_folder( nullptr );
					lc_store_save( lc_store_shared() );
					return true;
				}
			}
			if ( !testChoose.isEmpty() )
				return false;
			continue;
		}
		char base[LC_MAX_PATH] = "", problem[1024] = "";
		const int rc = lc_validate_choice( pb.constData(), inPlace, base, sizeof base, problem, sizeof problem );
		if ( rc == 0 ) {
			if ( inPlace ) {
				char parentDir[LC_MAX_PATH];
				lc_path_dirname( parentDir, sizeof parentDir, base );
				lc_remember_game_folder( parentDir );
				lc_store_save( lc_store_shared() );
				log( QString( "Using game files in place from \"%1\" (remembered)" ).arg( U( base ) ) );
				return true;
			}
			if ( copyGameFiles( parent, U( base ) ) ) {
				lc_remember_game_folder( nullptr );
				lc_store_save( lc_store_shared() );
				return true;
			}
			if ( !testChoose.isEmpty() )
				return false;
			continue;
		}
		log( QString( "Rejected \"%1\": %2" ).arg( path, U( problem ) ) );
		alert( parent, Warning, "That folder can't be used", path + "\n\n" + U( problem ) );
		if ( !testChoose.isEmpty() )
			return false;
	}
}

int addGameFiles( QWidget *parent, const QString &testPath )
{
	QStringList picks;
	if ( !testPath.isEmpty() )
		picks << testPath;
	else {
		QDialog d( parent );
		d.setWindowTitle( "Add Game Files" );
		d.setModal( true );
		d.setFixedWidth( px( 500 ) );
		QVBoxLayout *v = new QVBoxLayout( &d );
		v->setContentsMargins( px( 20 ), px( 18 ), px( 20 ), px( 16 ) );
		v->setSpacing( px( 12 ) );
		v->addWidget( label( QString::fromUtf8( "Choose .pk3 files, a folder that contains them (such as BaseEF of the Expansion Pack or its CD), "
			"or GOG's installer (setup_\xe2\x80\xa6.exe)" ), 13, QFont::Normal, QColor(), 460 ) );
		QHBoxLayout *h = new QHBoxLayout;
		QPushButton *files = new QPushButton( QString::fromUtf8( "Choose Files\xe2\x80\xa6" ) );
		QPushButton *folder = new QPushButton( QString::fromUtf8( "Choose Folder\xe2\x80\xa6" ) );
		QPushButton *cancel = new QPushButton( "Cancel" );
		files->setDefault( true );
		h->addWidget( files );
		h->addWidget( folder );
		h->addStretch( 1 );
		h->addWidget( cancel );
		v->addLayout( h );
		const QString start = U( lc_home_dir() );
		QObject::connect( cancel, &QPushButton::clicked, &d, &QDialog::reject );
		QObject::connect( files, &QPushButton::clicked, &d, [&]() {
			const QStringList l = QFileDialog::getOpenFileNames( &d, "Add", start,
				"Game files (*.pk3 *.exe);;All files (*)", nullptr, fileOptions() );
			if ( !l.isEmpty() ) {
				picks = l;
				d.accept();
			}
		} );
		QObject::connect( folder, &QPushButton::clicked, &d, [&]() {
			const QString p = QFileDialog::getExistingDirectory( &d, "Add", start, QFileDialog::ShowDirsOnly | fileOptions() );
			if ( !p.isEmpty() ) {
				picks = QStringList( p );
				d.accept();
			}
		} );
		if ( d.exec() != QDialog::Accepted )
			return 0;
	}

	QStringList installers, others;
	for ( const QString &p : picks )
		( lc_is_installer( B( p ).constData() ) ? installers : others ) << p;

	int total = 0;
	for ( const QString &exe : installers ) {
		const int r = unpackInstaller( parent, exe, true );
		if ( r > 0 )
			total += r;
	}
	if ( others.isEmpty() )
		return total;

	QList<QByteArray> bs;
	QVector<const char *> ptrs;
	for ( const QString &p : others ) {
		bs << B( p );
	}
	for ( const QByteArray &b : bs )
		ptrs << b.constData();
	int present = 0;
	const QString ext = externalBase();
	lc_copyjob *job = lc_plan_add_files( ptrs.constData(), int( ptrs.size() ), B( homeBase() ).constData(),
		ext.isEmpty() ? nullptr : B( ext ).constData(), &present );
	const int count = job ? lc_copyjob_count( job ) : 0;
	log( QString( "Add game files: %1 new .pk3 (%2), %3 already present" ).arg( count )
		.arg( bytes( job ? lc_copyjob_total( job ) : 0 ) ).arg( present ) );
	if ( !job || count == 0 ) {
		if ( job )
			lc_copyjob_free( job );
		const QString info = present == 1 ? "The game already has that .pk3 file."
			: present > 1 ? "The game already has all of those .pk3 files."
			: "No .pk3 files were found there. Choose .pk3 files, or the BaseEF folder that contains them.";
		alert( parent, Warning, "Nothing to add", info );
		return total;
	}
	if ( !freeSpaceCheck( parent, lc_copyjob_total( job ), "Adding the files", false ) ) {
		lc_copyjob_free( job );
		return total;
	}
	const unsigned long long jobTotal = lc_copyjob_total( job );
	QString e;
	const int r = runJob( parent, job, &e );
	lc_copyjob_free( job );
	if ( r == 0 ) {
		log( "Copy failed: " + e );
		alert( parent, Warning, "Copying the game files failed", e + "\n\nThe partial copy was removed." );
		return total;
	}
	if ( r < 0 )
		return total;
	log( QString( "Added %1 files (%2) to \"%3\"" ).arg( count ).arg( bytes( jobTotal ), homeBase() ) );
	return total + count;
}

}	/* namespace DataUi */
