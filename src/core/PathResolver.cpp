#include "PathResolver.h"

#include <QDir>
#include <QProcessEnvironment>

namespace {
QString envOrDefault(const QString& name, const QString& fallback = QString()) {
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains(name) && !env.value(name).trimmed().isEmpty()) {
        return QDir::fromNativeSeparators(env.value(name));
    }
    return QDir::fromNativeSeparators(fallback);
}

QString joinPath(const QString& left, const QString& right) {
    return QDir(left).filePath(right);
}
} // namespace

namespace lcs {

RuntimePaths PathResolver::resolve() {
    RuntimePaths paths;
    paths.projectRoot = envOrDefault("LOCAL_COMPUTER_SPEECH_PROJECT_ROOT");
    paths.toolsRoot = envOrDefault("LOCAL_COMPUTER_SPEECH_TOOLS_ROOT");
    paths.repoRoot = envOrDefault("LOCAL_COMPUTER_SPEECH_REPO_ROOT", QDir::currentPath());
    paths.largeDataRoot = envOrDefault(
        "LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT",
        "F:/My_Programs/Local_Computer_Speech_Large_Data");

    paths.pythonEnvRoot = joinPath(paths.largeDataRoot, "python_env");
    paths.modelsRoot = joinPath(paths.largeDataRoot, "models");
    paths.logsRoot = joinPath(paths.largeDataRoot, "logs");
    paths.cacheRoot = joinPath(paths.largeDataRoot, "cache");
    paths.runtimeRoot = joinPath(paths.largeDataRoot, "runtime");
    paths.outputRoot = joinPath(paths.largeDataRoot, "output");
    paths.tempRoot = joinPath(paths.largeDataRoot, "temp");

    paths.tokenizerPath = joinPath(paths.modelsRoot, "qwen/Qwen3-TTS-Tokenizer-12Hz");
    paths.modelPath = joinPath(paths.modelsRoot, "qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice");
    paths.fastModelPath = joinPath(paths.modelsRoot, "qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice");
    paths.backendPackageRoot = joinPath(paths.repoRoot, "backend");
    paths.backendCliPath = joinPath(paths.backendPackageRoot, "local_computer_speech_backend/cli.py");
    paths.backendPythonExe = joinPath(paths.pythonEnvRoot, "Scripts/python.exe");

    return paths;
}

} // namespace lcs
