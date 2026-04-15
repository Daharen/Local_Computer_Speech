#pragma once

#include <QString>

namespace lcs {

struct RuntimePaths {
    QString projectRoot;
    QString toolsRoot;
    QString repoRoot;
    QString largeDataRoot;
    QString pythonEnvRoot;
    QString modelsRoot;
    QString logsRoot;
    QString cacheRoot;
    QString runtimeRoot;
    QString outputRoot;
    QString tempRoot;

    QString tokenizerPath;
    QString modelPath;
    QString backendPackageRoot;
    QString backendCliPath;
    QString backendPythonExe;
};

class PathResolver {
public:
    static RuntimePaths resolve();
};

} // namespace lcs
