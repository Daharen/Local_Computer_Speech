#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>

#include <memory>

class QProcess;

namespace lcs {
class SoxInstaller;
}

namespace lcs {

struct SynthResult {
    bool ok = false;
    QString outputPath;
    int sampleRate = 0;
    QString speaker;
    QString language;
    qint64 elapsedMs = 0;
    QString device;
    QString profile;
    QString error;
};

class BackendBridge : public QObject {
    Q_OBJECT

public:
    explicit BackendBridge(QObject* parent = nullptr);
    explicit BackendBridge(std::shared_ptr<SoxInstaller> soxInstaller, QObject* parent = nullptr);

    QString quickStatusSummary() const;
    bool isSynthesisInProgress() const;
    bool startSynthesis(const QString& text, const QString& profileName);

// QT_NO_KEYWORDS is enabled project-wide, so use macro-safe Qt signal syntax.
Q_SIGNALS:
    void synthesisStarted();
    void synthesisCompleted(const lcs::SynthResult& result);

private:
    void finishWithError(const QString& error);
    QString buildOutputFileName(const QString& profileName) const;
    bool ensureWorkerStarted();
    void resetWorkerState();
    void handleWorkerStdout();
    void handleWorkerFinished(int exitCode, int exitStatus);

    std::shared_ptr<SoxInstaller> m_soxInstaller;
    QProcess* m_process;
    QString m_stdoutBuffer;
    QString m_stderrBuffer;
    QString m_pendingOutputPath;
    QString m_pendingProfile;
    QString m_soxExePath;
    bool m_synthesisInProgress = false;
};

} // namespace lcs

Q_DECLARE_METATYPE(lcs::SynthResult)
