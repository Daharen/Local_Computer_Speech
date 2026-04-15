#pragma once

#include <QApplication>

namespace lcs {

class BackendBridge;
class MainWindow;
class TrayController;

class Application {
public:
    Application(int& argc, char** argv);
    int run();

private:
    QApplication m_qtApp;
    BackendBridge* m_backendBridge;
    MainWindow* m_mainWindow;
    TrayController* m_trayController;
};

} // namespace lcs
