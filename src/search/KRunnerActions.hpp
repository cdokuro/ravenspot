#pragma once

#include "SearchResultItem.hpp"
#include <QString>
#include <vector>

namespace ravenspot {

class KRunnerActions {
public:
    KRunnerActions();
    ~KRunnerActions() = default;

    // Search matching triggers in Turkish and English
    [[nodiscard]] std::vector<SearchResultItem> search(const QString &query) const;

    // Direct action execution (D-Bus or system commands)
    static bool executeAction(const QString &actionId, const QString &param = QString());

    // Get the standard 7 power actions for Menu.qml systemGridView
    [[nodiscard]] std::vector<SearchResultItem> getSystemGridActions() const;

    // Process listing for `kill <name>`
    [[nodiscard]] static std::vector<SearchResultItem> listMatchingProcesses(const QString &filter);

    // Terminate process by PID
    static bool killProcess(const QString &pidStr);

    // Turkish diacritic folding helper
    static QString normalizeTurkish(const QString &input);

private:
    struct ActionEntry {
        QString id;
        QString itemId;
        QString label;
        QString detail;
        QString iconGlyph;
        QString iconName;
        QString color;
        QString execCommand;
        QString path;
        QString provider;
        QString value;
        int score{-100};
        std::vector<QString> keywords;
        bool isGridAction{false};
    };

    std::vector<ActionEntry> m_actionRegistry;
    void initRegistry();
};

} // namespace ravenspot
