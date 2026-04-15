#include "Application.h"

#include "core/BackendBridge.h"
#include "ui/MainWindow.h"
#include "ui/TrayController.h"

namespace lcs {

Application::Application(int& argc, char** argv)
    : m_qtApp(argc, argv),
      m_backendBridge(new BackendBridge(&m_qtApp)),
      m_mainWindow(new MainWindow(m_backendBridge)),
      m_trayController(new TrayController(m_mainWindow)) {
    m_qtApp.setQuitOnLastWindowClosed(false);

    const auto status = m_backendBridge->quickStatusSummary();
    m_mainWindow->setBackendStatus(status);
}

int Application::run() {
    m_trayController->show();
    return m_qtApp.exec();
}

} // namespace lcs
