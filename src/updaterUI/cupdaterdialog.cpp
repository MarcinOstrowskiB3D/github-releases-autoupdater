#include "cupdaterdialog.h"
#include "ui_cupdaterdialog.h"

#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>
#include <qthread.h>
#include <qtimer.h>

CUpdaterDialog::CUpdaterDialog(QWidget *parent, const QString& githubRepoName, const QString& versionString, const QString fileNameTag, const QString accessToken, bool silentCheck) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_silent(silentCheck),
	_updater(new CAutoUpdaterGithub(nullptr, githubRepoName, versionString, fileNameTag, accessToken)),
	_updaterThread(new QThread)
{
	ui->setupUi(this);

	if (_silent)
		hide();

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CUpdaterDialog::applyUpdate);
	connect(this, &QDialog::finished, _updater, &CAutoUpdaterGithub::cancelDownload);

	ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Install"));

	ui->stackedWidget->setCurrentIndex(0);
	ui->progressBar->setMaximum(0);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(false);
	ui->changeLogViewer->setWordWrapMode(QTextOption::WrapMode::WordWrap);

	_updater->moveToThread(_updaterThread);
	_updater->setUpdateStatusListener(this);
	_updaterThread->start();

	QTimer::singleShot(std::chrono::milliseconds(1), _updater, &CAutoUpdaterGithub::checkForUpdates);
}

CUpdaterDialog::~CUpdaterDialog()
{
	delete ui;

	if (_updater) {
		delete _updater;
		_updater = nullptr;
	}

	if (_updaterThread) {
		_updaterThread->quit();
		_updaterThread->wait();

		delete _updaterThread;
		_updaterThread = nullptr;
	}
}

void CUpdaterDialog::applyUpdate()
{
	QMetaObject::invokeMethod(this, [this] {

#ifdef _WIN32
		if (_latestUpdateFilename.endsWith(UPDATE_FILE_EXTENSION))
		{
			ui->progressBar->setMaximum(100);
			ui->progressBar->setValue(0);
			ui->lblPercentage->setVisible(true);
			ui->lblOperationInProgress->setText(tr("Downloading the update..."));
			ui->stackedWidget->setCurrentIndex(0);

			QMetaObject::invokeMethod(_updater, [&] {_updater->downloadAndInstallUpdate(_latestUpdateUrl, _latestUpdateFilename); });
		}
		else {
			QDesktopServices::openUrl(QUrl(_latestUpdateUrl));
			accept();
		}
#else
		QMessageBox msg(
			QMessageBox::Question,
			tr("Manual update required"),
			tr("Automatic update is not supported on this operating system. Do you want to download and install the update manually?"),
			QMessageBox::Yes | QMessageBox::No,
			this);

		if (msg.exec() == QMessageBox::Yes)
			QDesktopServices::openUrl(QUrl(_latestUpdateUrl));
#endif

	});
}

// If no updates are found, the change log is empty
void CUpdaterDialog::onUpdateAvailable(const CAutoUpdaterGithub::ChangeLog& changelog)
{
	QMetaObject::invokeMethod(this, [this, changelog=changelog] {
		if (!changelog.empty())
		{
			ui->stackedWidget->setCurrentIndex(1);
			for (const auto& changelogItem : changelog) {
				// modified text plain formatter tags to html tags.
				auto versionChanges = changelogItem.versionChanges;
				versionChanges.replace("\r\n", "<br />");
				versionChanges.replace("\n", "<br />");
				versionChanges.replace("\r", "<br />");

				ui->changeLogViewer->append("<b>" % changelogItem.versionString % "</b>" % "<br />" % versionChanges % "<p></p>");
			}

			_latestUpdateUrl = changelog.front().versionUpdateUrl;
			_latestUpdateFilename = changelog.front().versionUpdateFilename;

			QMetaObject::invokeMethod(this, [&] { show(); });
		}
		else
		{
			accept();
			if (!_silent)
				QMessageBox::information(this, tr("No update available"), tr("You already have the latest version of the program."));
		}
	});
}

// percentageDownloaded >= 100.0f means the download has finished
void CUpdaterDialog::onUpdateDownloadProgress(float percentageDownloaded) {
	QMetaObject::invokeMethod(this, [this, percentageDownloaded=percentageDownloaded] {
		ui->progressBar->setValue((int)percentageDownloaded);
		ui->lblPercentage->setText(QString::number(percentageDownloaded, 'f', 2) + " %");
	});
}

void CUpdaterDialog::onUpdateDownloadFinished() {
	QMetaObject::invokeMethod(this, [this] {
		accept();
	});	
}

void CUpdaterDialog::onUpdateError(const QString& errorMessage) {
	QMetaObject::invokeMethod(this, [this, errorMessage = errorMessage] {
		reject();
		if (!_silent)
			QMessageBox::critical(this, tr("Error checking for updates"), tr(errorMessage.toUtf8().data()));
		});
}
