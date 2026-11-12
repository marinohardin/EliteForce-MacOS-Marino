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

QString homeBase() { return U( lc_homebase() ); }

QString externalBase()
{
	char out[LC_MAX_PATH] = "";
	lc_data_external_base( out, sizeof out );
	return U( out );
}

/* alert texts the core worded (title, text) */
void coreAlert( QWidget *parent, const char *title, const char *text )
{
	alert( parent, Warning, U( title ), U( text ) );
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

int freeSpaceCheck( QWidget *parent, lc_copyjob *job )
{
	char title[256] = "", text[1024] = "";
	if ( lc_copyjob_check_space( job, title, sizeof title, text, sizeof text ) )
		return 1;
	coreAlert( parent, title, text );
	return 0;
}

void failureAlert( QWidget *parent, lc_copyjob *job, const QString &err )
{
	char title[256] = "", text[1024] = "";
	lc_copyjob_failure_texts( job, B( err ).constData(), title, sizeof title, text, sizeof text );
	log( "Copy failed: " + err );
	coreAlert( parent, title, text );
}

/* GOG installer unpack (LDC §4.3): returns files unpacked, 0 nothing to do, -1 failed */
int unpackInstaller( QWidget *parent, const QString &exe, bool onlyNew )
{
	char err[512] = "";
	int present = 0;
	const QString ext = externalBase();
	lc_copyjob *job = lc_plan_unpack_installer( B( exe ).constData(), onlyNew, B( homeBase() ).constData(),
		ext.isEmpty() ? nullptr : B( ext ).constData(), &present, err, sizeof err );
	char title[256] = "", text[1024] = "";
	if ( !job ) {
		if ( err[0] ) {
			lc_installer_error_alert( B( exe ).constData(), err, title, sizeof title, text, sizeof text );
			coreAlert( parent, title, text );
			return -1;
		}
		lc_nothing_to_add_alert( 1, present, title, sizeof title, text, sizeof text );
		coreAlert( parent, title, text );
		return 0;
	}
	const int count = lc_copyjob_count( job );
	if ( !freeSpaceCheck( parent, job ) ) {
		lc_copyjob_free( job );
		return -1;
	}
	QString e;
	const int r = runJob( parent, job, &e );
	if ( r == 0 )
		failureAlert( parent, job, e );
	lc_copyjob_free( job );
	return r > 0 ? count : -1;
}

bool copyGameFiles( QWidget *parent, const QString &srcBase )
{
	int pk3s = 0, saves = 0, kept = 0;
	lc_copyjob *job = lc_plan_copy_game_files( B( srcBase ).constData(), B( homeBase() ).constData(), &pk3s, &saves, &kept );
	if ( !job ) {
		coreAlert( parent, "Copying the game files failed", "Nothing could be copied from that folder." );
		return false;
	}
	if ( !freeSpaceCheck( parent, job ) ) {
		lc_copyjob_free( job );
		return false;
	}
	QString e;
	const int r = runJob( parent, job, &e );
	if ( r == 0 )
		failureAlert( parent, job, e );
	lc_copyjob_free( job );
	return r > 0;
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
		{
			char title[256] = "", text[2048] = "";
			lc_choice_alert( pb.constData(), problem, title, sizeof title, text, sizeof text );
			coreAlert( parent, title, text );
		}
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
	if ( !job || count == 0 ) {
		if ( job )
			lc_copyjob_free( job );
		char title[256] = "", text[1024] = "";
		lc_nothing_to_add_alert( 0, present, title, sizeof title, text, sizeof text );
		coreAlert( parent, title, text );
		return total;
	}
	if ( !freeSpaceCheck( parent, job ) ) {
		lc_copyjob_free( job );
		return total;
	}
	QString e;
	const int r = runJob( parent, job, &e );
	if ( r == 0 )
		failureAlert( parent, job, e );
	lc_copyjob_free( job );
	return r > 0 ? total + count : total;
}

}	/* namespace DataUi */
