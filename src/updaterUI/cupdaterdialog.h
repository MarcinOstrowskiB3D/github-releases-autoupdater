#pragma once

#include "../cautoupdatergithub.h"
#include <QDialog>


namespace Ui {
	class CUpdaterDialog;
}

class CAutoUpdaterGithub;

class CUpdaterDialog final : public QDialog, private CAutoUpdaterGithub::UpdateStatusListener
{
public:
	explicit CUpdaterDialog(QWidget *parent,
							const QString& githubRepoName, // Name of the repo, e. g. VioletGiraffe/github-releases-autoupdater
							const QString& versionString,
							const QString fileNameTag = "",
							const QString accessToken = "",
							bool silentCheck = false);
	~CUpdaterDialog() override;

private:
	void applyUpdate();

private:
	// If no updates are found, the changelog is empty
	void onUpdateAvailable(const CAutoUpdaterGithub::ChangeLog& changelog) override;
	// percentageDownloaded >= 100.0f means the download has finished
	void onUpdateDownloadProgress(float percentageDownloaded) override;
	void onUpdateDownloadFinished() override;
	void onUpdateError(const QString& errorMessage) override;

private:
	Ui::CUpdaterDialog *ui;
	const bool _silent;

	QString _latestUpdateUrl;
	QString _latestUpdateFilename;
	CAutoUpdaterGithub _updater;
};

