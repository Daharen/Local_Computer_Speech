#include "core/BackendBridge.h"
#include "core/SoxInstaller.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

namespace lcs {
namespace {

RuntimePaths makePaths(const QString& root) {
    RuntimePaths p;
    p.projectRoot = root;
    p.toolsRoot = root;
    p.repoRoot = QDir(root).filePath("repo");
    p.largeDataRoot = QDir(root).filePath("large");
    p.pythonEnvRoot = QDir(p.largeDataRoot).filePath("python_env");
    p.modelsRoot = QDir(p.largeDataRoot).filePath("models");
    p.logsRoot = QDir(p.largeDataRoot).filePath("logs");
    p.cacheRoot = QDir(p.largeDataRoot).filePath("cache");
    p.runtimeRoot = QDir(p.largeDataRoot).filePath("runtime");
    p.outputRoot = QDir(p.largeDataRoot).filePath("output");
    p.tempRoot = QDir(p.largeDataRoot).filePath("temp");
    p.tokenizerPath = QDir(p.modelsRoot).filePath("qwen/Qwen3-TTS-Tokenizer-12Hz");
    p.modelPath = QDir(p.modelsRoot).filePath("qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice");
    p.fastModelPath = QDir(p.modelsRoot).filePath("qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice");
    p.backendPackageRoot = QDir(p.repoRoot).filePath("backend");
    p.backendCliPath = QDir(p.backendPackageRoot).filePath("local_computer_speech_backend/cli.py");
    p.backendPythonExe = QDir(p.pythonEnvRoot).filePath("Scripts/python.exe");
    return p;
}

void writeExecutableScript(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
    file.close();
    QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                QFile::ReadGroup | QFile::ExeGroup |
                                QFile::ReadOther | QFile::ExeOther));
}

class FakeInstaller final : public SoxInstaller {
public:
    explicit FakeInstaller(const QString& fakeExe) : SoxInstaller(), m_fakeExe(fakeExe) {}

    SoxProbeResult probeExisting(const RuntimePaths&) const override {
        return {};
    }

    InstallResult ensureSoxAvailable(const RuntimePaths&) const override {
        return {true, m_fakeExe, QStringLiteral("managed install (%1)").arg(SoxInstaller::pinnedVersion()), {}};
    }

private:
    QString m_fakeExe;
};

class TestBackendBridge : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void startSynthesisContinuesAfterInstallerSucceeds();
    void nonZeroBackendExitIncludesBackendJsonError();
    void cudaAssertFailureResetsWorkerAndNormalizesError();
};

void TestBackendBridge::startSynthesisContinuesAfterInstallerSucceeds() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    qputenv("LOCAL_COMPUTER_SPEECH_PROJECT_ROOT", paths.projectRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_TOOLS_ROOT", paths.toolsRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_REPO_ROOT", paths.repoRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT", paths.largeDataRoot.toUtf8());

    QDir().mkpath(paths.tokenizerPath);
    QDir().mkpath(paths.modelPath);
    QDir().mkpath(paths.backendPackageRoot);

    const QString fakeSox = QDir(paths.tempRoot).filePath("fake-sox/sox.exe");
    writeExecutableScript(fakeSox, "#!/usr/bin/env bash\nexit 0\n");

    const QByteArray backendScript =
        "#!/usr/bin/env bash\n"
        "echo \"READY\"\n"
        "while IFS= read -r request_file; do\n"
        "  output_path=$(sed -n 's/.*\"output_path\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p' \"$request_file\" | head -n1)\n"
        "  mkdir -p \"$(dirname \"$output_path\")\"\n"
        "  touch \"$output_path\"\n"
        "  echo \"{\\\"ok\\\":true,\\\"output_path\\\":\\\"$output_path\\\",\\\"sample_rate\\\":24000,\\\"speaker\\\":\\\"Ryan\\\",\\\"language\\\":\\\"English\\\",\\\"elapsed_ms\\\":5,\\\"device\\\":\\\"cpu\\\",\\\"profile\\\":\\\"hq_qwen_1_7b_customvoice\\\",\\\"error\\\":\\\"\\\"}\"\n"
        "done\n";
    writeExecutableScript(paths.backendPythonExe, backendScript);

    auto installer = std::make_shared<FakeInstaller>(fakeSox);
    BackendBridge bridge(installer);
    QSignalSpy completedSpy(&bridge, &BackendBridge::synthesisCompleted);

    QVERIFY(bridge.startSynthesis("hello world", "hq"));
    QVERIFY(completedSpy.wait(5000));

    const auto args = completedSpy.takeFirst();
    const SynthResult result = args.at(0).value<SynthResult>();
    QVERIFY(result.ok);
    QVERIFY(QFileInfo::exists(result.outputPath));
}


void TestBackendBridge::nonZeroBackendExitIncludesBackendJsonError() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    qputenv("LOCAL_COMPUTER_SPEECH_PROJECT_ROOT", paths.projectRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_TOOLS_ROOT", paths.toolsRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_REPO_ROOT", paths.repoRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT", paths.largeDataRoot.toUtf8());

    QDir().mkpath(paths.tokenizerPath);
    QDir().mkpath(paths.modelPath);
    QDir().mkpath(paths.backendPackageRoot);

    const QString fakeSox = QDir(paths.tempRoot).filePath("fake-sox/sox.exe");
    writeExecutableScript(fakeSox, "#!/usr/bin/env bash\nexit 0\n");

    const QByteArray backendScript =
        "#!/usr/bin/env bash\n"
        "echo \"READY\"\n"
        "while IFS= read -r _request_file; do\n"
        "  echo \"{\\\"ok\\\":false,\\\"output_path\\\":\\\"\\\",\\\"sample_rate\\\":0,\\\"speaker\\\":\\\"Ryan\\\",\\\"language\\\":\\\"English\\\",\\\"elapsed_ms\\\":1,\\\"device\\\":\\\"\\\",\\\"profile\\\":\\\"hq_qwen_1_7b_customvoice\\\",\\\"error\\\":\\\"Synthesis failed: got an unexpected keyword argument 'dtype'\\\"}\"\n"
        "done\n";
    writeExecutableScript(paths.backendPythonExe, backendScript);

    auto installer = std::make_shared<FakeInstaller>(fakeSox);
    BackendBridge bridge(installer);
    QSignalSpy completedSpy(&bridge, &BackendBridge::synthesisCompleted);

    QVERIFY(bridge.startSynthesis("hello world", "hq"));
    QVERIFY(completedSpy.wait(5000));

    const auto args = completedSpy.takeFirst();
    const SynthResult result = args.at(0).value<SynthResult>();
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("unexpected keyword argument 'dtype'"));
    QVERIFY(result.error.contains("unexpected keyword argument 'dtype'"));
}

void TestBackendBridge::cudaAssertFailureResetsWorkerAndNormalizesError() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const RuntimePaths paths = makePaths(tmp.path());

    qputenv("LOCAL_COMPUTER_SPEECH_PROJECT_ROOT", paths.projectRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_TOOLS_ROOT", paths.toolsRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_REPO_ROOT", paths.repoRoot.toUtf8());
    qputenv("LOCAL_COMPUTER_SPEECH_LARGE_DATA_ROOT", paths.largeDataRoot.toUtf8());

    QDir().mkpath(paths.tokenizerPath);
    QDir().mkpath(paths.modelPath);
    QDir().mkpath(paths.backendPackageRoot);

    const QString fakeSox = QDir(paths.tempRoot).filePath("fake-sox/sox.exe");
    writeExecutableScript(fakeSox, "#!/usr/bin/env bash\nexit 0\n");

    const QString markerPath = QDir(paths.tempRoot).filePath("worker-marker.txt");
    const QByteArray backendScript =
        QByteArray("#!/usr/bin/env bash\n") +
        "echo \"READY\"\n"
        "while IFS= read -r request_file; do\n"
        "  if [ ! -f \"" + markerPath.toUtf8() + "\" ]; then\n"
        "    touch \"" + markerPath.toUtf8() + "\"\n"
        "    echo \"{\\\"ok\\\":false,\\\"output_path\\\":\\\"\\\",\\\"sample_rate\\\":0,\\\"speaker\\\":\\\"Ryan\\\",\\\"language\\\":\\\"English\\\",\\\"elapsed_ms\\\":1,\\\"device\\\":\\\"cuda:0\\\",\\\"profile\\\":\\\"hq_qwen_1_7b_customvoice\\\",\\\"error\\\":\\\"Synthesis failed: CUDA error: device-side assert triggered (TensorCompare.cu Assertion)\\\"}\"\n"
        "  else\n"
        "    output_path=$(sed -n 's/.*\"output_path\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p' \"$request_file\" | head -n1)\n"
        "    mkdir -p \"$(dirname \"$output_path\")\"\n"
        "    touch \"$output_path\"\n"
        "    echo \"{\\\"ok\\\":true,\\\"output_path\\\":\\\"$output_path\\\",\\\"sample_rate\\\":24000,\\\"speaker\\\":\\\"Ryan\\\",\\\"language\\\":\\\"English\\\",\\\"elapsed_ms\\\":5,\\\"device\\\":\\\"cpu\\\",\\\"profile\\\":\\\"hq_qwen_1_7b_customvoice\\\",\\\"error\\\":\\\"\\\"}\"\n"
        "  fi\n"
        "done\n";
    writeExecutableScript(paths.backendPythonExe, backendScript);

    auto installer = std::make_shared<FakeInstaller>(fakeSox);
    BackendBridge bridge(installer);
    QSignalSpy completedSpy(&bridge, &BackendBridge::synthesisCompleted);

    QVERIFY(bridge.startSynthesis("hello world", "hq"));
    QVERIFY(completedSpy.wait(5000));

    SynthResult first = completedSpy.takeFirst().at(0).value<SynthResult>();
    QVERIFY(!first.ok);
    QVERIFY(first.error.contains("CUDA assert"));

    QVERIFY(bridge.startSynthesis("hello again", "hq"));
    QVERIFY(completedSpy.wait(5000));
    SynthResult second = completedSpy.takeFirst().at(0).value<SynthResult>();
    QVERIFY(second.ok);
}

} // namespace
} // namespace lcs

QTEST_MAIN(lcs::TestBackendBridge)
#include "TestBackendBridge.moc"
