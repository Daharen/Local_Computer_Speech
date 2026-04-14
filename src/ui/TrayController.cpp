#include "TrayController.h"

#include "MainWindow.h"
#include "core/PathResolver.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QUrl>

namespace lcs {

TrayController::TrayController(MainWindow* window, QObject* parent)
    : QObject(parent), m_window(window), m_tray(new QSystemTrayIcon(this)), m_menu(new QMenu()) {
    m_tray->setIcon(QIcon::fromTheme("audio-volume-high"));
    m_tray->setToolTip("Local Computer Speech");

    auto* launchUi = m_menu->addAction("Launch UI");
    auto* liveRead = m_menu->addAction("Live Read");
    auto* textFileToAudio = m_menu->addAction("Text File To Audio");
    auto* openOutput = m_menu->addAction("Open Output Folder");
    m_menu->addSeparator();
    auto* exit = m_menu->addAction("Exit");

    connect(launchUi, &QAction::triggered, m_window, &QWidget::showNormal);
    connect(launchUi, &QAction::triggered, m_window, &QWidget::raise);
    connect(launchUi, &QAction::triggered, m_window, &QWidget::activateWindow);

    connect(liveRead, &QAction::triggered, this, [this]() {
        QMessageBox::information(m_window,
                                 "Live Read",
                                 "Placeholder: C++ command dispatch exists. Backend synthesis wiring comes next.");
    });

    connect(textFileToAudio, &QAction::triggered, this, [this]() {
        QMessageBox::information(m_window,
                                 "Text File To Audio",
                                 "Placeholder: task queue and file workflow will be implemented in C++.");
    });

    connect(openOutput, &QAction::triggered, this, []() {
        const auto paths = PathResolver::resolve();
        QDir().mkpath(paths.outputRoot);
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.outputRoot));
    });

    connect(exit, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_window->showNormal();
            m_window->raise();
            m_window->activateWindow();
        }
    });
}

void TrayController::show() {
    m_window->show();
    m_tray->show();
}

} // namespace lcs
