#pragma once

#include "PathResolver.h"

#include <QString>

#include <functional>

namespace lcs {

struct InstallResult {
    bool ok = false;
    QString soxExePath;
    QString source;
    QString error;
};

struct SoxProbeResult {
    bool available = false;
    QString soxExePath;
    QString source;
};

class SoxInstaller {
public:
    struct Ops {
        std::function<bool(const QString& url, const QString& destinationFile, QString& error)> downloadFile;
        std::function<bool(const QString& archiveFile, const QString& destinationDir, QString& error)>
            extractArchive;
    };

    explicit SoxInstaller(Ops ops = {});
    virtual ~SoxInstaller() = default;

    virtual SoxProbeResult probeExisting(const RuntimePaths& paths) const;
    virtual InstallResult ensureSoxAvailable(const RuntimePaths& paths) const;

    static QString pinnedVersion();
    static QString pinnedPackageUrl();
    static QString pinnedPackageSha256();

    static QString managedToolsRoot(const RuntimePaths& paths);
    static QString managedVersionDir(const RuntimePaths& paths);
    static QString managedCurrentDir(const RuntimePaths& paths);
    static QString managedCurrentExe(const RuntimePaths& paths);

private:
    Ops m_ops;
};

} // namespace lcs
