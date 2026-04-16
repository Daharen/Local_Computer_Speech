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
    void pinnedPackageHashAllowlistContainsKnownValues();
    void acceptsFirstKnownPinnedPackageHash();
    void acceptsSecondKnownPinnedPackageHash();
    void rejectsUnknownPinnedPackageHash();
    void ensureSoxAlreadyInstalledPinnedVersion();
    void ensureSoxDownloadFailure();
    void ensureSoxChecksumMismatchIncludesActualAndAllowlist();
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

void TestSoxInstaller::pinnedPackageHashAllowlistContainsKnownValues() {
    const QStringList hashes = SoxInstaller::pinnedPackageSha256Allowlist();
    QCOMPARE(hashes.size(), 2);
    QVERIFY(hashes.contains("cbd670e723e8f04ff9a32f221decb51a0a056a0ebe315536579b5d5e5b2fe048"));
    QVERIFY(hashes.contains("e6953e3007c13a40f64cfc448de8dce6619894487c8e7716965d2ad0f1bc349"));
}

void TestSoxInstaller::acceptsFirstKnownPinnedPackageHash() {
    QVERIFY(SoxInstaller::isPinnedPackageSha256Accepted(
        "cbd670e723e8f04ff9a32f221decb51a0a056a0ebe315536579b5d5e5b2fe048"));
}

void TestSoxInstaller::acceptsSecondKnownPinnedPackageHash() {
    QVERIFY(SoxInstaller::isPinnedPackageSha256Accepted(
        "e6953e3007c13a40f64cfc448de8dce6619894487c8e7716965d2ad0f1bc349"));
}

void TestSoxInstaller::rejectsUnknownPinnedPackageHash() {
    QVERIFY(!SoxInstaller::isPinnedPackageSha256Accepted(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
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

void TestSoxInstaller::ensureSoxChecksumMismatchIncludesActualAndAllowlist() {
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
    QVERIFY(result.error.contains("actual=", Qt::CaseInsensitive));
    QVERIFY(result.error.contains("accepted=[", Qt::CaseInsensitive));
    QVERIFY(result.error.contains("cbd670e723e8f04ff9a32f221decb51a0a056a0ebe315536579b5d5e5b2fe048",
                                  Qt::CaseInsensitive));
    QVERIFY(result.error.contains("e6953e3007c13a40f64cfc448de8dce6619894487c8e7716965d2ad0f1bc349",
                                  Qt::CaseInsensitive));
#else
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("Windows only", Qt::CaseInsensitive));
#endif
}

} // namespace
} // namespace lcs

QTEST_MAIN(lcs::TestSoxInstaller)
#include "TestSoxInstaller.moc"
