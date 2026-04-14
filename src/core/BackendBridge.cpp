#include "BackendBridge.h"

#include "PathResolver.h"

#include <QFileInfo>

namespace lcs {

QString BackendBridge::quickStatusSummary() const {
    const auto paths = PathResolver::resolve();

    const bool hasTokenizer = QFileInfo::exists(paths.tokenizerPath);
    const bool hasModel = QFileInfo::exists(paths.modelPath);

    if (!hasTokenizer || !hasModel) {
        return QStringLiteral(
            "Model assets missing. Run run.ps1 -InstallModel to install tokenizer + model into large-data root.");
    }

    return QStringLiteral(
        "Backend bridge ready: local model/tokenizer paths found in persistent large-data root.");
}

} // namespace lcs
