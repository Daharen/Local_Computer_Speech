#include "SoxInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace {

constexpr auto kPinnedSoxVersion = "14.4.2";
constexpr auto kPinnedSoxPackageUrl =
    "https://downloads.sourceforge.net/project/sox/sox/14.4.2/sox-14-4-2-win32.zip";
constexpr auto kPinnedSoxSha256 = "31d47237959c53dbd5cbbd327a904eeb253e062d45b0bc51aef1937c29543d84";

QString joinPath(const QString& left, const QString& right) {
    return QDir(left).filePath(right);
}

QString installerLogPath(const lcs::RuntimePaths& paths) {
    return QDir(paths.logsRoot).filePath("sox_installer.log");
}

void appendInstallerLog(const lcs::RuntimePaths& paths, const QString& message) {
    QDir().mkpath(paths.logsRoot);
    QFile logFile(installerLogPath(paths));
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&logFile);
    stream << message << "\n";
}

bool removeDirIfExists(const QString& path, QString& error) {
    QFileInfo info(path);
    if (!info.exists()) {
        return true;
    }
    QDir dir(path);
    if (!dir.removeRecursively()) {
        error = QStringLiteral("Failed to remove directory: %1").arg(path);
        return false;
    }
    return true;
}

bool copyRecursively(const QString& sourcePath, const QString& targetPath, QString& error) {
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        error = QStringLiteral("Source directory does not exist: %1").arg(sourcePath);
        return false;
    }

    QDir targetDir(targetPath);
    if (!targetDir.exists() && !QDir().mkpath(targetPath)) {
        error = QStringLiteral("Failed to create destination directory: %1").arg(targetPath);
        return false;
    }

    QDirIterator it(sourcePath, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceEntryPath = it.next();
        const QFileInfo sourceEntryInfo(sourceEntryPath);
        const QString relativePath = QDir(sourcePath).relativeFilePath(sourceEntryPath);
        const QString destinationEntryPath = QDir(targetPath).filePath(relativePath);

        if (sourceEntryInfo.isDir()) {
            if (!QDir().mkpath(destinationEntryPath)) {
                error = QStringLiteral("Failed to create directory during copy: %1")
                            .arg(destinationEntryPath);
                return false;
            }
            continue;
        }

        QFile::remove(destinationEntryPath);
        if (!QFile::copy(sourceEntryPath, destinationEntryPath)) {
            error = QStringLiteral("Failed to copy file %1 to %2")
                        .arg(sourceEntryPath, destinationEntryPath);
            return false;
        }
    }

    return true;
}

QString computeSha256(const QString& filePath, QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Failed to open downloaded package for hashing: %1").arg(file.errorString());
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 256));
    }

    return QString::fromLatin1(hash.result().toHex()).toLower();
}

bool defaultDownloadFile(const QString& url, const QString& destinationFile, QString& error) {
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        error = QStringLiteral("Download failed: %1").arg(reply->errorString());
        reply->deleteLater();
        return false;
    }

    QSaveFile output(destinationFile);
    if (!output.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Failed to open destination file: %1").arg(output.errorString());
        reply->deleteLater();
        return false;
    }

    output.write(reply->readAll());
    if (!output.commit()) {
        error = QStringLiteral("Failed to save downloaded archive atomically.");
        reply->deleteLater();
        return false;
    }

    reply->deleteLater();
    return true;
}

bool defaultExtractArchive(const QString& archiveFile, const QString& destinationDir, QString& error) {
#ifdef Q_OS_WIN
    QDir().mkpath(destinationDir);
    QProcess process;
    QStringList args = {
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(QDir::toNativeSeparators(archiveFile), QDir::toNativeSeparators(destinationDir)),
    };
    process.start("powershell.exe", args);
    if (!process.waitForFinished(120000)) {
        error = QStringLiteral("Archive extraction timed out.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = QStringLiteral("Expand-Archive failed: %1")
                    .arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
        return false;
    }
    return true;
#else
    Q_UNUSED(archiveFile)
    Q_UNUSED(destinationDir)
    error = QStringLiteral("Managed SoX installation is currently supported on Windows only.");
    return false;
#endif
}

QString findSoxExeRecursively(const QString& rootDir) {
    QDirIterator it(rootDir, QStringList() << "sox.exe", QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return QDir::fromNativeSeparators(it.next());
    }
    return {};
}

} // namespace

namespace lcs {

SoxInstaller::SoxInstaller(Ops ops) : m_ops(std::move(ops)) {
    if (!m_ops.downloadFile) {
        m_ops.downloadFile = defaultDownloadFile;
    }
    if (!m_ops.extractArchive) {
        m_ops.extractArchive = defaultExtractArchive;
    }
}

QString SoxInstaller::pinnedVersion() {
    return QString::fromLatin1(kPinnedSoxVersion);
}

QString SoxInstaller::pinnedPackageUrl() {
    return QString::fromLatin1(kPinnedSoxPackageUrl);
}

QString SoxInstaller::pinnedPackageSha256() {
    return QString::fromLatin1(kPinnedSoxSha256);
}

QString SoxInstaller::managedToolsRoot(const RuntimePaths& paths) {
    return joinPath(paths.largeDataRoot, "tools/sox");
}

QString SoxInstaller::managedVersionDir(const RuntimePaths& paths) {
    return joinPath(managedToolsRoot(paths), pinnedVersion());
}

QString SoxInstaller::managedCurrentDir(const RuntimePaths& paths) {
    return joinPath(managedToolsRoot(paths), "current");
}

QString SoxInstaller::managedCurrentExe(const RuntimePaths& paths) {
    return joinPath(managedCurrentDir(paths), "sox.exe");
}

SoxProbeResult SoxInstaller::probeExisting(const RuntimePaths& paths) const {
    const QString largeDataBundled = joinPath(paths.largeDataRoot, "tools/sox/sox.exe");
    const QString repoBundled = joinPath(paths.repoRoot, "third_party/sox/sox.exe");
    const QString systemOnPath = QStandardPaths::findExecutable(QStringLiteral("sox"));
    const QString managedCurrent = managedCurrentExe(paths);

    const struct {
        QString path;
        QString source;
    } candidates[] = {
        {largeDataBundled, QStringLiteral("bundled (large-data root)")},
        {repoBundled, QStringLiteral("bundled (repo-local)")},
    };

    for (const auto& candidate : candidates) {
        const QFileInfo info(candidate.path);
        if (info.exists() && info.isFile()) {
            return {true, info.absoluteFilePath(), candidate.source};
        }
    }

    if (!systemOnPath.isEmpty()) {
        return {true,
                QDir::fromNativeSeparators(systemOnPath),
                QStringLiteral("system PATH")};
    }

    const QFileInfo managedInfo(managedCurrent);
    if (managedInfo.exists() && managedInfo.isFile()) {
        return {true,
                managedInfo.absoluteFilePath(),
                QStringLiteral("managed install (%1)").arg(pinnedVersion())};
    }

    return {};
}

InstallResult SoxInstaller::ensureSoxAvailable(const RuntimePaths& paths) const {
    const SoxProbeResult existingProbe = probeExisting(paths);
    if (existingProbe.available) {
        return {true, existingProbe.soxExePath, existingProbe.source, {}};
    }

#ifdef Q_OS_WIN
    appendInstallerLog(paths,
                       QStringLiteral("Starting SoX managed install. version=%1 url=%2")
                           .arg(pinnedVersion(), pinnedPackageUrl()));

    const QString toolsRoot = managedToolsRoot(paths);
    const QString versionDir = managedVersionDir(paths);
    const QString currentDir = managedCurrentDir(paths);
    const QString currentExe = managedCurrentExe(paths);
    const QString versionExe = joinPath(versionDir, "sox.exe");

    if (QFileInfo::exists(versionExe)) {
        QString error;
        if (!removeDirIfExists(currentDir, error)) {
            appendInstallerLog(paths, error);
            return {false, {}, {}, error};
        }
        if (!copyRecursively(versionDir, currentDir, error)) {
            appendInstallerLog(paths, error);
            return {false, {}, {}, error};
        }
        if (QFileInfo::exists(currentExe)) {
            appendInstallerLog(paths,
                               QStringLiteral("Reused installed pinned SoX version at %1").arg(versionExe));
            return {true,
                    QDir::fromNativeSeparators(currentExe),
                    QStringLiteral("managed install (%1)").arg(pinnedVersion()),
                    {}};
        }
    }

    QDir().mkpath(toolsRoot);
    QDir().mkpath(paths.tempRoot);

    const QString archivePath = joinPath(paths.tempRoot,
                                         QStringLiteral("sox_%1.zip").arg(pinnedVersion()));
    const QString extractRoot = joinPath(paths.tempRoot,
                                         QStringLiteral("sox_extract_%1").arg(pinnedVersion()));
    QString error;

    if (!m_ops.downloadFile(pinnedPackageUrl(), archivePath, error)) {
        appendInstallerLog(paths, QStringLiteral("Download failure: %1").arg(error));
        return {false, {}, {}, QStringLiteral("Unable to download SoX package: %1").arg(error)};
    }

    const QString computedHash = computeSha256(archivePath, error);
    if (computedHash.isEmpty()) {
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (computedHash != pinnedPackageSha256()) {
        QFile::remove(archivePath);
        appendInstallerLog(paths,
                           QStringLiteral("Checksum mismatch. expected=%1 actual=%2")
                               .arg(pinnedPackageSha256(), computedHash));
        return {false,
                {},
                {},
                QStringLiteral("SoX package checksum mismatch. expected=%1 actual=%2")
                    .arg(pinnedPackageSha256(), computedHash)};
    }

    if (!removeDirIfExists(extractRoot, error)) {
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (!m_ops.extractArchive(archivePath, extractRoot, error)) {
        removeDirIfExists(extractRoot, error);
        appendInstallerLog(paths, QStringLiteral("Extraction failure: %1").arg(error));
        return {false, {}, {}, QStringLiteral("Unable to extract SoX package: %1").arg(error)};
    }

    const QString extractedExePath = findSoxExeRecursively(extractRoot);
    if (extractedExePath.isEmpty()) {
        removeDirIfExists(extractRoot, error);
        appendInstallerLog(paths, QStringLiteral("Extraction completed but sox.exe not found."));
        return {false, {}, {}, QStringLiteral("sox.exe was not found in extracted archive.")};
    }

    const QString extractedDir = QFileInfo(extractedExePath).absolutePath();

    if (!removeDirIfExists(versionDir, error)) {
        removeDirIfExists(extractRoot, error);
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (!copyRecursively(extractedDir, versionDir, error)) {
        removeDirIfExists(versionDir, error);
        removeDirIfExists(extractRoot, error);
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (!removeDirIfExists(currentDir, error)) {
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (!copyRecursively(versionDir, currentDir, error)) {
        appendInstallerLog(paths, error);
        return {false, {}, {}, error};
    }

    if (!QFileInfo::exists(currentExe)) {
        appendInstallerLog(paths, QStringLiteral("current/sox.exe missing after install."));
        return {false, {}, {}, QStringLiteral("Managed install completed but current/sox.exe missing.")};
    }

    removeDirIfExists(extractRoot, error);
    appendInstallerLog(paths, QStringLiteral("SoX install completed successfully: %1").arg(currentExe));
    return {
        true,
        QDir::fromNativeSeparators(currentExe),
        QStringLiteral("managed install (%1)").arg(pinnedVersion()),
        {},
    };
#else
    return {
        false,
        {},
        {},
        QStringLiteral(
            "SoX not found in bundled paths or system PATH. Managed install is only supported on Windows."),
    };
#endif
}

} // namespace lcs
