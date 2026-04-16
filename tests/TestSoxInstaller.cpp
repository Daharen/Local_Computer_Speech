#include "core/SoxInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace lcs {
namespace {

RuntimePaths makePaths(const QString& root) {
    RuntimePaths p;
    p.repoRoot = QDir(root).filePath("repo");
    p.largeDataRoot = QDir(root).filePath("large");
    p.logsRoot = QDir(p.largeDataRoot).filePath("logs");
    p.tempRoot = QDir(p.largeDataRoot).filePath("temp");
    return p;
}

void touchFile(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write("x");
}

class TestSoxInstaller : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void probePrefersBundledThenSystemThenManaged();
    void managedPathResolutionUsesPinnedVersion();
    void ensureSoxAlreadyInstalledPinnedVersion();
    void ensureSoxDownloadFailure();
    void ensureSoxChecksumMismatch();
};

void TestSoxInstaller::probePrefersBundledThenSystemThenManaged() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    const QString largeBundled = QDir(paths.largeDataRoot).filePath("tools/sox/sox.exe");
    touchFile(largeBundled);

    SoxInstaller installer;
    const auto probe = installer.probeExisting(paths);
    QVERIFY(probe.available);
    QCOMPARE(probe.source, QStringLiteral("bundled (large-data root)"));
    QCOMPARE(QDir::fromNativeSeparators(probe.soxExePath), QDir::fromNativeSeparators(largeBundled));
}

void TestSoxInstaller::managedPathResolutionUsesPinnedVersion() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    QCOMPARE(SoxInstaller::managedToolsRoot(paths), QDir(paths.largeDataRoot).filePath("tools/sox"));
    QCOMPARE(SoxInstaller::managedVersionDir(paths),
             QDir(paths.largeDataRoot).filePath(QStringLiteral("tools/sox/%1").arg(SoxInstaller::pinnedVersion())));
    QCOMPARE(SoxInstaller::managedCurrentExe(paths), QDir(paths.largeDataRoot).filePath("tools/sox/current/sox.exe"));
}

void TestSoxInstaller::ensureSoxAlreadyInstalledPinnedVersion() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());
    touchFile(SoxInstaller::managedCurrentExe(paths));

    SoxInstaller installer;
    const auto result = installer.ensureSoxAvailable(paths);
    QVERIFY(result.ok);
    QCOMPARE(result.source, QStringLiteral("managed install (%1)").arg(SoxInstaller::pinnedVersion()));
}

void TestSoxInstaller::ensureSoxDownloadFailure() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    SoxInstaller::Ops ops;
    ops.downloadFile = [](const QString&, const QString&, QString& error) {
        error = "network down";
        return false;
    };
    ops.extractArchive = [](const QString&, const QString&, QString&) { return false; };

    SoxInstaller installer(ops);
    const auto result = installer.ensureSoxAvailable(paths);
#ifdef Q_OS_WIN
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("download", Qt::CaseInsensitive));
#else
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("Windows only", Qt::CaseInsensitive));
#endif
}

void TestSoxInstaller::ensureSoxChecksumMismatch() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    SoxInstaller::Ops ops;
    ops.downloadFile = [](const QString&, const QString& destinationFile, QString&) {
        QDir().mkpath(QFileInfo(destinationFile).absolutePath());
        QFile f(destinationFile);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        f.write("invalid-archive-content");
        return true;
    };
    ops.extractArchive = [](const QString&, const QString&, QString&) { return true; };

    SoxInstaller installer(ops);
    const auto result = installer.ensureSoxAvailable(paths);
#ifdef Q_OS_WIN
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("checksum", Qt::CaseInsensitive));
#else
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("Windows only", Qt::CaseInsensitive));
#endif
}

} // namespace
} // namespace lcs

QTEST_MAIN(lcs::TestSoxInstaller)
#include "TestSoxInstaller.moc"
