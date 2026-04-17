#include "BackendBridge.h"

#include "PathResolver.h"
#include "SoxInstaller.h"

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
QByteArray extractJsonLine(QString& buffer) {
    const int newline = buffer.indexOf('\n');
    if (newline < 0) {
        return {};
    }

    const QString line = buffer.left(newline).trimmed();
    buffer = buffer.mid(newline + 1);
    if (line.startsWith('{') && line.endsWith('}')) {
        return line.toUtf8();
    }
    return {};
}

QString mapProfileLabelToConfig(const QString& profileLabel) {
    if (profileLabel == "fast") {
        return QStringLiteral("fast_qwen_0_6b_customvoice");
    }
    return QStringLiteral("hq_qwen_1_7b_customvoice");
}

QString profilePrefix(const QString& profileName) {
    return profileName.startsWith("fast") ? QStringLiteral("fast") : QStringLiteral("hq");
}
} // namespace

namespace lcs {

BackendBridge::BackendBridge(QObject* parent)
    : BackendBridge(std::make_shared<SoxInstaller>(), parent) {}

BackendBridge::BackendBridge(std::shared_ptr<SoxInstaller> soxInstaller, QObject* parent)
    : QObject(parent), m_soxInstaller(std::move(soxInstaller)), m_process(nullptr) {
    qRegisterMetaType<lcs::SynthResult>("lcs::SynthResult");
}

QString BackendBridge::quickStatusSummary() const {
    const auto paths = PathResolver::resolve();
    const SoxProbeResult soxProbe = m_soxInstaller->probeExisting(paths);

    const bool hasTokenizer = QFileInfo::exists(paths.tokenizerPath);
    const bool hasModel = QFileInfo::exists(paths.modelPath);
    const bool hasBackendPython = QFileInfo::exists(paths.backendPythonExe);

    if (!hasBackendPython) {
        return QStringLiteral(
            "Backend Python not found in persistent env. Run run.ps1 -SetupBackend to create python_env.");
    }

    if (!hasTokenizer || !hasModel) {
        return QStringLiteral(
            "HQ model/tokenizer missing. Run run.ps1 -InstallModel to install tokenizer + models into large-data root.");
    }

    if (!soxProbe.available) {
        return QStringLiteral(
            "SoX executable not currently available. The app can bootstrap managed SoX (%1) during synthesis on demand.")
            .arg(SoxInstaller::pinnedVersion());
    }

    return QStringLiteral(
               "Backend bridge ready: profile-driven synthesis enabled (HQ/Fast). SoX source: %1.")
        .arg(soxProbe.source);
}

bool BackendBridge::isSynthesisInProgress() const {
    return m_synthesisInProgress;
}

QString BackendBridge::buildOutputFileName(const QString& profileName) const {
    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");
    return QStringLiteral("tts_%1_%2.wav").arg(profilePrefix(profileName), stamp);
}

void BackendBridge::finishWithError(const QString& error) {
    SynthResult result;
    result.ok = false;
    result.error = error;
    result.profile = m_pendingProfile;
    m_synthesisInProgress = false;
    Q_EMIT synthesisCompleted(result);
}

bool BackendBridge::ensureWorkerStarted() {
    if (m_process) {
        return true;
    }

    const auto paths = PathResolver::resolve();

    m_process = new QProcess(this);
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!m_soxExePath.isEmpty()) {
        const QString soxDir = QFileInfo(m_soxExePath).absolutePath();
        const QString existingPath = env.value("PATH");
        if (existingPath.isEmpty()) {
            env.insert("PATH", soxDir);
        } else {
            env.insert("PATH", soxDir + QDir::listSeparator() + existingPath);
        }
    }
    const QString existingPyPath = env.value("PYTHONPATH");
    if (existingPyPath.isEmpty()) {
        env.insert("PYTHONPATH", paths.backendPackageRoot);
    } else {
        env.insert("PYTHONPATH", paths.backendPackageRoot + ";" + existingPyPath);
    }
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { handleWorkerStdout(); });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrBuffer += QString::fromUtf8(m_process->readAllStandardError());
    });
    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                handleWorkerFinished(exitCode, static_cast<int>(exitStatus));
            });

    const QStringList args = {
        "-m",
        "local_computer_speech_backend.cli",
        "serve",
    };
    m_process->start(paths.backendPythonExe, args);

    if (!m_process->waitForStarted(3000)) {
        std::unique_ptr<QProcess> processGuard(m_process);
        m_process = nullptr;
        finishWithError(QStringLiteral("Failed to start backend worker: %1").arg(processGuard->errorString()));
        return false;
    }

    if (!m_process->waitForReadyRead(3000)) {
        finishWithError("Backend worker did not report readiness.");
        return false;
    }

    handleWorkerStdout();
    return true;
}

void BackendBridge::handleWorkerStdout() {
    if (!m_process) {
        return;
    }

    m_stdoutBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    while (true) {
        if (m_stdoutBuffer.startsWith("READY")) {
            const int nl = m_stdoutBuffer.indexOf('\n');
            if (nl >= 0) {
                m_stdoutBuffer = m_stdoutBuffer.mid(nl + 1);
                continue;
            }
        }

        const QByteArray jsonPayload = extractJsonLine(m_stdoutBuffer);
        if (jsonPayload.isEmpty()) {
            break;
        }

        QJsonParseError parseError{};
        const auto jsonDoc = QJsonDocument::fromJson(jsonPayload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
            finishWithError(
                QStringLiteral("Malformed backend JSON response. stdout: %1 stderr: %2")
                    .arg(QString::fromUtf8(jsonPayload), m_stderrBuffer.trimmed()));
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
        result.profile = obj.value("profile").toString(m_pendingProfile);
        result.error = obj.value("error").toString();

        m_synthesisInProgress = false;

        if (!result.ok) {
            if (result.error.isEmpty()) {
                result.error = "Backend reported synthesis failure with no error message.";
            }
            Q_EMIT synthesisCompleted(result);
            continue;
        }

        const QString expectedPath = result.outputPath.isEmpty() ? m_pendingOutputPath : result.outputPath;
        if (!QFileInfo::exists(QDir::fromNativeSeparators(expectedPath))) {
            result.ok = false;
            result.error = QStringLiteral("Backend reported success but output file is missing: %1")
                               .arg(expectedPath);
            Q_EMIT synthesisCompleted(result);
            continue;
        }

        result.outputPath = QDir::toNativeSeparators(expectedPath);
        Q_EMIT synthesisCompleted(result);
    }
}

void BackendBridge::handleWorkerFinished(int exitCode, int exitStatus) {
    std::unique_ptr<QProcess> processGuard(m_process);
    m_process = nullptr;

    if (m_synthesisInProgress) {
        m_synthesisInProgress = false;
        if (exitStatus != static_cast<int>(QProcess::NormalExit)) {
            finishWithError("Backend worker crashed before completing synthesis.");
            return;
        }
        finishWithError(
            QStringLiteral("Backend worker exited unexpectedly (%1): %2")
                .arg(exitCode)
                .arg(m_stderrBuffer.trimmed()));
    }
}

bool BackendBridge::startSynthesis(const QString& text, const QString& profileName) {
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        finishWithError("Input text is empty. Please type text before synthesizing.");
        return false;
    }

    if (m_synthesisInProgress) {
        finishWithError("Synthesis is already running. Please wait for the current request to finish.");
        return false;
    }

    const auto paths = PathResolver::resolve();
    if (!QFileInfo::exists(paths.backendPythonExe)) {
        finishWithError(
            QStringLiteral("Backend Python executable not found at: %1").arg(paths.backendPythonExe));
        return false;
    }

    const InstallResult soxInstall = m_soxInstaller->ensureSoxAvailable(paths);
    if (!soxInstall.ok) {
        finishWithError(QStringLiteral("SoX bootstrap failed: %1").arg(soxInstall.error));
        return false;
    }
    m_soxExePath = soxInstall.soxExePath;

    QDir().mkpath(paths.outputRoot);
    QDir().mkpath(paths.runtimeRoot);

    const QString requestDir = QDir(paths.runtimeRoot).filePath("requests");
    QDir().mkpath(requestDir);

    const QString requestFilePath =
        QDir(requestDir).filePath(
            QStringLiteral("request_%1_%2.json")
                .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz"))
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    const QString profileConfigName = mapProfileLabelToConfig(profileName);
    const QString outputPath = QDir(paths.outputRoot).filePath(buildOutputFileName(profileConfigName));

    QJsonObject requestObj{
        {"text", trimmedText},
        {"output_path", QDir::toNativeSeparators(outputPath)},
        {"language", "English"},
        {"speaker", "Ryan"},
        {"instruct", ""},
        {"profile", profileConfigName},
    };

    QFile requestFile(requestFilePath);
    if (!requestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishWithError(QStringLiteral("Failed to write request JSON: %1").arg(requestFile.errorString()));
        return false;
    }
    requestFile.write(QJsonDocument(requestObj).toJson(QJsonDocument::Indented));
    requestFile.close();

    if (!ensureWorkerStarted()) {
        return false;
    }

    m_pendingOutputPath = outputPath;
    m_pendingProfile = profileConfigName;
    m_synthesisInProgress = true;

    Q_EMIT synthesisStarted();
    m_process->write((QDir::toNativeSeparators(requestFilePath) + "\n").toUtf8());
    m_process->waitForBytesWritten(1000);

    return true;
}

} // namespace lcs
