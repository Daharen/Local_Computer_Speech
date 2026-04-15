#include "BackendBridge.h"

#include "PathResolver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include <memory>

namespace {
QByteArray findJsonPayload(const QString& stdoutText) {
    const auto lines = stdoutText.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (line.startsWith('{') && line.endsWith('}')) {
            return line.toUtf8();
        }
    }

    const int start = stdoutText.indexOf('{');
    const int end = stdoutText.lastIndexOf('}');
    if (start >= 0 && end > start) {
        return stdoutText.mid(start, end - start + 1).toUtf8();
    }

    return {};
}
} // namespace

namespace lcs {

BackendBridge::BackendBridge(QObject* parent) : QObject(parent), m_process(nullptr) {
    qRegisterMetaType<lcs::SynthResult>("lcs::SynthResult");
}

QString BackendBridge::quickStatusSummary() const {
    const auto paths = PathResolver::resolve();

    const bool hasTokenizer = QFileInfo::exists(paths.tokenizerPath);
    const bool hasModel = QFileInfo::exists(paths.modelPath);
    const bool hasBackendPython = QFileInfo::exists(paths.backendPythonExe);

    if (!hasBackendPython) {
        return QStringLiteral(
            "Backend Python not found in persistent env. Run run.ps1 -SetupBackend to create python_env.");
    }

    if (!hasTokenizer || !hasModel) {
        return QStringLiteral(
            "Model assets missing. Run run.ps1 -InstallModel to install tokenizer + model into large-data root.");
    }

    return QStringLiteral(
        "Backend bridge ready: local model/tokenizer paths found in persistent large-data root.");
}

bool BackendBridge::isSynthesisInProgress() const {
    return m_process != nullptr;
}

QString BackendBridge::buildOutputFileName() const {
    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    return QStringLiteral("tts_%1.wav").arg(stamp);
}

void BackendBridge::finishWithError(const QString& error) {
    SynthResult result;
    result.ok = false;
    result.error = error;
    Q_EMIT synthesisCompleted(result);
}

bool BackendBridge::startSynthesis(const QString& text) {
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        finishWithError("Input text is empty. Please type text before synthesizing.");
        return false;
    }

    if (m_process) {
        finishWithError("Synthesis is already running. Please wait for the current request to finish.");
        return false;
    }

    const auto paths = PathResolver::resolve();
    if (!QFileInfo::exists(paths.backendPythonExe)) {
        finishWithError(
            QStringLiteral("Backend Python executable not found at: %1").arg(paths.backendPythonExe));
        return false;
    }

    QDir().mkpath(paths.outputRoot);
    QDir().mkpath(paths.runtimeRoot);

    const QString requestDir = QDir(paths.runtimeRoot).filePath("requests");
    QDir().mkpath(requestDir);

    const QString requestFilePath =
        QDir(requestDir).filePath(
            QStringLiteral("request_%1_%2.json")
                .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz"))
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    const QString outputPath = QDir(paths.outputRoot).filePath(buildOutputFileName());

    QJsonObject requestObj{
        {"text", trimmedText},
        {"output_path", QDir::toNativeSeparators(outputPath)},
        {"language", "English"},
        {"speaker", "Ryan"},
        {"instruct", ""},
    };

    QFile requestFile(requestFilePath);
    if (!requestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishWithError(QStringLiteral("Failed to write request JSON: %1").arg(requestFile.errorString()));
        return false;
    }
    requestFile.write(QJsonDocument(requestObj).toJson(QJsonDocument::Indented));
    requestFile.close();

    m_process = new QProcess(this);
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString existingPyPath = env.value("PYTHONPATH");
    if (existingPyPath.isEmpty()) {
        env.insert("PYTHONPATH", paths.backendPackageRoot);
    } else {
        env.insert("PYTHONPATH", paths.backendPackageRoot + ";" + existingPyPath);
    }
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrBuffer += QString::fromUtf8(m_process->readAllStandardError());
    });

    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, outputPath](int exitCode, QProcess::ExitStatus exitStatus) {
                std::unique_ptr<QProcess> processGuard(m_process);
                m_process = nullptr;

                const QByteArray jsonPayload = findJsonPayload(m_stdoutBuffer);
                QJsonParseError parseError{};
                const auto jsonDoc = QJsonDocument::fromJson(jsonPayload, &parseError);

                if (exitStatus != QProcess::NormalExit) {
                    finishWithError("Backend process crashed before completing synthesis.");
                    return;
                }

                if (exitCode != 0) {
                    finishWithError(
                        QStringLiteral("Backend returned non-zero exit (%1): %2")
                            .arg(exitCode)
                            .arg(m_stderrBuffer.trimmed()));
                    return;
                }

                if (jsonPayload.isEmpty() || parseError.error != QJsonParseError::NoError ||
                    !jsonDoc.isObject()) {
                    finishWithError(
                        QStringLiteral("Malformed backend JSON response. stdout: %1 stderr: %2")
                            .arg(m_stdoutBuffer.trimmed(), m_stderrBuffer.trimmed()));
                    return;
                }

                const auto obj = jsonDoc.object();
                SynthResult result;
                result.ok = obj.value("ok").toBool(false);
                result.outputPath = obj.value("output_path").toString();
                result.sampleRate = obj.value("sample_rate").toInt(0);
                result.speaker = obj.value("speaker").toString();
                result.language = obj.value("language").toString();
                result.elapsedMs = static_cast<qint64>(obj.value("elapsed_ms").toDouble(0));
                result.device = obj.value("device").toString();
                result.error = obj.value("error").toString();

                if (!result.ok) {
                    if (result.error.isEmpty()) {
                        result.error = "Backend reported synthesis failure with no error message.";
                    }
                    Q_EMIT synthesisCompleted(result);
                    return;
                }

                const QString expectedPath = result.outputPath.isEmpty() ? outputPath : result.outputPath;
                if (!QFileInfo::exists(QDir::fromNativeSeparators(expectedPath))) {
                    result.ok = false;
                    result.error = QStringLiteral("Backend reported success but output file is missing: %1")
                                       .arg(expectedPath);
                    Q_EMIT synthesisCompleted(result);
                    return;
                }

                result.outputPath = QDir::toNativeSeparators(expectedPath);
                Q_EMIT synthesisCompleted(result);
            });

    Q_EMIT synthesisStarted();

    const QStringList args = {
        "-m",
        "local_computer_speech_backend.cli",
        "synth",
        "--request-json",
        QDir::toNativeSeparators(requestFilePath),
    };
    m_process->start(paths.backendPythonExe, args);

    if (!m_process->waitForStarted(3000)) {
        std::unique_ptr<QProcess> processGuard(m_process);
        m_process = nullptr;
        finishWithError(QStringLiteral("Failed to start backend process: %1")
                            .arg(processGuard->errorString()));
        return false;
    }

    return true;
}

} // namespace lcs
