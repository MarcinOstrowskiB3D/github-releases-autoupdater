#include "cautoupdatergithub.h"
#include "updateinstaller.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <qjsonarray.h>

#include <qthread.h>
#include <qtimer.h>

#include <assert.h>
#include <utility>

static const auto naturalSortQstringComparator = [](const QString& l, const QString& r) {
	static QCollator collator;
	collator.setNumericMode(true);
	collator.setCaseSensitivity(Qt::CaseInsensitive);

	// Fix for the new breaking changes in QCollator in Qt 5.14 - null strings are no longer a valid input
	return collator.compare(qToStringViewIgnoringNull(l), qToStringViewIgnoringNull(r)) < 0;
};

CAutoUpdaterGithub::CAutoUpdaterGithub(QObject* parent, QString githubRepositoryName, QString currentVersionString, QString fileNameTag, QString accessToken, const std::function<bool (const QString&, const QString&)>& versionStringComparatorLessThan) :
	QObject(parent),
	_repoName(std::move(githubRepositoryName)),
	_currentVersionString(std::move(currentVersionString)),
	_fileNameTag(std::move(fileNameTag)),
	_accessToken(accessToken),
	_lessThanVersionStringComparator(versionStringComparatorLessThan ? versionStringComparatorLessThan : naturalSortQstringComparator),
	_networkManager(new QNetworkAccessManager(this))
{
	assert(_repoName.count(QChar('/')) == 1);
	assert(!_currentVersionString.isEmpty());
}

void CAutoUpdaterGithub::setUpdateStatusListener(UpdateStatusListener* listener)
{
	_listener = listener;
}

void CAutoUpdaterGithub::checkForUpdates() {
	QNetworkRequest request;
	request.setUrl(QUrl(QString(RepoUrl.data()) + _repoName));
	request.setRawHeader("Accept", "application/vnd.github+json");

	// set access token if enabled:
	if (!_accessToken.isEmpty()) {
		QString tokenValue = "token " + _accessToken;
		request.setRawHeader("Authorization", tokenValue.toUtf8());
	}
	
	QNetworkReply * reply = _networkManager->get(request);
	if (!reply)
	{
		if (_listener)
			_listener->onUpdateError("Network request rejected.");
		return;
	}

	connect(reply, &QNetworkReply::finished, this, &CAutoUpdaterGithub::updateCheckRequestFinished);
}

void CAutoUpdaterGithub::downloadAndInstallUpdate(const QString& updateUrl, const QString& filename)
{
	assert(!_downloadedBinaryFile.isOpen());

	_downloadedBinaryFile.setFileName(QDir::tempPath() + '/' + filename);
	if (!_downloadedBinaryFile.open(QFile::WriteOnly))
	{
		if (_listener)
			_listener->onUpdateError("Failed to open temporary file " + _downloadedBinaryFile.fileName());
		return;
	}

	QNetworkRequest request((QUrl(updateUrl)));

	// set access token if enabled:
	if (!_accessToken.isEmpty()) {
		QString tokenValue = "token " + _accessToken;
		request.setRawHeader("Authorization", tokenValue.toUtf8());
	}
	
	request.setRawHeader("Accept", "application/octet-stream");
	request.setSslConfiguration(QSslConfiguration::defaultConfiguration()); // HTTPS
	request.setMaximumRedirectsAllowed(5);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply * reply = _networkManager->get(request);
	if (!reply)
	{
		if (_listener)
			_listener->onUpdateError("Network request rejected.");
		return;
	}

	connect(reply, &QNetworkReply::readyRead, this, &CAutoUpdaterGithub::onNewDataDownloaded);
	connect(reply, &QNetworkReply::downloadProgress, this, &CAutoUpdaterGithub::onDownloadProgress);
	connect(reply, &QNetworkReply::finished, this, &CAutoUpdaterGithub::updateDownloaded, Qt::UniqueConnection);
}

void CAutoUpdaterGithub::updateCheckRequestFinished()
{
	auto* reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;

	QSharedPointer<QNetworkReply> replyPtr {reply, & QNetworkReply::deleteLater};

	if (replyPtr->error() != QNetworkReply::NoError)
	{
		if (_listener)
			_listener->onUpdateError(replyPtr->errorString());
		return;
	}

	if (replyPtr->bytesAvailable() <= 0)
	{
		if (_listener)
			_listener->onUpdateError("No data downloaded.");

		return;
	}

	ChangeLog changelog;

	QJsonParseError jsonError;
	const QJsonDocument jsonDocument = QJsonDocument::fromJson(replyPtr->readAll(),&jsonError);
	if (jsonError.error != QJsonParseError::NoError) {
		if (_listener)
			_listener->onUpdateError("Failed to parse json data");

		return;
	}

	if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
		if (_listener)
			_listener->onUpdateError("Failed to read data from update server. No processing data!");
		return;
	}

	//qDebug() << jsonDocument.object();

	auto parseReleaseJsonObject = [&](const QJsonObject & object){
		QString updateVersion = object["tag_name"].toString();

		// found properly update file extension:
		auto assetsObject = object["assets"];

		QJsonArray assetsJsonArray;
		if (assetsObject.isArray()) {
			assetsJsonArray = assetsObject.toArray();
		}
		else {
			assetsJsonArray = QJsonArray({ assetsObject.toObject() });
		}

		QUrl url;
		QString filename;

		for (const auto& asset : assetsJsonArray) {
			// Get binaries with only valid extension.
			auto assetObject = asset.toObject();

			auto browserUrl = assetObject["browser_download_url"].toString();
			filename = QUrl(browserUrl).fileName();

			if (filename.indexOf(UPDATE_FILE_EXTENSION) < 0) {
				continue;
			}

			// Get binaries with only valid filename.
			if (!_fileNameTag.isEmpty() && filename.indexOf(_fileNameTag) < 0) {
				continue;
			}

			// Generate url link:	
			const auto assetIdUrl = QVariant(assetObject["id"].toInt()).toString();

			url = QString(RepoUrl.data())
				.append(_repoName)
				.append("/")
				.append("assets")
				.append("/")
				.append(assetIdUrl);

			break;
		}

		if (updateVersion.startsWith(QStringLiteral(".v"))) {
			updateVersion = updateVersion.remove(0, 2);
		}
		else if (updateVersion.startsWith('v')) {
			updateVersion = updateVersion.remove(0, 1);
		}
		if (updateVersion.startsWith('#')) {
			updateVersion = updateVersion.remove(0, 1);
		}

		if (!naturalSortQstringComparator(_currentVersionString, updateVersion)) {
			return;
		}

		if (url.isEmpty()) {
			return;
		}

		const QString updateChanges = object["body"].toString();
		changelog.push_back({ updateVersion, updateChanges, /*!url.isEmpty() ? url : releaseUrl*/ url.toString(), filename });
	};

	if (jsonDocument.isArray()) {
		auto jsonArray = jsonDocument.array();

		// Skipping the 0 item because anything before the first "release-header" is not a release. Ignore skipping when jsonDocument hasn't array.
		for (qsizetype releaseIndex = 0, numItems = jsonArray.size(); releaseIndex < numItems; ++releaseIndex) {
			parseReleaseJsonObject(jsonArray.at(releaseIndex).toObject());
		}
	}
	else {
		parseReleaseJsonObject(jsonDocument.object());
	}

	if (_listener)
		_listener->onUpdateAvailable(changelog);
}

void CAutoUpdaterGithub::updateDownloaded()
{
	_downloadedBinaryFile.close();

	auto* reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) 
	{
		return;
	}

	QSharedPointer<QNetworkReply> replyPtr {reply, & QNetworkReply::deleteLater};

	if (replyPtr->error() != QNetworkReply::NoError)
	{
		if (_listener)
			_listener->onUpdateError(replyPtr->errorString());

		return;
	}

	if (_listener) 
	{
		_listener->onUpdateDownloadFinished();
	}

	if (!UpdateInstaller::install(_downloadedBinaryFile.fileName()) && _listener)
	{
		_listener->onUpdateError("Failed to launch the downloaded update.");
	}
	else {
		exit(EXIT_SUCCESS); // force close program after successfully update installer launch
	}
}

void CAutoUpdaterGithub::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
	if (_listener)
	{
		_listener->onUpdateDownloadProgress(bytesReceived < bytesTotal ? static_cast<float>(bytesReceived * 100) / static_cast<float>(bytesTotal) : 100.0f);
	}
}

void CAutoUpdaterGithub::onNewDataDownloaded()
{
	auto* reply = qobject_cast<QNetworkReply*>(sender());
	if (!reply)
		return;

	_downloadedBinaryFile.write(reply->readAll());
}
