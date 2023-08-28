#include "cupdaterdialog.h"
#include "ui_cupdaterdialog.h"

#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>

CUpdaterDialog::CUpdaterDialog(QWidget *parent, const QString& githubRepoName, const QString& versionString, const QString fileNameTag, const QString accessToken, bool silentCheck) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_silent(silentCheck),
	_updater(githubRepoName, versionString, fileNameTag, accessToken)
{
	ui->setupUi(this);

	if (_silent)
		hide();

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CUpdaterDialog::applyUpdate);
	ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Install"));

	ui->stackedWidget->setCurrentIndex(0);
	ui->progressBar->setMaximum(0);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(false);
	ui->changeLogViewer->setWordWrapMode(QTextOption::WrapMode::WordWrap);

	_updater.setUpdateStatusListener(this);
	_updater.checkForUpdates();
}

CUpdaterDialog::~CUpdaterDialog()
{
	delete ui;
}

void CUpdaterDialog::applyUpdate()
{
#ifdef _WIN32
	if (_latestUpdateFilename.endsWith(UPDATE_FILE_EXTENSION))
	{
		ui->progressBar->setMaximum(100);
		ui->progressBar->setValue(0);
		ui->lblPercentage->setVisible(true);
		ui->lblOperationInProgress->setText(tr("Downloading the update..."));
		ui->stackedWidget->setCurrentIndex(0);

		_updater.downloadAndInstallUpdate(_latestUpdateUrl, _latestUpdateFilename);
	} else {
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
}

// If no updates are found, the change log is empty
void CUpdaterDialog::onUpdateAvailable(const CAutoUpdaterGithub::ChangeLog& changelog)
{
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

		show();
	}
	else
	{
		accept();
		if (!_silent)
			QMessageBox::information(this, tr("No update available"), tr("You already have the latest version of the program."));
	}
}

// percentageDownloaded >= 100.0f means the download has finished
void CUpdaterDialog::onUpdateDownloadProgress(float percentageDownloaded)
{
	ui->progressBar->setValue((int)percentageDownloaded);
	ui->lblPercentage->setText(QString::number(percentageDownloaded, 'f', 2) + " %");
}

void CUpdaterDialog::onUpdateDownloadFinished()
{
	accept();
}

void CUpdaterDialog::onUpdateError(const QString& errorMessage)
{
	reject();
	if (!_silent)
		QMessageBox::critical(this, tr("Error checking for updates"), tr(errorMessage.toUtf8().data()));
}
