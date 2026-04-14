#pragma once

#include <QApplication>

namespace lcs {

class MainWindow;
class TrayController;

class Application {
public:
    Application(int& argc, char** argv);
    int run();

private:
    QApplication m_qtApp;
    MainWindow* m_mainWindow;
    TrayController* m_trayController;
};

} // namespace lcs
