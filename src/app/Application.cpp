#include "Application.h"

#include "core/BackendBridge.h"
#include "ui/MainWindow.h"
#include "ui/TrayController.h"

namespace lcs {

Application::Application(int& argc, char** argv)
    : m_qtApp(argc, argv),
      m_mainWindow(new MainWindow()),
      m_trayController(new TrayController(m_mainWindow)) {
    m_qtApp.setQuitOnLastWindowClosed(false);

    BackendBridge backend;
    const auto status = backend.quickStatusSummary();
    m_mainWindow->setBackendStatus(status);
}

int Application::run() {
    m_trayController->show();
    return m_qtApp.exec();
}

} // namespace lcs
