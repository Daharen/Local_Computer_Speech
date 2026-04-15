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
    BackendBridge* m_backendBridge = nullptr;
    MainWindow* m_mainWindow = nullptr;
    TrayController* m_trayController = nullptr;
};

} // namespace lcs
