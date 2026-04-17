#pragma once

#include <QMainWindow>

class QLabel;
class QPushButton;
class QTextEdit;

namespace lcs {

class BackendBridge;
struct SynthResult;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(BackendBridge* backendBridge, QWidget* parent = nullptr);
    void setBackendStatus(const QString& statusText);

private:
    void onSynthesisStarted();
    void onSynthesisCompleted(const lcs::SynthResult& result);

    BackendBridge* m_backendBridge;
    QLabel* m_statusValue;
    QTextEdit* m_textInput;
    QPushButton* m_synthButton;
    QLabel* m_synthStateValue;
    QLabel* m_outputPathValue;
};

} // namespace lcs
