#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <functional>
#include <qcollator.h>
#include <vector>
#include <qversionnumber.h>

#if defined _WIN32
#define UPDATE_FILE_EXTENSION QLatin1String(".exe")
#elif defined __APPLE__
#define UPDATE_FILE_EXTENSION QLatin1String(".dmg")
#else
#define UPDATE_FILE_EXTENSION QLatin1String(".AppImage")
#endif

class CAutoUpdaterGithub final : public QObject {
  Q_OBJECT

 public:
  struct VersionEntry {
    QString versionString;
    QString versionChanges;
    QString versionUpdateUrl;
    QString versionUpdateFilename;

    inline bool operator > (const VersionEntry& str) const
    {
        auto currentVersion = QVersionNumber::fromString(versionString);
        auto otherVersion = QVersionNumber::fromString(str.versionString);

        return currentVersion > otherVersion;
    }
  };

  using ChangeLog = std::vector<VersionEntry>;

  struct UpdateStatusListener {
    virtual ~UpdateStatusListener() = default;
    // If no updates are found, the changelog is empty
    virtual void onUpdateAvailable(const ChangeLog& changelog) = 0;
    virtual void onUpdateDownloadProgress(float percentageDownloaded) = 0;
    virtual void onUpdateDownloadFinished() = 0;
    virtual void onUpdateError(const QString& errorMessage) = 0;
  };

 public:
  // If the string comparison functior is not supplied, case-insensitive natural
  // sorting is used (using QCollator)
  CAutoUpdaterGithub(
      QObject* parent,
      QString
          githubRepositoryName,  // Name of the repo, e. g.
                                 // VioletGiraffe/github-releases-autoupdater
      QString currentVersionString,
      QString fileNameTag =
          "",  // Name of the file, e. g. Banach3DScanner-Installer ->
               // Banach3DScanner-Installer.exe
      QString accessToken = "",
      bool allowPreRelease = false,
      const std::function<bool(const QString&, const QString&)>&
          versionStringComparatorLessThan = {});

  CAutoUpdaterGithub& operator=(const CAutoUpdaterGithub& other) = delete;

  Q_SLOT void setUpdateStatusListener(UpdateStatusListener* listener);

  Q_SLOT void checkForUpdates();
  Q_SLOT void downloadAndInstallUpdate(const QString& updateUrl,
                                       const QString& filename);

  Q_SIGNAL void cancelDownload();

 private:
  void updateCheckRequestFinished();
  void updateDownloaded();
  void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void onNewDataDownloaded();

 private:
  QFile _downloadedBinaryFile;
  const bool _allowPreRelease;
  const QString _fileNameTag;
  const QString _accessToken;
  const QString _repoName;
  const QString _currentVersionString;
  const std::function<bool(const QString&, const QString&)>
      _lessThanVersionStringComparator;

  static constexpr std::string_view RepoUrl = "https://api.github.com/repos/";

  UpdateStatusListener* _listener = nullptr;

  QNetworkAccessManager* _networkManager;
};
