#include "MainWindow.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace lcs {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_statusValue(new QLabel("Not evaluated.")) {
    setWindowTitle("Local Computer Speech");
    resize(920, 620);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);

    auto* statusGroup = new QGroupBox("Runtime Status", central);
    auto* statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->addWidget(new QLabel("Backend bridge status:"));
    statusLayout->addWidget(m_statusValue);

    auto* controlsGroup = new QGroupBox("Placeholder Controls", central);
    auto* controlsLayout = new QVBoxLayout(controlsGroup);
    controlsLayout->addWidget(new QLabel("Input text (future live-read and file-to-audio pipeline):"));
    controlsLayout->addWidget(new QTextEdit());

    auto* row = new QHBoxLayout();
    row->addWidget(new QPushButton("Live Read (placeholder)"));
    row->addWidget(new QPushButton("Text File To Audio (placeholder)"));
    row->addWidget(new QPushButton("Open Output Folder (placeholder)"));
    controlsLayout->addLayout(row);

    mainLayout->addWidget(statusGroup);
    mainLayout->addWidget(controlsGroup);
    setCentralWidget(central);
}

void MainWindow::setBackendStatus(const QString& statusText) {
    m_statusValue->setText(statusText);
}

} // namespace lcs
