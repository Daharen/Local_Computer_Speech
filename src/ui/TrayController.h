#pragma once

#include <QObject>

class QMenu;
class QSystemTrayIcon;

namespace lcs {

class MainWindow;

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(MainWindow* window, QObject* parent = nullptr);
    void show();

private:
    MainWindow* m_window;
    QSystemTrayIcon* m_tray;
    QMenu* m_menu;
};

} // namespace lcs
