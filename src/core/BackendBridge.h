#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>

class QProcess;

namespace lcs {

struct SynthResult {
    bool ok = false;
    QString outputPath;
    int sampleRate = 0;
    QString speaker;
    QString language;
    qint64 elapsedMs = 0;
    QString device;
    QString error;
};

class BackendBridge : public QObject {
    Q_OBJECT

public:
    explicit BackendBridge(QObject* parent = nullptr);

    QString quickStatusSummary() const;
    bool isSynthesisInProgress() const;
    bool startSynthesis(const QString& text);

Q_SIGNALS:
    void synthesisStarted();
    void synthesisCompleted(const lcs::SynthResult& result);

private:
    void finishWithError(const QString& error);
    QString buildOutputFileName() const;

    QProcess* m_process;
    QString m_stdoutBuffer;
    QString m_stderrBuffer;
};

} // namespace lcs

Q_DECLARE_METATYPE(lcs::SynthResult)
