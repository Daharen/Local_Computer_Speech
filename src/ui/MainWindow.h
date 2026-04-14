#pragma once

#include <QMainWindow>

class QLabel;

namespace lcs {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void setBackendStatus(const QString& statusText);

private:
    QLabel* m_statusValue;
};

} // namespace lcs
