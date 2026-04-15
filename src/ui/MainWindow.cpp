#include "MainWindow.h"

#include "core/BackendBridge.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace lcs {

MainWindow::MainWindow(BackendBridge* backendBridge, QWidget* parent)
    : QMainWindow(parent),
      m_backendBridge(backendBridge),
      m_statusValue(new QLabel("Not evaluated.")),
      m_textInput(new QTextEdit()),
      m_synthButton(new QPushButton("Synthesize WAV")),
      m_synthStateValue(new QLabel("Idle.")),
      m_outputPathValue(new QLabel("(none)")) {
    setWindowTitle("Local Computer Speech");
    resize(920, 620);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);

    auto* statusGroup = new QGroupBox("Runtime Status", central);
    auto* statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->addWidget(new QLabel("Backend bridge status:"));
    statusLayout->addWidget(m_statusValue);

    auto* controlsGroup = new QGroupBox("Synthesis", central);
    auto* controlsLayout = new QVBoxLayout(controlsGroup);
    controlsLayout->addWidget(new QLabel("Input text:"));
    controlsLayout->addWidget(m_textInput);

    auto* row = new QHBoxLayout();
    row->addWidget(m_synthButton);
    row->addStretch();
    controlsLayout->addLayout(row);

    controlsLayout->addWidget(new QLabel("Synthesis status:"));
    controlsLayout->addWidget(m_synthStateValue);
    controlsLayout->addWidget(new QLabel("Last output WAV path:"));
    controlsLayout->addWidget(m_outputPathValue);

    mainLayout->addWidget(statusGroup);
    mainLayout->addWidget(controlsGroup);
    setCentralWidget(central);

    connect(m_synthButton, &QPushButton::clicked, this, [this]() {
        if (!m_backendBridge) {
            QMessageBox::critical(this,
                                  "Synthesis Error",
                                  "Backend bridge is unavailable. Restart the app and try again.");
            return;
        }

        const QString text = m_textInput->toPlainText();
        if (text.trimmed().isEmpty()) {
            QMessageBox::warning(this,
                                 "Empty Input",
                                 "Please type some text in the input box before synthesizing.");
            return;
        }

        if (m_backendBridge->isSynthesisInProgress()) {
            QMessageBox::information(this,
                                     "Synthesis In Progress",
                                     "Synthesis is already running. Please wait for it to complete.");
            return;
        }

        if (!m_backendBridge->startSynthesis(text)) {
            m_synthButton->setEnabled(true);
        }
    });

    connect(m_backendBridge, &BackendBridge::synthesisStarted, this, [this]() { onSynthesisStarted(); });
    connect(m_backendBridge,
            &BackendBridge::synthesisCompleted,
            this,
            [this](const lcs::SynthResult& result) { onSynthesisCompleted(result); });
}

void MainWindow::setBackendStatus(const QString& statusText) {
    m_statusValue->setText(statusText);
}

void MainWindow::onSynthesisStarted() {
    m_synthButton->setEnabled(false);
    m_synthStateValue->setText("Synthesizing...");
}

void MainWindow::onSynthesisCompleted(const lcs::SynthResult& result) {
    m_synthButton->setEnabled(true);

    if (result.ok) {
        m_synthStateValue->setText(
            QStringLiteral("Success. %1 Hz on %2 (%3 ms)")
                .arg(result.sampleRate)
                .arg(result.device.isEmpty() ? QStringLiteral("unknown device") : result.device)
                .arg(result.elapsedMs));
        m_outputPathValue->setText(result.outputPath);
        QMessageBox::information(this,
                                 "Synthesis Complete",
                                 QStringLiteral("WAV created successfully:\n%1").arg(result.outputPath));
        return;
    }

    m_synthStateValue->setText("Failed.");
    if (!result.outputPath.isEmpty()) {
        m_outputPathValue->setText(result.outputPath);
    }

    QMessageBox::critical(
        this,
        "Synthesis Failed",
        QStringLiteral("Synthesis failed:\n%1").arg(result.error.isEmpty() ? "Unknown error" : result.error));
}

} // namespace lcs
