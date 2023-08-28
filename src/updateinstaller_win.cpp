#include "updateinstaller.hpp"

#include <QProcess>

bool UpdateInstaller::install(const QString& downloadedUpdateFilePath)
{
	auto result = QProcess::startDetached('\"' + downloadedUpdateFilePath + '\"', {});
	return result;
}
